#include "VideoEncoder.h"

// 包含你的 ff_err2str 辅助函数 (或者在这里重新定义)
static inline std::string ff_err2str(int errnum) {
    char buf[256];
    av_strerror(errnum, buf, sizeof(buf));
    return {buf};
}

VideoEncoder::VideoEncoder() {
    // 构造函数
}

VideoEncoder::~VideoEncoder() {
    stop();
}

bool VideoEncoder::init(int width, int height, int fps, int bit_rate) {
    frame_width_ = width;
    frame_height_ = height;
    next_pts_ = 0;

    const AVCodec* codec = avcodec_find_encoder_by_name("libx264");
    if (!codec) {
        std::cerr << "[Encoder] Error: Codec libx264 not found." << std::endl;
        return false;
    }

    codec_ctx_ = avcodec_alloc_context3(codec);
    if (!codec_ctx_) {
        std::cerr << "[Encoder] Error: avcodec_alloc_context3 failed." << std::endl;
        return false;
    }

    // --- 设置编码器参数 ---
    codec_ctx_->width = width;
    codec_ctx_->height = height;
    codec_ctx_->bit_rate = bit_rate;
    // time_base (时间基): 1/fps。PTS 将以此为单位递增。
    codec_ctx_->time_base = {1, fps};
    codec_ctx_->framerate = {fps, 1};
    // gop_size: 关键帧间隔。设置短一点（如 2*fps）对直播延迟有好处。
    codec_ctx_->gop_size = fps;
    codec_ctx_->max_b_frames = 0; // B 帧会增加延迟，直播流设为 0
    // pix_fmt: libx264 偏好 YUV420P
    codec_ctx_->pix_fmt = AV_PIX_FMT_YUV420P;

    // --- 设置 x264 特有参数 ---
    // "preset": veryfast, superfast, ultrafast... (速度越快，压缩率越低)
    av_opt_set(codec_ctx_->priv_data, "preset", "ultrafast", 0);
    // "tune": zerolatency (零延迟，对直播非常重要)
    av_opt_set(codec_ctx_->priv_data, "tune", "zerolatency", 0);
    // 添加profile约束以确保兼容性
    av_opt_set(codec_ctx_->priv_data, "profile", "baseline", 0);
    // 强制每个关键帧都包含SPS/PPS
    av_opt_set(codec_ctx_->priv_data, "repeat-headers", "1", 0);

    int ret = avcodec_open2(codec_ctx_, codec, nullptr);
    if (ret < 0) {
        std::cerr << "[Encoder] Error: avcodec_open2 failed: " << ff_err2str(ret) << std::endl;
        avcodec_free_context(&codec_ctx_);
        return false;
    }

    // --- 初始化格式转换器 (SWS) ---
    // 因为采集是 NV12，而编码需要 YUV420P
    sws_ctx_ = sws_getContext(
            width, height, AV_PIX_FMT_NV12,      // 输入 (源)
            width, height, AV_PIX_FMT_YUV420P,   // 输出 (目标)
            SWS_BICUBIC, nullptr, nullptr, nullptr
    );
    if (!sws_ctx_) {
        std::cerr << "[Encoder] Error: sws_getContext failed." << std::endl;
        stop(); // 清理已分配的 codec_ctx_
        return false;
    }

    // --- 分配用于转换的 YUV420P 帧 ---
    yuv420p_frame_ = av_frame_alloc();
    if (!yuv420p_frame_) {
        std::cerr << "[Encoder] Error: av_frame_alloc failed." << std::endl;
        stop();
        return false;
    }
    yuv420p_frame_->format = AV_PIX_FMT_YUV420P;
    yuv420p_frame_->width = width;
    yuv420p_frame_->height = height;

    ret = av_frame_get_buffer(yuv420p_frame_, 0);
    if (ret < 0) {
        std::cerr << "[Encoder] Error: av_frame_get_buffer failed: " << ff_err2str(ret) << std::endl;
        stop();
        return false;
    }

    // --- 填充 Muxer 需要的 CodecParameters ---
    codec_par_ = avcodec_parameters_alloc();
    if (!codec_par_) {
        std::cerr << "[Encoder] Error: avcodec_parameters_alloc failed." << std::endl;
        stop();
        return false;
    }
    ret = avcodec_parameters_from_context(codec_par_, codec_ctx_);
    if (ret < 0) {
        std::cerr << "[Encoder] Error: avcodec_parameters_from_context failed: " << ff_err2str(ret) << std::endl;
        stop();
        return false;
    }

    std::cout << "[INFO] VideoEncoder initialized successfully (NV12 -> YUV420P -> H.264)." << std::endl;
    return true;
}

bool VideoEncoder::encodeFrame(AVFrame* nv12_frame) {
    if (!codec_ctx_ || !sws_ctx_ || !yuv420p_frame_) {
        return false;
    }

    // 1. 格式转换: NV12 -> YUV420P
    int ret = sws_scale(
            sws_ctx_,
            (const uint8_t* const*)nv12_frame->data, nv12_frame->linesize,
            0, frame_height_,
            yuv420p_frame_->data, yuv420p_frame_->linesize
    );
    if (ret < 0) {
        std::cerr << "[Encoder] Error: sws_scale failed." << std::endl;
        return false;
    }

    // 2. 设置 PTS (Presentation Timestamp)
    // 我们使用编码器的时间基 {1, fps}
    // 你的采集代码使用了 (av_gettime() - startTime) / 1000 (毫秒)
    // 我们这里采用更简单的帧计数法，保证 PTS 严格递增
    yuv420p_frame_->pts = next_pts_++;
    // (注意：如果音视频同步要求非常高，你需要把采集的毫秒时间戳 (nv12_frame->pts)
    //  转换为编码器的 time_base:
    //  AVRational ms_timebase = {1, 1000};
    //  yuv420p_frame_->pts = av_rescale_q(nv12_frame->pts, ms_timebase, codec_ctx_->time_base);
    //  这里我们暂时使用简单的帧计数)

    // 3. 发送帧给编码器
    ret = avcodec_send_frame(codec_ctx_, yuv420p_frame_);
    if (ret < 0) {
        std::cerr << "[Encoder] Error: avcodec_send_frame failed: " << ff_err2str(ret) << std::endl;
        return false;
    }

    // 4. 循环接收编码后的 AVPacket
    while (ret >= 0) {
        AVPacket* packet = av_packet_alloc();
        if (!packet) {
            std::cerr << "[Encoder] Error: av_packet_alloc failed." << std::endl;
            return false;
        }

        ret = avcodec_receive_packet(codec_ctx_, packet);

        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            // EAGAIN: 编码器需要更多输入 (即当前帧不够，或B帧需要未来帧)
            // EOF:    编码器已 flush
            av_packet_free(&packet);
            break;
        } else if (ret < 0) {
            std::cerr << "[Encoder] Error: avcodec_receive_packet failed: " << ff_err2str(ret) << std::endl;
            av_packet_free(&packet);
            break;
        }

        // 5. 成功编码一个 Packet，通过回调发送出去
        if (callback_) {
            // packet 的 pts 和 dts 已经由编码器根据 codec_ctx_->time_base 设置好了
            callback_(packet);
        } else {
            // 如果没有设置回调，我们必须自己释放，否则内存泄漏
            av_packet_free(&packet);
        }
    }

    return true;
}

void VideoEncoder::flush() {
    if (!codec_ctx_) return;

    std::cout << "[INFO] Flushing VideoEncoder..." << std::endl;
    // 发送空帧 (nullptr) 来 flush 编码器
    int ret = avcodec_send_frame(codec_ctx_, nullptr);
    if (ret < 0) {
        std::cerr << "[Encoder] Error: avcodec_send_frame (flush) failed: " << ff_err2str(ret) << std::endl;
    }

    // 循环接收所有剩余的 packet
    while (ret >= 0) {
        AVPacket* packet = av_packet_alloc();
        if (!packet) break;

        ret = avcodec_receive_packet(codec_ctx_, packet);
        if (ret == AVERROR_EOF) {
            av_packet_free(&packet);
            break;
        } else if (ret < 0) {
            av_packet_free(&packet);
            break;
        }

        if (callback_) {
            callback_(packet);
        } else {
            av_packet_free(&packet);
        }
    }
}

void VideoEncoder::stop() {
    // 1. 刷新编码器
    flush();

    // 2. 释放资源
    if (sws_ctx_) {
        sws_freeContext(sws_ctx_);
        sws_ctx_ = nullptr;
    }
    if (yuv420p_frame_) {
        av_frame_free(&yuv420p_frame_);
        yuv420p_frame_ = nullptr;
    }
    if (codec_par_) {
        avcodec_parameters_free(&codec_par_);
        codec_par_ = nullptr;
    }
    if (codec_ctx_) {
        avcodec_free_context(&codec_ctx_);
        codec_ctx_ = nullptr;
    }

    frame_width_ = 0;
    frame_height_ = 0;
    next_pts_ = 0;
}