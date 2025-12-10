cmake_minimum_required(VERSION 4.0)
project(encode_video)

set(CMAKE_CXX_STANDARD 11)

# 设置FFmpeg路径
set(FFMPEG_ROOT "C:/tools/msys64/home/13127/ffmpeg_build")

# 添加头文件目录
include_directories(${FFMPEG_ROOT}/include)

# 添加库文件目录
link_directories(${FFMPEG_ROOT}/lib)

# 添加可执行文件
add_executable(encode_video main.cpp)

# 链接FFmpeg库和其他必要的系统库
target_link_libraries(encode_video
        avcodec
        avformat
        avutil
        swresample
        mp3lame
        fdk-aac
        vpx
        x264
        lzma
        bz2
        z
        iconv
        bcrypt
        ole32
        oleaut32
        strmiids
        ws2_32
        secur32
        crypt32
)