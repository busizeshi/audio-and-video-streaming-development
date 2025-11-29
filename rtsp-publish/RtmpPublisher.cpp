#include "RtmpPublisher.h"

// 辅助函数
static inline std::string ff_err2str(int errnum) {
    char buf[256];
    av_strerror(errnum, buf, sizeof(buf));
    return {buf};
}

RtmpPublisher::RtmpPublisher() {}

RtmpPublisher::~RtmpPublisher() {
    stop();
}

bool RtmpPublisher::init(const std::string& url) {
    url_ = url;
    // 分配 FLV 封装的上下文
    // 注意：如果 url 是 rtmp:// 开头，avformat_alloc_output_context2 通常会自动识别为 flv
    // 但为了保险，显式指定 "flv"
    int ret = avformat_alloc_output_context2(&fmt_ctx_, nullptr, "flv", url.c_str());
    if (ret < 0 || !fmt_ctx_) {
        std::cerr << "[Publisher] Error: alloc output context failed: " << ff_err2str(ret) << std::endl;
        return false;
    }
    
    return true;
}

bool RtmpPublisher::addVideoStream(const AVCodecParameters* codecpar, AVRational timebase) {
    if (!fmt_ctx_) return false;

    video_stream_ = avformat_new_stream(fmt_ctx_, nullptr);
    if (!video_stream_) {
        std::cerr << "[Publisher] Error: new video stream failed." << std::endl;
        return false;
    }

    // 复制编码器参数到流参数
    int ret = avcodec_parameters_copy(video_stream_->codecpar, codecpar);
    if (ret < 0) return false;

    video_stream_->codecpar->codec_tag = 0; // 兼容性设置
    video_enc_tb_ = timebase; // 记录源时间基 (例如 1/30)

    std::cout << "[Publisher] Video stream added. Timebase: " << timebase.num << "/" << timebase.den << std::endl;
    return true;
}

bool RtmpPublisher::addAudioStream(const AVCodecParameters* codecpar, AVRational timebase) {
    if (!fmt_ctx_) return false;

    audio_stream_ = avformat_new_stream(fmt_ctx_, nullptr);
    if (!audio_stream_) {
        std::cerr << "[Publisher] Error: new audio stream failed." << std::endl;
        return false;
    }

    int ret = avcodec_parameters_copy(audio_stream_->codecpar, codecpar);
    if (ret < 0) return false;

    audio_stream_->codecpar->codec_tag = 0;
    audio_enc_tb_ = timebase; // 记录源时间基 (例如 1/44100)

    std::cout << "[Publisher] Audio stream added. Timebase: " << timebase.num << "/" << timebase.den << std::endl;
    return true;
}

bool RtmpPublisher::start() {
    if (!fmt_ctx_) return false;

    av_dump_format(fmt_ctx_, 0, url_.c_str(), 1);

    // 打开网络 I/O
    if (!(fmt_ctx_->oformat->flags & AVFMT_NOFILE)) {
        int ret = avio_open(&fmt_ctx_->pb, url_.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            std::cerr << "[Publisher] Error: could not open url '" << url_ << "': " << ff_err2str(ret) << std::endl;
            return false;
        }
    }

    // 写入头信息
    int ret = avformat_write_header(fmt_ctx_, nullptr);
    if (ret < 0) {
        std::cerr << "[Publisher] Error: write header failed: " << ff_err2str(ret) << std::endl;
        return false;
    }

    is_connected_ = true;
    std::cout << "[INFO] RTMP Publish Started: " << url_ << std::endl;
    return true;
}

void RtmpPublisher::stop() {
    if (fmt_ctx_) {
        if (is_connected_) {
            av_write_trailer(fmt_ctx_);
            is_connected_ = false;
        }
        if (fmt_ctx_->pb) {
            avio_closep(&fmt_ctx_->pb);
        }
        avformat_free_context(fmt_ctx_);
        fmt_ctx_ = nullptr;
    }
}

void RtmpPublisher::sendPacket(AVPacket* packet, AVStream* out_stream, AVRational src_timebase) {
    if (!is_connected_ || !out_stream) {
        av_packet_free(&packet);
        return;
    }

    // --- 关键：时间戳转换 (Rescale) ---
    // 从 编码器时间基 (e.g. 1/30 或 1/44100) 转换到 FLV 流时间基 (通常 1/1000)
    // 注意：av_packet_rescale_ts 会处理 pts, dts 和 duration
    av_packet_rescale_ts(packet, src_timebase, out_stream->time_base);

    packet->stream_index = out_stream->index;

    {
        // FFmpeg 的 av_interleaved_write_frame 不是线程安全的
        std::lock_guard<std::mutex> lock(write_mutex_);
        int ret = av_interleaved_write_frame(fmt_ctx_, packet);
        if (ret < 0) {
            std::cerr << "[Publisher] Error writing frame: " << ff_err2str(ret) << std::endl;
            // 检查是否是网络错误，如果是则标记连接断开
            if (ret == AVERROR(EPIPE) || ret == AVERROR_EOF) {
                std::cerr << "[Publisher] Network connection lost, stopping publisher." << std::endl;
                is_connected_ = false;
            }
        }
    }

    // 发送完毕后释放 packet
    av_packet_free(&packet);
}

void RtmpPublisher::pushVideoPacket(AVPacket* packet) {
    sendPacket(packet, video_stream_, video_enc_tb_);
}

void RtmpPublisher::pushAudioPacket(AVPacket* packet) {
    sendPacket(packet, audio_stream_, audio_enc_tb_);
}