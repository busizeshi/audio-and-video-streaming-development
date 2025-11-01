cmake_minimum_required(VERSION 3.10)

project(audio-and-video-streaming-development C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(FFMPEG_BUILD_DIR "D:/devtools/cxx/msys2/home/jwd/ffmpeg_build")
set(SDL2_DIR "${PROJECT_SOURCE_DIR}/SDL2-2.0.10")

# 添加头文件搜索路径
include_directories(${FFMPEG_BUILD_DIR}/include)
include_directories(${SDL2_DIR}/include)

# 添加库文件搜索路径
link_directories(${FFMPEG_BUILD_DIR}/lib)
link_directories(${SDL2_DIR}/lib/x64)

# 添加可执行文件
add_executable(${PROJECT_NAME} main.c)

# 链接所有必需的库
target_link_libraries(${PROJECT_NAME}
        # FFmpeg 核心库
        avformat
        avcodec
        swresample
        avutil

        # FFmpeg 依赖的第三方编码/工具库
        x264
        fdk-aac
        mp3lame
        vpx
        z
        bz2
        iconv
        lzma

        #链接 SDL2 库
        SDL2main
        SDL2

        # Windows 系统库
        ws2_32
        secur32
        bcrypt
        strmiids
)