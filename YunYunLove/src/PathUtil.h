#pragma once
// ============================================================
// 超长路径（> MAX_PATH / 260）支持。
// Win32 宽字符文件 API（_wfopen / std::ifstream / GetFileAttributesW / WIC /
// CreateDirectoryW ...）在路径超过 260 字符时会直接失败。给「已是绝对且已规范」
// 的路径加 "\\?\" 前缀即可绕过该限制（前缀会关闭路径规范化，故路径中的分隔符
// 必须是 '\\'，且不能含 '.' / '..' 段——本工程的路径都是这样拼出来的）。
// ============================================================
#include <string>
#include <windows.h>

namespace PathUtil
{
    inline std::wstring Long(const std::wstring& path)
    {
        if (path.size() < 248)
            return path;                                  // 未接近上限：原样返回
        if (path.rfind(L"\\\\?\\", 0) == 0)
            return path;                                  // 已有前缀
        if (path.size() >= 2 && path[1] == L':')
            return L"\\\\?\\" + path;                     // 盘符绝对路径  C:\...
        if (path.rfind(L"\\\\", 0) == 0)
            return L"\\\\?\\UNC\\" + path.substr(2);      // UNC  \\server\share
        return path;                                      // 其它（相对路径等）原样返回
    }
}