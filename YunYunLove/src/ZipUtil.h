#pragma once
// ============================================================
// ZipUtil.h — zip 解包（miniz）与导入流水线
// ============================================================
#include <string>
#include <vector>
#include <functional>

namespace ZipUtil
{
    struct Entry
    {
        std::string name;    // zip 内相对路径（'/'分隔）
        unsigned long long size = 0;
    };

    // 列出 zip 内全部条目；失败返回 false
    bool List(const std::wstring& zipPath, std::vector<Entry>* out);

    // 解压到目标目录（自动建子目录；只解压常规文件）
    bool ExtractAll(const std::wstring& zipPath, const std::wstring& destDir, std::string* error);

    // 读取单个文件到内存
    bool ReadFile(const std::wstring& zipPath, const std::string& entryName, std::string* out);

    // 判断扩展名
    bool HasExt(const std::string& name, const char* extLower);
}
