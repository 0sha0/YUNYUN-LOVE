#include "ZipUtil.h"
#include "Log.h"
#include "PathUtil.h"
#include <windows.h>
#include "miniz.h"
#include "miniz_zip.h"
#include <algorithm>
#include <cctype>

namespace ZipUtil
{
    static std::wstring Utf8ToWide(const std::string& s)
    {
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        std::wstring w(n, 0);
        if (n > 0)
            MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
        return w;
    }

    bool HasExt(const std::string& name, const char* extLower)
    {
        size_t dot = name.find_last_of('.');
        if (dot == std::string::npos)
            return false;
        std::string ext = name.substr(dot + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        return ext == extLower;
    }

    // 打开 zip：miniz 无宽字符接口，用 _wfopen + cfile 通道
    struct ZipFile
    {
        mz_zip_archive zip = {};
        FILE* fp = nullptr;
        bool ok = false;

        ~ZipFile()
        {
            if (ok)
                mz_zip_reader_end(&zip);
            if (fp)
                fclose(fp);
        }
    };

    static bool ZipOpen(const std::wstring& zipPath, ZipFile* zf)
    {
        zf->fp = _wfopen(PathUtil::Long(zipPath).c_str(), L"rb");
        if (!zf->fp)
            return false;
        _fseeki64(zf->fp, 0, SEEK_END);
        long long sz = _ftelli64(zf->fp);
        _fseeki64(zf->fp, 0, SEEK_SET);
        zf->ok = mz_zip_reader_init_cfile(&zf->zip, zf->fp, (mz_uint64)sz, 0) != 0;
        return zf->ok;
    }

    bool List(const std::wstring& zipPath, std::vector<Entry>* out)
    {
        ZipFile zf;
        if (!ZipOpen(zipPath, &zf))
            return false;
        int n = (int)mz_zip_reader_get_num_files(&zf.zip);
        out->clear();
        out->reserve(n);
        for (int i = 0; i < n; i++)
        {
            mz_zip_archive_file_stat st;
            if (!mz_zip_reader_file_stat(&zf.zip, i, &st))
                continue;
            if (mz_zip_reader_is_file_a_directory(&zf.zip, i))
                continue;
            Entry e;
            e.name = st.m_filename;
            e.size = st.m_uncomp_size;
            out->push_back(std::move(e));
        }
        return true;
    }

    static void MkDirs(const std::wstring& dir)
    {
        std::wstring cur;
        for (size_t i = 0; i <= dir.size(); i++)
        {
            if (i == dir.size() || dir[i] == L'\\' || dir[i] == L'/')
            {
                if (!cur.empty())
                    CreateDirectoryW(PathUtil::Long(cur).c_str(), nullptr);
                if (i < dir.size())
                    cur += dir[i];
            }
            else
            {
                cur += dir[i];
            }
        }
    }

    // 写文件回调（目标路径经 pOpaque 传入）
    struct WriteCtx
    {
        std::wstring full;
        FILE* fp = nullptr;
    };

    static size_t WriteCb(void* opaque, mz_uint64, const void* buf, size_t n)
    {
        WriteCtx* c = (WriteCtx*)opaque;
        if (!c->fp)
        {
            c->fp = _wfopen(PathUtil::Long(c->full).c_str(), L"wb");
            if (!c->fp)
                return 0;
        }
        return fwrite(buf, 1, n, c->fp);
    }

    bool ExtractAll(const std::wstring& zipPath, const std::wstring& destDir, std::string* error)
    {
        ZipFile zf;
        if (!ZipOpen(zipPath, &zf))
        {
            if (error) *error = "zip open failed";
            return false;
        }
        int n = (int)mz_zip_reader_get_num_files(&zf.zip);
        MkDirs(destDir);
        bool ok = true;
        for (int i = 0; i < n && ok; i++)
        {
            mz_zip_archive_file_stat st;
            if (!mz_zip_reader_file_stat(&zf.zip, i, &st))
                continue;
            if (mz_zip_reader_is_file_a_directory(&zf.zip, i))
                continue;

            // 安全化：替换 Windows 非法字符与 ..
            std::string clean;
            for (const char* p = st.m_filename; *p; p++)
            {
                char c = *p;
                if (c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
                    c = '_';
                clean += c;
            }
            size_t pos = 0;
            while ((pos = clean.find("..", pos)) != std::string::npos)
                clean[pos] = '_';

            std::wstring rel = Utf8ToWide(clean);
            std::replace(rel.begin(), rel.end(), L'/', L'\\');
            while (!rel.empty() && rel[0] == L'\\')
                rel.erase(rel.begin());
            if (rel.empty())
                continue;

            std::wstring full = destDir + L"\\" + rel;
            size_t slash = full.find_last_of(L'\\');
            if (slash != std::wstring::npos)
                MkDirs(full.substr(0, slash));

            WriteCtx ctx;
            ctx.full = full;
            if (!mz_zip_reader_extract_to_callback(&zf.zip, (mz_uint)i, WriteCb, &ctx, 0) ||
                (ctx.fp && fclose(ctx.fp) != 0))
            {
                Log::Printf("[Zip] extract fail: %s", clean.c_str());
                if (error) *error = "extract failed: " + clean;
                ok = false;
            }
        }
        return ok;
    }

    bool ReadFile(const std::wstring& zipPath, const std::string& entryName, std::string* out)
    {
        ZipFile zf;
        if (!ZipOpen(zipPath, &zf))
            return false;
        int idx = mz_zip_reader_locate_file(&zf.zip, entryName.c_str(), nullptr, 0);
        if (idx < 0)
        {
            // 大小写不敏感兜底
            int n = (int)mz_zip_reader_get_num_files(&zf.zip);
            for (int i = 0; i < n; i++)
            {
                mz_zip_archive_file_stat st;
                if (mz_zip_reader_file_stat(&zf.zip, i, &st) &&
                    _stricmp(st.m_filename, entryName.c_str()) == 0)
                {
                    idx = i;
                    break;
                }
            }
        }
        if (idx < 0)
            return false;
        size_t sz = 0;
        void* data = mz_zip_reader_extract_to_heap(&zf.zip, (mz_uint)idx, &sz, 0);
        if (!data)
            return false;
        out->assign((const char*)data, sz);
        mz_free(data);
        return true;
    }
}
