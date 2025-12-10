#include <iostream>
#include <fstream>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
}

// 配置参数 (请根据你的YUV文件实际情况修改)
constexpr int WIDTH = 1920;
constexpr int HEIGHT = 1080;
constexpr int FPS = 25;
const int BITRATE = 400000; // 400kbps
const char* INPUT_FILE = R"(D:\cxx\audio-and-video-streaming-development\resource\output.yuv)";
const char* OUTPUT_FILE = "../output.h264";

// 辅助函数：将编码后的 Packet 写入文件
void write_packet(FILE* f, AVPacket* pkt) {
    if (pkt->data && pkt->size > 0) {
        fwrite(pkt->data, 1, pkt->size, f);
    }
}

// 核心编码循环
void encode(AVCodecContext* enc_ctx, const AVFrame* frame, AVPacket* pkt, FILE* outfile) {
    int ret;

    // 1. 发送原始帧给编码器
    // frame 为 NULL 时表示发送 Flush 信号，告诉编码器没有新数据了
    ret = avcodec_send_frame(enc_ctx, frame);
    if (ret < 0) {
        std::cerr << "Error sending a frame for encoding" << std::endl;
        exit(1);
    }

    // 2. 循环接收编码后的数据包
    while (true) {
        ret = avcodec_receive_packet(enc_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            // EAGAIN: 需要更多输入帧才能输出 Packet
            // EOF: 编码结束
            return;
        } else if (ret < 0) {
            std::cerr << "Error during encoding" << std::endl;
            exit(1);
        }

        // 写入文件
        std::cout << "Write packet: pts=" << pkt->pts << " size=" << pkt->size << std::endl;
        write_packet(outfile, pkt);

        // 释放 packet 引用，为下一次使用重置
        av_packet_unref(pkt);
    }
}

int main() {
    const AVCodec* codec = nullptr;
    AVCodecContext* c = nullptr;
    FILE* f_in = nullptr;
    FILE* f_out = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* pkt = nullptr;
    int ret;

    // 1. 查找 H.264 编码器 (libx264)
    codec = avcodec_find_encoder_by_name("libx264");
    if (!codec) {
        std::cerr << "Codec 'libx264' not found" << std::endl;
        return 1;
    }

    // 2. 分配编码器上下文
    c = avcodec_alloc_context3(codec);
    if (!c) {
        std::cerr << "Could not allocate video codec context" << std::endl;
        return 1;
    }

    // 3. 设置编码参数
    c->bit_rate = BITRATE;
    c->width = WIDTH;
    c->height = HEIGHT;
    // 时间基数 (Timebase) 和 帧率
    c->time_base = (AVRational){1, FPS};
    c->framerate = (AVRational){FPS, 1};

    // 关键帧间隔 (GOP size), 这里设为 10 帧一个 I 帧
    c->gop_size = 10;
    c->max_b_frames = 1;
    c->pix_fmt = AV_PIX_FMT_YUV420P;

    //如果是 libx264，通常需要设置 preset (速度/质量权衡)
    if (codec->id == AV_CODEC_ID_H264) {
        av_opt_set(c->priv_data, "preset", "slow", 0);
    }

    // 4. 打开编码器
    ret = avcodec_open2(c, codec, nullptr);
    if (ret < 0) {
        std::cerr << "Could not open codec" << std::endl;
        return 1;
    }

    // 5. 打开输入输出文件
    f_in = fopen(INPUT_FILE, "rb");
    if (!f_in) {
        std::cerr << "Could not open " << INPUT_FILE << std::endl;
        return 1;
    }
    f_out = fopen(OUTPUT_FILE, "wb");
    if (!f_out) {
        std::cerr << "Could not open " << OUTPUT_FILE << std::endl;
        return 1;
    }

    // 6. 分配 Packet 和 Frame
    pkt = av_packet_alloc();
    frame = av_frame_alloc();
    if (!pkt || !frame) {
        std::cerr << "Could not allocate packet or frame" << std::endl;
        return 1;
    }

    frame->format = c->pix_fmt;
    frame->width = c->width;
    frame->height = c->height;

    // 为 Frame 分配数据缓冲区
    ret = av_frame_get_buffer(frame, 32);
    if (ret < 0) {
        std::cerr << "Could not allocate the video frame data" << std::endl;
        return 1;
    }

    // 计算每帧 YUV 大小 (Y=w*h, U=w/2*h/2, V=w/2*h/2 => Total = w*h*1.5)
    int y_size = c->width * c->height;
    int uv_size = y_size / 4;
    int frame_idx = 0;

    // 7. 循环读取 YUV 数据并编码
    // 注意：这里假设输入文件是紧凑的 YUV420P (无 padding)
    while (true) {
        // 确保 Frame 数据可写
        ret = av_frame_make_writable(frame);
        if (ret < 0) break;

        // 读取 Y 分量
        if (fread(frame->data[0], 1, y_size, f_in) != static_cast<size_t>(y_size)) break;
        // 读取 U 分量
        if (fread(frame->data[1], 1, uv_size, f_in) != static_cast<size_t>(uv_size)) break;
        // 读取 V 分量
        if (fread(frame->data[2], 1, uv_size, f_in) != static_cast<size_t>(uv_size)) break;

        frame->pts = frame_idx++;

        // 编码当前帧
        encode(c, frame, pkt, f_out);
    }

    // 8. 冲刷编码器 (Flush)
    // 发送 NULL 告诉编码器已经没有新数据了，把剩余缓存的帧都输出来
    encode(c, nullptr, pkt, f_out);

    // 9. 释放资源
    std::cout << "Encoding finished." << std::endl;
    fclose(f_in);
    fclose(f_out);
    avcodec_free_context(&c);
    av_frame_free(&frame);
    av_packet_free(&pkt);

    return 0;
}