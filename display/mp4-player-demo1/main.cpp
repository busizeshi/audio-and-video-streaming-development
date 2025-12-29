#include <iostream>
#include "VideoDecode.h"
#include "SDLPlayer.h"

// 简单的宏，用于处理没有获取到帧率的情况
#define DEFAULT_FPS 25.0

int main(int argc, char *argv[]) {
    // 设置标准输出无缓冲，方便调试信息实时输出
    setbuf(stdout, nullptr);

    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <file>" << std::endl;
        return -1;
    }
    std::string filename = argv[1];
    std::cout << "播放的视频文件：filename: " << filename << std::endl;

    // 创建对象
    VideoDecode decoder;
    SDLPlayer player;

    // 初始化解码器
    if (!decoder.init(filename)) {
        std::cout << "初始化解码器失败" << std::endl;
        return -1;
    }

    // 获取视频宽高和帧率
    int width = decoder.getWidth();
    int height = decoder.getHeight();
    double fps = decoder.getFPS();

    if (fps <= 0) fps = DEFAULT_FPS;
    std::cout << "视频信息: " << width << "x" << height << ", FPS: " << fps << std::endl;

    // 计算每帧显示的理想耗时 (毫秒)
    int frame_duration = (int)(1000.0 / fps);

    // 初始化播放器
    if (!player.init(width, height)) {
        std::cout << "初始化播放器失败" << std::endl;
        return -1;
    }

    std::cout << "开始播放" << std::endl;

    bool is_playing = true;

    // 主循环
    while (is_playing) {
        // 记录开始解码渲染的时间
        uint32_t start_time = SDL_GetTicks();//表示SDL自初始化以来的时间，毫秒值

        // 1. 处理UI事件 (退出等)
        if (SDLPlayer::handleEvents()) {
            is_playing = false;
            break;
        }

        // 2. 解码下一帧
        // 如果解码成功，进行渲染
        if (decoder.readNextFrame()) {
            // 3. 获取解码后的RGB数据并渲染
            player.render(decoder.getRGBData(), decoder.getLineSize());

            // 4. 帧率控制 (Sync)
            // 计算解码+渲染花了多少时间
            uint32_t process_time = SDL_GetTicks() - start_time;

            // 如果处理时间小于每帧的标准间隔，则通过 Delay 补齐
            if (process_time < frame_duration) {
                SDL_Delay(frame_duration - process_time);
            }
        } else {
            // 解码失败或文件结束
            std::cout << "播放结束或读取失败" << std::endl;
            is_playing = false;
        }
    }

    // 显式关闭资源 (也可依赖析构函数)
    decoder.close();
    player.close();

    return 0;
}