#ifndef VIDEOOUTSDL_H
#define VIDEOOUTSDL_H

#include <SDL.h>
#include <SDL_thread.h>
#include "mediabase.h"

namespace LQF
{
    //定义分辨率
    // 窗口分辨率
    // YUV像素分辨率
#define YUV_WIDTH   320
#define YUV_HEIGHT  240
    //定义YUV格式
#define YUV_FORMAT  SDL_PIXELFORMAT_IYUV

    class VideoOutSDL final
    {
    public:
        VideoOutSDL();

        ~VideoOutSDL();

        RET_CODE Init(const Properties& properties);

        RET_CODE Cache(const uint8_t* video_buf) const;

        RET_CODE Output(const uint8_t* video_buf);

        RET_CODE Loop();

    private:
        // SDL
        SDL_Event event{}; // 事件
        SDL_Rect rect{}; // 矩形
        SDL_Window* win = nullptr; // 窗口
        SDL_Renderer* renderer = nullptr; // 渲染
        SDL_Texture* texture = nullptr; // 纹理
        uint32_t pixFormat = YUV_FORMAT; // YUV420P，即是SDL_PIXELFORMAT_IYUV
        SDL_mutex* mutex{};

        // 分辨率
        int video_width = YUV_WIDTH;
        int video_height = YUV_HEIGHT;
        int win_width = YUV_WIDTH;
        int win_height = YUV_WIDTH;
        int video_buf_size_{};
        uint8_t* video_buf_{};
    };
}


#endif // VIDEOOUTSDL_H
