#ifndef MP4_PLAYER_DEMO2_SDLPLAYER_H
#define MP4_PLAYER_DEMO2_SDLPLAYER_H

#include <SDL.h>
#include <iostream>

class SDLPlayer {
public:
    SDLPlayer() = default;
    ~SDLPlayer();

    bool init(int width, int height);
    void render(uint8_t *data, int pitch);
    static bool handleEvents();
    void close();

private:
    SDL_Texture *texture = nullptr;
    SDL_Renderer *renderer = nullptr;
    SDL_Window *window = nullptr;

    int width = 0;
    int height = 0;
};

#endif //MP4_PLAYER_DEMO2_SDLPLAYER_H