#include "AudioEncoder.h"

// 包含你的 ff_err2str 辅助函数
static inline std::string ff_err2str(int errnum) {
    char buf[256];
    av_strerror(errnum, buf, sizeof(buf));
    return {buf};
}

AudioEncoder::AudioEncoder() {

}

AudioEncoder::~AudioEncoder() {
    stop();
}

bool AudioEncoder::init(int in_sample_rate, AVSampleFormat in_sample_fmt, int64_t in_ch_layout,
                        int out_sample_rate, int out_channels, int out_bit_rate) {
    const AVCodec* codec = avcodec_find_encoder_by_name("aac");
    if (!codec) {
        std::cerr << "[AudioEncoder] Error: aac encoder not found." << std::endl;
        return false;
    }

    codec_ctx_ = avcodec_alloc_context3(codec);
    if (!codec_ctx_) {
        std::cerr << "[AudioEncoder] Error: avcodec_alloc_context3 failed." << std::endl;
        return false;
    }

    // 设置编码器参数
    codec_ctx_->sample_rate = out_sample_rate;
    // 使用新的声道布局API
    av_channel_layout_default(&codec_ctx_->ch_layout, out_channels);
    codec_ctx_->sample_fmt = AV_SAMPLE_FMT_FLTP; // AAC 偏好 FLTP
    codec_ctx_->bit_rate = out_bit_rate;
    codec_ctx_->time_base = {1, out_sample_rate};

    // 打开编码器
    int ret = avcodec_open2(codec_ctx_, codec, nullptr);
    if (ret < 0) {
        std::cerr << "[AudioEncoder] Error: avcodec_open2 failed: " << ff_err2str(ret) << std::endl;
        avcodec_free_context(&codec_ctx_);
        return false;
    }

    // 初始化重采样器 (SWR)
    swr_ctx_ = swr_alloc();
    if (!swr_ctx_) {
        std::cerr << "[AudioEncoder] Error: swr_alloc failed." << std::endl;
        stop();
        return false;
    }

    // 设置输入参数 (来自 AudioCapture)
    av_opt_set_int(swr_ctx_, "in_sample_rate", in_sample_rate, 0);
    av_opt_set_sample_fmt(swr_ctx_, "in_sample_fmt", in_sample_fmt, 0);
    // 使用新的声道布局API
    AVChannelLayout in_chlayout;
    av_channel_layout_from_mask(&in_chlayout, in_ch_layout);
    av_opt_set_chlayout(swr_ctx_, "in_chlayout", &in_chlayout, 0);
    
    // 设置输出参数 (匹配编码器)
    av_opt_set_int(swr_ctx_, "out_sample_rate", codec_ctx_->sample_rate, 0);
    av_opt_set_sample_fmt(swr_ctx_, "out_sample_fmt", codec_ctx_->sample_fmt, 0);
    av_opt_set_chlayout(swr_ctx_, "out_chlayout", &codec_ctx_->ch_layout, 0);

    ret = swr_init(swr_ctx_);
    if (ret < 0) {
        std::cerr << "[AudioEncoder] Error: swr_init failed: " << ff_err2str(ret) << std::endl;
        stop();
        return false;
    }

    // 初始化 FIFO (用于缓存重采样后的数据)
    // 估算编码器偏好的帧大小 (例如 AAC 通常喜欢 1024 个采样点)
    int encoder_frame_size = codec_ctx_->frame_size ? codec_ctx_->frame_size : 1024;
    // 分配 FIFO，大小至少是 4 倍编码帧大小，防止溢出
    fifo_ = av_audio_fifo_alloc(codec_ctx_->sample_fmt, codec_ctx_->ch_layout.nb_channels, encoder_frame_size * 4);
    if (!fifo_) {
        std::cerr << "[AudioEncoder] Error: av_audio_fifo_alloc failed." << std::endl;
        stop();
        return false;
    }

    // 分配重采样输出帧 (我们将把 SWR 输出写入这个帧，然后送给编码器)
    resampled_frame_ = av_frame_alloc();
    if (!resampled_frame_) {
        std::cerr << "[AudioEncoder] Error: av_frame_alloc failed." << std::endl;
        stop();
        return false;
    }
    resampled_frame_->format = codec_ctx_->sample_fmt;
    // 使用新的声道布局API
    av_channel_layout_copy(&resampled_frame_->ch_layout, &codec_ctx_->ch_layout);
    resampled_frame_->sample_rate = codec_ctx_->sample_rate;
    // 注意：nb_samples 会在每次重采样时设置

    // 分配重采样输出缓冲区 (绑定到 resampled_frame_)
    // 先估算一个足够大的缓冲区 (例如 4096 个采样点)
    max_resampled_samples_ = 4096;
    ret = av_samples_alloc_array_and_samples(&resampled_data_, nullptr, codec_ctx_->ch_layout.nb_channels,
                                             max_resampled_samples_, codec_ctx_->sample_fmt, 0);
    if (ret < 0) {
        std::cerr << "[AudioEncoder] Error: av_samples_alloc_array_and_samples failed: " << ff_err2str(ret) << std::endl;
        stop();
        return false;
    }
    // 把缓冲区绑定到 frame
    resampled_frame_->data[0] = resampled_data_[0];
    if (codec_ctx_->ch_layout.nb_channels > 1) {
        resampled_frame_->data[1] = resampled_data_[0] + av_get_bytes_per_sample(codec_ctx_->sample_fmt) * max_resampled_samples_;
    }

    // 填充 CodecParameters (用于 Muxer)
    codec_par_ = avcodec_parameters_alloc();
    if (!codec_par_) {
        std::cerr << "[AudioEncoder] Error: avcodec_parameters_alloc failed." << std::endl;
        stop();
        return false;
    }
    ret = avcodec_parameters_from_context(codec_par_, codec_ctx_);
    if (ret < 0) {
        std::cerr << "[AudioEncoder] Error: avcodec_parameters_from_context failed: " << ff_err2str(ret) << std::endl;
        stop();
        return false;
    }

    std::cout << "[INFO] AudioEncoder initialized (in: " << in_sample_rate << "Hz/"
              << av_get_sample_fmt_name(in_sample_fmt) << "/" << in_ch_layout
              << " -> out: " << out_sample_rate << "Hz/"
              << av_get_sample_fmt_name(codec_ctx_->sample_fmt) << "/"
              << codec_ctx_->ch_layout.nb_channels << "ch)." << std::endl;
    return true;
}

bool AudioEncoder::encodeFrame(AVFrame* pcm_frame) {
    if (!swr_ctx_ || !fifo_) return false;

    // 1. --- 重采样 ---
    // 计算重采样后会产生多少样本
    int dst_nb_samples = av_rescale_rnd(
            swr_get_delay(swr_ctx_, pcm_frame->sample_rate) + pcm_frame->nb_samples,
            codec_ctx_->sample_rate,
            pcm_frame->sample_rate,
            AV_ROUND_UP
    );

    if (dst_nb_samples > max_resampled_samples_) {
        std::cerr << "[AudioEncoder] Resampled samples exceed buffer size." << std::endl;
        // (需要动态重新分配 resampled_data_，这里为简化暂不处理)
        return false;
    }

    // 执行重采样
    int converted_nb_samples = swr_convert(
            swr_ctx_,
            resampled_data_,      // 输出
            dst_nb_samples,
            (const uint8_t**)pcm_frame->data, // 输入
            pcm_frame->nb_samples
    );
    if (converted_nb_samples < 0) {
        std::cerr << "[AudioEncoder] Error: swr_convert failed." << std::endl;
        return false;
    }

    // 2. --- 写入 FIFO ---
    if (converted_nb_samples > 0) {
        int written = av_audio_fifo_write(fifo_, (void**)resampled_data_, converted_nb_samples);
        if (written < converted_nb_samples) {
            std::cerr << "[AudioEncoder] Warning: FIFO buffer overflow." << std::endl;
        }
    }

    // 3. --- 从 FIFO 读取并编码 ---
    return pushToFifoAndEncode();
}

bool AudioEncoder::pushToFifoAndEncode() {
    if (!fifo_ || !codec_ctx_) return false;

    const int frame_size = codec_ctx_->frame_size;

    // 检查 FIFO 中是否有足够数据（一个编码帧）
    while (av_audio_fifo_size(fifo_) >= frame_size) {

        // 1. 从 FIFO 读取 1024 个样本
        int read = av_audio_fifo_read(fifo_, (void**)resampled_frame_->data, frame_size);
        if (read < frame_size) {
            std::cerr << "[AudioEncoder] Error: av_audio_fifo_read failed." << std::endl;
            return false;
        }

        // 2. 设置 PTS
        // PTS 单位是 time_base (1 / sample_rate)
        resampled_frame_->pts = next_pts_;
        next_pts_ += frame_size; // PTS 增加 1024

        // 3. 编码
        if (!sendAndReceive(resampled_frame_)) {
            return false;
        }
    }
    return true;
}

bool AudioEncoder::sendAndReceive(AVFrame* frame) {
    // 发送帧 (frame=nullptr 时用于 flush)
    int ret = avcodec_send_frame(codec_ctx_, frame);
    if (ret < 0) {
        std::cerr << "[AudioEncoder] Error: avcodec_send_frame failed: " << ff_err2str(ret) << std::endl;
        return false;
    }

    // 循环接收 Packet
    while (ret >= 0) {
        AVPacket* packet = av_packet_alloc();
        if (!packet) return false;

        ret = avcodec_receive_packet(codec_ctx_, packet);

        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            av_packet_free(&packet);
            break;
        } else if (ret < 0) {
            av_packet_free(&packet);
            std::cerr << "[AudioEncoder] Error: avcodec_receive_packet failed: " << ff_err2str(ret) << std::endl;
            break;
        }

        // 成功，通过回调发送
        if (callback_) {
            // packet 的 pts 已经由编码器根据 time_base 设置好了
            callback_(packet);
        } else {
            av_packet_free(&packet);
        }
    }
    return true;
}

void AudioEncoder::flush() {
    if (!codec_ctx_ || !fifo_) return;

    std::cout << "[INFO] Flushing AudioEncoder..." << std::endl;

    // 1. 处理 FIFO 中剩余的不足 1024 的样本
    int remaining = av_audio_fifo_size(fifo_);
    if (remaining > 0) {
        // (注意：理想情况下应该补齐静音到 1024，但为简单起见，我们直接读取)
        // (如果编码器允许，也可以发送 < 1024 的帧)

        // 我们这里选择补齐静音 (如果 av_frame_make_writable 确保了)
        // 简化：我们直接读取剩余的
        if (av_audio_fifo_read(fifo_, (void**)resampled_frame_->data, remaining) == remaining) {
            resampled_frame_->nb_samples = remaining; // 关键：告诉编码器这帧不完整
            resampled_frame_->pts = next_pts_;
            // next_pts_ += remaining; // (flush 后不需要再更新)

            // 清理 buffer 的剩余部分 (防止上次的残留数据)
            int data_size = av_get_bytes_per_sample(codec_ctx_->sample_fmt);
            for(int i=0; i < codec_ctx_->channels; ++i) {
                memset(resampled_frame_->data[i] + remaining * data_size, 0, (codec_ctx_->frame_size - remaining) * data_size);
            }
            resampled_frame_->nb_samples = codec_ctx_->frame_size; // 依然发送 1024

            sendAndReceive(resampled_frame_);
        }
    }

    // 2. 发送 nullptr 信号
    sendAndReceive(nullptr);
}

void AudioEncoder::stop() {
    flush();

    if (fifo_) {
        av_audio_fifo_free(fifo_);
        fifo_ = nullptr;
    }
    if (resampled_data_) {
        av_freep(&resampled_data_[0]);
        av_freep(&resampled_data_);
        resampled_data_ = nullptr;
    }
    if (resampled_frame_) {
        av_frame_free(&resampled_frame_);
        resampled_frame_ = nullptr;
    }
    if (swr_ctx_) {
        swr_free(&swr_ctx_);
        swr_ctx_ = nullptr;
    }
    if (codec_par_) {
        avcodec_parameters_free(&codec_par_);
        codec_par_ = nullptr;
    }
    if (codec_ctx_) {
        avcodec_free_context(&codec_ctx_);
        codec_ctx_ = nullptr;
    }
}