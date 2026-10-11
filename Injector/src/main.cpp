// ============================================================
// Injector — YunYunLove 注入器
//   用法: Injector.exe [进程名|PID] [DLL 路径]
//   缺省: 找 Yunyun_Syndrome.exe，注入同目录 YunYunLove.dll
// ============================================================
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <cstdio>
#include <string>

#pragma comment(lib, "psapi.lib")

static DWORD FindPidByName(const wchar_t* name)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return 0;
    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof pe;
    DWORD pid = 0;
    SIZE_T bestSize = 0;
    if (Process32FirstW(snap, &pe))
    {
        do
        {
            if (_wcsicmp(pe.szExeFile, name) == 0)
            {
                // 同名多进程（自带启动器行为）时选工作集最大的那个
                SIZE_T sz = 0;
                HANDLE hp = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE,
                                        pe.th32ProcessID);
                if (hp)
                {
                    PROCESS_MEMORY_COUNTERS pmc = {};
                    if (K32GetProcessMemoryInfo(hp, &pmc, sizeof pmc))
                        sz = pmc.WorkingSetSize;
                    CloseHandle(hp);
                }
                if (pid == 0 || sz > bestSize)
                {
                    pid = pe.th32ProcessID;
                    bestSize = sz;
                }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

static bool Inject(DWORD pid, const wchar_t* dllPath)
{
    HANDLE h = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                               PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                           FALSE, pid);
    if (!h)
    {
        wprintf(L"[!] OpenProcess(%lu) failed: %lu\n", pid, GetLastError());
        return false;
    }
    size_t bytes = (wcslen(dllPath) + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(h, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote)
    {
        wprintf(L"[!] VirtualAllocEx failed: %lu\n", GetLastError());
        CloseHandle(h);
        return false;
    }
    if (!WriteProcessMemory(h, remote, dllPath, bytes, nullptr))
    {
        wprintf(L"[!] WriteProcessMemory failed: %lu\n", GetLastError());
        CloseHandle(h);
        return false;
    }
    LPTHREAD_START_ROUTINE loadLib = (LPTHREAD_START_ROUTINE)GetProcAddress(
        GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    if (!loadLib)
    {
        CloseHandle(h);
        return false;
    }
    HANDLE t = CreateRemoteThread(h, nullptr, 0, loadLib, remote, 0, nullptr);
    if (!t)
    {
        wprintf(L"[!] CreateRemoteThread failed: %lu\n", GetLastError());
        CloseHandle(h);
        return false;
    }
    WaitForSingleObject(t, 10000);
    DWORD code = 0;
    GetExitCodeThread(t, &code);
    CloseHandle(t);
    VirtualFreeEx(h, remote, 0, MEM_RELEASE);
    CloseHandle(h);
    wprintf(L"[+] inject done (thread exit=%08lX)\n", code);
    return code != 0;
}

int wmain(int argc, wchar_t** argv)
{
    const wchar_t* procName = L"Yunyun_Syndrome.exe";
    std::wstring dll;

    if (argc >= 2)
        procName = argv[1];
    if (argc >= 3)
        dll = argv[2];
    else
    {
        wchar_t self[MAX_PATH];
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        std::wstring dir = self;
        size_t s = dir.find_last_of(L'\\');
        dll = (s == std::wstring::npos ? L"" : dir.substr(0, s + 1)) + L"YunYunLove.dll";
    }

    DWORD pid = 0;
    if (procName[0] >= L'0' && procName[0] <= L'9' && wcslen(procName) > 3)
        pid = (DWORD)wcstoul(procName, nullptr, 10);
    else
        pid = FindPidByName(procName);

    if (!pid)
    {
        wprintf(L"[!] process not found: %s\n", procName);
        return 1;
    }
    wprintf(L"[*] target pid=%lu\n[*] dll=%s\n", pid, dll.c_str());
    return Inject(pid, dll.c_str()) ? 0 : 2;
}
