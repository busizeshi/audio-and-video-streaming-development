/*
 * FFmpeg Filter Graph Demo (C++ version)
 * Target FFmpeg Version: 6.1
 */

#include <iostream>
#include <string>
#include <memory>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
}

// 自定义删除器，用于 std::unique_ptr
struct FrameDeleter {
    void operator()(AVFrame *frame) const {
        av_frame_free(&frame);
    }
};

struct GraphDeleter {
    void operator()(AVFilterGraph *graph) const {
        avfilter_graph_free(&graph);
    }
};

AVFilterContext *create_and_link_filter(
    AVFilterGraph *graph,
    const std::string &filter_name,
    const std::string &instance_name,
    const std::string &args) {
    const AVFilter *filter = avfilter_get_by_name(filter_name.c_str());
    if (!filter) {
        std::cerr << "Could not find filter: " << filter_name << std::endl;
        return nullptr;
    }

    AVFilterContext *ctx = nullptr;
    int ret = avfilter_graph_create_filter(&ctx, filter, instance_name.c_str(),
                                           args.empty() ? nullptr : args.c_str(),
                                           nullptr, graph);
    if (ret < 0) {
        std::cerr << "Failed to create filter context: " << instance_name << std::endl;
        return nullptr;
    }
    return ctx;
}

// ffmpeg -i test_1280x720.mp4 -t 10 -pix_fmt yuv420p yuv420p_1280x720.yuv
// ffplay -pixel_format yuv420p -video_size 1280x720 -framerate 5 yuv420p_1280x720.yuv
int main(int argc, char *argv[]) {
    // 配置参数
    const int in_width = 1280;
    const int in_height = 720;
    const AVPixelFormat in_fmt = AV_PIX_FMT_YUV420P;

    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input file> <output file>" << std::endl;
    }
    const char *inFileName = argv[1];
    const char *outFileName = argv[2];

    // 1. 打开文件 (使用标准 C IO，方便处理二进制 buffer)
    FILE *inFile = fopen(inFileName, "rb");
    if (!inFile) {
        std::cerr << "Fail to open input file: " << inFileName << std::endl;
        return -1;
    }

    FILE *outFile = fopen(outFileName, "wb");
    if (!outFile) {
        std::cerr << "Fail to open output file: " << outFileName << std::endl;
        fclose(inFile);
        return -1;
    }

    // 2. 创建 Filter Graph (使用智能指针管理)
    std::unique_ptr<AVFilterGraph, GraphDeleter> filter_graph(avfilter_graph_alloc());
    if (!filter_graph) {
        return -1;
    }

    // ==========================================
    // 构建滤镜链：手动连接 (Manual Linking)
    // 拓扑：BufferSrc -> Split -> [Overlay(Back), Crop->Vflip->Overlay(Fore)] -> Sink
    // ==========================================

    int ret = 0;

    // --- A. 创建 Buffer Source (入口) ---
    char args[512];
    snprintf(args, sizeof(args),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
             in_width, in_height, in_fmt, 1, 25, 1, 1);

    AVFilterContext *bufferSrc_ctx = create_and_link_filter(filter_graph.get(), "buffer", "in", args);
    if (!bufferSrc_ctx) return -1;

    // --- B. 创建 Buffer Sink (出口) ---
    // FFmpeg 6.1 推荐方式：先创建 Context，再通过 av_opt_set 设置参数
    AVFilterContext *bufferSink_ctx = create_and_link_filter(filter_graph.get(), "buffersink", "out", "");
    if (!bufferSink_ctx) return -1;

    // 设置 Sink 支持的输出格式 (替代旧的 av_buffersink_params_alloc)
    enum AVPixelFormat pix_fmts[] = {AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE};
    // "pixel_fmts" 是 buffersink 的选项名，传入 int 列表
    ret = av_opt_set_int_list(bufferSink_ctx, "pix_fmts", pix_fmts, AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        std::cerr << "Cannot set output pixel format" << std::endl;
        return -1;
    }

    // --- C. 创建中间处理滤镜 ---

    // Split: 1路进，2路出
    AVFilterContext *split_ctx = create_and_link_filter(filter_graph.get(), "split", "split", "outputs=2");

    // Crop: 裁剪上半部分
    AVFilterContext *crop_ctx = create_and_link_filter(filter_graph.get(), "crop", "crop",
                                                       "out_w=iw:out_h=ih/2:x=0:y=0");

    // VFlip: 垂直翻转
    AVFilterContext *vflip_ctx = create_and_link_filter(filter_graph.get(), "vflip", "vflip", "");

    // Overlay: 叠加 (y=H/2 放在下半部分)
    AVFilterContext *overlay_ctx = create_and_link_filter(filter_graph.get(), "overlay", "overlay", "y=0:H/2");

    if (!split_ctx || !crop_ctx || !vflip_ctx || !overlay_ctx) {
        return -1;
    }

    // --- D. 连接 Link (Wiring) ---

    // 1. Source -> Split
    if (avfilter_link(bufferSrc_ctx, 0, split_ctx, 0) < 0) return -1;

    // 2. Split[0] -> Overlay[0] (背景)
    if (avfilter_link(split_ctx, 0, overlay_ctx, 0) < 0) return -1;

    // 3. Split[1] -> Crop -> VFlip -> Overlay[1] (前景/倒影)
    if (avfilter_link(split_ctx, 1, crop_ctx, 0) < 0) return -1;
    if (avfilter_link(crop_ctx, 0, vflip_ctx, 0) < 0) return -1;
    if (avfilter_link(vflip_ctx, 0, overlay_ctx, 1) < 0) return -1;

    // 4. Overlay -> Sink
    if (avfilter_link(overlay_ctx, 0, bufferSink_ctx, 0) < 0) return -1;

    // --- E. 配置生效 (Config) ---
    ret = avfilter_graph_config(filter_graph.get(), nullptr);
    if (ret < 0) {
        std::cerr << "Error configuring the filter graph." << std::endl;
        return -1;
    }

    // 打印调试图信息
    char *graph_str = avfilter_graph_dump(filter_graph.get(), nullptr);
    std::cout << "Graph Description:\n" << graph_str << std::endl;
    av_free(graph_str);

    // 3. 准备 Frames (智能指针管理)
    // frame_in: 用于存放读取的 YUV 数据
    std::unique_ptr<AVFrame, FrameDeleter> frame_in(av_frame_alloc());
    // frame_out: 用于接收处理后的数据
    std::unique_ptr<AVFrame, FrameDeleter> frame_out(av_frame_alloc());

    // 分配输入帧的内存 (这是 FFmpeg 标准做法，自动处理对齐)
    frame_in->width = in_width;
    frame_in->height = in_height;
    frame_in->format = in_fmt;
    if (av_frame_get_buffer(frame_in.get(), 32) < 0) {
        // 32字节对齐
        std::cerr << "Error allocating frame buffer." << std::endl;
        return -1;
    }

    // 4. 处理循环
    uint32_t frame_count = 0;
    bool finished = false;

    while (!finished) {
        // --- 读取 RAW YUV ---
        // 注意：AVFrame 的 linesize 可能大于 width (Padding)，必须逐行读取
        // Y Plane
        for (int i = 0; i < in_height; i++) {
            // 读取一行数据到 frame 的 buffer 中
            if (fread(frame_in->data[0] + i * frame_in->linesize[0], 1, in_width, inFile) != in_width) {
                finished = true;
                break;
            }
        }
        if (finished) break;

        // U Plane (高/2, 宽/2)
        for (int i = 0; i < in_height / 2; i++) {
            if (fread(frame_in->data[1] + i * frame_in->linesize[1], 1, in_width / 2, inFile) != in_width / 2) {
                finished = true;
                break;
            }
        }

        // V Plane (高/2, 宽/2)
        for (int i = 0; i < in_height / 2; i++) {
            if (fread(frame_in->data[2] + i * frame_in->linesize[2], 1, in_width / 2, inFile) != in_width / 2) {
                finished = true;
                break;
            }
        }

        // 设置基本信息
        frame_in->pts = frame_count;

        // --- Push: 送入滤镜图 ---
        // av_buffersrc_add_frame_flags 使用 AV_BUFFERSRC_FLAG_KEEP_REF
        // 这样 frame_in 的数据所有权还在我们要手里，我们可以重用它读取下一帧
        ret = av_buffersrc_add_frame_flags(bufferSrc_ctx, frame_in.get(), AV_BUFFERSRC_FLAG_KEEP_REF);
        if (ret < 0) {
            std::cerr << "Error feeding the filter graph." << std::endl;
            break;
        }

        // --- Pull: 从滤镜图获取 ---
        while (true) {
            ret = av_buffersink_get_frame(bufferSink_ctx, frame_out.get());
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break; // 需要更多输入 或 结束
            }
            if (ret < 0) {
                std::cerr << "Error getting frame from sink." << std::endl;
                finished = true;
                break;
            }

            // --- 写入 Output ---
            // 同样需要逐行写入，因为 Sink 出来的 frame 可能也有 Padding
            if (frame_out->format == AV_PIX_FMT_YUV420P) {
                // Y
                for (int i = 0; i < frame_out->height; i++) {
                    fwrite(frame_out->data[0] + i * frame_out->linesize[0], 1, frame_out->width, outFile);
                }
                // U
                for (int i = 0; i < frame_out->height / 2; i++) {
                    fwrite(frame_out->data[1] + i * frame_out->linesize[1], 1, frame_out->width / 2, outFile);
                }
                // V
                for (int i = 0; i < frame_out->height / 2; i++) {
                    fwrite(frame_out->data[2] + i * frame_out->linesize[2], 1, frame_out->width / 2, outFile);
                }
            }

            // 必须释放 frame_out 内部的引用，以便下一次循环使用
            av_frame_unref(frame_out.get());
        }

        frame_count++;
        if (frame_count % 25 == 0) {
            std::cout << "Processed " << frame_count << " frames." << std::endl;
        }

        // 如果想要复用 frame_in，需要确保它是可写的（通常 add_frame_flags KEEP_REF 后是安全的，
        // 但严谨做法是 make_writable，不过对于 pure reading loop，直接覆盖数据通常没问题）
    }

    // Cleanup
    fclose(inFile);
    fclose(outFile);

    // unique_ptr 会自动调用 deleter 释放 graph 和 frame，无需手动 av_free
    std::cout << "Done. Total frames: " << frame_count << std::endl;

    return 0;
}
