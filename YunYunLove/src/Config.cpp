#include "Config.h"
#include <windows.h>
#include <cstdio>
#include <mutex>
#include <string>

namespace Config
{
    static std::wstring s_path;
    static std::mutex s_mx;
    static double s_leadIn = 0.0;
    static bool s_autoHide = true;
    static float s_uiScale = 1.0f;
    static float s_winW = 0.f;   // <=0 = 用 AppUI 内置默认
    static float s_winH = 0.f;

    static double ClampD(double v, double lo, double hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    double LeadInSec() { std::lock_guard<std::mutex> lk(s_mx); return s_leadIn; }
    void SetLeadInSec(double v)
    {
        std::lock_guard<std::mutex> lk(s_mx);
        s_leadIn = ClampD(v, -0.3, 0.3);
        Save();
    }

    bool AutoHideInGame() { std::lock_guard<std::mutex> lk(s_mx); return s_autoHide; }
    void SetAutoHideInGame(bool v)
    {
        std::lock_guard<std::mutex> lk(s_mx);
        s_autoHide = v;
        Save();
    }

    float UiScale() { std::lock_guard<std::mutex> lk(s_mx); return s_uiScale; }
    void SetUiScale(float v)
    {
        std::lock_guard<std::mutex> lk(s_mx);
        s_uiScale = v < 0.75f ? 0.75f : (v > 1.5f ? 1.5f : v);
        Save();
    }

    void WindowSize(float* w, float* h)
    {
        std::lock_guard<std::mutex> lk(s_mx);
        if (w) *w = s_winW;
        if (h) *h = s_winH;
    }
    void SetWindowSize(float w, float h)
    {
        std::lock_guard<std::mutex> lk(s_mx);
        s_winW = ClampD(w, 720.0, 4000.0);
        s_winH = ClampD(h, 440.0, 4000.0);
        Save();
    }

    void Load(const wchar_t* path)
    {
        std::lock_guard<std::mutex> lk(s_mx);
        s_path = path;
        FILE* f = _wfopen(path, L"rb");
        if (!f)
            return;
        std::string j;
        char buf[1024];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0)
            j.append(buf, n);
        fclose(f);

        auto num = [&](const char* key, double* dst) {
            size_t k = j.find(key);
            if (k == std::string::npos) return;
            k = j.find(':', k);
            if (k == std::string::npos) return;
            *dst = strtod(j.c_str() + k + 1, nullptr);
        };
        num("LeadInSec", &s_leadIn);
        s_leadIn = ClampD(s_leadIn, -0.3, 0.3);
        double scale = s_uiScale;
        num("UiScale", &scale);
        s_uiScale = (float)(scale < 0.75 ? 0.75 : (scale > 1.5 ? 1.5 : scale));
        double ww = s_winW, wh = s_winH;
        num("WindowW", &ww);
        num("WindowH", &wh);
        s_winW = (ww > 0) ? (float)ClampD(ww, 720.0, 4000.0) : 0.f;
        s_winH = (wh > 0) ? (float)ClampD(wh, 440.0, 4000.0) : 0.f;
        {
            size_t k = j.find("AutoHideInGame");
            if (k != std::string::npos)
            {
                size_t v = j.find("true", k);
                size_t f2 = j.find("false", k);
                if (v != std::string::npos && (f2 == std::string::npos || v < f2))
                    s_autoHide = true;
                else if (f2 != std::string::npos)
                    s_autoHide = false;
            }
        }
    }

    void Save()
    {
        // 调用方持锁（SetXxx 均持锁）；Load 阶段 s_path 为空则跳过
        if (s_path.empty())
            return;
        FILE* f = _wfopen(s_path.c_str(), L"wb");
        if (!f)
            return;
        fprintf(f, "{\n \"LeadInSec\": %.3f,\n \"AutoHideInGame\": %s,\n \"UiScale\": %.2f,\n"
                   " \"WindowW\": %.1f,\n \"WindowH\": %.1f\n}\n",
                s_leadIn, s_autoHide ? "true" : "false", s_uiScale, s_winW, s_winH);
        fclose(f);
    }
}
