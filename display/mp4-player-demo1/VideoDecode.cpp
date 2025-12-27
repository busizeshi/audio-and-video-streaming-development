#include "VideoDecode.h"
#include <iostream>

VideoDecode::~VideoDecode() {
    close();
}

bool VideoDecode::init(const std::string &filename) {
    int ret = -1;
//    打开视频文件
    ret = avformat_open_input(&format_ctx, filename.c_str(), nullptr, nullptr);
    if (ret < 0) {
        std::cout << "打开视频文件失败" << std::endl;
        return false;
    }

//    检索信息流
    ret = avformat_find_stream_info(format_ctx, nullptr);
    if (ret < 0) {
        std::cout << "检索信息流失败" << std::endl;
        return false;
    }

//    查找视频流得到视频流索引
    for (int i = 0; i < format_ctx->nb_streams; ++i) {
        if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            break;
        }
    }
    if (video_stream_index == -1) {
        std::cout << "未找到视频流" << std::endl;
        return false;
    }

//    查找解码器
    AVCodecParameters *video_codec_params = format_ctx->streams[video_stream_index]->codecpar;
    const AVCodec *video_codec = avcodec_find_decoder(video_codec_params->codec_id);
    if (!video_codec) {
        std::cout << "未找到解码器" << std::endl;
        return false;
    }

//    为视频分配解码器上下文
    video_ctx = avcodec_alloc_context3(video_codec);
    if (!video_ctx) {
        std::cout << "为视频分配解码器上下文失败" << std::endl;
        return false;
    }

//    为解码器上下文复制六种的解码器参数
    ret = avcodec_parameters_to_context(video_ctx, video_codec_params);
    if (ret < 0) {
        std::cout << "为解码器上下文复制六种的解码器参数失败" << std::endl;
        return false;
    }

//    打开解码器
    ret = avcodec_open2(video_ctx, video_codec, nullptr);
    if (ret < 0) {
        std::cout << "打开解码器失败" << std::endl;
        return false;
    }

//    分配frame和rgb_frame
    frame = av_frame_alloc();
    rgb_frame = av_frame_alloc();
    if (!frame || !rgb_frame) {
        std::cout << "分配frame和rgb_frame失败" << std::endl;
        return false;
    }

    int num_bytes=av_image_get_buffer_size(AV_PIX_FMT_RGB24, video_ctx->width, video_ctx->height, 1);
    buffer = (uint8_t *) av_malloc(num_bytes * sizeof(uint8_t));
    av_image_fill_arrays(rgb_frame->data, rgb_frame->linesize, buffer, AV_PIX_FMT_RGB24, video_ctx->width, video_ctx->height, 1);

    if (!initSwsContext()) {
        std::cout << "初始化sws上下文失败" << std::endl;
        return false;
    }
    return true;
}

bool VideoDecode::readNextFrame() {
    AVPacket packet;

    int ret = -1;
    while (av_read_frame(format_ctx, &packet) >= 0) {
        if (packet.stream_index == video_stream_index) {
//            发送数据包到解码器
            ret = avcodec_send_packet(video_ctx, &packet);
            if (ret < 0) {
                std::cout << "发送packet失败" << std::endl;
                av_packet_unref(&packet);
                return false;
            }

//            从解码接收帧
            while (true) {
                ret = avcodec_receive_frame(video_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    std::cout << "接收frame失败" << std::endl;
                    av_packet_unref(&packet);
                    return false;
                }

//                转换颜色格式为RGB
                sws_scale(sws_ctx, (const uint8_t *const *) frame->data, frame->linesize, 0, video_ctx->height,
                          rgb_frame->data, rgb_frame->linesize);
                return true;
            }
        }
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

void VideoDecode::close() {
    av_frame_free(&frame);
    av_frame_free(&rgb_frame);
    avcodec_free_context(&video_ctx);
    avformat_close_input(&format_ctx);
    av_freep(&buffer);
    av_freep(&sws_ctx);
}

bool VideoDecode::initSwsContext() {
    sws_ctx = sws_getContext(video_ctx->width, video_ctx->height, video_ctx->pix_fmt,
                             video_ctx->width, video_ctx->height, AV_PIX_FMT_RGB24,
                             SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (sws_ctx) {
        return true;
    }
    return false;
}

VideoDecode::VideoDecode() = default;
