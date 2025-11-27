cmake_minimum_required(VERSION 4.0)
project(rtsp_publish)

set(CMAKE_CXX_STANDARD 17)

#乐木骆电脑环境
#set(FFMPEG_DIR "C:/tools/msys64/home/13127/ffmpeg_build")
set(FFMPEG_DIR "D:/devtools/cxx/msys2/home/jwd/ffmpeg_build")

set(SDL2_DIR "D:/dev/cxx/audio-and-video-streaming-development/rtsp-publish/SDL2")

find_package(SDL2 REQUIRED)

set(SOURCE_FILES main.cpp
        VideoCapture.cpp
        VideoCapture.h
        ConfigManager.cpp
        ConfigManager.h
        AudioCapture.cpp
        ThreadSafeQueue.h
        SDLViewer.cpp
        SDLViewer.h)

add_executable(${PROJECT_NAME} ${SOURCE_FILES})

# 添加测试可执行文件
add_executable(test_config ConfigManager.cpp ConfigManager.h)

target_include_directories(
        ${PROJECT_NAME} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/librtmp
        ${FFMPEG_DIR}/include
        ${SDL2_INCLUDE_DIRS}
)

target_include_directories(
        test_config PUBLIC
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

        ${SDL2_LIBRARIES}

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

target_link_libraries(test_config PUBLIC
        ${SDL2_LIBRARIES}
        ws2_32
        wsock32)

message(STATUS "项目源文件目录: ${CMAKE_CURRENT_SOURCE_DIR}")
message(STATUS "FFmpeg 头文件路径: ${FFMPEG_DIR}/include")
message(STATUS "FFmpeg 库文件路径: ${FFMPEG_DIR}/lib")
message(STATUS "SDL2 头文件路径: ${SDL2_INCLUDE_DIRS}")
message(STATUS "SDL2 库文件: ${SDL2_LIBRARIES}")