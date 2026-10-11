// ============================================================
// YunYunLove.dll — 云云症 (Yunyun_Syndrome, Steam, Unity 6000.0.58f2 Mono)
// 自定义 Malody 谱面商城：
//   注入 → 等 Mono 运行时 → 挂 DXGI 覆盖层（ImGui, D3D12/D3D11）
//   → 商城 UI（搜索/导入/分页/详情）→ 构建谱面 + 音频
//   → GameStateManager.GoTo<InGameScene> 进原生游玩。
//
// 快捷键：INSERT 显隐界面；END 自卸载。
// ============================================================
#include "State.h"
#include "Log.h"
#include "MonoApi.h"
#include "MainThread.h"
#include "GameBridge.h"
#include "RenderHook.h"
#include "AppUI.h"

#include <windows.h>
#include <atomic>

namespace YYState
{
    std::atomic<bool> ExitRequested{ false };
    std::atomic<bool> GameQuitting{ false };
}

void YYState::RequestExit()
{
    ExitRequested.store(true, std::memory_order_relaxed);
}

static HMODULE g_hSelf = nullptr;

// ---------------- 卸载流程（worker 线程） ----------------
static void CleanupAndUnload()
{
    Log::Printf("[Main] exit requested, cleaning up...");

    // 1) 渲染侧停手
    YYState::ExitRequested.store(true, std::memory_order_relaxed);
    Sleep(250);

    // 2) 摘 WndProc / ImGui / DXGI 钩子
    RenderHook::Shutdown();

    // 3) 等待线程离开本模块代码后自卸载
    Sleep(400);
    Log::Printf("[Main] unloading self");
    HMODULE h = g_hSelf;
    FreeLibraryAndExitThread(h, 0);
}

// ---------------- worker 线程 ----------------
static DWORD WINAPI WorkerMain(LPVOID)
{
    Log::Init();
    Log::Printf("[Main] injected, module=%p", (void*)g_hSelf);

    // 被动模式（隔离注入本身的影响）
    {
        char env[8] = {};
        if (GetEnvironmentVariableA("YYL_PASSIVE", env, 8) > 0 && env[0] == '1')
        {
            Log::Printf("[Main] YYL_PASSIVE=1 — passive mode");
            while (!YYState::ExitRequested.load(std::memory_order_relaxed))
                Sleep(200);
            return 0;
        }
    }

    // 等 Mono 运行时（游戏启动后 ~120s 窗口）
    for (int i = 0; i < 600 && !GetModuleHandleW(L"mono-2.0-bdwgc.dll"); i++)
        Sleep(200);
    if (!GetModuleHandleW(L"mono-2.0-bdwgc.dll"))
    {
        Log::Printf("[Main] FATAL: mono runtime not found after 120s");
        return 0;
    }
    if (!GameBridge::InitNative())
    {
        Log::Printf("[Main] FATAL: MonoApi init failed");
        return 0;
    }

    // 商城数据层（建目录/扫描）
    AppUI::InitBackend();

    // 渲染钩子（覆盖层）。失败则只留桥（无法交互，仅日志）。
    for (int i = 0; i < 600 && !RenderHook::Install(); i++)
    {
        if (YYState::ExitRequested.load(std::memory_order_relaxed))
            return 0;
        Sleep(200);
    }

    Log::Printf("[Main] ready — INSERT for menu");

    while (!YYState::ExitRequested.load(std::memory_order_relaxed))
        Sleep(100);

    CleanupAndUnload();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        g_hSelf = hModule;
        DisableThreadLibraryCalls(hModule);
        if (!CreateThread(nullptr, 0, WorkerMain, nullptr, 0, nullptr))
            return FALSE;
        break;
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}
