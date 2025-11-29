#pragma once

#include <string>
#include <functional>
#include <iostream>

extern "C" {
#include "libavcodec/avcodec.h"
#include "libswresample/swresample.h" // <-- 音频重采样
#include "libavutil/audio_fifo.h"    // <-- 音频 FIFO
#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "libavutil/channel_layout.h"
}

// (可以复用 VideoEncoder 的定义)
using PacketCallback = std::function<void(AVPacket*)>;

class AudioEncoder {
public:
    AudioEncoder();
    ~AudioEncoder();

    /**
     * @brief 初始化音频编码器和重采样器
     * @param in_sample_rate    输入采样率 (来自 AudioCapture)
     * @param in_sample_fmt     输入采样格式 (来自 AudioCapture)
     * @param in_ch_layout      输入声道布局 (来自 AudioCapture)
     * @param out_sample_rate   目标采样率 (例如 44100)
     * @param out_channels      目标声道数 (例如 2)
     * @param out_bit_rate      目标码率 (例如 128000)
     * @return true 成功, false 失败
     */
    bool init(int in_sample_rate, AVSampleFormat in_sample_fmt, int64_t in_ch_layout,
              int out_sample_rate, int out_channels, int out_bit_rate);

    /**
     * @brief 编码一帧音频
     * @param pcm_frame 从 AudioCapture 传入的 PCM 帧
     * 函数内部会处理引用，调用者仍需释放传入的 pcm_frame
     * @return true 成功, false 失败
     */
    bool encodeFrame(AVFrame* pcm_frame);

    /**
     * @brief 停止编码器，并刷新(flush)所有剩余的帧
     */
    void stop();

    /**
     * @brief 设置编码包的回调函数
     */
    void setCallback(const PacketCallback& cb) {
        callback_ = cb;
    }

    /**
     * @brief 获取时间基 (用于 Muxer)
     */
    AVRational getTimebase() const {
        // AAC 编码器通常使用 1/samplerate 作为时间基
        return {1, 44100}; // 注意：应与输出采样率一致
    }

    AVCodecParameters* getCodecParameters() const {
        return codec_par_;
    }


private:
    /**
     * @brief 将重采样后的数据写入 FIFO，并尝试从 FIFO 读取固定大小的帧进行编码
     * @return true 成功, false 失败
     */
    bool pushToFifoAndEncode();

    /**
     * @brief 刷新编码器
     */
    void flush();

    /**
     * @brief 核心编码函数：发送一帧 (frame) 并接收所有可用的包 (packet)
     */
    bool sendAndReceive(AVFrame* frame);


    AVCodecContext* codec_ctx_ = nullptr;
    AVCodecParameters* codec_par_ = nullptr;

    // --- 重采样 (SWR) ---
    SwrContext* swr_ctx_ = nullptr;
    AVFrame* resampled_frame_ = nullptr; // 用于 SWR 重采样输出 和 编码器输入
    uint8_t** resampled_data_ = nullptr; // SWR 输出缓冲
    int max_resampled_samples_ = 0;      // SWR 输出缓冲大小

    // --- FIFO ---
    AVAudioFifo* fifo_ = nullptr;

    PacketCallback callback_;
    int64_t next_pts_ = 0; // 下一帧的 PTS (以采样点为单位)
};