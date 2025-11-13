#include "rtmpbase.h"
#include "dlog.h"

#include <utility>
#include "librtmp/rtmp_sys.h"
#include "librtmp/log.h"

#ifdef _WIN32
#pragma comment(lib, "ws2_32.lib")
#endif
namespace LQF
{
    bool RTMPBase::initRtmp()
    {
        bool ret_code = true;
#ifdef WIN32
        WORD version;
        WSADATA wsaData;
        version = MAKEWORD(1, 1);
        ret_code = (WSAStartup(version, &wsaData) == 0);
#endif
        LogInfo("at rtmp object create");
        rtmp_ = RTMP_Alloc();
        RTMP_Init(rtmp_);
        return ret_code;
    }

    RTMPBase::RTMPBase() :
        rtmp_obj_type_(RTMP_BASE_TYPE_UNKNOW),
        enable_video_(true),
        enable_audio_(true)
    {
        initRtmp();
    }

    RTMPBase::RTMPBase(RTMP_BASE_TYPE rtmp_obj_type) :
        rtmp_obj_type_(rtmp_obj_type),
        enable_video_(true),
        enable_audio_(true)
    {
        initRtmp();
    }

    RTMPBase::RTMPBase(RTMP_BASE_TYPE rtmp_obj_type, std::string& url) :
        rtmp_obj_type_(rtmp_obj_type),
        url_(url),
        enable_video_(true),
        enable_audio_(true)
    {
        initRtmp();
    }

    RTMPBase::RTMPBase(std::string& url, bool is_recv_audio, bool is_recv_video) :
        rtmp_obj_type_(RTMP_BASE_TYPE_PLAY),
        url_(url),
        enable_video_(is_recv_audio),
        enable_audio_(is_recv_video)
    {
        initRtmp();
    }

    RTMPBase::~RTMPBase()
    {
        if (IsConnect())
        {
            Disconnect();
        }
        RTMP_Free(rtmp_);
        rtmp_ = nullptr;
#ifdef WIN32
        WSACleanup();
#endif // WIN32
    }

    void RTMPBase::SetConnectUrl(std::string& url)
    {
        url_ = url;
    }

    bool RTMPBase::SetReceiveAudio(bool is_recv_audio)
    {
        if (is_recv_audio == enable_audio_)
            return true;
        if (IsConnect())
        {
            LogInfo("zkzszd RTMP_SendReceiveAudio");
            if (RTMP_SendReceiveAudio(rtmp_, is_recv_audio))
            {
                is_recv_audio = enable_audio_;
                return true;
            }
        }
        else
            is_recv_audio = enable_audio_;
        return false;
    }

    bool RTMPBase::SetReceiveVideo(bool is_recv_video)
    {
        if (is_recv_video == enable_video_)
            return true;
        if (IsConnect())
        {
            if (RTMP_SendReceiveVideo(rtmp_, is_recv_video))
            {
                is_recv_video = enable_video_;
                return true;
            }
        }
        else
            is_recv_video = enable_video_;

        return false;
    }

    bool RTMPBase::IsConnect()
    {
        return RTMP_IsConnected(rtmp_);
    }

    void RTMPBase::Disconnect()
    {
        RTMP_Close(rtmp_);
    }

    bool RTMPBase::Connect()
    {
        //如果 RTMP 推流失败后重新连，librtmp 内部状态会很乱。
        //必须释放旧对象，重新创建一个新的 RTMP 对象，才能保证重新连接成功
        RTMP_Free(rtmp_);
        rtmp_ = RTMP_Alloc();
        RTMP_Init(rtmp_);

        LogInfo("base begin connect");
        //设置连接超时 默认30s
        rtmp_->Link.timeout = 10;
        //设置连接地址，解析url,提取字段(hostname,port,app,stream key)
        if (RTMP_SetupURL(rtmp_, (char*)url_.c_str()) < 0)
        {
            LogInfo("RTMP_SetupURL failed!");
            return FALSE;
        }
        rtmp_->Link.lFlags |= RTMP_LF_LIVE; //  设置直播流，不可seek
        RTMP_SetBufferMS(rtmp_, 3600 * 1000); //  设置播放器缓冲时间，推流时这个设置一般不重要
        if (rtmp_obj_type_ == RTMP_BASE_TYPE_PUSH)
            RTMP_EnableWrite(
                rtmp_); // 设置推流，默认rtmp时播放模式，否则无法推流/RTMP_ConnectStream 会失败/RTMP_SendPacket 会返回错误
        //        建立TCP+RTMP握手。此处报错一般是:地址写错/防火墙阻拦 1935 端口/服务器未开启推流权限
        if (!RTMP_Connect(rtmp_, nullptr))
        {
            LogInfo("RTMP_Connect failed!");
            return FALSE;
        }

        //        向服务器发送createStream 命令，获得stream id，进入可收发数据状态
        if (!RTMP_ConnectStream(rtmp_, 0))
        {
            LogInfo("RTMP_ConnectStream failed");
            return FALSE;
        }
        //判断是否打开音视频,默认打开
        if (rtmp_obj_type_ == RTMP_BASE_TYPE_PUSH)
        {
            if (!enable_video_)
            {
                RTMP_SendReceiveVideo(rtmp_, enable_video_);
            }
            if (!enable_audio_)
            {
                RTMP_SendReceiveAudio(rtmp_, enable_audio_);
            }
        }

        return true;
    }

    bool RTMPBase::Connect(std::string url)
    {
        url_ = std::move(url);
        return Connect();
    }

    uint32_t RTMPBase::GetSampleRateByFreqIdx(uint8_t freq_idx)
    {
        uint32_t freq_idx_table[] = {
            96000, 88200, 64000, 48000, 44100, 32000,
            24000, 22050, 16000, 12000, 11025, 8000, 7350
        };
        if (freq_idx < 13)
        {
            return freq_idx_table[freq_idx];
        }
        LogError("freq_idx:%d is error", freq_idx);
        return 44100;
        //    switch (freq_idx)
        //    {
        //        case 0 : return 96000;
        //        case 1: return 88200;
        //        case 2: return 64000;
        //        case 3: return 48000;
        //        case 4: return 44100;
        //        case 5: return 32000;
        //        case 6: return 24000;
        //        case 7: return 22050;
        //        case 8: return 16000;
        //        case 9: return 12000;
        //        case 10: return 11025;
        //        case 11: return 8000;
        //        case 12 : return 7350;
        //        default: return 44100;
        //    }
        //    return 44100;
    }
}
