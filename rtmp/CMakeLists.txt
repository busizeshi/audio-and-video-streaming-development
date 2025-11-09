# 1. CMake 最低版本和项目定义
cmake_minimum_required(VERSION 3.15)
# 项目名称，同时指定 C 和 CXX (C++) 语言
project(rtmp-publish C CXX)

# 2. 设置 C++ 11 标准 (从你的 .pro 文件中得知)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 3. --- 依赖项配置 (请根据你的实际路径修改) ---

# 你的 64-bit FFmpeg 编译输出路径 (使用 /)
set(FFMPEG_DIR "D:/devtools/cxx/msys2/home/jwd/ffmpeg_build")

# 你的 64-bit MinGW SDL2 开发包解压路径 (!! 替换为你下载的新 SDL2 路径 !!)
set(SDL2_PATH "D:/dev/cxx/audio-and-video-streaming-development/rtmp/SDL2") # 示例路径，请修改为你实际解压的文件夹名

find_package(SDL2 REQUIRED)

# 5. --- 收集所有源文件 ---
# (从你的 .pro 文件中复制而来)
set(SOURCE_FILES
        main_publish.cpp
        librtmp/amf.c
        librtmp/hashswf.c
        librtmp/log.c
        librtmp/parseurl.c
        librtmp/rtmp.c
        rtmp_publish.cpp
        aacencoder.cpp
        audioresample.cpp
        dlog.cpp
        rtmppusher.cpp
        looper.cpp
        h264encoder.cpp
        rtmpbase.cpp
        naluloop.cpp
        pushwork.cpp
        audiocapturer.cpp
        commonlooper.cpp
        mediabase.cpp
        aacrtmppackager.cpp
        videocapturer.cpp
        videooutsdl.cpp
        avtimebase.cpp
)

# 6. --- 定义可执行文件 ---
add_executable(rtmp-publish ${SOURCE_FILES})

# 7. --- 配置头文件搜索路径 ---
target_include_directories(rtmp-publish PUBLIC
        # 添加项目根目录和 librtmp 目录
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/librtmp

        # 添加 FFmpeg 头文件
        ${FFMPEG_DIR}/include

        # 添加 SDL2 头文件 (由 find_package 自动设置)
        ${SDL2_INCLUDE_DIRS}
)

# 8. --- 配置库文件搜索路径 ---
# (注意：我们使用 target_link_libraries 来指定库，
#  但如果库不在标准路径，target_link_directories 很有用)
target_link_directories(rtmp-publish PUBLIC
        # 添加 FFmpeg 库路径
        ${FFMPEG_DIR}/lib
)

# 9. --- 链接所有库文件 ---
target_link_libraries(rtmp-publish PUBLIC
        # FFmpeg 库
        avformat
        avcodec
        avdevice
        avfilter
        avutil
        postproc
        swresample
        swscale

        # --- FFmpeg 的依赖库 ---
        mp3lame
        vpx
        x264
        fdk-aac
        iconv
        z
        lzma

        # SDL2 库
        ${SDL2_LIBRARIES}

        # Windows 系统库
        ws2_32
        wsock32
        bcrypt
        uuid
        ole32
        strmiids  # <-- 新添加的库
)

# 10. --- 打印调试信息 ---
message(STATUS "项目源文件目录: ${CMAKE_CURRENT_SOURCE_DIR}")
message(STATUS "FFmpeg 头文件路径: ${FFMPEG_DIR}/include")
message(STATUS "FFmpeg 库文件路径: ${FFMPEG_DIR}/lib")
message(STATUS "SDL2 头文件路径: ${SDL2_INCLUDE_DIRS}")
message(STATUS "SDL2 库文件: ${SDL2_LIBRARIES}")