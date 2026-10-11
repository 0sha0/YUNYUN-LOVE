#include "OsuChart.h"
#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

namespace OsuChart
{
    namespace
    {
        std::string Trim(const std::string& s)
        {
            size_t a = 0, b = s.size();
            while (a < b && (unsigned char)s[a] <= ' ') a++;
            while (b > a && (unsigned char)s[b - 1] <= ' ') b--;
            return s.substr(a, b - a);
        }

        std::vector<std::string> Split(const std::string& s, char sep)
        {
            std::vector<std::string> v;
            size_t start = 0;
            for (size_t i = 0; i <= s.size(); i++)
            {
                if (i == s.size() || s[i] == sep)
                {
                    v.push_back(s.substr(start, i - start));
                    start = i + 1;
                }
            }
            return v;
        }

        double ToD(const std::string& s) { return atof(Trim(s).c_str()); }
        int    ToI(const std::string& s) { return atoi(Trim(s).c_str()); }

        bool KeyVal(const std::string& line, std::string* key, std::string* val)
        {
            size_t c = line.find(':');
            if (c == std::string::npos)
                return false;
            *key = Trim(line.substr(0, c));
            *val = Trim(line.substr(c + 1));
            return true;
        }
    }

    bool Looks(const std::string& text)
    {
        return text.size() >= 15 && text.compare(0, 15, "osu file format") == 0;
    }

    bool Parse(const std::string& text, MalodyChart::Chart* out, std::string* error)
    {
        if (!out)
            return false;
        *out = MalodyChart::Chart();

        struct TP { double ms = 0.0; double bpm = 120.0; };
        std::vector<TP> tps;

        enum Sec { S_None, S_General, S_Meta, S_Diff, S_Events, S_Timing, S_Hit };
        Sec sec = S_None;

        int    mode = -1;
        int    keys = 0;
        double previewMs = 0.0;
        std::string audio, bg, title, titleU, artist, creator, version;
        std::vector<std::string> hitLines;

        size_t pos = 0;
        while (pos <= text.size())
        {
            size_t nl = text.find('\n', pos);
            size_t end = (nl == std::string::npos) ? text.size() : nl;
            std::string line = text.substr(pos, end - pos);
            pos = (nl == std::string::npos) ? text.size() + 1 : nl + 1;
            if (!line.empty() && line.back() == '\r')
                line.pop_back();

            std::string t = Trim(line);
            if (t.empty())
                continue;
            if (t[0] == '/' && t.size() > 1 && t[1] == '/')
                continue;                              // 注释
            if (t[0] == '[' && t.back() == ']')
            {
                std::string h = t.substr(1, t.size() - 2);
                sec = S_None;
                if (h == "General")          sec = S_General;
                else if (h == "Metadata")    sec = S_Meta;
                else if (h == "Difficulty")  sec = S_Diff;
                else if (h == "Events")      sec = S_Events;
                else if (h == "TimingPoints") sec = S_Timing;
                else if (h == "HitObjects")  sec = S_Hit;
                continue;
            }

            std::string k, v;
            switch (sec)
            {
            case S_General:
                if (!KeyVal(t, &k, &v)) break;
                if (k == "AudioFilename")      audio = v;
                else if (k == "Mode")          mode = ToI(v);
                else if (k == "PreviewTime")   previewMs = ToD(v);
                break;
            case S_Meta:
                if (!KeyVal(t, &k, &v)) break;
                if (k == "Title")              title = v;
                else if (k == "TitleUnicode")  titleU = v;
                else if (k == "Artist")        artist = v;
                else if (k == "Creator")       creator = v;
                else if (k == "Version")       version = v;
                break;
            case S_Diff:
                if (!KeyVal(t, &k, &v)) break;
                if (k == "CircleSize")         keys = (int)(ToD(v) + 0.5);
                break;
            case S_Events:
                // 背景图：0,0,"bg.jpg",0,0（也有 Background,0,0,"bg.jpg" 的写法）；
                // 后面可能还有 video/storyboard，取第一个即可（背景行永远在最前）。
                if (bg.empty() && (t.compare(0, 2, "0,") == 0 || t.compare(0, 10, "Background") == 0))
                {
                    size_t q1 = t.find('"');
                    if (q1 != std::string::npos)
                    {
                        size_t q2 = t.find('"', q1 + 1);
                        if (q2 != std::string::npos)
                            bg = t.substr(q1 + 1, q2 - q1 - 1);
                    }
                }
                break;
            case S_Timing:
            {
                std::vector<std::string> f = Split(t, ',');
                if (f.size() < 2) break;
                double ms = ToD(f[0]);
                double bl = ToD(f[1]);                     // beatLength（ms/拍）
                // 第 7 个字段 uninherited：1=红线段（改 BPM），0=绿线段（只改 SV）
                bool uninherited = (f.size() >= 7) ? (ToI(f[6]) != 0) : (bl > 0);
                if (!uninherited || !(bl > 0)) break;
                TP p;
                p.ms = ms;
                p.bpm = 60000.0 / bl;
                if (p.bpm > 0)
                    tps.push_back(p);
                break;
            }
            case S_Hit:
                hitLines.push_back(t);
                break;
            default:
                break;
            }
        }

        if (keys <= 0 || keys > 18)
        {
            if (error) *error = "missing/invalid CircleSize";
            return false;
        }
        if (hitLines.empty())
        {
            if (error) *error = "no hitobjects";
            return false;
        }

        std::sort(tps.begin(), tps.end(), [](const TP& a, const TP& b) { return a.ms < b.ms; });

        // ---- 毫秒 → 拍位（音频 0ms = 第 0 拍）----
        double baseBpm = tps.empty() ? 120.0 : tps.front().bpm;
        std::vector<TP> seg;                                // 只保留 ms > 0 的分段点
        for (const TP& p : tps)
        {
            if (p.ms <= 0.0)
                baseBpm = p.bpm;                            // 0ms 之前生效的 BPM
            else
                seg.push_back(p);
        }
        auto BeatAt = [&](double ms) -> double
        {
            double beat = 0.0, prevMs = 0.0, bpm = baseBpm;
            for (const TP& p : seg)
            {
                if (p.ms >= ms)
                    break;
                beat += (p.ms - prevMs) * (bpm / 60000.0);
                prevMs = p.ms;
                bpm = p.bpm;
            }
            beat += (ms - prevMs) * (bpm / 60000.0);
            return beat;
        };

        // ---- BPM 表（拍位 = 该时间点对应的拍）----
        for (const TP& p : tps)
        {
            MalodyChart::BpmPoint bp;
            bp.beat = BeatAt(p.ms);
            bp.bpm = p.bpm;
            out->bpms.push_back(bp);
        }
        std::sort(out->bpms.begin(), out->bpms.end(),
                  [](const MalodyChart::BpmPoint& a, const MalodyChart::BpmPoint& b) { return a.beat < b.beat; });
        out->bpms.erase(std::unique(out->bpms.begin(), out->bpms.end(),
                                    [](const MalodyChart::BpmPoint& a, const MalodyChart::BpmPoint& b)
                                    { return a.beat == b.beat && a.bpm == b.bpm; }),
                        out->bpms.end());
        // 第 0 拍之前没有时间点（第一个红线段在音频之后）时补一个第 0 拍点，
        // 供 BuildScoreDataJson 取 InitBpm。
        if (out->bpms.empty() || out->bpms.front().beat > 0.0)
        {
            MalodyChart::BpmPoint b0;
            b0.beat = 0.0;
            b0.bpm = out->bpms.empty() ? 120.0 : out->bpms.front().bpm;
            out->bpms.insert(out->bpms.begin(), b0);
        }

        // ---- 音符 ----
        for (const std::string& hl : hitLines)
        {
            std::vector<std::string> f = Split(hl, ',');
            if (f.size() < 4)
                continue;
            int type = ToI(f[3]);
            bool hold = (type & 128) != 0;                  // mania 长条
            if (!hold && !(type & 1))
                continue;                                   // 非按键音符
            double ms = ToD(f[2]);

            MalodyChart::ParsedNote pn;
            pn.beat = BeatAt(ms);
            if (pn.beat < 0.0)
                pn.beat = 0.0;

            int x = ToI(f[0]);
            int col = (int)((double)x * (double)keys / 512.0);
            if (col < 0) col = 0;
            if (col > keys - 1) col = keys - 1;
            pn.column = col;

            if (hold && f.size() >= 6)
            {
                const std::string& spec = f[5];             // "endTime:hitSample"
                size_t c = spec.find(':');
                double endMs = (c == std::string::npos) ? ToD(spec) : ToD(spec.substr(0, c));
                pn.endBeat = BeatAt(endMs);
                if (pn.endBeat <= pn.beat)
                    pn.endBeat = -1.0;                      // 退化长条 → 当单点
            }
            out->notes.push_back(pn);
        }

        if (out->notes.empty())
        {
            if (error) *error = "no playable notes";
            return false;
        }

        out->title = title.empty() ? titleU : title;
        out->artist = artist;
        out->creator = creator;
        out->version = version;
        out->background = bg;
        out->bgm = audio;
        out->bgmBeat = 0.0;
        out->bgmOffsetMs = 0.0;
        if (!audio.empty())
            out->soundCandidates.push_back(audio);

        // 只有 osu!mania 记为 Malody 的 KEY(0)，其余模式记 1 → 被 Is4K 过滤
        out->mode = (mode == 3) ? 0 : 1;
        out->column = keys;
        out->previewMs = previewMs;
        out->is4K = MalodyChart::Is4K(*out);
        return true;
    }
}