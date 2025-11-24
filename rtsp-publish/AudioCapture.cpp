#include "AudioCapture.h"

static inline std::string ff_err2str(int errnum) {
    char buf[256];
    av_strerror(errnum, buf, sizeof(buf));
    return std::string(buf);
}

AudioCapture::AudioCapture() {
    // 确保设备库已注册，虽然 VideoCapture 里调过，但多调无害
    avdevice_register_all();
}

AudioCapture::~AudioCapture() {
    stop();
}

bool AudioCapture::open(const std::string& deviceName, int channels, int sampleRate) {
    const AVInputFormat* inputFormat = av_find_input_format("dshow");
    if (!inputFormat) {
        std::cerr << "[Audio] Error: cannot find dshow input format" << std::endl;
        return false;
    }

    AVDictionary* options = nullptr;
    // Windows dshow 音频参数配置
    // 注意：有些麦克风不支持随意设置采样率，若打开失败，可尝试注释掉这两行让 FFmpeg 自动协商
    av_dict_set(&options, "sample_rate", std::to_string(sampleRate).c_str(), 0);
    av_dict_set(&options, "channels", std::to_string(channels).c_str(), 0);
    
    // 关键：音频设备前缀是 audio=
    std::string url = "audio=" + deviceName;

    int ret = avformat_open_input(&fmt_ctx, url.c_str(), inputFormat, &options);
    av_dict_free(&options); // 释放字典
    if (ret < 0) {
        std::cerr << "[Audio] Error: open input failed: " << ff_err2str(ret) << std::endl;
        // 提示：可以用 ffmpeg -list_devices true -f dshow -i dummy 查看真实设备名
        return false;
    }

    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        std::cerr << "[Audio] Error: find stream info failed" << std::endl;
        avformat_close_input(&fmt_ctx);
        return false;
    }

    audio_stream_index = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audio_stream_index < 0) {
        std::cerr << "[Audio] Error: cannot find audio stream" << std::endl;
        avformat_close_input(&fmt_ctx);
        return false;
    }

    const AVStream* stream = fmt_ctx->streams[audio_stream_index];
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        std::cerr << "[Audio] Error: decoder not found" << std::endl;
        avformat_close_input(&fmt_ctx);
        return false;
    }

    codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_ctx, stream->codecpar);

    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        std::cerr << "[Audio] Error: open codec failed" << std::endl;
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return false;
    }

    std::cout << "[INFO] Mic Opened: " << sampleRate << "Hz | Channels: " << channels 
              << " | Fmt: " << av_get_sample_fmt_name(codec_ctx->sample_fmt) << std::endl;
    
    return true;
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

void AudioCapture::start(const AudioFrameCallback& cb) {
    if (isRunning) return;
    if (!fmt_ctx) return;

    callback = cb;
    isRunning = true;
    // 必须与视频使用同一个基准时间点，这里假设调用 start 是近似同时的
    // 更严谨的做法是可以通过参数传入一个统一的 startTime
    startTime = av_gettime(); 

    worker_thread = std::thread(&AudioCapture::captureThreadLoop, this);
}

void AudioCapture::captureThreadLoop() {
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();

    while (isRunning) {
        int ret = av_read_frame(fmt_ctx, packet);
        if (ret == AVERROR(EAGAIN)) {
            continue;
        } else if (ret < 0) {
            std::cerr << "[Audio] Read error or EOF" << std::endl;
            break;
        }

        if (packet->stream_index == audio_stream_index) {
            ret = avcodec_send_packet(codec_ctx, packet);
            if (ret >= 0) {
                while (avcodec_receive_frame(codec_ctx, frame) >= 0) {
                    // --- 关键禁制：重写时间戳 ---
                    // 为了配合你的视频代码，这里强制使用 Wall Clock
                    // 注意：音频样本数 frame->nb_samples 代表了持续时长
                    frame->pts = av_gettime() - startTime;

                    if (callback) {
                        AVFrame* ref_frame = av_frame_alloc();
                        // 深拷贝引用，传递给下游
                        if (av_frame_ref(ref_frame, frame) == 0) {
                            // 务必确保：frame->pts, frame->pkt_dts 等都被正确复制
                            callback(ref_frame);
                        } else {
                            av_frame_free(&ref_frame);
                        }
                    }
                    av_frame_unref(frame);
                }
            }
        }
        av_packet_unref(packet);
    }
    
    // Flush decoder (虽是采集，但好习惯不能丢)
    avcodec_send_packet(codec_ctx, nullptr);
    while(avcodec_receive_frame(codec_ctx, frame) >= 0) {
        // 处理残留帧...
        av_frame_unref(frame);
    }

    av_packet_free(&packet);
    av_frame_free(&frame);
}