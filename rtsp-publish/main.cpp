#include <iostream>
#include "SDL.h"
#include <fstream>
#include <vector>
#include <windows.h>
#include <consoleapi2.h>
#include <chrono>
#include <thread>

#include "VideoCapture.h"
#include "ConfigManager.h"
#include "SDLViewer.h"


int main(int argc, char *argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    setbuf(stdout, nullptr);

    ConfigManager config;
    if (!config.loadConfig(R"(D:\dev\cxx\audio-and-video-streaming-development\rtsp-publish\config.properties)")) {
        std::cerr << "Failed to load config.properties" << std::endl;
        return -1;
    }

    // 1. 初始化 SDL 预览
    SDLViewer viewer;
    if (!viewer.init("Local Preview",
                     config.getInt("video_width"),
                     config.getInt("video_height"))) {
        return -1;
    }
    viewer.start(); // 启动 SDL 渲染线程

    // 2. 初始化 H264 编码器 (假设)
    // H264Encoder encoder;
    // encoder.init(width, height, fps, ...);
    // encoder.start(); // 启动编码线程

    // --- 初始化生产者 ---
    VideoCapture capture;
    if (!capture.open(config)) {
        std::cerr << "Failed to open camera" << std::endl;
        return -1;
    }

    std::cout << "Starting capture. Press 'q' to quit." << std::endl;

    // --- 核心：帧分发 ---
    // 这个 Lambda 运行在 VideoCapture 的线程中
    capture.start([&](AVFrame *frame) {

        // 你的 VideoCapture 回调给了你一个 frame (ref count=1)
        // 我们有两个消费者，所以需要两个引用

        // 1. 为 SDLViewer 创建一个新引用
        AVFrame *frame_for_sdl = av_frame_alloc();
        if (av_frame_ref(frame_for_sdl, frame) == 0) {
            // [非阻塞] 推送给 SDL 线程
            viewer.pushFrame(frame_for_sdl);
        } else {
            av_frame_free(&frame_for_sdl);
        }

        // 2. 为 H264Encoder 创建一个新引用 (或者直接传递原始帧)
        AVFrame *frame_for_encoder = av_frame_alloc();
        if (av_frame_ref(frame_for_encoder, frame) == 0) {
            // [阻塞/非阻塞] 推送给编码器线程
            // encoder.pushFrame(frame_for_encoder);
        } else {
            av_frame_free(&frame_for_encoder);
        }

        // 3. 释放 VideoCapture 给你的原始引用 (!!! 关键 !!!)
        // 因为我们已经为所有消费者创建了新的引用，
        // 所以必须释放掉回调给我们的这个引用。
        av_frame_unref(frame);
        av_frame_free(&frame);

        // --- 更高效的优化 ---
        // 如果只有一个消费者（比如编码器）是“必须”的，
        // 而其他消费者（比如预览）是“可选”的，
        // 你可以把原始的 frame (ref count=1) 直接给编码器，
        // 只为 SDL 创建新的 ref。
        /*
        // 1. 为 SDL (可选) 创建新 ref
        AVFrame *frame_for_sdl = av_frame_alloc();
        if (av_frame_ref(frame_for_sdl, frame) == 0) {
            viewer.pushFrame(frame_for_sdl);
        } else {
            av_frame_free(&frame_for_sdl);
        }

        // 2. 将原始 frame (ref count=1) 直接传递给编码器 (必须)
        // 这样编码器就接管了这个引用
        // encoder.pushFrame(frame);
        */

        // [上面两种方式选一种。我注释掉了第二种优化，因为第一种更清晰]

    });

    // ... 等待 'q' 键 ...
    std::cout << "Press 'q' and Enter to quit..." << std::endl;
    char input;
    while (std::cin >> input && input != 'q') {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // --- 清理 ---
    std::cout << "Stopping capture..." << std::endl;
    capture.stop();

    std::cout << "Stopping viewer..." << std::endl;
    viewer.stop();

    // std::cout << "Stopping encoder..." << std::endl;
    // encoder.stop();

    std::cout << "Program ended successfully." << std::endl;
    return 0;
}