#pragma once
// ============================================================
// AppUI.h — 悬浮窗主界面（商城 / 谱面详情 / 设置）
//   全部运行在渲染线程；与库/游戏桥通过快照与队列交互。
// ============================================================
namespace AppUI
{
    // worker 线程一次性初始化（Library::Init 等）
    void InitBackend();

    // 渲染线程每帧：状态同步、封面装载节流（Draw 之前）
    void TickFrame();

    // 渲染线程：绘制界面（visible=false 时不画）
    void Draw();
    void SetVisible(bool v);

    // 当前是否在原生游玩中（供 RenderHook 自动隐藏）
    bool IsInGameplay();
}
