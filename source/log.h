#pragma once

// 轻量日志模块
// 作用：
//   - debug 编译（Makefile 传 DEBUG=1，定义 ENABLE_DEBUG_LOG）时
//     日志写入 sdmc:/3dbili/log/YYYYMMDD_HHMMSS.log
//   - stable 编译时所有调用被宏展开为空，零开销
//
// 使用方式：
//   LOG_INIT();
//   LOGF("value=%d\n", 42);
//   LOG_CLOSE();

#ifdef ENABLE_DEBUG_LOG

#include <cstdio>
#include <cstdarg>

bool Log_Init();
void Log_Close();
void Log_Printf(const char* fmt, ...);

#define LOG_INIT()   Log_Init()
#define LOG_CLOSE()  Log_Close()
#define LOGF(...)    Log_Printf(__VA_ARGS__)

#else

#define LOG_INIT()   ((void)0)
#define LOG_CLOSE()  ((void)0)
#define LOGF(...)    ((void)0)

#endif