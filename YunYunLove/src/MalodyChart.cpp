#include "MalodyChart.h"
#include "OsuChart.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <map>
#include <windows.h>

// ============================================================
// 内嵌 JSON 解析器（只读；对象保序，字符串处理 \u 转义）
// ============================================================
namespace
{
    struct JValue;
    using JObjectMap = std::vector<std::pair<std::string, JValue*>>;
    using JArray = std::vector<JValue*>;

    struct JValue
    {
        enum Kind { Null, Bool, Num, Str, Arr, Obj } kind = Null;
        bool b = false;
        double num = 0;
        std::string str;
        JArray arr;
        JObjectMap obj;

        const JValue* Find(const char* key) const
        {
            for (auto& kv : obj)
                if (kv.first == key)
                    return kv.second;
            return nullptr;
        }
    };

    struct JParser
    {
        const char* p = nullptr;
        const char* end = nullptr;
        std::string err;

        void SkipWs()
        {
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
                p++;
        }

        bool Fail(const char* msg)
        {
            if (err.empty())
                err = msg;
            return false;
        }

        bool ParseValue(JValue** out)
        {
            SkipWs();
            if (p >= end)
                return Fail("unexpected end");
            char c = *p;
            if (c == '{') return ParseObj(out);
            if (c == '[') return ParseArr(out);
            if (c == '"')
            {
                JValue* v = new JValue();
                v->kind = JValue::Str;
                if (!ParseString(v->str)) { delete v; return false; }
                *out = v;
                return true;
            }
            if (c == 't' || c == 'f')
            {
                JValue* v = new JValue();
                v->kind = JValue::Bool;
                if (end - p >= 4 && strncmp(p, "true", 4) == 0) { v->b = true; p += 4; }
                else if (end - p >= 5 && strncmp(p, "false", 5) == 0) { v->b = false; p += 5; }
                else { delete v; return Fail("bad literal"); }
                *out = v;
                return true;
            }
            if (c == 'n')
            {
                if (end - p >= 4 && strncmp(p, "null", 4) == 0) { p += 4; *out = new JValue(); return true; }
                return Fail("bad literal");
            }
            // number
            JValue* v = new JValue();
            v->kind = JValue::Num;
            char* e = nullptr;
            v->num = strtod(p, &e);
            if (e == p) { delete v; return Fail("bad number"); }
            p = e;
            *out = v;
            return true;
        }

        bool ParseString(std::string& out)
        {
            if (*p != '"')
                return Fail("expect string");
            p++;
            out.clear();
            while (p < end)
            {
                char c = *p++;
                if (c == '"')
                    return true;
                if (c == '\\')
                {
                    if (p >= end) return Fail("bad escape");
                    char e = *p++;
                    switch (e)
                    {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u':
                    {
                        if (end - p < 4) return Fail("bad \\u");
                        unsigned cp = 0;
                        for (int i = 0; i < 4; i++)
                        {
                            char h = *p++;
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= (h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= (h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= (h - 'A' + 10);
                            else return Fail("bad hex");
                        }
                        // 代理对
                        if (cp >= 0xD800 && cp <= 0xDBFF && end - p >= 6 && p[0] == '\\' && p[1] == 'u')
                        {
                            unsigned lo = 0;
                            const char* q = p + 2;
                            bool ok = true;
                            for (int i = 0; i < 4; i++)
                            {
                                char h = q[i];
                                lo <<= 4;
                                if (h >= '0' && h <= '9') lo |= (h - '0');
                                else if (h >= 'a' && h <= 'f') lo |= (h - 'a' + 10);
                                else if (h >= 'A' && h <= 'F') lo |= (h - 'A' + 10);
                                else { ok = false; break; }
                            }
                            if (ok && lo >= 0xDC00 && lo <= 0xDFFF)
                            {
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                                p = q + 4;
                            }
                        }
                        // UTF-8 编码
                        if (cp < 0x80) out += (char)cp;
                        else if (cp < 0x800)
                        {
                            out += (char)(0xC0 | (cp >> 6));
                            out += (char)(0x80 | (cp & 0x3F));
                        }
                        else if (cp < 0x10000)
                        {
                            out += (char)(0xE0 | (cp >> 12));
                            out += (char)(0x80 | ((cp >> 6) & 0x3F));
                            out += (char)(0x80 | (cp & 0x3F));
                        }
                        else
                        {
                            out += (char)(0xF0 | (cp >> 18));
                            out += (char)(0x80 | ((cp >> 12) & 0x3F));
                            out += (char)(0x80 | ((cp >> 6) & 0x3F));
                            out += (char)(0x80 | (cp & 0x3F));
                        }
                        break;
                    }
                    default: return Fail("bad escape");
                    }
                }
                else
                {
                    out += c;
                }
            }
            return Fail("unterminated string");
        }

        bool ParseArr(JValue** out)
        {
            p++; // [
            JValue* v = new JValue();
            v->kind = JValue::Arr;
            SkipWs();
            if (p < end && *p == ']') { p++; *out = v; return true; }
            for (;;)
            {
                JValue* item = nullptr;
                if (!ParseValue(&item)) { delete v; return false; }
                v->arr.push_back(item);
                SkipWs();
                if (p < end && *p == ',') { p++; continue; }
                if (p < end && *p == ']') { p++; *out = v; return true; }
                delete v;
                return Fail("expect , or ]");
            }
        }

        bool ParseObj(JValue** out)
        {
            p++; // {
            JValue* v = new JValue();
            v->kind = JValue::Obj;
            SkipWs();
            if (p < end && *p == '}') { p++; *out = v; return true; }
            for (;;)
            {
                SkipWs();
                std::string key;
                if (!ParseString(key)) { delete v; return false; }
                SkipWs();
                if (p >= end || *p != ':') { delete v; return Fail("expect :"); }
                p++;
                JValue* item = nullptr;
                if (!ParseValue(&item)) { delete v; return false; }
                v->obj.emplace_back(std::move(key), item);
                SkipWs();
                if (p < end && *p == ',') { p++; continue; }
                if (p < end && *p == '}') { p++; *out = v; return true; }
                delete v;
                return Fail("expect , or }");
            }
        }
    };

    void FreeValue(JValue* v)
    {
        if (!v) return;
        for (auto& kv : v->obj) FreeValue(kv.second);
        for (JValue* x : v->arr) FreeValue(x);
        delete v;
    }

    double BeatOf(const JValue* arr)
    {
        // [beat, num, den] → 绝对拍位
        //   实测校准（MalodyV\\chart 全库 201 张 4K 谱）：拍位 = beat + num/den。
        //   若按 beat*4 + num/den 计算，谱面长度约为音频时长的 3.9 倍（严重错位）。
        //   按 beat + num/den 计算，谱面末尾恰好在音频结束前约 0~1 秒。
        if (!arr || arr->kind != JValue::Arr || arr->arr.size() < 3)
            return -1.0;
        double whole = arr->arr[0]->num;
        double n = arr->arr[1]->num;
        double d = arr->arr[2]->num;
        if (d <= 0)
            d = 1;
        return whole + n / d;
    }
}

namespace MalodyChart
{
    double Chart::LengthSec() const
    {
        double maxT = 0;
        for (const ParsedNote& n : notes)
        {
            double t = BeatToSec(bpms, n.endBeat > n.beat ? n.endBeat : n.beat);
            maxT = std::max(maxT, t);
        }
        return maxT;
    }

    double BeatToSec(const std::vector<BpmPoint>& bpms, double beat)
    {
        // bpms 已按 beat 升序；beat 之前各段积分
        double t = 0;
        double prevBeat = 0;
        double bpm = bpms.empty() ? 120.0 : bpms.front().bpm;
        for (const BpmPoint& b : bpms)
        {
            if (b.beat >= beat)
                break;
            t += (b.beat - prevBeat) / (bpm / 60.0);
            prevBeat = b.beat;
            bpm = b.bpm;
        }
        t += (beat - prevBeat) / (bpm / 60.0);
        return t;
    }

    bool Is4K(const Chart& c)
    {
        return c.mode == 0 && c.column == 4;
    }

    // 实际解析（C++ 对象自由使用；SEH 由 Parse 的守卫承担）
    static bool ParseImpl(const JValue* root, Chart* out, std::string* error)
    {
        const JValue* meta = root->Find("meta");
        const JValue* time = root->Find("time");
        const JValue* note = root->Find("note");
        if (!meta || !note)
        {
            if (error) *error = "missing meta/note";
            return false;
        }

        const JValue* mode = meta->Find("mode");
        const JValue* col = meta->Find("mode_ext");
        out->mode = mode ? (int)mode->num : -1;
        out->column = (col && col->Find("column")) ? (int)col->Find("column")->num : 0;
        const JValue* song = meta->Find("song");
        if (song)
        {
            if (song->Find("title"))  out->title = song->Find("title")->str;
            if (song->Find("artist")) out->artist = song->Find("artist")->str;
        }
        if (meta->Find("creator"))    out->creator = meta->Find("creator")->str;
        if (meta->Find("version"))    out->version = meta->Find("version")->str;
        if (meta->Find("background")) out->background = meta->Find("background")->str;
        if (meta->Find("preview"))    out->previewMs = meta->Find("preview")->num;

        if (time && time->kind == JValue::Arr)
        {
            for (const JValue* t : time->arr)
            {
                BpmPoint bp;
                if (const JValue* b = t->Find("beat")) bp.beat = BeatOf(b);
                if (const JValue* bm = t->Find("bpm")) bp.bpm = bm->num;
                if (bp.bpm > 0)
                    out->bpms.push_back(bp);
            }
            std::sort(out->bpms.begin(), out->bpms.end(),
                      [](const BpmPoint& a, const BpmPoint& c2) { return a.beat < c2.beat; });
        }
        if (out->bpms.empty() || out->bpms.front().beat > 0)
        {
            BpmPoint first;
            first.beat = 0;
            first.bpm = out->bpms.empty() ? 120.0 : out->bpms.front().bpm;
            out->bpms.insert(out->bpms.begin(), first);
        }

        // type=1 音符既可能是主 BGM，也可能是铺在谱面里的打击音效（hitsound），
        // 因此主 BGM 优先取 meta.song.file；没有该字段时取最早、且不带 column 的
        // type=1 音符（主 BGM 通常不带 column，音效一般带）。全部音效名另行收集，
        // 供导入端在文件缺失时按体积兜底。
        struct SoundNote { double beat; std::string sound; bool hasColumn; double offsetMs; };
        std::vector<SoundNote> sounds;
        for (const JValue* n : note->arr)
        {
            int type = 0;
            if (const JValue* t = n->Find("type"))
                type = (int)t->num;
            if (const JValue* b = n->Find("beat"))
            {
                if (type == 1)
                {
                    const JValue* snd = n->Find("sound");
                    if (snd && !snd->str.empty())
                    {
                        SoundNote sn;
                        sn.beat = BeatOf(b);
                        sn.sound = snd->str;
                        sn.hasColumn = (n->Find("column") != nullptr);
                        sn.offsetMs = 0.0;
                        if (const JValue* off = n->Find("offset"))
                            sn.offsetMs = off->num;
                        sounds.push_back(sn);
                        out->soundCandidates.push_back(snd->str);
                    }
                    continue;
                }
                if (type != 0)
                    continue;   // 其他类型（旧版特殊键）忽略
                ParsedNote pn;
                pn.beat = BeatOf(b);
                if (pn.beat < 0)
                    continue;
                if (const JValue* c2 = n->Find("column"))
                    pn.column = (int)c2->num;
                if (pn.column < 0 || pn.column > 3)
                    continue;
                if (const JValue* eb = n->Find("endbeat"))
                    pn.endBeat = BeatOf(eb);
                out->notes.push_back(pn);
            }
        }
        std::string songFile;
        if (song)
        {
            if (const JValue* f = song->Find("file"))
                songFile = f->str;
        }
        const SoundNote* pick = nullptr;
        if (!songFile.empty())
        {
            for (const SoundNote& s : sounds)
                if (s.sound == songFile) { pick = &s; break; }
            if (!pick)
            {
                out->bgm = songFile;   // 元数据指定主音频，但谱面无对应音符
                out->bgmBeat = 0.0;
            }
        }
        else
        {
            for (const SoundNote& s : sounds)
                if (!s.hasColumn && (!pick || s.beat < pick->beat)) pick = &s;
            if (!pick)
                for (const SoundNote& s : sounds)
                    if (!pick || s.beat < pick->beat) pick = &s;
        }
        if (pick)
        {
            out->bgm = pick->sound;
            out->bgmBeat = pick->beat;
            out->bgmOffsetMs = pick->offsetMs;
        }
        if (out->bgm.empty())
        {
            out->bgmBeat = 0;
        }
        if (out->bgmBeat < 0)
            out->bgmBeat = 0;
        out->is4K = Is4K(*out);
        return true;
    }

    // SEH 只能包在不展开对象的函数里；ParseImpl 的 C++ 对象因此隔离在外
    static bool ParseGuarded(JValue* root, Chart* out, std::string* error)
    {
        __try
        {
            return ParseImpl(root, out, error);
        }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
                      ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
        {
            if (error) *error = "json: access violation";
            return false;
        }
    }

    bool Parse(const std::string& jsonText, Chart* out, std::string* error)
    {
        JParser ps;
        ps.p = jsonText.c_str();
        ps.end = jsonText.c_str() + jsonText.size();
        JValue* root = nullptr;
        if (!ps.ParseValue(&root) || !root || root->kind != JValue::Obj)
        {
            if (error) *error = "json: " + ps.err;
            FreeValue(root);
            return false;
        }

        bool ok = ParseGuarded(root, out, error);
        FreeValue(root);
        return ok;
    }

    bool ParseAny(const std::string& text, Chart* out, std::string* error)
    {
        const std::string* p = &text;
        std::string stripped;
        if (text.size() >= 3 && (unsigned char)text[0] == 0xEF &&
            (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF)
        {
            stripped = text.substr(3);   // 去 UTF-8 BOM，否则 osu 首行嗅探失败
            p = &stripped;
        }
        if (OsuChart::Looks(*p))
            return OsuChart::Parse(*p, out, error);
        return Parse(*p, out, error);
    }

    std::string BuildScoreDataJson(const Chart& c, double leadIn)
    {
        // 云云症 ScoreData（JsonUtility 字段名严格一致；TPQN=480）
        //   拍位 → tick = beat*480；时间积分与 Malody 一致（拍 → 秒）。
        //   ScoreOffset = leadIn - bgm 起始时间（BGM 晚于 0 拍时整体前移补偿）
        char buf[256];
        std::string j;
        j.reserve(4096 + c.notes.size() * 48);

        // 云云症的播放时间轴：playTime(tick) = 拍时间 + ScoreOffset，
        // 而 m_Time = AudioSource.time（音乐自 0 秒起）。Malody 的 BGM 音符
        // 所在拍位即"音频 0 秒"对应的谱面拍位，因此要把整体拍位平移
        // -bgmBeat（音频起点 → 第 0 拍），ScoreOffset 仅保留用户设定的
        // 起步缓冲（默认 0 = 与音乐严格同步）。这样既对齐音乐，又保证
        // 所有 Tick ≥ 0（负 Tick 会让 NoteDataCollection 的区间索引越界）。
        double shift = c.bgmBeat;
        double scoreOffset = leadIn;

        // BGM 起始偏移（毫秒）：Malody 的 offset 表示音频相对谱面的播放偏移，
        // 即"音频内容比谱面时间早 offset/1000 秒"（正值通常来自音频前导静音 /
        // 编码延迟）。转换关系为 filePos = 谱面秒 - offset，故拍位整体前移。
        //
        //   实测校准（MalodyV 官方库中全部含 offset 的 4K 谱）：把音符落点与
        //   音频 onset 能量包络做对齐打分，filePos = 谱面秒 - offset 在 28 张谱
        //   中 23 张优于反号（0.284 vs 0.243）；旧实现沿用的反号是错的。
        if (c.bgmOffsetMs != 0.0)
        {
            auto bpmAtBeat = [&](double b) {
                double v = c.bpms.empty() ? 120.0 : c.bpms.front().bpm;
                for (const BpmPoint& p : c.bpms)
                {
                    if (p.beat <= b)
                        v = p.bpm;
                    else
                        break;
                }
                return v;
            };
            double offSec = c.bgmOffsetMs / 1000.0;
            double s1 = c.bgmBeat + offSec * (bpmAtBeat(c.bgmBeat) / 60.0);
            shift = c.bgmBeat + offSec * (bpmAtBeat(s1) / 60.0);   // 二次收敛
        }

        // 第 0 拍处生效的 BPM（= 平移后 InitBpm）：取最后一个 beat <= shift 的时间点
        double initBpm = c.bpms.empty() ? 120.0 : c.bpms.front().bpm;
        for (const BpmPoint& b : c.bpms)
        {
            if (b.beat <= shift)
                initBpm = b.bpm;
            else
                break;
        }

        j += "{\n";
        snprintf(buf, sizeof buf, " \"Version\": %d,\n", 1); j += buf;
        snprintf(buf, sizeof buf, " \"MusicInfoName\": %s,\n", "\"\""); j += buf;
        snprintf(buf, sizeof buf, " \"Level\": %d,\n", 1); j += buf;
        snprintf(buf, sizeof buf, " \"MusicPath\": %s,\n", "\"\""); j += buf;
        snprintf(buf, sizeof buf, " \"ScoreOffset\": %.6f,\n", scoreOffset); j += buf;

        j += " \"InitBpm\": { \"Tick\": 0, \"Bpm\": ";
        snprintf(buf, sizeof buf, "%.6f", initBpm); j += buf;
        j += " },\n";
        j += " \"InitTimeSignature\": { \"Tick\": 0, \"Numerator\": 4, \"Denominator\": 4 },\n";

        // BPM 变化（tick>0 的进事件表）
        j += " \"BpmChangeEvents\": [";
        bool first = true;
        for (size_t i = 0; i < c.bpms.size(); i++)
        {
            const BpmPoint& b = c.bpms[i];
            int tick = (int)llround((b.beat - shift) * 480.0);
            if (tick <= 0)
                continue;
            if (!first) j += ",";
            first = false;
            snprintf(buf, sizeof buf, "\n  { \"Tick\": %d, \"Bpm\": %.6f }", tick, b.bpm);
            j += buf;
        }
        j += "\n ],\n";
        j += " \"TimeSignature\": [],\n";
        j += " \"PhaseChangeEvents\": [],\n";

        // 单点音符（Malody column 0-3 → 云云症 lane 2-5 = S/D/K/L）
        j += " \"SingleNotes\": [";
        first = true;
        for (const ParsedNote& n : c.notes)
        {
            if (n.endBeat > n.beat)
                continue; // 长条走 HoldNotes
            int tick = (int)llround((n.beat - shift) * 480.0);
            if (tick < 0) tick = 0; // 防御：BGM 之前的音符 → 贴到第 0 拍（避免负 Tick 越界）
            snprintf(buf, sizeof buf, "%s\n  { \"Tick\": %d, \"Lane\": %d, \"Type\": 0 }",
                     first ? "" : ",", tick, n.column + 2);
            j += buf;
            first = false;
        }
        j += "\n ],\n";

        // 长条
        j += " \"HoldNotes\": [";
        first = true;
        for (const ParsedNote& n : c.notes)
        {
            if (n.endBeat <= n.beat)
                continue;
            int tick = (int)llround((n.beat - shift) * 480.0);
            if (tick < 0) tick = 0;
            int dur = (int)llround((n.endBeat - n.beat) * 480.0);
            if (dur < 1) dur = 1;
            snprintf(buf, sizeof buf, "%s\n  { \"Tick\": %d, \"Lane\": %d, \"Type\": 0, \"Duration\": %d }",
                     first ? "" : ",", tick, n.column + 2, dur);
            j += buf;
            first = false;
        }
        j += "\n ],\n";
        j += " \"ShiftNotes\": [],\n";
        j += " \"RushNotes\": []\n";
        j += "}";
        return j;
    }
}
