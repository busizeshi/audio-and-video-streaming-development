//
// Created by jwd on 2025/11/27.
//

#ifndef RTSP_PUBLISH_RTMPPUBLISHER_H
#define RTSP_PUBLISH_RTMPPUBLISHER_H

#include <iostream>
#include <string>
#include <mutex>
#include <functional>
#include <atomic>

extern "C" {
#include "libavformat/avformat.h"
}

using PacketCallback = std::function<void(AVPacket*)>;

class RtmpPublisher {
public:
    RtmpPublisher();
    ~RtmpPublisher();

    /**
     * @brief 初始化推流器 (创建 AVFormatContext)
     * @param url RTMP 地址 (如 rtmp://server/live/key)
     * @return true 成功, false 失败
     */
    bool init(const std::string& url);

    /**
     * @brief 添加一路视频流 (复制 CodecParameters)
     * @param codecpar 编码器参数 (来自 VideoEncoder)
     * @param timebase 编码器时间基 (用于后续的时间戳转换)
     * @return true 成功, false 失败
     */
    bool addVideoStream(const AVCodecParameters* codecpar, AVRational timebase);

    /**
     * @brief 添加一路音频流
     * @param codecpar 编码器参数 (来自 AudioEncoder)
     * @param timebase 编码器时间基
     * @return true 成功, false 失败
     */
    bool addAudioStream(const AVCodecParameters* codecpar, AVRational timebase);

    /**
     * @brief 开始推流 (连接服务器并写入 Header)
     * @return true 成功, false 失败
     */
    bool start();

    /**
     * @brief 停止推流 (写入 Trailer 并释放资源)
     */
    void stop();

    /**
     * @brief 推送一个视频包
     * @param packet 从 VideoEncoder 传来的 H.264 包
     * 内部会处理时间戳转换和写入，调用者无需再管理 packet 生命周期
     */
    void pushVideoPacket(AVPacket* packet);

    /**
     * @brief 推送一个音频包
     * @param packet 从 AudioEncoder 传来的 AAC 包
     * 内部会处理时间戳转换和写入，调用者无需再管理 packet 生命周期
     */
    void pushAudioPacket(AVPacket* packet);

    /**
     * @brief 检查是否已连接到服务器
     * @return true 已连接, false 未连接或已断开
     */
    bool isConnected() const { return is_connected_.load(); }

private:
    /**
     * @brief 核心写入函数 (处理时间戳转换和互斥锁)
     * @param packet 数据包
     * @param out_stream 对应的 AVStream
     * @param src_timebase 源时间基
     */
    void sendPacket(AVPacket* packet, AVStream* out_stream, AVRational src_timebase);


    AVFormatContext* fmt_ctx_ = nullptr;
    AVStream* video_stream_ = nullptr;
    AVStream* audio_stream_ = nullptr;
    AVRational video_enc_tb_ = {0, 0}; // VideoEncoder 的时间基
    AVRational audio_enc_tb_ = {0, 0}; // AudioEncoder 的时间基
    std::string url_;
    std::atomic<bool> is_connected_{false};
    std::mutex write_mutex_;
};


#endif //RTSP_PUBLISH_RTMPPUBLISHER_H