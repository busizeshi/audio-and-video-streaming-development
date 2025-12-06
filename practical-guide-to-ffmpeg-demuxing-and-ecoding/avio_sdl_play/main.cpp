/**
 * FFmpeg Custom AVIO + SDL2 Player
 * 基于《7-9-AVIO内存输入模式》文档实现
 */

#include <iostream>
#include <vector>
#include <fstream>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <SDL2/SDL.h>
}

// 模拟网络流的 buffer 大小 (文档建议 4kb，但对于视频流通常稍大一些更好)
#define IO_BUFFER_SIZE 32768

// 自定义用户数据结构 (对应文档中的 opaque) [cite: 863]
// 在实际网络流中，这里可能包含 Socket 句柄或 Session 指针
struct InputStream {
    FILE* fp;
};

// 1. 自定义读取回调函数 [cite: 864, 870]
// 作用：当 FFmpeg 需要数据时，会调用此函数填充 buf
int read_packet(void *opaque, uint8_t *buf, int buf_size) {
    auto *is = (InputStream *)opaque;

    // 模拟网络接收：从文件读取数据
    // 在真实网络场景中，这里替换为 recv(socket, buf, buf_size, 0);
    size_t len = fread(buf, 1, buf_size, is->fp);

    if (len == 0) {
        // 返回 EOF (End of File) 或错误码
        return AVERROR_EOF;
    }
    return (int)len;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <input_file>" << std::endl;
        return -1;
    }

    const char *input_path = argv[1];

    // ==========================================
    // 1. 初始化 SDL
    // ==========================================
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        std::cerr << "Could not initialize SDL - " << SDL_GetError() << std::endl;
        return -1;
    }

    // ==========================================
    // 2. 初始化 AVIO (自定义输入)
    // ==========================================

    // 分配 AVFormatContext
    AVFormatContext *fmt_ctx = avformat_alloc_context();

    // 准备用户数据
    InputStream input_stream{};
    input_stream.fp = fopen(input_path, "rb");
    if (!input_stream.fp) {
        std::cerr << "Cannot open file." << std::endl;
        return -1;
    }

    // 分配 AVIO Buffer (必须使用 av_malloc)
    auto *avio_ctx_buffer = (uint8_t *)av_malloc(IO_BUFFER_SIZE);
    if (!avio_ctx_buffer) return -1;

    // 创建 AVIOContext [cite: 858]
    // write_flag = 0 表示输入 (Buffer用于读)
    AVIOContext *avio_ctx = avio_alloc_context(
        avio_ctx_buffer,
        IO_BUFFER_SIZE,
        0,                  // write_flag
        &input_stream,      // opaque (用户数据)
        read_packet,        // read_packet callback
        nullptr,               // write_packet (输入模式不需要)
        nullptr                // seek (网络流通常不可 seek，设为 nullptr)
    );

    if (!avio_ctx) return -1;

    // 将自定义 IO 上下文挂载到 fmt_ctx [cite: 866]
    fmt_ctx->pb = avio_ctx;

    // 打开输入 (此时 filename 传 nullptr 或 dummy 字符串，因为数据来源由 pb 接管)
    if (avformat_open_input(&fmt_ctx, nullptr, nullptr, nullptr) < 0) {
        std::cerr << "Could not open input" << std::endl;
        return -1;
    }

    // 查找流信息
    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        std::cerr << "Could not find stream info" << std::endl;
        return -1;
    }

    // ==========================================
    // 3. 初始化解码器
    // ==========================================
    int video_stream_idx = -1;
    const AVCodec *codec = nullptr;
    AVCodecParameters *codec_par = nullptr;

    // 寻找视频流
    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_idx = i;
            codec_par = fmt_ctx->streams[i]->codecpar;
            codec = avcodec_find_decoder(codec_par->codec_id);
            break;
        }
    }

    if (video_stream_idx == -1) return -1;

    AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_ctx, codec_par);
    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) return -1;

    // ==========================================
    // 4. 配置 SDL 显示
    // ==========================================
    SDL_Window *window = SDL_CreateWindow("FFmpeg AVIO Player",
                                          SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                          codec_ctx->width, codec_ctx->height,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);

    // 创建纹理：FFmpeg 默认解码出 YUV420P，SDL 可以直接渲染这种格式
    SDL_Texture *texture = SDL_CreateTexture(renderer,
                                             SDL_PIXELFORMAT_IYUV,
                                             SDL_TEXTUREACCESS_STREAMING,
                                             codec_ctx->width, codec_ctx->height);

    // ==========================================
    // 5. 解码循环
    // ==========================================
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    struct SwsContext *sws_ctx = nullptr; // 如果格式不兼容，可能需要转换，这里暂略

    SDL_Event event;
    bool quit = false;

    while (!quit && av_read_frame(fmt_ctx, packet) >= 0) {
        // 处理 SDL 事件
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                quit = true;
            }
        }

        if (packet->stream_index == video_stream_idx) {
            // 发送包给解码器
            int ret = avcodec_send_packet(codec_ctx, packet);
            if (ret < 0) break;

            // 接收解码后的帧
            while (ret >= 0) {
                ret = avcodec_receive_frame(codec_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;

                // --- 渲染到 SDL ---
                // 更新 YUV 纹理
                // frame->data[0] = Y, data[1] = U, data[2] = V
                // frame->linesize 是步长 (stride)
                SDL_UpdateYUVTexture(texture, nullptr,
                                     frame->data[0], frame->linesize[0],
                                     frame->data[1], frame->linesize[1],
                                     frame->data[2], frame->linesize[2]);

                SDL_RenderClear(renderer);
                SDL_RenderCopy(renderer, texture, nullptr, nullptr);
                SDL_RenderPresent(renderer);

                // 简单的帧率控制 (延时 40ms ~ 25fps)
                SDL_Delay(40);
            }
        }
        av_packet_unref(packet);
    }

    // ==========================================
    // 6. 清理资源 [cite: 830, 840]
    // ==========================================
    fclose(input_stream.fp);

    // 注意：avformat_close_input 会尝试释放 pb，但如果是自定义 buffer
    // 有时需要手动处理，标准流程是 av_freep(&fmt_ctx->pb->buffer) 然后 avio_context_free
    if (fmt_ctx) {
        if (fmt_ctx->pb) {
            av_freep(&fmt_ctx->pb->buffer); // 释放 av_malloc 的 buffer [cite: 840]
            avio_context_free(&fmt_ctx->pb); // 释放 avio context
        }
        avformat_close_input(&fmt_ctx);
    }

    av_frame_free(&frame);
    av_packet_free(&packet);
    avcodec_free_context(&codec_ctx);

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}