#pragma once
// ============================================================
// MainThread.h — 主线程任务泵。
// Unity 6 的 Mono 是协作式 GC：任何 mono API 只能在托管主线程调用。
// 本模块把 lambda 投递到游戏主线程（经渲染钩子的 WndProc 消息驱动）执行。
// ============================================================
#include <functional>

namespace MainThread
{
    // 由渲染钩子注册：向游戏主窗口 PostMessage 的回调（线程安全）
    void SetPoster(void (*fn)());

    // 任意线程调用：把任务排队到主线程执行。若 poster 未就绪则任务保留直到就绪。
    void Request(std::function<void()> fn);

    // 仅主线程调用（WndProc 收到泵消息）：attach mono 并排空队列
    void Pump();

    // 当前线程是否为 Unity 主线程（ Pump 所在线程）
    bool OnMainThread();

    // 渲染线程每帧调用：主线程最近一次心跳（毫秒 tick）
    unsigned long LastPumpTick();

    // 游戏退出中（WndProc 检测到 WM_CLOSE 等）：不再投递任何任务
    void SetGameQuitting();
    bool GameQuitting();
}
