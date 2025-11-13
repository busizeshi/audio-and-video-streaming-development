#include <SDL2/SDL.h>
#include <SDL2/SDL_thread.h>
#include <cstdio>

// --- 配置常量 ---
const int VIDEO_WIDTH = 720;
const int VIDEO_HEIGHT = 480;
const char* YUV_FILE_PATH = R"(D:\cxx\resource\720x480_25fps_420p.yuv)";
// YUV 420P 一帧的大小 = 宽 * 高 * 1.5
const int FRAME_SIZE = VIDEO_WIDTH * VIDEO_HEIGHT * 3 / 2;

// --- 全局共享资源 ---
// 1. 共享的缓冲区，用于存放一帧YUV数据
static uint8_t* g_video_buf = nullptr;
// 2. 保护 g_video_buf 的互斥锁
static SDL_mutex* g_mutex = nullptr;
// 3. 线程退出标志
static int g_quit = 0;

/**
 * @brief 读取线程 (Thread A: 生产者)
 *
 * 这个函数在一个单独的线程中运行。
 * 它的唯一工作就是循环地从YUV文件中读取一帧数据，
 * 然后安全地将这帧数据放入全局的 g_video_buf。
 */
int reader_thread(void* opaque) {
    FILE* file = nullptr;
    // 临时缓冲区，用于从文件读取
    auto* local_buf = (uint8_t*)malloc(FRAME_SIZE);
    if (!local_buf) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ReaderThread: Failed to allocate local_buf");
        return -1;
    }

    // 打开YUV文件
    file = fopen(YUV_FILE_PATH, "rb");
    if (!file) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to open YUV file: %s", YUV_FILE_PATH);
        free(local_buf);
        return -1;
    }

    SDL_Log("ReaderThread: Started...");

    while (!g_quit) {
        // 1. 从文件读取一帧数据到 local_buf
        size_t bytes_read = fread(local_buf, 1, FRAME_SIZE, file);

        if (bytes_read < FRAME_SIZE) {
            // 文件读到末尾，重置文件指针以循环播放
            fseek(file, 0, SEEK_SET);
            continue;
        }

        // 2.【关键】锁定互斥锁，准备写入共享缓冲区
        // 如果此时主线程(B)正持有锁(在读取)，这里会阻塞等待
        SDL_LockMutex(g_mutex);

        // 3. --- 临界区开始 ---
        // 现在我们是安全的，可以安全地写入 g_video_buf
        memcpy(g_video_buf, local_buf, FRAME_SIZE);
        // --- 临界区结束 ---

        // 4.【关键】解锁互斥锁，允许其他线程访问
        SDL_UnlockMutex(g_mutex);

        // 模拟帧率，比如 25fps (1000ms / 25 = 40ms)
        SDL_Delay(40);
    }

    // 清理
    SDL_Log("ReaderThread: Quitting...");
    free(local_buf);
    fclose(file);
    return 0;
}

/**
 * @brief 主函数 (同时也是渲染线程, Thread B: 消费者)
 */
int main(int argc, char* argv[]) {

    // 1. 初始化SDL
    if (SDL_Init(SDL_INIT_VIDEO)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not initialize SDL: %s", SDL_GetError());
        return -1;
    }

    // 2. 创建窗口
    SDL_Window* window = SDL_CreateWindow(
            "YUV Mutex Example",
            SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED,
            VIDEO_WIDTH, VIDEO_HEIGHT,
            SDL_WINDOW_RESIZABLE
    );
    if (!window) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not create window: %s", SDL_GetError());
        return -1;
    }

    // 3. 创建渲染器
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not create renderer: %s", SDL_GetError());
        return -1;
    }

    // 4. 创建纹理
    // YUV420P 在SDL中对应的格式是 IYUV (或 YV12)
    SDL_Texture* texture = SDL_CreateTexture(
            renderer,
            SDL_PIXELFORMAT_IYUV, // IYUV (I420)
            SDL_TEXTUREACCESS_STREAMING, // 表示我们会频繁更新它
            VIDEO_WIDTH,
            VIDEO_HEIGHT
    );
    if (!texture) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not create texture: %s", SDL_GetError());
        return -1;
    }

    // 5. 分配共享缓冲区
    g_video_buf = (uint8_t*)malloc(FRAME_SIZE);
    if (!g_video_buf) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to allocate g_video_buf");
        return -1;
    }

    // 6.创建互斥锁
    g_mutex = SDL_CreateMutex();
    if (!g_mutex) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create mutex: %s", SDL_GetError());
        return -1;
    }

    // 7. 启动读取线程 (Thread A)
    SDL_Thread* thread_id = SDL_CreateThread(reader_thread, "reader_thread", nullptr);
    if (!thread_id) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create reader thread: %s", SDL_GetError());
        return -1;
    }

    SDL_Log("MainThread: Running render loop...");

    // 8. 主循环 (渲染线程, Thread B)
    SDL_Event event;
    while (!g_quit) {
        // 处理退出事件
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                g_quit = 1;
            }
        }

        // 9.锁定互斥锁，准备读取共享缓冲区
        // 如果此时读取线程(A)正持有锁(在写入)，这里会阻塞等待
        SDL_LockMutex(g_mutex);

        // 10. --- 临界区开始 ---
        // 安全地从 g_video_buf 更新纹理
        // SDL_UpdateYUVTexture 会从 g_video_buf 读取 Y, U, V 数据
        SDL_UpdateYUVTexture(
                texture,
                nullptr, // 更新整个纹理
                g_video_buf,                                 // Y平面 (W * H)
                VIDEO_WIDTH,
                g_video_buf + VIDEO_WIDTH * VIDEO_HEIGHT,    // U平面 (W/2 * H/2)
                VIDEO_WIDTH / 2,
                g_video_buf + VIDEO_WIDTH * VIDEO_HEIGHT * 5 / 4, // V平面 (W/2 * H/2)
                VIDEO_WIDTH / 2
        );
        // --- 临界区结束 ---

        // 11.解锁互斥锁
        SDL_UnlockMutex(g_mutex);

        // 12. 正常渲染
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr); // 将纹理复制到渲染器
        SDL_RenderPresent(renderer); // 显示

        // 模拟渲染帧率，比如 30fps (1000ms / 30 = ~33ms)
        SDL_Delay(33);
    }

    // 13. 等待读取线程(A)结束
    SDL_Log("MainThread: Waiting for reader thread to join...");
    SDL_WaitThread(thread_id, nullptr);

    // 14. 清理所有资源
    SDL_Log("MainThread: Cleaning up...");
    free(g_video_buf);
    SDL_DestroyMutex(g_mutex); // 销毁锁
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}