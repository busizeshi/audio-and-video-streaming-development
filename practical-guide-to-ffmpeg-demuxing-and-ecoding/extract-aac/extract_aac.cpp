/**
 * AAC Extractor from MP4 (Adding ADTS Headers)
 * 基于提供的 PDF 文档逻辑实现 ADTS 头封装
 */

#include <iostream>
#include <fstream>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/log.h>
}

// 对应 PDF [cite: 120]：采样率频率索引表
const int sampling_frequencies[] = {
    96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050,
    16000, 12000, 11025, 8000, 7350, -1, -1, -1
};

// 获取采样率对应的 index (0-15)
int get_sample_rate_index(int freq) {
    for (int i = 0; i < 16; i++) {
        if (sampling_frequencies[i] == freq) {
            return i;
        }
    }
    return 4; // 默认为 44100Hz (index 4)
}

/**
 * 核心函数：构建 ADTS 头 (7 字节)
 * 逻辑参考 PDF  和
 */
void get_adts_header(uint8_t *adts_header, int data_len, int profile, int sample_rate_idx, int channels) {
    // ADTS 头固定为 7 字节 (protection_absent=1) [cite: 77, 80]
    // 包含头部的总帧长
    int frame_length = data_len + 7;

    // --- adts_fixed_header  ---

    // Byte 0: Syncword (前8位) -> 0xFF
    adts_header[0] = 0xFF;

    // Byte 1: Syncword (后4位) + ID + Layer + Protection Absent
    // Syncword (1111) | ID (0 for MPEG-4) | Layer (00) | Protection Absent (1)
    // 1111 0 00 1 = 0xF1
    adts_header[1] = 0xF1; // [cite: 87-90]

    // Byte 2: Profile + Sampling Freq Index + Private Bit + Channel Config (高位)
    // Profile (2 bits): MPEG-4 AAC LC通常是2 (AOT)，但ADTS header里需要减1。
    // 注意：FFmpeg的 profile 值通常已经对齐，这里假设传入的是 AOT-1。
    // 如果是 LC，profile值应为 1 (01)。
    // 结构: Profile(2) | FreqIdx(4) | Private(1) | ChanCfg(1)
    adts_header[2] = ((profile & 0x03) << 6) |
                     ((sample_rate_idx & 0x0F) << 2) |
                     (0 << 1) | // Private bit
                     ((channels & 0x04) >> 2); // Channel config 的最高位

    // Byte 3: Channel Config (低2位) + Original/Copy + Home + Copyright bits + Frame Length (高2位)
    // 结构: ChanCfg(2) | Orig(1) | Home(1) | CopyID(1) | CopyStart(1) | FrameLen(2)
    adts_header[3] = ((channels & 0x03) << 6) |
                     (0 << 5) | // Original/Copy
                     (0 << 4) | // Home
                     (0 << 3) | // Copyright ID
                     (0 << 2) | // Copyright Start
                     ((frame_length & 0x1800) >> 11); // Frame Length 高2位 (Total 13 bits)

    // --- adts_variable_header  ---

    // Byte 4: Frame Length (中间8位)
    adts_header[4] = (frame_length & 0x7F8) >> 3;

    // Byte 5: Frame Length (低3位) + Buffer Fullness (高5位)
    // Buffer Fullness 0x7FF 说明是码率可变 [cite: 142]
    // 0x7FF >> 6 = 0x1F (高5位)
    adts_header[5] = ((frame_length & 0x07) << 5) | 0x1F;

    // Byte 6: Buffer Fullness (低6位) + Number of Raw Data Blocks
    // 0x7FF & 0x3F = 0x3F (低6位)
    // Blocks: 0 (代表1个AAC数据块) [cite: 143]
    adts_header[6] = 0xFC; // 111111 00
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input_mp4> <output_aac>" << std::endl;
        return -1;
    }

    const char* input_file = argv[1];
    const char* output_file = argv[2];

    // 1. 打开输入文件
    AVFormatContext* fmt_ctx = nullptr;
    if (avformat_open_input(&fmt_ctx, input_file, nullptr, nullptr) < 0) {
        std::cerr << "Could not open source file" << std::endl;
        return -1;
    }

    // 2. 查找流信息
    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        std::cerr << "Could not find stream information" << std::endl;
        return -1;
    }

    // 3. 找到 AAC 音频流索引
    int audio_stream_idx = -1;
    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO &&
            fmt_ctx->streams[i]->codecpar->codec_id == AV_CODEC_ID_AAC) {
            audio_stream_idx = i;
            break;
        }
    }

    if (audio_stream_idx == -1) {
        std::cerr << "Could not find AAC stream in the input file" << std::endl;
        return -1;
    }

    AVCodecParameters* codec_par = fmt_ctx->streams[audio_stream_idx]->codecpar;

    // 打开输出文件
    std::ofstream outfile(output_file, std::ios::binary);
    if (!outfile.is_open()) {
        std::cerr << "Could not open output file" << std::endl;
        return -1;
    }

    // 准备 ADTS 头参数
    // 注意：FFmpeg 的 profile 定义：LC=1, HE=4...
    // ADTS header 需要的是 Audio Object Type - 1。
    // 对于 AAC LC，AOT=2，所以 profile 字段应填 1。
    // FFmpeg 的 FF_PROFILE_AAC_LOW 定义为 128 (某些版本)，但在 codecpar->profile 中通常是 1 (LC)。
    // 保险起见，AAC LC 常用 1。
    int profile = codec_par->profile;
    if (profile == AV_PROFILE_UNKNOWN) profile = 1; // 默认 LC

    int freq_idx = get_sample_rate_index(codec_par->sample_rate);
    int channels = codec_par->ch_layout.nb_channels; // 新版 API，旧版用 channels

    std::cout << "Detected AAC Stream:" << std::endl;
    std::cout << "Sample Rate: " << codec_par->sample_rate << " (Index: " << freq_idx << ")" << std::endl;
    std::cout << "Channels: " << channels << std::endl;
    std::cout << "Profile: " << profile << std::endl;

    AVPacket* pkt = av_packet_alloc();
    uint8_t adts_header[7];

    // 4. 读取数据包并写入
    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == audio_stream_idx) {
            // 生成 ADTS 头 [cite: 7]
            // 注意：必须为每一帧重新计算，因为每一帧的 pkt->size 可能不同 (VBR)
            get_adts_header(adts_header, pkt->size, profile, freq_idx, channels);

            // 写入 ADTS 头 (7字节)
            outfile.write(reinterpret_cast<char*>(adts_header), 7);

            // 写入 AAC 裸数据 (ES)
            outfile.write(reinterpret_cast<char*>(pkt->data), pkt->size);
        }
        av_packet_unref(pkt);
    }

    // 清理
    outfile.close();
    av_packet_free(&pkt);
    avformat_close_input(&fmt_ctx);

    std::cout << "Extraction complete: " << output_file << std::endl;

    return 0;
}