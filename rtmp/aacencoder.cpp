#include "aacencoder.h"
#include "dlog.h"
// 包含这个头文件以使用新的声道布局 API
#include <libavutil/channel_layout.h>

/**
 * @brief AACEncoder::AACEncoder
 * @param properties
 * key value
 * sample_fmt 默认AV_SAMPLE_FMT_FLTP
 * samplerate 默认 48000
 * ch_layout  默认AV_CH_LAYOUT_STEREO
 * bitrate    默认out_samplerate*3
 */
AACEncoder::AACEncoder() = default;

AACEncoder::~AACEncoder()
{
    if (ctx_)
        avcodec_free_context(&ctx_);

    if (frame_)
        av_frame_free(&frame_);
}

RET_CODE AACEncoder::Init(const Properties& properties)
{
    // 获取参数
    sample_rate_ = properties.GetProperty("sample_rate", 48000);
    bitrate_ = properties.GetProperty("bitrate", 128 * 1024);
    channels_ = properties.GetProperty("channels", 2);

    int ret;

    type_ = AudioCodec::AAC;
    // 读取默认的aac encoder
    // 注意：请确保你的 aacencoder.h 文件中 codec_ 成员变量是 const AVCodec*
    codec_ = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!codec_)
    {
        LogError("AAC: No encoder found\n");
        return RET_ERR_MISMATCH_CODE;
    }

    ctx_ = avcodec_alloc_context3(codec_);
    if (ctx_ == nullptr)
    {
        LogError("AAC: could not allocate context.\n");
        return RET_ERR_OUTOFMEMORY;
    }

    av_channel_layout_default(&ctx_->ch_layout, channels_);

    ctx_->sample_fmt = AV_SAMPLE_FMT_FLTP;
    ctx_->sample_rate = sample_rate_;
    ctx_->bit_rate = bitrate_;
    ctx_->thread_count = 1;

    ctx_->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;

    if (avcodec_open2(ctx_, codec_, nullptr) < 0)
    {
        LogError("AAC: could not open codec\n");
        return RET_FAIL;
    }


    // 每个样本字节数*通道数*采样数
    frame_byte_size_ = av_get_bytes_per_sample(ctx_->sample_fmt) //每个采样占用的字节数
        * ctx_->ch_layout.nb_channels * ctx_->frame_size;
    //Create frame
    frame_ = av_frame_alloc();
    //Set defaults
    frame_->nb_samples = ctx_->frame_size;
    frame_->format = ctx_->sample_fmt;
    frame_->sample_rate = ctx_->sample_rate;

    av_channel_layout_copy(&frame_->ch_layout, &ctx_->ch_layout);;

    //分配data buf
    ret = av_frame_get_buffer(frame_, 0);

    //Log
    LogInfo("AAC: Encoder open with frame sample size %d.\n", ctx_->frame_size);

    return RET_OK;
}


/**
 * @brief 旧的 Encode 函数，使用 avcodec_encode_audio2
 * * 已重构为使用 send/receive API，以模拟旧的 1:1 行为。
 * 这是导致你编译失败的 "avcodec_encode_audio2" 错误的地方。
 */
int AACEncoder::Encode(AVFrame* frame, uint8_t* out, int out_len)
{
    if (!frame)
        return 0;

    if (ctx_ == nullptr)
    {
        LogError("AAC: no context.\n");
        return -1;
    }

    // --- 新 API：发送帧 ---
    int ret = avcodec_send_frame(ctx_, frame);
    if (ret < 0)
    {
        LogError("AAC: avcodec_send_frame failed (%d)\n", ret);
        return -1;
    }

    // --- 新 API：接收包 ---
    // 必须在栈上创建一个新 packet，或者 av_packet_alloc
    AVPacket pkt = {nullptr}; // 初始化为 0

    // 旧 API (已废弃)
    // av_init_packet(&pkt);
    // pkt.data = out; // 错误！不能将用户缓冲区直接交给 receive_packet
    // pkt.size = out_len;

    ret = avcodec_receive_packet(ctx_, &pkt);

    // 检查我们是否得到了输出
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
    {
        // 这相当于旧 API 的 "got_output = 0"
        LogError("AAC: could not get output packet (EAGAIN/EOF)\n");
        av_packet_unref(&pkt); // 即使失败也要 unref
        return -1; // 匹配旧逻辑
    }
    if (ret < 0)
    {
        // 发生了真正的错误
        LogError("AAC: could not encode audio frame (receive failed %d)\n", ret);
        av_packet_unref(&pkt);
        return -1;
    }

    // 成功 (ret == 0)，这相当于 "got_output = 1"

    if (pkt.size > out_len)
    {
        LogError("AAC: output buffer is too small (need %d, have %d)\n", pkt.size, out_len);
        av_packet_unref(&pkt);
        return -1;
    }

    // 将数据从 packet 复制到用户的 'out' 缓冲区
    memcpy(out, pkt.data, pkt.size);
    int encoded_size = pkt.size;

    // 释放 packet
    av_packet_unref(&pkt);

    //Return encoded size
    return encoded_size;
}


// ---
// 以下函数 (Encode, EncodeInput, EncodeOutput)
// 已经在使用新的 send/receive API，它们是正确的。
// 我只修复了它们内部的 'channel' 和 'packet' API 废弃问题。
// ---


AVPacket* AACEncoder::Encode(AVFrame* frame, int64_t pts, const int flush)
{
    AVPacket* packet = nullptr;
    int ret = 0;
    AVRational src_time_base = AVRational{1, 1000};
    frame->pts = pts;

    if (ctx_ == nullptr)
    {
        LogError("AAC: no context.\n");
        return nullptr;
    }
    if (frame)
    {
        int ret = avcodec_send_frame(ctx_, frame);
        if (ret != 0)
        {
            LogError("avcodec_send_frame failed");
            return nullptr;
        }
    }

    packet = av_packet_alloc();
    ret = avcodec_receive_packet(ctx_, packet);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
    {
        av_packet_free(&packet); // 必须释放分配的包
        return nullptr;
    }
    else if (ret < 0)
    {
        LogError("avcodec_receive_packet() failed.");
        av_packet_free(&packet); // 必须释放分配的包
        return nullptr;
    }
    //    LogInfo("apts1:%ld", frame_->pts);
    //    LogInfo("apts2:%ld", packet->pts);
    return packet;
}

/**
 * @brief 每次需要输入足够进行一次编码的数据
 * @param in
 * @param size
 * @return
 */
RET_CODE AACEncoder::EncodeInput(const uint8_t* in, const uint32_t size)
{
    RET_CODE ret_code = RET_OK;
    if (in)
    {
        // 新 API：使用 ch_layout.nb_channels
        uint32_t need_size = av_get_bytes_per_sample(ctx_->sample_fmt) * ctx_->ch_layout.nb_channels *
            ctx_->frame_size;
        // 旧 API (已废弃)
        //        uint32_t need_size = av_get_bytes_per_sample(ctx_->sample_fmt) * ctx_->channels *
        //                ctx_->frame_size;

        if (size != need_size)
        {
            LogError("need size:%u, but the size:%u", need_size, size);
            return RET_ERR_PARAMISMATCH;
        }
        AVFrame* frame = av_frame_alloc();
        frame->nb_samples = ctx_->frame_size;
        frame->format = ctx_->sample_fmt;
        av_channel_layout_copy(&frame->ch_layout, &ctx_->ch_layout);

        // 新 API：使用 av_samples_fill_arrays 来设置帧数据指针
        // 这假设 'in' 缓冲区的数据布局与 planar FLTP 兼容
        av_samples_fill_arrays(frame->data, frame->linesize, in, ctx_->ch_layout.nb_channels,
                               frame->nb_samples, ctx_->sample_fmt, 0);

        // 旧 API (已废弃)
        // avcodec_fill_audio_frame(frame, ctx_->channels, ctx_->sample_fmt, in, size, 0);

        ret_code = EncodeInput(frame);
        av_frame_free(&frame); // 释放自己申请的数据
        return ret_code;
    }
    else // 为null时flush编码器
    {
        return EncodeInput(nullptr);
    }
}

RET_CODE AACEncoder::EncodeInput(const AVFrame* frame)
{
    int ret = avcodec_send_frame(ctx_, frame);
    if (ret != 0)
    {
        if (AVERROR(EAGAIN) == ret)
        {
            LogWarn("please receive packet then send frame");
            return RET_ERR_EAGAIN;
        }
        else if (AVERROR_EOF == ret)
        {
            LogWarn("if you wan continue use it, please new one decoder again");
        }
        return RET_FAIL;
    }
    return RET_OK;
}

RET_CODE AACEncoder::EncodeOutput(AVPacket* pkt)
{
    int ret = avcodec_receive_packet(ctx_, pkt);
    if (ret != 0)
    {
        if (AVERROR(EAGAIN) == ret)
        {
            LogWarn("output is not available in the current state - user must try to send input");
            return RET_ERR_EAGAIN;
        }
        else if (AVERROR_EOF == ret)
        {
            LogWarn("the encoder has been fully flushed, and there will be no more output packets");
            return RET_ERR_EOF;
        }
        return RET_FAIL;
    }
    return RET_OK;
}

RET_CODE AACEncoder::EncodeOutput(uint8_t* out, uint32_t& size)
{
    // 新 API：在栈上创建 AVPacket
    AVPacket pkt = {0};

    // 旧 API (已废弃且危险)
    // av_init_packet(&pkt);
    // pkt.data = out;
    // pkt.size = size;

    RET_CODE ret = EncodeOutput(&pkt); // 此函数将为 pkt.data 分配内存
    if (ret == RET_OK)
    {
        if (pkt.size > (int)size)
        {
            LogError("AAC Output buffer too small. Need %d, have %u", pkt.size, size);
            size = 0; // 报告 0 字节写入
            ret = RET_ERR_PARAMISMATCH;
        }
        else
        {
            // 将数据从 packet 复制到用户的 'out' 缓冲区
            memcpy(out, pkt.data, pkt.size);
            size = pkt.size; // 实际编码后的数据长度
        }
    }
    else
    {
        size = 0; // 失败时确保大小为 0
    }

    av_packet_unref(&pkt); // 必须 unref 以释放 pkt.data
    return ret;
}
