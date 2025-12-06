/**
* @file: main.c
 * @brief: 解码音频，主要的测试格式aac和mp3
 * @author: jwd
 * @date: 2025.12.06
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavutil/frame.h>
#include <libavutil/mem.h>

#include <libavcodec/avcodec.h>

#define AUDIO_IN_BUF_SIZE 20480
#define AUDIO_REFILL_THRESH 4096

static char err_buf[128] = {0};

/**
 * @brief 获取FFmpeg错误码对应的描述字符串
 * @param err_num FFmpeg错误码
 * @return 错误描述字符串
 */
static char *av_get_err(int err_num) {
    av_strerror(err_num, err_buf, sizeof(err_buf));
    return err_buf;
}

/**
 * @brief 打印音频帧的基本信息
 * @param frame 音频帧指针
 */
static void print_sample_format(const AVFrame *frame) {
    printf("ar-samplerate:%uHz\n", frame->sample_rate);
    printf("ac_channel:%u\n", frame->channels);
    printf("f-format:%u\n", frame->format);
}

/**
 * @brief 解码音频数据并写入输出文件
 * @param dec_ctx 解码器上下文
 * @param pkt 包含编码数据的AVPacket
 * @param frame 用于接收解码后数据的AVFrame
 * @param outfile 输出文件指针
 */
static void decode(AVCodecContext *dec_ctx, AVPacket *pkt, AVFrame *frame, FILE *outfile) {
    int i, ch;
    int ret, data_size;
    
    // 向解码器发送数据包
    ret = avcodec_send_packet(dec_ctx, pkt);
    if (ret == AVERROR(EAGAIN)) {
        // 解码器需要先输出数据才能接收新数据包
        fprintf(stderr, "avcodec_send_packet EAGAIN\n");
    } else if (ret < 0) {
        // 发送数据包失败
        fprintf(stderr, "Error submitting the packet to the decoder, err:%s, pkt_size:%d\n",
                av_get_err(ret), pkt->size);
        return;
    }

    // 循环接收解码后的帧数据
    while (ret >= 0) {
        ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            // 需要更多数据包或已到达流末尾
            return;
        } else if (ret < 0) {
            // 解码过程中发生错误
            fprintf(stderr, "Error during decoding, err:%s\n",
                    av_get_err(ret));
            exit(1);
        }
        
        // 获取每个样本的字节数
        data_size = av_get_bytes_per_sample(dec_ctx->sample_fmt);
        if (data_size < 0) {
            fprintf(stderr, "Failed to calculate data size\n");
            exit(1);
        }

        // 仅在第一次解码时打印音频格式信息
        static int s_print_format = 0;
        if (!s_print_format) {
            print_sample_format(frame);
            s_print_format = 1;
        }

        // 将解码后的PCM数据写入输出文件
        // 对于每个样本和每个声道，逐个写入数据
        for (i = 0; i < frame->nb_samples; i++) {
            for (ch = 0; ch < frame->ch_layout.nb_channels; ch++) {
                fwrite(frame->data[ch] + i * data_size, 1, data_size, outfile);
            }
        }
    }
}

// 使用示例: ffplay -f f32le -ar 48000 -ac 2 out.pcm
int main(int argc, char *argv[]) {
    const char *out_filename;
    const char *filename;
    const AVCodec *codec;
    AVCodecContext *codec_ctx = nullptr;
    AVCodecParserContext *parser = nullptr;
    size_t len = 0;  // 修改为size_t以避免类型转换警告
    int ret = 0;
    FILE *infile = nullptr;
    FILE *outfile = nullptr;
    uint8_t inbuf[AUDIO_IN_BUF_SIZE + AV_INPUT_BUFFER_PADDING_SIZE];
    uint8_t *data = nullptr;
    size_t data_size = 0;
    AVPacket *pkt = nullptr;
    AVFrame *decoded_frame = nullptr;

    // 检查命令行参数数量
    if (argc != 3) {
        fprintf(stderr, "Usage: %s input output\n", argv[0]);
        return -1;
    }
    filename = argv[1];
    out_filename = argv[2];

    // 分配数据包内存
    pkt = av_packet_alloc();
    
    // 根据文件扩展名确定编解码器ID
    enum AVCodecID audio_codec_id = AV_CODEC_ID_AAC;
    if (strstr(filename, ".aac") != nullptr) {
        audio_codec_id = AV_CODEC_ID_AAC;
    } else if (strstr(filename, ".mp3") != nullptr) {
        audio_codec_id = AV_CODEC_ID_MP3;
    } else {
        printf("default codec id:%d\n", audio_codec_id);
    }

    // 查找对应解码器
    codec = avcodec_find_decoder(audio_codec_id);
    if (!codec) {
        fprintf(stderr, "avcodec_find_decoder failed, err:%s\n",
                av_get_err(ret));
        return -1;
    }

    // 初始化解析器
    parser = av_parser_init(codec->id);
    if (!parser) {
        fprintf(stderr, "av_parser_init failed, err:%s\n",
                av_get_err(ret));
        return -1;
    }

    // 分配解码器上下文
    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        fprintf(stderr, "avcodec_alloc_context3 failed, err:%s\n",
                av_get_err(ret));
        return -1;
    }

    // 打开解码器
    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        fprintf(stderr, "avcodec_open2 failed, err:%s\n",
                av_get_err(ret));
        return -1;
    }

    // 打开输入文件
    infile = fopen(filename, "rb");
    if (!infile) {
        fprintf(stderr, "fopen failed, err:%s\n",
                av_get_err(ret));
        return -1;
    }

    // 打开输出文件
    outfile = fopen(out_filename, "wb");
    if (!outfile) {
        fprintf(stderr, "fopen failed, err:%s\n",
                av_get_err(ret));
        return -1;
    }

    // 从输入文件读取初始数据
    data = inbuf;
    data_size = fread(inbuf, 1, AUDIO_IN_BUF_SIZE, infile);

    // 循环处理所有数据直到文件结束
    while (data_size > 0) {
        // 确保解码帧已分配
        if (!decoded_frame) {
            if (!(decoded_frame = av_frame_alloc())) {
                fprintf(stderr, "av_frame_alloc failed, err:%s\n",
                        av_get_err(ret));
                return -1;
            }
        }

        // 解析数据并填充数据包
        ret = av_parser_parse2(parser, codec_ctx, &pkt->data, &pkt->size,
                               data, data_size,
                               AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
        if (ret < 0) {
            fprintf(stderr, "av_parser_parse2 failed, err:%s\n",
                    av_get_err(ret));
            return -1;
        }
        
        // 更新数据指针和剩余数据大小
        data += ret;
        data_size -= ret;

        // 如果数据包中有有效数据，则进行解码
        if (pkt->size) {
            decode(codec_ctx, pkt, decoded_frame, outfile);
        }
        
        // 当缓冲区数据低于阈值时，移动剩余数据并读取新数据
        if (data_size < AUDIO_REFILL_THRESH) {
            memmove(inbuf, data, data_size);
            data = inbuf;
            len = fread(data + data_size, 1,
                        AUDIO_IN_BUF_SIZE - data_size, infile);
            if (len > 0) {
                data_size += len;
            }
        }
    }
    
    // 处理解码器中剩余的数据（drain模式）
    pkt->data = nullptr;
    pkt->size = 0;
    decode(codec_ctx, pkt, decoded_frame, outfile);

    // 关闭文件
    fclose(outfile);
    fclose(infile);

    // 释放资源
    avcodec_free_context(&codec_ctx);
    av_parser_close(parser);
    av_frame_free(&decoded_frame);
    av_packet_free(&pkt);

    printf("main finish, please enter Enter and exit\n");
    return 0;
}