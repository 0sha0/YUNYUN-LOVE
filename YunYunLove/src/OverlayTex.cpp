#include "OverlayTex.h"
#include "Log.h"
#include "PathUtil.h"

#include <windows.h>
#include <wincodec.h>
#include <d3d11.h>
#include <d3d12.h>
#include <map>
#include <string>
#include <vector>
#include <cstring>

// ============================================================
// 贴图管线：WIC 解码 RGBA8 →
//   D3D12: 默认堆资源 + SRV（描述符槽由 RenderHook 的分配器给出）
//   D3D11: IMMUTABLE 纹理 + SRV
// 缓存失败结果，防止坏图每帧重试。
// ============================================================
namespace OverlayTex
{
    struct Entry
    {
        Tex tex;
        bool failed = false;
    };

    static ID3D12Device* s_dev12 = nullptr;
    static ID3D11Device* s_dev11 = nullptr;
    static ID3D11DeviceContext* s_ctx11 = nullptr;
    static std::map<std::wstring, Entry> s_cache;

    // SRV 描述符槽（自管空闲表；与 ImGui 后端共用同一个堆，槽位由 RenderHook 划定范围）
    static ID3D12DescriptorHeap* s_srvHeap = nullptr;
    static D3D12_CPU_DESCRIPTOR_HANDLE s_heapCpu = {};
    static D3D12_GPU_DESCRIPTOR_HANDLE s_heapGpu = {};
    static UINT s_heapInc = 0;
    static int s_freeList[512];
    static int s_freeCount = 0;
    struct SlotRef
    {
        unsigned slot = 0;
        unsigned long long gpu = 0;
        ID3D12Resource* res = nullptr;   // 贴图资源（Release 时归还）
    };
    static std::map<std::wstring, SlotRef> s_slots;

    void BackendReady(void* dev12, void* dev11, void* ctx11, void* srvHeap12)
    {
        s_dev12 = (ID3D12Device*)dev12;
        s_dev11 = (ID3D11Device*)dev11;
        s_ctx11 = (ID3D11DeviceContext*)ctx11;
        s_srvHeap = (ID3D12DescriptorHeap*)srvHeap12;
        if (s_dev12 && s_srvHeap)
        {
            s_heapCpu = s_srvHeap->GetCPUDescriptorHandleForHeapStart();
            s_heapGpu = s_srvHeap->GetGPUDescriptorHandleForHeapStart();
            s_heapInc = s_dev12->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            // 槽 0..511 全部归本模块（堆实际大小由 RenderHook 决定，超发时装载失败）
            s_freeCount = 512;
            for (int i = 0; i < 512; i++)
                s_freeList[i] = i;   // 从高下标弹出（511 起步），低段让给 ImGui 字体图集
        }
    }

    static bool AllocSlot(unsigned* slot, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu)
    {
        if (s_freeCount <= 0 || !s_heapInc)
            return false;
        int idx = s_freeList[--s_freeCount];
        cpu->ptr = s_heapCpu.ptr + (SIZE_T)idx * s_heapInc;
        gpu->ptr = s_heapGpu.ptr + (UINT64)idx * s_heapInc;
        *slot = (unsigned)idx;
        return true;
    }

    static void FreeSlot(unsigned slot)
    {
        if (s_freeCount < 512)
            s_freeList[s_freeCount++] = (int)slot;
    }

    // ---- WIC 解码 ----
    // 封面最长边上限：商城卡片/详情封面根本用不到原图分辨率，
    // 缩放后解码与 GPU 上传都变成微秒级，避免切换曲包时的卡顿。
    static const UINT kMaxCoverDim = 1024;

    // 把解码器的某一帧转成 RGBA8。largestFrame=true 时在 .ico 这类多帧容器里挑最大的一张
    // （图标文件通常同时含 16/32/48/256，取第 0 帧会得到糊的小图）。
    static bool DecodeFramesRGBA(IWICImagingFactory* wic, IWICBitmapDecoder* dec, bool largestFrame,
                                 std::vector<BYTE>* px, UINT* w, UINT* h)
    {
        IWICBitmapFrameDecode* frame = nullptr;
        IWICFormatConverter* conv = nullptr;
        IWICBitmapScaler* scaler = nullptr;
        bool ok = false;
        UINT frameIdx = 0;
        if (largestFrame)
        {
            UINT n = 0, best = 0, bestArea = 0;
            if (SUCCEEDED(dec->GetFrameCount(&n)))
                for (UINT i = 0; i < n; i++)
                {
                    IWICBitmapFrameDecode* f = nullptr;
                    if (FAILED(dec->GetFrame(i, &f)) || !f) continue;
                    UINT fw = 0, fh = 0;
                    f->GetSize(&fw, &fh);
                    f->Release();
                    if (fw * fh > bestArea) { bestArea = fw * fh; best = i; }
                }
            frameIdx = best;
        }
        if (SUCCEEDED(dec->GetFrame(frameIdx, &frame)) && frame &&
            SUCCEEDED(wic->CreateFormatConverter(&conv)) &&
            SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
                                       WICBitmapDitherTypeNone, nullptr, 0.f,
                                       WICBitmapPaletteTypeCustom)))
        {
            UINT iw = 0, ih = 0;
            conv->GetSize(&iw, &ih);
            IWICBitmapSource* src = conv;
            if ((iw > kMaxCoverDim || ih > kMaxCoverDim) &&
                SUCCEEDED(wic->CreateBitmapScaler(&scaler)) && scaler)
            {
                double k = (double)kMaxCoverDim / (double)(iw > ih ? iw : ih);
                UINT nw = (UINT)(iw * k + 0.5), nh = (UINT)(ih * k + 0.5);
                if (nw < 1) nw = 1;
                if (nh < 1) nh = 1;
                if (SUCCEEDED(scaler->Initialize(conv, nw, nh, WICBitmapInterpolationModeFant)))
                    src = scaler;
            }
            src->GetSize(&iw, &ih);
            if (iw > 0 && ih > 0 && iw <= 8192 && ih <= 8192)
            {
                px->resize((size_t)iw * ih * 4);
                WICRect rc = { 0, 0, (INT)iw, (INT)ih };
                if (SUCCEEDED(src->CopyPixels(&rc, iw * 4, (UINT)px->size(), px->data())))
                {
                    *w = iw;
                    *h = ih;
                    ok = true;
                }
            }
        }
        if (scaler) scaler->Release();
        if (conv) conv->Release();
        if (frame) frame->Release();
        return ok;
    }

    static bool DecodeRGBA(const std::wstring& path, std::vector<BYTE>* px, UINT* w, UINT* h)
    {
        IWICImagingFactory* wic = nullptr;
        IWICBitmapDecoder* dec = nullptr;
        bool ok = false;
        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(&wic))) && wic &&
            SUCCEEDED(wic->CreateDecoderFromFilename(PathUtil::Long(path).c_str(), nullptr, GENERIC_READ,
                                                     WICDecodeMetadataCacheOnDemand, &dec)))
            ok = DecodeFramesRGBA(wic, dec, false, px, w, h);
        if (dec) dec->Release();
        if (wic) wic->Release();
        return ok;
    }

    // 从内存里的图片字节解码（用于内嵌资源里的应用图标）
    static bool DecodeRGBAFromMemory(const void* data, size_t size,
                                     std::vector<BYTE>* px, UINT* w, UINT* h)
    {
        if (!data || size < 4) return false;
        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, size);
        if (!hg) return false;
        if (void* dst = GlobalLock(hg)) { memcpy(dst, data, size); GlobalUnlock(hg); }
        IStream* stream = nullptr;
        if (FAILED(CreateStreamOnHGlobal(hg, TRUE, &stream)) || !stream)
        {
            GlobalFree(hg);
            return false;
        }
        IWICImagingFactory* wic = nullptr;
        IWICBitmapDecoder* dec = nullptr;
        bool ok = false;
        if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(&wic))) && wic &&
            SUCCEEDED(wic->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &dec)))
            ok = DecodeFramesRGBA(wic, dec, true, px, w, h);
        if (dec) dec->Release();
        if (wic) wic->Release();
        stream->Release();
        return ok;
    }

    // ---- DX12 上传 ----
    static void* Upload12(const BYTE* px, UINT w, UINT h, unsigned* outSlot, ID3D12Resource** outRes)
    {
        if (!s_dev12)
            return nullptr;
        unsigned slot = 0;
        D3D12_CPU_DESCRIPTOR_HANDLE cpuH = {};
        D3D12_GPU_DESCRIPTOR_HANDLE gpuH = {};
        if (!AllocSlot(&slot, &cpuH, &gpuH))
            return nullptr;

        D3D12_RESOURCE_DESC rd = {};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width = w;
        rd.Height = h;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;

        ID3D12Resource* tex = nullptr;
        if (FAILED(s_dev12->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                                                    D3D12_RESOURCE_STATE_COPY_DEST,
                                                    nullptr, IID_PPV_ARGS(&tex))) || !tex)
            return nullptr;

        UINT64 total = 0;
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {};
        UINT rows = 0;
        UINT64 rowPitch = 0;
        s_dev12->GetCopyableFootprints(&rd, 0, 1, 0, &fp, &rows, &rowPitch, &total);

        D3D12_HEAP_PROPERTIES up = {};
        up.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC ud = {};
        ud.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        ud.Width = total;
        ud.Height = 1;
        ud.DepthOrArraySize = 1;
        ud.MipLevels = 1;
        ud.SampleDesc.Count = 1;
        ud.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        ID3D12Resource* upload = nullptr;
        if (FAILED(s_dev12->CreateCommittedResource(&up, D3D12_HEAP_FLAG_NONE, &ud,
                                                    D3D12_RESOURCE_STATE_GENERIC_READ,
                                                    nullptr, IID_PPV_ARGS(&upload))) || !upload)
        {
            tex->Release();
            return nullptr;
        }
        void* mapped = nullptr;
        D3D12_RANGE rr = { 0, 0 };
        if (FAILED(upload->Map(0, &rr, &mapped)) || !mapped)
        {
            upload->Release();
            tex->Release();
            return nullptr;
        }
        for (UINT y = 0; y < rows; y++)
            memcpy((BYTE*)mapped + fp.Offset + (UINT64)y * fp.Footprint.RowPitch,
                   px + (size_t)y * w * 4, (size_t)w * 4);
        upload->Unmap(0, nullptr);

        // 上传的命令串行化交给 RenderHook（ExecuteUpload 回调）——
        // 简化：这里直接用独立队列创建一次（封面装载低频，可接受）
        D3D12_COMMAND_QUEUE_DESC qd = {};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        ID3D12CommandQueue* q = nullptr;
        if (FAILED(s_dev12->CreateCommandQueue(&qd, IID_PPV_ARGS(&q))) || !q)
        {
            upload->Release();
            tex->Release();
            return nullptr;
        }
        if (outRes)
            *outRes = nullptr;
        ID3D12CommandAllocator* alloc = nullptr;
        ID3D12GraphicsCommandList* list = nullptr;
        bool ok = false;
        if (SUCCEEDED(s_dev12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                      IID_PPV_ARGS(&alloc))) &&
            SUCCEEDED(s_dev12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc,
                                                 nullptr, IID_PPV_ARGS(&list))) && list)
        {
            D3D12_TEXTURE_COPY_LOCATION dst = {};
            dst.pResource = tex;
            dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            D3D12_TEXTURE_COPY_LOCATION src = {};
            src.pResource = upload;
            src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint = fp;
            list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            D3D12_RESOURCE_BARRIER b = {};
            b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            b.Transition.pResource = tex;
            b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            list->ResourceBarrier(1, &b);
            list->Close();
            q->ExecuteCommandLists(1, (ID3D12CommandList* const*)&list);

            // 同步等待（低频路径，简单可靠）
            ID3D12Fence* fence = nullptr;
            if (SUCCEEDED(s_dev12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))) && fence)
            {
                HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
                q->Signal(fence, 1);
                fence->SetEventOnCompletion(1, ev);
                WaitForSingleObject(ev, 5000);
                CloseHandle(ev);
                fence->Release();
            }
            list->Release();
            ok = true;
        }
        if (alloc) alloc->Release();
        q->Release();
        upload->Release();
        if (!ok)
        {
            tex->Release();
            return nullptr;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC sd = {};
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sd.Texture2D.MipLevels = 1;
        s_dev12->CreateShaderResourceView(tex, &sd, cpuH);

        if (outSlot)
            *outSlot = slot;
        if (outRes)
            *outRes = tex;   // 所有权移交调用方
        return (void*)(uintptr_t)gpuH.ptr;
    }

    // ---- DX11 上传 ----
    static void* Upload11(const BYTE* px, UINT w, UINT h)
    {
        if (!s_dev11)
            return nullptr;
        void* srv = nullptr;
        ID3D11Texture2D* tex = nullptr;
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = w;
        td.Height = h;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sd = { px, w * 4, 0 };
        if (SUCCEEDED(s_dev11->CreateTexture2D(&td, &sd, &tex)) && tex)
        {
            s_dev11->CreateShaderResourceView(tex, nullptr, (ID3D11ShaderResourceView**)&srv);
            tex->Release();
        }
        return srv;
    }

    Tex Peek(const std::wstring& path)
    {
        auto it = s_cache.find(path);
        return it != s_cache.end() ? it->second.tex : Tex{};
    }

    bool IsLoaded(const std::wstring& path)
    {
        return s_cache.find(path) != s_cache.end();
    }

    // 上传已解码的像素并写入缓存（key 同时作为 D3D12 描述符槽的登记键）
    static Tex UploadAndCache(const std::wstring& key, bool decoded,
                              std::vector<BYTE>& px, UINT w, UINT h)
    {
        Entry e;
        if (decoded && w > 0 && h > 0)
        {
            unsigned slot = 0;
            ID3D12Resource* res12 = nullptr;
            if (s_dev12)
                e.tex.id = Upload12(px.data(), w, h, &slot, &res12);
            else if (s_dev11)
                e.tex.id = Upload11(px.data(), w, h);
            if (e.tex.id)
            {
                e.tex.w = (int)w;
                e.tex.h = (int)h;
                if (s_dev12)
                    s_slots[key] = { slot, (unsigned long long)(uintptr_t)e.tex.id, res12 };
                Log::Printf("[Tex] loaded %ls (%ux%u)", key.c_str(), w, h);
            }
        }
        if (!e.tex.id)
        {
            e.failed = true;
            Log::Printf("[Tex] FAILED %ls", key.c_str());
        }
        s_cache[key] = e;
        return e.tex;
    }

    Tex Load(const std::wstring& path)
    {
        auto it = s_cache.find(path);
        if (it != s_cache.end())
            return it->second.tex;

        std::vector<BYTE> px;
        UINT w = 0, h = 0;
        bool decoded = DecodeRGBA(path, &px, &w, &h);
        return UploadAndCache(path, decoded, px, w, h);
    }

    // 从内存字节装载贴图（key 仅作为缓存标识，不会访问文件系统）
    Tex LoadFromMemory(const std::wstring& key, const void* data, size_t size)
    {
        auto it = s_cache.find(key);
        if (it != s_cache.end())
            return it->second.tex;

        std::vector<BYTE> px;
        UINT w = 0, h = 0;
        bool decoded = DecodeRGBAFromMemory(data, size, &px, &w, &h);
        return UploadAndCache(key, decoded, px, w, h);
    }

    void Release(const std::wstring& path)
    {
        auto it = s_cache.find(path);
        if (it == s_cache.end())
            return;
        // 描述符槽归还空闲表（贴图资源随引用释放；翻页后不会再被绘制）
        auto sit = s_slots.find(path);
        if (sit != s_slots.end())
        {
            if (sit->second.res)
                sit->second.res->Release();
            FreeSlot(sit->second.slot);
            s_slots.erase(sit);
        }
        else if (s_dev11 && it->second.tex.id)
        {
            ((ID3D11ShaderResourceView*)it->second.tex.id)->Release();
        }
        s_cache.erase(it);
    }

    void ReleaseAll()
    {
        for (auto& kv : s_slots)
        {
            if (kv.second.res)
                kv.second.res->Release();
            FreeSlot(kv.second.slot);
        }
        s_slots.clear();
        if (s_dev11 && !s_dev12)
        {
            for (auto& kv : s_cache)
                if (kv.second.tex.id)
                    ((ID3D11ShaderResourceView*)kv.second.tex.id)->Release();
        }
        s_cache.clear();
    }
}
