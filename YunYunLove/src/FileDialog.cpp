#include "FileDialog.h"
#include <windows.h>
#include <shobjidl.h>
#include <thread>

namespace FileDialog
{
    void PickZipAsync(std::function<void(std::wstring)> onDone)
    {
        std::thread([onDone = std::move(onDone)] {
            HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            bool com = SUCCEEDED(hr);

            std::wstring result;
            IFileOpenDialog* dlg = nullptr;
            if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                           IID_PPV_ARGS(&dlg))) && dlg)
            {
                DWORD flags;
                dlg->GetOptions(&flags);
                dlg->SetOptions(flags | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
                COMDLG_FILTERSPEC specs[] = {
                    { L"谱面包 (*.zip)", L"*.zip" },
                    { L"所有文件 (*.*)", L"*.*" },
                };
                dlg->SetFileTypes(2, specs);
                dlg->SetFileTypeIndex(1);
                dlg->SetTitle(L"导入 4K 谱面包（Malody .mc / osu!mania .osu）");
                if (dlg->Show(nullptr) == S_OK)
                {
                    IShellItem* item = nullptr;
                    if (SUCCEEDED(dlg->GetResult(&item)) && item)
                    {
                        PWSTR path = nullptr;
                        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path)
                        {
                            result = path;
                            CoTaskMemFree(path);
                        }
                        item->Release();
                    }
                }
                dlg->Release();
            }
            if (com)
                CoUninitialize();
            onDone(result);
        }).detach();
    }
}
