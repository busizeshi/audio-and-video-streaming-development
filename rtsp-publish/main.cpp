// --- 1. 封印 SDL 的 main 劫持（必须在最前面） ---
#define SDL_MAIN_HANDLED

#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <mutex>

// 引入 SDL
#include <SDL2/SDL.h>

#include "VideoCapture.h"
#include "AudioCapture.h"

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
}

// --- 全局法宝（共享状态） ---
std::mutex g_video_mutex;
std::vector<uint8_t> g_video_buffer; // 存放最新一帧的像素数据
bool g_frame_ready = false; // 标记是否有新数据
int g_video_width = 640;
int g_video_height = 480;

// SDL 音频设备 ID
SDL_AudioDeviceID g_audio_dev = 0;

int main()
{
    // 初始化网络库
    avformat_network_init();

    std::cout << "========================================" << std::endl;
    std::cout << "    Stream Artifact: Water Mirror (SDL) " << std::endl;
    std::cout << "========================================" << std::endl;

    // --- 2. 初始化 SDL (建立水镜) ---
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0)
    {
        std::cerr << "[Error] SDL Init failed: " << SDL_GetError() << std::endl;
        return -1;
    }

    // 创建窗口
    SDL_Window* window = SDL_CreateWindow("Han Li's Stream Preview",
                                          SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          g_video_width, g_video_height,
                                          SDL_WINDOW_SHOWN);
    if (!window)
    {
        std::cerr << "[Error] Window creation failed: " << SDL_GetError() << std::endl;
        return -1;
    }

    // 创建渲染器
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer)
    {
        std::cerr << "[Error] Renderer creation failed: " << SDL_GetError() << std::endl;
        return -1;
    }

    // 创建纹理 (Texture)
    // 摄像头输出是 YUYV422，对应 SDL 的 YUY2 格式
    // 使用 STREAMING 模式，因为我们要频繁更新它
    SDL_Texture* texture = SDL_CreateTexture(renderer,
                                             SDL_PIXELFORMAT_YUY2,
                                             SDL_TEXTUREACCESS_STREAMING,
                                             g_video_width, g_video_height);
    if (!texture)
    {
        std::cerr << "[Error] Texture creation failed: " << SDL_GetError() << std::endl;
        return -1;
    }

    // --- 3. 初始化 SDL 音频播放 (耳返) ---
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = 44100;
    want.format = AUDIO_S16SYS; // 与采集格式保持一致 (S16)
    want.channels = 2;
    want.samples = 1024;
    want.callback = nullptr; // 使用 Queue 模式，不需要回调

    g_audio_dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (g_audio_dev == 0)
    {
        std::cerr << "[Warn] Failed to open audio playback device: " << SDL_GetError() << std::endl;
    }
    else
    {
        SDL_PauseAudioDevice(g_audio_dev, 0); // 开始播放（静音状态等待数据）
        std::cout << "[Info] Audio playback started." << std::endl;
    }

    // 预分配视频缓存 (YUY2 = 2 bytes per pixel)
    g_video_buffer.resize(g_video_width * g_video_height * 2);

    // --- 4. 启动采集 ---
    VideoCapture videoCap;
    AudioCapture audioCap;

    // 启动视频
    std::string videoDevice = "Integrated Camera";
    if (videoCap.open(videoDevice, g_video_width, g_video_height, 30))
    {
        videoCap.start([](AVFrame* frame)
        {
            // --- 采集线程 ---
            // 将数据拷贝到全局缓存
            std::lock_guard<std::mutex> lock(g_video_mutex);

            // 处理 Padding: 逐行拷贝
            // YUYV422 packed mode: data[0] is everything
            int bpp = 2; // Bytes per pixel
            int widthBytes = g_video_width * bpp;

            for (int i = 0; i < g_video_height; i++)
            {
                // 目标地址
                uint8_t* dst = g_video_buffer.data() + i * widthBytes;
                // 源地址 (注意 linesize)
                uint8_t* src = frame->data[0] + i * frame->linesize[0];
                memcpy(dst, src, widthBytes);
            }

            g_frame_ready = true;

            // 必须释放
            av_frame_unref(frame);
            av_frame_free(&frame);
        });
    }
    else
    {
        std::cerr << "[Error] Video Open Failed!" << std::endl;
    }

    // 启动音频
    // 使用你的 ID
    std::string audioDevice =
        "@device_cm_{33D9A762-90C8-11D0-BD43-00A0C911CE86}\\wave_{C40D8240-6E1C-46FE-A805-A1BE8301CB36}";
    if (audioCap.open(audioDevice, 2, 44100))
    {
        audioCap.start([](AVFrame* frame)
        {
            // --- 音频采集线程 ---
            // 直接塞给 SDL 播放队列
            if (g_audio_dev != 0)
            {
                // 计算字节数: samples * channels * bytes_per_sample
                int data_size = frame->nb_samples * frame->channels * 2; // S16 = 2 bytes
                SDL_QueueAudio(g_audio_dev, frame->data[0], data_size);
            }

            av_frame_unref(frame);
            av_frame_free(&frame);
        });
    }
    else
    {
        std::cerr << "[Warn] Audio Open Failed!" << std::endl;
    }

    // --- 5. 主循环 (Main Loop) ---
    // 类似于护法，主线程必须在此死守，处理 SDL 事件
    bool running = true;
    SDL_Event event;

    while (running)
    {
        // 处理事件 (关闭窗口等)
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_QUIT)
            {
                running = false;
            }
        }

        // 检查是否有新画面
        {
            std::lock_guard<std::mutex> lock(g_video_mutex);
            if (g_frame_ready)
            {
                // 更新纹理 (将内存上传显存)
                SDL_UpdateTexture(texture, nullptr, g_video_buffer.data(), g_video_width * 2);
                g_frame_ready = false;
            }
        }

        // 渲染三部曲
        SDL_RenderClear(renderer); // 清屏
        SDL_RenderCopy(renderer, texture, nullptr, nullptr); // 贴图
        SDL_RenderPresent(renderer); // 显示

        // 稍微休息一下，避免烧坏 CPU (约 60fps)
        SDL_Delay(16);
    }

    // --- 6. 收功 ---
    std::cout << "[Main] Closing Water Mirror..." << std::endl;
    videoCap.stop();
    audioCap.stop();

    SDL_CloseAudioDevice(g_audio_dev);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
