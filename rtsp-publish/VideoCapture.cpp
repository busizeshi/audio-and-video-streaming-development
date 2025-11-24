//
// Created by 13127 on 2025/11/17.
//

#include "VideoCapture.h"

#include <utility>

static std::string ff_err2str(int errnum) {
    char buf[256];
    av_strerror(errnum, buf, sizeof(buf));
    return std::string(buf);
}

VideoCapture::VideoCapture()
{
    avdevice_register_all();
}

VideoCapture::~VideoCapture()
{
    stop();
}

bool VideoCapture::open(const std::string& deviceName, const int width, const int height, const int fps)
{
    // 查找 dshow（Windows）；如果在其他平台，应改为对应 input format
    const AVInputFormat* inputFormat = av_find_input_format("dshow");
    if (!inputFormat)
    {
        std::cerr << "[ERROR]: cannot find input format dshow" << std::endl;
        return false;
    }

    AVDictionary* options = nullptr;
    const std::string size = std::to_string(width) + "x" + std::to_string(height);
    av_dict_set(&options, "video_size", size.c_str(), 0);
    av_dict_set(&options, "framerate", std::to_string(fps).c_str(), 0);
    av_dict_set(&options, "rtbufsize", "100M", 0);

    const std::string url = "video=" + deviceName;

    int ret = avformat_open_input(&fmt_ctx, url.c_str(), inputFormat, &options);
    // options 内存由 av_dict_free 释放
    av_dict_free(&options);
    if (ret < 0)
    {
        std::cerr << "[ERROR]: avformat_open_input failed: " << ff_err2str(ret) << std::endl;
        return false;
    }

    // 获取流信息（会读取一些 packet 来探测）
    ret = avformat_find_stream_info(fmt_ctx, nullptr);
    if (ret < 0)
    {
        std::cerr << "[ERROR]: avformat_find_stream_info failed: " << ff_err2str(ret) << std::endl;
        avformat_close_input(&fmt_ctx);
        return false;
    }

    // 找到视频流
    video_stream_index = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_stream_index < 0)
    {
        std::cerr << "[ERROR]: cannot find video stream" << std::endl;
        avformat_close_input(&fmt_ctx);
        return false;
    }

    const AVStream* video_stream = fmt_ctx->streams[video_stream_index];
    if (!video_stream || !video_stream->codecpar)
    {
        std::cerr << "[ERROR]: invalid video stream or codecpar" << std::endl;
        avformat_close_input(&fmt_ctx);
        return false;
    }

    const AVCodec* codec = avcodec_find_decoder(video_stream->codecpar->codec_id);
    if (!codec)
    {
        std::cerr << "[ERROR]: decoder not found for codec id " << video_stream->codecpar->codec_id << std::endl;
        avformat_close_input(&fmt_ctx);
        return false;
    }

    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx)
    {
        std::cerr << "[ERROR]: avcodec_alloc_context3 failed" << std::endl;
        avformat_close_input(&fmt_ctx);
        return false;
    }

    // 将探测到的参数复制到 codec_ctx
    ret = avcodec_parameters_to_context(codec_ctx, video_stream->codecpar);
    if (ret < 0)
    {
        std::cerr << "[ERROR]: avcodec_parameters_to_context failed: " << ff_err2str(ret) << std::endl;
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return false;
    }

    // 可选：设置线程数（根据需要），例如 codec_ctx->thread_count = 0 (auto)
    // codec_ctx->thread_count = 0;

    ret = avcodec_open2(codec_ctx, codec, nullptr);
    if (ret < 0)
    {
        std::cerr << "[ERROR]: avcodec_open2 failed: " << ff_err2str(ret) << std::endl;
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return false;
    }

    std::cout << "[INFO]: camera opened: " << width << "x" << height
              << " | Format: " << (codec_ctx->pix_fmt != AV_PIX_FMT_NONE ? av_get_pix_fmt_name(codec_ctx->pix_fmt) : "unknown")
              << " | Codec: " << codec->name << std::endl;

    return true;
}

void VideoCapture::stop()
{
    // 标记停止，等待线程退出
    isRunning = false;

    if (worker_thread.joinable())
    {
        worker_thread.join();
    }

    // 释放上下文和输入（如果尚未释放）
    if (codec_ctx)
    {
        avcodec_free_context(&codec_ctx);
        codec_ctx = nullptr;
    }
    if (fmt_ctx)
    {
        avformat_close_input(&fmt_ctx);
        fmt_ctx = nullptr;
    }
}

void VideoCapture::start(const VideoFrameCallback& cb)
{
    if (isRunning)
        return;

    if (!fmt_ctx || !codec_ctx || video_stream_index < 0)
    {
        std::cerr << "[ERROR]: device not opened or codec not ready. call open() first." << std::endl;
        return;
    }

    callback = cb;
    isRunning = true;
    startTime = av_gettime();

    // 启动线程（成员函数）
    worker_thread = std::thread(&VideoCapture::captureThreadLoop, this);
}

void VideoCapture::captureThreadLoop()
{
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();

    if (!packet || !frame)
    {
        std::cerr << "[ERROR]: alloc packet/frame failed" << std::endl;
        if (packet) av_packet_free(&packet);
        if (frame) av_frame_free(&frame);
        isRunning = false;
        return;
    }

    // 主循环
    while (isRunning)
    {
        int ret = av_read_frame(fmt_ctx, packet);
        if (ret == AVERROR(EAGAIN))
        {
            // 设备暂时没有数据，立即重试（不 sleep，避免丢帧）
            av_packet_unref(packet);
            continue;
        }
        else if (ret < 0)
        {
            // 遇到不可恢复错误或 EOF：跳出循环
            // 打印错误信息以便调试
            std::cerr << "[WARN]: av_read_frame returned error: " << ff_err2str(ret) << std::endl;
            av_packet_unref(packet);
            break;
        }

        if (packet->stream_index == video_stream_index)
        {
            ret = avcodec_send_packet(codec_ctx, packet);
            if (ret < 0 && ret != AVERROR(EAGAIN))
            {
                std::cerr << "[WARN]: avcodec_send_packet failed: " << ff_err2str(ret) << std::endl;
            }
            else
            {
                // 读取所有可用的解码帧
                while (ret >= 0)
                {
                    ret = avcodec_receive_frame(codec_ctx, frame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                    {
                        break;
                    }
                    else if (ret < 0)
                    {
                        std::cerr << "[WARN]: avcodec_receive_frame error: " << ff_err2str(ret) << std::endl;
                        break;
                    }

                    // 设置 pts（毫秒级）
                    frame->pts = av_gettime() - startTime;

                    if (callback)
                    {
                        // 使用 av_frame_ref 将 frame 的引用传给回调（避免昂贵拷贝）
                        AVFrame* ref = av_frame_alloc();
                        if (ref)
                        {
                            if (av_frame_ref(ref, frame) == 0)
                            {
                                // 调用回调（回调要负责 av_frame_unref/ref_free）
                                callback(ref);
                            }
                            else
                            {
                                av_frame_free(&ref);
                                std::cerr << "[WARN]: av_frame_ref failed" << std::endl;
                            }
                        }
                        else
                        {
                            std::cerr << "[WARN]: av_frame_alloc for ref failed" << std::endl;
                        }
                    }

                    av_frame_unref(frame); // 清理供下一次接收使用
                }
            }
        }

        av_packet_unref(packet);
    }

    // 退出前 drain decoder：发送空包，取出残留帧
    avcodec_send_packet(codec_ctx, nullptr);
    while (avcodec_receive_frame(codec_ctx, frame) == 0)
    {
        frame->pts = av_gettime() - startTime;
        if (callback)
        {
            AVFrame* ref = av_frame_alloc();
            if (ref && av_frame_ref(ref, frame) == 0)
            {
                callback(ref);
            }
            else if (ref)
            {
                av_frame_free(&ref);
            }
        }
        av_frame_unref(frame);
    }

    av_packet_free(&packet);
    av_frame_free(&frame);
}
