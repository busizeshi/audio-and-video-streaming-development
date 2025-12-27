#ifndef MP4_PLAYER_DEMO2_VIDEODECODE_H
#define MP4_PLAYER_DEMO2_VIDEODECODE_H

#include <string>

extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavutil/imgutils.h"
};

class VideoDecode {
public:
    VideoDecode();
    ~VideoDecode();

    /**
     * 初始化
     * @param filename 文件名
     * @return
     */
    bool init(const std::string &filename);

    /**
     * 读取并解码下一帧
     * @return true 如果成功获取一帧 RGB 数据
     */
    bool readNextFrame();

    /**
     * 获取当前解码并转换好的rgb数据指针
     */
    uint8_t *getRGBData();

    /**
     * 获取每行数据的字节数 (Pitch)
     */
    int getLineSize();

    /**
     * 获取视频宽度
     */
    int getWidth();

    /**
     * 获取视频高度
     */
    int getHeight();

    /**
     * 获取视频帧率
     */
    double getFPS();

    /**
    * 关闭并释放资源
    */
    void close();

private:
    // FFmpeg 核心组件
    AVFormatContext *format_ctx = nullptr;
    AVCodecContext *video_ctx = nullptr;
    AVFrame *frame = nullptr;       // 原始解码帧 (YUV)
    AVFrame *rgb_frame = nullptr;   // 转换为RGB的帧
    SwsContext *sws_ctx = nullptr;  // 图像格式转换上下文

    int video_stream_index = -1;    // 视频流索引
    uint8_t* buffer = nullptr;      // RGB数据缓存区

    /**
     * 初始化sws上下文
     */
    bool initSwsContext();
};

#endif //MP4_PLAYER_DEMO2_VIDEODECODE_H