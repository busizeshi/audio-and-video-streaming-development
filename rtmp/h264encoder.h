#ifndef H264ENCODER_H
#define H264ENCODER_H
#include "mediabase.h"
#ifdef __cplusplus
extern "C" {
#endif
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#ifdef __cplusplus
};
#endif


namespace LQF
{
    using std::string;

    class H264Encoder final
    {
    public:
        H264Encoder();
        /**
         * @brief Init
         * @param properties
         * @return
         */
        int Init(const Properties& properties);
        virtual ~H264Encoder();
        int Encode(uint8_t* in, int in_samples, uint8_t* out, int& out_size);
        int Encode(AVFrame* frame, uint8_t* out, int& out_size);
        int get_sps(uint8_t* sps, int& sps_len) const;
        int get_pps(uint8_t* pps, int& pps_len);

        int get_width() const
        {
            return ctx_->width;
        }

        int get_height() const
        {
            return ctx_->height;
        }

        double get_framerate() const
        {
            return ctx_->framerate.num / ctx_->framerate.den;
        }

        int64_t get_bit_rate() const
        {
            return ctx_->bit_rate;
        }

        uint8_t* get_sps_data() const
        {
            return (uint8_t*)sps_.c_str();
        }

        int get_sps_size()
        {
            return sps_.size();
        }

        uint8_t* get_pps_data() const
        {
            return (uint8_t*)pps_.c_str();
        }

        int get_pps_size() const
        {
            return pps_.size();
        }

        AVPacket* Encode(uint8_t* yuv, uint64_t pts = 0, int flush = 0);

        AVCodecContext* GetCodecContext() const
        {
            return ctx_;
        }

    private:
        int count{};
        int data_size_{};
        int framecnt{};

        // 初始化参数
        string codec_name_; //
        int width_{}; // 宽
        int height_{}; // 高
        int fps_{}; // 帧率
        int b_frames_{}; // b帧数量
        int bitrate_{};
        int gop_{};
        bool annexb_{}; // 默认不带star code
        int threads_{};
        string profile_;
        string level_id_;

        string sps_;
        string pps_;
        //data
        AVFrame* frame_ = nullptr;
        uint8_t* picture_buf_ = nullptr;
        AVPacket packet_{};

        //encoder message
        const AVCodec* codec_ = nullptr;
        AVDictionary* param = nullptr;
        AVCodecContext* ctx_ = nullptr;

        int64_t pts_ = 0;
    };
}
#endif // H264ENCODER_H
