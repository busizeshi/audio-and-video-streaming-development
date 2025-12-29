/**
 * FFmpeg 6.1 + SDL2 音频播放器示例
 *
 * 编译说明 (Linux/macOS):
 * g++ ffmpeg_audio_player.cpp -o audio_player -std=c++17 \
 * $(pkg-config --cflags --libs libavformat libavcodec libswresample libavutil sdl2)
 *
 * Windows (MSYS2/MinGW):
 * 确保已安装 ffmpeg 和 SDL2 开发库，并链接相应的 .lib/.a 文件
 */

#include <iostream>
#include <vector>
#include <thread>
#include <chrono>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
}

#include <SDL2/SDL.h>

// SDL 音频回调配置
// 我们使用 SDL_QueueAudio 主动推送模式，因此不需要复杂的回调函数
#define SDL_AUDIO_BUFFER_SIZE 4096

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input_file>" << std::endl;
        return -1;
    }

    const char* input_filename = argv[1];

    // ============================
    // 1. 初始化 SDL 音频子系统
    // ============================
    if (SDL_Init(SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        std::cerr << "Could not initialize SDL - " << SDL_GetError() << std::endl;
        return -1;
    }

    // ============================
    // 2. 打开输入文件并查找流信息
    // ============================
    AVFormatContext* format_ctx = nullptr;
    if (avformat_open_input(&format_ctx, input_filename, nullptr, nullptr) < 0) {
        std::cerr << "Could not open input file." << std::endl;
        return -1;
    }

    if (avformat_find_stream_info(format_ctx, nullptr) < 0) {
        std::cerr << "Could not find stream info." << std::endl;
        return -1;
    }

    // ============================
    // 3. 查找音频流和解码器
    // ============================
    int audio_stream_index = -1;
    const AVCodec* codec = nullptr;
    AVCodecParameters* codecpar = nullptr;

    for (unsigned int i = 0; i < format_ctx->nb_streams; i++) {
        if (format_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_index = i;
            codecpar = format_ctx->streams[i]->codecpar;
            codec = avcodec_find_decoder(codecpar->codec_id);
            break;
        }
    }

    if (audio_stream_index == -1 || !codec) {
        std::cerr << "Could not find audio stream or decoder." << std::endl;
        return -1;
    }

    // ============================
    // 4. 初始化解码器上下文
    // ============================
    AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        std::cerr << "Could not allocate codec context." << std::endl;
        return -1;
    }

    if (avcodec_parameters_to_context(codec_ctx, codecpar) < 0) {
        std::cerr << "Could not copy codec params to context." << std::endl;
        return -1;
    }

    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        std::cerr << "Could not open codec." << std::endl;
        return -1;
    }

    // ============================
    // 5. 配置 SDL 音频参数
    // ============================
    SDL_AudioSpec wanted_spec, obtained_spec;
    SDL_zero(wanted_spec);

    // 设置期望的 SDL 播放格式
    // 注意：我们将把 FFmpeg 的输出重采样为这个格式
    wanted_spec.freq = codec_ctx->sample_rate;
    wanted_spec.format = AUDIO_S16SYS; // Signed 16-bit system endian
    wanted_spec.channels = codec_ctx->ch_layout.nb_channels;
    wanted_spec.silence = 0;
    wanted_spec.samples = 1024; // SDL 缓冲区样本数
    wanted_spec.callback = nullptr; // 使用 SDL_QueueAudio，不需要回调

    SDL_AudioDeviceID audio_dev = SDL_OpenAudioDevice(nullptr, 0, &wanted_spec, &obtained_spec, 0);
    if (audio_dev == 0) {
        std::cerr << "Failed to open audio device: " << SDL_GetError() << std::endl;
        return -1;
    }

    // 开启播放（此时队列为空，会静音）
    SDL_PauseAudioDevice(audio_dev, 0);

    // ============================
    // 6. 初始化重采样上下文 (SwrContext)
    // ============================
    // FFmpeg 6.1 推荐使用 swr_alloc_set_opts2 和 AVChannelLayout
    SwrContext* swr_ctx = nullptr;

    // 定义输出给 SDL 的通道布局 (根据获取到的 SDL spec)
    AVChannelLayout out_ch_layout;
    av_channel_layout_default(&out_ch_layout, obtained_spec.channels);

    // 获取输入的通道布局
    // 注意：有些旧文件可能没有设置 ch_layout，需要手动基于 channels 猜测
    if (codec_ctx->ch_layout.u.mask == 0 && codec_ctx->ch_layout.nb_channels == 0) {
        av_channel_layout_default(&codec_ctx->ch_layout, 2); // 默认当作立体声
    }

    int ret = swr_alloc_set_opts2(
            &swr_ctx,
            &out_ch_layout,                 // 输出通道布局
            AV_SAMPLE_FMT_S16,              // 输出格式 (匹配 SDL 的 AUDIO_S16SYS)
            obtained_spec.freq,             // 输出采样率
            &codec_ctx->ch_layout,          // 输入通道布局
            codec_ctx->sample_fmt,          // 输入格式
            codec_ctx->sample_rate,         // 输入采样率
            0, nullptr
    );

    if (ret < 0 || swr_init(swr_ctx) < 0) {
        std::cerr << "Failed to initialize SwrContext" << std::endl;
        return -1;
    }

    // ============================
    // 7. 解码循环
    // ============================
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();

    // 简单的同步策略：
    // 如果 SDL 队列中的数据过多，休眠一会，防止内存暴涨。
    // 这不是最完美的音视频同步（AVSync），但对于单独播放音频足够了。

    while (av_read_frame(format_ctx, packet) >= 0) {
        if (packet->stream_index == audio_stream_index) {

            // 发送 Packet 到解码器
            if (avcodec_send_packet(codec_ctx, packet) == 0) {
                // 从解码器接收 Frame (一个 Packet 可能包含多个 Frame)
                while (avcodec_receive_frame(codec_ctx, frame) == 0) {

                    // 计算输出样本数
                    // swr_get_out_samples 预估需要的缓冲区大小
                    int dst_nb_samples = av_rescale_rnd(
                            swr_get_delay(swr_ctx, codec_ctx->sample_rate) + frame->nb_samples,
                            obtained_spec.freq,
                            codec_ctx->sample_rate,
                            AV_ROUND_UP
                    );

                    // 分配输出缓冲区
                    uint8_t* output_buffer = nullptr;
                    int linesize;
                    av_samples_alloc(&output_buffer, &linesize,
                                     obtained_spec.channels, dst_nb_samples,
                                     AV_SAMPLE_FMT_S16, 1);

                    // 执行重采样
                    int converted_samples = swr_convert(
                            swr_ctx,
                            &output_buffer, dst_nb_samples,
                            (const uint8_t**)frame->data, frame->nb_samples
                    );

                    if (converted_samples > 0) {
                        // 计算转换后的数据字节大小
                        // S16 = 2 bytes per sample
                        int data_size = converted_samples * obtained_spec.channels * 2;

                        // 将数据推入 SDL 播放队列
                        SDL_QueueAudio(audio_dev, output_buffer, data_size);
                    }

                    // 释放临时缓冲区
                    if (output_buffer) {
                        av_freep(&output_buffer);
                    }

                    // 简单的流控：保持队列中有大约 0.5 到 1 秒的数据
                    // 防止解码速度远快于播放速度导致内存耗尽
                    while (SDL_GetQueuedAudioSize(audio_dev) > obtained_spec.freq * obtained_spec.channels * 2) {
                        SDL_Delay(10);
                    }
                }
            }
        }
        av_packet_unref(packet);
    }

    // ============================
    // 8. 清理资源
    // ============================
    // 等待剩余音频播放完毕
    while (SDL_GetQueuedAudioSize(audio_dev) > 0) {
        SDL_Delay(100);
    }

    SDL_CloseAudioDevice(audio_dev);
    SDL_Quit();

    av_packet_free(&packet);
    av_frame_free(&frame);
    swr_free(&swr_ctx);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&format_ctx);
    av_channel_layout_uninit(&out_ch_layout);

    std::cout << "Playback finished." << std::endl;

    return 0;
}