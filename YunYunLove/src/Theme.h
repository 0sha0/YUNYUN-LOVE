#pragma once
// ============================================================
// Theme.h — YunYunLove 视觉体系（完全依据 1xSOFT Menu 的设计语言重写）
//   调色板 / 圆角 / 缓动 / 字体槽 / 绘制工具 统一在此。
//   本文件为 YunYunLove 自有实现，不引用任何示例工程源码。
// ============================================================
#include "imgui.h"
#include "imgui_internal.h"
#include <cmath>
#include <string>

namespace Theme
{
    // ---------------- 调色板 ----------------
    inline ImVec4 WindowBg    = ImColor(14, 14, 18, 255);
    inline ImVec4 PanelBg     = ImColor(18, 18, 22, 255);
    inline ImVec4 PanelTitle  = ImColor(27, 27, 33, 255);
    inline ImVec4 Stroke      = ImColor(28, 28, 36, 255);
    inline ImVec4 Separator   = ImColor(22, 23, 26, 255);

    inline ImVec4 ElemBg      = ImColor(22, 23, 25, 255);
    inline ImVec4 ElemHov     = ImColor(31, 33, 38, 255);
    inline ImVec4 BgColor     = ImColor(27, 27, 33, 240);
    inline ImVec4 Second      = ImColor(27, 27, 33, 255);

    inline ImVec4 Main        = ImColor(128, 192, 255, 191);
    inline ImVec4 Accent      = ImColor(118, 187, 117, 255);
    inline ImVec4 Danger      = ImColor(224, 106, 106, 255);
    inline ImVec4 Mark        = ImColor(0, 0, 0, 255);

    // 卡片 / 层次 / 投影（1xSOFT 观感的补充层次色）
    inline ImVec4 CardBg      = ImColor(20, 21, 26, 255);
    inline ImVec4 CardBgHov   = ImColor(26, 28, 34, 255);
    inline ImVec4 Shadow      = ImColor(0, 0, 0, 160);
    inline ImVec4 Highlight   = ImColor(255, 255, 255, 12);

    // 难度卡配色
    inline ImVec4 DiffSelA    = ImColor(233, 71, 130, 255);
    inline ImVec4 DiffSelB    = ImColor(168, 46, 96, 255);
    inline ImVec4 DiffIdle    = ImColor(44, 47, 58, 170);
    inline ImVec4 DiffIdleSt  = ImColor(70, 75, 92, 160);

    // 三档文字明度
    inline ImVec4 TxtActive   = ImColor(255, 255, 255, 255);
    inline ImVec4 TxtHover    = ImColor(255, 255, 255, 190);
    inline ImVec4 TxtIdle     = ImColor(255, 255, 255, 127);
    inline ImVec4 DescActive  = ImColor(200, 200, 200, 102);
    inline ImVec4 DescHover   = ImColor(200, 200, 200, 63);
    inline ImVec4 DescIdle    = ImColor(200, 200, 200, 40);

    inline ImVec4 PageText    = ImColor(68, 71, 85, 255);
    inline ImVec4 PageTextHov = ImColor(69, 74, 95, 255);

    // ---------------- 圆角 / 尺寸 ----------------
    inline constexpr float RBg    = 8.f;
    inline constexpr float RChild = 8.f;
    inline constexpr float RPage  = 4.f;
    inline constexpr float RElem  = 2.5f;

    inline constexpr float SidebarW = 200.f;   // 左侧栏宽
    inline constexpr float HeaderH  = 68.f;    // 顶部条高

    // ---------------- 缓动 ----------------
    inline float Speed()     { return ImClamp(ImGui::GetIO().DeltaTime * 6.f, 0.f, 1.f); }
    inline float SpeedSlow() { return ImClamp(ImGui::GetIO().DeltaTime * 3.f, 0.f, 1.f); }
    inline ImVec4 Lerp(const ImVec4& a, const ImVec4& b, float t)
    {
        return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                      a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
    }
    inline float LerpF(float a, float b, float t) { return a + (b - a) * t; }

    // ---------------- 颜色工具 ----------------
    inline ImVec4 A(const ImVec4& c, float alpha) { return ImVec4(c.x, c.y, c.z, alpha); }
    inline ImVec4 Dark(const ImVec4& c, float k = 0.2f) { return ImVec4(c.x * k, c.y * k, c.z * k, c.w); }
    inline ImU32  U32(const ImVec4& c) { return ImGui::GetColorU32(c); }
    inline ImVec2 CenterText(const ImVec2& mn, const ImVec2& mx, const char* t)
    {
        ImVec2 s = ImGui::CalcTextSize(t);
        return ImVec2(mn.x + (mx.x - mn.x - s.x) * 0.5f, mn.y + (mx.y - mn.y - s.y) * 0.5f);
    }
    inline ImVec2 CenterV(const ImVec2& mn, const ImVec2& mx, float h)
    {
        return ImVec2(mn.x, mn.y + (mx.y - mn.y - h) * 0.5f);
    }

    // ---------------- 字体 ----------------
    enum FontSlot
    {
        F_Normal = 0,   // 15  正文
        F_Small  = 1,   // 13  次要
        F_Bold   = 2,   // 19  小标题
        F_Title  = 3,   // 25  大标题
        F_Medium = 4,   // 17  常规偏大
        F_Count  = 5,
    };
    inline ImFont* Fonts[F_Count] = {};
    inline float BaseSize[F_Count] = { 16.f, 14.f, 20.f, 26.f, 17.f };

    // 当前整体界面缩放（AppUI 每帧写入，自绘控件据此统一换算尺寸）
    inline float TextScale = 1.f;

    // 字体大小随整体缩放联动，保证文字与控件几何始终匹配
    inline void PushFont(int slot)
    {
        ImFont* f = Fonts[slot] ? Fonts[slot] : ImGui::GetFont();
        ImGui::PushFont(f, BaseSize[slot] * TextScale);
    }
    inline void PopFont() { ImGui::PopFont(); }
    inline void PushF(int slot) { PushFont(slot); }

    // 尺寸换算：把设计稿像素按当前缩放换算
    inline float S(float v) { return v * TextScale; }

    // 按当前字体宽度做省略号截断
    inline std::string EllipsizeText(const std::string& s, float maxW)
    {
        if (maxW <= 0.f || s.empty()) return s;
        if (ImGui::CalcTextSize(s.c_str()).x <= maxW) return s;
        const char* dots = "...";
        float dotsW = ImGui::CalcTextSize(dots).x;
        std::string t = s;
        while (!t.empty())
        {
            size_t n = t.size();
            do { n--; } while (n > 0 && ((unsigned char)t[n] & 0xC0) == 0x80);
            t.resize(n);
            if (ImGui::CalcTextSize(t.c_str()).x + dotsW <= maxW) break;
        }
        return t + dots;
    }

    // ---------------- 全局样式 ----------------
    inline void Apply()
    {
        ImGuiStyle& s = ImGui::GetStyle();
        s.WindowRounding    = RBg;
        s.ChildRounding     = RChild;
        s.FrameRounding     = RElem;
        s.PopupRounding     = RChild;
        s.GrabRounding      = RElem;
        s.ScrollbarRounding = RElem;
        s.TabRounding       = RElem;
        s.WindowBorderSize  = 0.f;
        s.ChildBorderSize   = 0.f;
        s.PopupBorderSize   = 0.f;
        s.FrameBorderSize   = 0.f;
        s.FramePadding      = ImVec2(10, 8);
        s.ItemSpacing       = ImVec2(10, 8);
        s.ItemInnerSpacing  = ImVec2(6, 4);
        s.ScrollbarSize     = 9.f;
        s.GrabMinSize       = 8.f;
        s.WindowPadding     = ImVec2(0, 0);

        ImVec4* c = s.Colors;
        c[ImGuiCol_WindowBg]            = WindowBg;
        c[ImGuiCol_ChildBg]             = PanelBg;
        c[ImGuiCol_PopupBg]             = PanelBg;
        c[ImGuiCol_Text]                = TxtActive;
        c[ImGuiCol_TextDisabled]        = TxtIdle;
        c[ImGuiCol_Border]              = Stroke;
        c[ImGuiCol_FrameBg]             = ElemBg;
        c[ImGuiCol_FrameBgHovered]      = ElemHov;
        c[ImGuiCol_FrameBgActive]       = BgColor;
        c[ImGuiCol_ScrollbarBg]         = ImVec4(0, 0, 0, 0);
        c[ImGuiCol_ScrollbarGrab]       = ElemHov;
        c[ImGuiCol_ScrollbarGrabHovered]= BgColor;
        c[ImGuiCol_ScrollbarGrabActive] = Main;
        c[ImGuiCol_SliderGrab]          = Main;
        c[ImGuiCol_SliderGrabActive]    = Main;
        c[ImGuiCol_CheckMark]           = Mark;
        c[ImGuiCol_Separator]           = Separator;
        c[ImGuiCol_Button]              = ElemBg;
        c[ImGuiCol_ButtonHovered]       = ElemHov;
        c[ImGuiCol_ButtonActive]        = BgColor;
        c[ImGuiCol_Header]              = ElemBg;
        c[ImGuiCol_HeaderHovered]       = ElemHov;
        c[ImGuiCol_HeaderActive]        = BgColor;
        c[ImGuiCol_ModalWindowDimBg]    = ImVec4(0, 0, 0, 0.55f);
    }
}
