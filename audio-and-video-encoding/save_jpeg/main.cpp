/**
 * YUV 转 JPEG 工具
 * 功能：从 YUV 文件中提取指定的一帧保存为 JPEG
 */

#include <iostream>
#include <cstdio>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
}

// ================= 配置参数 =================
const char *IN_YUV_FILE = "D:\\resource\\input_1280x720.yuv";
const char *OUT_JPG_FILE = "../snapshot.jpg";
const int WIDTH = 1280;
const int HEIGHT = 720;
const int EXTRACT_FRAME_INDEX = 50; // 提取第 50 帧
// ===========================================

int main() {
    // 1. 打开 YUV 文件
    FILE *f_yuv = fopen(IN_YUV_FILE, "rb");
    if (!f_yuv) {
        perror("无法打开 YUV 文件");
        return -1;
    }

    // 计算一帧 YUV420P 的大小
    // Y = W*H, U = W/2*H/2, V = W/2*H/2
    // Total = W*H * 1.5
    int y_size = WIDTH * HEIGHT;
    int frame_size = y_size * 3 / 2;

    // 2. 跳到指定帧 (Seek)
    // fseek 可能会受到 2GB 文件限制，生产环境建议用 fseeko64
    long offset = static_cast<long>(EXTRACT_FRAME_INDEX) * frame_size;
    if (fseek(f_yuv, offset, SEEK_SET) != 0) {
        printf("无法跳到第 %d 帧 (文件可能太小)\n", EXTRACT_FRAME_INDEX);
        fclose(f_yuv);
        return -1;
    }

    // 3. 查找 MJPEG 编码器
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    if (!codec) {
        printf("找不到 MJPEG 编码器\n");
        return -1;
    }

    AVCodecContext *c = avcodec_alloc_context3(codec);
    if (!c) return -1;

    // 4. 配置编码器参数
    c->bit_rate = 400000; // 影响图片质量/大小
    c->width = WIDTH;
    c->height = HEIGHT;
    // 时间基对于单张图片不重要，但必须设置
    c->time_base = (AVRational){1, 25};
    c->framerate = (AVRational){25, 1};

    // 【关键点】JPEG 通常使用 YUVJ420P (Full Range 0-255)
    // 如果用 YUV420P，FFmpeg 会自动转换或者标记 Range
    c->pix_fmt = AV_PIX_FMT_YUVJ420P;

    if (avcodec_open2(c, codec, NULL) < 0) {
        printf("无法打开编码器\n");
        return -1;
    }

    // 5. 准备 Frame 和 Packet
    AVFrame *frame = av_frame_alloc();
    frame->format = c->pix_fmt;
    frame->width = c->width;
    frame->height = c->height;

    // 分配 frame 内部的 buffer
    if (av_frame_get_buffer(frame, 32) < 0) return -1;

    AVPacket *pkt = av_packet_alloc();

    // 6. 读取 YUV 数据到 Frame
    // 注意：这里简单假设 YUV 是紧凑存储的。如果 linesize != width，需要逐行 copy
    // 对于 YUVJ420P / YUV420P：
    // Data[0] = Y, Data[1] = U, Data[2] = V
    fread(frame->data[0], 1, y_size, f_yuv); // Y
    fread(frame->data[1], 1, y_size / 4, f_yuv); // U
    fread(frame->data[2], 1, y_size / 4, f_yuv); // V

    // 7. 发送给编码器
    int ret = avcodec_send_frame(c, frame);
    if (ret < 0) {
        printf("发送帧失败\n");
        return -1;
    }

    // 8. 接收编码后的 JPEG 数据
    // JPEG 是一帧一包，所以不需要 while 循环，调用一次 receive 即可
    ret = avcodec_receive_packet(c, pkt);
    if (ret == 0) {
        // 9. 直接写入文件
        FILE *f_jpg = fopen(OUT_JPG_FILE, "wb");
        if (f_jpg) {
            fwrite(pkt->data, 1, pkt->size, f_jpg);
            fclose(f_jpg);
            printf("? 成功保存图片: %s (大小: %d bytes)\n", OUT_JPG_FILE, pkt->size);
        }
        av_packet_unref(pkt);
    } else {
        printf("编码失败或无数据输出: %s\n", av_err2str(ret));
    }

    // 清理
    fclose(f_yuv);
    avcodec_free_context(&c);
    av_frame_free(&frame);
    av_packet_free(&pkt);

    return 0;
}
