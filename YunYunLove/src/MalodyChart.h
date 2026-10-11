#pragma once
// ============================================================
// MalodyChart.h — Malody .mc 谱面解析（4K Key 模式）→ 云云症 ScoreData JSON
//   .mc 为 JSON：meta{mode=0(KEY), mode_ext.column=4, song{title,artist}, background}
//               time[{beat:[整拍,分子,分母], bpm}]  note[{beat, column, endbeat?, type, sound}]
//   拍位 = 整拍 + 分子/分母（全库实测校准，见 MalodyChart.cpp：BeatOf）。
//   时间 = 按 BPM 表逐段积分；type=1 音符的 offset 表示音频相对谱面的播放偏移。
// ============================================================
#include <string>
#include <vector>
#include <cstdint>

namespace MalodyChart
{
    struct ParsedNote
    {
        double beat = 0.0;      // 绝对拍位
        double endBeat = -1.0;  // 长条结束拍位（<0 = 非长条）
        int column = 0;         // 0..3
    };

    struct BpmPoint
    {
        double beat = 0.0;
        double bpm = 120.0;
    };

    struct Chart
    {
        std::string title;
        std::string artist;
        std::string creator;
        std::string version;        // "4K Easy Lv.7"
        std::string background;     // 封面文件名（zip 内）
        std::string bgm;            // BGM 音频文件名（zip 内）
        double bgmBeat = 0.0;       // BGM 起始拍位（type=1 音轨注释所在拍）
        double bgmOffsetMs = 0.0;   // BGM 起始偏移（毫秒，type=1 音符 offset 字段）
        std::vector<std::string> soundCandidates;  // 所有 type=1 声音（BGM 兜底用）
        int column = 0;             // mode_ext.column
        int mode = -1;              // 0 = Key
        double previewMs = 0.0;
        bool is4K = false;
        std::vector<BpmPoint> bpms;
        std::vector<ParsedNote> notes;   // 仅 type=0 按键音符

        // 派生
        double LengthSec() const;   // 谱面末尾时间（秒）
        int NoteCount() const { return (int)notes.size(); }
    };

    // 解析 .mc 文本；失败返回 false 并填 error
    bool Parse(const std::string& jsonText, Chart* out, std::string* error);

    // 统一解析入口：按"内容"自动分派 Malody .mc(JSON) 与 osu! .osu(纯文本)，
    // 并顺带去掉 UTF-8 BOM。扩展名不可靠（4K dan 包两种格式混用），故一律嗅探。
    bool ParseAny(const std::string& text, Chart* out, std::string* error);

    // 4K Key 模式判定（mode==0 && column==4）
    bool Is4K(const Chart& c);

    // 拍位 → 秒（按 BPM 表逐段积分；不依赖每小节拍数）
    double BeatToSec(const std::vector<BpmPoint>& bpms, double beat);

    // 生成云云症 YYNoteEditor.ScoreData 的 JsonUtility JSON。
    //   leadIn: 谱面同步微调（秒，作为 ScoreOffset；负=音符提前，正=音符延后）
    std::string BuildScoreDataJson(const Chart& c, double leadIn);
}
