/**
 * @file main.c
 * @brief 解析获取H264视频数据
 * 
 * 这个程序演示了如何使用FFmpeg库从媒体文件中提取H.264视频流，
 * 并将其保存为原始H.264格式文件。
 * 
 * 主要功能：
 * 1. 打开输入媒体文件
 * 2. 查找视频流信息
 * 3. 使用bitstream过滤器将H.264数据从MP4封装转换为Annex B格式
 * 4. 提取视频数据包并写入输出文件
 */
#include <stdio.h>
#include <libavformat/avio.h>
#include <libavformat/avformat.h>
#include <libavcodec/bsf.h>

/// 错误信息缓冲区
static char arr_buf[128] = {0};

/**
 * @brief 获取FFmpeg错误码对应的错误描述字符串
 * 
 * @param err_num FFmpeg错误码
 * @return 错误描述字符串
 */
static char *av_get_error(int err_num) {
    av_strerror(err_num, arr_buf, sizeof(arr_buf));
    return arr_buf;
}

/**
 * @brief 程序主函数
 * 
 * @param argc 命令行参数数量
 * @param argv 命令行参数数组
 * @return int 程序退出码
 * 
 * 使用方法: ./extract_h264 <输入文件> <输出文件>
 * 示例: ./extract_h264 input.mp4 output.h264
 */
int main(int argc, char *argv[]) {
    /// 输入文件格式上下文
    AVFormatContext *ifmt_ctx = NULL;
    /// 视频流索引
    int video_index = -1;
    /// 数据包，用于存储从文件中读取的音频/视频数据
    AVPacket *pkt = NULL;
    /// 返回值，用于存储FFmpeg函数的返回状态
    int ret = -1;
    /// 文件读取结束标志
    int file_end = 0;

    // 检查命令行参数数量
    if (argc < 3) {
        printf("Usage: %s <input file> <output file>\n", argv[0]);
        return -1;
    }

    // 打开输出文件用于写入
    FILE *outfile = fopen(argv[2], "wb");
    printf("open %s\n", argv[2]);
    if (!outfile) {
        printf("open %s failed\n", argv[2]);
        return -1;
    }

    // 分配输入格式上下文
    ifmt_ctx = avformat_alloc_context();
    if (!ifmt_ctx) {
        printf("alloc context failed\n");
        return -1;
    }

    // 打开输入文件
    ret = avformat_open_input(&ifmt_ctx, argv[1], NULL, NULL);
    if (ret < 0) {
        printf("open %s failed: %s\n", argv[1], av_get_error(ret));
        return -1;
    }

    // 查找输入文件的流信息
    ret = avformat_find_stream_info(ifmt_ctx, NULL);
    if (ret < 0) {
        printf("find stream info failed: %s\n", av_get_error(ret));
        return -1;
    }

    // 查找最佳视频流
    video_index = av_find_best_stream(ifmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (video_index < 0) {
        printf("find video stream failed: %s\n", av_get_error(ret));
        avformat_close_input(&ifmt_ctx);
        return -1;
    }

    // 分配数据包
    pkt = av_packet_alloc();

    // 获取H.264 MP4到Annex B格式的bitstream过滤器
    // 这个过滤器用于将MP4封装的H.264数据转换为原始H.264流格式
    const AVBitStreamFilter *bsf_filter = av_bsf_get_by_name("h264_mp4toannexb");
    /// Bitstream过滤器上下文
    AVBSFContext *bsf_ctx = NULL;
    // 分配bitstream过滤器上下文
    ret = av_bsf_alloc(bsf_filter, &bsf_ctx);
    // 将视频流的编解码参数复制到过滤器输入参数中
    avcodec_parameters_copy(bsf_ctx->par_in, ifmt_ctx->streams[video_index]->codecpar);
    // 初始化bitstream过滤器
    av_bsf_init(bsf_ctx);

    // 循环读取数据帧
    file_end = 0;
    while (0 == file_end) {
        // 读取一帧数据
        if ((ret = av_read_frame(ifmt_ctx, pkt)) < 0) {
            printf("read frame failed: %s\n", av_get_error(ret));
            file_end = 1;
        }
        
        // 如果读取成功且是视频数据包
        if (ret == 0 && pkt->stream_index == video_index) {
#if 1
            /// 输入数据包大小
            int input_size=pkt->size;
            /// 输出数据包计数
            int out_pkt_count=0;
            
            // 将数据包发送到bitstream过滤器
            if(av_bsf_send_packet(bsf_ctx,pkt)!=0){
                av_packet_unref(pkt);
                continue;
            }
            // 取消引用已发送的数据包
            av_packet_unref(pkt);
            
            // 从bitstream过滤器接收处理后的数据包
            while(av_bsf_receive_packet(bsf_ctx, pkt) == 0){
                out_pkt_count++;
                // 将处理后的数据写入输出文件
                size_t size = fwrite(pkt->data, 1, pkt->size, outfile);
                if (size != pkt->size) {
                    printf("write file failed: %s\n", av_get_error(ret));
                }
                // 取消引用已处理的数据包
                av_packet_unref(pkt);
            }
            
            // 如果一个输入包产生了多个输出包，则打印提示信息
            if(out_pkt_count >= 2){
                printf("cur pkt(size:%d) only get 1 out pkt, it get %d pkts\n",
                       input_size, out_pkt_count);
            }
#else
            // 不使用bitstream过滤器的简单写入方式（已被弃用）
            size_t size = fwrite(pkt->data, 1, pkt->size, outfile);
            if (size != pkt->size) {
                printf("write file failed: %s\n", av_get_error(ret));
            }
            av_packet_unref(pkt);
#endif
        } else {
            // 如果不是视频数据包，直接取消引用
            if (ret == 0) {
                av_packet_unref(pkt);
            }
        }
    }
    
    // 清空bitstream过滤器缓冲区
    // 发送一个NULL包表示输入结束
    av_bsf_send_packet(bsf_ctx, NULL);
    // 接收过滤器中剩余的所有数据包
    while (av_bsf_receive_packet(bsf_ctx, pkt) == 0) {
        size_t size = fwrite(pkt->data, 1, pkt->size, outfile);
        if (size != pkt->size) {
            printf("write file failed: %s\n", av_get_error(ret));
        }
        av_packet_unref(pkt);
    }

    // 关闭输出文件
    fclose(outfile);
    // 释放bitstream过滤器上下文
    if (bsf_ctx)
        av_bsf_free(&bsf_ctx);
    // 释放数据包
    if (pkt)
        av_packet_free(&pkt);
    // 关闭输入文件
    if (ifmt_ctx)
        avformat_close_input(&ifmt_ctx);
    printf("finish\n");

    return 0;
}