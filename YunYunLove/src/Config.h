#pragma once
// ============================================================
// Config.h — 工具设置持久化（<游戏目录>\YunYunLove\config.json）
// ============================================================
namespace Config
{
    // 谱面同步微调（秒，-0.3 ~ 0.3）：负 = 音符提前，正 = 音符延后。
    // 0 = 与音乐严格同步（默认）；改变后下一次游玩生效
    double LeadInSec();
    void   SetLeadInSec(double v);

    // 进入原生游玩界面时自动隐藏悬浮窗
    bool AutoHideInGame();
    void SetAutoHideInGame(bool v);

    // UI 缩放（0.75 ~ 1.5）
    float UiScale();
    void  SetUiScale(float v);

    // 悬浮窗尺寸（UiScale=1.0 下的逻辑像素；由用户拖边框改变，<=0 = 用内置默认）
    void WindowSize(float* w, float* h);
    void SetWindowSize(float w, float h);

    void Load(const wchar_t* path);   // worker 线程 Init 时调用
    void Save();
}
