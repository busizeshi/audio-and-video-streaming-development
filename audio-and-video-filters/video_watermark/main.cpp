#include <iostream>
#include <string>
#include <memory>
#include <cstdio>

// FFmpeg 是 C 语言库，在 C++ 中引用需要 extern "C"
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
}

// 辅助类：用于处理图片的加载和保存
class ImageUtils {
public:
    // 从 JPEG 文件读取 AVFrame
    static AVFrame* loadFromJpeg(const std::string& filename) {
        AVFormatContext* format_ctx = nullptr;
        if (avformat_open_input(&format_ctx, filename.c_str(), nullptr, nullptr) != 0) {
            std::cerr << "Error: Could not open file " << filename << std::endl;
            return nullptr;
        }

        avformat_find_stream_info(format_ctx, nullptr);

        const AVCodec* codec = nullptr;
        int video_stream_idx = av_find_best_stream(format_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
        if (video_stream_idx < 0) {
            std::cerr << "Error: Could not find video stream" << std::endl;
            avformat_close_input(&format_ctx);
            return nullptr;
        }

        AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(codec_ctx, format_ctx->streams[video_stream_idx]->codecpar);

        if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
            std::cerr << "Error: Could not open codec" << std::endl;
            avcodec_free_context(&codec_ctx);
            avformat_close_input(&format_ctx);
            return nullptr;
        }

        AVPacket* pkt = av_packet_alloc();
        AVFrame* frame = av_frame_alloc();
        AVFrame* result_frame = nullptr;

        while (av_read_frame(format_ctx, pkt) >= 0) {
            if (pkt->stream_index == video_stream_idx) {
                if (avcodec_send_packet(codec_ctx, pkt) == 0) {
                    if (avcodec_receive_frame(codec_ctx, frame) == 0) {
                        // 成功解码一帧，这里我们只需要第一帧图片
                        result_frame = av_frame_clone(frame); // 克隆一份返回，因为原 frame 要被释放
                        break;
                    }
                }
            }
            av_packet_unref(pkt);
        }

        av_packet_free(&pkt);
        av_frame_free(&frame);
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&format_ctx);

        return result_frame;
    }

    // 将 AVFrame 保存为 JPEG
    static int saveToJpeg(const std::string& filename, const AVFrame* frame) {
        const AVCodec* jpeg_codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
        if (!jpeg_codec) return -1;

        AVCodecContext* codec_ctx = avcodec_alloc_context3(jpeg_codec);
        if (!codec_ctx) return -2;

        codec_ctx->pix_fmt = AV_PIX_FMT_YUVJ420P; // JPEG 通常使用 YUVJ420P
        codec_ctx->width = frame->width;
        codec_ctx->height = frame->height;
        codec_ctx->time_base = AVRational{1, 25};
        codec_ctx->framerate = AVRational{25, 1};

        // 设置编码质量
        AVDictionary* opts = nullptr;
        av_dict_set(&opts, "qmin", "2", 0);
        av_dict_set(&opts, "qmax", "2", 0);

        if (avcodec_open2(codec_ctx, jpeg_codec, &opts) < 0) {
            avcodec_free_context(&codec_ctx);
            return -3;
        }
        av_dict_free(&opts);

        AVPacket* pkt = av_packet_alloc();
        int ret = avcodec_send_frame(codec_ctx, frame);
        if (ret < 0) {
            av_packet_free(&pkt);
            avcodec_free_context(&codec_ctx);
            return -4;
        }

        FILE* outfile = nullptr;
        while (ret >= 0) {
            ret = avcodec_receive_packet(codec_ctx, pkt);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;

            if (ret >= 0) {
                outfile = fopen(filename.c_str(), "wb");
                if (outfile) {
                    fwrite(pkt->data, 1, pkt->size, outfile);
                    fclose(outfile);
                }
                av_packet_unref(pkt);
                break; // 图片只存一帧
            }
        }

        av_packet_free(&pkt);
        avcodec_free_context(&codec_ctx);
        return (outfile != nullptr) ? 0 : -1;
    }
};

// 核心类：处理水印叠加
class WatermarkProcessor {
private:
    AVFilterGraph* filter_graph = nullptr;
    AVFilterContext* mainsrc_ctx = nullptr;
    AVFilterContext* logosrc_ctx = nullptr;
    AVFilterContext* resultsink_ctx = nullptr;

public:
    WatermarkProcessor() = default;

    ~WatermarkProcessor() {
        if (filter_graph) {
            avfilter_graph_free(&filter_graph);
        }
    }

    // 初始化滤镜图
    int init(const AVFrame* main_frame, const AVFrame* logo_frame, int x, int y) {
        filter_graph = avfilter_graph_alloc();
        if (!filter_graph) return -1;

        char args[1024];
        // 构建滤镜描述字符串
        // [main] 输入1: 主图
        // [logo] 输入2: Logo图
        // [main][logo]overlay=x:y[result]: 混合动作
        // [result]buffersink: 输出
        snprintf(args, sizeof(args),
                 "buffer=video_size=%dx%d:pix_fmt=%d:time_base=1/25:pixel_aspect=%d/%d[main];"
                 "buffer=video_size=%dx%d:pix_fmt=%d:time_base=1/25:pixel_aspect=%d/%d[logo];"
                 "[main][logo]overlay=%d:%d[result];"
                 "[result]buffersink",
                 main_frame->width, main_frame->height, main_frame->format, main_frame->sample_aspect_ratio.num, main_frame->sample_aspect_ratio.den,
                 logo_frame->width, logo_frame->height, logo_frame->format, logo_frame->sample_aspect_ratio.num, logo_frame->sample_aspect_ratio.den,
                 x, y);

        AVFilterInOut* inputs = nullptr;
        AVFilterInOut* outputs = nullptr;

        if (avfilter_graph_parse2(filter_graph, args, &inputs, &outputs) < 0) {
            std::cerr << "Error parsing graph string" << std::endl;
            return -1;
        }

        if (avfilter_graph_config(filter_graph, nullptr) < 0) {
            std::cerr << "Error configuring graph" << std::endl;
            return -1;
        }

        // 获取滤镜上下文，注意名字是 FFmpeg 自动生成的 parsing 名字，或者根据上面 string 中的 label 查找
        // avfilter_graph_parse2 解析出来的 filter 名称通常是 Parsed_filterName_index
        // 为了稳健性，这里使用 filter_graph->filters 数组或者更精确的查找方式。
        // 在原 C 代码中使用了 "Parsed_buffer_0" 这种硬编码名字，这依赖于 parse 的顺序。
        // 更安全的方法是根据我们在 string 中定义的 label ([main], [logo]) 来找，但 buffersrc 不直接支持按 label 找 context。
        // 最简单的方法是遍历 graph->filters 并打印名字确认，或者沿用原代码的假设（如果是简单 graph，顺序通常固定）。

        mainsrc_ctx = avfilter_graph_get_filter(filter_graph, "Parsed_buffer_0");
        logosrc_ctx = avfilter_graph_get_filter(filter_graph, "Parsed_buffer_1");
        resultsink_ctx = avfilter_graph_get_filter(filter_graph, "Parsed_buffersink_3");

        if (!mainsrc_ctx || !logosrc_ctx || !resultsink_ctx) {
             std::cerr << "Error: Could not find filters by name. Check filter names." << std::endl;
             return -1;
        }

        return 0;
    }

    // 执行处理
    int process(AVFrame* main_frame, AVFrame* logo_frame, AVFrame* result_frame) {
        // 1. 将数据送入 filter
        if (av_buffersrc_add_frame(mainsrc_ctx, main_frame) < 0) {
            std::cerr << "Error feeding main frame" << std::endl;
            return -1;
        }
        if (av_buffersrc_add_frame(logosrc_ctx, logo_frame) < 0) {
            std::cerr << "Error feeding logo frame" << std::endl;
            return -1;
        }

        // 2. 从 sink 获取结果
        if (av_buffersink_get_frame(resultsink_ctx, result_frame) < 0) {
            std::cerr << "Error pulling result frame" << std::endl;
            return -1;
        }
        return 0;
    }
};

int main(int argc, char* argv[]) {
    // 1. 加载图片
    AVFrame* main_frame = ImageUtils::loadFromJpeg("../girl.jpg");
    AVFrame* logo_frame = ImageUtils::loadFromJpeg("../girl1.jpg");

    if (!main_frame || !logo_frame) {
        std::cerr << "Failed to load input images." << std::endl;
        return -1;
    }

    // 2. 初始化水印处理器
    WatermarkProcessor processor;
    // 在 (100, 200) 坐标处叠加水印
    if (processor.init(main_frame, logo_frame, 100, 200) < 0) {
        std::cerr << "Failed to init filters." << std::endl;
        return -1;
    }

    // 3. 处理
    AVFrame* result_frame = av_frame_alloc();
    if (processor.process(main_frame, logo_frame, result_frame) == 0) {
        std::cout << "Overlay successful." << std::endl;

        // 4. 保存结果
        if (ImageUtils::saveToJpeg("../test-output3.jpg", result_frame) == 0) {
            std::cout << "Saved to test-output3.jpg" << std::endl;
        } else {
            std::cerr << "Failed to save output." << std::endl;
        }
    }

    // 5. 资源清理
    av_frame_free(&main_frame);
    av_frame_free(&logo_frame);
    av_frame_free(&result_frame);

    return 0;
}