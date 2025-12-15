#include "audiomixer.h"
#include <cstdio>

AudioMixer::AudioMixer() {}

AudioMixer::~AudioMixer() {
    if (graph_) {
        avfilter_graph_free(&graph_);
    }
}

int AudioMixer::addInput(int sample_rate, int channels, AVSampleFormat fmt) {
    InputContext info;
    info.sample_rate = sample_rate;
    info.channels = channels;
    info.fmt = fmt;
    info.time_base = {1, sample_rate};
    inputs_.push_back(info);
    return inputs_.size() - 1;
}

int AudioMixer::setOutput(int sample_rate, int channels, AVSampleFormat fmt) {
    out_sample_rate_ = sample_rate;
    out_channels_ = channels;
    out_fmt_ = fmt;
    return 0;
}

int AudioMixer::init(const char* duration_mode) {
    if (inputs_.empty()) return -1;

    graph_ = avfilter_graph_alloc();
    if (!graph_) return -1;

    int ret = 0;
    char args[512];

    // 1. 创建 amix (混音) 过滤器
    const AVFilter* amix = avfilter_get_by_name("amix");
    AVFilterContext* amix_ctx = nullptr;

    // inputs: 输入流数量
    // duration: 结束策略
    // dropout_transition: 当一路音频结束时，音量恢复的过渡时间(秒)
    snprintf(args, sizeof(args), "inputs=%zu:duration=%s:dropout_transition=0",
             inputs_.size(), duration_mode);

    ret = avfilter_graph_create_filter(&amix_ctx, amix, "amix_node", args, nullptr, graph_);
    if (ret < 0) {
        printf("Error creating amix filter\n");
        return ret;
    }

    // 2. 创建 sink (输出) 过滤器
    const AVFilter* abuffersink = avfilter_get_by_name("abuffersink");
    ret = avfilter_graph_create_filter(&sink_ctx_, abuffersink, "sink_node", nullptr, nullptr, graph_);
    if (ret < 0) return ret;

    // 设置 sink 的输出格式 (强制转换)
    // 使用新的声道布局API
    AVChannelLayout ch_layout;
    av_channel_layout_default(&ch_layout, out_channels_);
    char ch_layout_str[64];
    av_channel_layout_describe(&ch_layout, ch_layout_str, sizeof(ch_layout_str));
    av_channel_layout_uninit(&ch_layout);

    // 强制 sink 输出我们指定的格式，否则它可能输出 planar 格式，导致 fwrite 写入麻烦
    int64_t sample_fmts[] = {out_fmt_, -1};
    ret = av_opt_set_int_list(sink_ctx_, "sample_fmts", sample_fmts, -1, AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) return ret;
    
    int sample_rates[] = {out_sample_rate_, -1};
    ret = av_opt_set_int_list(sink_ctx_, "sample_rates", sample_rates, -1, AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) return ret;
    
    // 3. 创建 inputs 并链接到 amix
    for (size_t i = 0; i < inputs_.size(); ++i) {
        const AVFilter* abuffer = avfilter_get_by_name("abuffer");
        std::string name = "input_" + std::to_string(i);

        // 构建参数字符串
        // sample_rate, sample_fmt, channel_layout 是 abuffer 必须的参数
        AVChannelLayout in_ch_layout;
        av_channel_layout_default(&in_ch_layout, inputs_[i].channels);
        char in_ch_layout_str[64];
        av_channel_layout_describe(&in_ch_layout, in_ch_layout_str, sizeof(in_ch_layout_str));
        av_channel_layout_uninit(&in_ch_layout);

        snprintf(args, sizeof(args),
                "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=%s",
                 inputs_[i].time_base.num, inputs_[i].time_base.den,
                 inputs_[i].sample_rate,
                 av_get_sample_fmt_name(inputs_[i].fmt),
                 in_ch_layout_str);

        ret = avfilter_graph_create_filter(&inputs_[i].ctx, abuffer, name.c_str(), args, nullptr, graph_);
        if (ret < 0) {
            printf("Error creating source filter %zu\n", i);
            return ret;
        }

        // 链接: input[i] -> amix[pad i]
        ret = avfilter_link(inputs_[i].ctx, 0, amix_ctx, i);
        if (ret < 0) return ret;
    }

    // 4. 链接 amix -> sink
    // 为了确保格式统一，最好在 amix 和 sink 之间加一个 aformat 过滤器，
    // 这里为了代码简短，直接链接，依赖 sink 的协商能力
    ret = avfilter_link(amix_ctx, 0, sink_ctx_, 0);
    if (ret < 0) return ret;

    // 5. 配置图
    ret = avfilter_graph_config(graph_, nullptr);
    if (ret < 0) {
        printf("Error configuring graph\n");
        return ret;
    }

    initialized_ = true;
    return 0;
}

int AudioMixer::sendFrame(int index, const uint8_t* data, int size) {
    if (!initialized_ || index >= inputs_.size()) return -1;

    std::lock_guard<std::mutex> lock(mutex_);
    InputContext& ctx = inputs_[index];

    if (data == nullptr || size == 0) {
        // 发送 NULL 表示 EOF (冲刷)
        return av_buffersrc_add_frame(ctx.ctx, nullptr);
    }

    // 分配 AVFrame
    AVFrame* frame = av_frame_alloc();
    frame->sample_rate = ctx.sample_rate;
    frame->format = ctx.fmt;
    // 使用新的声道布局API
    av_channel_layout_default(&frame->ch_layout, ctx.channels);

    // 计算样本数 = 总字节 / (通道数 * 每个样本的字节数)
    int bytes_per_sample = av_get_bytes_per_sample(ctx.fmt);
    frame->nb_samples = size / (ctx.channels * bytes_per_sample);

    // 关键：设置 PTS (Presentation Time Stamp)
    frame->pts = ctx.next_pts;
    ctx.next_pts += frame->nb_samples;

    // 填充数据
    av_frame_get_buffer(frame, 0);
    // 这里假设输入是 packed 格式（非 planar），通常 PCM 文件都是 packed
    memcpy(frame->data[0], data, size);

    int ret = av_buffersrc_add_frame(ctx.ctx, frame);
    av_frame_free(&frame); // av_buffersrc_add_frame 会引用或拷贝，所以这里可以释放

    return ret;
}

int AudioMixer::receiveFrame(uint8_t* out_buf, int max_size) {
    if (!initialized_) return -1;

    std::lock_guard<std::mutex> lock(mutex_);

    AVFrame* frame = av_frame_alloc();
    int ret = av_buffersink_get_frame(sink_ctx_, frame);

    int read_size = 0;
    if (ret >= 0) {
        // 计算大小
        int data_size = av_samples_get_buffer_size(nullptr, frame->ch_layout.nb_channels, frame->nb_samples, (AVSampleFormat)frame->format, 1);

        if (data_size <= max_size) {
            memcpy(out_buf, frame->data[0], data_size);
            read_size = data_size;
        } else {
            // 缓冲区太小，简单起见这里丢弃或仅拷贝部分，实际应处理剩余
            memcpy(out_buf, frame->data[0], max_size);
            read_size = max_size;
        }
    } else {
        if (ret == AVERROR(EAGAIN)) read_size = 0; // 需要更多输入
        else if (ret == AVERROR_EOF) read_size = -1; // 结束
        else read_size = -1; // 错误
    }

    av_frame_free(&frame);
    return read_size;
}