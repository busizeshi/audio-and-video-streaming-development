cmake_minimum_required(VERSION 4.0)
project(muxing_flv)

set(CMAKE_CXX_STANDARD 11)

set(FFMPEG_ROOT "C:/tools/msys64/home/13127/ffmpeg_build")

include_directories(${FFMPEG_ROOT}/include)

link_directories(${FFMPEG_ROOT}/lib)

add_executable(muxing_flv main.cpp)

target_link_libraries(muxing_flv
        avformat
        avcodec
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