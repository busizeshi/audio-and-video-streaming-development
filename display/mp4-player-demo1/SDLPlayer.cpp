#include "SDLPlayer.h"
#include <iostream>

bool SDLPlayer::init(int width, int height) {
    this->width = width;
    this->height = height;
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cout << "SDL_Init error: " << SDL_GetError() << std::endl;
        return false;
    }
    window = SDL_CreateWindow("MP4播放器",
                              SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              width, height, SDL_WINDOW_SHOWN);
    if (!window) {
        std::cout << "SDL_CreateWindow error: " << SDL_GetError() << std::endl;
        return false;
    }
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        std::cout << "SDL_CreateRenderer error: " << SDL_GetError() << std::endl;
        return false;
    }
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!texture) {
        std::cout << "SDL_CreateTexture error: " << SDL_GetError() << std::endl;
        return false;
    }

    return true;
}

void SDLPlayer::render(uint8_t *data, int pitch) {
    if(!texture||!renderer)
        return;
    SDL_UpdateTexture(texture, nullptr, data, pitch);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
}

bool SDLPlayer::handleEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
//        判断 SDL_QUIT 或 SDL_KEYDOWN，如果触发则返回 true (表示要退出了)
        switch (event.type) {
            case SDL_QUIT:
            case SDL_KEYDOWN:
                return true;
        }
    }

    return false;
}

void SDLPlayer::close() {
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

SDLPlayer::~SDLPlayer() {
    close();
}


