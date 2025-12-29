cmake_minimum_required(VERSION 3.12)
project(mp4-player-demo2 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(FFMPEG_ROOT "C:/tools/msys64/home/13127/ffmpeg_build")

include_directories(${FFMPEG_ROOT}/include)
include_directories(${SDL2_ROOT}/include/SDL2)

link_directories(${FFMPEG_ROOT}/lib)
link_directories(${SDL2_ROOT}/lib)

add_compile_definitions(SDL_MAIN_HANDLED)

add_executable(${PROJECT_NAME}
        main.cpp
        SDLPlayer.cpp
        SDLPlayer.h
        AudioDecoder.cpp
        AudioDecoder.h
)

set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "-mconsole")

target_link_libraries(${PROJECT_NAME}
        avformat
        avcodec
        avutil
        swscale
        swresample
        postproc
        SDL2
        z
        bz2
        lzma
        fdk-aac
        mp3lame
        x264
        vpx
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