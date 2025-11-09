#include <SDL.h>
#include <stdio.h>

#define FF_QUIT_EVENT    (SDL_USEREVENT + 2)
#undef main

int main(int argc, char* argv[])
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("SDL_Init error: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow("An SDL2 window",
                                          SDL_WINDOWPOS_UNDEFINED,
                                          SDL_WINDOWPOS_UNDEFINED,
                                          640, 480,
                                          SDL_WINDOW_SHOWN | SDL_WINDOW_BORDERLESS);
    if (!window) {
        printf("CreateWindow error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1,
                                                SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        printf("CreateRenderer error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255); // 红色
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);

    int running = 1;
    SDL_Event event;

    while (running) {
        // 处理所有待处理事件（非阻塞）
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    printf("SDL_QUIT received\n");
                    running = 0;
                    break;
                case SDL_KEYDOWN:
                    if (event.key.keysym.sym == SDLK_q) {
                        SDL_Event ev = {0};
                        ev.type = FF_QUIT_EVENT;
                        SDL_PushEvent(&ev);
                    } else if (event.key.keysym.sym == SDLK_a) {
                        printf("key down a\n");
                    } else if (event.key.keysym.sym == SDLK_s) {
                        printf("key down s\n");
                    } else if (event.key.keysym.sym == SDLK_d) {
                        printf("key down d\n");
                    } else {
                        printf("key down 0x%x\n", event.key.keysym.sym);
                    }
                    break;
                case SDL_MOUSEBUTTONDOWN:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        printf("mouse down left\n");
                    } else if (event.button.button == SDL_BUTTON_RIGHT) {
                        printf("mouse down right\n");
                    } else {
                        printf("mouse down %d\n", event.button.button);
                    }
                    break;
                case SDL_MOUSEMOTION:
                    printf("mouse move (%d,%d)\n", event.motion.x, event.motion.y);
                    break;
                case FF_QUIT_EVENT:
                    printf("receive quit event\n");
                    running = 0;
                    break;
                default:
                    break;
            }
        }

        // 如果需要每帧更新与渲染，可在这里处理
        // SDL_Delay(10); // 若不需要高帧率，可短暂 sleep 降低 CPU 占用
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
