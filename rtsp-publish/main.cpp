#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>
#include <thread>
#include <windows.h> // SetConsoleOutputCP

// 引入你的模块头文件
#include "ConfigManager.h"
#include "VideoCapture.h"
#include "AudioCapture.h"
#include "SDLViewer.h"
#include "VideoEncoder.h"
#include "AudioEncoder.h"
#include "RtmpPublisher.h"

int main(int argc, char *argv[]) {
    // 设置控制台编码为 UTF-8，防止日志乱码
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    setbuf(stdout, nullptr);

    // 1. 加载配置
    ConfigManager config;
    // 建议使用相对路径或检查文件是否存在
    std::string configPath = R"(D:\dev\cxx\audio-and-video-streaming-development\rtsp-publish\config.properties)";
    if (!config.loadConfig(configPath)) {
        std::cerr << "Failed to load config.properties from: " << configPath << std::endl;
        return -1;
    }

    // 2. 实例化所有模块
    VideoCapture videoCap;
    AudioCapture audioCap;
    VideoEncoder videoEnc;
    AudioEncoder audioEnc;
    SDLViewer viewer;
    RtmpPublisher publisher;

    // 3. 打开视频采集
    if (!videoCap.open(config)) {
        std::cerr << "Failed to open video capture." << std::endl;
        return -1;
    }

    // 4. 打开音频采集
    // 修正: 之前的 config key 是 audio_capture_name
    std::string audioDev = config.getString("audio_capture_name");
    if (audioDev.empty()) {
        std::cerr << "Config 'audio_capture_name' is empty!" << std::endl;
        // 如果没有配置音频设备，则仅进行视频推流
        std::cout << "Warning: No audio device configured. Continuing with video only streaming." << std::endl;
    } else {
        // 假设麦克风用默认参数: 2通道, 44100Hz
        if (!audioCap.open(audioDev, 2, 44100)) {
            std::cerr << "Failed to open audio capture: " << audioDev << std::endl;
            std::cout << "Warning: Audio capture failed. Continuing with video only streaming." << std::endl;
        }
    }

    // 5. 初始化预览窗口
    if (!viewer.init("Local Preview", videoCap.getWidth(), videoCap.getHeight())) {
        std::cerr << "Failed to init SDL viewer." << std::endl;
        return -1;
    }

    // 6. 初始化视频编码器 (使用采集到的宽高)
    // 码率 2Mbps, 30fps
    if (!videoEnc.init(videoCap.getWidth(), videoCap.getHeight(), 30, 2000000)) {
        std::cerr << "Failed to init video encoder." << std::endl;
        return -1;
    }

    // 7. 初始化音频编码器
    // 输入: S16格式, Stereo, 44100Hz (需与 AudioCapture 匹配)
    // 输出: AAC, Stereo, 44100Hz, 128kbps
    if (!audioEnc.init(44100, AV_SAMPLE_FMT_S16, AV_CH_LAYOUT_STEREO,
                       44100, 2, 128000)) {
        std::cerr << "Failed to init audio encoder." << std::endl;
        return -1;
    }

    // 8. 初始化推流器
    std::string rtmpUrl = config.getString("rtmp_push_url");
    if (rtmpUrl.empty()) {
        std::cerr << "Config 'rtmp_push_url' is empty!" << std::endl;
        return -1;
    }
    if (!publisher.init(rtmpUrl)) {
        std::cerr << "Failed to init publisher." << std::endl;
        std::cout << "Warning: Failed to connect to RTMP server. Check network connectivity and server status." << std::endl;
        // 程序将继续运行，但不会推流
    }

    // 9. 添加流信息 (必须在 encoder init 之后)
    // 注意：VideoEncoder 的 Timebase 已经在内部设为 1/fps (1/30)
    publisher.addVideoStream(videoEnc.getCodecParameters(), videoEnc.getTimebase());
    // 注意：AudioEncoder (AAC) 的 Timebase 通常是 1/sample_rate
    publisher.addAudioStream(audioEnc.getCodecParameters(), {1, 44100});

    // 10. 打开推流网络连接
    if (!publisher.start()) {
        std::cerr << "Failed to start publisher (connect to server)." << std::endl;
        std::cout << "Warning: Failed to connect to RTMP server. Check network connectivity and server status." << std::endl;
        // 程序将继续运行，但不会推流
    }

    // ==========================================
    // 11. 绑定回调 (核心数据流向)
    // ==========================================

    // [视频路径]: Capture -> (1.SDL预览) & (2.编码 -> 推流)
    videoCap.start([&](AVFrame *frame) {
        // 分支1: 给预览 (clone一份，因为 Viewer 是异步队列，需要拥有独立的 frame 引用)
        AVFrame *viewFrame = av_frame_clone(frame);
        if (viewFrame) {
            viewer.pushFrame(viewFrame);
        }

        // 分支2: 给编码
        // VideoEncoder::encodeFrame 内部做了 sws_scale (深拷贝)，所以直接传 frame 是安全的
        videoEnc.encodeFrame(frame);

        // 释放 Capture 传来的原始引用
        av_frame_unref(frame);
        av_frame_free(&frame);
    });

    // 视频编码完成回调: Encoder -> Publisher
    videoEnc.setCallback([&](AVPacket *packet) {
        publisher.pushVideoPacket(packet);
    });

    // [音频路径]: Capture -> 编码 -> 推流
    // 只有在音频设备成功打开的情况下才启动音频采集线程
    if (!audioDev.empty()) {
        audioCap.start([&](AVFrame *frame) {
            // AudioEncoder 内部有 FIFO 缓冲，会拷贝数据，所以直接传 frame 安全
            audioEnc.encodeFrame(frame);

            // 释放 Capture 传来的引用
            av_frame_unref(frame);
            av_frame_free(&frame);
        });

        // 音频编码完成回调: Encoder -> Publisher
        audioEnc.setCallback([&](AVPacket *packet) {
            publisher.pushAudioPacket(packet);
        });
    }

    // 12. 启动预览线程
    viewer.start();

    // 13. 主循环
    std::cout << "[MAIN] Running... Press Ctrl+C to stop." << std::endl;
    // 处理SDL事件循环，检测窗口关闭事件
    SDL_Event event;
    while (true) {
        // 处理SDL事件，检查是否需要退出
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                std::cout << "[MAIN] SDL window closed, shutting down..." << std::endl;
                goto cleanup; // 跳出循环进行清理
            }
        }
        
        // 检查RTMP连接状态
        if (rtmpUrl.empty() == false && publisher.isConnected() == false) {
            std::cout << "[MAIN] RTMP connection lost." << std::endl;
            // 可以选择在这里尝试重连或者继续运行
        }
        
        // 短暂休眠以减少CPU占用
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
cleanup:
    // 程序结束前进行资源清理
    std::cout << "[MAIN] Cleaning up resources..." << std::endl;
    return 0;
}