#pragma once
// ============================================================
// State.h — 全局运行态（注入 DLL 生命周期）
// ============================================================
#include <atomic>

namespace YYState
{
    extern std::atomic<bool> ExitRequested;   // END 键 / 卸载请求
    extern std::atomic<bool> GameQuitting;    // WndProc 探测到游戏退出

    void RequestExit();                       // 触发自卸载流程
}
