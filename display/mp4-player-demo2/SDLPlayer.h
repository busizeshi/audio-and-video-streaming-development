//
// Created by 13127 on 2025/12/29.
//

#ifndef MP4_PLAYER_DEMO2_SDLPLAYER_H
#define MP4_PLAYER_DEMO2_SDLPLAYER_H

#include <SDL2/SDL.h>

class SDLPlayer {

private:
    SDL_AudioSpec wanted_spec;
    SDL_AudioSpec obtained_spec;
    SDL_AudioDeviceID audio_dev;
};


#endif //MP4_PLAYER_DEMO2_SDLPLAYER_H
