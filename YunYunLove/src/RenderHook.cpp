#include "RenderHook.h"
#include "OverlayTex.h"
#include "Theme.h"
#include "AppUI.h"
#include "Log.h"
#include "MainThread.h"
#include "Config.h"
#include "State.h"
#include "GameBridge.h"

#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <detours/detours.h>
#include <atomic>
#include <cstring>
#include <string>

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx12.h"
#include "backends/imgui_impl_dx11.h"

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ============================================================
// 挂钩方式：哑设备取 IDXGISwapChain::Present / Present1 / ResizeBuffers
// 与 ID3D12CommandQueue::ExecuteCommandLists 的 vtable 地址，Detours 挂钩。
// D3D12 为云云症本体的图形 API（ECL 钩子捕获 Unity 直队列驱动 ImGui）；
// D3D11 全套兜底，游戏若切回 D3D11 也能工作。
// ============================================================
namespace RenderHook
{
    typedef HRESULT(STDMETHODCALLTYPE* Present_t)(IDXGISwapChain*, UINT, UINT);
    typedef HRESULT(STDMETHODCALLTYPE* Present1_t)(IDXGISwapChain*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
    typedef HRESULT(STDMETHODCALLTYPE* ResizeBuffers_t)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
    typedef void(STDMETHODCALLTYPE* ECL_t)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);

    static constexpr int kIdxPresent = 8;
    static constexpr int kIdxResizeBuffers = 13;
    static constexpr int kIdxPresent1 = 18;
    static constexpr int kIdxECL = 10;

    static constexpr UINT kNumBackBuffers = 4;
    static constexpr UINT kNumFramesInFlight = 2;
    static constexpr UINT kSrvHeapSize = 512;   // 商城封面并发

    static Present_t       g_oPresent = nullptr;
    static Present1_t      g_oPresent1 = nullptr;
    static ResizeBuffers_t g_oResizeBuffers = nullptr;
    static ECL_t           g_oECL = nullptr;
    static bool g_hooksInstalled = false;

    enum class Api { Unknown, D3D11, D3D12 };
    static std::atomic<Api> g_api{ Api::Unknown };
    static std::atomic<bool> g_imguiReady{ false };
    static bool g_initFailed = false;
    static std::atomic<bool> g_menuVisible{ true };

    static HWND g_hwnd = nullptr;
    static WNDPROC g_origWndProc = nullptr;
    static IDXGISwapChain* g_sc = nullptr;
    static IDXGISwapChain3* g_sc3 = nullptr;
    static HWND g_scHwnd = nullptr;

    // ---- D3D12 状态 ----
    static ID3D12Device* g_dev12 = nullptr;
    static ID3D12CommandQueue* g_queue12 = nullptr;    // ECL 捕获的 Unity 直队列
    static ID3D12DescriptorHeap* g_rtvHeap12 = nullptr;
    static ID3D12DescriptorHeap* g_srvHeap12 = nullptr;
    static D3D12_CPU_DESCRIPTOR_HANDLE g_rtvStart12{};
    static UINT g_rtvInc12 = 0;
    static ID3D12Resource* g_rtResources12[kNumBackBuffers] = {};
    static bool g_rtValid12 = false;

    struct FrameCtx12
    {
        ID3D12CommandAllocator* allocator = nullptr;
        UINT64 fenceValue = 0;
    };
    static FrameCtx12 g_frames12[kNumFramesInFlight];
    static ID3D12GraphicsCommandList* g_cmdList12 = nullptr;
    static ID3D12Fence* g_fence12 = nullptr;
    static HANDLE g_fenceEvent12 = nullptr;
    static UINT64 g_fenceLast = 0;

    // ---- D3D11 状态 ----
    static ID3D11Device* g_dev11 = nullptr;
    static ID3D11DeviceContext* g_ctx11 = nullptr;
    static ID3D11RenderTargetView* g_rtv11[kNumBackBuffers] = {};
    static bool g_rtValid11 = false;

    // ECL 捕获防自触发
    static thread_local bool t_inOurRender = false;

    // ---- 主线程初始化消息 ----
    static constexpr UINT kMsgBridgeInit = WM_APP + 0x5C7;   // 自定义：YunYunLove 泵

    static void WINAPI PostBridgeInit()
    {
        if (g_hwnd)
            PostMessageW(g_hwnd, kMsgBridgeInit, 0, 0);
    }

    // ---- 工具 ----
    static bool IsUnityWindow(HWND h)
    {
        if (!h) return false;
        wchar_t cls[64] = {};
        GetClassNameW(h, cls, 64);
        return wcsncmp(cls, L"UnityWndClass", 13) == 0;
    }

    // ---- WndProc ----
    static LRESULT WINAPI HK_WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        if (msg == WM_CLOSE || msg == WM_DESTROY || msg == WM_NCDESTROY ||
            msg == WM_QUERYENDSESSION || msg == WM_ENDSESSION)
        {
            if (!YYState::GameQuitting.exchange(true, std::memory_order_relaxed))
                Log::Printf("[Render] game shutdown detected (msg=0x%04X)", (unsigned)msg);
            return CallWindowProcW(g_origWndProc, hwnd, msg, wp, lp);
        }
        if (YYState::GameQuitting.load(std::memory_order_relaxed))
            return CallWindowProcW(g_origWndProc, hwnd, msg, wp, lp);

        if (msg == kMsgBridgeInit)
        {
            MainThread::Pump();          // 游戏主线程：mono 任务泵
            GameBridge::TickMainThread();   // 游戏侧主线程逻辑（音频轮询/状态发布）
            return 0;
        }

        if (g_imguiReady.load(std::memory_order_relaxed))
        {
            if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp))
                return 1;

            if (msg == WM_KEYDOWN && wp == VK_INSERT)
            {
                bool v = g_menuVisible.load(std::memory_order_relaxed);
                g_menuVisible.store(!v, std::memory_order_relaxed);
            }
            else if (msg == WM_KEYDOWN && wp == VK_END)
            {
                Log::Printf("[Render] END pressed — requesting unload");
                YYState::RequestExit();
                return 0;
            }
            else if (g_menuVisible.load(std::memory_order_relaxed) &&
                     ImGui::GetIO().WantCaptureMouse)
            {
                switch (msg)
                {
                case WM_LBUTTONDOWN: case WM_LBUTTONUP:
                case WM_RBUTTONDOWN: case WM_RBUTTONUP:
                case WM_MBUTTONDOWN: case WM_MBUTTONUP:
                case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
                case WM_MOUSEMOVE:
                    return 0;   // 菜单打开且指针悬停：吞掉鼠标，防止穿到游戏
                }
            }
        }
        return CallWindowProcW(g_origWndProc, hwnd, msg, wp, lp);
    }

    // ---- 渲染目标管理 ----
    static void ReleaseRT12()
    {
        for (auto& r : g_rtResources12)
        {
            if (r) { r->Release(); r = nullptr; }
        }
        g_rtValid12 = false;
    }

    static bool EnsureRT12(IDXGISwapChain3* sc3)
    {
        if (g_rtValid12)
            return true;
        DXGI_SWAP_CHAIN_DESC desc{};
        sc3->GetDesc(&desc);
        UINT count = desc.BufferCount > kNumBackBuffers ? kNumBackBuffers : desc.BufferCount;
        for (UINT i = 0; i < count; i++)
        {
            ID3D12Resource* res = nullptr;
            if (FAILED(sc3->GetBuffer(i, IID_PPV_ARGS(&res))))
                return false;
            D3D12_CPU_DESCRIPTOR_HANDLE h = g_rtvStart12;
            h.ptr += (SIZE_T)i * g_rtvInc12;
            g_dev12->CreateRenderTargetView(res, nullptr, h);
            g_rtResources12[i] = res;
        }
        g_rtValid12 = true;
        return true;
    }

    static void ReleaseRT11()
    {
        for (auto& v : g_rtv11)
        {
            if (v) { v->Release(); v = nullptr; }
        }
        g_rtValid11 = false;
    }

    static bool EnsureRT11(IDXGISwapChain* sc)
    {
        if (g_rtValid11)
            return true;
        DXGI_SWAP_CHAIN_DESC desc{};
        sc->GetDesc(&desc);
        UINT count = desc.BufferCount > kNumBackBuffers ? kNumBackBuffers : desc.BufferCount;
        for (UINT i = 0; i < count; i++)
        {
            ID3D11Texture2D* surf = nullptr;
            if (FAILED(sc->GetBuffer(i, IID_PPV_ARGS(&surf))))
                return false;
            g_dev11->CreateRenderTargetView(surf, nullptr, &g_rtv11[i]);
            surf->Release();
        }
        g_rtValid11 = true;
        return true;
    }

    // ---- 字体 ----
    static void BuildFonts()
    {
        ImGuiIO& io = ImGui::GetIO();
        char winDir[MAX_PATH] = {};
        GetWindowsDirectoryA(winDir, MAX_PATH);
        std::string fontDir = winDir;

        // 微软雅黑（简体中文 UI + 日文假名 + 常用汉字；缺失字形 1.92 动态补载）
        std::string regular = fontDir + "\\Fonts\\msyh.ttc";
        std::string bold = fontDir + "\\Fonts\\msyhbd.ttc";

        const ImWchar* cjk = io.Fonts->GetGlyphRangesChineseSimplifiedCommon();
        ImFontConfig cfg;
        cfg.OversampleH = 2;
        cfg.OversampleV = 2;

        const char* boldPath = (GetFileAttributesA(bold.c_str()) != INVALID_FILE_ATTRIBUTES) ? bold.c_str()
                                                                                            : regular.c_str();
        Theme::Fonts[Theme::F_Normal] = io.Fonts->AddFontFromFileTTF(regular.c_str(), 16.0f, &cfg, cjk);
        Theme::Fonts[Theme::F_Small]  = io.Fonts->AddFontFromFileTTF(regular.c_str(), 14.0f, &cfg, cjk);
        Theme::Fonts[Theme::F_Bold]   = io.Fonts->AddFontFromFileTTF(boldPath, 20.0f, &cfg, cjk);
        Theme::Fonts[Theme::F_Title]  = io.Fonts->AddFontFromFileTTF(boldPath, 26.0f, &cfg, cjk);
        Theme::Fonts[Theme::F_Medium] = io.Fonts->AddFontFromFileTTF(regular.c_str(), 17.0f, &cfg, cjk);
    }

    // ---- ImGui 初始化 ----
    static bool InitImGui(IDXGISwapChain* sc, Api api)
    {
        DXGI_SWAP_CHAIN_DESC desc{};
        sc->GetDesc(&desc);
        g_hwnd = desc.OutputWindow;

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;    // 不在游戏目录落 imgui.ini
        // 覆盖层为固定尺寸绝对布局：ImGui 1.92 的可恢复错误（如绝对定位边界）
        // 走日志恢复，不弹断言中断游戏。内部 sanity 断言（IM_ASSERT）保留。
        ImGui::GetIO().ConfigErrorRecoveryEnableAssert = false;

        BuildFonts();
        Theme::Apply();

        if (!ImGui_ImplWin32_Init((void*)g_hwnd))
        {
            Log::Printf("[Render] ImGui_ImplWin32_Init failed");
            return false;
        }

        if (api == Api::D3D12)
        {
            D3D12_DESCRIPTOR_HEAP_DESC srvDesc = {};
            srvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            srvDesc.NumDescriptors = kSrvHeapSize;
            srvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            if (FAILED(g_dev12->CreateDescriptorHeap(&srvDesc, IID_PPV_ARGS(&g_srvHeap12))))
            {
                Log::Printf("[Render] CreateDescriptorHeap(SRV) failed");
                return false;
            }
            D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
            rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            rtvDesc.NumDescriptors = kNumBackBuffers;
            if (FAILED(g_dev12->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&g_rtvHeap12))))
            {
                Log::Printf("[Render] CreateDescriptorHeap(RTV) failed");
                return false;
            }
            g_rtvStart12 = g_rtvHeap12->GetCPUDescriptorHandleForHeapStart();
            g_rtvInc12 = g_dev12->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

            for (UINT i = 0; i < kNumFramesInFlight; i++)
            {
                if (FAILED(g_dev12->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                           IID_PPV_ARGS(&g_frames12[i].allocator))))
                {
                    Log::Printf("[Render] CreateCommandAllocator failed");
                    return false;
                }
            }
            if (FAILED(g_dev12->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                  g_frames12[0].allocator, nullptr,
                                                  IID_PPV_ARGS(&g_cmdList12))) ||
                FAILED(g_cmdList12->Close()))
            {
                Log::Printf("[Render] CreateCommandList failed");
                return false;
            }
            if (FAILED(g_dev12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence12))))
            {
                Log::Printf("[Render] CreateFence failed");
                return false;
            }
            g_fenceEvent12 = CreateEventW(nullptr, FALSE, FALSE, nullptr);

            ImGui_ImplDX12_InitInfo info = {};
            info.Device = g_dev12;
            info.CommandQueue = g_queue12;
            info.NumFramesInFlight = (int)kNumFramesInFlight;
            info.RTVFormat = desc.BufferDesc.Format;
            info.DSVFormat = DXGI_FORMAT_UNKNOWN;
            info.SrvDescriptorHeap = g_srvHeap12;
            // 1.92 的分配回调：给 ImGui 一个保留槽（槽 0 永久给 ImGui 自身字体等）
            static D3D12_CPU_DESCRIPTOR_HANDLE s_cpu = g_srvHeap12->GetCPUDescriptorHandleForHeapStart();
            static D3D12_GPU_DESCRIPTOR_HANDLE s_gpu = g_srvHeap12->GetGPUDescriptorHandleForHeapStart();
            static UINT s_inc = g_dev12->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            static int s_next = 0;   // 从 0 号开始线性分配（ImGui 保留低段；OverlayTex 从高段分配）
            info.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE* cpu, D3D12_GPU_DESCRIPTOR_HANDLE* gpu) {
                int slot = s_next++;
                cpu->ptr = s_cpu.ptr + (SIZE_T)(slot + 0) * s_inc;
                gpu->ptr = s_gpu.ptr + (UINT64)(slot + 0) * s_inc;
            };
            info.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE, D3D12_GPU_DESCRIPTOR_HANDLE) {};
            if (!ImGui_ImplDX12_Init(&info))
            {
                Log::Printf("[Render] ImGui_ImplDX12_Init failed");
                return false;
            }
            // 贴图缓存从堆的高段分配，避免与 ImGui 冲突
            OverlayTex::BackendReady(g_dev12, nullptr, nullptr, g_srvHeap12);
        }
        else
        {
            if (!g_dev11 || !g_ctx11)
                return false;
            if (!ImGui_ImplDX11_Init(g_dev11, g_ctx11))
            {
                Log::Printf("[Render] ImGui_ImplDX11_Init failed");
                return false;
            }
            OverlayTex::BackendReady(nullptr, g_dev11, g_ctx11, nullptr);
        }

        g_api.store(api, std::memory_order_release);
        g_imguiReady.store(true, std::memory_order_release);

        g_origWndProc = (WNDPROC)SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)HK_WndProc);
        MainThread::SetPoster(&PostBridgeInit);

        Log::Printf("[Render] ImGui ready (api=%s hwnd=%p)",
                    api == Api::D3D12 ? "D3D12" : "D3D11", (void*)g_hwnd);
        return true;
    }

    // ---- 每帧 ----
    static void RenderDX12(IDXGISwapChain3* sc3)
    {
        UINT bb = sc3->GetCurrentBackBufferIndex();
        FrameCtx12& fc = g_frames12[bb % kNumFramesInFlight];

        if (g_fence12->GetCompletedValue() < fc.fenceValue)
        {
            g_fence12->SetEventOnCompletion(fc.fenceValue, g_fenceEvent12);
            WaitForSingleObject(g_fenceEvent12, 1000);
        }
        if (!EnsureRT12(sc3))
            return;
        ID3D12Resource* res = g_rtResources12[bb];
        if (!res)
            return;

        fc.allocator->Reset();
        g_cmdList12->Reset(fc.allocator, nullptr);

        D3D12_RESOURCE_BARRIER b = {};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = res;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        g_cmdList12->ResourceBarrier(1, &b);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = g_rtvStart12;
        rtv.ptr += (SIZE_T)bb * g_rtvInc12;
        g_cmdList12->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        g_cmdList12->SetDescriptorHeaps(1, &g_srvHeap12);

        ImGui_ImplDX12_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        AppUI::Draw();
        ImGui::Render();
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_cmdList12);

        b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        g_cmdList12->ResourceBarrier(1, &b);
        g_cmdList12->Close();

        t_inOurRender = true;
        g_queue12->ExecuteCommandLists(1, (ID3D12CommandList* const*)&g_cmdList12);
        t_inOurRender = false;

        g_queue12->Signal(g_fence12, ++g_fenceLast);
        fc.fenceValue = g_fenceLast;
    }

    static void RenderDX11(IDXGISwapChain* sc)
    {
        if (!EnsureRT11(sc))
            return;
        UINT bb = 0;
        {
            IDXGISwapChain3* sc3 = nullptr;
            if (SUCCEEDED(sc->QueryInterface(IID_PPV_ARGS(&sc3))))
            {
                bb = sc3->GetCurrentBackBufferIndex();
                sc3->Release();
            }
        }
        bb %= kNumBackBuffers;
        if (!g_rtv11[bb])
            return;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        AppUI::Draw();
        ImGui::Render();
        g_ctx11->OMSetRenderTargets(1, &g_rtv11[bb], nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        ID3D11RenderTargetView* nullRTV = nullptr;
        g_ctx11->OMSetRenderTargets(1, &nullRTV, nullptr);
    }

    static void ProcessFrame(IDXGISwapChain* sc, UINT flags)
    {
        if (flags & DXGI_PRESENT_TEST)
            return;


        // 诊断：未就绪时每 3 秒打一行 Present 心跳（确认钩子确实被游戏调用）
        if (!g_imguiReady.load(std::memory_order_relaxed))
        {
            static DWORD s_lastBeat = 0;
            DWORD nowBeat = GetTickCount();
            if (nowBeat - s_lastBeat > 3000)
            {
                s_lastBeat = nowBeat;
                wchar_t cls[64] = {};
                GetClassNameW(g_scHwnd, cls, 64);
                Log::Printf("[Render] present beat hwnd=%p cls=%ls", (void*)g_scHwnd, cls);
            }
        }
        {
            DXGI_SWAP_CHAIN_DESC sd{};
            if (SUCCEEDED(sc->GetDesc(&sd)) && sd.OutputWindow)
                g_scHwnd = sd.OutputWindow;
        }

        // 主线程泵心跳：空闲时也保持 GameBridge Tick（状态发布/音频轮询）
        static DWORD s_lastKick = 0;
        DWORD nowKick = GetTickCount();
        if (nowKick - s_lastKick > 15)
        {
            s_lastKick = nowKick;
            PostBridgeInit();
        }

        if (YYState::GameQuitting.load(std::memory_order_relaxed))
            return;

        // 游玩中自动隐藏（设置可关）
        bool wantDraw = g_menuVisible.load(std::memory_order_relaxed);
        if (wantDraw && Config::AutoHideInGame() && AppUI::IsInGameplay())
            wantDraw = false;
        AppUI::SetVisible(wantDraw);
        AppUI::TickFrame();

        {
            static bool s_loggedSize = false;
            if (!s_loggedSize && g_imguiReady.load(std::memory_order_acquire))
            {
                s_loggedSize = true;
                ImVec2 ds = ImGui::GetIO().DisplaySize;
                RECT rc{};
                if (g_hwnd) GetClientRect(g_hwnd, &rc);
                Log::Printf("[Render] DisplaySize=%.0fx%.0f client=%ldx%ld hwnd=%p",
                            ds.x, ds.y, rc.right - rc.left, rc.bottom - rc.top, (void*)g_hwnd);
            }
        }

        if (!g_imguiReady.load(std::memory_order_acquire))
        {
            if (g_initFailed || !wantDraw)
                return;
            DXGI_SWAP_CHAIN_DESC desc{};
            sc->GetDesc(&desc);
            if (!IsUnityWindow(desc.OutputWindow))
            {
                static bool s_loggedOnce = false;
                if (!s_loggedOnce)
                {
                    s_loggedOnce = true;
                    wchar_t cls[64] = {};
                    GetClassNameW(desc.OutputWindow, cls, 64);
                    Log::Printf("[Render] non-unity swapchain hwnd=%p cls=%ls", (void*)desc.OutputWindow, cls);
                }
                return;
            }

            Api api = Api::Unknown;
            if (SUCCEEDED(sc->GetDevice(IID_ID3D12Device, (void**)&g_dev12)) && g_dev12)
                api = Api::D3D12;
            else if (SUCCEEDED(sc->GetDevice(IID_ID3D11Device, (void**)&g_dev11)) && g_dev11)
            {
                api = Api::D3D11;
                g_dev11->GetImmediateContext(&g_ctx11);
            }
            if (api == Api::Unknown)
            {
                Log::Printf("[Render] unknown device type; overlay disabled");
                g_initFailed = true;
                return;
            }
            if (api == Api::D3D12 && !g_queue12)
            {
                g_dev12->Release();
                g_dev12 = nullptr;
                return;   // 等 ECL 捕获直队列
            }

            sc->QueryInterface(IID_PPV_ARGS(&g_sc3));
            g_sc = sc;

            if (!InitImGui(sc, api))
            {
                Log::Printf("[Render] ImGui init failed; overlay disabled");
                g_initFailed = true;
            }
            return;
        }

        // 交换链重建（切分辨率/全屏）
        if (sc != g_sc)
        {
            DXGI_SWAP_CHAIN_DESC desc{};
            sc->GetDesc(&desc);
            if (IsUnityWindow(desc.OutputWindow))
            {
                ReleaseRT12();
                ReleaseRT11();
                OverlayTex::ReleaseAll();
                if (g_sc3) { g_sc3->Release(); g_sc3 = nullptr; }
                sc->QueryInterface(IID_PPV_ARGS(&g_sc3));
                g_sc = sc;
                Log::Printf("[Render] swapchain replaced -> re-targeted");
            }
            else
            {
                return;
            }
        }

        if (!wantDraw)
            return;   // 隐藏时不画（省 GPU；主线程泵照常走）

        if (g_api.load(std::memory_order_relaxed) == Api::D3D12 && g_sc3)
            RenderDX12(g_sc3);
        else if (g_api.load(std::memory_order_relaxed) == Api::D3D11)
            RenderDX11(sc);
    }

    // ---- 钩子体 ----
    static HRESULT WINAPI HK_Present(IDXGISwapChain* sc, UINT sync, UINT flags)
    {
        if (!YYState::GameQuitting.load(std::memory_order_relaxed))
            ProcessFrame(sc, flags);
        return g_oPresent(sc, sync, flags);
    }

    static HRESULT WINAPI HK_Present1(IDXGISwapChain* sc, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS* pp)
    {
        if (!YYState::GameQuitting.load(std::memory_order_relaxed))
            ProcessFrame(sc, flags);
        return g_oPresent1(sc, sync, flags, pp);
    }

    static HRESULT WINAPI HK_ResizeBuffers(IDXGISwapChain* sc, UINT bufCount, UINT w, UINT h, DXGI_FORMAT fmt, UINT flags)
    {
        ReleaseRT12();
        ReleaseRT11();
        OverlayTex::ReleaseAll();
        return g_oResizeBuffers(sc, bufCount, w, h, fmt, flags);
    }

    static void STDMETHODCALLTYPE HK_ECL(ID3D12CommandQueue* queue, UINT count, ID3D12CommandList* const* lists)
    {
        if (!g_queue12 && !t_inOurRender)
        {
            D3D12_COMMAND_QUEUE_DESC d = queue->GetDesc();
            if (d.Type == D3D12_COMMAND_LIST_TYPE_DIRECT)
            {
                g_queue12 = queue;
                g_queue12->AddRef();
                Log::Printf("[Render] captured Unity direct queue = %p", (void*)queue);
            }
        }
        g_oECL(queue, count, lists);
    }

    // ---- 哑设备取 vtable ----
    template <typename T>
    static void* VTableIdx(T obj, int idx)
    {
        return (*(void***)obj)[idx];
    }

    static HWND MakeDummyWindow()
    {
        return CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                               L"STATIC", L"", WS_POPUP, 0, 0, 8, 8,
                               nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    }

    static bool DummyD3D12(void** presentAddr, void** present1Addr, void** resizeAddr, void** eclAddr)
    {
        *presentAddr = *present1Addr = *resizeAddr = *eclAddr = nullptr;
        HWND w = MakeDummyWindow();
        if (!w)
            return false;
        ID3D12Device* dev = nullptr;
        bool ok = false;
        if (SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev))) && dev)
        {
            D3D12_COMMAND_QUEUE_DESC qd = {};
            qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            ID3D12CommandQueue* q = nullptr;
            IDXGIFactory2* factory = nullptr;
            IDXGISwapChain1* sc = nullptr;
            if (SUCCEEDED(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&q))) && q &&
                SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))) && factory)
            {
                DXGI_SWAP_CHAIN_DESC1 sd = {};
                sd.BufferCount = 2;
                sd.Width = 8;
                sd.Height = 8;
                sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
                sd.SampleDesc.Count = 1;
                sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
                if (SUCCEEDED(factory->CreateSwapChainForHwnd(q, w, &sd, nullptr, nullptr, &sc)) && sc)
                {
                    IDXGISwapChain* sc0 = nullptr;
                    if (SUCCEEDED(sc->QueryInterface(IID_PPV_ARGS(&sc0))))
                    {
                        *presentAddr = VTableIdx(sc0, kIdxPresent);
                        *resizeAddr = VTableIdx(sc0, kIdxResizeBuffers);
                        sc0->Release();
                    }
                    // Present1 与 Present 同一 COM 对象 vtable，直接用 IDXGISwapChain1 指针取槽 18
                    *present1Addr = VTableIdx(sc, kIdxPresent1);
                    *eclAddr = VTableIdx(q, kIdxECL);
                    ok = *presentAddr != nullptr;
                    sc->Release();
                }
                factory->Release();
            }
            if (q) q->Release();
            dev->Release();
        }
        DestroyWindow(w);
        return ok;
    }

    static bool DummyD3D11(void** presentAddr, void** resizeAddr)
    {
        *presentAddr = *resizeAddr = nullptr;
        HWND w = MakeDummyWindow();
        if (!w)
            return false;
        DXGI_SWAP_CHAIN_DESC sd = {};
        sd.BufferCount = 2;
        sd.BufferDesc.Width = 8;
        sd.BufferDesc.Height = 8;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = w;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

        IDXGISwapChain* sc = nullptr;
        ID3D11Device* dev = nullptr;
        const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
        HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                                   levels, 2, D3D11_SDK_VERSION, &sd, &sc, &dev,
                                                   nullptr, nullptr);
        if (FAILED(hr))
        {
            sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
            hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                               levels, 2, D3D11_SDK_VERSION, &sd, &sc, &dev,
                                               nullptr, nullptr);
        }
        if (SUCCEEDED(hr) && sc)
        {
            *presentAddr = VTableIdx(sc, kIdxPresent);
            *resizeAddr = VTableIdx(sc, kIdxResizeBuffers);
            sc->Release();
        }
        if (dev) dev->Release();
        DestroyWindow(w);
        return SUCCEEDED(hr);
    }

    // ---- 安装 ----
    bool Install()
    {
        if (g_hooksInstalled)
            return true;
        if (!GetModuleHandleW(L"dxgi.dll"))
            return false;

        void* p12 = nullptr, * p11 = nullptr, * p1 = nullptr, * rz12 = nullptr, * rz11 = nullptr, * ecl = nullptr;
        DummyD3D12(&p12, &p1, &rz12, &ecl);
        DummyD3D11(&p11, &rz11);

        void* presentAddr = p12 ? p12 : p11;
        void* resizeAddr = rz12 ? rz12 : rz11;
        if (!presentAddr)
        {
            Log::Printf("[Render] Present address not found");
            return false;
        }

        LONG rv = DetourTransactionBegin();
        if (rv != NO_ERROR)
            return false;
        DetourUpdateThread(GetCurrentThread());

        g_oPresent = (Present_t)presentAddr;
        if (DetourAttach((PVOID*)&g_oPresent, (PVOID)HK_Present) != NO_ERROR)
        {
            DetourTransactionAbort();
            return false;
        }
        if (p1 && p1 != presentAddr)
        {
            g_oPresent1 = (Present1_t)p1;
            if (DetourAttach((PVOID*)&g_oPresent1, (PVOID)HK_Present1) != NO_ERROR)
                g_oPresent1 = nullptr;
        }
        if (resizeAddr)
        {
            g_oResizeBuffers = (ResizeBuffers_t)resizeAddr;
            if (DetourAttach((PVOID*)&g_oResizeBuffers, (PVOID)HK_ResizeBuffers) != NO_ERROR)
                g_oResizeBuffers = nullptr;
        }
        if (ecl)
        {
            g_oECL = (ECL_t)ecl;
            if (DetourAttach((PVOID*)&g_oECL, (PVOID)HK_ECL) != NO_ERROR)
                g_oECL = nullptr;
        }
        rv = DetourTransactionCommit();
        if (rv != NO_ERROR)
            return false;

        g_hooksInstalled = true;
        Log::Printf("[Render] hooks installed (Present=%p Present1=%p Resize=%p ECL=%p)",
                    presentAddr, (void*)g_oPresent1, (void*)g_oResizeBuffers, (void*)g_oECL);
        return true;
    }

    // ---- 卸载 ----
    void Shutdown()
    {
        if (!g_hooksInstalled)
            return;
        g_imguiReady.store(false, std::memory_order_release);
        Sleep(150);

        if (g_hwnd && g_origWndProc)
        {
            SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)g_origWndProc);
            g_origWndProc = nullptr;
        }
        if (ImGui::GetCurrentContext())
        {
            if (g_api.load(std::memory_order_relaxed) == Api::D3D12)
                ImGui_ImplDX12_Shutdown();
            else if (g_api.load(std::memory_order_relaxed) == Api::D3D11)
                ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
        }
        ReleaseRT12();
        ReleaseRT11();
        OverlayTex::ReleaseAll();
        if (g_cmdList12) { g_cmdList12->Release(); g_cmdList12 = nullptr; }
        for (auto& fc : g_frames12)
            if (fc.allocator) { fc.allocator->Release(); fc.allocator = nullptr; }
        if (g_fence12) { g_fence12->Release(); g_fence12 = nullptr; }
        if (g_fenceEvent12) { CloseHandle(g_fenceEvent12); g_fenceEvent12 = nullptr; }
        if (g_rtvHeap12) { g_rtvHeap12->Release(); g_rtvHeap12 = nullptr; }
        if (g_srvHeap12) { g_srvHeap12->Release(); g_srvHeap12 = nullptr; }
        if (g_queue12) { g_queue12->Release(); g_queue12 = nullptr; }
        if (g_dev12) { g_dev12->Release(); g_dev12 = nullptr; }
        if (g_ctx11) { g_ctx11->Release(); g_ctx11 = nullptr; }
        if (g_dev11) { g_dev11->Release(); g_dev11 = nullptr; }
        if (g_sc3) { g_sc3->Release(); g_sc3 = nullptr; }
        g_sc = nullptr;

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourDetach((PVOID*)&g_oPresent, (PVOID)HK_Present);
        if (g_oPresent1) DetourDetach((PVOID*)&g_oPresent1, (PVOID)HK_Present1);
        if (g_oResizeBuffers) DetourDetach((PVOID*)&g_oResizeBuffers, (PVOID)HK_ResizeBuffers);
        if (g_oECL) DetourDetach((PVOID*)&g_oECL, (PVOID)HK_ECL);
        DetourTransactionCommit();

        g_hooksInstalled = false;
        Log::Printf("[Render] shutdown complete");
    }

    bool Ready()
    {
        return g_imguiReady.load(std::memory_order_acquire);
    }

    void GetGameWindowSize(float* w, float* h)
    {
        static float s_lastW = 1280.f, s_lastH = 720.f;
        float cw = 0, ch = 0;
        auto client = [](HWND hwnd, float* a, float* b) -> bool {
            if (!hwnd) return false;
            RECT rc;
            if (!GetClientRect(hwnd, &rc)) return false;
            float x = (float)(rc.right - rc.left), y = (float)(rc.bottom - rc.top);
            if (x < 200.f || y < 200.f) return false;
            *a = x; *b = y;
            return true;
        };
        if (!client(g_hwnd, &cw, &ch) && !client(g_scHwnd, &cw, &ch))
        {
            cw = s_lastW;
            ch = s_lastH;
        }
        s_lastW = cw;
        s_lastH = ch;
        *w = cw;
        *h = ch;
    }

    void* LoadTexture(const std::wstring& path)
    {
        return OverlayTex::Load(path).id;
    }

    void* PeekTexture(const std::wstring& path, int* w, int* h)
    {
        OverlayTex::Tex t = OverlayTex::Peek(path);
        if (w) *w = t.w;
        if (h) *h = t.h;
        return t.id;
    }

    void ReleaseTexture(const std::wstring& path)
    {
        OverlayTex::Release(path);
    }

    void ReleaseAllTextures()
    {
        OverlayTex::ReleaseAll();
    }

    void SetGameQuitting()
    {
        YYState::GameQuitting.store(true, std::memory_order_relaxed);
    }
}
