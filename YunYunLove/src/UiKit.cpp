#include "UiKit.h"
#include "Theme.h"

#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace UiKit
{
    using namespace Theme;

    // ================= 缓动缓存 =================
    static std::map<ImGuiID, float> s_anim;
    static float Anim(ImGuiID id, float target)
    {
        float& v = s_anim[id];
        v += (target - v) * Speed();
        return v;
    }
    static ImVec4 Anim4(ImGuiID id, const ImVec4& a, const ImVec4& b)
    {
        // 用同一个 id 的进度插值两个颜色
        return Lerp(a, b, Anim(id, 1.f));
    }
    static float TextW(const char* s) { return ImGui::CalcTextSize(s ? s : "").x; }
    static ImVec2 VCenter(const ImVec2& mn, const ImVec2& mx, float lineH)
    {
        return ImVec2(mn.x, mn.y + (mx.y - mn.y - lineH) * 0.5f);
    }

    // ================= 矢量图标 =================
    void DrawIcon(ImDrawList* dl, Icon ic, const ImVec2& c, float size, ImU32 col, float thick)
    {
        if (thick <= 0.f) thick = ImMax(1.3f, size * 0.10f);
        float h = size * 0.5f;
        switch (ic)
        {
        case Icon_Grid:
        {
            // 2x2 方块网格。四块整体关于 c 居中：外接盒 [-ext/2, +ext/2]，
            // 两列各自贴住中缝（±g/2），这样既有明确间隙、也不会左右/上下偏心。
            float ext = size * 0.86f;               // 网格外接尺寸（与齿轮等图标观感一致）
            float g = ext * 0.20f;                  // 中缝宽度
            float s = (ext - g) * 0.5f;             // 单个方块边长
            float r = s * 0.16f;
            float h = ext * 0.5f, gy = g * 0.5f;
            for (int i = 0; i < 2; i++)
            {
                float x0 = i ? gy : -h, x1 = i ? h : -gy;
                for (int j = 0; j < 2; j++)
                {
                    float y0 = j ? gy : -h, y1 = j ? h : -gy;
                    dl->AddRectFilled(c + ImVec2(x0, y0), c + ImVec2(x1, y1), col, r);
                }
            }
            break;
        }
        case Icon_Gear:
        {
            float rr = size * 0.28f, rt = size * 0.46f;
            for (int i = 0; i < 8; i++)
            {
                float a = (float)i * IM_PI / 4.f;
                dl->AddLine(c + ImVec2(cosf(a), sinf(a)) * (rr * 0.78f), c + ImVec2(cosf(a), sinf(a)) * rt, col, thick);
            }
            dl->AddCircle(c, rr, col, 24, thick);
            dl->AddCircleFilled(c, rr * 0.34f, col, 16);
            break;
        }
        case Icon_Search:
            dl->AddCircle(c + ImVec2(-size * 0.07f, -size * 0.07f), size * 0.30f, col, 22, thick);
            dl->AddLine(c + ImVec2(size * 0.15f, size * 0.15f), c + ImVec2(size * 0.42f, size * 0.42f), col, thick);
            break;
        case Icon_Download:
            dl->AddLine(c + ImVec2(0, -h * 0.92f), c + ImVec2(0, h * 0.20f), col, thick);
            dl->AddLine(c + ImVec2(-h * 0.42f, -h * 0.14f), c + ImVec2(0, h * 0.28f), col, thick);
            dl->AddLine(c + ImVec2(h * 0.42f, -h * 0.14f), c + ImVec2(0, h * 0.28f), col, thick);
            dl->AddLine(c + ImVec2(-h * 0.80f, h * 0.74f), c + ImVec2(h * 0.80f, h * 0.74f), col, thick);
            break;
        case Icon_Play:
            dl->AddTriangleFilled(c + ImVec2(-h * 0.46f, -h * 0.80f), c + ImVec2(-h * 0.46f, h * 0.80f),
                                  c + ImVec2(h * 0.86f, 0.f), col);
            break;
        case Icon_Note:
            dl->AddLine(c + ImVec2(h * 0.18f, -h * 0.86f), c + ImVec2(h * 0.18f, h * 0.34f), col, thick);
            dl->AddBezierCubic(c + ImVec2(h * 0.18f, -h * 0.86f), c + ImVec2(h * 0.86f, -h * 0.70f),
                               c + ImVec2(h * 0.88f, -h * 0.26f), c + ImVec2(h * 0.36f, -h * 0.06f), col, thick);
            dl->AddCircleFilled(c + ImVec2(-h * 0.24f, h * 0.40f), size * 0.21f, col, 18);
            break;
        case Icon_Minus:
            dl->AddLine(c + ImVec2(-h * 0.60f, 0.f), c + ImVec2(h * 0.60f, 0.f), col, thick);
            break;
        case Icon_Close:
            dl->AddLine(c + ImVec2(-h * 0.52f, -h * 0.52f), c + ImVec2(h * 0.52f, h * 0.52f), col, thick);
            dl->AddLine(c + ImVec2(h * 0.52f, -h * 0.52f), c + ImVec2(-h * 0.52f, h * 0.52f), col, thick);
            break;
        case Icon_Back:
            dl->AddLine(c + ImVec2(h * 0.30f, -h * 0.58f), c + ImVec2(-h * 0.42f, 0.f), col, thick);
            dl->AddLine(c + ImVec2(-h * 0.42f, 0.f), c + ImVec2(h * 0.30f, h * 0.58f), col, thick);
            break;
        case Icon_ChevL:
            dl->AddLine(c + ImVec2(h * 0.34f, -h * 0.52f), c + ImVec2(-h * 0.34f, 0.f), col, thick);
            dl->AddLine(c + ImVec2(-h * 0.34f, 0.f), c + ImVec2(h * 0.34f, h * 0.52f), col, thick);
            break;
        case Icon_ChevR:
            dl->AddLine(c + ImVec2(-h * 0.34f, -h * 0.52f), c + ImVec2(h * 0.34f, 0.f), col, thick);
            dl->AddLine(c + ImVec2(h * 0.34f, 0.f), c + ImVec2(-h * 0.34f, h * 0.52f), col, thick);
            break;
        case Icon_Folder:
            dl->AddRect(c + ImVec2(-h * 0.86f, -h * 0.34f), c + ImVec2(h * 0.86f, h * 0.74f), col, 2.f, 0, thick);
            dl->AddLine(c + ImVec2(-h * 0.86f, -h * 0.34f), c + ImVec2(-h * 0.30f, -h * 0.34f), col, thick);
            dl->AddLine(c + ImVec2(-h * 0.30f, -h * 0.34f), c + ImVec2(-h * 0.08f, -h * 0.70f), col, thick);
            dl->AddLine(c + ImVec2(-h * 0.08f, -h * 0.70f), c + ImVec2(h * 0.52f, -h * 0.70f), col, thick);
            break;
        case Icon_Star:
        {
            ImVec2 pts[10];
            for (int i = 0; i < 10; i++)
            {
                float a = -IM_PI * 0.5f + (float)i * IM_PI / 5.f;
                float rr = (i % 2 == 0) ? h : h * 0.44f;
                pts[i] = c + ImVec2(cosf(a), sinf(a)) * rr;
            }
            dl->AddConvexPolyFilled(pts, 10, col);
            break;
        }
        case Icon_Info:
            dl->AddCircle(c, h * 0.92f, col, 24, thick);
            dl->AddCircleFilled(c + ImVec2(0.f, -h * 0.42f), thick * 0.85f, col, 8);
            dl->AddLine(c + ImVec2(0.f, -h * 0.10f), c + ImVec2(0.f, h * 0.48f), col, thick);
            break;
        case Icon_Refresh:
            dl->PathArcTo(c, h * 0.70f, IM_PI * 0.35f, IM_PI * 1.80f, 28);
            dl->PathStroke(col, 0, thick);
            dl->AddTriangleFilled(c + ImVec2(cosf(IM_PI * 1.80f), sinf(IM_PI * 1.80f)) * (h * 0.70f) + ImVec2(-h * 0.28f, -h * 0.12f),
                                  c + ImVec2(cosf(IM_PI * 1.80f), sinf(IM_PI * 1.80f)) * (h * 0.70f) + ImVec2(h * 0.28f, -h * 0.12f),
                                  c + ImVec2(cosf(IM_PI * 1.80f), sinf(IM_PI * 1.80f)) * (h * 0.70f) + ImVec2(0.f, h * 0.42f), col);
            break;
        case Icon_Check:
            dl->AddLine(c + ImVec2(-h * 0.56f, 0.f), c + ImVec2(-h * 0.14f, h * 0.44f), col, thick);
            dl->AddLine(c + ImVec2(-h * 0.14f, h * 0.44f), c + ImVec2(h * 0.58f, -h * 0.46f), col, thick);
            break;
        case Icon_Heart:
        {
            float rr = size * 0.23f;
            dl->AddCircleFilled(c + ImVec2(-rr * 0.85f, -rr * 0.55f), rr, col, 20);
            dl->AddCircleFilled(c + ImVec2(rr * 0.85f, -rr * 0.55f), rr, col, 20);
            dl->AddTriangleFilled(c + ImVec2(-rr * 1.78f, -rr * 0.10f), c + ImVec2(rr * 1.78f, -rr * 0.10f),
                                  c + ImVec2(0.f, h * 0.94f), col);
            break;
        }
        case Icon_Trash:
            dl->AddLine(c + ImVec2(-h * 0.74f, -h * 0.50f), c + ImVec2(h * 0.74f, -h * 0.50f), col, thick);
            dl->AddRect(c + ImVec2(-h * 0.50f, -h * 0.50f), c + ImVec2(h * 0.50f, h * 0.80f), col, 2.f, 0, thick);
            dl->AddLine(c + ImVec2(-h * 0.18f, -h * 0.84f), c + ImVec2(h * 0.18f, -h * 0.84f), col, thick);
            break;
        case Icon_Music:
            dl->AddLine(c + ImVec2(-h * 0.30f, -h * 0.84f), c + ImVec2(-h * 0.30f, h * 0.32f), col, thick);
            dl->AddLine(c + ImVec2(h * 0.50f, -h * 0.64f), c + ImVec2(h * 0.50f, h * 0.20f), col, thick);
            dl->AddLine(c + ImVec2(-h * 0.30f, -h * 0.84f), c + ImVec2(h * 0.50f, -h * 0.64f), col, thick);
            dl->AddCircleFilled(c + ImVec2(-h * 0.56f, h * 0.36f), size * 0.16f, col, 14);
            dl->AddCircleFilled(c + ImVec2(h * 0.24f, h * 0.24f), size * 0.16f, col, 14);
            break;
        case Icon_Float:
            dl->AddCircle(c, h * 0.80f, col, 26, thick);
            dl->AddCircleFilled(c, h * 0.30f, col, 16);
            break;
        default:
            dl->AddCircle(c, h * 0.72f, col, 20, thick);
            break;
        }
    }
    // ================= 窗口骨架 =================
    bool BeginOverlay(const char* id, const ImVec2& pos, const ImVec2& size, Chrome* ch, bool withSidebar)
    {
        ImGui::SetNextWindowPos(pos);
        ImGui::SetNextWindowSize(size);
        ImGui::SetNextWindowSizeConstraints(ImVec2(720.f, 440.f), ImVec2(FLT_MAX, FLT_MAX));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        // 边框/缩放手柄全部自绘，但保留 ImGui 的「拖边框改尺寸」命中区与光标反馈
        ImGui::PushStyleColor(ImGuiCol_ResizeGrip, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ResizeGripHovered, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ResizeGripActive, ImVec4(0, 0, 0, 0));
        ImGui::GetIO().ConfigWindowsResizeFromEdges = true;
        ImGuiWindowFlags wf = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                              ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
                              ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        if (!withSidebar)
            wf |= ImGuiWindowFlags_NoResize;   // 缩小球不允许改尺寸
        ImGui::Begin(id, nullptr, wf);

        // 用户拖边框后的真实尺寸回写给调用方（布局按真实尺寸走，缩放即时生效）
        ImVec2 realSize = ImGui::GetWindowSize();

        ImVec2 wp = ImGui::GetWindowPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float sbW = ch ? ch->sbW : SidebarW;
        float hdrH = ch ? ch->hdrH : HeaderH;

        if (withSidebar)
        {
            // 外侧柔和投影（画在背景层：窗口自身的绘制会被裁剪到窗口矩形内）
            ImDrawList* bdl = ImGui::GetBackgroundDrawList();
            for (int i = 7; i >= 1; i--)
            {
                float o = (float)i * 1.7f;
                bdl->AddRectFilled(wp - ImVec2(o, o), wp + realSize + ImVec2(o, o * 0.7f),
                                   U32(ImVec4(0, 0, 0, 0.055f)), RBg + o);
            }
            // 底板
            dl->AddRectFilled(wp, wp + realSize, U32(WindowBg), RBg);
            // 顶条（右侧留 10px 内缩，1xSOFT 观感）
            dl->AddRectFilled(wp + ImVec2(160.f, 0.f), wp + ImVec2(realSize.x - 10.f, hdrH),
                              U32(PanelBg), RElem, ImDrawFlags_RoundCornersTop);
            // 左侧栏压在顶条之上
            dl->AddRectFilled(wp, wp + ImVec2(sbW, realSize.y), U32(PanelBg), RChild,
                              ImDrawFlags_RoundCornersLeft);
            // 顶部内高光
            dl->AddRectFilled(wp + ImVec2(RBg, 1.f), wp + ImVec2(realSize.x - RBg, 2.2f), U32(Highlight));
            // 分隔线
            dl->AddRectFilled(wp + ImVec2(0.f, hdrH), wp + ImVec2(realSize.x, hdrH + 1.f), U32(Stroke));
            dl->AddRectFilled(wp + ImVec2(sbW, hdrH), wp + ImVec2(sbW + 1.f, realSize.y), U32(Stroke));
            // 外描边
            dl->AddRect(wp, wp + realSize, U32(Stroke), RBg);
        }
        else
        {
            // 缩小球：背景完全由调用方自绘（圆形），这里不铺矩形底板
        }
        if (ch)
        {
            ch->pos = wp;
            ch->size = realSize;
            ch->sbW = sbW;
            ch->hdrH = hdrH;
        }
        return true;
    }

    void EndOverlay()
    {
        ImGui::End();
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(2);
    }

    // 依据自身矩形拖动窗口。为避免抢走控件悬停，本函数应在窗口内容绘制完之后调用。
    bool DragArea(const char* id, const ImVec2& mn, const ImVec2& mx, ImVec2* pos)
    {
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        ImGuiID iid = w->GetID(id);
        static std::map<ImGuiID, bool> s_armed;
        bool& armed = s_armed[iid];

        ImGuiIO& io = ImGui::GetIO();
        bool inside = io.MousePos.x >= mn.x && io.MousePos.x < mx.x &&
                      io.MousePos.y >= mn.y && io.MousePos.y < mx.y;

        if (inside && !ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemActive() && ImGui::IsMouseClicked(0))
            armed = true;
        if (!ImGui::IsMouseDown(0))
            armed = false;

        if (armed)
        {
            if (pos && (io.MouseDelta.x != 0.f || io.MouseDelta.y != 0.f))
            {
                *pos += io.MouseDelta;
                ImGui::SetWindowPos(*pos);
            }
            return true;
        }
        return false;
    }

    // ================= 导航 =================
    bool SideTab(const char* id, Icon ic, const char* label, bool active, const ImVec2& size)
    {
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        if (w->SkipItems) return false;
        ImGuiID iid = w->GetID(id);
        ImVec2 pos = w->DC.CursorPos;
        ImRect bb(pos, pos + size);
        ImGui::ItemSize(bb);
        if (!ImGui::ItemAdd(bb, iid)) return false;

        bool hovered, held;
        bool pressed = ImGui::ButtonBehavior(bb, iid, &hovered, &held, ImGuiButtonFlags_PressedOnRelease);
        float t = Anim(iid, active ? 1.f : (hovered ? 0.45f : 0.f));

        ImU32 bg = U32(A(BgColor, t * 0.95f));
        if (t > 0.01f)
            w->DrawList->AddRectFilled(bb.Min, bb.Max, bg, RElem);
        if (hovered && !active)
            w->DrawList->AddRectFilled(bb.Min, bb.Max, U32(ImVec4(1, 1, 1, 0.03f)), RElem);
        if (t > 0.01f)
            w->DrawList->AddRectFilled(ImVec2(bb.Min.x + S(1.f), bb.Min.y + S(9.f)),
                                       ImVec2(bb.Min.x + S(3.5f), bb.Max.y - S(9.f)),
                                       U32(A(active ? Main : TxtHover, t)), 2.f);

        float cy = (bb.Min.y + bb.Max.y) * 0.5f;
        ImU32 icc = U32(Lerp(TxtIdle, active ? Main : TxtHover, t));
        ImU32 txc = U32(Lerp(TxtIdle, active ? TxtActive : TxtHover, t));
        DrawIcon(w->DrawList, ic, ImVec2(bb.Min.x + S(24.f), cy), S(17.f), icc, 1.6f);
        w->DrawList->AddText(ImVec2(bb.Min.x + S(46.f), cy - ImGui::GetTextLineHeight() * 0.5f), txc, label);
        return pressed;
    }

    bool HeaderTab(const char* id, const char* label, bool active, const ImVec2& size)
    {
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        if (w->SkipItems) return false;
        ImGuiID iid = w->GetID(id);
        ImVec2 pos = w->DC.CursorPos;
        ImRect bb(pos, pos + size);
        ImGui::ItemSize(bb);
        if (!ImGui::ItemAdd(bb, iid)) return false;

        bool hovered, held;
        bool pressed = ImGui::ButtonBehavior(bb, iid, &hovered, &held, ImGuiButtonFlags_PressedOnRelease);
        float t = Anim(iid, active ? 1.f : (hovered ? 0.5f : 0.f));

        if (active)
        {
            w->DrawList->AddRectFilled(bb.Min, bb.Max, U32(BgColor), RElem);
            w->DrawList->AddRectFilled(ImVec2(bb.Min.x + S(12.f), bb.Max.y - S(2.f)),
                                       ImVec2(bb.Max.x - S(12.f), bb.Max.y), U32(Main), 1.f);
        }
        else if (hovered)
        {
            w->DrawList->AddRectFilled(bb.Min, bb.Max, U32(ImVec4(1, 1, 1, 0.03f)), RElem);
        }
        ImU32 tc = U32(Lerp(TxtIdle, active ? TxtActive : TxtHover, t));
        ImVec2 ts = ImGui::CalcTextSize(label);
        w->DrawList->AddText(ImVec2(bb.Min.x + (bb.GetWidth() - ts.x) * 0.5f,
                                    bb.Min.y + (bb.GetHeight() - ts.y) * 0.5f), tc, label);
        return pressed;
    }

    bool CircleButton(const char* id, const ImVec2& center, float radius, Icon ic,
                      float baseAlpha, bool danger)
    {
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        if (w->SkipItems) return false;
        ImGuiID iid = w->GetID(id);
        // center 以「窗口本地坐标」传入（与 SetCursorPos 保持一致），
        // 绘制与命中判定统一换算到屏幕坐标，避免画到窗口之外。
        ImVec2 c = center + w->Pos;
        ImRect bb(c - ImVec2(radius, radius), c + ImVec2(radius, radius));
        ImGui::ItemSize(bb);
        if (!ImGui::ItemAdd(bb, iid)) return false;

        bool hovered, held;
        bool pressed = ImGui::ButtonBehavior(bb, iid, &hovered, &held, ImGuiButtonFlags_PressedOnRelease);
        float t = Anim(iid, hovered ? 1.f : 0.f);
        ImVec4 hi = danger ? Danger : Main;
        float a = (0.35f + 0.65f * t) * baseAlpha;

        w->DrawList->AddCircleFilled(c, radius, U32(ImVec4(1, 1, 1, 0.04f * t + 0.02f)), 28);
        w->DrawList->AddCircleFilled(c, radius, U32(A(hi, 0.14f * t)), 28);
        w->DrawList->AddCircle(c, radius, U32(A(hi, a)), 28, 1.4f);
        DrawIcon(w->DrawList, ic, c, radius * 1.05f,
                 U32(Lerp(A(TxtIdle, a), danger ? Danger : TxtActive, t)), 1.5f);
        return pressed;
    }
    // ================= 按钮 =================
    struct BtnState { float t = 0.f; };

    static bool ButtonCore(ImGuiID iid, const ImRect& bb, int kind, float* outT)
    {
        bool hovered, held;
        bool pressed = ImGui::ButtonBehavior(bb, iid, &hovered, &held, ImGuiButtonFlags_PressedOnRelease);
        float& t = s_anim[iid];
        t += ((held || hovered) ? 1.f - t : -t) * Speed();
        t = ImClamp(t, 0.f, 1.f);
        *outT = t;

        ImU32 bg;
        ImU32 border = 0;
        bool hasBorder = false;
        switch (kind)
        {
        case 1:  // 主色实心
            bg = U32(Lerp(A(Main, 0.75f + 0.25f * t), A(Main, 1.f), t));
            break;
        case 2:  // 幽灵描边
            bg = U32(Lerp(ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, 0.05f), t));
            border = U32(Lerp(A(Stroke, 1.f), A(Main, 0.9f), t));
            hasBorder = true;
            break;
        case 3:  // 危险
            bg = U32(Lerp(A(Danger, 0.16f), A(Danger, 0.85f), t));
            border = U32(A(Danger, 0.55f + 0.45f * t));
            hasBorder = true;
            break;
        case 4:  // 危险幽灵
            bg = U32(Lerp(ImVec4(0, 0, 0, 0), A(Danger, 0.18f), t));
            border = U32(Lerp(A(Stroke, 1.f), A(Danger, 0.85f), t));
            hasBorder = true;
            break;
        default: // 元素底色 → 主色
            bg = U32(Lerp(ElemBg, Main, t));
            break;
        }
        ImGui::GetWindowDrawList()->AddRectFilled(bb.Min, bb.Max, bg, RElem);
        if (hasBorder)
            ImGui::GetWindowDrawList()->AddRect(bb.Min, bb.Max, border, RElem, 0, 1.2f);
        return pressed;
    }

    bool Button(const char* label, const ImVec2& size, int kind)
    {
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        if (w->SkipItems) return false;
        ImGuiID iid = w->GetID(label);
        ImVec2 lp = w->DC.CursorPos;
        ImVec2 sz = ImGui::CalcItemSize(size, ImGui::CalcTextSize(label).x + S(24.f), S(32.f));
        ImRect bb(lp, lp + sz);
        ImGui::ItemSize(bb);
        if (!ImGui::ItemAdd(bb, iid)) return false;

        float t = 0.f;
        bool pressed = ButtonCore(iid, bb, kind, &t);

        ImU32 fg;
        if (kind == 1) fg = U32(Lerp(TxtActive, Mark, t));
        else if (kind == 3) fg = U32(Lerp(TxtHover, ImVec4(1, 1, 1, 1), t));
        else if (kind == 2) fg = U32(Lerp(TxtHover, TxtActive, t));
        else if (kind == 4) fg = U32(Lerp(A(Danger, 0.90f), ImVec4(1, 1, 1, 1), t));
        else fg = U32(Lerp(TxtHover, Mark, t));
        ImVec2 ts = ImGui::CalcTextSize(label);
        w->DrawList->AddText(ImVec2(bb.Min.x + (bb.GetWidth() - ts.x) * 0.5f,
                                    bb.Min.y + (bb.GetHeight() - ts.y) * 0.5f), fg, label);
        return pressed;
    }

    bool ButtonIcon(Icon ic, const char* label, const ImVec2& size, int kind)
    {
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        if (w->SkipItems) return false;
        ImGuiID iid = w->GetID(label);
        ImVec2 lp = w->DC.CursorPos;
        ImVec2 sz = ImGui::CalcItemSize(size, ImGui::CalcTextSize(label).x + S(24.f), S(32.f));
        ImRect bb(lp, lp + sz);
        ImGui::ItemSize(bb);
        if (!ImGui::ItemAdd(bb, iid)) return false;

        float t = 0.f;
        bool pressed = ButtonCore(iid, bb, kind, &t);

        ImU32 fg;
        if (kind == 1) fg = U32(Lerp(TxtActive, Mark, t));
        else if (kind == 3) fg = U32(Lerp(TxtHover, ImVec4(1, 1, 1, 1), t));
        else if (kind == 4) fg = U32(Lerp(A(Danger, 0.90f), ImVec4(1, 1, 1, 1), t));
        else fg = U32(Lerp(TxtHover, kind == 2 ? TxtActive : Mark, t));

        float isz = ImMin(S(16.f), bb.GetHeight() * 0.48f);
        float tw = TextW(label);
        float total = isz + S(8.f) + tw;
        float x0 = bb.Min.x + (bb.GetWidth() - total) * 0.5f;
        float cy = bb.GetCenter().y;
        DrawIcon(w->DrawList, ic, ImVec2(x0 + isz * 0.5f, cy), isz, fg, 1.6f);
        w->DrawList->AddText(ImVec2(x0 + isz + S(8.f), cy - ImGui::GetTextLineHeight() * 0.5f), fg, label);
        return pressed;
    }

    bool IconButton(const char* id, Icon ic, const ImVec2& size, int kind, const char* tip)
    {
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        if (w->SkipItems) return false;
        ImGuiID iid = w->GetID(id);
        ImVec2 lp = w->DC.CursorPos;
        ImRect bb(lp, lp + size);
        ImGui::ItemSize(bb);
        if (!ImGui::ItemAdd(bb, iid)) return false;

        float t = 0.f;
        bool pressed = ButtonCore(iid, bb, kind, &t);
        ImU32 fg = (kind == 1 || kind == 0) ? U32(Lerp(TxtHover, Mark, t)) : U32(Lerp(TxtHover, TxtActive, t));
        DrawIcon(w->DrawList, ic, bb.GetCenter(), ImMin(size.x, size.y) * 0.5f, fg, 1.6f);
        if (tip && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tip);
        return pressed;
    }

    // ================= 输入框 =================
    bool TextField(const char* id, char* buf, size_t bufSize, const ImVec2& size, const char* hint)
    {
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        if (w->SkipItems) return false;

        float lh = ImGui::GetTextLineHeight();
        float fh = ImMax(size.y, lh + S(12.f));

        // 1xSOFT 风格：底色元素块 + 无描边 + 文字颜色随悬停/激活由暗到亮渐变
        ImGuiID aid = w->GetID(id);
        static std::map<ImGuiID, float> s_txt;
        float& tc0 = s_txt[aid];
        float tgt = (ImGui::GetActiveID() == aid) ? 2.f : (ImGui::GetHoveredID() == aid ? 1.f : 0.f);
        tc0 += (tgt - tc0) * Speed();
        ImVec4 tcol = (tc0 <= 1.f) ? Lerp(PageText, PageTextHov, tc0)
                                   : Lerp(PageTextHov, TxtActive, tc0 - 1.f);

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(12.f), (fh - lh) * 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, RElem);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ElemBg);
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ElemBg);
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ElemBg);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, tcol);
        ImGui::PushStyleColor(ImGuiCol_TextSelectedBg, A(Accent, 0.22f));
        ImGui::SetNextItemWidth(size.x);
        bool changed = ImGui::InputText(id, buf, bufSize, ImGuiInputTextFlags_None);
        ImVec2 mn = ImGui::GetItemRectMin();
        ImVec2 mx = ImGui::GetItemRectMax();
        bool active = ImGui::IsItemActive();
        ImGui::PopStyleColor(6);
        ImGui::PopStyleVar(3);

        if (hint && buf && buf[0] == '\0' && !active)
        {
            w->DrawList->AddText(ImVec2(mn.x + S(12.f), mn.y + (fh - ImGui::GetTextLineHeight()) * 0.5f),
                                 U32(PageTextHov), hint);
        }
        return changed;
    }

    // ================= 勾选框（1xSOFT：整行底色 + 右侧方块） =================
    bool Checkbox(const char* label, bool* v)
    {
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        if (w->SkipItems) return false;
        ImGuiID iid = w->GetID(label);

        float rowW = ImGui::GetContentRegionAvail().x;
        const float rowH = S(38.f);
        ImVec2 pos = w->DC.CursorPos;
        ImRect bb(pos, pos + ImVec2(rowW, rowH));
        ImGui::ItemSize(bb);
        if (!ImGui::ItemAdd(bb, iid)) return false;

        bool hovered, held;
        ImGui::ButtonBehavior(bb, iid, &hovered, &held, ImGuiButtonFlags_None);
        bool changed = false;
        if (ImGui::IsItemClicked())
        {
            *v = !*v;
            changed = true;
            ImGui::MarkItemEdited(iid);
        }

        static std::map<ImGuiID, float> anim;
        float& on = anim[iid];
        on += ((*v ? 1.f : 0.f) - on) * Speed();

        ImU32 bg = U32(Lerp(Lerp(ElemBg, ElemHov, hovered ? 1.f : 0.f), Accent, on));
        w->DrawList->AddRectFilled(bb.Min, bb.Max, bg, RElem);
        w->DrawList->AddRect(bb.Min, bb.Max, U32(Stroke), RElem);

        ImU32 tc = U32(Lerp(Lerp(TxtIdle, TxtHover, hovered ? 1.f : 0.f), ImVec4(1, 1, 1, 1), on));
        w->DrawList->AddText(ImVec2(bb.Min.x + S(13.f), bb.Min.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f),
                             tc, label);

        // 右侧 20x20 方块
        float sz = S(20.f);
        ImVec2 cc(bb.Max.x - S(20.f), bb.GetCenter().y);
        ImVec2 mn(cc.x - sz * 0.5f, cc.y - sz * 0.5f);
        ImVec2 mx(cc.x + sz * 0.5f, cc.y + sz * 0.5f);
        w->DrawList->AddRectFilled(mn, mx, U32(A(Dark(Main), 0.25f + 0.35f * on)), 3.f);
        w->DrawList->AddRect(mn, mx, U32(A(Main, 0.45f + 0.55f * on)), 3.f, 0, 1.3f);
        if (on > 0.02f)
        {
            ImVec2 c = ImVec2(cc.x, cc.y);
            w->DrawList->AddLine(ImVec2(c.x - S(5.f), c.y), ImVec2(c.x - S(1.5f), c.y + S(4.f)), U32(A(Mark, on)), S(2.f));
            w->DrawList->AddLine(ImVec2(c.x - S(1.5f), c.y + S(4.f)), ImVec2(c.x + S(5.f), c.y - S(4.f)), U32(A(Mark, on)), S(2.f));
        }
        return true;
    }

    // ================= 滑条 =================
    static bool SliderCore(const char* label, void* v, ImGuiDataType dt, float mn, float mx,
                           const char* fmt, void* pmin, void* pmax)
    {
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        if (w->SkipItems) return false;
        ImGuiID iid = w->GetID(label);

        float rowW = ImGui::GetContentRegionAvail().x;
        const float rowH = S(50.f);
        ImVec2 pos = w->DC.CursorPos;
        ImRect bb(pos, pos + ImVec2(rowW, rowH));
        ImGui::ItemSize(bb);
        if (!ImGui::ItemAdd(bb, iid, nullptr, ImGuiItemFlags_Inputable)) return false;

        // 按下即激活条目 —— ImGui::SliderScalar 内部也会做这一步；缺了它
        // SliderBehavior 永远看不到 g.ActiveId == id，滑条就完全拖不动。
        bool hovered = false, held = false;
        ImGui::ButtonBehavior(bb, iid, &hovered, &held, ImGuiButtonFlags_None);
        ImRect track(ImVec2(bb.Min.x + S(12.f), bb.Max.y - S(13.f)), ImVec2(bb.Max.x - S(12.f), bb.Max.y - S(8.f)));

        ImRect grab;
        // SliderBehavior 的返回值才是「本帧数值改变」，它不会自己 MarkItemEdited
        bool changed = ImGui::SliderBehavior(bb, iid, dt, v, pmin, pmax, fmt, ImGuiSliderFlags_None, &grab);
        if (changed)
            ImGui::MarkItemEdited(iid);

        if (hovered)
            w->DrawList->AddRectFilled(bb.Min, bb.Max, U32(ImVec4(1, 1, 1, 0.035f)), RElem);

        // 标签
        w->DrawList->AddText(ImVec2(bb.Min.x + S(12.f), bb.Min.y + S(8.f)), U32(TxtHover), label);

        // 数值
        char vbuf[64];
        ImGui::DataTypeFormatString(vbuf, IM_ARRAYSIZE(vbuf), dt, v, fmt);
        ImVec2 vts = ImGui::CalcTextSize(vbuf);
        ImVec2 vmx(bb.Max.x - S(12.f), bb.Min.y + S(25.f));
        ImVec2 vmn(vmx.x - vts.x - S(16.f), bb.Min.y + S(6.f));
        w->DrawList->AddRectFilled(vmn, vmx, U32(BgColor), RElem);
        w->DrawList->AddText(ImVec2(vmn.x + S(8.f), vmn.y + (vmx.y - vmn.y - vts.y) * 0.5f), U32(TxtActive), vbuf);

        // 轨道
        w->DrawList->AddRectFilled(track.Min, track.Max, U32(Second), RElem);
        float frac = (mx > mn) ? ((dt == ImGuiDataType_S32 ? (float)(*(int*)v) : *(float*)v) - mn) / (mx - mn) : 0.f;
        frac = ImClamp(frac, 0.f, 1.f);
        w->DrawList->AddRectFilled(track.Min, ImVec2(track.Min.x + track.GetWidth() * frac, track.Max.y),
                                   U32(Main), RElem);
        w->DrawList->AddCircleFilled(ImVec2(track.Min.x + track.GetWidth() * frac, track.GetCenter().y),
                                     S(5.5f), U32(Main), 20);
        return changed;
    }

    bool SliderI(const char* label, int* v, int mn, int mx, const char* fmt)
    {
        return SliderCore(label, v, ImGuiDataType_S32, (float)mn, (float)mx, fmt, &mn, &mx);
    }
    bool SliderF(const char* label, float* v, float mn, float mx, const char* fmt)
    {
        return SliderCore(label, v, ImGuiDataType_Float, mn, mx, fmt, &mn, &mx);
    }

    void Separator()
    {
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        if (w->SkipItems) return;
        ImVec2 p = w->DC.CursorPos;
        float wd = ImGui::GetContentRegionAvail().x;
        w->DrawList->AddRectFilled(p, ImVec2(p.x + wd, p.y + 1.f), U32(Theme::Separator));
        ImGui::ItemSize(ImVec2(wd, 1.f));
    }
    // ================= 面板（1xSOFT 标题条） =================
    static float kPanelTitleH() { return S(40.f); }
    #define kPanelTitleH kPanelTitleH()

    bool BeginPanel(const char* title, const ImVec2& size, bool scroll)
    {
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        if (w->SkipItems) return false;
        ImVec2 pos = w->DC.CursorPos;
        ImDrawList* dl = w->DrawList;

        dl->AddRectFilled(pos, pos + size, U32(PanelBg), RChild);
        dl->AddRectFilled(pos, pos + ImVec2(size.x, kPanelTitleH), U32(PanelTitle), RChild,
                          ImDrawFlags_RoundCornersTop);
        dl->AddRectFilled(ImVec2(pos.x, pos.y + kPanelTitleH - 1.f),
                          ImVec2(pos.x + size.x, pos.y + kPanelTitleH), U32(Stroke));
        dl->AddRect(pos, pos + size, U32(Stroke), RChild);
        if (title && title[0])
        {
            float lh = ImGui::GetTextLineHeight();
            PushF(F_Bold);
            ImVec2 ts = ImGui::CalcTextSize(title);
            dl->AddText(ImVec2(pos.x + S(16.f), pos.y + (kPanelTitleH - ts.y) * 0.5f), U32(TxtActive), title);
            PopFont();
        }

        ImGui::SetCursorScreenPos(pos + ImVec2(0.f, kPanelTitleH));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(18.f), S(16.f)));
        // ImGui 1.90 起 BeginChild 默认不使用 WindowPadding，必须显式声明，
        // 否则面板内容会紧贴边框（文字/图标贴边）。
        ImGui::BeginChild(title && title[0] ? title : "##panel",
                          ImVec2(size.x, ImMax(20.f, size.y - kPanelTitleH)),
                          ImGuiChildFlags_AlwaysUseWindowPadding,
                          scroll ? ImGuiWindowFlags_None : ImGuiWindowFlags_NoScrollbar);
        return true;
    }

    void EndPanel()
    {
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    // ================= 图片自适应（cover 裁切） =================
    void ImageFit(ImDrawList* dl, void* tex, int tw, int th, const ImVec2& mn, const ImVec2& mx,
                  float rounding)
    {
        ImVec2 sz(mx.x - mn.x, mx.y - mn.y);
        if (sz.x <= 1.f || sz.y <= 1.f) return;
        dl->PushClipRect(mn, mx, true);
        if (tex && tw > 0 && th > 0)
        {
            float boxA = sz.x / sz.y;
            float imgA = (float)tw / (float)ImMax(1, th);
            ImVec2 uv0(0, 0), uv1(1, 1);
            if (imgA > boxA)
            {
                float f = boxA / imgA;
                uv0.x = (1.f - f) * 0.5f;
                uv1.x = 1.f - uv0.x;
            }
            else
            {
                float f = imgA / boxA;
                uv0.y = (1.f - f) * 0.5f;
                uv1.y = 1.f - uv0.y;
            }
            dl->AddImageRounded(tex, mn, mx, uv0, uv1, IM_COL32_WHITE, rounding);
        }
        else
        {
            dl->AddRectFilled(mn, mx, U32(ElemBg), rounding);
            DrawIcon(dl, Icon_Music, ImVec2((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f),
                     ImMin(sz.x, sz.y) * 0.30f, U32(DescIdle));
        }
        dl->PopClipRect();
    }

    // ================= 商城大卡片 =================
    bool BigCard(const char* id, const char* title, const char* sub, void* tex, int tw, int th,
                 const ImVec2& size, bool selected, bool* delOut)
    {
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        if (w->SkipItems) return false;
        ImGuiID iid = w->GetID(id);
        ImVec2 pos = w->DC.CursorPos;
        // 未悬停时预留抬升空间，保证布局不跳
        const float lift = S(3.f);
        ImRect bb(pos + ImVec2(0, lift), pos + size);
        ImGui::ItemSize(ImRect(pos, pos + size));
        if (!ImGui::ItemAdd(ImRect(pos, pos + size), iid)) return false;

        bool hovered, held;
        bool pressed = ImGui::ButtonBehavior(ImRect(pos, pos + size), iid, &hovered, &held,
                                             ImGuiButtonFlags_PressedOnRelease);
        ImDrawList* dl = w->DrawList;
        float t = Anim(iid, hovered ? 1.f : 0.f);
        float off = lift * t;

        ImRect r(bb.Min - ImVec2(0, off), bb.Max - ImVec2(0, off));
        float infoH = ImMin(S(62.f), size.y * 0.34f);
        ImRect img(r.Min, ImVec2(r.Max.x, r.Max.y - infoH));

        // 悬停投影
        if (t > 0.01f)
            dl->AddRectFilled(r.Min - ImVec2(0, S(2.f)), r.Max + ImVec2(0, S(3.f)),
                              U32(ImVec4(0, 0, 0, 0.30f * t)), RChild + 1.f);

        dl->AddRectFilled(r.Min, r.Max, U32(hovered ? CardBgHov : CardBg), RChild);
        ImRect imgClip(img.Min + ImVec2(1, 1), img.Max - ImVec2(1, 0));
        dl->PushClipRect(imgClip.Min, imgClip.Max, true);
        if (tex && tw > 0 && th > 0)
        {
            // 悬停时封面轻微放大（保持裁切填满）
            float zoom = 1.0f + 0.07f * t;
            float boxA = imgClip.GetWidth() / ImMax(1.f, imgClip.GetHeight());
            float imgA = (float)tw / (float)ImMax(1, th);
            ImVec2 uv0(0, 0), uv1(1, 1);
            if (imgA > boxA)
            {
                float f = boxA / imgA / zoom;
                uv0.x = (1.f - f) * 0.5f;
                uv1.x = 1.f - uv0.x;
            }
            else
            {
                float f = imgA / boxA / zoom;
                uv0.y = (1.f - f) * 0.5f;
                uv1.y = 1.f - uv0.y;
            }
            dl->AddImageRounded(tex, imgClip.Min, imgClip.Max, uv0, uv1, IM_COL32_WHITE, RChild);
        }
        else
        {
            dl->AddRectFilled(imgClip.Min, imgClip.Max, U32(ElemBg), RChild);
            DrawIcon(dl, Icon_Music, imgClip.GetCenter(),
                     ImMin(imgClip.GetWidth(), imgClip.GetHeight()) * 0.30f, U32(DescIdle));
        }
        dl->PopClipRect();
        // 图片底部渐隐到卡片底色
        dl->AddRectFilledMultiColor(ImVec2(img.Min.x, img.Max.y - S(30.f)), img.Max,
                                    IM_COL32(20, 21, 26, 0), IM_COL32(20, 21, 26, 0),
                                    IM_COL32(20, 21, 26, 255), IM_COL32(20, 21, 26, 255));
        float bw = selected ? 2.f : (1.2f + 0.4f * t);
        ImVec4 bc = selected ? Main : Lerp(Stroke, Main, 0.30f + 0.70f * t);
        dl->AddRect(r.Min, r.Max, U32(bc), RChild, 0, bw);

        // 信息区
        float tx0 = r.Min.x + S(10.f);
        float tw2 = r.GetWidth() - S(20.f);
        PushF(F_Medium);
        std::string tt = EllipsizeText(title ? title : "", tw2);
        dl->AddText(ImVec2(tx0, r.Max.y - infoH + S(9.f)), U32(TxtActive), tt.c_str());
        PopFont();
        PushF(F_Small);
        std::string ss = EllipsizeText(sub ? sub : "", tw2);
        dl->AddText(ImVec2(tx0, r.Max.y - infoH + S(33.f)), U32(hovered ? DescHover : DescActive), ss.c_str());
        PopFont();

        // 悬停：中央播放标记
        if (t > 0.01f)
        {
            ImVec2 c((img.Min.x + img.Max.x) * 0.5f, (img.Min.y + img.Max.y) * 0.5f);
            dl->AddCircleFilled(c, S(24.f), U32(A(ImVec4(0, 0, 0, 0.50f), t)), 32);
            dl->AddCircle(c, S(24.f), U32(A(Main, t)), 32, 1.8f);
            DrawIcon(dl, Icon_Play, c + ImVec2(S(1.f), 0.f), S(20.f), U32(A(ImVec4(1, 1, 1, 1), t)));
        }

        // ---- 删除角标（右上角，卡片悬停时淡入） ----
        if (delOut)
        {
            ImVec2 cc(r.Max.x - S(15.f), r.Min.y + S(15.f));
            float cd = S(27.f);
            ImRect cb(cc - ImVec2(cd * 0.5f, cd * 0.5f), cc + ImVec2(cd * 0.5f, cd * 0.5f));

            // 注意：卡片 ButtonBehavior 会在按下的那一帧抢走 ActiveId，
            // 因此这里必须允许"被 ActiveId 阻挡"，否则按下帧判定不到角标命中。
            bool chipHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
                               ImGui::IsMouseHoveringRect(cb.Min, cb.Max);

            // 只有"从角标上按下"的这一次释放才算删除，避免拖动卡片误触
            static std::map<ImGuiID, bool> s_arm;
            bool& arm = s_arm[iid];
            bool chipPressed = false;
            if (ImGui::IsMouseClicked(0))
                arm = chipHovered;
            if (ImGui::IsMouseReleased(0))
            {
                chipPressed = chipHovered && arm;
                arm = false;
            }
            if (!ImGui::IsMouseDown(0))
                arm = false;

            float a = ImClamp(t * 1.35f, 0.f, 1.f);
            if (a > 0.02f)
            {
                float hv = Anim(iid ^ 0x9E3779B9u, chipHovered ? 1.f : 0.f);
                ImVec4 base = Lerp(ImVec4(0, 0, 0, 0.60f), Danger, hv);
                dl->AddCircleFilled(cc, cd * 0.5f, U32(A(base, (0.22f + 0.78f * a) * (0.75f + 0.25f * hv))), 26);
                dl->AddCircle(cc, cd * 0.5f, U32(A(Lerp(A(Stroke, 1.f), Danger, hv), a * (0.55f + 0.45f * hv))), 26, 1.3f);
                DrawIcon(dl, Icon_Trash, cc, cd * 0.50f,
                         U32(A(Lerp(TxtHover, ImVec4(1, 1, 1, 1), hv), 0.55f + 0.45f * hv)), 1.6f);
            }
            if (chipHovered)
                ImGui::SetTooltip("删除该曲包");

            *delOut = chipPressed;
            // 悬停/点击角标时，卡片本体不响应点击
            if (chipHovered || chipPressed)
                pressed = false;
        }
        return pressed;
    }

    // ================= 曲目行 =================
    bool SongRow(const char* id, const char* title, const char* sub, const char* badge,
                 const char* right, bool selected, const ImVec2& size)
    {
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        if (w->SkipItems) return false;
        ImGuiID iid = w->GetID(id);
        ImVec2 pos = w->DC.CursorPos;
        ImRect bb(pos, pos + size);
        ImGui::ItemSize(bb);
        if (!ImGui::ItemAdd(bb, iid)) return false;

        bool hovered, held;
        bool pressed = ImGui::ButtonBehavior(bb, iid, &hovered, &held, ImGuiButtonFlags_PressedOnRelease);
        ImDrawList* dl = w->DrawList;
        float t = Anim(iid, selected ? 1.f : (hovered ? 0.5f : 0.f));

        ImVec4 bg = selected ? Lerp(ElemBg, ImVec4(DiffSelB.x, DiffSelB.y, DiffSelB.z, 1.f), 0.55f)
                             : Lerp(ElemBg, ElemHov, hovered ? 1.f : 0.f);
        dl->AddRectFilled(bb.Min, bb.Max, U32(bg), RElem);
        dl->AddRect(bb.Min, bb.Max, U32(selected ? A(DiffSelA, 0.95f) : Stroke), RElem, 0, selected ? 1.6f : 1.f);
        if (selected || hovered)
            dl->AddRectFilled(ImVec2(bb.Min.x + S(1.f), bb.Min.y + S(7.f)),
                              ImVec2(bb.Min.x + S(3.5f), bb.Max.y - S(7.f)),
                              U32(A(selected ? DiffSelA : Main, selected ? 1.f : 0.7f * t)), 2.f);

        float ih = size.y - S(16.f);
        ImRect ic(bb.Min + ImVec2(S(10.f), S(8.f)), ImVec2(bb.Min.x + S(10.f) + ih, bb.Min.y + S(8.f) + ih));
        dl->AddRectFilled(ic.Min, ic.Max, U32(Second), RElem);
        DrawIcon(dl, Icon_Note, ic.GetCenter(), ih * 0.52f, U32(selected ? DiffSelA : Main), 1.7f);

        float x0 = ic.Max.x + S(12.f);
        float xRight = bb.Max.x - S(12.f);
        if (right && right[0])
        {
            ImVec2 rs = ImGui::CalcTextSize(right);
            dl->AddText(ImVec2(xRight - rs.x, bb.GetCenter().y - rs.y * 0.5f), U32(TxtHover), right);
            xRight -= rs.x + S(14.f);
        }
        if (badge && badge[0])
        {
            ImVec2 bs = ImGui::CalcTextSize(badge);
            ImVec2 bmx(xRight, bb.GetCenter().y + bs.y * 0.5f + S(3.f));
            ImVec2 bmn(bmx.x - bs.x - S(14.f), bmx.y - bs.y - S(6.f));
            dl->AddRectFilled(bmn, bmx, U32(A(Main, 0.16f)), RElem);
            dl->AddText(ImVec2(bmn.x + S(7.f), bmn.y + S(3.f)), U32(A(Main, 1.f)), badge);
            xRight -= (bmx.x - bmn.x) + S(14.f);
        }
        float twMax = ImMax(60.f, xRight - x0);
        PushF(F_Medium);
        std::string tt = EllipsizeText(title ? title : "", twMax);
        dl->AddText(ImVec2(x0, bb.Min.y + S(10.f)), U32(TxtActive), tt.c_str());
        PopFont();
        PushF(F_Small);
        std::string ss = EllipsizeText(sub ? sub : "", twMax);
        dl->AddText(ImVec2(x0, bb.Min.y + S(30.f)), U32(DescActive), ss.c_str());
        PopFont();
        return pressed;
    }

    // ================= 分页器 =================
    bool Pager(int* page, int total)
    {
        if (total < 1) total = 1;
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        if (w->SkipItems) return false;

        std::vector<int> items;   // -1 = 省略号
        if (total <= 7)
        {
            for (int i = 1; i <= total; i++) items.push_back(i);
        }
        else
        {
            items.push_back(1);
            int cur = ImClamp(*page + 1, 1, total);
            int lo = ImMax(2, cur - 1), hi = ImMin(total - 1, cur + 1);
            if (lo > 2) items.push_back(-1);
            for (int i = lo; i <= hi; i++) items.push_back(i);
            if (hi < total - 1) items.push_back(-1);
            items.push_back(total);
        }

        const float bw = S(34.f), gap = S(6.f), bh = S(30.f);
        float totalW = (float)(items.size() + 2) * bw + (float)(items.size() + 1) * gap;
        float availW = ImGui::GetContentRegionAvail().x;
        ImVec2 base = w->DC.CursorPos;
        float baseX = base.x + ImMax(0.f, (availW - totalW) * 0.5f);
        bool changed = false;

        auto cell = [&](int i, const ImVec2& mn, const ImVec2& mx, bool active, const char* txt,
                        bool drawChev, Icon chev, int px) -> bool
        {
            ImGui::SetCursorScreenPos(mn);
            ImGui::InvisibleButton(("##pg" + std::to_string(px) + "_" + std::to_string(i)).c_str(),
                                   ImVec2(mx.x - mn.x, mx.y - mn.y));
            bool hov = ImGui::IsItemHovered();
            bool clk = ImGui::IsItemClicked();
            ImU32 bg = active ? U32(Main) : hov ? U32(ElemHov) : U32(ElemBg);
            w->DrawList->AddRectFilled(mn, mx, bg, RElem);
            if (active) w->DrawList->AddRect(mn, mx, U32(A(Main, 1.f)), RElem, 0, 1.4f);
            ImU32 tc = active ? U32(Mark) : hov ? U32(TxtActive) : U32(TxtIdle);
            if (drawChev)
            {
                DrawIcon(w->DrawList, chev, ImVec2((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f), S(16.f), tc, 1.6f);
            }
            else
            {
                ImVec2 ts = ImGui::CalcTextSize(txt);
                w->DrawList->AddText(ImVec2((mn.x + mx.x - ts.x) * 0.5f, (mn.y + mx.y - ts.y) * 0.5f), tc, txt);
            }
            return clk;
        };

        float x = baseX;
        if (cell(-2, ImVec2(x, base.y), ImVec2(x + bw, base.y + bh), false, "", true, Icon_ChevL, -2))
        {
            *page = ImMax(0, *page - 1);
            changed = true;
        }
        x += bw + gap;
        for (size_t k = 0; k < items.size(); k++)
        {
            int it = items[k];
            if (it < 0)
            {
                ImVec2 ts = ImGui::CalcTextSize("...");
                w->DrawList->AddText(ImVec2(x + (bw - ts.x) * 0.5f, base.y + (bh - ts.y) * 0.5f), U32(TxtIdle), "...");
            }
            else
            {
                bool act = (it == *page + 1);
                char b[16];
                snprintf(b, sizeof b, "%d", it);
                if (cell(it, ImVec2(x, base.y), ImVec2(x + bw, base.y + bh), act, b, false, Icon_ChevL, it))
                {
                    *page = it - 1;
                    changed = true;
                }
            }
            x += bw + gap;
        }
        if (cell(-3, ImVec2(x, base.y), ImVec2(x + bw, base.y + bh), false, "", true, Icon_ChevR, -3))
        {
            *page = ImMin(total - 1, *page + 1);
            changed = true;
        }
        ImGui::SetCursorScreenPos(ImVec2(base.x, base.y + bh));
        return changed;
    }

    bool BackButton(const char* id, const char* label)
    {
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        if (w->SkipItems) return false;
        ImGuiID iid = w->GetID(id);
        float tw = TextW(label);
        ImVec2 sz(S(40.f) + tw, S(34.f));
        ImVec2 pos = w->DC.CursorPos;
        ImRect bb(pos, pos + sz);
        ImGui::ItemSize(bb);
        if (!ImGui::ItemAdd(bb, iid)) return false;

        bool hovered, held;
        bool pressed = ImGui::ButtonBehavior(bb, iid, &hovered, &held, ImGuiButtonFlags_PressedOnRelease);
        float t = Anim(iid, hovered ? 1.f : 0.f);
        w->DrawList->AddRectFilled(bb.Min, bb.Max, U32(Lerp(ElemBg, ElemHov, t)), RElem);
        w->DrawList->AddRect(bb.Min, bb.Max, U32(Lerp(Stroke, Main, t * 0.8f)), RElem);
        ImU32 fg = U32(Lerp(TxtHover, TxtActive, t));
        DrawIcon(w->DrawList, Icon_Back, ImVec2(bb.Min.x + S(18.f), bb.GetCenter().y), S(16.f), fg, 1.7f);
        w->DrawList->AddText(ImVec2(bb.Min.x + S(32.f), bb.GetCenter().y - ImGui::GetTextLineHeight() * 0.5f),
                             fg, label);
        return pressed;
    }

    // ================= 文字工具 =================
    void Label(const char* text, const ImVec4& col)
    {
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        if (w->SkipItems) return;
        ImVec2 p = w->DC.CursorPos;
        ImVec2 sz = ImGui::CalcTextSize(text);
        w->DrawList->AddText(p, U32(col), text);
        ImGui::ItemSize(ImVec2(sz.x, sz.y));
    }

    void IconLabel(Icon ic, const char* text, const ImVec4& col, float iconSize)
    {
        ImGuiWindow* w = ImGui::GetCurrentWindow();
        if (w->SkipItems) return;
        ImVec2 p = w->DC.CursorPos;
        float lh = ImGui::GetTextLineHeight();
        float ih = ImMax(lh, iconSize * TextScale);
        float tw = TextW(text);
        DrawIcon(w->DrawList, ic, ImVec2(p.x + ih * 0.5f, p.y + lh * 0.5f), iconSize * TextScale, U32(col), 1.5f);
        w->DrawList->AddText(ImVec2(p.x + ih + S(6.f), p.y), U32(col), text);
        ImGui::ItemSize(ImVec2(ih + S(6.f) + tw, lh));
    }

    // ================= Toast =================
    struct ToastItem { std::string txt; float life; float max; bool good; };
    static std::vector<ToastItem> s_toasts;

    void Toast(const char* text, bool good)
    {
        ToastItem t;
        t.txt = text ? text : "";
        t.max = 4.0f;
        t.life = 4.0f;
        t.good = good;
        s_toasts.push_back(t);
        if (s_toasts.size() > 5)
            s_toasts.erase(s_toasts.begin());
    }

    void DrawToasts()
    {
        ImGuiIO& io = ImGui::GetIO();
        float dt = io.DeltaTime;
        for (size_t i = 0; i < s_toasts.size();)
        {
            s_toasts[i].life -= dt;
            if (s_toasts[i].life <= 0.f)
                s_toasts.erase(s_toasts.begin() + i);
            else
                i++;
        }
        if (s_toasts.empty()) return;

        ImDrawList* fg = ImGui::GetForegroundDrawList();
        ImVec2 disp = io.DisplaySize;
        float y = 22.f;
        for (const ToastItem& t : s_toasts)
        {
            float a = ImClamp(t.life / 0.6f, 0.f, 1.f);
            ImVec2 ts = ImGui::CalcTextSize(t.txt.c_str());
            ImVec2 mn(disp.x - ts.x - S(76.f), y);
            ImVec2 mx(disp.x - S(18.f), y + S(40.f));
            ImU32 accent = U32(A(t.good ? Accent : Danger, a));
            // 注意：U32 → ImGui::GetColorU32 收 0..1 归一化颜色，写成 0..255 会被
            // 全部夹到 1 → 变成纯白（曾经右上角那条"神秘白条"）。
            fg->AddRectFilled(mn, mx, U32(ImVec4(18 / 255.f, 18 / 255.f, 22 / 255.f, 0.96f * a)), RChild);
            fg->AddRectFilled(mn, ImVec2(mn.x + S(3.f), mx.y), accent, RChild, ImDrawFlags_RoundCornersLeft);
            fg->AddRect(mn, mx, U32(A(Stroke, a)), RChild);
            DrawIcon(fg, t.good ? Icon_Check : Icon_Close, ImVec2(mn.x + S(22.f), (mn.y + mx.y) * 0.5f),
                     S(14.f), accent, 1.7f);
            fg->AddText(ImVec2(mn.x + S(40.f), (mn.y + mx.y - ts.y) * 0.5f), U32(A(TxtActive, a)), t.txt.c_str());
            y += S(46.f);
        }
    }

    // ================= 确认框（自绘模态） =================
    // 按字符换行（中文无空格，逐字排版即可）
    static void DrawTextWrapped(ImDrawList* dl, ImVec2 pos, float maxW, ImU32 col, const char* text)
    {
        if (!text) return;
        float lh = ImGui::GetTextLineHeight() + S(5.f);
        size_t len = strlen(text);
        std::string cur;
        for (size_t i = 0; i < len;)
        {
            unsigned char c = (unsigned char)text[i];
            size_t n = (c < 0x80) ? 1 : (c >= 0xF0 ? 4 : (c >= 0xE0 ? 3 : 2));
            if (i + n > len) n = 1;
            if (c == '\n')
            {
                dl->AddText(pos, col, cur.c_str());
                pos.y += lh;
                cur.clear();
                i += 1;
                continue;
            }
            std::string ch(text + i, n);
            if (!cur.empty() && ImGui::CalcTextSize((cur + ch).c_str()).x > maxW)
            {
                dl->AddText(pos, col, cur.c_str());
                pos.y += lh;
                cur.clear();
            }
            cur += ch;
            i += n;
        }
        if (!cur.empty())
            dl->AddText(pos, col, cur.c_str());
    }

    int ConfirmDialog(const char* id, bool* open, const char* title, const char* body,
                      const char* okLabel, const char* cancelLabel, bool danger)
    {
        if (!open || !*open || !id)
            return Confirm_None;

        ImGuiIO& io = ImGui::GetIO();
        ImVec2 disp = io.DisplaySize;
        ImGuiID wid = ImHashStr(id, 0, 0);
        int frame = ImGui::GetFrameCount();

        static std::map<ImGuiID, int> s_lastFrame;
        int& lastFrame = s_lastFrame[wid];
        bool justOpened = (lastFrame != frame - 1);
        lastFrame = frame;

        // 全屏遮罩窗口：既做暗化，也让底层控件整体失去命中（等效模态）
        ImGui::SetNextWindowPos(ImVec2(0.f, 0.f));
        ImGui::SetNextWindowSize(disp);
        if (justOpened)
            ImGui::SetNextWindowFocus();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.f, 0.f, 0.f, 0.55f));
        bool vis = ImGui::Begin(id, nullptr,
                                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                                ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
                                ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavFocus);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(3);
        if (!vis)
        {
            ImGui::End();
            return Confirm_None;
        }

        ImDrawList* dl = ImGui::GetWindowDrawList();
        float pw = ImMin(S(440.f), ImMax(S(240.f), disp.x - S(64.f)));
        float ph = S(208.f);
        ImVec2 mn((disp.x - pw) * 0.5f, (disp.y - ph) * 0.5f);
        ImVec2 mx(mn.x + pw, mn.y + ph);
        const float headH = S(48.f);

        dl->AddRectFilled(mn + ImVec2(0.f, S(10.f)), mx + ImVec2(0.f, S(16.f)),
                          U32(ImVec4(0, 0, 0, 0.35f)), RChild + 2.f);
        dl->AddRectFilled(mn, mx, U32(PanelBg), RChild);
        dl->AddRectFilled(mn, ImVec2(mx.x, mn.y + headH), U32(PanelTitle), RChild,
                          ImDrawFlags_RoundCornersTop);
        dl->AddRectFilledMultiColor(ImVec2(mn.x, mn.y + headH - S(52.f)), ImVec2(mx.x, mn.y + headH),
                                    U32(A(danger ? Danger : Main, 0.f)), U32(A(danger ? Danger : Main, 0.f)),
                                    U32(A(danger ? Danger : Main, 0.14f)), U32(A(danger ? Danger : Main, 0.14f)));
        dl->AddRectFilled(ImVec2(mn.x, mn.y + headH - 1.f), ImVec2(mx.x, mn.y + headH), U32(Stroke));
        dl->AddRect(mn, mx, U32(A(danger ? Danger : Stroke, danger ? 0.40f : 1.f)), RChild);
        dl->AddRectFilled(ImVec2(mn.x + RBg, mn.y + 1.f), ImVec2(mx.x - RBg, mn.y + 2.2f), U32(Highlight));

        DrawIcon(dl, danger ? Icon_Trash : Icon_Info,
                 ImVec2(mn.x + S(30.f), mn.y + headH * 0.5f), S(19.f),
                 U32(danger ? Danger : Main), 1.7f);
        PushF(F_Bold);
        dl->AddText(ImVec2(mn.x + S(50.f), mn.y + (headH - ImGui::GetTextLineHeight()) * 0.5f),
                    U32(TxtActive), title ? title : "");
        PopFont();

        PushF(F_Normal);
        DrawTextWrapped(dl, ImVec2(mn.x + S(24.f), mn.y + headH + S(18.f)), pw - S(48.f),
                        U32(TxtHover), body);
        PopFont();

        const float bh = S(38.f);
        const float bw = S(132.f);
        float by = mx.y - S(20.f) - bh;
        ImGui::SetCursorPos(ImVec2(mx.x - S(20.f) - bw, by));
        bool ok = Button(okLabel ? okLabel : "确认", ImVec2(bw, bh), danger ? 4 : 1);
        ImGui::SetCursorPos(ImVec2(mx.x - S(20.f) - bw * 2.f - S(10.f), by));
        bool cancel = Button(cancelLabel ? cancelLabel : "取消", ImVec2(bw, bh), 2);

        bool outside = !justOpened && ImGui::IsMouseClicked(0) &&
                       !ImRect(mn, mx).Contains(io.MousePos);
        bool esc = ImGui::IsKeyPressed(ImGuiKey_Escape, false);

        int res = Confirm_None;
        if (ok)
            res = Confirm_Yes;
        else if (cancel || outside || esc)
            res = Confirm_No;

        ImGui::End();
        if (res != Confirm_None)
        {
            *open = false;
            s_lastFrame.erase(wid);
        }
        return res;
    }
}
