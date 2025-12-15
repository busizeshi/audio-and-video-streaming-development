/**
 * FFmpeg 6.1 H.264 + AAC to FLV Muxer (Fixed Sync)
 * 修复了裸流没有时间戳导致视频播放过快的问题
 */

#include <iostream>
#include <string>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/timestamp.h>
#include <libavutil/opt.h>
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cout << "Usage: " << argv[0] << " <input_h264> <input_aac> <output_flv> [fps]" << std::endl;
        std::cout << "Default fps is 25.0 if not provided." << std::endl;
        return 1;
    }

    const char* in_filename_v = argv[1];
    const char* in_filename_a = argv[2];
    const char* out_filename = argv[3];
    double target_fps = (argc >= 5) ? atof(argv[4]) : 25.0; // 允许外部传入帧率

    int ret = 0;

    // --- 1. 准备输出上下文 ---
    AVFormatContext* out_fmt_ctx = nullptr;
    avformat_alloc_output_context2(&out_fmt_ctx, nullptr, nullptr, out_filename);
    if (!out_fmt_ctx) return 1;

    // --- 2. 打开视频输入 ---
    AVFormatContext* fmt_ctx_v = nullptr;
    if (avformat_open_input(&fmt_ctx_v, in_filename_v, nullptr, nullptr) < 0) {
        std::cerr << "Could not open video file." << std::endl;
        return 1;
    }
    if (avformat_find_stream_info(fmt_ctx_v, nullptr) < 0) return 1;

    // --- 3. 打开音频输入 ---
    AVFormatContext* fmt_ctx_a = nullptr;
    if (avformat_open_input(&fmt_ctx_a, in_filename_a, nullptr, nullptr) < 0) {
        std::cerr << "Could not open audio file." << std::endl;
        return 1;
    }
    if (avformat_find_stream_info(fmt_ctx_a, nullptr) < 0) return 1;

    // --- 4. 添加流到输出 ---

    // Video
    int video_idx = av_find_best_stream(fmt_ctx_v, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    AVStream* in_video_st = fmt_ctx_v->streams[video_idx];
    AVStream* out_video_st = avformat_new_stream(out_fmt_ctx, nullptr);
    avcodec_parameters_copy(out_video_st->codecpar, in_video_st->codecpar);
    out_video_st->codecpar->codec_tag = 0;

    // Audio
    int audio_idx = av_find_best_stream(fmt_ctx_a, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    AVStream* in_audio_st = fmt_ctx_a->streams[audio_idx];
    AVStream* out_audio_st = avformat_new_stream(out_fmt_ctx, nullptr);
    avcodec_parameters_copy(out_audio_st->codecpar, in_audio_st->codecpar);
    out_audio_st->codecpar->codec_tag = 0;//创建新流时推荐使用0

    // 打开输出 IO
    if (!(out_fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&out_fmt_ctx->pb, out_filename, AVIO_FLAG_WRITE) < 0) return 1;
    }

    if (avformat_write_header(out_fmt_ctx, nullptr) < 0) return 1;

    // --- 5. 核心逻辑：手动计算时间戳 ---

    AVPacket* pkt = av_packet_alloc();

    // 计数器
    int64_t video_frame_count = 0;
    int64_t audio_frame_count = 0;

    // 计算每帧视频的持续时间 (基于 AV_TIME_BASE = 1,000,000 微秒)
    // 比如 25FPS -> 每帧 40000 微秒
    auto video_duration = static_cast<int64_t>((AV_TIME_BASE / target_fps));

    // 记录当前的播放时间 (微秒)，用于交错写入判断
    int64_t cur_pts_v = 0;
    int64_t cur_pts_a = 0;

    // 标记是否读完
    bool video_finished = false;
    bool audio_finished = false;

    // 预读取的 Packet 缓存（简单的单帧缓存）
    AVPacket* pkt_v = av_packet_alloc();
    AVPacket* pkt_a = av_packet_alloc();

    int ret_v = av_read_frame(fmt_ctx_v, pkt_v);
    int ret_a = av_read_frame(fmt_ctx_a, pkt_a);

    while (!video_finished || !audio_finished) {

        // 检查流结束
        if (ret_v < 0) video_finished = true;
        if (ret_a < 0) audio_finished = true;

        bool write_video = false;

        // 决策：写视频还是写音频？
        // 比较两者的当前累计时间戳 (都在微秒单位下比较)
        if (!video_finished && !audio_finished) {
            if (cur_pts_v <= cur_pts_a) {
                write_video = true;
            } else {
                write_video = false;
            }
        } else if (!video_finished) {
            write_video = true;
        } else if (!audio_finished) {
            write_video = false;
        } else {
            break; // 都结束了
        }

        if (write_video) {
            if (pkt_v->stream_index == video_idx) {
                // 【关键修复】：完全忽略 pkt_v 里的原始 DTS/PTS
                // 手动利用计数器生成递增的时间戳

                // 1. 计算当前帧在微秒基准下的 PTS
                // 公式：帧号 * (1秒 / 帧率)
                int64_t pts_us = video_frame_count * video_duration;

                // 2. 将微秒 (AV_TIME_BASE_Q) 转换到输出流的 time_base
                // FLV 的 video timebase 通常是 1/1000 (毫秒)
                pkt_v->pts = av_rescale_q(pts_us, AV_TIME_BASE_Q, out_video_st->time_base);
                pkt_v->dts = pkt_v->pts; // 对于只有 I/P 帧的简单情况，DTS=PTS。如果有 B 帧需另行处理，但裸流通常按解码顺序
                pkt_v->duration = av_rescale_q(video_duration, AV_TIME_BASE_Q, out_video_st->time_base);

                pkt_v->pos = -1;
                pkt_v->stream_index = out_video_st->index;

                // 更新用于比较的 cursor
                cur_pts_v = pts_us;
                video_frame_count++;

                // log_packet(out_fmt_ctx, pkt_v); // 可选：打印日志调试
                if (av_interleaved_write_frame(out_fmt_ctx, pkt_v) < 0) {
                    std::cerr << "Error writing video frame" << std::endl;
                }
            }
            av_packet_unref(pkt_v);
            ret_v = av_read_frame(fmt_ctx_v, pkt_v);
        }
        else {
            if (pkt_a->stream_index == audio_idx) {
                // 音频处理
                // AAC 裸流通常有正确的采样率信息，但 PTS 往往也是乱的或 0
                // 我们基于采样点数累加来计算

                // 获取每一帧的采样数，AAC 通常是 1024
                int nb_samples = 1024;
                // 安全起见，如果解析器没有分析出 nb_samples，我们默认 1024
                // 在 FFmpeg 新版本中，有时需要解码器上下文辅助，这里简化处理

                // 计算当前音频帧的时间戳
                // 公式：TotalSamples * TimeBase / SampleRate
                // 这里用 av_rescale_q 实现： count * (1/SampleRate) -> OutputTimeBase

                AVRational sample_time_base = {1, in_audio_st->codecpar->sample_rate};

                pkt_a->pts = av_rescale_q(audio_frame_count * nb_samples, sample_time_base, out_audio_st->time_base);
                pkt_a->dts = pkt_a->pts;
                pkt_a->duration = av_rescale_q(nb_samples, sample_time_base, out_audio_st->time_base);

                pkt_a->stream_index = out_audio_st->index;
                pkt_a->pos = -1;

                // 更新用于比较的 cursor (转成微秒)
                cur_pts_a = av_rescale_q(pkt_a->pts, out_audio_st->time_base, AV_TIME_BASE_Q);

                audio_frame_count++; // 帧数加1

                if (av_interleaved_write_frame(out_fmt_ctx, pkt_a) < 0) {
                    std::cerr << "Error writing audio frame" << std::endl;
                }
            }
            av_packet_unref(pkt_a);
            ret_a = av_read_frame(fmt_ctx_a, pkt_a);
        }
    }

    av_write_trailer(out_fmt_ctx);
    std::cout << "Done. Video Frames: " << video_frame_count << " Audio Frames: " << audio_frame_count << std::endl;

    av_packet_free(&pkt);
    av_packet_free(&pkt_v);
    av_packet_free(&pkt_a);
    avformat_close_input(&fmt_ctx_v);
    avformat_close_input(&fmt_ctx_a);
    if (!(out_fmt_ctx->oformat->flags & AVFMT_NOFILE)) avio_closep(&out_fmt_ctx->pb);
    avformat_free_context(out_fmt_ctx);

    return 0;
}