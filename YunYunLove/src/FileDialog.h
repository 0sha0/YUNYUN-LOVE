#pragma once
// ============================================================
// FileDialog.h — 后台线程的原生"打开文件"对话框（不卡渲染）
// ============================================================
#include <string>
#include <functional>

namespace FileDialog
{
    // 异步弹出 zip 选择框。onDone 在后台线程回调（选中路径；取消为空串）。
    void PickZipAsync(std::function<void(std::wstring)> onDone);
}
