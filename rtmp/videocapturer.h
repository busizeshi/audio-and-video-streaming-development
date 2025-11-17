#ifndef VIDEOCAPTURER_H
#define VIDEOCAPTURER_H

#include <functional>
#include "commonlooper.h"
#include "mediabase.h"

namespace LQF
{
    using std::function;

    class VideoCapturer final : public CommonLooper
    {
    public:
        VideoCapturer();
        ~VideoCapturer() override;
        /**
         * @brief Init
         * @param properties
         * @param "x", x起始位置，缺省为0
         *          "y", y起始位置，缺省为0
         *          "width", 宽度，缺省为屏幕宽带
         *          "height", 高度，缺省为屏幕高度
         *          "format", 像素格式，AVPixelFormat对应的值，缺省为AV_PIX_FMT_YUV420P
         *          "fps", 帧数，缺省为25
         * @return
         */
        RET_CODE Init(const Properties& properties);

        void Loop() override;

        void AddCallback(const function<void(uint8_t*, int32_t)>& callable_object)
        {
            callable_object_ = callable_object;
        }

    private:
        int video_test_ = 0;
        std::string input_yuv_name_;
        int x_{};
        int y_{};
        int width_ = 0;
        int height_ = 0;
        int pixel_format_ = 0;
        int fps_{};
        double frame_duration_ = 40;

        // 本地文件测试
        int openYuvFile(const char* file_name);
        int readYuvFile(uint8_t* yuv_buf, int32_t yuv_buf_size);
        int closeYuvFile() const;
        int64_t yuv_start_time_ = 0; // 起始时间
        double yuv_total_duration_ = 0; // PCM读取累计的时间
        FILE* yuv_fp_ = nullptr;
        uint8_t* yuv_buf_ = nullptr;
        int yuv_buf_size = 0;


        function<void(uint8_t*, int32_t)> callable_object_ = nullptr;

        bool is_first_frame_ = false;
    };
}


#endif // VIDEOCAPTURER_H
