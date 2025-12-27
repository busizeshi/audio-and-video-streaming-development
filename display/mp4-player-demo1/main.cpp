#include <iostream>
#include "VideoDecode.h"
#include "SDLPlayer.h"

int main(int argc, char *argv[]) {
    setbuf(stdout, nullptr);

    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <file>" << std::endl;
    }
    std::string filename = argv[1];
    std::cout << "播放的视频文件：filename: " << filename << std::endl;

//    创建对象
    VideoDecode decoder;
    SDLPlayer player;

//    初始化解码器
    if (!decoder.init(filename)) {
        std::cout << "初始化解码器失败" << std::endl;
        return -1;
    }

//    初始化播放器
    if (!player.init(decoder.getWidth(), decoder.getHeight())) {
        std::cout << "初始化播放器失败" << std::endl;
        return -1;
    }

    std::cout << "开始播放" << std::endl;

//    主循环
    while (true) {
//        处理事件
        if (player.handleEvents()) {
            break;
        }

//        解码下一帧
        if (decoder.readNextFrame()) {
//            获取解码后的RGB数据并渲染
            player.render(decoder.getRGBData(), decoder.getLineSize());

//            帧率控制
            SDL_Delay(30);
        } else {
            std::cout << "播放结束" << std::endl;
            break;
        }
    }

    decoder.close();
    player.close();

    return 0;
}