/*
 * 基于 FFmpeg 6.1+ 的音频重采样示例
 * 编译命令参考: gcc resample_test.c -o resample_test -lavutil -lswresample -lm
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>

// 辅助函数：将 FFmpeg 的采样格式枚举转换为字符串，用于 ffplay 播放命令提示
static int get_format_from_sample_fmt(const char **fmt, enum AVSampleFormat sample_fmt)
{
    struct sample_fmt_entry {
        enum AVSampleFormat sample_fmt; const char *fmt_be, *fmt_le;
    } sample_fmt_entries[] = {
            { AV_SAMPLE_FMT_U8,  "u8",    "u8"    },
            { AV_SAMPLE_FMT_S16, "s16be", "s16le" },
            { AV_SAMPLE_FMT_S32, "s32be", "s32le" },
            { AV_SAMPLE_FMT_FLT, "f32be", "f32le" },
            { AV_SAMPLE_FMT_DBL, "f64be", "f64le" },
    };
    *fmt = nullptr;

    for (int i = 0; i < FF_ARRAY_ELEMS(sample_fmt_entries); i++) {
        struct sample_fmt_entry *entry = &sample_fmt_entries[i];
        if (sample_fmt == entry->sample_fmt) {
            *fmt = AV_NE(entry->fmt_be, entry->fmt_le);
            return 0;
        }
    }

    fprintf(stderr, "Sample format %s not supported as output format\n",
            av_get_sample_fmt_name(sample_fmt));
    return AVERROR(EINVAL);
}

// 填充样本数据：生成正弦波
static void fill_samples(double *dst, int nb_samples, int nb_channels, int sample_rate, double *t)
{
    int i, j;
    double tincr = 1.0 / sample_rate, *dstp = dst;
    const double c = 2 * M_PI * 440.0;

    /* 生成 440Hz 正弦波 */
    for (i = 0; i < nb_samples; i++) {
        *dstp = sin(c * *t);
        // 将第一个通道的数据复制到其他通道
        for (j = 1; j < nb_channels; j++)
            dstp[j] = dstp[0];
        dstp += nb_channels;
        *t += tincr;
    }
}

//ffplay -f s16le -channels 2 -ar 44100 output_44100.pcm
int main(int argc, char *argv[])
{
    // === 1. 定义输入输出参数 ===

    // 输入参数 (使用新的 AVChannelLayout 结构体)
    AVChannelLayout src_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    int src_rate = 48000;
    enum AVSampleFormat src_sample_fmt = AV_SAMPLE_FMT_DBL;
    uint8_t **src_data = nullptr;
    int src_line_size;
    int src_nb_samples = 1024;

    // 输出参数
    AVChannelLayout dst_ch_layout = AV_CHANNEL_LAYOUT_STEREO;
    int dst_rate = 44100;
    enum AVSampleFormat dst_sample_fmt = AV_SAMPLE_FMT_S16;
    uint8_t **dst_data = nullptr;
    int dst_line_size;
    int dst_nb_samples;
    int max_dst_nb_samples;

    const char *dst_filename;
    FILE *dst_file;
    int dst_buf_size;
    const char *fmt;
    struct SwrContext *swr_ctx = nullptr;
    double t;
    int ret;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s output_file\n", argv[0]);
        exit(1);
    }
    dst_filename = argv[1];

    dst_file = fopen(dst_filename, "wb");
    if (!dst_file) {
        fprintf(stderr, "Could not open destination file %s\n", dst_filename);
        exit(1);
    }

    // === 2. 初始化重采样上下文 (FFmpeg 6.1+ 推荐写法) ===

    /* * swr_alloc_set_opts2 是现代 API，替代了 swr_alloc_set_opts 和手动的 av_opt_set_int
     * 它直接接受 AVChannelLayout 指针
     */
    ret = swr_alloc_set_opts2(&swr_ctx,
                              &dst_ch_layout, dst_sample_fmt, dst_rate,
                              &src_ch_layout, src_sample_fmt, src_rate,
                              0, nullptr);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate resampler context\n");
        goto end;
    }

    /* 初始化上下文 */
    if ((ret = swr_init(swr_ctx)) < 0) {
        fprintf(stderr, "Failed to initialize the resampling context\n");
        goto end;
    }

    // === 3. 分配样本内存 ===

    /* * 注意：src_ch_layout.nb_channels 直接获取通道数，
     * 不再需要 av_get_channel_layout_nb_channels()
     */
    ret = av_samples_alloc_array_and_samples(&src_data, &src_line_size, src_ch_layout.nb_channels,
                                             src_nb_samples, src_sample_fmt, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate source samples\n");
        goto end;
    }

    /* 计算输出缓冲区大小 (预估) */
    max_dst_nb_samples = dst_nb_samples =
            av_rescale_rnd(src_nb_samples, dst_rate, src_rate, AV_ROUND_UP);

    /* 分配输出缓冲区 */
    ret = av_samples_alloc_array_and_samples(&dst_data, &dst_line_size, dst_ch_layout.nb_channels,
                                             dst_nb_samples, dst_sample_fmt, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate destination samples\n");
        goto end;
    }

    // === 4. 循环处理数据 ===
    t = 0;
    do {
        /* 生成输入音频数据 */
        fill_samples((double *)src_data[0], src_nb_samples, src_ch_layout.nb_channels, src_rate, &t);

        /* 计算由重采样延迟导致的额外样本数 */
        int64_t delay = swr_get_delay(swr_ctx, src_rate);

        /* 重新计算目标样本数 = 延迟 + 输入样本转换后的数量 */
        dst_nb_samples = av_rescale_rnd(delay + src_nb_samples, dst_rate, src_rate, AV_ROUND_UP);

        /* 如果输出空间不够，重新分配内存 */
        if (dst_nb_samples > max_dst_nb_samples) {
            av_freep(&dst_data[0]);
            ret = av_samples_alloc(dst_data, &dst_line_size, dst_ch_layout.nb_channels,
                                   dst_nb_samples, dst_sample_fmt, 1);
            if (ret < 0)
                break;
            max_dst_nb_samples = dst_nb_samples;
        }

        /* 执行重采样转换 */
        // 返回值 ret 是实际转换出来的每个通道的样本数
        ret = swr_convert(swr_ctx, dst_data, dst_nb_samples, (const uint8_t **)src_data, src_nb_samples);
        if (ret < 0) {
            fprintf(stderr, "Error while converting\n");
            goto end;
        }

        /* 获取实际数据大小并写入文件 */
        dst_buf_size = av_samples_get_buffer_size(&dst_line_size, dst_ch_layout.nb_channels,
                                                  ret, dst_sample_fmt, 1);
        if (dst_buf_size < 0) {
            fprintf(stderr, "Could not get sample buffer size\n");
            goto end;
        }

        printf("t:%f in:%d out:%d\n", t, src_nb_samples, ret);
        fwrite(dst_data[0], 1, dst_buf_size, dst_file);

    } while (t < 10); // 生成 10 秒音频

    // === 5. 冲刷剩余数据 (Flush) ===
    /* 当输入为 nullptr 且数量为 0 时，swr_convert 会输出内部缓存的剩余数据 */
    ret = swr_convert(swr_ctx, dst_data, dst_nb_samples, nullptr, 0);
    if (ret < 0) {
        fprintf(stderr, "Error while converting\n");
        goto end;
    }

    dst_buf_size = av_samples_get_buffer_size(&dst_line_size, dst_ch_layout.nb_channels,
                                              ret, dst_sample_fmt, 1);
    if (dst_buf_size < 0) {
        fprintf(stderr, "Could not get sample buffer size\n");
        goto end;
    }
    printf("flush in:%d out:%d\n", 0, ret);
    fwrite(dst_data[0], 1, dst_buf_size, dst_file);

    // === 6. 打印播放命令 ===
    if ((ret = get_format_from_sample_fmt(&fmt, dst_sample_fmt)) < 0)
        goto end;

    // 注意：这里打印命令时，使用 dst_ch_layout.nb_channels
    fprintf(stderr, "Resampling succeeded. Play the output file with the command:\n"
                    "ffplay -f %s -channels %d -ar %d %s\n",
            fmt, dst_ch_layout.nb_channels, dst_rate, dst_filename);

    end:
    fclose(dst_file);

    if (src_data) {
        av_freep(&src_data[0]);
        av_freep(&src_data);
    }

    if (dst_data) {
        av_freep(&dst_data[0]);
        av_freep(&dst_data);
    }

    if (swr_ctx) swr_free(&swr_ctx);

    // 清理 Channel Layout (虽然静态定义的如 STEREO 不需要清理，但养成习惯很好)
    av_channel_layout_uninit(&src_ch_layout);
    av_channel_layout_uninit(&dst_ch_layout);

    return ret < 0;
}