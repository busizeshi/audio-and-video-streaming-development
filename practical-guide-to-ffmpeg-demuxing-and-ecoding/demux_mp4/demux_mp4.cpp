/**
 * MP4 解封装器 (Demuxer)
 * 功能：将 MP4 分离为 H.264 裸流文件和 AAC 音频文件
 * 关键技术：h264_mp4toannexb 滤镜, ADTS Muxer
 */

#include <iostream>
#include <string>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h> // Bitstream Filter
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <input_file.mp4>" << std::endl;
        return -1;
    }

    const char* input_filename = argv[1];
    const char* video_filename = argv[2];
    const char* audio_filename = argv[3];

    // 1. 打开输入文件
    AVFormatContext* ifmt_ctx = nullptr;
    if (avformat_open_input(&ifmt_ctx, input_filename, nullptr, nullptr) < 0) {
        std::cerr << "Could not open input file." << std::endl;
        return -1;
    }

    // 2. 获取流信息
    if (avformat_find_stream_info(ifmt_ctx, nullptr) < 0) {
        std::cerr << "Could not find stream info." << std::endl;
        return -1;
    }

    // 3. 查找音视频流索引
    int video_index = -1;
    int audio_index = -1;
    for (unsigned int i = 0; i < ifmt_ctx->nb_streams; i++) {
        if (ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_index = i;
        } else if (ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_index = i;
        }
    }

    if (video_index == -1 && audio_index == -1) {
        std::cerr << "No video or audio stream found." << std::endl;
        return -1;
    }

    // ==========================================
    // 4. 初始化输出上下文 (Muxers)
    // ==========================================

    // --- 视频输出 (H.264) ---
    AVFormatContext* pFormatContext = nullptr;
    const AVBitStreamFilter* bsf = nullptr;
    AVBSFContext* bsf_ctx = nullptr;

    if (video_index != -1) {
        // 创建输出上下文，指定格式为 raw h264
        avformat_alloc_output_context2(&pFormatContext, nullptr, "h264", video_filename);
        // 创建输出流
        AVStream* out_stream = avformat_new_stream(pFormatContext, nullptr);
        // 复制参数
        avcodec_parameters_copy(out_stream->codecpar, ifmt_ctx->streams[video_index]->codecpar);
        // 打开输出文件
        if (!(pFormatContext->oformat->flags & AVFMT_NOFILE)) {
            avio_open(&pFormatContext->pb, video_filename, AVIO_FLAG_WRITE);
        }
        // 写文件头
        if (avformat_write_header(pFormatContext, nullptr) < 0) {
            std::cerr << "Failed to write video header" << std::endl;
            return -1;
        }

        // *** 关键：初始化 h264_mp4toannexb 过滤器 ***
        // MP4 中的 H.264 是 AVCC 格式，需要转为 Annex B 才能保存为 .h264 文件
        bsf = av_bsf_get_by_name("h264_mp4toannexb");
        if (bsf) {
            av_bsf_alloc(bsf, &bsf_ctx);
            // 复制 codec parameters 给过滤器，这样它能提取 sps/pps
            avcodec_parameters_copy(bsf_ctx->par_in, ifmt_ctx->streams[video_index]->codecpar);
            av_bsf_init(bsf_ctx);
        } else {
            std::cerr << "Error: h264_mp4toannexb filter not found." << std::endl;
        }
    }

    // --- 音频输出 (AAC) ---
    AVFormatContext* ofmt_ctx_a = nullptr;
    if (audio_index != -1) {
        // 创建输出上下文，指定格式为 adts (标准的 .aac 文件封装)
        avformat_alloc_output_context2(&ofmt_ctx_a, nullptr, "adts", audio_filename);
        AVStream* out_stream = avformat_new_stream(ofmt_ctx_a, nullptr);
        avcodec_parameters_copy(out_stream->codecpar, ifmt_ctx->streams[audio_index]->codecpar);

        if (!(ofmt_ctx_a->oformat->flags & AVFMT_NOFILE)) {
            avio_open(&ofmt_ctx_a->pb, audio_filename, AVIO_FLAG_WRITE);
        }
        if (avformat_write_header(ofmt_ctx_a, nullptr) < 0) {
            std::cerr << "Failed to write audio header" << std::endl;
            return -1;
        }
    }

    // ==========================================
    // 5. 循环读取与处理 [cite: 36]
    // ==========================================
    AVPacket* pkt = av_packet_alloc();

    std::cout << "Start demuxing..." << std::endl;

    while (av_read_frame(ifmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == video_index) {
            // --- 处理视频 ---
            // 应用比特流过滤器 (AVCC -> Annex B)
            if (bsf_ctx) {
                // 发送 Packet 到过滤器
                if (av_bsf_send_packet(bsf_ctx, pkt) == 0) {
                    // 从过滤器接收 Packet (可能产生多个，如 SPS, PPS, IDR)
                    while (av_bsf_receive_packet(bsf_ctx, pkt) == 0) {
                        // 重置 pts/dts/stream_index 以适应输出流
                        pkt->stream_index = 0;
                        // 将时间基从输入流转换到输出流 (通常 H.264 裸流忽略时间戳，但为了严谨保留转换)
                        av_packet_rescale_ts(pkt, ifmt_ctx->streams[video_index]->time_base,
                                             pFormatContext->streams[0]->time_base);

                        // 写入文件
                        av_interleaved_write_frame(pFormatContext, pkt);
                    }
                }
            }
        } else if (pkt->stream_index == audio_index) {
            // --- 处理音频 ---
            // ADTS Muxer 会自动处理头部
            pkt->stream_index = 0;
            av_packet_rescale_ts(pkt, ifmt_ctx->streams[audio_index]->time_base,
                                 ofmt_ctx_a->streams[0]->time_base);

            av_interleaved_write_frame(ofmt_ctx_a, pkt);
        }

        // 释放引用，准备下一次读取
        av_packet_unref(pkt);
    }

    // ==========================================
    // 6. 收尾与释放 [cite: 40]
    // ==========================================

    // 写入文件尾
    if (pFormatContext) av_write_trailer(pFormatContext);
    if (ofmt_ctx_a) av_write_trailer(ofmt_ctx_a);

    std::cout << "Demuxing finished." << std::endl;
    std::cout << "Video saved to: " << video_filename << std::endl;
    std::cout << "Audio saved to: " << audio_filename << std::endl;

    // 释放资源
    if (pFormatContext) {
        if (!(pFormatContext->oformat->flags & AVFMT_NOFILE)) avio_closep(&pFormatContext->pb);
        avformat_free_context(pFormatContext);
    }
    if (ofmt_ctx_a) {
        if (!(ofmt_ctx_a->oformat->flags & AVFMT_NOFILE)) avio_closep(&ofmt_ctx_a->pb);
        avformat_free_context(ofmt_ctx_a);
    }
    if (bsf_ctx) av_bsf_free(&bsf_ctx);

    avformat_close_input(&ifmt_ctx);
    av_packet_free(&pkt);

    return 0;
}