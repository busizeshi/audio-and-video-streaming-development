/**
 * @file main.c
 * @brief 音视频解封装
 */
#include <stdio.h>
#include <libavformat/avformat.h>

int main(int argc, char **argv) {
    setbuf(stdout, NULL);

    char *in_filename = NULL;
    if (argv[1] == NULL) {
        printf("Usage: %s <input_file>\n", argv[0]);
    } else {
        in_filename = argv[1];
    }

    printf("Input file: %s\n", in_filename);

    AVFormatContext *ifmt_ctx = NULL;

    int video_index = -1;
    int audio_index = -1;

    int ret = avformat_open_input(&ifmt_ctx, in_filename, NULL, NULL);
    if (ret < 0) {
        char buf[1024] = {0};
        av_strerror(ret, buf, sizeof(buf) - 1);
        printf("avformat_open_input error: %s\n", buf);
        goto failed;
    }

    ret = avformat_find_stream_info(ifmt_ctx, NULL);
    if (ret < 0) {
        char buf[1024] = {0};
        av_strerror(ret, buf, sizeof(buf) - 1);
        printf("avformat_find_stream_info error: %s\n", buf);
        goto failed;
    }

    printf("Input Info:\n");
    av_dump_format(ifmt_ctx, 0, in_filename, 0);
    printf("\n==== av_dump_format finish =======\n\n");
    printf("media name:%s\n", ifmt_ctx->url);
    printf("stream number:%d\n", ifmt_ctx->nb_streams);
    printf("media average ratio:%lld kbps\n", (int64_t) (ifmt_ctx->bit_rate / 1024));
    int64_t total_secondes, hour, minute, second;
    total_secondes = (ifmt_ctx->duration) / AV_TIME_BASE;
    hour = total_secondes / 3600;
    minute = (total_secondes % 3600) / 60;
    second = total_secondes % 60;
    printf("media duration:%02lld:%02lld:%02lld\n", hour, minute, second);
    printf("\n---------------------------------------------------\n");

    for (uint32_t i = 0; i < ifmt_ctx->nb_streams; i++) {
        AVStream *in_stream = ifmt_ctx->streams[i];
        if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            printf("audio stream index:%d\n", in_stream->index);
            printf("audio stream codec_id:%d\n", in_stream->codecpar->codec_id);
            printf("audio stream sample_rate:%d\n", in_stream->codecpar->sample_rate);
            if (AV_SAMPLE_FMT_FLTP == in_stream->codecpar->format) {
                printf("audio stream sample_fmt:%s\n", av_get_sample_fmt_name(in_stream->codecpar->format));
            } else if (AV_SAMPLE_FMT_S16P == in_stream->codecpar->format) {
                printf("audio stream sample_fmt:%s\n", av_get_sample_fmt_name(in_stream->codecpar->format));
            }
            printf("audio stream channels:%d\n", in_stream->codecpar->channels);
            if (AV_CODEC_ID_AAC == in_stream->codecpar->codec_id) {
                printf("audio stream profile:%s\n",
                       av_get_profile_name(avcodec_find_decoder(in_stream->codecpar->codec_id),
                                           in_stream->codecpar->profile));
            } else if (AV_CODEC_ID_MP3 == in_stream->codecpar->codec_id) {
                printf("audio stream profile:%s\n",
                       av_get_profile_name(avcodec_find_decoder(in_stream->codecpar->codec_id),
                                           in_stream->codecpar->profile));
            } else {
                printf("audio stream profile:%s\n",
                       av_get_profile_name(avcodec_find_decoder(in_stream->codecpar->codec_id),
                                           in_stream->codecpar->profile));
            }

            if (in_stream->duration != AV_NOPTS_VALUE) {
                int64_t duration_audio = (int64_t)((double)(in_stream->duration) * av_q2d(in_stream->time_base));
                printf("audio duration: %02lld:%02lld:%02lld\n",
                       duration_audio / 3600, (duration_audio % 3600) / 60, (duration_audio % 60));
            } else {
                printf("audio duration: %s\n", "unknown");
            }
            printf("\n---------------------------------------------------\n");
            audio_index = i;
        } else if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            printf("video stream index:%d\n", in_stream->index);
            printf("video stream codec_id:%d\n", in_stream->codecpar->codec_id);
            printf("video stream width:%d\n", in_stream->codecpar->width);
            printf("video stream height:%d\n", in_stream->codecpar->height);
            printf("video stream fps:%d\n", (int) (av_q2d(in_stream->avg_frame_rate)));
            if (AV_CODEC_ID_MPEG4 == in_stream->codecpar->codec_id) {
                printf("video codec:MPEG4\n");
            } else if (AV_CODEC_ID_H264 == in_stream->codecpar->codec_id) {
                printf("video codec:H264\n");
            } else {
                printf("video codec_id:%d\n", in_stream->codecpar->codec_id);
            }

            if (in_stream->duration != AV_NOPTS_VALUE) {
                int64_t duration_video = (int64_t)((double)(in_stream->duration) * av_q2d(in_stream->time_base));
                printf("video duration: %02lld:%02lld:%02lld\n",
                       duration_video / 3600,
                       (duration_video % 3600) / 60,
                       (duration_video % 60)); //将视频总时长转换为时分秒的格式打印到控制台上
            } else {
                printf("video duration unknown");
            }
            printf("\n---------------------------------------------------\n");
            video_index = i;
        }
    }

    AVPacket *pkt = av_packet_alloc();

    int pkt_count = 0;
    int print_max_count = 10;
    printf("av_read_frame start\n");

    while (av_read_frame(ifmt_ctx, pkt) >= 0 && pkt_count < print_max_count) {
        if (pkt->stream_index == audio_index) {
            printf("audio pts:%lld\n", pkt->pts);
            printf("audio dts:%lld\n", pkt->dts);
            printf("audio pkt_size:%d\n", pkt->size);
            printf("audio pos:%lld\n", pkt->pos);
            printf("audio duration: %lf\n\n",
                   (double)pkt->duration * av_q2d(ifmt_ctx->streams[audio_index]->time_base));
        } else if (pkt->stream_index == video_index) {
            printf("video pts: %lld\n", pkt->pts);
            printf("video dts: %lld\n", pkt->dts);
            printf("video size: %d\n", pkt->size);
            printf("video pos: %lld\n", pkt->pos);
            printf("video duration: %lf\n\n",
                   (double)pkt->duration * av_q2d(ifmt_ctx->streams[video_index]->time_base));
        } else {
            printf("unknown stream_index: %d\n", pkt->stream_index);
        }
        pkt_count++;
        av_packet_unref(pkt);
    }

    if (pkt) {
        av_packet_free(&pkt);
    }

    failed:
    if (ifmt_ctx) {
        avformat_close_input(&ifmt_ctx);
    }
    return 0;
}