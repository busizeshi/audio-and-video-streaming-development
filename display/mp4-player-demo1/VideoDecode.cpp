#include "VideoDecode.h"
#include <iostream>

VideoDecode::VideoDecode() = default;

VideoDecode::~VideoDecode() {
    close();
}

bool VideoDecode::init(const std::string &filename) {
    // 确保资源是干净的
    close();

    int ret = -1;
    // 打开视频文件
    ret = avformat_open_input(&format_ctx, filename.c_str(), nullptr, nullptr);
    if (ret < 0) {
        std::cerr << "打开视频文件失败: " << filename << std::endl;
        return false;
    }

    // 检索信息流
    ret = avformat_find_stream_info(format_ctx, nullptr);
    if (ret < 0) {
        std::cerr << "检索信息流失败" << std::endl;
        return false;
    }

    // 查找视频流得到视频流索引
    video_stream_index = av_find_best_stream(format_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_stream_index < 0) {
        std::cerr << "未找到视频流" << std::endl;
        return false;
    }

    // 查找解码器
    AVCodecParameters *video_codec_params = format_ctx->streams[video_stream_index]->codecpar;
    const AVCodec *video_codec = avcodec_find_decoder(video_codec_params->codec_id);
    if (!video_codec) {
        std::cerr << "未找到解码器" << std::endl;
        return false;
    }

    // 为视频分配解码器上下文
    video_ctx = avcodec_alloc_context3(video_codec);
    if (!video_ctx) {
        std::cerr << "为视频分配解码器上下文失败" << std::endl;
        return false;
    }

    // 复制解码器参数到上下文
    ret = avcodec_parameters_to_context(video_ctx, video_codec_params);
    if (ret < 0) {
        std::cerr << "参数复制失败" << std::endl;
        return false;
    }

    // 打开解码器
    ret = avcodec_open2(video_ctx, video_codec, nullptr);
    if (ret < 0) {
        std::cerr << "打开解码器失败" << std::endl;
        return false;
    }

    // 分配frame
    frame = av_frame_alloc();
    rgb_frame = av_frame_alloc();
    if (!frame || !rgb_frame) {
        std::cerr << "分配Frame内存失败" << std::endl;
        return false;
    }

    // 预分配 RGB buffer
    // 注意：宽度必须对齐
    int num_bytes = av_image_get_buffer_size(AV_PIX_FMT_RGB24, video_ctx->width, video_ctx->height, 1);
    buffer = (uint8_t *) av_malloc(num_bytes * sizeof(uint8_t));

    // 关联 buffer 到 rgb_frame
    av_image_fill_arrays(rgb_frame->data, rgb_frame->linesize, buffer, AV_PIX_FMT_RGB24, video_ctx->width, video_ctx->height, 1);

    // 初始化色彩转换器
    if (!initSwsContext()) {
        std::cerr << "初始化sws上下文失败" << std::endl;
        return false;
    }
    return true;
}

bool VideoDecode::readNextFrame() {
    if (!format_ctx || !video_ctx) return false;

    AVPacket packet;
    int ret = 0;

    while (av_read_frame(format_ctx, &packet) >= 0) {
        if (packet.stream_index == video_stream_index) {
            // 1. 发送数据包到解码器
            ret = avcodec_send_packet(video_ctx, &packet);
            if (ret < 0) {
                std::cerr << "发送Packet到解码器失败" << std::endl;
                av_packet_unref(&packet); // 释放内存
                return false;
            }

            // 2. 从解码器接收帧 (可能一个包对应多个帧，或者需要多个包才能出一个帧)
            // 这里为了简单，我们只取第一帧，但在严格循环中应处理所有帧
            ret = avcodec_receive_frame(video_ctx, frame);

            if (ret == 0) {
                // 解码成功
                // 转换颜色格式 YUV -> RGB
                sws_scale(sws_ctx,
                          (const uint8_t *const *) frame->data, frame->linesize,
                          0, video_ctx->height,
                          rgb_frame->data, rgb_frame->linesize);

                // 释放 packet 引用（重要！）
                av_packet_unref(&packet);
                return true;
            } else if (ret == AVERROR(EAGAIN)) {
                // 需要更多 Packet 才能解码出一帧，继续循环读取
            } else if (ret == AVERROR_EOF) {
                // 文件结束
                av_packet_unref(&packet);
                return false;
            } else {
                std::cerr << "解码接收Frame失败" << std::endl;
                av_packet_unref(&packet);
                return false;
            }
        }
        // 释放非视频流或未完成解码的 Packet 引用（防止内存泄漏）
        av_packet_unref(&packet);
    }

    return false;
}

uint8_t *VideoDecode::getRGBData() {
    if (rgb_frame) {
        return rgb_frame->data[0];
    }
    return nullptr;
}

int VideoDecode::getLineSize() {
    if (rgb_frame) {
        return rgb_frame->linesize[0];
    }
    return 0;
}

int VideoDecode::getWidth() {
    return video_ctx ? video_ctx->width : 0;
}

int VideoDecode::getHeight() {
    return video_ctx ? video_ctx->height : 0;
}

double VideoDecode::getFPS() {
    if (format_ctx && video_stream_index >= 0) {
        AVStream *stream = format_ctx->streams[video_stream_index];
        // 优先使用 avg_frame_rate
        if (stream->avg_frame_rate.den > 0) {
            return av_q2d(stream->avg_frame_rate);
        }
        // 备选 r_frame_rate
        if (stream->r_frame_rate.den > 0) {
            return av_q2d(stream->r_frame_rate);
        }
    }
    return 0.0;
}

void VideoDecode::close() {
    if (frame) av_frame_free(&frame);
    if (rgb_frame) av_frame_free(&rgb_frame);
    if (video_ctx) avcodec_free_context(&video_ctx);
    if (format_ctx) avformat_close_input(&format_ctx);
    if (buffer) av_freep(&buffer);
    if (sws_ctx) {
        sws_freeContext(sws_ctx);
        sws_ctx = nullptr;
    }
    video_stream_index = -1;
}

bool VideoDecode::initSwsContext() {
    if (!video_ctx) return false;

    // 如果已经存在，先释放
    if (sws_ctx) sws_freeContext(sws_ctx);

    sws_ctx = sws_getContext(video_ctx->width, video_ctx->height, video_ctx->pix_fmt,
                             video_ctx->width, video_ctx->height, AV_PIX_FMT_RGB24,
                             SWS_BILINEAR, nullptr, nullptr, nullptr);
    return (sws_ctx != nullptr);
}