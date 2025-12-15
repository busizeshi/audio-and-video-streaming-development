#ifndef AUDIOMIXER_H
#define AUDIOMIXER_H

#include <vector>
#include <string>
#include <mutex>
#include <memory>

extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
}

class AudioMixer {
public:
    AudioMixer();

    ~AudioMixer();

    // 初始化混音器
    // duration_mode: "longest" (最长), "shortest" (最短), "first" (以第一个为主)
    int init(const char *duration_mode = "longest");

    // 添加输入流配置 (必须在 init 之前调用)
    int addInput(int sample_rate, int channels, AVSampleFormat fmt);

    // 设置期望的输出格式 (必须在 init 之前调用)
    int setOutput(int sample_rate, int channels, AVSampleFormat fmt);

    // 发送 PCM 数据到指定输入流
    // index: 输入流索引 (0 或 1)
    // data: PCM 数据指针 (若为 NULL 表示 EOF/Flush)
    // size: 数据字节大小
    int sendFrame(int index, const uint8_t *data, int size);

    // 从混音器获取数据
    // out_buf: 接收数据的缓冲区
    // max_size: 缓冲区最大大小
    // 返回: 实际读取的字节数，0 表示暂时无数据，-1 表示结束或错误
    int receiveFrame(uint8_t *out_buf, int max_size);

private:
    struct InputContext {
        AVFilterContext *ctx = nullptr; // 过滤器上下文
        int sample_rate;
        int channels;
        AVSampleFormat fmt;
        int64_t next_pts = 0; // 用于计算 PTS
        AVRational time_base;
    };

    bool initialized_ = false;
    std::mutex mutex_;
    AVFilterGraph *graph_ = nullptr;
    std::vector<InputContext> inputs_;

    // 输出相关
    AVFilterContext *sink_ctx_ = nullptr;
    int out_sample_rate_ = 44100;
    int out_channels_ = 2;
    AVSampleFormat out_fmt_ = AV_SAMPLE_FMT_S16;
};

#endif // AUDIOMIXER_H
