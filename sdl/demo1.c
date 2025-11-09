/**
 * 打开一个SDL窗口
 */

#include <stdio.h>
#include <SDL.H>

int main(int argc, char *argv[]) {
    printf("Hello World!\n");

    SDL_Window *window = NULL;
    SDL_Init(SDL_INIT_VIDEO);

    window = SDL_CreateWindow("Hello World!", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 480,
                              SDL_WINDOW_SHOWN);

    if (!window) {
        printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Delay(5000);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}