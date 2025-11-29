#include <iostream>
#include <thread>
#include <windows.h>
#include <objbase.h>          // COM 组件
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include "ConfigManager.h"
#include "VideoCapture.h"
#include "AudioCapture.h"
#include "SDLViewer.h"
#include "VideoEncoder.h"
#include "AudioEncoder.h"
#include "RtmpPublisher.h"

// 辅助函数：列出 dshow 设备
// 请务必在控制台输出中找到类似 "Microphone (Realtek High Definition Audio)" 的名称
void listDshowDevices() {
    std::cout << "\n================= DShow Device List Start =================" << std::endl;
    std::cout << "Please copy the exact device name (excluding 'video=' or 'audio=') into config.properties" << std::endl;
    AVFormatContext *pFormatCtx = avformat_alloc_context();
    AVDictionary* options = NULL;
    av_dict_set(&options, "list_devices", "true", 0);
    const AVInputFormat *iformat = av_find_input_format("dshow");
    // 这行代码会把设备列表打印到 stderr，请注意观察控制台的红色/非标准输出
    avformat_open_input(&pFormatCtx, "video=dummy", iformat, &options);
    av_dict_free(&options);
    avformat_close_input(&pFormatCtx);
    std::cout << "================== DShow Device List End ==================\n" << std::endl;
}

int main(int argc, char *argv[]) {
    // 0. Windows 平台基础初始化 (对音频采集非常重要)
    SetConsoleOutputCP(CP_UTF8);
    HRESULT hr = CoInitialize(nullptr);
    if (FAILED(hr)) {
        std::cerr << "[System] Warning: CoInitialize failed." << std::endl;
    }

    avdevice_register_all();
    avformat_network_init();

    // 调试：列出设备，帮助你核对配置文件
    listDshowDevices();

    ConfigManager config;
    std::string configPath = R"(../config.properties)";
    if (!config.loadConfig(configPath)) {
        std::cerr << "[System] Error: Cannot load config.properties" << std::endl;
        return -1;
    }

    VideoCapture videoCap;
    AudioCapture audioCap;
    VideoEncoder videoEnc;
    AudioEncoder audioEnc;
    SDLViewer viewer;
    RtmpPublisher publisher;

    // 1. 启动视频采集
    std::cout << "[Step 1] Opening Video Capture..." << std::endl;
    if (!videoCap.open(config)) {
        std::cerr << "[System] Failed to open video capture." << std::endl;
        return -1;
    }

    // 2. 启动音频采集
    std::string audioDev = config.getString("audio_capture_name");
    bool audioEnabled = false;

    std::cout << "[Step 2] Configuring Audio..." << std::endl;
    if (audioDev.empty()) {
        std::cerr << "[Warning] 'audio_capture_name' is empty in config. Audio disabled." << std::endl;
    } else {
        std::cout << "[Audio] Attempting to open device: [" << audioDev << "]" << std::endl;
        // 尝试以双通道 44100Hz 打开，如果设备不支持该参数，open 内部可能会失败
        if (audioCap.open(audioDev, 2, 44100)) {
            audioEnabled = true;
            std::cout << "[Audio] Device opened successfully!" << std::endl;
            std::cout << "[Audio] Hardware Params: " << audioCap.getSampleRate() << "Hz, "
                      << audioCap.getChannels() << "ch, Format: " << audioCap.getSampleFormat() << std::endl;
        } else {
            std::cerr << "[Error] Failed to open audio device. Check the name strictly." << std::endl;
            std::cerr << "        Make sure the device is not occupied by another app." << std::endl;
        }
    }

    // 3. SDL 预览
    if (!viewer.init("Local Preview", videoCap.getWidth(), videoCap.getHeight())) {
        return -1;
    }

    // 4. 初始化视频编码器
    if (!videoEnc.init(videoCap.getWidth(), videoCap.getHeight(), 30, 2000000)) {
        return -1;
    }

    // 5. 初始化音频编码器
    if (audioEnabled) {
        // 使用采集设备实际得到的参数来初始化编码器，防止参数不匹配导致的杂音或失败
        if (!audioEnc.init(audioCap.getSampleRate(), audioCap.getSampleFormat(),
                           audioCap.getChannelLayout(),
                           44100, 2, 128000)) {
            std::cerr << "[Error] Audio Encoder init failed." << std::endl;
            audioEnabled = false;
        }
    }

    // 6. RTMP 推流初始化
    std::string rtmpUrl = config.getString("rtmp_push_url");
    if (publisher.init(rtmpUrl)) {
        publisher.addVideoStream(videoEnc.getCodecParameters(), videoEnc.getTimebase());
        if (audioEnabled) {
            publisher.addAudioStream(audioEnc.getCodecParameters(), audioEnc.getTimebase());
        }
        publisher.start();
    } else {
        std::cerr << "[Warning] RTMP init failed (Network issue?). Proceeding without publishing." << std::endl;
    }

    // --- 视频编码专用线程 ---
    std::queue<AVFrame*> encQueue;
    std::mutex encMutex;
    std::condition_variable encCv;
    std::atomic<bool> encThreadRunning{true};

    std::thread encThread([&]() {
        while (encThreadRunning) {
            AVFrame* frame = nullptr;
            {
                std::unique_lock<std::mutex> lock(encMutex);
                encCv.wait(lock, [&] { return !encQueue.empty() || !encThreadRunning; });

                if (!encThreadRunning && encQueue.empty()) break;
                if (encQueue.empty()) continue;

                frame = encQueue.front();
                encQueue.pop();
            }

            if (frame) {
                videoEnc.encodeFrame(frame);
                av_frame_unref(frame);
                av_frame_free(&frame);
            }
        }
    });

    // 7. 绑定回调
    videoCap.start([&](AVFrame *frame) {
        // 1. 发送给预览
        AVFrame *viewFrame = av_frame_clone(frame);
        if (viewFrame) viewer.pushFrame(viewFrame);

        // 2. 发送给编码队列
        {
            std::lock_guard<std::mutex> lock(encMutex);
            if (encQueue.size() > 30) {
                // 如果队列积压，说明编码太慢，或者网络阻塞导致回调慢
                // 打印一次警告，避免刷屏
                static int log_limit = 0;
                if (log_limit++ % 100 == 0) {
                    std::cout << "[System] Video encoding queue overloaded (" << encQueue.size() << "), dropping frame!" << std::endl;
                }

                av_frame_unref(frame);
                av_frame_free(&frame);
            } else {
                encQueue.push(frame);
                encCv.notify_one();
            }
        }
    });

    videoEnc.setCallback([&](AVPacket *pkt) {
        publisher.pushVideoPacket(pkt);
    });

    if (audioEnabled) {
        std::cout << "[System] Starting Audio Capture Loop..." << std::endl;
        audioCap.start([&](AVFrame *frame) {
            // 调试日志：确认音频数据确实进来了
            static int a_count = 0;
            if (++a_count % 100 == 0) { // 每100帧（约2秒）打印一次
                std::cout << "[Debug] Audio frame captured (pts: " << frame->pts << ")" << std::endl;
            }

            audioEnc.encodeFrame(frame);
            av_frame_unref(frame);
            av_frame_free(&frame);
        });
        audioEnc.setCallback([&](AVPacket *pkt) {
            publisher.pushAudioPacket(pkt);
        });
    }

    viewer.start();

    // 8. 主循环
    std::cout << "[System] Main loop running. Press closing window to stop." << std::endl;
    SDL_Event event;
    while (true) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) goto cleanup;
        }

        // 可在此处检查 RTMP 状态
        if (!publisher.isConnected() && !rtmpUrl.empty()) {
            // std::cout << "RTMP disconnected..." << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    cleanup:
    std::cout << "[System] Shutting down..." << std::endl;
    videoCap.stop();
    audioCap.stop();

    {
        std::lock_guard<std::mutex> lock(encMutex);
        encThreadRunning = false;
    }
    encCv.notify_all();
    if (encThread.joinable()) encThread.join();

    // 清理残留帧
    while (!encQueue.empty()) {
        AVFrame* f = encQueue.front();
        encQueue.pop();
        av_frame_unref(f);
        av_frame_free(&f);
    }

    videoEnc.stop();
    audioEnc.stop();
    publisher.stop();
    viewer.stop();

    CoUninitialize(); // 反初始化 COM
    return 0;
}