cmake_minimum_required(VERSION 3.10)

project(ffplay_debug C)

set(CMAKE_C_STANDARD 11)

# Windows 兼容性定义
add_compile_definitions(_WIN32_WINNT=0x0601 WIN32_LEAN_AND_MEAN)
add_compile_definitions(__USE_MINGW_ANSI_STDIO=1)

# =========================================================
# 1. 源文件 (必须包含 opt_common.c !)
# =========================================================
set(SOURCES
        ffplay.c
        cmdutils.c
        opt_common.c  # <--- 务必确认此文件已复制到项目目录
)

set(HEADERS
        cmdutils.h
)

# =========================================================
# 2. 路径设置
# =========================================================
# FFmpeg 编译产物路径
set(FFMPEG_ROOT "C:/tools/msys64/home/13127/ffmpeg_build")
# MinGW 系统库路径 (用于找 x264, sdl2 等)
set(MSYS_LIB_DIR "C:/tools/msys64/mingw64/lib")

# 头文件路径
include_directories(
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${FFMPEG_ROOT}/include
        C:/tools/msys64/mingw64/include/SDL2
        C:/tools/msys64/mingw64/include # 防止找不到 x264.h 等
)

# 库文件查找路径
link_directories(
        ${FFMPEG_ROOT}/lib
        ${MSYS_LIB_DIR}
)

# =========================================================
# 3. 创建可执行文件
# =========================================================
add_executable(ffplay_debug ${SOURCES} ${HEADERS})

# 解决 SDL2 main 函数冲突
target_compile_definitions(ffplay_debug PRIVATE SDL_MAIN_HANDLED)

# =========================================================
# 4. 链接库 (顺序至关重要！)
#    原则：依赖别人的库放在前面，被依赖的库放在后面
# =========================================================
target_link_libraries(ffplay_debug PRIVATE
        # --- 1. FFmpeg 核心库 ---
        libavdevice.a
        libavfilter.a
        libavformat.a
        libavcodec.a
        libswscale.a
        libswresample.a
        libpostproc.a
        libavutil.a

        # --- 2. 第三方编解码库 ---
        x264
        vpx
        mp3lame
        fdk-aac

        # --- 3. 基础依赖库 ---
        z
        bz2
        lzma
        iconv

        # --- 4. Windows 系统库 (新增 strmiids 和 mfuuid) ---
        vfw32
        ws2_32
        bcrypt
        secur32
        iphlpapi
        shlwapi
        winmm
        gdi32
        user32
        shell32
        ole32
        oleaut32
        advapi32
        psapi

        # === 新增这两行 ===
        strmiids   # 解决 IID_IAMTVAudio
        mfuuid     # 解决 IID_ICodecAPI

        # --- 5. SDL2 ---
        mingw32
        SDL2main
        SDL2
)