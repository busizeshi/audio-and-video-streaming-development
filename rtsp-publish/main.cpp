#define SDL_MAIN_HANDLED // 禁止 SDL 劫持 main

#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>

// SDL 封装类
#include "SDLDisplay.h"
#include "VideoCapture.h"
#include "AudioCapture.h"

// FFmpeg
extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
}

// 全局视频缓冲和状态
std::mutex g_video_mutex;
std::vector<uint8_t> g_video_buffer;
bool g_frame_ready = false;

// 视频参数
int g_video_width = 640;
int g_video_height = 480;

int main()
{
    avformat_network_init();

    std::cout << "========================================" << std::endl;
    std::cout << "    Stream Artifact: Water Mirror (SDL) " << std::endl;
    std::cout << "========================================" << std::endl;

    // --- 初始化 SDL (视频 + 音频) ---
    SDLDisplay display;
    if (!display.initialize(g_video_width, g_video_height))
    {
        std::cerr << "[Error] SDL Display init failed" << std::endl;
        return -1;
    }

    // 预分配视频缓冲 (YUY2)
    g_video_buffer.resize(g_video_width * g_video_height * 2);

    // --- 初始化采集 ---
    VideoCapture videoCap;
    AudioCapture audioCap;

    // 启动视频采集
    std::string videoDevice = "Integrated Camera";
    if (videoCap.open(videoDevice, g_video_width, g_video_height, 30))
    {
        videoCap.start([](AVFrame* frame)
        {
            std::lock_guard<std::mutex> lock(g_video_mutex);
            int bpp = 2;
            int lineBytes = g_video_width * bpp;
            for (int i = 0; i < g_video_height; i++)
            {
                memcpy(g_video_buffer.data() + i * lineBytes, frame->data[0] + i * frame->linesize[0], lineBytes);
            }
            g_frame_ready = true;
            av_frame_unref(frame);
            av_frame_free(&frame);
        });
    }
    else
    {
        std::cerr << "[Error] Video open failed" << std::endl;
    }

    // 启动音频采集
    std::string audioDevice =
        "@device_cm_{33D9A762-90C8-11D0-BD43-00A0C911CE86}\\wave_{C40D8240-6E1C-46FE-A805-A1BE8301CB36}";
    if (audioCap.open(audioDevice, 2, 44100))
    {
        audioCap.start([&display](AVFrame* frame)
        {
            int dataSize = frame->nb_samples * frame->channels * 2; // S16 = 2 bytes
            display.queueAudio(frame->data[0], dataSize);
            av_frame_unref(frame);
            av_frame_free(&frame);
        });
    }
    else
    {
        std::cerr << "[Warn] Audio open failed" << std::endl;
    }

    // --- 主循环 ---
    while (display.isRunning())
    {
        display.pollEvents();

        // 更新视频纹理
        {
            std::lock_guard<std::mutex> lock(g_video_mutex);
            if (g_frame_ready)
            {
                display.updateVideo(g_video_buffer);
                g_frame_ready = false;
            }
        }

        display.render();

        // 控制帧率 ~60fps
        SDL_Delay(16);
    }

    // --- 清理 ---
    std::cout << "[Main] Closing Water Mirror..." << std::endl;
    videoCap.stop();
    audioCap.stop();

    display.destroy();
    SDL_Quit();

    return 0;
}
