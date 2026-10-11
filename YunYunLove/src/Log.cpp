#include "Log.h"
#include <windows.h>
#include <string>
#include <mutex>

namespace Log
{
    static std::mutex s_mx;
    static FILE* s_fp = nullptr;
    static std::string s_path;

    void Init()
    {
        std::lock_guard<std::mutex> lk(s_mx);
        if (s_fp)
            return;
        char base[MAX_PATH] = {};
        if (!GetEnvironmentVariableA("LOCALAPPDATA", base, MAX_PATH))
            GetTempPathA(MAX_PATH, base);
        s_path = std::string(base) + "\\YunYunLove";
        CreateDirectoryA(s_path.c_str(), nullptr);
        s_path += "\\log.txt";
        s_fp = fopen(s_path.c_str(), "wb");
    }

    const char* FilePath() { return s_path.c_str(); }

    void Printf(const char* fmt, ...)
    {
        std::lock_guard<std::mutex> lk(s_mx);
        if (!s_fp)
            return;
        SYSTEMTIME st;
        GetLocalTime(&st);
        fprintf(s_fp, "[%02u:%02u:%02u.%03u] ",
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        va_list ap;
        va_start(ap, fmt);
        vfprintf(s_fp, fmt, ap);
        va_end(ap);
        fputc('\n', s_fp);
        fflush(s_fp);
    }
}
