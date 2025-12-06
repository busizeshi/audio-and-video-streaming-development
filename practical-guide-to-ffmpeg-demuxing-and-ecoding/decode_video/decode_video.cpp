/**
 * 基于《视频解码实战》文档实现的 H.264 转 YUV 解码器
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
}

#define INBUF_SIZE 4096

// 辅助函数：将解码后的 AVFrame (YUV420P) 写入文件
// 注意：必须处理 linesize 对齐，不能一次性写入整个 plane
static void write_yuv_frame(AVFrame *frame, FILE *outfile) {
    // 1. 写入 Y 分量 (data[0])
    // Y 的宽是 frame->width，高是 frame->height
    for (int y = 0; y < frame->height; y++) {
        fwrite(frame->data[0] + y * frame->linesize[0], 1, frame->width, outfile);
    }

    // 2. 写入 U 分量 (data[1])
    // YUV420P 中，UV 的宽高是 Y 的一半
    for (int y = 0; y < frame->height / 2; y++) {
        fwrite(frame->data[1] + y * frame->linesize[1], 1, frame->width / 2, outfile);
    }

    // 3. 写入 V 分量 (data[2])
    for (int y = 0; y < frame->height / 2; y++) {
        fwrite(frame->data[2] + y * frame->linesize[2], 1, frame->width / 2, outfile);
    }
}

// 核心解码函数
static void decode(AVCodecContext *dec_ctx, AVPacket *pkt, AVFrame *frame, FILE *outfile) {
    int ret;

    // 1. 发送 Packet 给解码器 [cite: 38, 115]
    // pkt 为 NULL 时触发冲刷 (Flush)
    ret = avcodec_send_packet(dec_ctx, pkt);
    if (ret < 0) {
        fprintf(stderr, "Error sending a packet for decoding\n");
        return;
    }

    // 2. 循环接收 Frame [cite: 39, 131]
    while (true) {
        ret = avcodec_receive_frame(dec_ctx, frame);

        // 如果是 EAGAIN (需要更多输入) 或 EOF (解码结束) [cite: 138, 139]
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return;
        else if (ret < 0) {
            fprintf(stderr, "Error during decoding\n");
            return;
        }

        printf("Decoded frame: %3d, Resolution: %dx%d, Format: %d\n",
               dec_ctx->frame_number, frame->width, frame->height, frame->format);

        // 3. 写入 YUV 数据 [cite: 45]
        write_yuv_frame(frame, outfile);
    }
}

//ffplay -pixel_format yuv420p -video_size 640x360 out.yuv
int main(int argc, char **argv) {
    const char *filename, *outfilename;
    const AVCodec *codec;
    AVCodecParserContext *parser;
    AVCodecContext *c = nullptr;
    FILE *f, *outfile;
    AVFrame *frame;
    uint8_t inbuf[INBUF_SIZE + AV_INPUT_BUFFER_PADDING_SIZE]; //
    uint8_t *data;
    size_t   data_size;
    int ret;
    AVPacket *pkt;

    if (argc <= 2) {
        fprintf(stderr, "Usage: %s <input file> <output file>\n", argv[0]);
        exit(0);
    }
    filename    = argv[1];
    outfilename = argv[2];

    // 初始化 Packet
    pkt = av_packet_alloc();
    if (!pkt) exit(1);

    // 1. 查找 H.264 解码器 [cite: 26, 51]
    codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        fprintf(stderr, "Codec not found\n");
        exit(1);
    }

    // 2. 初始化 H.264 解析器 [cite: 27, 52]
    parser = av_parser_init(codec->id);
    if (!parser) {
        fprintf(stderr, "Parser not found\n");
        exit(1);
    }

    // 3. 分配上下文 [cite: 28, 53]
    c = avcodec_alloc_context3(codec);
    if (!c) {
        fprintf(stderr, "Could not allocate video codec context\n");
        exit(1);
    }

    // 4. 打开解码器 [cite: 29, 54]
    if (avcodec_open2(c, codec, nullptr) < 0) {
        fprintf(stderr, "Could not open codec\n");
        exit(1);
    }

    // 打开文件
    f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Could not open %s\n", filename);
        exit(1);
    }
    outfile = fopen(outfilename, "wb");
    if (!outfile) {
        exit(1);
    }

    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }

    // 5. 读取与解析循环 [cite: 42]
    while (!feof(f)) {
        // 读取原始裸流数据 [cite: 35]
        data_size = fread(inbuf, 1, INBUF_SIZE, f);
        if (!data_size) break;

        data = inbuf;
        while (data_size > 0) {
            // 解析出一个完整的 Packet [cite: 37, 55]
            // av_parser_parse2 返回使用的字节数
            ret = av_parser_parse2(parser, c, &pkt->data, &pkt->size,
                                   data, data_size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
            if (ret < 0) {
                fprintf(stderr, "Error while parsing\n");
                exit(1);
            }

            data      += ret;
            data_size -= ret;

            // 如果 Packet 大小大于 0，说明解析出了一帧，进行解码
            if (pkt->size)
                decode(c, pkt, frame, outfile);
        }
    }

    // 6. 冲刷解码器 (Flush) [cite: 46, 104]
    // 传入 NULL packet 进入 draining mode
    pkt->data = NULL;
    pkt->size = 0;
    decode(c, pkt, frame, outfile);

    printf("Decoding finished.\n");

    // 资源释放
    fclose(f);
    fclose(outfile);
    av_parser_close(parser);
    avcodec_free_context(&c);
    av_frame_free(&frame);
    av_packet_free(&pkt);

    return 0;
}