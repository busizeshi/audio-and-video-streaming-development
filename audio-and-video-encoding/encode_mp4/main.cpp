#include <iostream>
#include <cstdio>
#include <cstring>
#include <cerrno>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/time.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

// ================= 配置参数 (请确保文件就在当前工作目录下) =================
const char *IN_FILENAME_VIDEO = "D:\\resource\\input_1280x720.yuv";
const char *IN_FILENAME_AUDIO = "D:\\resource\\input_48000_stereo.pcm";
const char *OUT_FILENAME = "../output.mp4";

// 视频参数 (必须与输入 YUV 严格一致)
const int V_WIDTH = 1280;
const int V_HEIGHT = 720;
const int V_FPS = 30;
const int V_BITRATE = 2000000; // 2Mbps

// 音频参数 (必须与输入 PCM 严格一致)
const int A_SAMPLE_RATE = 48000;
const int A_CHANNELS = 2;
// ====================================================================

void add_stream(AVFormatContext *oc, AVStream **st, AVCodecContext **enc_ctx,
                AVCodecID codec_id, int width, int height, int fps, int sample_rate) {
    const AVCodec *codec = avcodec_find_encoder(codec_id);
    if (!codec) {
        fprintf(stderr, "Codec not found. ID: %d\n", codec_id);
        exit(1);
    }

    *st = avformat_new_stream(oc, nullptr);
    if (!*st) {
        fprintf(stderr, "Could not allocate stream\n");
        exit(1);
    }
    (*st)->id = oc->nb_streams - 1;

    *enc_ctx = avcodec_alloc_context3(codec);
    if (!*enc_ctx) {
        fprintf(stderr, "Could not alloc an encoding context\n");
        exit(1);
    }

    if (codec->type == AVMEDIA_TYPE_VIDEO) {
        (*enc_ctx)->codec_id = codec_id;
        (*enc_ctx)->bit_rate = V_BITRATE;
        (*enc_ctx)->width = width;
        (*enc_ctx)->height = height;
        (*enc_ctx)->time_base = (AVRational){1, fps};
        (*enc_ctx)->framerate = (AVRational){fps, 1};
        (*enc_ctx)->gop_size = 12;
        (*enc_ctx)->pix_fmt = AV_PIX_FMT_YUV420P;

        if ((*enc_ctx)->codec_id == AV_CODEC_ID_H264) {
            // preset: ultrafast(最快/体积大) -> medium -> veryslow(最慢/体积小)
            // Debug 模式下建议用 ultrafast，否则会感觉像死机
            av_opt_set((*enc_ctx)->priv_data, "preset", "ultrafast", 0);
        }
        (*st)->time_base = (*enc_ctx)->time_base;
    } else if (codec->type == AVMEDIA_TYPE_AUDIO) {
        (*enc_ctx)->sample_fmt = codec->sample_fmts ? codec->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
        (*enc_ctx)->bit_rate = 64000;
        (*enc_ctx)->sample_rate = sample_rate;
#if LIBAVCODEC_VERSION_MAJOR >= 60
        av_channel_layout_default(&(*enc_ctx)->ch_layout, A_CHANNELS);
#else
        (*enc_ctx)->channels = A_CHANNELS;
        (*enc_ctx)->channel_layout = av_get_default_channel_layout(A_CHANNELS);
#endif
        (*st)->time_base = (AVRational){1, sample_rate};
    }

    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
        (*enc_ctx)->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
}

int write_frame(AVFormatContext *fmt_ctx, AVCodecContext *c, AVStream *st, AVFrame *frame) {
    int ret = avcodec_send_frame(c, frame);
    if (ret < 0) {
        fprintf(stderr, "Error sending frame to encoder: %s\n", av_err2str(ret));
        return ret;
    }

    while (ret >= 0) {
        AVPacket *pkt = av_packet_alloc();
        ret = avcodec_receive_packet(c, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            av_packet_free(&pkt);
            return 0;
        } else if (ret < 0) {
            fprintf(stderr, "Error encoding audio frame: %s\n", av_err2str(ret));
            av_packet_free(&pkt);
            return ret;
        }

        av_packet_rescale_ts(pkt, c->time_base, st->time_base);
        pkt->stream_index = st->index;

        ret = av_interleaved_write_frame(fmt_ctx, pkt);
        av_packet_free(&pkt);
        if (ret < 0) {
            fprintf(stderr, "Error writing packet: %s\n", av_err2str(ret));
            return ret;
        }
    }
    return 0;
}

int main() {
    setbuf(stdout, nullptr);
    // 0. 打开输入文件 (增加详细错误检查)
    FILE *f_yuv = fopen(IN_FILENAME_VIDEO, "rb");
    if (!f_yuv) {
        fprintf(stderr, "错误: 无法打开视频文件 '%s'\n", IN_FILENAME_VIDEO);
        fprintf(stderr, "   系统原因: %s\n", strerror(errno));
        return -1;
    }

    FILE *f_pcm = fopen(IN_FILENAME_AUDIO, "rb");
    if (!f_pcm) {
        fprintf(stderr, "错误: 无法打开音频文件 '%s'\n", IN_FILENAME_AUDIO);
        fprintf(stderr, "   系统原因: %s\n", strerror(errno));
        return -1;
    }

    printf("成功打开输入文件，准备开始...\n");

    AVFormatContext *oc;
    avformat_alloc_output_context2(&oc, nullptr, nullptr, OUT_FILENAME);
    if (!oc) return -1;

    AVStream *v_st = nullptr, *a_st = nullptr;
    AVCodecContext *v_ctx = nullptr, *a_ctx = nullptr;

    add_stream(oc, &v_st, &v_ctx, AV_CODEC_ID_H264, V_WIDTH, V_HEIGHT, V_FPS, 0);
    add_stream(oc, &a_st, &a_ctx, AV_CODEC_ID_AAC, 0, 0, 0, A_SAMPLE_RATE);

    // 打开编码器
    if (avcodec_open2(v_ctx, v_ctx->codec, nullptr) < 0) return -1;
    if (avcodec_open2(a_ctx, a_ctx->codec, nullptr) < 0) return -1;

    // 复制参数
    avcodec_parameters_from_context(v_st->codecpar, v_ctx);
    avcodec_parameters_from_context(a_st->codecpar, a_ctx);

    if (!(oc->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&oc->pb, OUT_FILENAME, AVIO_FLAG_WRITE) < 0) {
            fprintf(stderr, "Could not open output file '%s'", OUT_FILENAME);
            return -1;
        }
    }

    if (avformat_write_header(oc, nullptr) < 0) return -1;

    // Frame Allocations
    AVFrame *v_frame = av_frame_alloc();
    v_frame->format = v_ctx->pix_fmt;
    v_frame->width = v_ctx->width;
    v_frame->height = v_ctx->height;
    av_frame_get_buffer(v_frame, 32);

    AVFrame *a_frame = av_frame_alloc();
    a_frame->nb_samples = a_ctx->frame_size;
    a_frame->format = a_ctx->sample_fmt;
#if LIBAVCODEC_VERSION_MAJOR >= 60
    av_channel_layout_copy(&a_frame->ch_layout, &a_ctx->ch_layout);
#else
    a_frame->channel_layout = a_ctx->channel_layout;
#endif
    av_frame_get_buffer(a_frame, 0);

    // Swr Init
    SwrContext *swr_ctx = swr_alloc();
#if LIBAVCODEC_VERSION_MAJOR >= 60
    AVChannelLayout src_layout;
    av_channel_layout_default(&src_layout, A_CHANNELS);
    av_opt_set_chlayout(swr_ctx, "in_chlayout", &src_layout, 0);
    av_opt_set_chlayout(swr_ctx, "out_chlayout", &a_ctx->ch_layout, 0);
#else
    av_opt_set_int(swr_ctx, "in_channel_layout", av_get_default_channel_layout(A_CHANNELS), 0);
    av_opt_set_int(swr_ctx, "out_channel_layout", a_ctx->channel_layout, 0);
#endif
    av_opt_set_int(swr_ctx, "in_sample_rate", A_SAMPLE_RATE, 0);
    av_opt_set_int(swr_ctx, "out_sample_rate", a_ctx->sample_rate, 0);
    av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", AV_SAMPLE_FMT_S16, 0);
    av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", a_ctx->sample_fmt, 0);
    swr_init(swr_ctx);

    int64_t v_pts = 0;
    int64_t a_pts = 0;
    bool v_finished = false;
    bool a_finished = false;
    int y_size = V_WIDTH * V_HEIGHT;

    printf("开始编码...\n");

    while (!v_finished || !a_finished) {
        double v_time = v_finished ? 1e9 : av_q2d(v_st->time_base) * v_pts;
        double a_time = a_finished ? 1e9 : av_q2d(a_st->time_base) * a_pts;

        if (!v_finished && v_time <= a_time) {
            // Video
            if (av_frame_make_writable(v_frame) < 0) break;

            // 注意：这里简化处理，假设 linesize = width。严谨项目需按行拷贝。
            int read_y = fread(v_frame->data[0], 1, y_size, f_yuv);
            int read_u = fread(v_frame->data[1], 1, y_size / 4, f_yuv);
            int read_v = fread(v_frame->data[2], 1, y_size / 4, f_yuv);

            if (read_y <= 0 || read_u <= 0 || read_v <= 0) {
                v_finished = true;
                printf("\n视频数据读取完毕!\n");
            } else {
                v_frame->pts = v_pts++;
                write_frame(oc, v_ctx, v_st, v_frame);

                // --- 进度打印 (防止看起来像死机) ---
                if (v_pts % 10 == 0) {
                    printf("\n 正在编码视频帧: %lld (时间: %.2fs)\r", v_pts, v_pts / static_cast<double>(V_FPS));
                    fflush(stdout); // 必须 flush，否则控制台看不到动态变化
                }
            }
        } else if (!a_finished) {
            // Audio
            if (av_frame_make_writable(a_frame) < 0) break;

            const int sample_size_bytes = 2 * A_CHANNELS;
            const int samples_per_frame = a_ctx->frame_size;
            uint8_t *pcm_buf = (uint8_t *) malloc(samples_per_frame * sample_size_bytes);

            int read_bytes = fread(pcm_buf, 1, samples_per_frame * sample_size_bytes, f_pcm);
            int read_samples = read_bytes / sample_size_bytes;

            if (read_samples < samples_per_frame) {
                a_finished = true;
                printf("\n音频数据读取完毕!\n");
            }

            if (read_samples > 0) {
                const uint8_t *in_data[1] = {pcm_buf};
                swr_convert(swr_ctx, a_frame->data, samples_per_frame, in_data, read_samples);
                a_frame->nb_samples = read_samples;
                a_frame->pts = a_pts;
                a_pts += read_samples;
                write_frame(oc, a_ctx, a_st, a_frame);
            }
            free(pcm_buf);
        }
    }

    printf("\n正在刷新编码器缓冲区...\n");
    write_frame(oc, v_ctx, v_st, nullptr);
    write_frame(oc, a_ctx, a_st, nullptr);

    av_write_trailer(oc);

    if (!(oc->oformat->flags & AVFMT_NOFILE)) avio_closep(&oc->pb);
    avformat_free_context(oc);
    avcodec_free_context(&v_ctx);
    avcodec_free_context(&a_ctx);
    av_frame_free(&v_frame);
    av_frame_free(&a_frame);
    swr_free(&swr_ctx);
    fclose(f_yuv);
    fclose(f_pcm);

    printf("? 全部完成! 输出文件: %s\n", OUT_FILENAME);
    return 0;
}
