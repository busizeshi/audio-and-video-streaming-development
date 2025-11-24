cmake_minimum_required(VERSION 3.10)

project(rtsp_publish CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(FFMPEG_BUILD_DIR "C:/tools/msys64/home/13127/ffmpeg_build")
set(SDL2_DIR "${PROJECT_SOURCE_DIR}/SDL2")

# 添加头文件搜索路径
include_directories(${FFMPEG_BUILD_DIR}/include)
include_directories(${SDL2_DIR}/include)

# 添加库文件搜索路径
link_directories(${FFMPEG_BUILD_DIR}/lib)
link_directories(${SDL2_DIR}/lib/x64)

# 添加可执行文件
set(SOURCES 
    main.cpp
    VideoCapture.cpp
    AudioCapture.cpp
    SDLDisplay.cpp
)

add_executable(${PROJECT_NAME} ${SOURCES})

# 链接所有必需的库
target_link_libraries(${PROJECT_NAME}
        avdevice
        avfilter
        avformat
        avcodec
        swresample
        swscale
        postproc
        avutil

        # 编码库
        x264
        fdk-aac
        mp3lame
        vpx
        z
        bz2
        iconv
        lzma

        # SDL
        SDL2main
        SDL2

        # Windows 系统库
        ws2_32
        secur32
        bcrypt
        strmiids
        shlwapi
        ole32
        oleaut32
        vfw32
        advapi32
        shell32
        user32
        gdi32
        winspool
        comdlg32
        uuid
        kernel32
        crypt32
        iphlpapi
        mswsock
        wldap32
)
