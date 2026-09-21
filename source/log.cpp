#ifdef ENABLE_DEBUG_LOG

#include "log.h"
#include <3ds.h>
#include <sys/stat.h>
#include <cstring>
#include <ctime>

// 日志目录（sdmc: 前缀，CIA / 3dsx 通用）
static const char* LOG_DIR = "sdmc:/3dbili/log";

static FILE* s_logFile = nullptr;

// 内部：确保目录存在
static void EnsureDir(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        mkdir(path, 0777);
    }
}

bool Log_Init() {
    // 建两级目录 sdmc:/3dbili 与 sdmc:/3dbili/log
    EnsureDir("sdmc:/3dbili");
    EnsureDir(LOG_DIR);

    // 以启动时间戳命名日志文件
    time_t t = time(nullptr);
    struct tm* tm_info = localtime(&t);
    char fname[128];
    strftime(fname, sizeof(fname), "sdmc:/3dbili/log/%Y%m%d_%H%M%S.log", tm_info);

    s_logFile = fopen(fname, "w");
    if (!s_logFile) {
        // 退回到固定文件名
        s_logFile = fopen("sdmc:/3dbili/log/latest.log", "w");
    }
    if (!s_logFile) return false;

    fprintf(s_logFile, "=== 3dbili debug log ===\n");
    fflush(s_logFile);
    return true;
}

void Log_Close() {
    if (s_logFile) {
        fprintf(s_logFile, "=== end ===\n");
        fclose(s_logFile);
        s_logFile = nullptr;
    }
}

void Log_Printf(const char* fmt, ...) {
    if (!s_logFile) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(s_logFile, fmt, ap);
    va_end(ap);
    fflush(s_logFile);
}

#endif // ENABLE_DEBUG_LOG