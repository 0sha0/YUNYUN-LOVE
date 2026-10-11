#pragma once
// ============================================================
// GameBridge.h — 云云症游戏侧桥（类解析 / 自定义谱面进游戏 / 状态读取）
//   mono 调用全部发生在游戏主线程（MainThread::Pump 驱动）。
// ============================================================
#include <string>
#include <atomic>

namespace GameBridge
{
    struct PlayRequest
    {
        std::wstring audioPath;    // 绝对路径（ogg/wav/mp3）
        std::string  scoreJson;    // YYNoteEditor.ScoreData 的 JsonUtility JSON
        std::string  title;        // 显示标题（utf8）
        std::string  artist;       // 艺术家（utf8）
        std::string  chartLabel;   // 谱面难度标签
    };

    // worker 线程：等待并解析 Mono 导出（不触碰托管世界）
    bool InitNative();

    // 主线程 Tick（每帧由 WndProc 泵驱动）：
    //   类解析 → 播放请求处理 → 音频加载轮询 → 入场后标题修正 → 状态发布
    void TickMainThread();

    // 任意线程：请求开始游玩（排队到主线程执行）
    void RequestPlay(const PlayRequest& req);

    // 类是否就绪（UI 可显示状态）
    bool Ready();

    // 当前是否在原生游玩界面（主线程 Tick 发布）
    bool InGameplay();

    // 最近一次错误（utf8，诊断用）
    std::string LastError();

    // UI 侧主动记录失败（谱面读取/解析错误等）
    void SetExternalError(const std::string& msg);
}
