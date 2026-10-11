#pragma once
// ============================================================
// OsuChart.h — osu! 谱面解析（osu!mania）→ 与 MalodyChart::Chart 同一中间表示
//
//   为什么需要它：网上流传的 4K "dan" 包很多是 osu! beatmapset（.osu），
//   不是 Malody 的 .mc。两者最终都编译成云云症原生 ScoreData，所以导入端
//   只要把 .osu 也归一到 MalodyChart::Chart 即可复用整条管线。
//
//   过滤规则：只在 Mode == 3（mania）时把 mode 记为 0，其余规则模式一律记为
//   非 0 → 由 MalodyChart::Is4K（mode==0 && column==4）自动过滤掉非 4K 谱面。
//
//   时间轴：osu 的 [TimingPoints] 全部以毫秒给出。这里把"音频 0 毫秒"定义为
//   第 0 拍（bgmBeat = 0），拍位按每个 uninherited 时间点分段积分得到。
//   于是 BuildScoreDataJson 里 shift=0、tick = beat*480，与 osu 的毫秒时间轴
//   严格一一对应（不引入任何偏移假设）。
// ============================================================
#include <string>
#include "MalodyChart.h"

namespace OsuChart
{
    // 内容嗅探：文本是否为 osu! 谱面（首行 "osu file format vN"）
    bool Looks(const std::string& text);

    // 解析 osu! 谱面文本；失败返回 false 并填 error
    bool Parse(const std::string& text, MalodyChart::Chart* out, std::string* error);
}