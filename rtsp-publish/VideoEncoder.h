#pragma once

#include <iostream>
#include <string>
#include <functional>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
#include <libavutil/audio_fifo.h>
}

// 定义回调类型
using PacketCallback = std::function<void(AVPacket*)>;

class VideoEncoder {
public:
    VideoEncoder();
    ~VideoEncoder();

    /**
     * @brief 初始化视频编码器 (H.264)
     * @param width     宽度
     * @param height    高度
     * @param fps       帧率
     * @param bit_rate  码率 (bps)
     * @return true 成功, false 失败
     */
    bool init(int width, int height, int fps, int bit_rate);

    /**
     * @brief 编码一帧 NV12 图像
     * @param nv12_frame 从 VideoCapture 传入的 NV12 帧
     * 函数内部会处理引用，调用者仍需释放传入的 nv12_frame
     * @return true 成功, false 失败
     */
    bool encodeFrame(AVFrame* nv12_frame);

    /**
     * @brief 设置编码包的回调函数
     */
    void setCallback(const PacketCallback& cb) {
        callback_ = cb;
    }

    /**
     * @brief 获取 CodecParameters (用于 Muxer)
     */
    AVCodecParameters* getCodecParameters() const {
        return codec_par_;
    }

    /**
     * @brief 获取时间基 (用于 Muxer)
     */
    AVRational getTimebase() const {
        if (codec_ctx_) {
            return codec_ctx_->time_base;
        }
        // 默认返回 1/30 (30fps)
        return {1, 30};
    }

    /**
     * @brief 停止并清理资源
     */
    void stop();

private:
    /**
     * @brief 刷新编码器 (发送 nullptr 帧并接收所有剩余 packet)
     */
    void flush();


    AVCodecContext* codec_ctx_ = nullptr;
    AVCodecParameters* codec_par_ = nullptr;

    // --- 格式转换 (SWS) ---
    struct SwsContext* sws_ctx_ = nullptr;
    AVFrame* yuv420p_frame_ = nullptr; // 用于 SWS 输出 和 编码器输入

    PacketCallback callback_;
    int frame_width_ = 0;
    int frame_height_ = 0;
    int64_t next_pts_ = 0; // 下一帧的 PTS
};