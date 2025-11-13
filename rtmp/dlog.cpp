#include "dlog.h"

#include <cstdio>
#include <ctime>
#include <cstdarg>
#include <cstdint>
#if defined(WIN32)
#include <io.h>
#include <direct.h>
#include <Windows.h>
#elif defined(linux)
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <execinfo.h>
#endif

#define MAX_DATE_STR                (10)
#define DATE_STR_FMT                "%04d%02d%02d"
#define MAX_TIME_STR                (20)
#define TIME_STR_FMT                "%04d/%02d/%02d %02d:%02d:%02d"
#define MAX_FILE_PATH               (260)
#define MAX_LOG_LINE                (4096)

#define MAX_DEEP                    (24)
#define MAX_ST_LINE                 (512)

#if defined(WIN32)
#define snprintf _snprintf
#define vsnprintf _vsnprintf
#define PROC_HANDLE HANDLE
#define SLOG_MUTEX CRITICAL_SECTION
#elif defined(linux)
#define PROC_HANDLE void *
#define SLOG_MUTEX pthread_mutex_t
#endif

typedef struct logger_cfg
{
    PROC_HANDLE curr_proc;
    FILE* log_file;
    SLOG_MUTEX mtx;
    slog_level filter_level;
    int inited;
} logger_cfg;

static logger_cfg g_logger_cfg = {
    nullptr, nullptr, {nullptr}, S_INFO, FALSE
};

static void slog_init_mutex(SLOG_MUTEX* mtx)
{
#if defined(WIN32)
    InitializeCriticalSection(mtx);
#elif defined(linux)
    pthread_mutex_init(mtx, NULL);
#endif
}

static void slog_lock(SLOG_MUTEX* mtx)
{
#if defined(WIN32)
    EnterCriticalSection(mtx);
#elif defined(linux)
    pthread_mutex_lock(mtx);
#endif
}

static void slog_unlock(SLOG_MUTEX* mtx)
{
#if defined(WIN32)
    LeaveCriticalSection(mtx);
#elif defined(linux)
    pthread_mutex_unlock(mtx);
#endif
}

static void get_curr_date(char datestr[])
{
    time_t tt = {0};
    struct tm* curr_time = nullptr;

    time(&tt);
    curr_time = localtime(&tt);
    snprintf(datestr, 10 - 1, DATE_STR_FMT,
             curr_time->tm_year + 1900, curr_time->tm_mon + 1, curr_time->tm_mday);
}

static void get_curr_time(char timestr[])
{
    time_t tt = {0};
    struct tm* curr_time = nullptr;

    time(&tt);
    curr_time = localtime(&tt);
    snprintf(timestr, 20 - 1, TIME_STR_FMT,
             curr_time->tm_year + 1900, curr_time->tm_mon + 1, curr_time->tm_mday,
             curr_time->tm_hour, curr_time->tm_min, curr_time->tm_sec);
}

static inline char* get_level_str(slog_level level)
{
    switch (level)
    {
    case S_TRACE:
        return (char*)"[TRACE]";
    case S_DEBUG:
        return (char*)"[DEBUG]";
    case S_INFO:
        return (char*)"[INFO ]";
    case S_WARN:
        return (char*)"[WARN ]";
    case S_ERROR:
        return (char*)"[ERROR]";
    default:
        return (char*)"[     ]";
    }
}

static void write_stacktrace()
{
    unsigned int i = 0;
    unsigned short frames = 0;
    void* stack[MAX_DEEP] = {0};
    char st_line[MAX_ST_LINE] = {0};

#if defined(WIN32)

#elif defined(linux)
    char** st_arr = NULL;

    frames = backtrace(stack, MAX_DEEP);
    st_arr = backtrace_symbols(stack, frames);
    for (i = 0; i < frames; ++i)
    {
        snprintf(st_line, sizeof(st_line) - 1, "    %d: %s\n", frames - i - 1, st_arr[i]);
        fwrite(st_line, sizeof(char), strlen(st_line), g_logger_cfg.log_file);
    }
    free(st_arr);
#endif
}

static int slog_mkdir(const char* log_dir)
{
#if defined(WIN32)
    if (mkdir(log_dir) != 0)
    {
        return FALSE;
    }
#elif defined(linux)
    if (mkdir(log_dir, 0744) != 0)
    {
        return FALSE;
    }
#endif
    return TRUE;
}

static int get_curr_proc_handle()
{
#if defined(WIN32)

    g_logger_cfg.curr_proc = nullptr;
#elif defined(linux)
    g_logger_cfg.curr_proc = NULL;
#endif
    return TRUE;
}

int init_logger(const char* log_dir, slog_level level)
{
    char log_filepath[MAX_FILE_PATH] = {0};
    char datestr[MAX_DATE_STR] = {0};

    if (TRUE == g_logger_cfg.inited)
    {
        return TRUE;
    }

    if (access(log_dir, 0) != 0)
    {
        if (slog_mkdir(log_dir) != TRUE)
        {
            return FALSE;
        }
    }

    slog_init_mutex(&g_logger_cfg.mtx);

    get_curr_proc_handle();

    get_curr_date(datestr);
    snprintf(log_filepath, sizeof(log_filepath) - 1, "%s/%s.log", log_dir, datestr);
    g_logger_cfg.log_file = fopen(log_filepath, "w+");
    if (nullptr == g_logger_cfg.log_file)
    {
        return FALSE;
    }

    g_logger_cfg.filter_level = level;
    g_logger_cfg.inited = TRUE;

    return TRUE;
}

static inline int64_t getTimeMillisecond()
{
#ifdef _WIN32
    return (int64_t)GetTickCount();
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((int64_t)tv.tv_sec * 1000 + (unsigned long long)tv.tv_usec / 1000);
#endif
}

void write_log(slog_level level, int print_stacktrace, const char* func_name, int line, const char* fmt, ...)
{
    va_list args;
    char* level_str = nullptr;
    char timestr[MAX_TIME_STR] = {0};
    char log_content[MAX_LOG_LINE] = {0};
    char log_line[MAX_LOG_LINE] = {0};

    if (g_logger_cfg.filter_level > level)
    {
        return;
    }
    va_start(args, fmt);
    vsnprintf(log_content, sizeof(log_content) - 1, fmt, args);
    va_end(args);
    get_curr_time(timestr);
    level_str = get_level_str(level);
    int64_t cur_time = getTimeMillisecond();
    snprintf(log_line, sizeof(log_line) - 1, "[%s %s-%d %s:%d] %s\n",
             level_str, timestr, int(cur_time % 1000), func_name, line, log_content);
    fwrite(log_line, sizeof(char), strlen(log_line), g_logger_cfg.log_file);

    fflush(g_logger_cfg.log_file);
    printf("%s", log_line); // 先打印到终端再说
}
