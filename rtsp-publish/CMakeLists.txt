cmake_minimum_required(VERSION 4.0)
project(rtsp_publish)

set(CMAKE_CXX_STANDARD 11)

set(FFMPEG_DIR "C:/tools/msys64/home/13127/ffmpeg_build")
set(SDL2_DIR "D:/cxx/audio-and-video-streaming-development/rtsp-publish/SDL2")

# 手动指定SDL2的头文件和库文件路径
set(SDL2_INCLUDE_DIRS "${SDL2_DIR}/include")
set(SDL2_LIBRARIES "${SDL2_DIR}/lib/x64/SDL2.lib" "${SDL2_DIR}/lib/x64/SDL2main.lib")

set(SOURCE_FILES main.cpp
        VideoCapture.cpp
        VideoCapture.h
        AudioCapture.cpp
        AudioCapture.h)

add_executable(${PROJECT_NAME} ${SOURCE_FILES})

target_include_directories(
        ${PROJECT_NAME} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/librtmp
        ${FFMPEG_DIR}/include
        ${SDL2_INCLUDE_DIRS}
)

target_link_directories(
        ${PROJECT_NAME} PUBLIC
        ${FFMPEG_DIR}/lib
)

target_link_libraries(${PROJECT_NAME} PUBLIC
        avformat
        avcodec
        avdevice
        avfilter
        avutil
        postproc
        swresample
        swscale

        mp3lame
        vpx
        x264
        fdk-aac
        iconv
        z
        lzma

        # 确保SDL2main在SDL2之前链接
        ${SDL2_DIR}/lib/x64/SDL2main.lib
        ${SDL2_DIR}/lib/x64/SDL2.lib

        ws2_32
        wsock32
        bcrypt
        uuid
        ole32
        strmiids

        -lbz2
        -lsecur32
        -lvfw32
        -lshlwapi
        -lavcodec
)

message(STATUS "项目源文件目录: ${CMAKE_CURRENT_SOURCE_DIR}")
message(STATUS "FFmpeg 头文件路径: ${FFMPEG_DIR}/include")
message(STATUS "FFmpeg 库文件路径: ${FFMPEG_DIR}/lib")
message(STATUS "SDL2 头文件路径: ${SDL2_INCLUDE_DIRS}")
message(STATUS "SDL2 库文件路径: ${SDL2_DIR}/lib/x64/")