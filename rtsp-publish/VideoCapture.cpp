#include "VideoCapture.h"

static inline std::string ff_err2str(int errnum) {
    char buf[256];
    av_strerror(errnum, buf, sizeof(buf));
    return {buf};
}

VideoCapture::VideoCapture() {
    avdevice_register_all();
}

VideoCapture::~VideoCapture() {
    stop();
}

bool VideoCapture::open(const ConfigManager &config) {
    const std::string deviceName = config.getString("video_capture_name");
    const int width = config.getInt("video_width");
    const int height = config.getInt("video_height");
    const int fps = config.getInt("fps");
    const std::string hardware_vcodec = config.getString("hardware_vcodec");
    const std::string rtbufsize = config.getString("rtbufsize");

    std::cout << "[INFO]: Video Capture Start: " << deviceName << " " << width << "x" << height << " @" << fps
              << std::endl;

    const AVInputFormat *inputFormat = av_find_input_format("dshow");
    if (!inputFormat) {
        std::cerr << "[ERROR]: Cannot find input format dshow" << std::endl;
        return false;
    }

    AVDictionary *options = nullptr;
    std::string size = std::to_string(width) + "x" + std::to_string(height);
    av_dict_set(&options, "video_size", size.c_str(), 0);
    av_dict_set(&options, "framerate", std::to_string(fps).c_str(), 0);
    av_dict_set(&options, "rtbufsize", rtbufsize.c_str(), 0);
    // 如果摄像头支持 mjpeg，使用 mjpeg 可以获得更高帧率，否则使用 rawvideo
    av_dict_set(&options, "vcodec", hardware_vcodec.c_str(), 0);

    std::string url = "video=" + deviceName;
    int ret = avformat_open_input(&fmt_ctx, url.c_str(), inputFormat, &options);
    av_dict_free(&options);
    if (ret < 0) {
        std::cerr << "[ERROR]: avformat_open_input failed: " << ff_err2str(ret) << std::endl;
        return false;
    }

    ret = avformat_find_stream_info(fmt_ctx, nullptr);
    if (ret < 0) return false;

    video_stream_index = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_stream_index < 0) return false;

    AVStream *video_stream = fmt_ctx->streams[video_stream_index];
    const AVCodec *codec = avcodec_find_decoder(video_stream->codecpar->codec_id);
    if (!codec) return false;

    codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_ctx, video_stream->codecpar);

    ret = avcodec_open2(codec_ctx, codec, nullptr);
    if (ret < 0) return false;

    std::cout << "[INFO]: Camera opened. Native: " << av_get_pix_fmt_name(codec_ctx->pix_fmt)
              << " -> Target: NV12" << std::endl;

    // 初始化 SWS Context (目标格式固定为 NV12，兼容 x264 和 SDL)
    sws_ctx = sws_getContext(codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt,
                             codec_ctx->width, codec_ctx->height, AV_PIX_FMT_NV12,
                             SWS_BICUBIC, nullptr, nullptr, nullptr);

    return true;
}

void VideoCapture::stop() {
    isRunning = false;
    if (worker_thread.joinable()) {
        worker_thread.join();
    }
    if (sws_ctx) {
        sws_freeContext(sws_ctx);
        sws_ctx = nullptr;
    }
    if (codec_ctx) {
        avcodec_free_context(&codec_ctx);
        codec_ctx = nullptr;
    }
    if (fmt_ctx) {
        avformat_close_input(&fmt_ctx);
        fmt_ctx = nullptr;
    }
}

void VideoCapture::start(const VideoFrameCallback &cb) {
    if (isRunning) return;
    if (!fmt_ctx || !codec_ctx) return;

    callback = cb;
    isRunning = true;
    startTime = av_gettime();
    worker_thread = std::thread(&VideoCapture::captureThreadLoop, this);
}

void VideoCapture::captureThreadLoop() {
    AVPacket *packet = av_packet_alloc();
    AVFrame *raw_frame = av_frame_alloc();

    while (isRunning) {
        int ret = av_read_frame(fmt_ctx, packet);
        if (ret < 0) {
            av_packet_unref(packet);
            if (ret == AVERROR(EAGAIN)) continue;
            std::cerr << "[WARN]: Read frame error or EOF." << std::endl;
            break;
        }

        if (packet->stream_index == video_stream_index) {
            ret = avcodec_send_packet(codec_ctx, packet);
            if (ret >= 0) {
                while (ret >= 0) {
                    ret = avcodec_receive_frame(codec_ctx, raw_frame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                    else if (ret < 0) break;

                    // --- 关键修改开始 ---
                    // 必须为每一帧分配独立的内存，防止多线程数据竞争
                    AVFrame *nv12_frame = av_frame_alloc();
                    nv12_frame->format = AV_PIX_FMT_NV12;
                    nv12_frame->width = codec_ctx->width;
                    nv12_frame->height = codec_ctx->height;

                    // 分配 buffer
                    if (av_frame_get_buffer(nv12_frame, 32) < 0) {
                        av_frame_free(&nv12_frame);
                        continue;
                    }

                    // 写入数据
                    // 确保 NV12 的 YUV 数据可写
                    if (av_frame_make_writable(nv12_frame) < 0) {
                        av_frame_free(&nv12_frame);
                        continue;
                    }

                    sws_scale(sws_ctx,
                              (const uint8_t *const *) raw_frame->data, raw_frame->linesize,
                              0, raw_frame->height,
                              nv12_frame->data, nv12_frame->linesize);

                    // 计算 PTS (使用当前相对时间)
                    // 注意：这里使用毫秒级 PTS，后续编码器通过 TimeBase 转换
                    nv12_frame->pts = (av_gettime() - startTime) / 1000;

                    if (callback) {
                        callback(nv12_frame); // 传递所有权，由接收方负责释放
                    } else {
                        av_frame_free(&nv12_frame);
                    }
                    // --- 关键修改结束 ---

                    av_frame_unref(raw_frame);
                }
            }
        }
        av_packet_unref(packet);
    }

    av_packet_free(&packet);
    av_frame_free(&raw_frame);
}