#include "AppUI.h"
#include "UiKit.h"
#include "Theme.h"
#include "Library.h"
#include "Config.h"
#include "FileDialog.h"
#include "GameBridge.h"
#include "RenderHook.h"
#include "MalodyChart.h"
#include "PathUtil.h"
#include "OverlayTex.h"
#include "Log.h"
#include "State.h"

#include "imgui.h"
#include "imgui_internal.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <set>
#include <string>
#include <thread>
#include <vector>

// ============================================================
// AppUI — 覆盖层前端（谱面商城 / 曲目详情 / 设置）
//   版式完全按 1xSOFT Menu 的设计语言重写：
//     · 左侧 200px 侧栏（Logo + 动画标签）
//     · 顶部条（搜索 / 导入 / 设置 / 最小化 / 关闭）
//     · 商城 5x3 大卡片 + 居中分页器（仅装载当前页封面）
//     · 详情页左封面 + 右曲目列表，互不重叠
//   标题栏任意空白处可拖动；点最小化收缩成可拖动小球。
// ============================================================
namespace AppUI
{
    using namespace Theme;

    enum Page
    {
        Page_Shop = 0,
        Page_Detail = 1,
        Page_SettingsPlay = 2,   // 侧栏：游玩设置
        Page_SettingsShop = 3,   // 侧栏：商城设置
        Page_About = 4,          // 侧栏：关于
    };

    static const char* kVersion = "1.0.0";

    static bool   s_visible = true;
    static bool   s_minimized = false;
    static bool   s_posInit = false;
    static ImVec2 s_winPos(0.f, 0.f);
    static ImVec2 s_restorePos(0.f, 0.f);   // 还原主窗时回到的位置
    static ImVec2 s_ballPos(0.f, 0.f);      // 缩小球自身位置（可拖动）
    static bool   s_ballInit = false;
    static float  s_ballDrag = 0.f;
    static bool   s_ballHeld = false;

    static Page   s_page = Page_Shop;
    static char   s_search[128] = "";
    static std::vector<Library::Entry> s_entries;
    static std::vector<int>            s_view;
    static unsigned s_libGen = 0xFFFFFFFFu;
    static int    s_pageIdx = 0;
    static const int kCardsX = 5;
    static const int kCardsY = 3;
    static const int kCardsPage = kCardsX * kCardsY;

    static std::set<std::wstring>    s_texAttempted;
    static std::vector<std::wstring> s_pageTexKeys;
    static std::vector<std::wstring> s_releaseQueue;
    static int s_texBudget = 0;

    static Library::Entry s_detail;
    static int s_selChart = -1;

    static std::atomic<bool> s_launching{ false };
    static std::string s_lastErrSeen;
    static unsigned s_lastResultSeq = 0;

    // 删除曲包确认框状态
    static bool        s_confirmOpen = false;
    static std::string s_confirmId;
    static std::string s_confirmTitle;

    // 悬浮窗逻辑尺寸（UiScale = 1.0 下的像素）。比早期版本小一圈；用户拖边框可改，
    // 尺寸持久化到 config.json（Config::WindowSize）。
    static const float kWinWDef = 1024.f;
    static const float kWinHDef = 640.f;
    static float s_winW = kWinWDef;
    static float s_winH = kWinHDef;
    static bool  s_sizeLoaded = false;
    static bool  s_sizeDirty = false;

    void SetVisible(bool v) { s_visible = v; }
    bool IsInGameplay() { return GameBridge::InGameplay(); }
    void InitBackend() { Library::Init(); }

    // 打开"删除曲包"确认框
    static void AskDelete(const Library::Entry& e)
    {
        s_confirmId = e.id;
        s_confirmTitle = e.title.empty() ? e.id : e.title;
        s_confirmOpen = true;
    }

    static std::string LowerCopy(const std::string& s)
    {
        std::string o = s;
        std::transform(o.begin(), o.end(), o.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return o;
    }
    static std::wstring Widen(const std::string& s)
    {
        if (s.empty()) return std::wstring();
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        std::wstring w(n, 0);
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
        return w;
    }
    static std::string Narrow(const std::wstring& w)
    {
        if (w.empty()) return std::string();
        int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string s(n, 0);
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
        return s;
    }

    static void RebuildView()
    {
        std::string q = LowerCopy(s_search);
        s_view.clear();
        for (size_t i = 0; i < s_entries.size(); i++)
        {
            const Library::Entry& e = s_entries[i];
            if (q.empty() || LowerCopy(e.title).find(q) != std::string::npos ||
                LowerCopy(e.artist).find(q) != std::string::npos ||
                LowerCopy(e.creator).find(q) != std::string::npos)
                s_view.push_back((int)i);
        }
        int pages = std::max(1, (int)((s_view.size() + kCardsPage - 1) / kCardsPage));
        if (s_pageIdx >= pages) s_pageIdx = std::max(0, pages - 1);
    }

    static std::wstring CoverPathOf(const Library::Entry& e)
    {
        if (e.cover.empty()) return std::wstring();
        return e.dir + L"\\" + Widen(e.cover);
    }

    static void ReleasePageTextures()
    {
        for (const std::wstring& k : s_pageTexKeys) s_releaseQueue.push_back(k);
        s_pageTexKeys.clear();
    }

    static void* AcquireCoverTex(const Library::Entry& e, int* outW, int* outH)
    {
        std::wstring path = CoverPathOf(e);
        if (path.empty()) return nullptr;
        void* id = RenderHook::PeekTexture(path, outW, outH);
        if (id) return id;
        if (s_texAttempted.count(path)) return nullptr;
        if (s_texBudget <= 0) return nullptr;
        s_texBudget--;
        s_texAttempted.insert(path);
        id = RenderHook::LoadTexture(path);
        if (id)
        {
            if (std::find(s_pageTexKeys.begin(), s_pageTexKeys.end(), path) == s_pageTexKeys.end())
                s_pageTexKeys.push_back(path);
            RenderHook::PeekTexture(path, outW, outH);
        }
        return id;
    }

    // ---------- 后台状态同步（每帧一次，绘制之前） ----------
    void TickFrame()
    {
        s_texBudget = 1;   // 每帧最多解一张封面：把解码/上传开销摊平，避免切换时卡顿
        if (!s_releaseQueue.empty())
        {
            for (const std::wstring& k : s_releaseQueue) RenderHook::ReleaseTexture(k);
            s_releaseQueue.clear();
        }

        unsigned gen = Library::Gen();
        if (gen != s_libGen)
        {
            s_libGen = gen;
            s_entries = Library::Snapshot();
            RebuildView();
            ReleasePageTextures();
            s_texAttempted.clear();
        }
        unsigned seq = Library::St().resultSeq.load(std::memory_order_acquire);
        if (seq != s_lastResultSeq)
        {
            s_lastResultSeq = seq;
            int r = Library::St().lastImportResult.load();
            std::string msg;
            {
                std::lock_guard<std::mutex> lk(Library::St().importMsgMx);
                msg = Library::St().importMsg;
            }
            if (!msg.empty())
                UiKit::Toast(msg.c_str(), r != 2);
        }

        if (s_launching.load())
        {
            if (GameBridge::InGameplay())
            {
                s_launching = false;
                UiKit::Toast("已进入游玩界面，祝开心！", true);
            }
            else
            {
                std::string err = GameBridge::LastError();
                if (!err.empty() && err != s_lastErrSeen)
                {
                    s_lastErrSeen = err;
                    s_launching = false;
                    UiKit::Toast(("启动失败: " + err).c_str(), false);
                }
            }
        }
    }

    // ---------- 开始游玩 ----------
    static void StartPlay(const Library::Entry& e, const Library::ChartInfo& c)
    {
        if (s_launching.exchange(true)) return;
        Library::Entry entry = e;
        Library::ChartInfo chart = c;
        std::thread([entry, chart] {
            std::wstring mcPath = entry.dir + L"\\" + Widen(chart.file);
            // 谱面名可能很长（Store 目录 + zip 内层目录 + 长文件名 > 260），需超长路径前缀
            FILE* f = _wfopen(PathUtil::Long(mcPath).c_str(), L"rb");
            if (!f)
            {
                GameBridge::SetExternalError("谱面文件读取失败: " + chart.file);
                s_launching = false;
                return;
            }
            std::string text;
            char buf[8192];
            size_t n;
            while ((n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
            fclose(f);

            MalodyChart::Chart ch;
            std::string perr;
            if (!MalodyChart::ParseAny(text, &ch, &perr))
            {
                GameBridge::SetExternalError("谱面解析失败: " + perr);
                s_launching = false;
                return;
            }
            int holds = 0;
            for (const auto& nn : ch.notes) if (nn.endBeat > nn.beat) holds++;
            Log::Printf("[UI] chart parsed: notes=%d (holds=%d singles=%d) bpms=%d len=%.2fs",
                        (int)ch.notes.size(), holds, (int)ch.notes.size() - holds,
                        (int)ch.bpms.size(), ch.LengthSec());

            std::string scoreJson = MalodyChart::BuildScoreDataJson(ch, Config::LeadInSec());
            Log::Printf("[UI] score json bytes=%d", (int)scoreJson.size());

            std::wstring audio = entry.dir + L"\\" + Widen(chart.bgm);
            if (GetFileAttributesW(audio.c_str()) == INVALID_FILE_ATTRIBUTES)
            {
                GameBridge::SetExternalError("找不到音频文件: " + chart.bgm);
                s_launching = false;
                return;
            }
            GameBridge::RequestPlay({ audio, scoreJson, entry.title, entry.artist, chart.label });
            for (int i = 0; i < 200 && s_launching.load(); i++) Sleep(100);
            s_launching = false;
        }).detach();
    }
    // ================= 顶部条 =================
    static void DrawHeader(const UiKit::Chrome& ch, float sc)
    {
        const float fieldH = 36.f * sc;
        float y = (ch.hdrH - fieldH) * 0.5f;
        float x = ch.sbW + 16.f * sc;

        ImGui::SetCursorPos(ImVec2(x, y));
        if (UiKit::TextField("##search", s_search, sizeof s_search, ImVec2(300.f * sc, fieldH),
                             "搜索歌名 / 艺术家 / 谱师…"))
        {
            RebuildView();
            ReleasePageTextures();
            s_texAttempted.clear();
        }
        x += 300.f * sc + 8.f * sc;

        ImGui::SetCursorPos(ImVec2(x, y));
        if (UiKit::ButtonIcon(UiKit::Icon_Search, "搜索", ImVec2(80.f * sc, fieldH), 0))
        {
            s_pageIdx = 0;
            RebuildView();
            ReleasePageTextures();
            s_texAttempted.clear();
        }
        x += 80.f * sc + 8.f * sc;

        ImGui::SetCursorPos(ImVec2(x, y));
        if (UiKit::ButtonIcon(UiKit::Icon_Download, "导入 Malody 谱面", ImVec2(186.f * sc, fieldH), 1))
        {
            FileDialog::PickZipAsync([](std::wstring path) {
                if (!path.empty()) Library::ImportAsync(path);
            });
        }

        // 右上角：X = 收起成可拖动小球（原“设置齿轮”已移除，设置入口在左侧栏）
        float rx = ch.size.x - 18.f * sc;
        ImGui::SetCursorPos(ImVec2(rx - 34.f * sc, (ch.hdrH - 34.f * sc) * 0.5f));
        if (UiKit::IconButton("##btn_min", UiKit::Icon_Close, ImVec2(34.f * sc, 34.f * sc), 0,
                              "收起为悬浮球"))
        {
            s_restorePos = s_winPos;
            s_minimized = true;
        }
    }

    // ================= 左侧栏 =================
    static void DrawSidebar(const UiKit::Chrome& ch, float sc)
    {
        ImVec2 wp = ch.pos;
        ImDrawList* dl = ImGui::GetWindowDrawList();

        ImVec2 a = wp + ImVec2(16.f * sc, 16.f * sc);
        float s = 36.f * sc;
        dl->AddRectFilled(a, a + ImVec2(s, s), U32(A(Main, 0.85f)), 10.f * sc);
        dl->AddRect(a, a + ImVec2(s, s), U32(A(Main, 1.f)), 10.f * sc);
        UiKit::DrawIcon(dl, UiKit::Icon_Note, a + ImVec2(s * 0.5f, s * 0.5f), s * 0.56f, U32(Mark), 2.1f * sc);
        PushF(F_Bold);
        dl->AddText(wp + ImVec2(60.f * sc, 15.f * sc), U32(TxtActive), "YUN-YUN");
        PopFont();
        PushF(F_Small);
        dl->AddText(wp + ImVec2(60.f * sc, 39.f * sc), U32(DescActive), "L O V E   ·  谱面商城");
        PopFont();

        const float tabW = ch.sbW - 24.f * sc;
        const float tabH = 42.f * sc;
        // 每个页签都从 12px 处起画：ImGui 的 ItemSize 换行时会把 CursorPos.x 重置为
        // 「窗口原点 + Indent」，不设 Indent 的话第二个页签会直接贴到窗口左边缘
        //（表现为两个页签图标左右错位）。
        ImGui::Indent(12.f * sc);
        ImGui::SetCursorPos(ImVec2(12.f * sc, ch.hdrH + 22.f * sc));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 8.f * sc));
        bool onShop = (s_page == Page_Shop || s_page == Page_Detail);
        if (UiKit::SideTab("##tab_shop", UiKit::Icon_Grid, "谱面商城", onShop, ImVec2(tabW, tabH)))
            s_page = Page_Shop;
        if (UiKit::SideTab("##tab_play", UiKit::Icon_Play, "游玩设置", s_page == Page_SettingsPlay,
                           ImVec2(tabW, tabH)))
            s_page = Page_SettingsPlay;
        if (UiKit::SideTab("##tab_storeset", UiKit::Icon_Folder, "商城设置", s_page == Page_SettingsShop,
                           ImVec2(tabW, tabH)))
            s_page = Page_SettingsShop;
        if (UiKit::SideTab("##tab_about", UiKit::Icon_Info, "关于", s_page == Page_About,
                           ImVec2(tabW, tabH)))
            s_page = Page_About;
        ImGui::PopStyleVar();
        ImGui::Unindent(12.f * sc);

        PushF(F_Small);
        bool ready = GameBridge::Ready();
        char st[128];
        snprintf(st, sizeof st, "%s", ready ? "游戏已就绪" : "正在连接游戏…");
        dl->AddText(wp + ImVec2(16.f * sc, ch.size.y - 56.f * sc), U32(ready ? Accent : TxtIdle), st);
        snprintf(st, sizeof st, "曲包 %d 个", (int)s_entries.size());
        dl->AddText(wp + ImVec2(16.f * sc, ch.size.y - 38.f * sc), U32(DescActive), st);
        PopFont();
    }

    // ================= 商城页 =================
    static void DrawShop(float sc)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 cp = ImGui::GetWindowPos();
        ImVec2 csz = ImGui::GetWindowSize();

        const float pad = 18.f;
        const float top = 16.f;
        const float footer = 80.f;
        float availW = csz.x - pad * 2.f;
        float availH = csz.y - top - footer;

        if (s_view.empty())
        {
            ImVec2 c = cp + ImVec2(csz.x * 0.5f, top + availH * 0.5f);
            PushF(F_Title);
            const char* t1 = s_entries.empty() ? "商城还是空的" : "没有匹配的曲包";
            ImVec2 ts = ImGui::CalcTextSize(t1);
            dl->AddText(c - ImVec2(ts.x * 0.5f, 30.f * sc), U32(TxtIdle), t1);
            PopFont();
            PushF(F_Normal);
            const char* t2 = s_entries.empty()
                                 ? "点击顶部「导入 Malody 谱面」选择 .zip 谱面包（仅支持 4K Key）"
                                 : "换个关键词试试";
            ts = ImGui::CalcTextSize(t2);
            dl->AddText(c - ImVec2(ts.x * 0.5f, 0.f), U32(DescActive), t2);
            PopFont();
            return;
        }

        const float gap = 14.f;
        float cardW = (availW - gap * (kCardsX - 1)) / kCardsX;
        float cardH = (availH - gap * (kCardsY - 1)) / kCardsY;

        int begin = s_pageIdx * kCardsPage;
        if (cardW > 70.f && cardH > 110.f)
        {
            for (int r = 0; r < kCardsY; r++)
            {
                for (int col = 0; col < kCardsX; col++)
                {
                    int idx = begin + r * kCardsX + col;
                    ImVec2 cmin(pad + col * (cardW + gap), top + r * (cardH + gap));
                    if (idx >= (int)s_view.size())
                        continue;   // 空位保持干净的背景，不画占位底板/图形
                    const Library::Entry& e = s_entries[s_view[idx]];
                    char cid[64];
                    snprintf(cid, sizeof cid, "##card_%d_%d", s_pageIdx, idx);
                    ImGui::SetCursorPos(cmin);
                    int tw = 0, th = 0;
                    void* tex = AcquireCoverTex(e, &tw, &th);
                    std::string sub = e.artist;
                    if (!e.versionText.empty())
                    {
                        if (!sub.empty()) sub += "  ·  ";
                        sub += e.versionText;
                    }
                    if (sub.empty()) sub = e.creator;
                    bool delClicked = false;
                    bool opened = UiKit::BigCard(cid, e.title.c_str(), sub.c_str(), tex, tw, th,
                                                 ImVec2(cardW, cardH), false, &delClicked);
                    if (delClicked)
                    {
                        AskDelete(e);
                    }
                    else if (opened)
                    {
                        s_detail = e;
                        s_selChart = e.charts.empty() ? -1 : 0;   // 默认选中第一条，避免空信息栏
                        s_page = Page_Detail;
                    }
                }
            }
        }

        int pages = (int)((s_view.size() + kCardsPage - 1) / kCardsPage);
        if (pages < 1) pages = 1;
        ImGui::SetCursorPos(ImVec2(0.f, csz.y - 58.f));
        if (UiKit::Pager(&s_pageIdx, pages))
        {
            ReleasePageTextures();
            s_texAttempted.clear();
        }

        PushF(F_Small);
        char st[192];
        snprintf(st, sizeof st, "共 %d 个曲包 · 第 %d/%d 页 · 仅装载当前页封面",
                 (int)s_view.size(), s_pageIdx + 1, pages);
        dl->AddText(cp + ImVec2(pad, csz.y - 22.f), U32(DescIdle), st);
        PopFont();
    }

    // ================= 详情页 =================
    static std::string BadgeOf(const std::string& label)
    {
        for (char ch : label)
        {
            if (ch == ' ' || ch == '-' || ch == '_') continue;
            if (ch >= '0' && ch <= '9')
            {
                std::string d;
                for (char c2 : label)
                    if ((c2 >= '0' && c2 <= '9') || c2 == '.') d += c2;
                if (d.size() > 4) d = d.substr(0, 4);
                return d.empty() ? std::string("?") : d;
            }
            return std::string(1, (char)std::toupper((unsigned char)ch));
        }
        return "4K";
    }

    static void DrawDetail(float sc)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 cp = ImGui::GetWindowPos();
        ImVec2 csz = ImGui::GetWindowSize();
        const float pad = 18.f;

        ImGui::SetCursorPos(ImVec2(pad, 14.f));
        if (UiKit::BackButton("##back", "返回"))
        {
            s_page = Page_Shop;
            return;
        }
        // 右上角：删除当前曲包（危险幽灵按钮 + 确认框）
        const float delW = S(126.f);
        ImGui::SetCursorPos(ImVec2(csz.x - pad - delW, 15.f));
        if (UiKit::ButtonIcon(UiKit::Icon_Trash, "删除曲包", ImVec2(delW, S(34.f)), 4))
            AskDelete(s_detail);

        PushF(F_Bold);
        float titleW = csz.x - (pad + 96.f) - (delW + pad + S(16.f));
        dl->AddText(cp + ImVec2(pad + 96.f, 20.f), U32(TxtActive),
                    EllipsizeText(s_detail.title, ImMax(S(120.f), titleW)).c_str());
        PopFont();

        const float top = 66.f;
        dl->AddRectFilled(cp + ImVec2(0.f, top - 12.f), cp + ImVec2(csz.x, top - 11.f), U32(Separator));

        float bodyH = csz.y - top - pad;
        if (bodyH < 120.f) bodyH = 120.f;
        float leftW = ImClamp(csz.x * 0.30f, 220.f, 320.f);

        // ---- 左：封面 + 曲目信息 ----
        ImGui::SetCursorPos(ImVec2(pad, top));
        ImGui::BeginChild("##dleft", ImVec2(leftW, bodyH), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        {
            ImDrawList* cdl = ImGui::GetWindowDrawList();
            float cw = leftW - 4.f;
            float chh = cw * 0.70f;
            ImGui::Dummy(ImVec2(cw, chh));
            ImVec2 mn = ImGui::GetItemRectMin();
            ImVec2 mx = ImGui::GetItemRectMax();
            int tw = 0, th = 0;
            void* tex = AcquireCoverTex(s_detail, &tw, &th);
            UiKit::ImageFit(cdl, tex, tw, th, mn, mx, RChild);
            cdl->AddRect(mn, mx, U32(Stroke), RChild);

            ImGui::Dummy(ImVec2(0.f, 10.f));
            PushF(F_Title);
            UiKit::Label(EllipsizeText(s_detail.title, cw).c_str(), TxtActive);
            PopFont();
            PushF(F_Medium);
            UiKit::Label(EllipsizeText(s_detail.artist, cw).c_str(), A(Main, 1.f));
            PopFont();
            ImGui::Dummy(ImVec2(0.f, 4.f));
            UiKit::Separator();
            ImGui::Dummy(ImVec2(0.f, 2.f));

            const Library::ChartInfo* ci = nullptr;
            if (s_selChart >= 0 && s_selChart < (int)s_detail.charts.size())
                ci = &s_detail.charts[s_selChart];

            auto kv = [&](const char* k, const std::string& v) {
                ImDrawList* d = ImGui::GetWindowDrawList();
                ImVec2 p = ImGui::GetCursorScreenPos();
                float lh = ImGui::GetTextLineHeight();
                d->AddText(p, U32(DescActive), k);
                d->AddText(ImVec2(p.x + 60.f, p.y), U32(TxtHover),
                           EllipsizeText(v, cw - 66.f).c_str());
                ImGui::Dummy(ImVec2(0.f, lh + 6.f));
            };

            char b[128];
            kv("谱师", s_detail.creator.empty() ? std::string("-") : s_detail.creator);
            if (ci)
            {
                snprintf(b, sizeof b, "%.0f", ci->bpm);
                kv("BPM", ci->bpm > 0 ? std::string(b) : std::string("-"));
                snprintf(b, sizeof b, "%d:%02d", (int)(ci->lengthSec / 60.0), (int)ci->lengthSec % 60);
                kv("时长", ci->lengthSec > 0 ? std::string(b) : std::string("-"));
                snprintf(b, sizeof b, "%d", ci->notes);
                kv("物件", ci->notes > 0 ? std::string(b) : std::string("-"));
                kv("难度", ci->label);
            }
            kv("曲包", s_detail.id);
        }
        ImGui::EndChild();

        // ---- 右：曲目列表 + 开始游玩 ----
        float rx = pad + leftW + 18.f;
        float rightW = csz.x - rx - pad;
        if (rightW < 280.f)
        {
            rightW = csz.x - pad * 2.f;
            rx = pad;
        }
        const float playH = 52.f;
        float listH = bodyH - playH - 12.f;
        if (listH < 140.f) listH = 140.f;

        ImGui::SetCursorPos(ImVec2(rx, top));
        if (UiKit::BeginPanel("选择曲目（仅 4K Key）", ImVec2(rightW, listH), true))
        {
            for (size_t i = 0; i < s_detail.charts.size(); i++)
            {
                const Library::ChartInfo& c = s_detail.charts[i];

                char id[64], sub[256];
                snprintf(id, sizeof id, "##song_%d", (int)i);
                snprintf(sub, sizeof sub, "BPM %.0f  ·  物件 %d  ·  %d:%02d", c.bpm, c.notes,
                         (int)(c.lengthSec / 60.0), (int)c.lengthSec % 60);
                std::string badge = BadgeOf(c.label);
                float rw = ImGui::GetContentRegionAvail().x;
                if (UiKit::SongRow(id, c.label.c_str(), sub, badge.c_str(), nullptr,
                                   (int)i == s_selChart, ImVec2(rw, 56.f)))
                {
                    s_selChart = (int)i;
                }
                ImGui::Dummy(ImVec2(0.f, 6.f));
            }
        }
        UiKit::EndPanel();

        bool canPlay = (s_selChart >= 0 && s_selChart < (int)s_detail.charts.size());
        ImGui::SetCursorPos(ImVec2(rx, top + listH + 12.f));
        if (UiKit::ButtonIcon(UiKit::Icon_Play, canPlay ? "开始游玩" : "请先选择一条曲目",
                              ImVec2(rightW, playH), canPlay ? 1 : 2))
        {
            if (canPlay) StartPlay(s_detail, s_detail.charts[s_selChart]);
        }
    }
    // 设置页外层容器（三页共用）：铺满内容区并允许上下滚动
    static bool BeginPageScroll(const char* id, ImVec2* outSize)
    {
        ImVec2 csz = ImGui::GetWindowSize();
        const float pad = 18.f;
        ImGui::SetCursorPos(ImVec2(pad, 16.f));
        ImGui::BeginChild(id, ImVec2(csz.x - pad * 2.f, csz.y - 16.f - pad),
                          ImGuiChildFlags_None, ImGuiWindowFlags_None);
        *outSize = ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y);
        return true;
    }

    // ================= 设置页：游玩 =================
    static void DrawSettingsPlay(float sc)
    {
        static bool  s_init = false;
        static bool  s_autoHide = true;
        static float s_leadIn = 0.f;
        static float s_uiScale = 1.f;
        if (!s_init)
        {
            s_init = true;
            s_autoHide = Config::AutoHideInGame();
            s_leadIn = (float)Config::LeadInSec();
            s_uiScale = Config::UiScale();
        }

        ImVec2 avail;
        BeginPageScroll("##setplay", &avail);
        {
            if (UiKit::BeginPanel("游玩设置", ImVec2(avail.x, S(236.f)), false))
            {
                if (UiKit::Checkbox("进入原生游玩界面后自动隐藏悬浮窗", &s_autoHide))
                    Config::SetAutoHideInGame(s_autoHide);
                if (UiKit::SliderF("谱面同步微调（秒）：负=音符提前 / 正=音符延后",
                                   &s_leadIn, -0.3f, 0.3f, "%+.3f s"))
                    Config::SetLeadInSec(s_leadIn);
                if (UiKit::SliderF("界面缩放", &s_uiScale, 0.75f, 1.5f, "%.2f x"))
                    Config::SetUiScale(s_uiScale);
            }
            UiKit::EndPanel();
        }
        ImGui::EndChild();
    }

    // ================= 设置页：商城 =================
    static void DrawSettingsShop(float sc)
    {
        ImVec2 avail;
        BeginPageScroll("##setshop", &avail);
        {
            if (UiKit::BeginPanel("商城设置", ImVec2(avail.x, S(232.f)), false))
            {
                char b[64];
                snprintf(b, sizeof b, "%d", (int)s_entries.size());
                UiKit::IconLabel(UiKit::Icon_Folder, ("曲包数量：" + std::string(b)).c_str(), TxtHover, 15.f);
                ImGui::Dummy(ImVec2(0.f, S(12.f)));
                float bw = (ImGui::GetContentRegionAvail().x - S(10.f)) * 0.5f;
                if (UiKit::ButtonIcon(UiKit::Icon_Refresh, "重新扫描商城", ImVec2(bw, S(38.f)), 0))
                    Library::RescanAsync();
                ImGui::SameLine(0.f, S(10.f));
                if (UiKit::ButtonIcon(UiKit::Icon_Trash, "清理封面缓存", ImVec2(bw, S(38.f)), 0))
                {
                    RenderHook::ReleaseAllTextures();
                    s_texAttempted.clear();
                    s_pageTexKeys.clear();
                    UiKit::Toast("封面缓存已清理", true);
                }
                ImGui::Dummy(ImVec2(0.f, S(10.f)));
                if (UiKit::ButtonIcon(UiKit::Icon_Float, "重置窗口尺寸（默认 1024×640）",
                                      ImVec2(ImGui::GetContentRegionAvail().x, S(38.f)), 0))
                {
                    s_winW = kWinWDef;
                    s_winH = kWinHDef;
                    s_sizeDirty = true;
                    UiKit::Toast("窗口尺寸已重置（可用鼠标拖动边框自由缩放）", true);
                }
            }
            UiKit::EndPanel();
        }
        ImGui::EndChild();
    }

    // ================= 设置页：关于 =================
    // 内嵌图标（RCDATA 102）→ WIC 从内存解码 → 贴图；只尝试一次
    static OverlayTex::Tex AboutIcon()
    {
        static bool tried = false;
        static OverlayTex::Tex tex{};
        if (!tried)
        {
            tried = true;
            const void* data = nullptr;
            DWORD size = 0;
            // 必须用本 DLL 的模块句柄：hModule=nullptr 会去宿主 exe（游戏本体）里找资源
            HMODULE self = nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                   GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               (LPCWSTR)&AboutIcon, &self);
            if (HRSRC hr = FindResourceW(self, MAKEINTRESOURCEW(102), RT_RCDATA))
                if (HGLOBAL hg = LoadResource(self, hr))
                {
                    data = LockResource(hg);
                    size = SizeofResource(self, hr);
                }
            if (data && size > 0)
                tex = OverlayTex::LoadFromMemory(L"##appicon", data, (size_t)size);
            else
                Log::Printf("[UI] about icon resource missing");
        }
        return tex;
    }

    static void DrawAbout(float sc)
    {
        ImVec2 avail;
        BeginPageScroll("##about", &avail);
        {
            float panelH = ImMax(S(400.f), avail.y);
            if (UiKit::BeginPanel("关于", ImVec2(avail.x, panelH), false))
            {
                float cw = ImGui::GetContentRegionAvail().x;
                ImDrawList* dl = ImGui::GetWindowDrawList();

                // ---- 居中应用图标（圆角方 + 由内而外的柔光）----
                const float ib = S(104.f);
                float r = ib * 0.5f;
                ImVec2 p = ImGui::GetCursorScreenPos();
                ImVec2 c(p.x + cw * 0.5f, p.y + S(10.f) + r);
                for (int i = 6; i >= 1; i--)
                {
                    float e = r + i * S(3.f);
                    dl->AddRectFilled(ImVec2(c.x - e, c.y - e), ImVec2(c.x + e, c.y + e),
                                      U32(A(Main, 0.035f)), S(30.f));
                }
                dl->AddRectFilled(ImVec2(c.x - r - S(3.f), c.y - r - S(3.f)),
                                  ImVec2(c.x + r + S(3.f), c.y + r + S(3.f)),
                                  U32(A(Main, 0.22f)), S(26.f));
                OverlayTex::Tex ic = AboutIcon();
                if (ic.id && ic.w > 0)
                    dl->AddImageRounded((ImTextureID)ic.id, ImVec2(c.x - r, c.y - r),
                                        ImVec2(c.x + r, c.y + r), ImVec2(0, 0), ImVec2(1, 1),
                                        IM_COL32_WHITE, S(22.f));
                else
                    dl->AddRectFilled(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r),
                                      U32(A(Main, 0.35f)), S(22.f));
                ImGui::Dummy(ImVec2(cw, S(10.f) + ib + S(18.f)));

                // ---- 居中文字 ----
                auto centered = [&](const char* t, const ImVec4& col, Theme::FontSlot f)
                {
                    PushF(f);
                    ImVec2 ts = ImGui::CalcTextSize(t);
                    ImVec2 tp = ImGui::GetCursorScreenPos();
                    ImGui::GetWindowDrawList()->AddText(
                        ImVec2(tp.x + (cw - ts.x) * 0.5f, tp.y), U32(col), t);
                    ImGui::Dummy(ImVec2(cw, ts.y));
                    PopFont();
                };
                centered("YUN-YUN LOVE", TxtActive, F_Title);
                ImGui::Dummy(ImVec2(0.f, S(6.f)));
                centered(("v" + std::string(kVersion)).c_str(), DescActive, F_Small);
                ImGui::Dummy(ImVec2(0.f, S(8.f)));
                centered("作者：是ShaShen吖", TxtHover, F_Bold);
                ImGui::Dummy(ImVec2(0.f, S(18.f)));

                UiKit::Separator();
                ImGui::Dummy(ImVec2(0.f, S(12.f)));

                // ---- 信息表（左右两列）----
                auto row = [&](const char* k, const char* v)
                {
                    ImVec2 rp = ImGui::GetCursorScreenPos();
                    ImGui::GetWindowDrawList()->AddText(rp, U32(DescIdle), k);
                    ImGui::GetWindowDrawList()->AddText(rp + ImVec2(S(104.f), 0.f), U32(DescActive), v);
                    ImGui::Dummy(ImVec2(cw, ImGui::GetTextLineHeight() + S(10.f)));
                };
                row("支持谱面", "Malody .mc / osu!mania .osu（4K）");
                row("运行方式", "DLL 注入 + IMGUI 覆盖层");
                row("游戏本体", "Yunyun_Syndrome");
                row("快捷键", "INSERT 显示/隐藏  ·  END 卸载");
            }
            UiKit::EndPanel();
        }
        ImGui::EndChild();
    }

    // ================= 缩小球 =================
    static void DrawBall()
    {
        ImVec2 disp = ImGui::GetIO().DisplaySize;
        float d = S(62.f);
        ImVec2 sz(d, d);
        if (!s_ballInit)
        {
            s_ballPos = ImVec2(disp.x - d - 44.f, disp.y - d - 76.f);
            s_ballInit = true;
        }
        s_ballPos.x = ImClamp(s_ballPos.x, 0.f, ImMax(0.f, disp.x - d));
        s_ballPos.y = ImClamp(s_ballPos.y, 0.f, ImMax(0.f, disp.y - d));

        UiKit::Chrome ch;
        ch.sbW = 0.f;
        ch.hdrH = 0.f;
        UiKit::BeginOverlay("##YunYunLoveBall", s_ballPos, sz, &ch, false);
        ImVec2 wp = ImGui::GetWindowPos();
        ImVec2 c = wp + ImVec2(d * 0.5f, d * 0.5f);

        bool held = UiKit::DragArea("##balldrag", wp, wp + sz, &s_ballPos);
        if (held)
            s_ballDrag += fabsf(ImGui::GetIO().MouseDelta.x) + fabsf(ImGui::GetIO().MouseDelta.y);
        if (ImGui::IsMouseReleased(0))
        {
            if (s_ballHeld && s_ballDrag < S(8.f))
            {
                // 几乎没移动 → 视为点击 → 还原主窗（回到最小化前的位置）
                s_winPos = s_restorePos;
                s_minimized = false;
            }
            s_ballDrag = 0.f;
        }
        s_ballHeld = held;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        bool hov = ImGui::IsMouseHoveringRect(wp, wp + sz);
        float r = d * 0.5f - S(3.f);
        dl->AddCircleFilled(c + ImVec2(0.f, S(4.f)), r, U32(ImVec4(0, 0, 0, 0.40f)), 48);
        dl->AddCircleFilled(c, r + S(2.f), U32(A(Main, hov ? 0.20f : 0.08f)), 48);
        dl->AddCircleFilled(c, r, U32(PanelBg), 48);
        dl->AddCircle(c, r, U32(A(Main, hov ? 1.f : 0.60f)), 48, 2.4f);
        dl->AddCircleFilled(c, r - S(8.f), U32(A(Main, hov ? 0.22f : 0.11f)), 48);
        UiKit::DrawIcon(dl, UiKit::Icon_Note, c, r * 0.92f, U32(hov ? TxtActive : TxtHover), 2.1f);
        UiKit::EndOverlay();
    }

    // ================= 每帧 =================
    // 删除曲包确认框（自绘模态，绘制在主悬浮窗之上）
    static void DrawConfirm()
    {
        if (!s_confirmOpen)
            return;
        std::string body = "曲包：" + s_confirmTitle +
                           "\n将从本地曲包目录移除该曲包及其全部谱面与音频文件，此操作不可恢复。";
        int r = UiKit::ConfirmDialog("##confirm_del_pack", &s_confirmOpen, "删除曲包", body.c_str(),
                                     "删除", "取消", true);
        if (r == UiKit::Confirm_Yes)
        {
            std::string id = s_confirmId;
            if (s_page == Page_Detail && s_detail.id == id)
            {
                s_page = Page_Shop;
                s_detail = Library::Entry();
                s_selChart = -1;
            }
            Library::RemoveAsync(id);
        }
    }

    void Draw()
    {
        UiKit::DrawToasts();
        if (!s_visible)
        {
            s_confirmOpen = false;
            return;
        }

        ImVec2 disp = ImGui::GetIO().DisplaySize;
        if (disp.x < 320.f || disp.y < 240.f) return;

        if (!s_sizeLoaded)
        {
            s_sizeLoaded = true;
            float w = 0.f, h = 0.f;
            Config::WindowSize(&w, &h);
            if (w > 0.f) s_winW = w;
            if (h > 0.f) s_winH = h;
        }

        float sc = Config::UiScale();
        float fit = ImMin(disp.x * 0.96f / s_winW, disp.y * 0.96f / s_winH);
        if (sc > fit) sc = fit;
        if (sc < 0.85f) sc = 0.85f;
        Theme::TextScale = sc;

        if (s_minimized)
        {
            s_confirmOpen = false;
            DrawBall();
            return;
        }

        ImVec2 sz(s_winW * sc, s_winH * sc);
        if (sz.x > disp.x - 6.f) sz.x = disp.x - 6.f;
        if (sz.y > disp.y - 6.f) sz.y = disp.y - 6.f;
        if (!s_posInit)
        {
            s_winPos = (disp - sz) * 0.5f;
            s_posInit = true;
        }
        s_winPos.x = ImClamp(s_winPos.x, -sz.x * 0.4f, ImMax(0.f, disp.x - sz.x * 0.6f));
        s_winPos.y = ImClamp(s_winPos.y, 0.f, ImMax(0.f, disp.y - sz.y * 0.9f));

        UiKit::Chrome ch;
        ch.sbW = SidebarW * sc;
        ch.hdrH = HeaderH * sc;

        UiKit::BeginOverlay("##YunYunLove", s_winPos, sz, &ch);

        // 用户拖边框 → ImGui 改了真实尺寸 → 回写；松手后再落盘（避免拖动中每帧写文件）
        if (fabsf(ch.size.x - sz.x) > 0.5f || fabsf(ch.size.y - sz.y) > 0.5f)
        {
            s_winW = ImMax(720.f * sc, ch.size.x) / sc;
            s_winH = ImMax(440.f * sc, ch.size.y) / sc;
            s_sizeDirty = true;
        }
        if (s_sizeDirty && !ImGui::IsMouseDown(0))
        {
            s_sizeDirty = false;
            Config::SetWindowSize(s_winW, s_winH);
        }
        sz = ch.size;

        DrawSidebar(ch, sc);
        DrawHeader(ch, sc);

        ImGui::SetCursorPos(ImVec2(ch.sbW, ch.hdrH));
        ImVec2 cpos = ImGui::GetCursorScreenPos();
        ImGui::SetNextWindowPos(cpos);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::BeginChild("##content", ImVec2(ch.size.x - ch.sbW, ch.size.y - ch.hdrH), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        switch (s_page)
        {
        case Page_Detail:       DrawDetail(sc);       break;
        case Page_SettingsPlay: DrawSettingsPlay(sc); break;
        case Page_SettingsShop: DrawSettingsShop(sc); break;
        case Page_About:        DrawAbout(sc);        break;
        default:                DrawShop(sc);         break;
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        // 右下角缩放提示（自绘三条斜线；拖动边框时更亮）
        {
            ImDrawList* gdl = ImGui::GetWindowDrawList();
            float a = (ImGui::GetCurrentWindow()->ResizeBorderHeld != -1) ? 0.50f : 0.16f;
            for (int i = 0; i < 3; i++)
            {
                float o = 7.f + i * 5.f;
                gdl->AddLine(ch.pos + ch.size - ImVec2(o + 6.f, 4.f),
                             ch.pos + ch.size - ImVec2(4.f, o + 6.f), U32(A(Main, a)), 1.6f);
            }
        }

        // 标题栏拖动（最后调用：仅在空白处生效，不抢控件悬停；模态开启时不拖动）
        if (!s_confirmOpen)
        {
            UiKit::DragArea("##drag_title", ch.pos, ch.pos + ImVec2(sz.x, ch.hdrH), &s_winPos);
            UiKit::DragArea("##drag_side", ch.pos, ch.pos + ImVec2(ch.sbW, ch.hdrH), &s_winPos);
        }

        UiKit::EndOverlay();

        DrawConfirm();
    }
}
