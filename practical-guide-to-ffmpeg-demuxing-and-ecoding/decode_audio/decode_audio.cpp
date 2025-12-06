#include <iostream>
#include <fstream>
#include <string>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
}

//ffplay -ar 44100 -ac 2 -f s16le output.pcm
int main(int argc, char **argv) {
    std::string inputFile = "input.aac";
    std::string outputFile = "output.pcm";

    if (argc >= 3) {
        inputFile = argv[1];
        outputFile = argv[2];
        std::cout << "输入文件：" << inputFile << "\n"
                  << "输出文件：" << outputFile << "\n";
    }

    // 1. 打开输入文件
    AVFormatContext *fmtCtx = nullptr;
    if (avformat_open_input(&fmtCtx, inputFile.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "无法打开输入文件\n";
        return -1;
    }

    if (avformat_find_stream_info(fmtCtx, nullptr) < 0) {
        std::cerr << "无法找到流信息\n";
        return -1;
    }

    // 2. 查找 AAC 音频流
    int audioStreamIndex = -1;
    for (unsigned int i = 0; i < fmtCtx->nb_streams; i++) {
        if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audioStreamIndex = i;
            break;
        }
    }
    if (audioStreamIndex < 0) {
        std::cerr << "找不到音频流\n";
        return -1;
    }

    AVCodecParameters *codecpar = fmtCtx->streams[audioStreamIndex]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) {
        std::cerr << "找不到 AAC 解码器\n";
        return -1;
    }

    // 3. 打开解码器
    AVCodecContext *codecCtx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codecCtx, codecpar);

    if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
        std::cerr << "解码器打开失败\n";
        return -1;
    }

    // 4. 创建 SwrContext 做重采样 → 输出 PCM s16
    SwrContext *swr = swr_alloc();
    av_opt_set_int(swr, "in_channel_layout", codecCtx->channel_layout, 0);
    av_opt_set_int(swr, "out_channel_layout", codecCtx->channel_layout, 0);
    av_opt_set_int(swr, "in_sample_rate", codecCtx->sample_rate, 0);
    av_opt_set_int(swr, "out_sample_rate", codecCtx->sample_rate, 0);
    av_opt_set_sample_fmt(swr, "in_sample_fmt", codecCtx->sample_fmt, 0);
    av_opt_set_sample_fmt(swr, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);
    swr_init(swr);

    // 5. 解码循环
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    std::ofstream outFile(outputFile, std::ios::binary);

    while (av_read_frame(fmtCtx, pkt) >= 0) {
        if (pkt->stream_index == audioStreamIndex) {
            if (avcodec_send_packet(codecCtx, pkt) == 0) {
                while (avcodec_receive_frame(codecCtx, frame) == 0) {

                    // 输出缓冲区
                    uint8_t *outBuffer[2] = {nullptr};
                    int outSamples = swr_get_out_samples(swr, frame->nb_samples);
                    int outBufferSize = av_samples_alloc(outBuffer, nullptr,
                                                         codecCtx->channels,
                                                         outSamples,
                                                         AV_SAMPLE_FMT_S16, 0);

                    // 重采样
                    int samples = swr_convert(swr, outBuffer, outSamples,
                                              (const uint8_t **) frame->extended_data,
                                              frame->nb_samples);

                    // 写入 PCM 文件
                    int dataSize = samples * codecCtx->channels * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
                    outFile.write((char *) outBuffer[0], dataSize);

                    av_freep(&outBuffer[0]);
                }
            }
        }
        av_packet_unref(pkt);
    }

    // 6. 清理资源
    outFile.close();
    swr_free(&swr);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmtCtx);

    std::cout << "AAC 解码完成，PCM 已输出到：" << outputFile << "\n";
    return 0;
}
