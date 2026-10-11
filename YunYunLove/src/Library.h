#pragma once
// ============================================================
// Library.h — 自定义谱面商城数据层。
//   导入 zip → 解包到 <游戏目录>\YunYunLove\Store\<id>\ → entry.json
//   扫描生成卡片数据；搜索过滤 + 分页由 UI 层完成。
// ============================================================
#include <string>
#include <vector>
#include <mutex>
#include <atomic>

namespace Library
{
    struct ChartInfo
    {
        std::string file;        // .mc 文件名（zip 解包后的相对名）
        std::string label;       // 难度标签（version 字段）
        std::string bgm;         // 音频文件名（相对该目录）
        int notes = 0;
        double bpm = 0;
        double lengthSec = 0;
    };

    struct Entry
    {
        std::string id;
        std::wstring dir;        // 绝对路径
        std::string title;
        std::string artist;
        std::string creator;
        std::string cover;       // 封面文件名（相对 dir；空 = 无封面）
        std::vector<ChartInfo> charts;
        std::string versionText;
        int metaVersion = 0;     // entry.txt 派生数据版本（< 2 时扫描期重算时长/BPM）
    };

    // ---- 状态 ----
    struct Status
    {
        std::atomic<bool> scanning{ false };
        std::atomic<bool> importing{ false };
        std::atomic<int>  lastImportResult{ 0 };   // 1=导入成功 2=失败 3=删除成功
        // 结果序号：每次写入结果 +1。UI 据此逐次弹出提示（相同结果也能重复提示）
        std::atomic<unsigned> resultSeq{ 0 };
        // 最近一次导入/删除的结果文本（utf8，锁内读写）
        std::mutex importMsgMx;
        std::string importMsg;
    };
    Status& St();
    unsigned Gen();   // 数据代次（快照失效检测）

    // 商店根目录（<游戏目录>\YunYunLove\Store）
    const std::wstring& StoreDir();

    // 初始化（建目录 + 首次扫描；worker 线程调用）
    void Init();

    // 重新扫描（后台线程执行；完成即 entries 就绪）
    void RescanAsync();

    // 导入 zip（后台线程执行解包/解析）
    void ImportAsync(const std::wstring& zipPath);

    // 读取条目快照（UI 线程调用；返回条目数）
    std::vector<Entry> Snapshot();

    // 单条目查询（id）
    bool Find(const std::string& id, Entry* out);

    // 删除条目（后台执行：校验 id → 递归删除目录 → 重新扫描）
    void RemoveAsync(const std::string& id);
}
