//
// Created by jwd on 2025/11/1.
//
#include <stdio.h>
#include <SDL.h>

int main(int argc, char *argv[]) {

    int run = 1;
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *texture = NULL;
    SDL_Rect rect;
    rect.w = 50;
    rect.h = 50;

    SDL_Init(SDL_INIT_VIDEO);

    window = SDL_CreateWindow("2 Window",
                              SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED,
                              640,
                              480,
                              SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

    if (!window) {
        return -1;
    }
    renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer) {
        return -1;
    }
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_TARGET, 640, 480);//创建纹理

    if (!texture) {
        return -1;
    }
    int show_count = 0;
    while (run) {
        rect.x = rand() % 600;
        rect.y = rand() % 400;

        SDL_SetRenderTarget(renderer, texture);//设置纹理为当前渲染目标
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);//设置渲染颜色
        SDL_RenderClear(renderer);//清空渲染目标

        SDL_RenderDrawRect(renderer, &rect);//绘制矩形
        SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);//设置渲染颜色
        SDL_RenderFillRect(renderer, &rect);

        SDL_SetRenderTarget(renderer, NULL); //恢复默认，渲染目标为窗口
        SDL_RenderCopy(renderer, texture, NULL, NULL); //拷贝纹理到CPU

        SDL_RenderPresent(renderer); //输出到目标窗口上
        SDL_Delay(300);
        if (show_count++ > 30) {
            run = 0;        // 不跑了
        }
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window); //销毁窗口
    SDL_Quit();
    return 0;
}