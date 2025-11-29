#include "AudioCapture.h"

static inline std::string ff_err2str(int errnum) {
    char buf[256];
    av_strerror(errnum, buf, sizeof(buf));
    return {buf};
}

AudioCapture::AudioCapture() {
    avdevice_register_all();
}

AudioCapture::~AudioCapture() {
    stop();
}

bool AudioCapture::open(const std::string &deviceName, int channels, int sampleRate) {
    const AVInputFormat *inputFormat = av_find_input_format("dshow");
    if (!inputFormat) {
        std::cerr << "[AudioCapture] Error: dshow not found." << std::endl;
        return false;
    }

    AVDictionary *options = nullptr;
    // dshow 音频参数通常通过 codec 协商，设置太死可能导致打开失败
    // 但可以尝试设置 sample_rate
    av_dict_set_int(&options, "sample_rate", sampleRate, 0);
    av_dict_set_int(&options, "channels", channels, 0);

    // Windows dshow 音频前缀通常是 "audio="
    // 如果 deviceName 已经是GUID格式，确保前面拼了 audio=
    std::string url = "audio=" + deviceName;

    std::cout << "[AudioCapture] Opening: " << url << std::endl;

    int ret = avformat_open_input(&fmt_ctx, url.c_str(), inputFormat, &options);
    av_dict_free(&options);
    if (ret < 0) {
        std::cerr << "[AudioCapture] Error opening audio: " << ff_err2str(ret) << std::endl;
        // 尝试列出设备（调试用）
        std::cerr << "Tip: Run 'ffmpeg -list_devices true -f dshow -i dummy' to check device names." << std::endl;
        return false;
    }

    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) return false;

    audio_stream_index = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audio_stream_index < 0) return false;

    AVStream *stream = fmt_ctx->streams[audio_stream_index];
    const AVCodec *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) return false;

    codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_ctx, stream->codecpar);

    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) return false;

    std::cout << "[AudioCapture] Opened. Rate: " << codec_ctx->sample_rate
              << ", Channels: " << codec_ctx->ch_layout.nb_channels
              << ", Fmt: " << av_get_sample_fmt_name(codec_ctx->sample_fmt) << std::endl;

    return true;
}

void AudioCapture::start(const AudioFrameCallback &cb) {
    if (isRunning) return;
    callback = cb;
    isRunning = true;
    startTime = av_gettime();
    worker_thread = std::thread(&AudioCapture::captureThreadLoop, this);
}

void AudioCapture::stop() {
    isRunning = false;
    if (worker_thread.joinable()) {
        worker_thread.join();
    }
    if (codec_ctx) {
        avcodec_free_context(&codec_ctx);
        codec_ctx = nullptr;
    }
    if (fmt_ctx) {
        avformat_close_input(&fmt_ctx);
        fmt_ctx = nullptr;
    }
}

void AudioCapture::captureThreadLoop() {
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();

    while (isRunning) {
        int ret = av_read_frame(fmt_ctx, packet);
        if (ret < 0) {
            av_packet_unref(packet);
            if (ret == AVERROR(EAGAIN)) continue;
            break;
        }

        if (packet->stream_index == audio_stream_index) {
            ret = avcodec_send_packet(codec_ctx, packet);
            if (ret >= 0) {
                while (ret >= 0) {
                    ret = avcodec_receive_frame(codec_ctx, frame);
                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
                    else if (ret < 0) break;

                    // 深拷贝一份 Frame 发送给回调，因为 frame 对象会被复用
                    AVFrame *outFrame = av_frame_clone(frame);
                    if (outFrame) {
                        // 确保 PTS 有效
                        if (outFrame->pts == AV_NOPTS_VALUE) {
                            outFrame->pts = av_gettime() - startTime; // 简易回退方案
                        }
                        if (callback) callback(outFrame);
                        else av_frame_free(&outFrame);
                    }
                    av_frame_unref(frame);
                }
            }
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    av_frame_free(&frame);
}