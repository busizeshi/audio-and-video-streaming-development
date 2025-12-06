cmake_minimum_required(VERSION 4.0)
project(demux_mp4 CXX)

# 设置FFmpeg路径
set(FFMPEG_ROOT "D:/devtools/cxx/msys2/home/jwd/ffmpeg_build")

# 添加头文件目录
include_directories(${FFMPEG_ROOT}/include)

# 添加库文件目录
link_directories(${FFMPEG_ROOT}/lib)


# 创建可执行文件
add_executable(${PROJECT_NAME}
#        main.c
        demux_mp4.cpp
)

# 链接FFmpeg库及依赖
target_link_libraries(${PROJECT_NAME}
        avformat
        avcodec
        avutil
        swresample
        swscale
        # FFmpeg依赖库
        ws2_32
        bcrypt
        iconv
        z
        bz2
        lzma
        secur32
        ole32
        strmiids
        user32
        mfuuid
        crypt32
        # 编解码器库
        vpx
        fdk-aac
        mp3lame
        x264
)