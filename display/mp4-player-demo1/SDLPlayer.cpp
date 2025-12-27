#include "SDLPlayer.h"

SDLPlayer::~SDLPlayer() {
    close();
}

bool SDLPlayer::init(int width, int height) {
    this->width = width;
    this->height = height;

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SDL_Init error: " << SDL_GetError() << std::endl;
        return false;
    }

    // 使用 SDL_WINDOW_RESIZABLE 允许调整大小，但目前代码逻辑可能不支持动态调整纹理
    window = SDL_CreateWindow("FFmpeg + SDL2 Player",
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              width, height, SDL_WINDOW_SHOWN);
    if (!window) {
        std::cerr << "SDL_CreateWindow error: " << SDL_GetError() << std::endl;
        return false;
    }

    // 开启垂直同步 (SDL_RENDERER_PRESENTVSYNC) 可以防止画面撕裂
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        std::cerr << "SDL_CreateRenderer error: " << SDL_GetError() << std::endl;
        return false;
    }

    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!texture) {
        std::cerr << "SDL_CreateTexture error: " << SDL_GetError() << std::endl;
        return false;
    }

    return true;
}

void SDLPlayer::render(uint8_t *data, int pitch) {
    if (!texture || !renderer || !data) return;

    SDL_UpdateTexture(texture, nullptr, data, pitch);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
}

bool SDLPlayer::handleEvents() {
    SDL_Event event;
    // 使用 while 处理队列中的所有事件，防止 UI 卡顿
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_QUIT:
                return true;
            case SDL_KEYDOWN:
                // 按下 ESC 键退出
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    return true;
                }
                break;
        }
    }
    return false;
}

void SDLPlayer::close() {
    if (texture) {
        SDL_DestroyTexture(texture);
        texture = nullptr;
    }
    if (renderer) {
        SDL_DestroyRenderer(renderer);
        renderer = nullptr;
    }
    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }
    // 注意：如果是多窗口程序，这里 Quit 可能会影响全局，根据情况决定是否调用
    SDL_Quit();
}