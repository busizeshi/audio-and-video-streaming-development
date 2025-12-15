cmake_minimum_required(VERSION 4.0)
project(audio_mix CXX)

# 设置FFmpeg路径
set(FFMPEG_ROOT "D:/devtools/cxx/msys2/home/jwd/ffmpeg_build")

# 添加头文件目录
include_directories(${FFMPEG_ROOT}/include)

# 添加库文件目录
link_directories(${FFMPEG_ROOT}/lib)


# 创建可执行文件
add_executable(${PROJECT_NAME}
        main.cpp
        AudioMixer.cpp
        AudioMixer.h
)

# 链接FFmpeg库及依赖
target_link_libraries(${PROJECT_NAME}
        # FFmpeg库（按依赖顺序排列）
        avfilter
        avformat
        avcodec
        swresample
        swscale
        postproc
        avutil
        
        # Windows安全相关库
        secur32
        crypt32
        ssl
        crypto
        
        # 系统库
        ws2_32
        bcrypt
        shlwapi
        
        # COM相关库
        ole32
        oleaut32
        uuid
        strmiids
        
        # 第三方库
        vpx
        fdk-aac
        mp3lame
        x264
        
        # 基础系统库
        m
        pthread
        iconv
        z
        bz2
        lzma
)
