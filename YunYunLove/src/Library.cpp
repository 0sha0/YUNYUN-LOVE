#include "Library.h"
#include "Log.h"
#include "ZipUtil.h"
#include "MalodyChart.h"
#include "Config.h"
#include "PathUtil.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <atomic>

namespace fs = std::filesystem;

    static std::error_code s_ec;   // filesystem 操作的错误码收容（失败只记日志不抛）

namespace Library
{
    static std::wstring s_storeDir;
    static std::mutex s_mx;
    static std::vector<Entry> s_entries;
    static std::atomic<unsigned> s_gen{ 0 };   // 数据代次（UI 检测刷新）
    static std::atomic<bool> s_removing{ false };  // 同一时刻只允许一个删除任务

    Status& St()
    {
        static Status s;
        return s;
    }

    unsigned Gen()
    {
        return s_gen.load(std::memory_order_relaxed);
    }

    static std::string WideToUtf8(const std::wstring& w)
    {
        int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string s(n, 0);
        if (n > 0)
            WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
        return s;
    }

    static std::wstring Utf8ToWide(const std::string& s)
    {
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        std::wstring w(n, 0);
        if (n > 0)
            MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
        return w;
    }

    const std::wstring& StoreDir() { return s_storeDir; }

    void Init()
    {
        wchar_t exe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring dir = exe;
        size_t slash = dir.find_last_of(L'\\');
        if (slash != std::wstring::npos)
            dir = dir.substr(0, slash);
        s_storeDir = dir + L"\\YunYunLove\\Store";
        CreateDirectoryW((dir + L"\\YunYunLove").c_str(), nullptr);
        CreateDirectoryW(s_storeDir.c_str(), nullptr);
        Config::Load((dir + L"\\YunYunLove\\config.json").c_str());
        Log::Printf("[Library] store = %ls", s_storeDir.c_str());
        RescanAsync();
    }

    // ---------- entry 元数据序列化（行式：k=v；chart=file|label|bgm|notes|bpm|length） ----------
    static std::string EntryToText(const Entry& e)
    {
        std::string j;
        j += "id=" + e.id + "\n";
        j += "v=2\n";
        j += "title=" + e.title + "\n";
        j += "artist=" + e.artist + "\n";
        j += "creator=" + e.creator + "\n";
        j += "cover=" + e.cover + "\n";
        j += "version=" + e.versionText + "\n";
        for (const ChartInfo& c : e.charts)
        {
            char buf[512];
            snprintf(buf, sizeof buf, "chart=%s|%s|%s|%d|%.2f|%.2f\n",
                     c.file.c_str(), c.label.c_str(), c.bgm.c_str(),
                     c.notes, c.bpm, c.lengthSec);
            j += buf;
        }
        return j;
    }

    // ---------- entry 元数据读取（行式） ----------
    static void ParseEntryText(const std::wstring& path, Entry* e)
    {
        std::ifstream f(PathUtil::Long(path), std::ios::binary);
        if (!f)
            return;
        std::string line;
        while (std::getline(f, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            size_t eq = line.find('=');
            if (eq == std::string::npos)
                continue;
            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);
            if (key == "id") e->id = val;
            else if (key == "v") e->metaVersion = atoi(val.c_str());
            else if (key == "title") e->title = val;
            else if (key == "artist") e->artist = val;
            else if (key == "creator") e->creator = val;
            else if (key == "cover") e->cover = val;
            else if (key == "version") e->versionText = val;
            else if (key == "chart")
            {
                // chart=file|label|bgm|notes|bpm|length
                std::vector<std::string> parts;
                size_t p = 0;
                while (true)
                {
                    size_t bar = val.find('|', p);
                    if (bar == std::string::npos)
                    {
                        parts.push_back(val.substr(p));
                        break;
                    }
                    parts.push_back(val.substr(p, bar - p));
                    p = bar + 1;
                }
                if (parts.size() < 6)
                    continue;
                ChartInfo c;
                c.file = parts[0];
                c.label = parts[1];
                c.bgm = parts[2];
                c.notes = atoi(parts[3].c_str());
                c.bpm = atof(parts[4].c_str());
                c.lengthSec = atof(parts[5].c_str());
                if (!c.file.empty())
                    e->charts.push_back(std::move(c));
            }
        }
    }

    // 旧版条目存的是文件名，文件实际在子目录：按名递归补全相对路径
    static bool RelFind(const std::wstring& dir, const std::string& nameUtf8, std::string* outRel)
    {
        std::error_code err;
        // std::filesystem 不会自动处理超长路径；目录与迭代结果统一带 \\?\ 前缀，
        // 这样 fs::relative 两端前缀一致，比较仍然成立。
        const std::wstring ldir = PathUtil::Long(dir);
        for (auto& it : fs::recursive_directory_iterator(ldir, fs::directory_options::skip_permission_denied, err))
        {
            if (err)
                break;
            if (!it.is_regular_file())
                continue;
            std::string nm = WideToUtf8(it.path().filename().wstring());
            if (_stricmp(nm.c_str(), nameUtf8.c_str()) == 0)
            {
                fs::path rel = fs::relative(it.path(), ldir, err);
                std::wstring relw = err ? it.path().filename().wstring() : rel.wstring();
                std::replace(relw.begin(), relw.end(), L'/', L'\\');
                *outRel = WideToUtf8(relw);
                return true;
            }
        }
        return false;
    }

    static void HealEntry(Entry& e)
    {
        bool changed = false;
        for (ChartInfo& c : e.charts)
        {
            if (!c.file.empty() &&
                GetFileAttributesW(PathUtil::Long(e.dir + L"\\" + Utf8ToWide(c.file)).c_str()) == INVALID_FILE_ATTRIBUTES)
            {
                std::string rel;
                if (RelFind(e.dir, c.file, &rel))
                {
                    c.file = rel;
                    changed = true;
                }
            }
            if (!c.bgm.empty() &&
                GetFileAttributesW(PathUtil::Long(e.dir + L"\\" + Utf8ToWide(c.bgm)).c_str()) == INVALID_FILE_ATTRIBUTES)
            {
                std::string rel;
                if (RelFind(e.dir, c.bgm, &rel))
                {
                    c.bgm = rel;
                    changed = true;
                }
            }
        }
        // 早期版本按错误换算写入的派生数据（时长/BPM/物件）在迁移时重算一次。
        if (e.metaVersion < 2)
        {
            for (ChartInfo& c : e.charts)
            {
                std::ifstream f(PathUtil::Long(e.dir + L"\\" + Utf8ToWide(c.file)), std::ios::binary);
                if (!f)
                    continue;
                std::stringstream ss;
                ss << f.rdbuf();
                MalodyChart::Chart ch;
                std::string perr;
                if (!MalodyChart::ParseAny(ss.str(), &ch, &perr))
                    continue;
                c.notes = ch.NoteCount();
                c.bpm = ch.bpms.empty() ? 0 : ch.bpms.front().bpm;
                c.lengthSec = ch.LengthSec();
            }
            e.metaVersion = 2;
            changed = true;
        }
        if (!e.cover.empty() &&
            GetFileAttributesW(PathUtil::Long(e.dir + L"\\" + Utf8ToWide(e.cover)).c_str()) == INVALID_FILE_ATTRIBUTES)
        {
            std::string rel;
            if (RelFind(e.dir, e.cover, &rel))
            {
                e.cover = rel;
                changed = true;
            }
        }
        if (changed)
        {
            // 写回修复后的元数据
            std::ofstream mf(PathUtil::Long(e.dir + L"\\entry.txt"), std::ios::binary);
            mf << EntryToText(e);
        }
    }

    // ---------- 扫描 ----------
    static void Scan()
    {
        std::vector<Entry> list;
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((s_storeDir + L"\\*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                {
                    if (fd.cFileName[0] == L'.') continue;
                    std::wstring dir = s_storeDir + L"\\" + fd.cFileName;
                    std::wstring meta = dir + L"\\entry.txt";
                    if (GetFileAttributesW(meta.c_str()) == INVALID_FILE_ATTRIBUTES)
                        continue;
                    Entry e;
                    e.dir = dir;
                    ParseEntryText(meta, &e);
                    if (e.id.empty()) e.id = WideToUtf8(fd.cFileName);
                    HealEntry(e);
                    if (!e.charts.empty())
                    {
                        list.push_back(std::move(e));
                    }
                    else
                    {
                        Log::Printf("[Library] skip %ls (no charts)", fd.cFileName);
                    }
                }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        // 按标题排序
        std::sort(list.begin(), list.end(), [](const Entry& a, const Entry& b) {
            return _stricmp(a.title.c_str(), b.title.c_str()) < 0;
        });
        const int scanCount = (int)list.size();
        {
            std::lock_guard<std::mutex> lk(s_mx);
            s_entries = std::move(list);
            s_gen++;
        }
        Log::Printf("[Library] scan done, %d entries", scanCount);
    }

    void RescanAsync()
    {
        St().scanning = true;
        std::thread([] {
            Scan();
            St().scanning = false;
        }).detach();
    }

    std::vector<Entry> Snapshot()
    {
        std::lock_guard<std::mutex> lk(s_mx);
        return s_entries;
    }

    bool Find(const std::string& id, Entry* out)
    {
        std::lock_guard<std::mutex> lk(s_mx);
        for (const Entry& e : s_entries)
        {
            if (e.id == id)
            {
                *out = e;
                return true;
            }
        }
        return false;
    }

    // ---------- 导入 ----------
    static void SetImportResult(int code, const std::string& msg)
    {
        {
            std::lock_guard<std::mutex> lk(St().importMsgMx);
            St().importMsg = msg;
        }
        St().lastImportResult = code;
        St().resultSeq.fetch_add(1, std::memory_order_release);
        Log::Printf("[Library] action: %s", msg.c_str());
    }

    // 大小写不敏感查找目录内文件（返回真实文件名 utf8）
    static bool FindFileCI(const std::wstring& dir, const std::string& nameUtf8, std::string* realName)
    {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE)
            return false;
        std::string want = nameUtf8;
        std::transform(want.begin(), want.end(), want.begin(),
                       [](unsigned char c) { return (char)std::tolower(c); });
        bool ok = false;
        do
        {
            std::string have = WideToUtf8(fd.cFileName);
            std::string low = have;
            std::transform(low.begin(), low.end(), low.begin(),
                           [](unsigned char c) { return (char)std::tolower(c); });
            if (low == want)
            {
                *realName = have;
                ok = true;
                break;
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        return ok;
    }

    // 目录里最大的音频文件（BGM 兜底）
    static std::string LargestAudio(const std::wstring& dir)
    {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE)
            return {};
        std::string best;
        unsigned long long bestSize = 0;
        do
        {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            std::string name = WideToUtf8(fd.cFileName);
            if (!(ZipUtil::HasExt(name, "ogg") || ZipUtil::HasExt(name, "mp3") || ZipUtil::HasExt(name, "wav")))
                continue;
            unsigned long long sz = ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            if (sz > bestSize)
            {
                bestSize = sz;
                best = name;
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        return best;
    }

    static std::string LargestImage(const std::wstring& dir)
    {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE)
            return {};
        std::string best;
        unsigned long long bestSize = 0;
        do
        {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            std::string name = WideToUtf8(fd.cFileName);
            if (!(ZipUtil::HasExt(name, "jpg") || ZipUtil::HasExt(name, "jpeg") ||
                  ZipUtil::HasExt(name, "png") || ZipUtil::HasExt(name, "bmp")))
                continue;
            unsigned long long sz = ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            if (sz > bestSize)
            {
                bestSize = sz;
                best = name;
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        return best;
    }

    static std::string SanitizeId(const std::string& s)
    {
        std::string o;
        for (char c : s)
        {
            if ((unsigned char)c >= 0x80 || isalnum((unsigned char)c) || c == '_' || c == '-')
                o += c;
            else
                o += '_';
        }
        if (o.size() > 48)
            o = o.substr(0, 48);
        return o;
    }

    static unsigned long long Fnv1a(const std::string& s)
    {
        unsigned long long h = 1469598103934665603ull;
        for (unsigned char c : s)
        {
            h ^= c;
            h *= 1099511628211ull;
        }
        return h;
    }

    // ---------- 超长路径安全的文件系统操作 ----------
    // 实测 std::filesystem 在路径 > MAX_PATH 时不可靠：remove_all 会返回
    // ERROR_DIR_NOT_EMPTY(code 145)，copy 会返回 ERROR_PATH_NOT_FOUND。这里用带 \\?\ 前缀
    // 的 Win32 调用自己实现「递归删除 / 递归复制」。
    static bool RemoveTree(const std::wstring& dir)
    {
        std::wstring pat = PathUtil::Long(dir) + L"\\*";
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(pat.c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
                    continue;
                std::wstring child = dir + L"\\" + fd.cFileName;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    RemoveTree(child);
                else
                    DeleteFileW(PathUtil::Long(child).c_str());
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        return RemoveDirectoryW(PathUtil::Long(dir).c_str()) != 0;
    }

    static bool CopyTree(const std::wstring& from, const std::wstring& to)
    {
        CreateDirectoryW(PathUtil::Long(to).c_str(), nullptr);
        std::wstring pat = PathUtil::Long(from) + L"\\*";
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(pat.c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE)
            return true;
        bool ok = true;
        do
        {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
                continue;
            std::wstring src = from + L"\\" + fd.cFileName;
            std::wstring dst = to + L"\\" + fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                ok = CopyTree(src, dst) && ok;
            else
                ok = (CopyFileW(PathUtil::Long(src).c_str(),
                                PathUtil::Long(dst).c_str(), FALSE) != 0) && ok;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        return ok;
    }

    static void ImportWorker(std::wstring zipPath)
    {
        St().importing = true;
        St().lastImportResult = 0;

        // 1) 临时目录解包
        wchar_t tmpName[40];
        swprintf_s(tmpName, L"_tmp_%u", GetTickCount());
        std::wstring tmp = s_storeDir + L"\\" + tmpName;
        std::string err;
        if (!ZipUtil::ExtractAll(zipPath, tmp, &err))
        {
            SetImportResult(2, "zip 解包失败: " + err);
            RemoveTree(tmp);
            St().importing = false;
            return;
        }

        // 2) 递归收集谱面文件：Malody .mc 与 osu! .osu 都收（最后统一过 4K 过滤）
        std::vector<std::wstring> chartFiles;
        for (auto& it : fs::recursive_directory_iterator(tmp))
        {
            if (it.is_regular_file())
            {
                std::wstring ext = it.path().extension().wstring();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
                if (ext == L".mc" || ext == L".osu")
                    chartFiles.push_back(it.path().wstring());
            }
        }
        std::sort(chartFiles.begin(), chartFiles.end());   // 顺序稳定：entry 元数据取首张
        if (chartFiles.empty())
        {
            SetImportResult(2, "zip 中没有找到谱面文件（.mc / .osu）");
            RemoveTree(tmp);
            St().importing = false;
            return;
        }

        Entry entry;
        std::vector<ChartInfo> charts;
        std::vector<std::vector<std::string>> chartSounds;   // 与 charts 一一对应（type=1 声音）
        std::string firstBgm, firstCover;
        std::string baseDirName;   // 主要谱面所在子目录（相对于 tmp）
        int filteredNon4K = 0;     // 被自动过滤掉的非 4K 谱面数

        for (const std::wstring& mc : chartFiles)
        {
            std::ifstream f(PathUtil::Long(mc), std::ios::binary);
            if (!f) continue;
            std::stringstream ss;
            ss << f.rdbuf();
            std::string text = ss.str();

            MalodyChart::Chart c;
            std::string perr;
            // 按内容自动分派 Malody .mc(JSON) / osu! .osu(纯文本)
            if (!MalodyChart::ParseAny(text, &c, &perr))
            {
                Log::Printf("[Library] skip bad chart (%s): %s",
                            WideToUtf8(mc).c_str(), perr.c_str());
                continue;
            }
            if (!MalodyChart::Is4K(c))
            {
                // 自动过滤：Malody 非 4K Key、osu 非 mania / mania 非 4K、taiko、catch
                Log::Printf("[Library] filter non-4K chart (%s): mode=%d column=%d",
                            WideToUtf8(mc).c_str(), c.mode, c.column);
                filteredNon4K++;
                continue;
            }

            ChartInfo ci;
            // 存相对 entry 目录的路径（zip 内子目录中的谱面也能定位）
            {
                std::error_code rerr;
                fs::path rel = fs::relative(mc, tmp, rerr);
                std::wstring relw = rerr ? fs::path(mc).filename().wstring() : rel.wstring();
                std::replace(relw.begin(), relw.end(), L'/', L'\\');
                ci.file = WideToUtf8(relw);
            }
            ci.label = c.version.empty() ? ci.file : c.version;
            ci.notes = c.NoteCount();
            ci.bpm = c.bpms.empty() ? 0 : c.bpms.front().bpm;
            ci.lengthSec = c.LengthSec();
            ci.bgm = c.bgm;

            if (entry.title.empty())
            {
                entry.title = c.title.empty() ? WideToUtf8(fs::path(zipPath).stem().wstring()) : c.title;
                entry.artist = c.artist;
                entry.creator = c.creator;
                entry.versionText = c.version;
                firstCover = c.background;
                baseDirName = WideToUtf8(fs::path(mc).parent_path().filename().wstring());
            }
            charts.push_back(std::move(ci));
            chartSounds.push_back(c.soundCandidates);
        }

        if (charts.empty())
        {
            SetImportResult(2, "没有找到 4K 谱面（仅支持 4K：Malody .mc / osu!mania .osu）");
            RemoveTree(tmp);
            St().importing = false;
            return;
        }

        // 3) 每个 chart 的 BGM 兜底 + 校验存在性（一律存相对 entry 目录的路径）
        for (size_t ciIndex = 0; ciIndex < charts.size(); ciIndex++)
        {
            ChartInfo& ci = charts[ciIndex];
            std::string foundFull;
            if (!ci.bgm.empty())
            {
                for (auto& it : fs::recursive_directory_iterator(tmp))
                {
                    if (it.is_regular_file() &&
                        _stricmp(WideToUtf8(it.path().filename().wstring()).c_str(), ci.bgm.c_str()) == 0)
                    {
                        foundFull = WideToUtf8(it.path().wstring());
                        break;
                    }
                }
            }
            if (foundFull.empty() && ciIndex < chartSounds.size() && !chartSounds[ciIndex].empty())
            {
                // 谱面里的 type=1 声音名（主 BGM 通常也是其一）：取其中体积最大的实际文件，
                // 主 BGM 一定远大于零散打击音效。
                unsigned long long bestSz = 0;
                for (auto& it : fs::recursive_directory_iterator(tmp))
                {
                    if (!it.is_regular_file())
                        continue;
                    std::string nm = WideToUtf8(it.path().filename().wstring());
                    if (!(ZipUtil::HasExt(nm, "ogg") || ZipUtil::HasExt(nm, "mp3") ||
                          ZipUtil::HasExt(nm, "wav")))
                        continue;
                    bool wanted = false;
                    for (const std::string& s : chartSounds[ciIndex])
                    {
                        size_t slash = s.find_last_of("/\\\\");
                        std::string base = (slash == std::string::npos) ? s : s.substr(slash + 1);
                        if (_stricmp(base.c_str(), nm.c_str()) == 0) { wanted = true; break; }
                    }
                    if (!wanted)
                        continue;
                    unsigned long long sz = (unsigned long long)it.file_size();
                    if (sz > bestSz) { bestSz = sz; foundFull = WideToUtf8(it.path().wstring()); }
                }
            }
            if (foundFull.empty())
            {
                // 兜底：tmp 树内体积最大的音频文件
                unsigned long long bestSz = 0;
                for (auto& it : fs::recursive_directory_iterator(tmp))
                {
                    if (!it.is_regular_file())
                        continue;
                    std::string nm = WideToUtf8(it.path().filename().wstring());
                    if (!(ZipUtil::HasExt(nm, "ogg") || ZipUtil::HasExt(nm, "mp3") ||
                          ZipUtil::HasExt(nm, "wav")))
                        continue;
                    unsigned long long sz = (unsigned long long)it.file_size();
                    if (sz > bestSz) { bestSz = sz; foundFull = WideToUtf8(it.path().wstring()); }
                }
            }
            if (foundFull.empty())
            {
                SetImportResult(2, "谱面缺少音频文件（找不到 ogg/mp3/wav）");
                RemoveTree(tmp);
                St().importing = false;
                return;
            }
            {
                std::error_code rerr2;
                fs::path rel = fs::relative(Utf8ToWide(foundFull), tmp, rerr2);
                std::wstring relw = rerr2 ? Utf8ToWide(foundFull) : rel.wstring();
                std::replace(relw.begin(), relw.end(), L'/', L'\\');
                ci.bgm = WideToUtf8(relw);
            }
        }

        // 4) 封面（存相对 entry 目录的路径；meta.background 精确匹配优先）
        std::string cover;
        {
            std::string best;
            unsigned long long bestSz = 0;
            for (auto& it : fs::recursive_directory_iterator(tmp))
            {
                if (!it.is_regular_file())
                    continue;
                std::string nm = WideToUtf8(it.path().filename().wstring());
                bool isImg = ZipUtil::HasExt(nm, "jpg") || ZipUtil::HasExt(nm, "jpeg") ||
                             ZipUtil::HasExt(nm, "png") || ZipUtil::HasExt(nm, "bmp");
                if (!isImg)
                    continue;
                if (!firstCover.empty())
                {
                    if (_stricmp(nm.c_str(), firstCover.c_str()) == 0)
                    {
                        best = WideToUtf8(it.path().wstring());
                        break;
                    }
                    continue;   // 有明确背景图目标时，不拿别的图兜底
                }
                unsigned long long sz = (unsigned long long)it.file_size();
                if (sz > bestSz)
                {
                    bestSz = sz;
                    best = WideToUtf8(it.path().wstring());
                }
            }
            if (!best.empty())
            {
                std::error_code rerr3;
                fs::path rel = fs::relative(Utf8ToWide(best), tmp, rerr3);
                std::wstring relw = rerr3 ? Utf8ToWide(best) : rel.wstring();
                std::replace(relw.begin(), relw.end(), L'/', L'\\');
                cover = WideToUtf8(relw);
            }
        }
        entry.cover = cover;
        entry.charts = std::move(charts);

        // 5) 目录名 + 落盘
        std::wstring zipStem = fs::path(zipPath).stem().wstring();
        std::string idSeed = WideToUtf8(zipStem) + "|" + std::to_string(entry.charts.size());
        char tag[24];
        snprintf(tag, sizeof tag, "%08llx", (unsigned long long)(Fnv1a(idSeed) & 0xFFFFFFFFull));
        entry.id = SanitizeId(WideToUtf8(zipStem)) + "_" + tag;
        std::wstring dest = s_storeDir + L"\\" + Utf8ToWide(entry.id);
        RemoveTree(dest);   // 重导入 = 覆盖（必须先真正清空，否则目录改名会 ACCESS_DENIED）
        if (!MoveFileExW(PathUtil::Long(tmp).c_str(), PathUtil::Long(dest).c_str(), 0))
        {
            // 同盘 rename 失败兜底：自己递归复制（long path 安全）
            Log::Printf("[Library] rename failed (err=%lu), fallback copy", GetLastError());
            if (!CopyTree(tmp, dest))
            {
                Log::Printf("[Library] copy failed (err=%lu)", GetLastError());
                SetImportResult(2, "目录落盘失败");
                RemoveTree(tmp);
                St().importing = false;
                return;
            }
            RemoveTree(tmp);
        }

        {
            // 独立作用域：写完立即关文件，随后的 Scan 才能读到完整内容
            std::ofstream mf(PathUtil::Long(dest + L"\\entry.txt"), std::ios::binary);
            mf << EntryToText(entry);
        }

        char buf[512];
        if (filteredNon4K > 0)
            snprintf(buf, sizeof buf, "导入成功: %s（%d 张 4K 谱面，已过滤 %d 张非 4K）",
                     entry.title.c_str(), (int)entry.charts.size(), filteredNon4K);
        else
            snprintf(buf, sizeof buf, "导入成功: %s（%d 张 4K 谱面）",
                     entry.title.c_str(), (int)entry.charts.size());
        SetImportResult(1, buf);

        Scan();
        St().importing = false;
    }

    void ImportAsync(const std::wstring& zipPath)
    {
        if (St().importing)
            return;
        std::thread(ImportWorker, zipPath).detach();
    }

    // 条目 ID 只由 SanitizeId + hex 生成：ASCII 字母数字 / _ - . 或 UTF-8 字节。
    // 这里做同样的白名单校验，任何分隔符或 ".." 一律拒绝。
    static bool IsSafeId(const std::string& id)
    {
        if (id.empty() || id.size() > 160)
            return false;
        if (id[0] == '.')
            return false;
        for (char c : id)
        {
            unsigned char u = (unsigned char)c;
            if (u >= 0x80)
                continue;   // UTF-8 续字节 / 多字节首字节
            if (isalnum(u) || c == '_' || c == '-' || c == '.')
                continue;
            return false;
        }
        return id.find("..") == std::string::npos;
    }

    void RemoveAsync(const std::string& id)
    {
        // 仅接受扫描期生成的目录名形态，杜绝路径穿越 / 绝对路径
        if (!IsSafeId(id))
        {
            SetImportResult(2, "删除失败: 非法曲包 ID");
            return;
        }
        if (s_removing.exchange(true))
            return;
        std::thread([id] {
            std::wstring dest = s_storeDir + L"\\" + Utf8ToWide(id);
            DWORD attr = GetFileAttributesW(dest.c_str());
            if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY))
            {
                SetImportResult(2, "删除失败: 曲包不存在");
            }
            else
            {
                if (!RemoveTree(dest))
                {
                    Log::Printf("[Library] delete failed (err=%lu)", GetLastError());
                    SetImportResult(2, "删除失败: 目录被占用或权限不足");
                }
                else
                    SetImportResult(3, "已删除曲包");
            }
            Scan();
            s_removing = false;
        }).detach();
    }
}
