#pragma once
// ============================================================
// OverlayTex.h — 覆盖层贴图缓存（WIC 解码 → D3D12/D3D11 SRV）
//   仅渲染线程调用；key 为文件绝对路径。
// ============================================================
#include <string>

namespace OverlayTex
{
    struct Tex
    {
        void* id = nullptr;      // ImTextureID
        int w = 0;
        int h = 0;
    };

    // 只查缓存不装载（未命中返回 id=nullptr 且 loaded=false）
    Tex Peek(const std::wstring& path);
    bool IsLoaded(const std::wstring& path);

    // 装载（命中缓存直接返回；失败返回 id==nullptr 的记录并缓存失败，避免反复解码）
    Tex Load(const std::wstring& path);

    // 从内存里的图片字节装载（内嵌图标等；key 仅作缓存标识，不访问文件系统）
    Tex LoadFromMemory(const std::wstring& key, const void* data, size_t size);

    // 释放单张（翻页回收）
    void Release(const std::wstring& path);

    // 释放全部
    void ReleaseAll();

    // 后端初始化完成后调用（拿到 device 与 SRV 描述符堆）
    void BackendReady(void* dev12, void* dev11, void* ctx11, void* srvHeap12);
}
