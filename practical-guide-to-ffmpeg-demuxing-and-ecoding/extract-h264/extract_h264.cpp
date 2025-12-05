#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <iomanip>
#include <cstdlib>
#include <cstdint>
#include <algorithm>

// 引入 FFmpeg 头文件 (必须在 extern "C" 中引用)
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavcodec/bsf.h>
}

/*
 * H.264 学习工具：提取器与分析器 (C++ API 版本)
 *
 * 依赖库: FFmpeg (libavformat, libavcodec, libavutil)
 *
 * Ubuntu/Debian 安装依赖:
 * sudo apt-get install libavformat-dev libavcodec-dev libavutil-dev
 *
 * 编译方法 (需要链接库):
 * g++ h264_analyzer.cpp -o h264_analyzer -lavformat -lavcodec -lavutil
 *
 * 使用方法:
 * ./h264_analyzer <video_file> <output_h264_file>
 */

// NALU 类型描述映射表
std::string getNaluDescription(int type) {
    switch (type) {
        case 0: return "Unspecified";
        case 1: return "SLICE (Non-IDR P/B frame)";  // 非关键帧
        case 2: return "SLICE_DPA";
        case 3: return "SLICE_DPB";
        case 4: return "SLICE_DPC";
        case 5: return "IDR_SLICE (Key Frame)";     // 关键帧
        case 6: return "SEI (Supplemental Enhancement Info)";  // 补充增强信息
        case 7: return "SPS (Sequence Parameter Set)";         // 序列参数集
        case 8: return "PPS (Picture Parameter Set)";         // 图像参数集
        case 9: return "AUD (Access Unit Delimiter)";         // 访问单元分隔符
        case 10: return "END_SEQ";
        case 11: return "END_STREAM";
        case 12: return "FILLER";
        default: return "Unknown (" + std::to_string(type) + ")";
    }
}

/// NALU信息结构体
struct NaluInfo {
    int forbidden{};  ///< 禁止位 (1位)
    int nri{};        ///< 重要性指示 (2位)
    int type{};       ///< NAL单元类型 (5位)
    std::string desc; ///< 类型描述
};

/**
 * @brief 解析NALU头部信息
 * 
 * NALU Header格式: [F(1) | NRI(2) | Type(5)]
 * F: 禁止位，一般为0
 * NRI: 重要性指示，值越大越重要
 * Type: NAL单元类型，决定NALU的用途
 * 
 * @param byteVal NALU头部字节
 * @return NaluInfo 解析后的NALU信息
 */
NaluInfo parseNaluHeader(uint8_t byteVal) {
    NaluInfo info;
    // 禁止位 (第7位)
    info.forbidden = (byteVal >> 7) & 0x01;
    // 重要性指示 (第5-6位)
    info.nri = (byteVal >> 5) & 0x03;
    // 单元类型 (第0-4位)
    info.type = byteVal & 0x1F;
    info.desc = getNaluDescription(info.type);
    return info;
}

/**
 * @brief 使用FFmpeg API提取H.264流
 * 
 * 核心流程:
 * 1. 打开输入文件
 * 2. 查找视频流信息
 * 3. 初始化bitstream filter (h264_mp4toannexb)
 * 4. 读取数据包并处理
 * 5. 写入输出文件
 * 
 * @param inputPath 输入文件路径
 * @param outputPath 输出H.264文件路径
 * @return bool 是否成功提取
 */
bool extractH264(const std::string& inputPath, const std::string& outputPath) {
    std::cout << "[*] 正在通过 FFmpeg API 从 " << inputPath << " 提取 H.264 流..." << std::endl;

    /// 输入文件格式上下文
    AVFormatContext* ifmt_ctx = nullptr;
    /// bitstream过滤器
    const AVBitStreamFilter* bsf = nullptr;
    /// bitstream过滤器上下文
    AVBSFContext* bsf_ctx = nullptr;
    /// 输出文件指针
    FILE* out_file = nullptr;
    /// 数据包
    AVPacket* pkt = nullptr;
    /// 视频流索引
    int video_stream_index = -1;
    /// 操作是否成功
    bool success = false;

    // 1. 打开输入文件
    // avformat_open_input会自动检测文件格式
    if (avformat_open_input(&ifmt_ctx, inputPath.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "[!] 无法打开输入文件: " << inputPath << std::endl;
        return false;
    }

    // 2. 获取流信息
    // 这一步会填充流的信息，比如编解码器参数等
    if (avformat_find_stream_info(ifmt_ctx, nullptr) < 0) {
        std::cerr << "[!] 无法获取流信息" << std::endl;
        goto cleanup;
    }

    // 3. 寻找视频流
    // 遍历所有流，找到第一个视频流
    for (unsigned int i = 0; i < ifmt_ctx->nb_streams; i++) {
        if (ifmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = static_cast<int>(i);  // 修复类型转换问题
            break;
        }
    }

    if (video_stream_index == -1) {
        std::cerr << "[!] 输入文件中未找到视频流" << std::endl;
        goto cleanup;
    }

    // 4. 初始化 Bitstream Filter (h264_mp4toannexb)
    // 这一步至关重要：MP4 通常使用 AVCC 格式（无 StartCode），
    // 分析 H.264 裸流通常需要 Annex B 格式（带 00 00 00 01 StartCode）。
    bsf = av_bsf_get_by_name("h264_mp4toannexb");
    if (!bsf) {
        std::cerr << "[!] FFmpeg 不支持 h264_mp4toannexb 过滤器" << std::endl;
        goto cleanup;
    }

    // 分配bitstream过滤器上下文
    if (av_bsf_alloc(bsf, &bsf_ctx) < 0) {
        std::cerr << "[!] 无法分配 BSF 上下文" << std::endl;
        goto cleanup;
    }

    // 将输入流的参数复制给 Filter
    if (avcodec_parameters_copy(bsf_ctx->par_in, ifmt_ctx->streams[video_stream_index]->codecpar) < 0) {
        std::cerr << "[!] 无法复制编解码器参数" << std::endl;
        goto cleanup;
    }

    // 初始化bitstream过滤器
    if (av_bsf_init(bsf_ctx) < 0) {
        std::cerr << "[!] 无法初始化 BSF" << std::endl;
        goto cleanup;
    }

    // 5. 打开输出文件
    out_file = fopen(outputPath.c_str(), "wb");
    if (!out_file) {
        std::cerr << "[!] 无法创建输出文件: " << outputPath << std::endl;
        goto cleanup;
    }

    // 6. 循环读取并处理包
    // 分配数据包
    pkt = av_packet_alloc();
    if (!pkt) {
        std::cerr << "[!] 无法分配 AVPacket" << std::endl;
        goto cleanup;
    }

    std::cout << "[*] 开始处理数据包..." << std::endl;
    // 循环读取帧数据
    while (av_read_frame(ifmt_ctx, pkt) >= 0) {
        // 只处理视频流的数据包
        if (pkt->stream_index == video_stream_index) {
            // 发送 Packet 到过滤器
            // 注意: av_bsf_send_packet 成功时会接管 pkt 的引用所有权
            int ret = av_bsf_send_packet(bsf_ctx, pkt);
            if (ret < 0) {
                // 发送失败，我们仍然拥有 pkt，需要手动释放
                av_packet_unref(pkt);
                continue;
            }

            // 从过滤器接收处理后的 Packet (可能一个输入对应多个输出)
            while (true) {
                ret = av_bsf_receive_packet(bsf_ctx, pkt);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    // EAGAIN表示需要发送更多数据，EOF表示结束
                    break;
                }
                if (ret < 0) {
                    // 其他错误
                    break;
                }

                // 写入文件
                fwrite(pkt->data, 1, pkt->size, out_file);

                // 释放从 BSF 接收到的 Packet 数据
                av_packet_unref(pkt);
            }
        } else {
            // 非视频流，释放
            av_packet_unref(pkt);
        }
    }

    success = true;
    std::cout << "[+] 提取成功: " << outputPath << std::endl;

    // 清理资源
    cleanup:
    if (pkt) av_packet_free(&pkt);
    if (bsf_ctx) av_bsf_free(&bsf_ctx);
    if (ifmt_ctx) avformat_close_input(&ifmt_ctx);
    if (out_file) fclose(out_file);

    return success;
}

/**
 * @brief 分析H.264二进制文件结构
 * 
 * 该函数会查找NALU的起始码(0x000001或0x00000001)，
 * 并解析每个NALU的头部信息。
 * 
 * @param filePath H.264文件路径
 * @param maxNalus 最大分析的NALU数量，默认20个
 */
void analyzeH264Stream(const std::string& filePath, int maxNalus = 20) {
    std::cout << "\n[*] 开始分析文件结构: " << filePath << std::endl;
    std::cout << "[*] 仅显示前 " << maxNalus << " 个 NALU...\n" << std::endl;

    // 打印表头
    std::cout << std::string(90, '-') << std::endl;
    std::cout << std::left << std::setw(15) << "Offset (Hex)"
              << "| " << std::setw(12) << "Start Code"
              << "| " << std::setw(9) << "Type ID"
              << "| " << std::setw(5) << "NRI"
              << "| " << "Description" << std::endl;
    std::cout << std::string(90, '-') << std::endl;

    // 打开文件
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "[!] 无法读取文件: " << filePath << std::endl;
        return;
    }

    // 获取文件大小并回到文件开头
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    // 读取整个文件到内存 (注意：对于超大文件可能不适用，但用于学习分析足够)
    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        std::cerr << "[!] 读取文件失败" << std::endl;
        return;
    }

    // 查找NALU
    int naluCount = 0;
    size_t idx = 0;

    while (idx < size && naluCount < maxNalus) {
        // 确保有足够空间读取 Start Code
        if (idx + 3 >= size) break;

        // 查找起始码长度 (3字节或4字节)
        int startCodeLen = 0;
        if (buffer[idx] == 0x00 && buffer[idx+1] == 0x00 && buffer[idx+2] == 0x01) {
            startCodeLen = 3;  // 3字节起始码
        } else if (buffer[idx] == 0x00 && buffer[idx+1] == 0x00 && buffer[idx+2] == 0x00 && buffer[idx+3] == 0x01) {
            startCodeLen = 4;  // 4字节起始码
        }

        if (startCodeLen > 0) {
            // 找到起始码，下一个字节就是NALU头部
            size_t headerPos = idx + startCodeLen;
            if (headerPos < size) {
                uint8_t headerByte = buffer[headerPos];
                NaluInfo info = parseNaluHeader(headerByte);

                // 格式化起始码为十六进制字符串
                std::string startCodeHex = (startCodeLen == 3) ? "000001" : "00000001";

                // 格式化输出NALU信息
                std::cout << "0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(8) << idx
                          << "   | " << std::setfill(' ') << std::setw(10) << startCodeHex
                          << " | " << std::setw(7) << std::dec << info.type
                          << " | " << std::setw(3) << info.nri
                          << " | " << info.desc << std::endl;

                // 对于关键信息(SPS/PPS/IDR)进行高亮显示
                if (info.type == 5 || info.type == 7 || info.type == 8) {
                    std::cout << "             ^--- 关键信息 (" << info.desc << ")" << std::endl;
                }
                naluCount++;
            }
            // 跳过当前的起始码，继续搜索
            idx += startCodeLen;
        } else {
            // 未找到起始码，向前移动一个字节
            idx++;
        }
    }

    // 如果没有找到任何NALU，说明可能不是Annex B格式
    if (naluCount == 0) {
        std::cout << "[!] 未找到 NALU Start Code。这可能不是 Annex B 格式的 H.264 文件 (可能是 mp4 模式?)" << std::endl;
    }
}

/**
 * @brief 检查字符串是否以特定后缀结尾
 * 
 * @param str 要检查的字符串
 * @param suffix 后缀
 * @return bool 是否以该后缀结尾
 */
bool hasSuffix(const std::string &str, const std::string &suffix) {
    if (str.length() >= suffix.length()) {
        return (0 == str.compare(str.length() - suffix.length(), suffix.length(), suffix));
    }
    return false;
}

/**
 * @brief 程序主入口
 * 
 * 程序功能：
 * 1. 如果输入文件是.h264/.264文件，直接进行分析
 * 2. 如果是其他视频文件，先提取H.264流再分析
 * 
 * @param argc 命令行参数数量
 * @param argv 命令行参数数组
 * @return int 程序退出码
 */
int main(int argc, char* argv[]) {

    // 检查命令行参数
    if(argc < 3){
        std::cout << "Usage: " << argv[0] << " <input_file> <output_file>" << std::endl;
        return 1;
    }

    // 获取命令行参数
    std::string targetFile = argv[1];
    std::string outputH264 = argv[2];

    std::cout << "=== H.264 学习助手 (C++ API 版) ===" << std::endl;

    // 检查文件是否存在
    std::ifstream f(targetFile.c_str());
    bool fileExists = f.good();
    f.close();

    // 判断是否为H.264文件
    bool isH264 = hasSuffix(targetFile, ".h264") || hasSuffix(targetFile, ".264");

    // 注意：av_register_all() 在 FFmpeg 4.0 之后已弃用，因此这里不需要调用

    if (isH264 && fileExists) {
        // 如果已经是H.264裸流文件，直接分析
        analyzeH264Stream(targetFile);
    } else if (fileExists) {
        // 使用 API 提取并分析
        if (extractH264(targetFile, outputH264)) {
            analyzeH264Stream(outputH264);
        }
    } else {
        std::cout << "[!] 文件 " << targetFile << " 不存在。请提供有效的视频文件路径。" << std::endl;
    }

    return 0;
}