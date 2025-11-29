#ifndef RTSP_PUBLISH_AUDIOCAPTURE_H
#define RTSP_PUBLISH_AUDIOCAPTURE_H

#include <atomic>
#include <string>
#include <thread>
#include <functional>
#include <iostream>

extern "C" {
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/time.h>
}

/**
 * @brief 音频帧回调函数
 * 这里的 frame 包含 PCM 数据。
 * 注意：采集出来的 sample_fmt 可能是 AV_SAMPLE_FMT_S16，
 * 而 AAC 编码器通常需要 AV_SAMPLE_FMT_FLTP，后续需要 SwrContext 重采样。
 */
using AudioFrameCallback = std::function<void(AVFrame *frame)>;

class AudioCapture {
public:
    AudioCapture();

    ~AudioCapture();

    /**
     * @brief 打开麦克风
     * @param deviceName 设备名 (例如 "Microphone (Realtek Audio)")
     * @param channels 通道数 (通常 2)
     * @param sampleRate 采样率 (通常 44100 或 48000)
     */
    bool open(const std::string &deviceName, int channels = 2, int sampleRate = 44100);

    void start(const AudioFrameCallback &cb);

    void stop();

    int getSampleRate() const { return codec_ctx ? codec_ctx->sample_rate : 0; }

    int getChannels() const { return codec_ctx ? codec_ctx->ch_layout.nb_channels : 0; }

    AVSampleFormat getSampleFormat() const { return codec_ctx ? codec_ctx->sample_fmt : AV_SAMPLE_FMT_NONE; }

    // 获取声道布局掩码 (兼容旧 API)
    int64_t getChannelLayout() const {
        if (!codec_ctx) return 0;
        // 如果 layout 掩码为空但有声道数，则返回默认布局
        if (codec_ctx->ch_layout.u.mask == 0) {
            return av_get_default_channel_layout(codec_ctx->ch_layout.nb_channels);
        }
        return codec_ctx->ch_layout.u.mask;
    }

private:
    void captureThreadLoop();

private:
    AVFormatContext *fmt_ctx = nullptr;
    AVCodecContext *codec_ctx = nullptr;
    int audio_stream_index = -1;

    std::thread worker_thread;
    std::atomic<bool> isRunning{false};
    AudioFrameCallback callback = nullptr;

    int64_t startTime = 0;
};

#endif // RTSP_PUBLISH_AUDIOCAPTURE_H