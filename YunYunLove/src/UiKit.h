#pragma once
// ============================================================
// UiKit.h — YunYunLove 覆盖层控件库（全部自绘 / 1xSOFT 设计语言）
//   窗口骨架、侧栏标签、按钮、输入框、勾选框、滑条、面板、
//   商城大卡片、曲目行、分页器、返回键、Toast、矢量图标。
//   屏幕上不出现任何 ImGui 原生控件外观。
// ============================================================
#include "imgui.h"
#include <string>

namespace UiKit
{
    // ---------------- 矢量图标 ----------------
    enum Icon
    {
        Icon_Grid = 0,
        Icon_Gear,
        Icon_Search,
        Icon_Download,
        Icon_Play,
        Icon_Note,
        Icon_Minus,
        Icon_Close,
        Icon_Back,
        Icon_Folder,
        Icon_Star,
        Icon_Info,
        Icon_Refresh,
        Icon_Check,
        Icon_Heart,
        Icon_ChevL,
        Icon_ChevR,
        Icon_Trash,
        Icon_Music,
        Icon_Float,
        Icon_COUNT
    };
    void DrawIcon(ImDrawList* dl, Icon ic, const ImVec2& c, float size, ImU32 col, float thick = 0.f);

    // ---------------- 窗口骨架 ----------------
    struct Chrome
    {
        ImVec2 pos;               // 窗口左上角（屏幕坐标）
        ImVec2 size;              // 窗口尺寸
        float  sbW  = 200.f;      // 侧栏宽
        float  hdrH = 68.f;       // 顶条高
    };
    // 开启悬浮窗并绘制背景/侧栏/顶条/描边；withSidebar=false 只画圆角背景（缩小球用）
    bool BeginOverlay(const char* id, const ImVec2& pos, const ImVec2& size, Chrome* ch,
                      bool withSidebar = true);
    void EndOverlay();

    // 以自身矩形拖动窗口（返回是否正在拖动）
    bool DragArea(const char* id, const ImVec2& mn, const ImVec2& mx, ImVec2* pos);

    // ---------------- 导航 ----------------
    bool SideTab(const char* id, Icon ic, const char* label, bool active, const ImVec2& size);
    bool HeaderTab(const char* id, const char* label, bool active, const ImVec2& size);
    bool CircleButton(const char* id, const ImVec2& center, float radius, Icon ic,
                      float baseAlpha = 1.f, bool danger = false);

    // ---------------- 控件 ----------------
    // kind: 0 = 元素底色, 1 = 主色实心, 2 = 描边幽灵, 3 = 危险实心, 4 = 危险幽灵
    bool Button(const char* label, const ImVec2& size, int kind = 0);
    bool ButtonIcon(Icon ic, const char* label, const ImVec2& size, int kind = 0);
    bool IconButton(const char* id, Icon ic, const ImVec2& size, int kind = 0, const char* tip = nullptr);
    bool TextField(const char* id, char* buf, size_t bufSize, const ImVec2& size, const char* hint = nullptr);
    bool Checkbox(const char* label, bool* v);
    bool SliderI(const char* label, int* v, int mn, int mx, const char* fmt = "%d");
    bool SliderF(const char* label, float* v, float mn, float mx, const char* fmt = "%.2f");
    void Separator();

    // ---------------- 面板（带 1xSOFT 标题条） ----------------
    bool BeginPanel(const char* title, const ImVec2& size, bool scroll = false);
    void EndPanel();

    // ---------------- 卡片 / 列表 ----------------
    // delOut 非空时，卡片右上角提供一个悬停淡入的删除角标；
    // 返回卡片本体是否被点击（点角标时本体不算被点击），*delOut 返回角标是否被点击。
    bool BigCard(const char* id, const char* title, const char* sub, void* tex, int tw, int th,
                 const ImVec2& size, bool selected = false, bool* delOut = nullptr);
    bool SongRow(const char* id, const char* title, const char* sub, const char* badge,
                 const char* right, bool selected, const ImVec2& size);
    bool Pager(int* page, int total);
    bool BackButton(const char* id, const char* label);

    // ---------------- 绘制工具 ----------------
    void ImageFit(ImDrawList* dl, void* tex, int tw, int th, const ImVec2& mn, const ImVec2& mx,
                  float rounding);
    void Label(const char* text, const ImVec4& col);
    void IconLabel(Icon ic, const char* text, const ImVec4& col, float iconSize = 15.f);

    // ---------------- Toast ----------------
    void Toast(const char* text, bool good = true);
    void DrawToasts();

    // ---------------- 确认框（自绘模态，非系统对话框） ----------------
    enum
    {
        Confirm_None = 0,   // 未开启 / 本帧仍在等待
        Confirm_Yes  = 1,   // 点了确认
        Confirm_No   = 2,   // 取消 / 点击遮罩 / Esc
    };
    // open 指向状态开关；本帧返回结果。danger=true 时确认按钮为危险色。
    // 注意：需在主悬浮窗 EndOverlay() 之后调用，才能保证绘制/命中在最上层。
    int ConfirmDialog(const char* id, bool* open, const char* title, const char* body,
                      const char* okLabel = "确认", const char* cancelLabel = "取消",
                      bool danger = false);
}
