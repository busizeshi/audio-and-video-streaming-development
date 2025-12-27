cmake_minimum_required(VERSION 3.12)
project(mp4-player-demo2 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置FFmpeg和SDL2的路径
set(FFMPEG_ROOT "D:/devtools/cxx/msys2/home/jwd/ffmpeg_build")
set(SDL2_ROOT "D:/devtools/cxx/msys2/ucrt64")

# 设置包含目录
include_directories(${FFMPEG_ROOT}/include)
include_directories(${SDL2_ROOT}/include/SDL2)

# 设置库目录
link_directories(${FFMPEG_ROOT}/lib)
link_directories(${SDL2_ROOT}/lib)

# 设置编译选项，防止SDL2使用自己的入口点
add_compile_definitions(SDL_MAIN_HANDLED)

# 创建可执行文件 - 包含所有源文件
add_executable(${PROJECT_NAME}
        main.cpp
        SDLPlayer.cpp
        SDLPlayer.h
        VideoDecode.cpp
        VideoDecode.h
)

# 设置链接标志以确保使用控制台子系统
set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "-mconsole")

# 链接库
target_link_libraries(${PROJECT_NAME}
        # FFmpeg库
        avformat
        avcodec
        avutil
        swscale
        swresample
        postproc
        # SDL2库
        SDL2
        # 压缩库
        z
        bz2
        lzma
        # 音频编码库
        fdk-aac
        mp3lame
        # 视频编码库
        x264
        vpx
        # Windows库
        ws2_32
        winmm
        version
        ole32
        oleaut32
        uuid
        gdi32
        user32
        shell32
        advapi32
        bcrypt
        setupapi
        Secur32
        imm32
        comdlg32
        mmdevapi
        dxva2
        strmiids
        avicap32
        vfw32
        iconv
        charset
)