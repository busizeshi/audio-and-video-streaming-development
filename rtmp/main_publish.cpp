#include "SDL.h"
#include "dlog.h"

using namespace std;

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/audio_fifo.h>
}

#include "pushwork.h"
using namespace LQF;

//#define RTMP_URL "rtmp://111.229.231.225/live/livestream"
#define RTMP_URL "rtmp://192.168.1.13/live/livestream"
// ffmpeg -re -i  rtmp_test_hd.flv  -vcodec copy -acodec copy  -f flv -y rtmp://111.229.231.225/live/livestream
// ffmpeg -re -i  rtmp_test_hd.flv  -vcodec copy -acodec copy  -f flv -y rtmp://192.168.1.12/live/livestream
//// ffmpeg -re -i  1920x832_25fps.flv  -vcodec copy -acodec copy  -f flv -y rtmp://111.229.231.225/live/livestream
int main(int argc, char* argv[])
{
    init_logger("rtmp_push.log", S_INFO);
    {
        PushWork pushWork; // 可以实例化多个，同时推送多路流

        Properties properties;
        // 音频test模式
        properties.SetProperty("audio_test", 1);    // 音频测试模式
        properties.SetProperty("input_pcm_name", R"(D:\dev\cxx\audio-and-video-streaming-development\resource\output_audio.pcm)");
        // 麦克风采样属性
        properties.SetProperty("mic_sample_fmt", AV_SAMPLE_FMT_S16);
        properties.SetProperty("mic_sample_rate", 48000);
        properties.SetProperty("mic_channels", 2);
        // 音频编码属性
        properties.SetProperty("audio_sample_rate", 48000);
        properties.SetProperty("audio_bitrate", 64*1024);
        properties.SetProperty("audio_channels", 2);

        //视频test模式
        properties.SetProperty("video_test", 1);
        properties.SetProperty("input_yuv_name", R"(D:\dev\cxx\audio-and-video-streaming-development\resource\output_video.yuv)");
        // 桌面录制属性
        properties.SetProperty("desktop_x", 0);
        properties.SetProperty("desktop_y", 0);
        properties.SetProperty("desktop_width", 720); //测试模式时和yuv文件的宽度一致
        properties.SetProperty("desktop_height", 480);  //测试模式时和yuv文件的高度一致
        //    properties.SetProperty("desktop_pixel_format", AV_PIX_FMT_YUV420P);
        properties.SetProperty("desktop_fps", 25);//测试模式时和yuv文件的帧率一致
        // 视频编码属性
        properties.SetProperty("video_bitrate", 512*1024);  // 设置码率

        // 使用缺省的
        // rtmp推流
        properties.SetProperty("rtmp_url", RTMP_URL);//测试模式时和yuv文件的帧率一致
        properties.SetProperty("rtmp_debug", 1);
        if(pushWork.Init(properties) != RET_OK)
        {
            LogError("pushWork.Init failed");
			pushWork.DeInit();
            return 0;
        }
        pushWork.Loop();

    } // 测试析构

	printf("rtmp push finish");
    return 0;
}



