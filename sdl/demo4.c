//
// Created by jwd on 2025/11/1.
//
#include <SDL.h>
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#define sleep(x) Sleep(x*1000)
#else
#include <unistd.h>
#endif

SDL_mutex *lock = NULL;
SDL_cond  *cond = NULL;

int turn = 0; // 0 表示该 A 打印，1 表示该 B 打印
int stop = 0; // 控制结束循环

int thread_A(void *arg)
{
    for (int i = 0; i < 10; i++) {
        SDL_LockMutex(lock);
        // 如果不是自己的回合，就等待
        while (turn != 0)
            SDL_CondWait(cond, lock);

        printf("A thread print A\n");

        // 修改标志，轮到B线程
        turn = 1;
        SDL_CondSignal(cond); // 唤醒B线程
        SDL_UnlockMutex(lock);

        sleep(1);
    }
    return 0;
}

int thread_B(void *arg)
{
    for (int i = 0; i < 10; i++) {
        SDL_LockMutex(lock);
        // 如果不是自己的回合，就等待
        while (turn != 1)
            SDL_CondWait(cond, lock);

        printf("B thread print B\n");

        // 修改标志，轮到A线程
        turn = 0;
        SDL_CondSignal(cond); // 唤醒A线程
        SDL_UnlockMutex(lock);

        sleep(1);
    }
    return 0;
}

#undef main
int main()
{
    setbuf(stdout,NULL);
    lock = SDL_CreateMutex();
    cond = SDL_CreateCond();

    SDL_Thread *tA = SDL_CreateThread(thread_A, "thread_A", NULL);
    SDL_Thread *tB = SDL_CreateThread(thread_B, "thread_B", NULL);

    SDL_WaitThread(tA, NULL);
    SDL_WaitThread(tB, NULL);

    SDL_DestroyMutex(lock);
    SDL_DestroyCond(cond);
    return 0;
}
