//
// Created by jwd on 2025/11/27.
//

#include "SDLViewer.h"
#include <iostream>

SDLViewer::SDLViewer() {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "Failed to init SDL: " << SDL_GetError() << std::endl;
    }
}

SDLViewer::~SDLViewer() {
    stop();
    SDL_Quit();
}

bool SDLViewer::init(const std::string &title, int width, int height) {
    window_ = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width, height, 0);
    if (!window_) {
        std::cerr << "Failed to create window: " << SDL_GetError() << std::endl;
        return false;
    }

    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer_) {
        std::cerr << "Failed to create renderer: " << SDL_GetError() << std::endl;
        return false;
    }

    // 关键：创建 NV12 格式的纹理
    nv12_texture_ = SDL_CreateTexture(
            renderer_,
            SDL_PIXELFORMAT_NV12, // 明确指定为 NV12
            SDL_TEXTUREACCESS_STREAMING,
            width,
            height
    );
    if (!nv12_texture_) {
        std::cerr << "Failed to create texture: " << SDL_GetError() << std::endl;
        return false;
    }

    return true;
}

void SDLViewer::start() {
    if (is_running_) return;
    is_running_ = true;
    render_thread_ = std::thread(&SDLViewer::runLoop, this);
}

void SDLViewer::stop() {
    is_running_ = false;
    frame_queue_.stop(); // 停止队列
    if (render_thread_.joinable()) {
        render_thread_.join();
    }

    if (nv12_texture_) SDL_DestroyTexture(nv12_texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
}

void SDLViewer::pushFrame(AVFrame *frame) {
    if (!is_running_) {
        av_frame_unref(frame);
        av_frame_free(&frame);
        return;
    }

    // 使用 push_latest 策略：
    // 只渲染最新帧，防止因渲染慢导致延迟累积
    // 这对预览非常重要
    frame_queue_.push_latest(frame);
}

void SDLViewer::runLoop() {
    SDL_Event event;
    AVFrame *frame = nullptr;

    while (is_running_) {
        // 检查退出事件
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                is_running_ = false;
            }
        }

        // 阻塞等待新帧
        if (!frame_queue_.pop(frame)) {
            if (!is_running_) break; // 队列已停止
            continue;
        }

        // --- 渲染 NV12 帧 ---

        // 锁定纹理以获取像素指针
        void *pixels;
        int pitch;
        if (SDL_LockTexture(nv12_texture_, nullptr, &pixels, &pitch) < 0) {
            std::cerr << "Failed to lock texture: " << SDL_GetError() << std::endl;
            // 别忘了释放帧
            av_frame_unref(frame);
            av_frame_free(&frame);
            continue;
        }

        // 拷贝 Y 平面
        // AVFrame 的 linesize[0] (Y 平面的 stride) 可能不等于 SDL texture 的 pitch
        if (frame->linesize[0] == pitch) {
            // 快速路径：如果 stride 一致，直接 memcpy
            memcpy(pixels, frame->data[0], frame->height * frame->linesize[0]);
        } else {
            // 慢速路径：逐行拷贝
            uint8_t *src = frame->data[0];
            uint8_t *dst = (uint8_t *) pixels;
            for (int i = 0; i < frame->height; ++i) {
                memcpy(dst, src, frame->width); // NV12 的 Y 平面宽度 = 帧宽
                src += frame->linesize[0];
                dst += pitch;
            }
        }

        // 拷贝 UV 平面
        // NV12 的 UV 平面在 SDL 纹理中紧跟 Y 平面之后
        uint8_t *uv_dst = (uint8_t *) pixels + frame->height * pitch;
        uint8_t *uv_src = frame->data[1];
        int uv_pitch = pitch; // NV12 的 UV pitch 通常与 Y pitch 相同
        int uv_height = frame->height / 2;
        int uv_linesize = frame->linesize[1]; // UV 平面的 stride

        if (uv_linesize == uv_pitch) {
            // 快速路径
            memcpy(uv_dst, uv_src, uv_height * uv_linesize);
        } else {
            // 慢速路径
            for (int i = 0; i < uv_height; ++i) {
                memcpy(uv_dst, uv_src, frame->width); // NV12 的 UV 平面宽度 (字节数) = 帧宽
                uv_src += uv_linesize;
                uv_dst += uv_pitch;
            }
        }

        SDL_UnlockTexture(nv12_texture_);

        // --- 渲染 ---
        SDL_RenderClear(renderer_);
        SDL_RenderCopy(renderer_, nv12_texture_, nullptr, nullptr);
        SDL_RenderPresent(renderer_);

        // 释放帧 (!!! 关键 !!!)
        // 这是 SDLViewer 拥有的引用，用完必须释放
        av_frame_unref(frame);
        av_frame_free(&frame);
    }
}
