//
// Created by jwd on 2025/11/27.
//

#ifndef RTSP_PUBLISH_SDLVIEWER_H
#define RTSP_PUBLISH_SDLVIEWER_H

#include "SDL.h"
#include <string>
#include <thread>
#include "ThreadSafeQueue.h"

extern "C" {
#include <libavutil/time.h>
#include <libavutil/frame.h>
}

class SDLViewer {
public:
    SDLViewer();

    ~SDLViewer();

    // 初始化窗口和 NV12 纹理
    bool init(const std::string &title, int width, int height);

    // 启动渲染线程
    void start();

    // 停止线程并清理
    void stop();

    // [线程安全] 从外部推送一帧 (这个 AVFrame* 必须是新 ref 的)
    void pushFrame(AVFrame *frame);

private:
    // 渲染线程的主循环
    void runLoop();

    SDL_Window *window_ = nullptr;
    SDL_Renderer *renderer_ = nullptr;
    SDL_Texture *nv12_texture_ = nullptr;

    std::thread render_thread_;
    std::atomic<bool> is_running_{false};

    // 帧队列
    ThreadSafeQueue<AVFrame *> frame_queue_;
};


#endif //RTSP_PUBLISH_SDLVIEWER_H
