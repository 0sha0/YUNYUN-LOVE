#pragma once
// ============================================================
// Log.h — 运行日志（写 %LOCALAPPDATA%\YunYunLove\log.txt）
// ============================================================
#include <cstdio>
#include <cstdarg>

namespace Log
{
    void Init();
    void Printf(const char* fmt, ...);
    const char* FilePath();
}
