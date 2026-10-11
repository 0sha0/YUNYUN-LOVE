#pragma once
// ============================================================
// RenderHook.h — DXGI Present/ResizeBuffers 钩子 + ImGui(D3D12/D3D11) 覆盖层
//   AppUI::Draw() 由本模块在渲染线程每帧调用。
//   纹理装载（商城封面等）走 WIC 解码 → SRV。
// ============================================================
#include <string>

namespace RenderHook
{
    // worker 线程轮询调用；dxgi 就绪后安装钩子（幂等）
    bool Install();

    // 卸载（worker 线程调用；先置 ExitRequested）
    void Shutdown();

    // 渲染线程当前可绘制
    bool Ready();

    // UI 缩放后端：渲染线程把本帧窗口尺寸给 UI（AppUI 用）
    void GetGameWindowSize(float* w, float* h);

    // ---- 纹理 ----
    // 从磁盘装载图片（jpg/png/bmp），返回 ImTextureID（失败 nullptr；带缓存）
    void* LoadTexture(const std::wstring& path);
    // 只查缓存（不触发解码）；命中时给尺寸
    void* PeekTexture(const std::wstring& path, int* w, int* h);
    // 释放缓存里的指定贴图（翻页回收显存）
    void  ReleaseTexture(const std::wstring& path);
    void  ReleaseAllTextures();

    // 游戏是否正在退出（WndProc 探测）
    void SetGameQuitting();
}
