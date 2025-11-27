#ifndef RTSP_PUBLISH_VIDEOCAPTURE_H
#define RTSP_PUBLISH_VIDEOCAPTURE_H

#include <atomic>
#include <string>
#include <thread>
#include <functional>
#include <iostream>

// 添加对 ConfigManager.h 的包含
#include "ConfigManager.h"

extern "C" {
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/time.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

/**
 * @brief 视频帧回调函数
 * 回调收到的是一个已经做了 av_frame_ref() 的 AVFrame*。
 * 使用者在处理完后**必须**调用 av_frame_unref(frame); av_frame_free(&frame);
 *
 * @param frame 已经被引用（ref）的 AVFrame*
 */
using VideoFrameCallback = std::function<void(AVFrame *frame)>;

class VideoCapture {
public:
    VideoCapture();

    ~VideoCapture();

    /**
     * @brief 打开摄像头
     * @param config 配置管理器对象
     * @return true 成功
     */
    bool open(const ConfigManager& config);

    /**
     * @brief 开始采集（会启动线程）
     * @param cb 回调函数（会在采集线程中调用）
     */
    void start(const VideoFrameCallback &cb);

    /**
     * @brief 停止采集（会等待线程退出并释放资源）
     */
    void stop();

    /**
     * @brief 获取解码后的像素格式
     * @note 必须在 open() 成功后调用
     */
    static AVPixelFormat getFormat() { return AV_PIX_FMT_NV12; }

    /**
     * @brief 获取解码后的宽度
     * @note 必须在 open() 成功后调用
     */
    int getWidth() const { return codec_ctx ? codec_ctx->width : 0; }

    /**
     * @brief 获取解码后的高度
     * @note 必须在 open() 成功后调用
     */
    int getHeight() const { return codec_ctx ? codec_ctx->height : 0; }

private:
    /**
     * @brief 采集工作线程（内部运行）
     */
    void captureThreadLoop();

private:
    AVFormatContext *fmt_ctx = nullptr;
    AVCodecContext *codec_ctx = nullptr;
    int video_stream_index = -1;

    SwsContext *sws_ctx = nullptr;
    AVFrame *nv12_frame = nullptr;

    std::thread worker_thread;
    std::atomic<bool> isRunning{false};
    VideoFrameCallback callback = nullptr;

    int64_t startTime = 0;
};

#endif // RTSP_PUBLISH_VIDEOCAPTURE_H