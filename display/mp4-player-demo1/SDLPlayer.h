/**
 * SDL播放器类
 */

#ifndef MP4_PLAYER_DEMO2_SDLPLAYER_H
#define MP4_PLAYER_DEMO2_SDLPLAYER_H


#include <SDL.h>

class SDLPlayer {
public:
    SDLPlayer() = default;

    // 析构函数
    ~SDLPlayer();

    /**
     * 初始化 SDL 窗口和渲染器
     * @param width 窗口宽度
     * @param height 窗口高度
     * @return
     */
    bool init(int width, int height);

    /**
     * 渲染
     */
    void render(uint8_t *data, int pitch);

    /**
     * 处理事件
     * @return
     */
    static bool handleEvents();

    /**
     * 关闭
     */
    void close();

private:
    SDL_Texture *texture = nullptr;//sdl纹理
    SDL_Renderer *renderer = nullptr;//sdl渲染器
    SDL_Window *window = nullptr;//sdl窗口
    SDL_Event event{};//sdl事件

    int width = 0;
    int height = 0;
};


#endif //MP4_PLAYER_DEMO2_SDLPLAYER_H
