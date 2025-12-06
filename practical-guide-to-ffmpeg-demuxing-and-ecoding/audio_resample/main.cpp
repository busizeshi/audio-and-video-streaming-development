/**
 * 编译命令: g++ PCMResampler.cpp -o PCMResampler -lswresample -lavutil
 */

#include <iostream>
#include <fstream>
#include <vector>
#include <stdexcept>

extern "C" {
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
}

// 简单的音频参数结构体
struct AudioParams {
    int sampleRate;
    AVSampleFormat sampleFmt;
    AVChannelLayout chLayout;

    // 辅助函数：计算每帧的字节数 (通道数 * 单样本字节数)
    int getBytesPerFrame() const {
        return chLayout.nb_channels * av_get_bytes_per_sample(sampleFmt);
    }
};

class AudioResampler {
public:
    AudioResampler(AudioParams in, AudioParams out)
            : inParams(in), outParams(out), swrCtx(nullptr) {

        // 使用 FFmpeg 6.0+ 的 swr_alloc_set_opts2
        int ret = swr_alloc_set_opts2(
                &swrCtx,
                &outParams.chLayout, outParams.sampleFmt, outParams.sampleRate,
                &inParams.chLayout, inParams.sampleFmt, inParams.sampleRate,
                0, nullptr
        );

        if (ret < 0 || !swrCtx) throw std::runtime_error("无法分配重采样上下文");
        if (swr_init(swrCtx) < 0) throw std::runtime_error("无法初始化重采样上下文");
    }

    ~AudioResampler() {
        if (swrCtx) swr_free(&swrCtx);
    }

    // 执行转换的核心函数
    std::vector<uint8_t> convert(const uint8_t* inputData, int inputSampleCount) {
        // 1. 计算目标缓冲区需要的最大样本数 (包含延迟)
        // 公式：(延迟 + 输入数) * (输出率 / 输入率)
        int64_t delay = swr_get_delay(swrCtx, inParams.sampleRate);
        int64_t dstNbSamples = av_rescale_rnd(
                delay + inputSampleCount,
                outParams.sampleRate, inParams.sampleRate,
                AV_ROUND_UP
        );

        if (dstNbSamples <= 0) return {};

        // 2. 准备输出 Buffer
        int outBytesPerSample = av_get_bytes_per_sample(outParams.sampleFmt);
        int outChannels = outParams.chLayout.nb_channels;

        std::vector<uint8_t> outputBuffer(dstNbSamples * outChannels * outBytesPerSample);
        uint8_t* outDataPtr = outputBuffer.data();
        const uint8_t* inDataPtr = inputData;

        // 3. 执行 FFmpeg 重采样
        // inputData 为 nullptr 时，swr_convert 会执行 flush 操作
        int convertedSamples = swr_convert(
                swrCtx,
                &outDataPtr, dstNbSamples,
                &inDataPtr, inputSampleCount
        );

        if (convertedSamples < 0) throw std::runtime_error("重采样转换失败");

        // 4. 调整 vector 大小为实际转换出的字节数
        outputBuffer.resize(convertedSamples * outChannels * outBytesPerSample);
        return outputBuffer;
    }

    std::vector<uint8_t> flush() {
        return convert(nullptr, 0);
    }

private:
    AudioParams inParams;
    AudioParams outParams;
    struct SwrContext* swrCtx;
};

int main() {
    // ==========================================
    // 1. 配置区域：请在这里修改参数
    // ==========================================

    // --- 输入文件参数 (必须与 output_44.1k_s16.pcm 的实际属性一致) ---
    const char* INPUT_FILE = "../output_44.1k_s16.pcm";
    AudioParams inParams{};
    inParams.sampleRate = 44100;                // 输入采样率
    inParams.sampleFmt  = AV_SAMPLE_FMT_S16;    // 输入格式: 16位整数
    inParams.chLayout   = AV_CHANNEL_LAYOUT_STEREO; // 输入: 立体声

    // --- 输出文件参数 (你可以修改这里！) ---
    // 场景 A: 提升到 48kHz (视频标准)
//    ffplay -f s16le -ar 48000 -ac 2 target_48k.pcm
    const char* OUTPUT_FILE = "../target_48k.pcm";
    AudioParams outParams{};
    outParams.sampleRate = 48000;               // 目标采样率
    outParams.sampleFmt  = AV_SAMPLE_FMT_S16;   // 目标格式 (保持 S16 或改为 AV_SAMPLE_FMT_FLT)
    outParams.chLayout   = AV_CHANNEL_LAYOUT_STEREO; // 目标声道

    /* // 场景 B: 降采样到 16kHz 单声道 (语音识别标准) - 如果想用这个，请取消注释并注释掉上面的
     * ffplay -f s16le -ar 16000 -ac 1 target_16k_mono.pcm
    const char* OUTPUT_FILE = "target_16k_mono.pcm";
    AudioParams outParams;
    outParams.sampleRate = 16000;
    outParams.sampleFmt  = AV_SAMPLE_FMT_S16;
    outParams.chLayout   = AV_CHANNEL_LAYOUT_MONO;
    */

    // ==========================================
    // 2. 执行逻辑
    // ==========================================

    std::ifstream inFile(INPUT_FILE, std::ios::binary);
    std::ofstream outFile(OUTPUT_FILE, std::ios::binary);

    if (!inFile) {
        std::cerr << "错误: 无法打开输入文件 " << INPUT_FILE << std::endl;
        return 1;
    }

    try {
        AudioResampler resampler(inParams, outParams);

        // 缓冲区：每次读取 1024 个输入帧
        int inputFrameSize = 1024;
        std::vector<uint8_t> readBuffer(inputFrameSize * inParams.getBytesPerFrame());

        std::cout << "正在将 " << INPUT_FILE << " 重采样到 " << OUTPUT_FILE << " ..." << std::endl;
        std::cout << "输入: " << inParams.sampleRate << "Hz, " << inParams.chLayout.nb_channels << "ch" << std::endl;
        std::cout << "输出: " << outParams.sampleRate << "Hz, " << outParams.chLayout.nb_channels << "ch" << std::endl;

        // 循环处理
        while (inFile.read(reinterpret_cast<char*>(readBuffer.data()), readBuffer.size())) {
            int readBytes = inFile.gcount();
            int readFrames = readBytes / inParams.getBytesPerFrame();

            auto processedData = resampler.convert(readBuffer.data(), readFrames);
            if (!processedData.empty()) {
                outFile.write(reinterpret_cast<char*>(processedData.data()), processedData.size());
            }
        }

        // 处理最后一次读取不足 1024 帧的情况
        if (inFile.gcount() > 0) {
            int readBytes = inFile.gcount();
            int readFrames = readBytes / inParams.getBytesPerFrame();
            auto processedData = resampler.convert(readBuffer.data(), readFrames);
            outFile.write(reinterpret_cast<char*>(processedData.data()), processedData.size());
        }

        // Flush (取出残留数据)
        auto flushedData = resampler.flush();
        if (!flushedData.empty()) {
            std::cout << "Flushing 残留数据: " << flushedData.size() << " bytes" << std::endl;
            outFile.write(reinterpret_cast<char*>(flushedData.data()), flushedData.size());
        }

        std::cout << "完成！" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "发生异常: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}