#include "MainThread.h"
#include "MonoApi.h"
#include "Log.h"
#include "State.h"
#include <windows.h>
#include <mutex>
#include <deque>

namespace MainThread
{
    // SEH 守卫需无对象展开的函数承载
    static void RunGuarded(const std::function<void()>& fn)
    {
        __try
        {
            fn();
        }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
                      ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
        {
            Log::Printf("[MainThread] task crashed (SEH swallowed)");
        }
    }

    static std::mutex s_mx;
    static std::deque<std::function<void()>> s_queue;
    static void (*s_poster)() = nullptr;
    static DWORD s_mainTid = 0;
    static volatile unsigned long s_lastPump = 0;

    void SetPoster(void (*fn)())
    {
        std::lock_guard<std::mutex> lk(s_mx);
        s_poster = fn;
    }

    void Request(std::function<void()> fn)
    {
        if (MainThread::GameQuitting())
            return;
        bool wake = false;
        {
            std::lock_guard<std::mutex> lk(s_mx);
            wake = s_queue.empty();
            s_queue.emplace_back(std::move(fn));
        }
        if (wake && s_poster && !MainThread::GameQuitting())
            s_poster();
    }

    void Pump()
    {
        s_mainTid = GetCurrentThreadId();
        s_lastPump = GetTickCount();

        // mono 托管线程 attach（幂等：已是托管线程则原样返回）
        if (MonoApi::Ready() && MonoApi::mono_thread_attach && MonoApi::mono_get_root_domain)
        {
            MonoDomain* dom = MonoApi::mono_domain_get ? MonoApi::mono_domain_get() : nullptr;
            if (!dom)
                dom = MonoApi::mono_get_root_domain();
            if (dom)
                MonoApi::mono_thread_attach(dom);
        }

        for (;;)
        {
            std::function<void()> fn;
            {
                std::lock_guard<std::mutex> lk(s_mx);
                if (s_queue.empty())
                    break;
                fn = std::move(s_queue.front());
                s_queue.pop_front();
            }
            if (fn)
                RunGuarded(fn);
        }
    }

    bool OnMainThread()
    {
        return s_mainTid != 0 && GetCurrentThreadId() == s_mainTid;
    }

    unsigned long LastPumpTick() { return s_lastPump; }

    void SetGameQuitting() { YYState::GameQuitting.store(true, std::memory_order_relaxed); }
    bool GameQuitting() { return YYState::GameQuitting.load(std::memory_order_relaxed); }
}
