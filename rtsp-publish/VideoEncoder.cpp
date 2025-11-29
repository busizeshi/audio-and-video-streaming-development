#include "VideoEncoder.h"

static inline std::string ff_err2str(int errnum) {
    char buf[256];
    av_strerror(errnum, buf, sizeof(buf));
    return {buf};
}

VideoEncoder::VideoEncoder() {}

VideoEncoder::~VideoEncoder() {
    stop();
}

bool VideoEncoder::init(int width, int height, int fps, int bit_rate) {
    frame_width_ = width;
    frame_height_ = height;

    // 强制使用 libx264
    const AVCodec *codec = avcodec_find_encoder_by_name("libx264");
    if (!codec) {
        std::cerr << "[Encoder] Error: libx264 not found." << std::endl;
        return false;
    }

    codec_ctx_ = avcodec_alloc_context3(codec);

    codec_ctx_->width = width;
    codec_ctx_->height = height;
    codec_ctx_->bit_rate = bit_rate;
    // 关键：时基。输入采集是毫秒(1/1000)，这里设置为 1/1000 以保持 PTS 精度，或者 1/fps
    codec_ctx_->time_base = {1, 1000};
    codec_ctx_->framerate = {fps, 1};
    codec_ctx_->gop_size = fps * 2;
    codec_ctx_->max_b_frames = 0; // 直播低延迟建议为 0
    codec_ctx_->pix_fmt = AV_PIX_FMT_YUV420P; // x264 最通用格式

    codec_ctx_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    // 低延迟参数
    av_opt_set(codec_ctx_->priv_data, "preset", "veryfast", 0);
    av_opt_set(codec_ctx_->priv_data, "tune", "zerolatency", 0);

    if (avcodec_open2(codec_ctx_, codec, nullptr) < 0) {
        std::cerr << "[Encoder] Failed to open codec." << std::endl;
        return false;
    }

    // 初始化 SWS: NV12 -> YUV420P
    sws_ctx_ = sws_getContext(width, height, AV_PIX_FMT_NV12,
                              width, height, AV_PIX_FMT_YUV420P,
                              SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);

    // 预分配 YUV420P 帧
    yuv420p_frame_ = av_frame_alloc();
    yuv420p_frame_->format = AV_PIX_FMT_YUV420P;
    yuv420p_frame_->width = width;
    yuv420p_frame_->height = height;
    av_frame_get_buffer(yuv420p_frame_, 32);

    // 填充 codecpar
    codec_par_ = avcodec_parameters_alloc();
    avcodec_parameters_from_context(codec_par_, codec_ctx_);

    return true;
}

bool VideoEncoder::encodeFrame(AVFrame *nv12_frame) {
    if (!codec_ctx_ || !sws_ctx_) return false;

    // 1. 转码 NV12 -> YUV420P
    // 必须确保 yuv420p_frame_ 是可写的
    av_frame_make_writable(yuv420p_frame_);

    sws_scale(sws_ctx_,
              (const uint8_t *const *) nv12_frame->data, nv12_frame->linesize,
              0, frame_height_,
              yuv420p_frame_->data, yuv420p_frame_->linesize);

    // 2. 传递 PTS
    // 假设 nv12_frame->pts 是毫秒 (VideoCapture产生)
    // 我们的 time_base 也是 {1, 1000}，所以直接传递
    yuv420p_frame_->pts = nv12_frame->pts;

    // 如果想要非常严格的固定帧率（忽略采集抖动），可以使用计数器：
    // yuv420p_frame_->pts = next_pts_ * (1000.0 / codec_ctx_->framerate.num);
    // next_pts_++;

    // 3. 发送给编码器 (注意：必须发送转换后的 yuv420p_frame_ !)
    int ret = avcodec_send_frame(codec_ctx_, yuv420p_frame_);
    if (ret < 0) {
        std::cerr << "[Encoder] Send frame error: " << ff_err2str(ret) << std::endl;
        return false;
    }

    while (ret >= 0) {
        AVPacket *packet = av_packet_alloc();
        ret = avcodec_receive_packet(codec_ctx_, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            av_packet_free(&packet);
            break;
        } else if (ret < 0) {
            av_packet_free(&packet);
            break;
        }

        if (callback_) {
            callback_(packet); // Callback 负责 free packet
        } else {
            av_packet_free(&packet);
        }
    }
    return true;
}

void VideoEncoder::flush() {
    if (!codec_ctx_) return;
    avcodec_send_frame(codec_ctx_, nullptr);
    // ... 接收剩余包逻辑同上 ...
}

void VideoEncoder::stop() {
    flush();
    if (sws_ctx_) sws_freeContext(sws_ctx_);
    if (yuv420p_frame_) av_frame_free(&yuv420p_frame_);
    if (codec_ctx_) avcodec_free_context(&codec_ctx_);
    if (codec_par_) avcodec_parameters_free(&codec_par_);
}