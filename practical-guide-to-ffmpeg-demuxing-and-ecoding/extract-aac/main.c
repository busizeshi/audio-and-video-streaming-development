/**
 * @file main.c
 * @brief 提取aac文件
 */

#include <stdio.h>
#include <libavutil/log.h>
#include <libavformat/avio.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

#define ADTS_HEADER_LEN 7

const int sampling_frequencies[] = {
        96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000
};

// 将FFmpeg的profile转换为ADTS的profile
int ffmpeg_profile_to_adts_profile(int ffmpeg_profile) {
    switch (ffmpeg_profile) {
        case FF_PROFILE_AAC_MAIN:
            return 0; // Main Profile
        case FF_PROFILE_AAC_LOW:
            return 1; // Low Complexity Profile (LC)
        case FF_PROFILE_AAC_SSR:
            return 2; // Scalable Sample Rate Profile (SSR)
        case FF_PROFILE_AAC_LTP:
            return 3; // Long Term Prediction Profile (LTP)
        default:
            return 1; // 默认使用LC Profile
    }
}

int adts_header(char *const p_adts_header, const int data_length, const int profile, const int samplerate,
                const int channels) {
    int sampling_frequency_index = 0;
    int adts_len = data_length + 7;
    int adts_profile = ffmpeg_profile_to_adts_profile(profile);

    int frequencies_size = sizeof(sampling_frequencies) / sizeof(sampling_frequencies[0]);

    int i = 0;

    for (i = 0; i < frequencies_size; i++) {
        if (samplerate == sampling_frequencies[i]) {
            sampling_frequency_index = i;
            break;
        }
    }
    if (i == frequencies_size) {
        printf("samplerate:%d not support\n", samplerate);
        return -1;
    }

    // ADTS header
    // 字节0: 0xFF 同步字节
    p_adts_header[0] = 0xff;
    
    // 字节1: 0xF9 syncword(MSB), ID, layer, protection_absent
    p_adts_header[1] = 0xf9; // protection_absent = 1 (没有CRC校验)
    
    // 字节2: profile, sampling_frequency_index, private_bit, channel_configuration(MSB)
    p_adts_header[2] = (adts_profile << 6) | (sampling_frequency_index << 2) | (channels >> 2);
    
    // 字节3: channel_configuration(LSB), original_copy, home, copyright_identification_bit, 
    //        copyright_identification_start, aac_frame_length(MSB)
    p_adts_header[3] = ((channels & 3) << 6) | ((adts_len >> 11) & 0x03);
    
    // 字节4: aac_frame_length(MID)
    p_adts_header[4] = (adts_len >> 3) & 0xFF;
    
    // 字节5: aac_frame_length(LSB), adts_buffer_fullness(MSB)
    p_adts_header[5] = ((adts_len & 0x07) << 5) | 0x1F; // 0x1F表示buffer fullness为全1，代表可变码率
    
    // 字节6: adts_buffer_fullness(LSB), no_raw_data_blocks_in_frame
    p_adts_header[6] = 0xFC;

    return 0;
}

int main(int argc, char *argv[]) {
    int ret = -1;
    char errors[1024];

    char *in_filename = NULL;
    char *aac_filename = NULL;

    FILE *aac_fd = NULL;

    int audio_index = -1;
    size_t len = 0;

    AVFormatContext *ifmt_ctx = NULL;
    AVPacket pkt;

    av_log_set_level(AV_LOG_DEBUG);

    if (argc < 3) {
        av_log(NULL, AV_LOG_ERROR, "the count of parameters should be more than three!\n");
        return -1;
    }

    in_filename = argv[1];
    aac_filename = argv[2];

    if (in_filename == NULL || aac_filename == NULL) {
        av_log(NULL, AV_LOG_ERROR, "src or dts file is null, plz check them!\n");
        return -1;
    }

    aac_fd = fopen(aac_filename, "wb");
    if (!aac_fd) {
        av_log(NULL, AV_LOG_ERROR, "Could not open destination file %s\n", aac_filename);
        return -1;
    }

    if ((ret = avformat_open_input(&ifmt_ctx, in_filename, NULL, NULL)) < 0) {
        av_strerror(ret, errors, 1024);
        av_log(NULL, AV_LOG_ERROR, "Could not open source file: %s, %d(%s)\n",
               in_filename,
               ret,
               errors);
        fclose(aac_fd);
        return -1;
    }

    if ((ret = avformat_find_stream_info(ifmt_ctx, NULL)) < 0) {
        av_strerror(ret, errors, 1024);
        av_log(NULL, AV_LOG_ERROR, "failed to find stream information: %s, %d(%s)\n",
               in_filename,
               ret,
               errors);
        avformat_close_input(&ifmt_ctx);
        fclose(aac_fd);
        return -1;
    }

    av_dump_format(ifmt_ctx, 0, in_filename, 0);

    av_init_packet(&pkt);

    audio_index = av_find_best_stream(ifmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (audio_index < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not find %s stream in input file %s\n",
               av_get_media_type_string(AVMEDIA_TYPE_AUDIO),
               in_filename);
        avformat_close_input(&ifmt_ctx);
        fclose(aac_fd);
        return AVERROR(EINVAL);
    }

    printf("audio profile:%d, FF_PROFILE_AAC_LOW:%d\n",
           ifmt_ctx->streams[audio_index]->codecpar->profile,
           FF_PROFILE_AAC_LOW);

    if (ifmt_ctx->streams[audio_index]->codecpar->codec_id != AV_CODEC_ID_AAC) {
        printf("the media file no contain AAC stream, it's codec_id is %d\n",
               ifmt_ctx->streams[audio_index]->codecpar->codec_id);
        avformat_close_input(&ifmt_ctx);
        fclose(aac_fd);
        return -1;
    }

    while (av_read_frame(ifmt_ctx, &pkt) >= 0) {
        if (pkt.stream_index == audio_index) {
            char adts_header_buf[7] = {0};
            ret = adts_header(adts_header_buf, pkt.size,
                              ifmt_ctx->streams[audio_index]->codecpar->profile,
                              ifmt_ctx->streams[audio_index]->codecpar->sample_rate,
                              ifmt_ctx->streams[audio_index]->codecpar->channels);
            
            if (ret < 0) {
                av_log(NULL, AV_LOG_ERROR, "Failed to create ADTS header\n");
                av_packet_unref(&pkt);
                continue;
            }
            
            // 写入ADTS头部
            fwrite(adts_header_buf, 1, 7, aac_fd);
            
            // 写入AAC数据
            len = fwrite(pkt.data, 1, pkt.size, aac_fd);
            if (len != pkt.size) {
                av_log(NULL, AV_LOG_WARNING, "warning, length of written data isn't equal pkt.size(%zu, %d)\n",
                       len,
                       pkt.size);
            }
        }
        av_packet_unref(&pkt);
    }

    // 正常结束处理
    if (ifmt_ctx) {
        avformat_close_input(&ifmt_ctx);
    }
    if (aac_fd) {
        fclose(aac_fd);
    }

    return 0;
}