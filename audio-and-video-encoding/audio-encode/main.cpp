/**
 * @projectName   PCM to AAC Encoder (FFmpeg 6.1 Optimized C++)
 * @brief         将 PCM 数据编码为 AAC (自动处理 ADTS 封装与重采样)
 * @optimization  使用 libswresample 替代手动格式转换；使用 libavformat 替代手动 ADTS 头拼接。
 */

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

// 错误检查辅助函数
static void check_ret(int ret, const std::string& func_name)
{
    if (ret < 0)
    {
        char err_buf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
        std::cerr << "[Error] " << func_name << ": " << err_buf << " (Error code: " << ret << ")" << std::endl;
        exit(1);
    }
}

class AudioEncoder
{
public:
    AudioEncoder() = default;
    ~AudioEncoder() { cleanup(); }

    void init(const char* output_file, const char* codec_name,
              const int input_rate, const int input_channels, const AVSampleFormat input_fmt)
    {
        // 1. 初始化封装上下文 (Format Context) - 负责写入文件和 ADTS 头
        /**
         * 创建一个可用于编码输出的封装上下文，并自动匹配合适的输出格式
         * oformat: 封装器上下文
         * format_name: 封装器名称
         * filename: 文件名
         * 会根据filename自动识别格式
         */
        int ret = avformat_alloc_output_context2(&fmt_ctx, nullptr, nullptr, output_file);
        if (!fmt_ctx)
        {
            std::cerr << "Could not create output context for " << output_file << std::endl;
            exit(1);
        }

        // 2. 查找编码器
        const AVCodec* codec = nullptr; // 编解码器
        if (codec_name)
        {
            codec = avcodec_find_encoder_by_name(codec_name);
        }
        else
        {
            codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
        }

        if (!codec)
        {
            std::cerr << "Codec not found: " << (codec_name ? codec_name : "default AAC") << std::endl;
            exit(1);
        }

        // 3. 分配并配置编码器上下文
        /**
         * 为编解码器分配并配置编码器上下文，可以关联编码器
         * 得到编解码器上下文
         */
        codec_ctx = avcodec_alloc_context3(codec);
        if (!codec_ctx) exit(1);

        // 设置编码参数
        codec_ctx->bit_rate = 128000; // 128 kbps
        codec_ctx->sample_rate = input_rate; // 简单起见，输出采样率=输入采样率
        // 自动选择编码器支持的第一个采样格式 (AAC通常是 FLTP, libfdk_aac可能是 S16)
        codec_ctx->sample_fmt = codec->sample_fmts ? codec->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;

        // FFmpeg 6.1: 设置通道布局
        av_channel_layout_default(&codec_ctx->ch_layout, input_channels);

        // 打开编码器:将 AVCodecContext 与指定的 AVCodec 关联
        check_ret(avcodec_open2(codec_ctx, codec, nullptr), "avcodec_open2");

        // 4. 添加流到封装器
        /**
         * 为输出文件添加流
         */
        stream = avformat_new_stream(fmt_ctx, nullptr);
        stream->id = fmt_ctx->nb_streams - 1;
        avcodec_parameters_from_context(stream->codecpar, codec_ctx);

        // 5. 打开输出文件
        if (!(fmt_ctx->oformat->flags & AVFMT_NOFILE))
        {
            check_ret(avio_open(&fmt_ctx->pb, output_file, AVIO_FLAG_WRITE), "avio_open");
        }

        // 写入文件头
        check_ret(avformat_write_header(fmt_ctx, nullptr), "avformat_write_header");

        // 6. 初始化重采样器 (SwrContext)
        // 无论输入是什么格式(S16/F32)，都转为编码器需要的格式
        AVChannelLayout in_ch_layout;
        av_channel_layout_default(&in_ch_layout, input_channels);

        check_ret(swr_alloc_set_opts2(&swr_ctx,
                                      &codec_ctx->ch_layout, codec_ctx->sample_fmt, codec_ctx->sample_rate,
                                      &in_ch_layout, input_fmt, input_rate,
                                      0, nullptr), "swr_alloc_set_opts2");
        swr_init(swr_ctx);

        // 7. 分配 Frame 和 Packet
        pkt = av_packet_alloc();
        frame = av_frame_alloc();
        frame->nb_samples = codec_ctx->frame_size;
        frame->format = codec_ctx->sample_fmt;
        av_channel_layout_copy(&frame->ch_layout, &codec_ctx->ch_layout);
        check_ret(av_frame_get_buffer(frame, 0), "av_frame_get_buffer");

        // 打印调试信息
        std::cout << "---------------- Config ----------------" << std::endl;
        std::cout << "Encoder: " << codec->name << std::endl;
        std::cout << "Bitrate: " << codec_ctx->bit_rate << std::endl;
        std::cout << "Input Fmt: " << av_get_sample_fmt_name(input_fmt) << std::endl;
        std::cout << "Output Fmt: " << av_get_sample_fmt_name(codec_ctx->sample_fmt) << std::endl;
        std::cout << "Frame Size: " << codec_ctx->frame_size << std::endl;
        std::cout << "----------------------------------------" << std::endl;
    }

    // 核心处理函数
    void process(const char* input_file, int input_channels, AVSampleFormat input_fmt)
    {
        std::ifstream infile(input_file, std::ios::binary);
        if (!infile)
        {
            std::cerr << "Cannot open input file: " << input_file << std::endl;
            return;
        }

        // 计算输入缓冲大小
        // 我们每次需要凑够 frame_size 个样本给编码器
        // 比如 AAC 一帧 1024 样本。不管输入是 S16 还是 F32，Swr 会帮我们凑。
        // 这里简单起见，我们按照编码器需要的样本数来读取输入数据进行 1:1 转换尝试
        const int input_bytes_per_sample = av_get_bytes_per_sample(input_fmt);
        const int read_size = codec_ctx->frame_size * input_channels * input_bytes_per_sample;
        std::vector<uint8_t> input_buf(read_size);

        int64_t pts = 0;

        while (infile.read(reinterpret_cast<char*>(input_buf.data()), read_size))
        {
            encode_frame(input_buf.data(), codec_ctx->frame_size, pts);
            pts += codec_ctx->frame_size;
        }

        // 处理最后剩余不足一帧的数据 (可选，通常直接 Flush 即可，音频对齐要求不高)

        // Flush 编码器
        encode_frame(nullptr, 0, pts);

        // 写入文件尾 (Trailer)
        av_write_trailer(fmt_ctx);
    }

private:
    AVFormatContext* fmt_ctx = nullptr; // 封装上下文
    AVCodecContext* codec_ctx = nullptr; // 编码器上下文
    AVStream* stream = nullptr; // 流
    SwrContext* swr_ctx = nullptr; // 重采样器
    AVFrame* frame = nullptr; // 是 FFmpeg 中存储原始音视频数据的核心结构体。
    AVPacket* pkt = nullptr; // Packet

    void encode_frame(const uint8_t* data, int nb_samples, int64_t pts)
    {
        int ret;

        // 1. 重采样 / 格式转换 (即使格式相同，swr_convert 也是最安全的拷贝方式)
        if (data)
        {
            // 确保 Frame 可写
            ret = av_frame_make_writable(frame);
            const uint8_t* in_data[1] = {data};

            // 将 PCM 数据转入 Frame
            // 注意：swr_convert 第三个参数是输出的样本数上限，第四个是输入数据，第五个是输入样本数
            const int dst_nb_samples = swr_convert(swr_ctx,
                                                   frame->data, frame->nb_samples,
                                                   (const uint8_t**)in_data, nb_samples);
            if (dst_nb_samples < 0)
            {
                std::cerr << "Error during resampling" << std::endl;
                return;
            }

            // 设置 PTS 和 实际样本数
            frame->pts = pts;
            // 实际上对于 AAC，每一帧必须填满 1024，除非是最后一帧 flush
            // swrContext 内部有缓存，这里简化处理，假设每次读取都填满了
        }

        // 2. 发送给编码器
        // data 为 nullptr 时，send_frame(nullptr) 会触发 flush
        ret = avcodec_send_frame(codec_ctx, data ? frame : nullptr);
        if (ret < 0) check_ret(ret, "avcodec_send_frame");

        // 3. 接收并封装数据包
        while (ret >= 0)
        {
            ret = avcodec_receive_packet(codec_ctx, pkt);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                return;
            else if (ret < 0) check_ret(ret, "avcodec_receive_packet");

            // 设置流索引
            pkt->stream_index = stream->index;

            // 时间基转换 (Codec TB -> Stream TB)
            av_packet_rescale_ts(pkt, codec_ctx->time_base, stream->time_base);

            // 写入文件 (自动处理 ADTS)
            ret = av_interleaved_write_frame(fmt_ctx, pkt);

            av_packet_unref(pkt);
        }
    }

    void cleanup()
    {
        if (fmt_ctx && !(fmt_ctx->oformat->flags & AVFMT_NOFILE))
            avio_closep(&fmt_ctx->pb);

        if (fmt_ctx) avformat_free_context(fmt_ctx);
        if (codec_ctx) avcodec_free_context(&codec_ctx);
        if (frame) av_frame_free(&frame);
        if (pkt) av_packet_free(&pkt);
        if (swr_ctx) swr_free(&swr_ctx);

        std::cout << "Resources cleaned up." << std::endl;
    }
};

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::cout << "Usage: " << argv[0] << " <input_pcm> <output_aac> [codec_name] [fmt:s16/f32]" << std::endl;
        std::cout << "Example: " << argv[0] << " input.pcm output.aac libfdk_aac s16" << std::endl;
        return 1;
    }

    const char* in_file = argv[1];
    const char* out_file = argv[2];
    const char* codec_name = (argc > 3) ? argv[3] : nullptr; // 默认 nullptr (auto select)

    // 默认输入参数 (根据你的需求修改)
    const int sample_rate = 48000;
    const int channels = 2;
    AVSampleFormat input_fmt = AV_SAMPLE_FMT_S16; // 交错格式（适合播放）;位深16;

    // 简单的命令行参数解析来切换输入格式
    if (argc > 4)
    {
        std::string fmt_str = argv[4];
        if (fmt_str == "f32") input_fmt = AV_SAMPLE_FMT_FLT; // F32LE Packed
        if (fmt_str == "fltp") input_fmt = AV_SAMPLE_FMT_FLTP;
    }

    AudioEncoder encoder;

    // 初始化
    encoder.init(out_file, codec_name, sample_rate, channels, input_fmt);

    // 执行编码
    encoder.process(in_file, channels, input_fmt);

    return 0;
}
