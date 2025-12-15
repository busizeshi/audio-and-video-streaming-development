#include "audiomixer.h"
#include <cstdio>
#include <vector>

// 假设两个 PCM 文件都是 44100Hz, 双声道, S16LE (16位整数)
#define SAMPLE_RATE 44100
#define CHANNELS 2
#define SAMPLE_FMT AV_SAMPLE_FMT_S16
#define BYTES_PER_SAMPLE 2

// 每次读取的缓冲区大小 (例如 1024 个样本)
#define FRAME_SIZE (1024 * CHANNELS * BYTES_PER_SAMPLE)

int main(int argc, char* argv[]) {
    if (argc < 4) {
        printf("Usage: %s <input1.pcm> <input2.pcm> <output.pcm>\n", argv[0]);
        return -1;
    }

    FILE* f1 = fopen(argv[1], "rb");
    FILE* f2 = fopen(argv[2], "rb");
    FILE* fout = fopen(argv[3], "wb");

    if (!f1 || !f2 || !fout) {
        printf("Failed to open files\n");
        return -1;
    }

    AudioMixer mixer;

    // 添加两个输入流 (索引分别为 0 和 1)
    mixer.addInput(SAMPLE_RATE, CHANNELS, SAMPLE_FMT);
    mixer.addInput(SAMPLE_RATE, CHANNELS, SAMPLE_FMT);

    // 设置输出格式
    mixer.setOutput(SAMPLE_RATE, CHANNELS, SAMPLE_FMT);

    // 初始化，使用 "longest" 模式 (输出时长等于最长的那个输入)
    if (mixer.init("longest") < 0) {
        printf("Mixer init failed\n");
        return -1;
    }

    uint8_t buf1[FRAME_SIZE];
    uint8_t buf2[FRAME_SIZE];
    uint8_t out_buf[FRAME_SIZE * 4]; // 输出缓冲区大一点比较安全

    bool f1_eof = false;
    bool f2_eof = false;

    printf("Start mixing...\n");

    while (!f1_eof || !f2_eof) {
        // 读取文件 1
        if (!f1_eof) {
            int len1 = fread(buf1, 1, FRAME_SIZE, f1);
            if (len1 > 0) {
                mixer.sendFrame(0, buf1, len1);
            } else {
                f1_eof = true;
                mixer.sendFrame(0, nullptr, 0); // 发送 EOF
            }
        }

        // 读取文件 2
        if (!f2_eof) {
            int len2 = fread(buf2, 1, FRAME_SIZE, f2);
            if (len2 > 0) {
                mixer.sendFrame(1, buf2, len2);
            } else {
                f2_eof = true;
                mixer.sendFrame(1, nullptr, 0); // 发送 EOF
            }
        }

        // 尝试获取混音后的数据
        while (true) {
            int ret = mixer.receiveFrame(out_buf, sizeof(out_buf));
            if (ret > 0) {
                fwrite(out_buf, 1, ret, fout);
            } else if (ret == 0) {
                // 需要更多输入数据
                break;
            } else {
                // 混音结束 (EOF)
                goto end;
            }
        }
    }

    // 循环结束后，继续拉取剩余的数据
    while (true) {
        int ret = mixer.receiveFrame(out_buf, sizeof(out_buf));
        if (ret > 0) fwrite(out_buf, 1, ret, fout);
        else break;
    }

end:
    printf("Mixing done.\n");
    fclose(f1);
    fclose(f2);
    fclose(fout);

    return 0;
}