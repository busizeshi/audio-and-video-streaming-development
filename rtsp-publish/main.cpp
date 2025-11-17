#include <iostream>
#include "SDL.h"
#include "VideoCapture.h"
#include <fstream>
#include <vector>

void saveFrameToPGM(const AVFrame* frame, const int frameCount)
{
    const std::string filename = "frame_" + std::to_string(frameCount) + ".pgm";
    std::ofstream file(filename, std::ios::binary);

    file << "P5\n" << frame->width << " " << frame->height << " " << 255 << "\n";

    for (int i = 0; i < frame->height; i++)
    {
        file.write(reinterpret_cast<char*>(frame->data[0] + i * frame->linesize[0]), frame->width);
    }

    file.close();
    std::cout << ">>> [验证成功] 已保存图片: " << filename << " (请用看图软件打开检查)" << std::endl;
}

int main(int argc, char* argv[])
{
    setbuf(stdout, nullptr);

    VideoCapture cap;

    const std::string devName = "Integrated Camera";

    std::cout << "正在打开摄像头: " << devName << " ..." << std::endl;

    if (!cap.open(devName, 1280, 720, 30))
    {
        std::cerr << "打开失败！请检查设备名是否正确，或是否被其他程序占用。" << std::endl;
        return -1;
    }

    int count = 0;

    cap.start([&](AVFrame* frame)
    {
        count++;
        if (count % 1 == 0)
        {
            std::cout << "采集第 " << count << " 帧 | 格式: " << frame->format
                << " | PTS: " << frame->pts << std::endl;
        }

        saveFrameToPGM(frame, count);

        av_frame_free(&frame);
    });

    std::this_thread::sleep_for(std::chrono::seconds(3));

    cap.stop();
    std::cout << "程序结束。" << std::endl;
    return 0;
}
