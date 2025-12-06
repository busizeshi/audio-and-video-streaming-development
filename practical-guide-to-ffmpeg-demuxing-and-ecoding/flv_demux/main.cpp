/**
 * FLV Parser (FLV 格式分析器)
 * 包含：Header 解析, Tag 遍历, H.264/AAC 详细信息提取, DTS/PTS 计算
 */

#include <iostream>
#include <fstream>
#include <utility>
#include <vector>
#include <iomanip>
#include <cstdint>
#include <string>

// ==========================================
// 1. 辅助工具：大端序读取与格式化
// ==========================================

// 读取 4 字节无符号整数 (Big Endian)
uint32_t readUI32(const uint8_t* buffer) {
    return (buffer[0] << 24) | (buffer[1] << 16) | (buffer[2] << 8) | buffer[3];
}

// 读取 3 字节无符号整数 (Big Endian)
uint32_t readUI24(const uint8_t* buffer) {
    return (buffer[0] << 16) | (buffer[1] << 8) | buffer[2];
}

// 读取 3 字节有符号整数 (CompositionTime 是 SI24)
int32_t readSI24(const uint8_t* buffer) {
    uint32_t val = (buffer[0] << 16) | (buffer[1] << 8) | buffer[2];
    // 如果第 24 位是 1，说明是负数，需要进行符号扩展
    if (val & 0x800000) {
        val |= 0xFF000000;
    }
    return static_cast<int32_t>(val);
}

// 读取 2 字节无符号整数
uint16_t readUI16(const uint8_t* buffer) {
    return (buffer[0] << 8) | buffer[1];
}

// 格式化输出时间戳
std::string formatTime(uint32_t ms) {
    char buf[64];
    uint32_t sec = ms / 1000;
    uint32_t msec = ms % 1000;
    snprintf(buf, sizeof(buf), "%u.%03us", sec, msec);
    return {buf};
}

// ==========================================
// 2. 核心解析逻辑
// ==========================================

class FLVParser {
public:
    FLVParser(std::string  filename) : filename_(std::move(filename)) {}

    bool run() {
        std::ifstream file(filename_, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Error: 无法打开文件 " << filename_ << std::endl;
            return false;
        }

        if (!parseHeader(file)) {
            return false;
        }

        parseBody(file);

        file.close();
        return true;
    }

private:
    std::string filename_;

    // 解析 FLV Header (9字节)
    static bool parseHeader(std::ifstream& file) {
        uint8_t buffer[9];
        if (!file.read((char*)buffer, 9)) return false;

        // Signature Check [cite: 58]
        if (buffer[0] != 'F' || buffer[1] != 'L' || buffer[2] != 'V') {
            std::cerr << "Error: 不是有效的 FLV 文件 (Signature 错误)" << std::endl;
            return false;
        }

        uint8_t version = buffer[3];
        uint8_t flags = buffer[4];
        uint32_t headerSize = readUI32(buffer + 5); // DataOffset

        bool hasAudio = flags & 0x04;
        bool hasVideo = flags & 0x01;

        std::cout << "========= FLV File Header =========" << std::endl;
        std::cout << "Version: " << (int)version << std::endl;
        std::cout << "Contains: " << (hasAudio ? "Audio " : "") << (hasVideo ? "Video" : "") << std::endl;
        std::cout << "Header Size: " << headerSize << std::endl;
        std::cout << "===================================" << std::endl;

        // 跳过 Header 可能存在的填充字节，通常 headerSize=9
        file.seekg(headerSize, std::ios::beg);
        return true;
    }

    // 循环解析 Tag
    static void parseBody(std::ifstream& file) {
        uint8_t prevTagSizeBuf[4];
        uint8_t tagHeaderBuf[11];
        int tagIndex = 0;

        while (true) {
            // 1. 读取 PreviousTagSize [cite: 19]
            if (!file.read((char*)prevTagSizeBuf, 4)) break; // EOF check
            uint32_t prevTagSize = readUI32(prevTagSizeBuf);

            // 2. 读取 Tag Header 
            if (!file.read((char*)tagHeaderBuf, 11)) break;

            uint8_t tagType = tagHeaderBuf[0];
            uint32_t dataSize = readUI24(tagHeaderBuf + 1);

            // 时间戳计算：Timestamp(低24位) | TimestampExtended(高8位) 
            uint32_t timestamp = readUI24(tagHeaderBuf + 4);
            uint8_t timestampExt = tagHeaderBuf[7];
            uint32_t dts = (timestampExt << 24) | timestamp; // DTS (Decoding Time Stamp)

            uint32_t streamID = readUI24(tagHeaderBuf + 8); // Always 0

            // 3. 读取 Tag Data
            std::vector<uint8_t> data(dataSize);
            file.read((char*)data.data(), dataSize);

            // 打印基础信息
            std::cout << "\n[Tag #" << tagIndex++ << "] Type: ";
            switch (tagType) {
                case 0x08: std::cout << "Audio"; break;
                case 0x09: std::cout << "Video"; break;
                case 0x12: std::cout << "Script"; break;
                default:   std::cout << "Unknown(" << (int)tagType << ")"; break;
            }
            std::cout << " | Size: " << dataSize
                      << " | DTS: " << dts << "ms (" << formatTime(dts) << ")";

            // 4. 详细解析
            if (tagType == 0x09) {
                parseVideoTag(data, dts);
            } else if (tagType == 0x08) {
                parseAudioTag(data);
            } else if (tagType == 0x12) {
                // Script Tag 解析比较复杂(AMF格式)，此处仅做简单标记
                std::cout << " -> MetaData Info";
            }
            std::cout << std::endl;
        }
    }

    // 解析视频 Tag Data [cite: 226]
    static void parseVideoTag(const std::vector<uint8_t>& data, uint32_t dts) {
        if (data.empty()) return;

        // 第1个字节：FrameType (高4位) + CodecID (低4位) [cite: 232]
        uint8_t val = data[0];
        uint8_t frameType = (val >> 4) & 0x0F;
        uint8_t codecID = val & 0x0F;

        std::string frameDesc;
        switch (frameType) {
            case 1: frameDesc = "KeyFrame (IDR)"; break; // 关键帧
            case 2: frameDesc = "InterFrame"; break;     // 普通帧
            default: frameDesc = "Other"; break;
        }

        std::string codecDesc;
        if (codecID == 7) codecDesc = "AVC(H.264)";
        else codecDesc = "Other(" + std::to_string(codecID) + ")";

        std::cout << "\n    -> Video Info: " << codecDesc << ", " << frameDesc;

        // 如果是 H.264 (AVC) [cite: 234]
        if (codecID == 7 && data.size() >= 5) {
            // data[1] = AVCPacketType
            // data[2-4] = CompositionTime (CTS)
            uint8_t avcPacketType = data[1];
            int32_t cts = readSI24(&data[2]); // Composition Time Offset

            // PTS = DTS + CTS 
            // 注意：如果 AVCPacketType 为 0 (Header)，CTS 通常为 0
            int64_t pts = dts + cts;

            std::cout << "\n    -> AVC Packet: ";
            if (avcPacketType == 0) {
                std::cout << "Sequence Header (AVCDecoderConfigurationRecord)";
                std::cout << " [SPS/PPS info]";
            } else if (avcPacketType == 1) {
                std::cout << "NALU";
                std::cout << " | CTS: " << cts << "ms";
                std::cout << " | PTS: " << pts << "ms (" << formatTime(pts) << ")";
            } else if (avcPacketType == 2) {
                std::cout << "End of Sequence";
            }
        }
    }

    // 解析音频 Tag Data [cite: 197]
    static void parseAudioTag(const std::vector<uint8_t>& data) {
        if (data.empty()) return;

        // 第1个字节：SoundFormat(4bit) Rate(2bit) Size(1bit) Type(1bit) [cite: 204]
        uint8_t val = data[0];
        uint8_t format = (val >> 4) & 0x0F;
        uint8_t rateIndex = (val >> 2) & 0x03;
        uint8_t sizeIndex = (val >> 1) & 0x01;
        uint8_t typeIndex = val & 0x01;

        std::string fmtStr;
        if (format == 10) fmtStr = "AAC";
        else if (format == 2) fmtStr = "MP3";
        else fmtStr = "Format_" + std::to_string(format);

        // 采样率映射 (注意：AAC 通常忽略此字段，实际信息在 Sequence Header 中)
        int rates[] = {5500, 11000, 22050, 44100};

        std::cout << "\n    -> Audio Info: " << fmtStr
                  << " | " << rates[rateIndex] << "Hz"
                  << " | " << (sizeIndex ? "16-bit" : "8-bit")
                  << " | " << (typeIndex ? "Stereo" : "Mono");

        // 如果是 AAC [cite: 218]
        if (format == 10 && data.size() >= 2) {
            uint8_t aacPacketType = data[1];
            std::cout << "\n    -> AAC Packet: ";
            if (aacPacketType == 0) {
                std::cout << "Sequence Header (AudioSpecificConfig)";
            } else if (aacPacketType == 1) {
                std::cout << "Raw Data";
            }
        }
    }
};

// ==========================================
// 3. 主程序入口
// ==========================================

int main(int argc, char* argv[]) {
    // 默认测试文件名，可以在这里修改
    std::string filename = "test.flv";

    if (argc > 1) {
        filename = argv[1];
    }

    std::cout << "Opening file: " << filename << "..." << std::endl;

    FLVParser parser(filename);
    if (!parser.run()) {
        return -1;
    }

    return 0;
}