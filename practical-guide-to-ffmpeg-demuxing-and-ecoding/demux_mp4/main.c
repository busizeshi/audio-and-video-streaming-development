/**
 * MP4文件解复用程序
 * 功能：将MP4文件中的H.264视频流和AAC音频流分离出来，
 * 分别保存为独立的H.264和AAC文件
 */

#include <stdio.h>

// FFmpeg库头文件
#include "libavutil/log.h"        // FFmpeg日志相关函数
#include "libavformat/avformat.h" // FFmpeg容器格式处理函数
#include "libavcodec/bsf.h"       // FFmpeg比特流过滤器相关函数

// 错误信息缓冲区大小
#define ERROR_STRING_SIZE 1024

// ADTS头部长度
#define ADTS_HEADER_LEN  7

// AAC支持的采样率列表
const int sampling_frequencies[] = {
        96000,  // 0x0
        88200,  // 0x1
        64000,  // 0x2
        48000,  // 0x3
        44100,  // 0x4
        32000,  // 0x5
        24000,  // 0x6
        22050,  // 0x7
        16000,  // 0x8
        12000,  // 0x9
        11025,  // 0xa
        8000    // 0xb
};

/**
 * 构造AAC ADTS头部
 * @param p_adts_header ADTS头部缓冲区指针
 * @param data_length AAC原始数据长度
 * @param profile AAC编码配置文件
 * @param samplerate 采样率
 * @param channels 声道数
 * @return 成功返回0，失败返回-1
 */
int adts_header(char *const p_adts_header, const int data_length,
                const int profile, const int samplerate,
                const int channels) {

    // 查找采样率对应的索引
    int sampling_frequency_index = 3;
    int adtsLen = data_length + ADTS_HEADER_LEN;

    int frequencies_size = sizeof(sampling_frequencies) / sizeof(sampling_frequencies[0]);
    int i = 0;
    for (i = 0; i < frequencies_size; i++) {
        if (sampling_frequencies[i] == samplerate) {
            sampling_frequency_index = i;
            break;
        }
    }
    // 如果找不到匹配的采样率，报错返回
    if (i >= frequencies_size) {
        printf("unsupported samplerate:%d\n", samplerate);
        return -1;
    }

    // 构造ADTS头部的7个字节
    // ADTS头部结构参考ISO/IEC 14496-3标准
    p_adts_header[0] = (char)0xff; // 同步字节高8位 (0xFF)
    p_adts_header[1] = (char)0xf0; // 同步字节低4位 + 版本等字段
    p_adts_header[1] |= (0 << 3);  // MPEG版本标识(MPEG Identifier): 0表示MPEG-4
    p_adts_header[1] |= (0 << 1);  // 层(Layer): 固定为00
    p_adts_header[1] |= 1;         // 保护缺失(Protection absent): 1表示无CRC校验

    // 配置信息字段
    p_adts_header[2] = (char)((profile) << 6);                                  // AAC配置文件(Profile)
    p_adts_header[2] |= (char)((sampling_frequency_index & 0x0f) << 2);         // 采样率索引(Sampling frequency index)
    p_adts_header[2] |= (char)(0 << 1);                                         // 私有位(Private bit)
    p_adts_header[2] |= (char)((channels & 0x04) >> 2);                         // 声道配置高1位(Channel configuration)

    // 更多配置字段
    p_adts_header[3] = (char)((channels & 0x03) << 6);                          // 声道配置低2位(Channel configuration)
    p_adts_header[3] |= (char)(0 << 5);                                         // 原始数据流位置(Originality)
    p_adts_header[3] |= (char)(0 << 4);                                         // 家庭版标识(Home)
    p_adts_header[3] |= (char)(0 << 3);                                         // 版权标识位(Copyright identification bit)
    p_adts_header[3] |= (char)(0 << 2);                                         // 版权标识起始(Copyright identification start)
    p_adts_header[3] |= (char)((adtsLen & 0x1800) >> 11);                       // 数据长度高2位(Length)

    // 数据长度中间8位
    p_adts_header[4] = (char) ((adtsLen & 0x7f8) >> 3);
    // 数据长度低3位 + 缓冲区满度高5位
    p_adts_header[5] = (char) ((adtsLen & 0x7) << 5);
    p_adts_header[5] |= (char)0x1f;                                             // 缓冲区满度低6位(Buffer fullness)
    p_adts_header[6] = (char)0xfc;                                              // 可靠数据块个数(Number of raw data blocks in frame)

    return 0;
}


/**
 * 主函数
 * 用法: app input.mp4 out.h264 out.aac
 * @param argc 参数个数
 * @param argv 参数数组
 * @return 成功返回0，失败返回非0值
 */
int main(int argc, char **argv) {
    // 检查命令行参数数量是否正确
    if (argc != 4) {
        printf("usage app input.mp4  out.h264 out.aac");
        return -1;
    }

    // 获取输入输出文件名
    char *in_filename = argv[1];       // 输入MP4文件路径
    char *h264_filename = argv[2];     // 输出H.264文件路径
    char *aac_filename = argv[3];      // 输出AAC文件路径
    
    // 文件描述符
    FILE *aac_fd;
    FILE *h264_fd;

    // 打开H.264输出文件
    h264_fd = fopen(h264_filename, "wb");
    if (!h264_fd) {
        printf("open %s failed\n", h264_filename);
        return -1;
    }

    // 打开AAC输出文件
    aac_fd = fopen(aac_filename, "wb");
    if (!aac_fd) {
        printf("open %s failed\n", aac_filename);
        return -1;
    }

    // FFmpeg相关变量声明
    AVFormatContext *ifmt_ctx = nullptr;  // 输入文件格式上下文
    int video_index = -1;                 // 视频流索引
    int audio_index = -1;                 // 音频流索引
    AVPacket *pkt = nullptr;              // 数据包
    int ret = 0;                          // 返回值
    char errors[ERROR_STRING_SIZE + 1];   // 错误信息缓冲区

    // 分配输入格式上下文
    ifmt_ctx = avformat_alloc_context();
    if (!ifmt_ctx) {
        printf("avformat_alloc_context failed\n");
        return -1;
    }

    // 打开输入文件
    ret = avformat_open_input(&ifmt_ctx, in_filename, nullptr, nullptr);
    if (ret < 0) {
        av_strerror(ret, errors, ERROR_STRING_SIZE);
        printf("avformat_open_input failed:%d\n", ret);
        printf("avformat_open_input failed:%s\n", errors);
        avformat_close_input(&ifmt_ctx);
        return -1;
    }

    // 查找最佳视频流
    video_index = av_find_best_stream(ifmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_index == -1) {
        printf("av_find_best_stream video_index failed\n");
        avformat_close_input(&ifmt_ctx);
        return -1;
    }

    // 查找最佳音频流
    audio_index = av_find_best_stream(ifmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audio_index == -1) {
        printf("av_find_best_stream audio_index failed\n");
        avformat_close_input(&ifmt_ctx);
        return -1;
    }

    // 获取H.264 MP4到Annex B格式转换的比特流过滤器
    const AVBitStreamFilter *bsf_filter = av_bsf_get_by_name("h264_mp4toannexb");
    if (!bsf_filter) {
        avformat_close_input(&ifmt_ctx);
        printf("av_bsf_get_by_name h264_mp4toannexb failed\n");
        return -1;
    }
    
    // 分配比特流过滤器上下文
    AVBSFContext *bsf_ctx = nullptr;
    ret = av_bsf_alloc(bsf_filter, &bsf_ctx);
    if (ret < 0) {
        av_strerror(ret, errors, ERROR_STRING_SIZE);
        printf("av_bsf_alloc failed:%s\n", errors);
        avformat_close_input(&ifmt_ctx);
        return -1;
    }
    
    // 复制视频流的编解码参数到比特流过滤器
    ret = avcodec_parameters_copy(bsf_ctx->par_in, ifmt_ctx->streams[video_index]->codecpar);
    if (ret < 0) {
        av_strerror(ret, errors, ERROR_STRING_SIZE);
        printf("avcodec_parameters_copy failed:%s\n", errors);
        avformat_close_input(&ifmt_ctx);
        av_bsf_free(&bsf_ctx);
        return -1;
    }
    
    // 初始化比特流过滤器
    ret = av_bsf_init(bsf_ctx);
    if (ret < 0) {
        av_strerror(ret, errors, ERROR_STRING_SIZE);
        printf("av_bsf_init failed:%s\n", errors);
        avformat_close_input(&ifmt_ctx);
        av_bsf_free(&bsf_ctx);
        return -1;
    }

    // 分配数据包
    pkt = av_packet_alloc();
    while (1) {
        // 从输入文件读取一帧数据
        ret = av_read_frame(ifmt_ctx, pkt);
        if (ret < 0) {
            av_strerror(ret, errors, ERROR_STRING_SIZE);
            printf("av_read_frame failed:%s\n", errors);
            break;
        }
        
        // 处理视频流数据
        if (pkt->stream_index == video_index) {
            // 发送数据包到比特流过滤器
            ret = av_bsf_send_packet(bsf_ctx, pkt);
            if (ret < 0) {
                av_strerror(ret, errors, ERROR_STRING_SIZE);
                printf("av_bsf_send_packet failed:%s\n", errors);
                av_packet_unref(pkt);
                continue;
            }
            
            // 接收处理后的数据包
            while (1) {
                ret = av_bsf_receive_packet(bsf_ctx, pkt);
                if (ret != 0) {
                    break;
                }
                
                // 将处理后的H.264数据写入输出文件
                size_t size = fwrite(pkt->data, 1, pkt->size, h264_fd);
                if (size != pkt->size) {
                    av_log(NULL, AV_LOG_DEBUG, "h264 warning, length of wrote data isn't equal pkt->size(%zu, %d)\n",
                           size,
                           pkt->size);
                }
                av_packet_unref(pkt);
            }
        } 
        // 处理音频流数据
        else if (pkt->stream_index == audio_index) {
            // 创建ADTS头部缓冲区
            char adts_header_buf[7] = {0};
            
            // 构造ADTS头部
            adts_header(adts_header_buf, pkt->size,
                        ifmt_ctx->streams[audio_index]->codecpar->profile,
                        ifmt_ctx->streams[audio_index]->codecpar->sample_rate,
                        ifmt_ctx->streams[audio_index]->codecpar->ch_layout.nb_channels);
            
            // 写入ADTS头部
            fwrite(adts_header_buf, 1, 7, aac_fd);
            
            // 写入AAC原始数据
            size_t size = fwrite(pkt->data, 1, pkt->size, aac_fd);
            if (size != pkt->size) {
                av_log(NULL, AV_LOG_DEBUG, "aac warning, length of writed data isn't equal pkt->size(%zu, %d)\n",
                       size,
                       pkt->size);
            }
            av_packet_unref(pkt);
        } 
        // 其他流的数据包直接释放
        else {
            av_packet_unref(pkt);
        }
    }

    printf("while finish\n");
    
    // 清理资源标签
    failed:
    // 关闭输出文件
    fclose(h264_fd);
    fclose(aac_fd);
    
    // 释放数据包
    if (pkt)
        av_packet_free(&pkt);
    
    // 关闭输入文件
    if (ifmt_ctx)
        avformat_close_input(&ifmt_ctx);

    printf("Hello World!\n");
    return 0;
}