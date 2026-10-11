#include "GameBridge.h"
#include "MonoApi.h"
#include "MonoHost.h"
#include "MainThread.h"
#include "AudioDecoder.h"
#include "Log.h"
#include "Config.h"

#include <windows.h>
#include <cstring>
#include <mutex>
#include <deque>
#include <cstring>

// ============================================================
// 进入原生游玩的完整链路（mono 调用全部在主线程）：
//   1. RequestPlay（任意线程）→ 请求排队
//   2. 主线程：file:// URI → UnityWebRequestMultimedia.GetAudioClip
//      （ogg/wav 原生解码轮询）或 dr_mp3 本地解码（mp3）
//   3. JsonUtility.FromJson<ScoreData>(json)（游戏自己的谱面装载通道）
//   4. new ScoreInfo / ScoreDataSet（LevelData=null → 结果页跳过排行/本地化路径）
//   5. ParameterHolder{Mode=Normal, Data=...} → manager.GoTo<App.InGameScene>(leaf)
//   6. 入场后：PlayInfo.set_Title(LocalizeTextKey.GetText(自定义标题))
// ============================================================
namespace GameBridge
{
    using namespace MonoApi;

    // ---- 镜像 ----
    static MonoImage* g_imgApp = nullptr;        // App.dll
    static MonoImage* g_imgScore = nullptr;      // YYNoteEditor.ScoreData.dll
    static MonoImage* g_imgGss = nullptr;        // GameStateStructure.dll
    static MonoImage* g_imgCore = nullptr;       // UnityEngine.CoreModule.dll
    static MonoImage* g_imgAudioMod = nullptr;   // UnityEngine.AudioModule.dll
    static MonoImage* g_imgWrAudio = nullptr;    // UnityEngine.UnityWebRequestAudioModule.dll
    static MonoImage* g_imgWr = nullptr;         // UnityEngine.UnityWebRequestModule.dll
    static MonoImage* g_imgL10n = nullptr;       // UVMBinding.Localization.dll
    static MonoImage* g_imgBinding = nullptr;    // App.Common.dll

    // ---- 类（主线程解析） ----
    static MonoClass* g_clsInGameScene = nullptr;      // App.InGameScene
    static MonoClass* g_clsInGameMode = nullptr;       // App.InGameMode (enum)
    static MonoClass* g_clsScoreDataSet = nullptr;     // App.Score.ScoreDataSet
    static MonoClass* g_clsScoreInfo = nullptr;        // App.ScoreInfo
    static MonoClass* g_clsScoreData = nullptr;        // YYNoteEditor.ScoreData
    static MonoClass* g_clsManager = nullptr;          // GameStateStructure.GameStateManager
    static MonoClass* g_clsParamHolder = nullptr;      // GameStateStructure.ParameterHolder
    static MonoClass* g_clsGameState = nullptr;        // GameStateStructure.GameState
    static MonoClass* g_clsJsonUtility = nullptr;      // UnityEngine.JsonUtility
    static MonoClass* g_clsUwrMultimedia = nullptr;    // UnityEngine.Networking.UnityWebRequestMultimedia
    static MonoClass* g_clsUwr = nullptr;              // UnityEngine.Networking.UnityWebRequest
    static MonoClass* g_clsDlAudioClip = nullptr;      // UnityEngine.Networking.DownloadHandlerAudioClip
    static MonoClass* g_clsAudioClip = nullptr;        // UnityEngine.AudioClip
    static MonoClass* g_clsAudioType = nullptr;        // UnityEngine.AudioType (enum)
    static MonoClass* g_clsUnityObj = nullptr;         // UnityEngine.Object
    static MonoClass* g_clsEnum = nullptr;             // System.Enum
    static MonoClass* g_clsLocalizeTextKey = nullptr;  // UVMBinding.Binders.LocalizeTextKey
    static MonoClass* g_clsUIInGame = nullptr;         // App.Binding.UIInGame
    static MonoClass* g_clsUIPlayInfo = nullptr;       // App.Binding.UIInGamePlayInfo
    static MonoClass* g_clsListDef = nullptr;          // System.Collections.Generic.List`1
    static MonoClass* g_clsActivator = nullptr;        // System.Activator

    // 音频服务：进场后必须停掉菜单 BGM，否则标题 BGM 会和自定义谱面音频重叠
    static MonoClass* g_clsISound = nullptr;           // App.ISound
    static MonoClass* g_clsAppGameState = nullptr;     // App.AppGameState（承载 Sound 属性）
    static MonoClass* g_clsBgmController = nullptr;    // ILib.Audio.SoundManagement.BgmController
    static MonoClass* g_clsServInjector = nullptr;     // ILib.ServInject.ServInjector
    static int        s_stopMenuAudioDelay = -1;       // >0 时逐帧倒计时，归零则停菜单音频
    static int        s_stopMenuAudioRepeats = 0;      // 剩余重试次数（防止淡入过程中 BGM 被重新拉起）

    // 原始遍历用的字段缓存
    static MonoClassField* g_fListItems = nullptr;        // List`1._items
    static MonoClassField* g_fListSize = nullptr;         // List`1._size
    static MonoClassField* g_fGameStateActive = nullptr;  // GameState.m_Active

    static std::atomic<bool> g_ready{ false };
    static std::atomic<bool> g_inGameplay{ false };
    static std::string g_lastError;
    static std::mutex s_errMx;

    static void SetError(const std::string& e)
    {
        std::lock_guard<std::mutex> lk(s_errMx);
        g_lastError = e;
        Log::Printf("[Bridge] ERROR: %s", e.c_str());
    }

    std::string LastError()
    {
        std::lock_guard<std::mutex> lk(s_errMx);
        return g_lastError;
    }

    void SetExternalError(const std::string& msg)
    {
        SetError(msg);
    }

    bool Ready() { return g_ready.load(std::memory_order_acquire); }
    bool InGameplay() { return g_inGameplay.load(std::memory_order_relaxed); }

    // ---------- 播放请求队列 ----------
    static std::mutex s_playMx;
    static std::deque<PlayRequest> s_playQueue;

    void RequestPlay(const PlayRequest& req)
    {
        std::lock_guard<std::mutex> lk(s_playMx);
        s_playQueue.push_back(req);
    }

    // ---------- 音频加载任务（worker 解码 → 主线程建 clip） ----------
    struct AudioJob
    {
        enum Stage
        {
            Idle,
            Decoding,    // worker 正在解码 ogg/wav/mp3
            Ready,
            Failed
        };
        Stage stage = Idle;
        std::wstring path;
        DWORD startTick = 0;

        // 解码结果（worker 写 → 主线程读，mx 保护）
        std::mutex mx;
        bool decodeDone = false;
        AudioDecoder::PcmData decoded;

        MonoObject* clip = nullptr;
        std::string error;

        void Reset()
        {
            stage = Idle;
            path.clear();
            {
                std::lock_guard<std::mutex> lk(mx);
                decodeDone = false;
                decoded = AudioDecoder::PcmData{};
            }
            clip = nullptr;
            error.clear();
        }
    };
    static AudioJob s_audio;

    // ---------- 解码 worker（任意格式） ----------
    static void AudioDecodeWorker(AudioJob* job)
    {
        AudioDecoder::PcmData d = AudioDecoder::DecodeFile(job->path);
        std::lock_guard<std::mutex> lk(job->mx);
        job->decoded = std::move(d);
        job->decodeDone = true;
    }

    // ---------- 主线程初始化：类解析 ----------
    static bool ResolveClasses()
    {
        if (!MonoHost::Init())
            return false;

        if (g_ready.load(std::memory_order_acquire))
            return true;   // 完全就绪（含 walk 字段）才免检；否则每次重走完整解析

        g_imgApp     = MonoHost::Image("App");
        g_imgScore   = MonoHost::Image("YYNoteEditor.ScoreData");
        g_imgGss     = MonoHost::Image("GameStateStructure");
        g_imgCore    = MonoHost::Image("UnityEngine.CoreModule");
        g_imgAudioMod = MonoHost::Image("UnityEngine.AudioModule");
        g_imgWrAudio = MonoHost::Image("UnityEngine.UnityWebRequestAudioModule");
        g_imgWr      = MonoHost::Image("UnityEngine.UnityWebRequestModule");
        g_imgL10n    = MonoHost::Image("UVMBinding.Localization");
        g_imgBinding = MonoHost::Image("App.Common");

        if (!g_imgApp || !g_imgScore || !g_imgGss || !g_imgCore)
        {
            static DWORD s_last = 0;
            if (GetTickCount() - s_last > 2000)
            {
                s_last = GetTickCount();
                Log::Printf("[Bridge] images pending: app=%p score=%p gss=%p core=%p",
                            (void*)g_imgApp, (void*)g_imgScore, (void*)g_imgGss, (void*)g_imgCore);
            }
            return false;
        }

        g_clsInGameScene  = MonoHost::Class(g_imgApp, "App", "InGameScene");
        g_clsInGameMode   = MonoHost::Class(g_imgApp, "App", "InGameMode");
        g_clsScoreDataSet = MonoHost::Class(g_imgApp, "App.Score", "ScoreDataSet");
        g_clsScoreInfo    = MonoHost::Class(g_imgApp, "App", "ScoreInfo");
        g_clsScoreData    = MonoHost::Class(g_imgScore, "YYNoteEditor", "ScoreData");
        g_clsManager      = MonoHost::Class(g_imgGss, "GameStateStructure", "GameStateManager");
        g_clsParamHolder  = MonoHost::Class(g_imgGss, "GameStateStructure", "ParameterHolder");
        g_clsGameState    = MonoHost::Class(g_imgGss, "GameStateStructure", "GameState");
        g_clsUIInGame     = MonoHost::Class(g_imgApp, "App.Binding", "UIInGame");
        g_clsUIPlayInfo   = MonoHost::Class(g_imgApp, "App.Binding", "UIInGamePlayInfo");
        g_clsJsonUtility  = MonoHost::Class(g_imgCore, "UnityEngine", "JsonUtility");
        if (!g_clsJsonUtility)
            g_clsJsonUtility = MonoHost::Class(MonoHost::Image("UnityEngine.JSONSerializeModule"),
                                               "UnityEngine", "JsonUtility");   // Unity 6 移到了 JSONSerializeModule
        g_clsUnityObj     = MonoHost::Class(g_imgCore, "UnityEngine", "Object");
        g_clsAudioClip    = MonoHost::Class(g_imgAudioMod ? g_imgAudioMod : g_imgCore, "UnityEngine", "AudioClip");
        g_clsAudioType    = MonoHost::Class(g_imgCore, "UnityEngine", "AudioType");
        g_clsUwrMultimedia = MonoHost::Class(g_imgWrAudio ? g_imgWrAudio : g_imgCore,
                                             "UnityEngine.Networking", "UnityWebRequestMultimedia");
        g_clsUwr          = MonoHost::Class(g_imgWr ? g_imgWr : g_imgCore,
                                            "UnityEngine.Networking", "UnityWebRequest");
        g_clsDlAudioClip  = MonoHost::Class(g_imgWrAudio ? g_imgWrAudio : g_imgCore,
                                            "UnityEngine.Networking", "DownloadHandlerAudioClip");
        g_clsListDef      = MonoHost::Class(MonoHost::Image("mscorlib"),
                                            "System.Collections.Generic", "List`1");
        g_clsActivator    = MonoHost::Class(MonoHost::Image("mscorlib"), "System", "Activator");
        g_clsLocalizeTextKey = MonoHost::Class(g_imgL10n ? g_imgL10n : g_imgApp,
                                               "UVMBinding.Binders", "LocalizeTextKey");

        // 音频服务（可为空：解析失败不影响主流程，只是停不掉菜单 BGM）
        g_clsISound = MonoHost::Class(g_imgBinding, "App", "ISound");
        g_clsAppGameState = MonoHost::Class(g_imgApp, "App", "AppGameState");
        g_clsBgmController = MonoHost::Class(MonoHost::Image("ILib.SoundManager"),
                                             "ILib.Audio.SoundManagement", "BgmController");
        g_clsServInjector = MonoHost::Class(MonoHost::Image("ILib.ServInjector"),
                                            "ILib.ServInject", "ServInjector");

        MonoClass* need[] = {
            g_clsInGameScene, g_clsInGameMode, g_clsScoreDataSet, g_clsScoreInfo,
            g_clsScoreData, g_clsManager, g_clsParamHolder, g_clsGameState,
            g_clsJsonUtility, g_clsUnityObj, g_clsAudioClip, g_clsAudioType,
            g_clsUwrMultimedia, g_clsUwr, g_clsDlAudioClip,
            g_clsListDef, g_clsActivator, g_clsLocalizeTextKey, g_clsUIInGame,
            g_clsUIPlayInfo
        };
        const char* needNames[] = {
            "InGameScene", "InGameMode", "ScoreDataSet", "ScoreInfo",
            "ScoreData", "GameStateManager", "ParameterHolder", "GameState",
            "JsonUtility", "UnityEngine.Object", "AudioClip", "AudioType",
            "UnityWebRequestMultimedia", "UnityWebRequest", "DownloadHandlerAudioClip",
            "List`1", "Activator", "LocalizeTextKey", "UIInGame",
            "UIInGamePlayInfo"
        };
        const int needCount = (int)(sizeof(need) / sizeof(need[0]));
        const char* missing = nullptr;
        for (int i = 0; i < needCount; i++)
        {
            if (!need[i])
            {
                missing = needNames[i];
                break;
            }
        }
        if (missing)
        {
            static DWORD s_last2 = 0;
            if (GetTickCount() - s_last2 > 2000)
            {
                s_last2 = GetTickCount();
                Log::Printf("[Bridge] classes pending, missing: %s", missing ? missing : "?");
            }
            return false;
        }

        g_fListItems = mono_class_get_field_from_name(g_clsListDef, "_items");
        g_fListSize = mono_class_get_field_from_name(g_clsListDef, "_size");
        g_fGameStateActive = mono_class_get_field_from_name(g_clsGameState, "m_Active");
        if (!g_fListItems || !g_fListSize || !g_fGameStateActive)
        {
            Log::Printf("[Bridge] walk fields missing: items=%p size=%p active=%p",
                        (void*)g_fListItems, (void*)g_fListSize, (void*)g_fGameStateActive);
            return false;
        }

        g_ready.store(true, std::memory_order_release);
        Log::Printf("[Bridge] all classes resolved");
        return true;
    }

    bool InitNative()
    {
        // 只等 Mono 导出表；真正的类解析投递到主线程
        return MonoApi::Init();
    }

    // ---------- 小工具 ----------
    static MonoObject* FindObjectOfType(MonoClass* typeClass)
    {
        MonoObject* type = MonoHost::TypeOf(typeClass);
        if (!type)
            return nullptr;
        // 精确匹配 FindObjectOfType(Type)。
        //   教训：mono_class_get_method_from_name 只按"名字+参数个数"匹配，
        //   元数据里排在前的 FindObjectOfType<T>(bool)（开放泛型，同为 1 参）
        //   会抢先命中 → JIT 开放泛型 → method-to-ir.c:6288 致命断言。
        static bool s_logged = false;
        MonoMethod* m = MonoHost::FindMethodExact(g_clsUnityObj, "FindObjectOfType", 1, "System.Type");
        if (!s_logged)
        {
            s_logged = true;
            Log::Printf("[Bridge] exact-find FindObjectOfType(Type) = %p", (void*)m);
        }
        if (m)
        {
            void* args[1] = { type };
            MonoHost::Inv r = MonoHost::Invoke(m, nullptr, args);
            if (r.ok && r.ret)
                return r.ret;
        }
        // 最终兜底：FindObjectsOfType(Type) 取第一个
        MonoMethod* m2 = mono_class_get_method_from_name(g_clsUnityObj, "FindObjectsOfType", 1);
        if (m2)
        {
            void* args[1] = { type };
            MonoHost::Inv r = MonoHost::Invoke(m2, nullptr, args);
            if (r.ok && r.ret && mono_array_length((MonoObject*)r.ret) > 0)
                return MonoHost::ObjArrGet((MonoArray*)r.ret, 0);
        }
        return nullptr;
    }

    // 沿唯一活动子链下探，返回最深活动状态（叶）；同时把「链上最靠根部的
    // App.InGameScene」写入 *topInGame（没有则 nullptr）。纯内存遍历状态树
    // （结构来自 GameStateStructure.dll 反编译：
    //   GameStateManager.m_Data : StackData { Parent, State, Children:List<StackData>, ... }
    //   GameState.m_Active : bool；List<T> 的 _items/_size 布局与开放泛型一致）
    // 零托管调用 → 不触碰 mono JIT，天然协作 GC 安全（Boehm 不移动对象）。
    static MonoObject* WalkActiveChain(MonoObject* manager, MonoObject** topInGame)
    {
        if (topInGame)
            *topInGame = nullptr;
        MonoClassField* fData = mono_class_get_field_from_name(g_clsManager, "m_Data");
        if (!fData)
            return nullptr;
        MonoObject* node = nullptr;   // StackData
        mono_field_get_value(manager, fData, &node);
        if (!node)
            return nullptr;

        MonoClass* sds = mono_object_get_class((MonoObject*)node);
        MonoClassField* fChildren = mono_class_get_field_from_name(sds, "Children");
        MonoClassField* fState = mono_class_get_field_from_name(sds, "State");
        if (!fChildren || !fState)
            return nullptr;
        MonoClassField* fItems = g_fListItems;
        MonoClassField* fSize = g_fListSize;
        MonoClassField* fActive = g_fGameStateActive;
        if (!fItems || !fSize || !fActive)
            return nullptr;

        // 沿唯一活动子链下探到叶
        MonoObject* visited[32] = {};
        int visitedN = 0;
        {
            MonoObject* st = nullptr;
            mono_field_get_value(node, fState, &st);
            if (st)
                visited[visitedN++] = st;
        }
        for (int depth = 0; depth < 32; depth++)
        {
            MonoObject* children = nullptr;
            mono_field_get_value(node, fChildren, &children);
            if (!children)
                break;
            MonoArray* items = nullptr;
            int size = 0;
            mono_field_get_value(children, fItems, &items);
            mono_field_get_value(children, fSize, &size);
            if (!items || size <= 0)
                break;
            MonoObject* next = nullptr;   // StackData
            for (int i = size - 1; i >= 0; i--)
            {
                MonoObject* cand = MonoHost::ObjArrGet(items, i);
                if (!cand)
                    continue;
                MonoObject* st = nullptr;
                mono_field_get_value(cand, fState, &st);
                if (!st)
                    continue;
                bool active = false;
                mono_field_get_value(st, fActive, &active);
                if (active)
                {
                    next = cand;
                    break;
                }
            }
            if (!next)
                break;
            node = next;
            MonoObject* st = nullptr;
            mono_field_get_value(node, fState, &st);
            if (st && visitedN < 32)
                visited[visitedN++] = st;
        }
        if (topInGame && g_clsInGameScene)
        {
            for (int i = 0; i < visitedN; i++)
            {
                if (mono_object_get_class(visited[i]) == g_clsInGameScene)
                {
                    *topInGame = visited[i];
                    break;
                }
            }
        }
        return visitedN > 0 ? visited[visitedN - 1] : nullptr;
    }

    static MonoObject* CurrentLeafState(MonoObject* manager)
    {
        return WalkActiveChain(manager, nullptr);
    }

    static bool IsInGameplayState(MonoObject* manager)
    {
        MonoObject* leaf = CurrentLeafState(manager);
        if (!leaf)
            return false;
        return mono_object_get_class(leaf) == g_clsInGameScene;
    }

    // ---------- 音频装载（完全本地解码 → AudioClip.Create + SetData） ----------
    //   不再用 UnityWebRequestMultimedia：实测 file:// 的 ogg 解出 loadState=Loaded
    //   但 samples=0 / length=0（静音、时间不推进）。改为自解码 PCM 后建 clip，
    //   行为确定，且音乐/判定时间轴/晕晕动画均依赖该 clip 的真实采样。
    static std::string Utf8ToNarrow(const std::wstring& w)
    {
        int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string r(n, 0);
        if (n > 0)
            WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), r.data(), n, nullptr, nullptr);
        return r;
    }

    static bool StartAudioLoad(const std::wstring& path)
    {
        s_audio.Reset();
        s_audio.path = path;
        s_audio.startTick = GetTickCount();
        s_audio.stage = AudioJob::Decoding;
        std::thread(AudioDecodeWorker, &s_audio).detach();
        Log::Printf("[Bridge] audio decode started: %s", Utf8ToNarrow(path).c_str());
        return true;
    }

    // 主线程轮询：解码完成 → 建 Unity AudioClip（SetData 交错 float）→ 返回 clip
    static MonoObject* PollAudioDecode()
    {
        {
            std::lock_guard<std::mutex> lk(s_audio.mx);
            if (!s_audio.decodeDone)
            {
                if (GetTickCount() - s_audio.startTick > 30000)
                {
                    s_audio.error = "audio decode timeout (30s)";
                    s_audio.stage = AudioJob::Failed;
                }
                return nullptr;
            }
        }

        std::lock_guard<std::mutex> lk(s_audio.mx);
        if (!s_audio.decoded.ok)
        {
            s_audio.error = "decode: " + s_audio.decoded.error;
            s_audio.stage = AudioJob::Failed;
            return nullptr;
        }
        AudioDecoder::PcmData& d = s_audio.decoded;
        int frames = d.FrameCount();
        if (frames <= 0 || d.channels <= 0 || d.sampleRate <= 0)
        {
            s_audio.error = "decoded PCM empty";
            s_audio.stage = AudioJob::Failed;
            return nullptr;
        }
        Log::Printf("[Bridge] decoded: frames=%d ch=%d rate=%d (%.2fs)",
                    frames, d.channels, d.sampleRate, (double)frames / (double)d.sampleRate);

        // AudioClip.Create(name, lengthSamples, channels, frequency, stream=false)
        MonoObject* a0 = MonoHost::StrW(L"YunYunLove_BGM");
        MonoObject* a1 = MonoHost::BoxInt(frames);
        MonoObject* a2 = MonoHost::BoxInt(d.channels);
        MonoObject* a3 = MonoHost::BoxInt(d.sampleRate);
        MonoObject* a4 = MonoHost::BoxBool(false);
        void* args[5] = { a0, MonoHost::RawArg(a1), MonoHost::RawArg(a2),
                          MonoHost::RawArg(a3), MonoHost::RawArg(a4) };
        MonoHost::Inv clip = MonoHost::InvokeStatic(g_clsAudioClip, "Create", 5, args);
        if (!clip.ok || !clip.ret)
        {
            s_audio.error = "AudioClip.Create: " + clip.err;
            s_audio.stage = AudioJob::Failed;
            return nullptr;
        }

        // SetData(float[], 0)
        MonoArray* arr = MonoHost::PrimArrNew(MonoHost::ClassSingle(), (int)d.pcm.size());
        if (!arr)
        {
            s_audio.error = "pcm array alloc failed";
            s_audio.stage = AudioJob::Failed;
            return nullptr;
        }
        float* dst = (float*)mono_array_addr_with_size((MonoObject*)arr, 4, 0);
        memcpy(dst, d.pcm.data(), d.pcm.size() * sizeof(float));
        MonoObject* offBox = MonoHost::BoxInt(0);
        void* sdArgs[2] = { arr, MonoHost::RawArg(offBox) };
        MonoHost::Inv sd = MonoHost::InvokeByName(g_clsAudioClip, clip.ret, "SetData", 2, sdArgs);
        if (!sd.ok)
        {
            s_audio.error = "SetData: " + sd.err;
            s_audio.stage = AudioJob::Failed;
            return nullptr;
        }

        d.pcm.clear();
        d.pcm.shrink_to_fit();
        s_audio.clip = clip.ret;
        s_audio.stage = AudioJob::Ready;
        Log::Printf("[Bridge] AudioClip ready (SetData ok)");
        return clip.ret;
    }

    // ---------- 入场后的标题修正 ----------
    static std::string s_pendingTitle;
    static std::string s_pendingArtist;
    static std::string s_pendingLabel;
    static std::string s_pendingScoreJson;
    static int s_titleFixDelay = 0;

    static void ApplyCustomTitle()
    {
        MonoObject* mgr = FindObjectOfType(g_clsManager);
        if (!mgr)
            return;
        MonoObject* leaf = CurrentLeafState(mgr);
        if (!leaf || mono_object_get_class(leaf) != g_clsInGameScene)
            return;

        // m_UIInGame 私有字段
        MonoClassField* fVm = mono_class_get_field_from_name(g_clsInGameScene, "m_UIInGame");
        if (!fVm)
            return;
        MonoObject* vm = nullptr;
        mono_field_get_value(leaf, fVm, &vm);
        if (!vm)
            return;
        MonoClassField* fPi = mono_class_get_field_from_name(g_clsUIInGame, "PlayInfo__BackingField");
        if (!fPi)
            return;
        MonoObject* playInfo = nullptr;
        mono_field_get_value(vm, fPi, &playInfo);
        if (!playInfo)
            return;

        // LocalizeTextKey.GetText(title)（RawText 直显）
        std::string titleUtf8 = s_pendingTitle;
        void* args[1] = { MonoHost::Str(titleUtf8.c_str()) };
        MonoHost::Inv ltk = MonoHost::InvokeStatic(g_clsLocalizeTextKey, "GetText", 1, args);
        if (!ltk.ok || !ltk.ret)
            return;
        // set_Title(LocalizeTextKey)——值类型参数：传装箱数据的裸指针
        void* setArgs[1] = { mono_object_unbox(ltk.ret) };
        MonoHost::InvokeByName(g_clsUIPlayInfo, playInfo, "set_Title", 1, setArgs);
    }

    // ---------- 启动日志（只跑一次）：确认音频子系统可用 ----------
    static void LogAudioInfoOnce()
    {
        static bool s_done = false;
        if (s_done)
            return;
        s_done = true;

        // AudioSettings.outputSampleRate
        MonoImage* imgAudio = MonoHost::Image("UnityEngine.AudioModule");
        MonoClass* clsAS = imgAudio ? MonoHost::Class(imgAudio, "UnityEngine", "AudioSettings") : nullptr;
        if (clsAS)
        {
            MonoHost::Inv r = MonoHost::PropGet(clsAS, nullptr, "outputSampleRate");
            int rate = (r.ok && r.ret) ? *(int*)mono_object_unbox(r.ret) : -1;
            MonoHost::Inv r2 = MonoHost::PropGet(clsAS, nullptr, "speakerMode");
            int sm = (r2.ok && r2.ret) ? *(int*)mono_object_unbox(r2.ret) : -1;
            Log::Printf("[Bridge] AudioSettings: outputSampleRate=%d speakerMode=%d", rate, sm);
        }
        else
        {
            Log::Printf("[Bridge] AudioSettings class not found");
        }

    }

    // ---------- 播放请求执行 ----------
    static void ProcessPlayQueue()
    {
        PlayRequest req;
        {
            std::lock_guard<std::mutex> lk(s_playMx);
            if (s_playQueue.empty())
                return;
            req = std::move(s_playQueue.front());
            s_playQueue.pop_front();
        }

        Log::Printf("[Bridge] play request: audio='%ls' title='%s'", req.audioPath.c_str(),
                    req.title.c_str());

        // 1) 音频
        if (!StartAudioLoad(req.audioPath))
        {
            SetError("audio load start failed: " + s_audio.error);
            return;
        }
        s_pendingTitle = req.title;
        s_pendingArtist = req.artist;
        s_pendingLabel = req.chartLabel;
        // ScoreData JSON 先备好（纯字符串操作）
        s_pendingScoreJson = req.scoreJson;
    }

    // 音频就绪后的完整进场
    static void LaunchWithClip(MonoObject* clip)
    {
        if (!clip)
        {
            SetError("audio clip is null");
            return;
        }
        // 1) ScoreData：new ScoreData() + JsonUtility.FromJsonOverwrite(json, obj)
        //   （FromJsonOverwrite 为非泛型，与游戏 DataStorage 的 FromJson 行为一致；
        //     避免开放泛型被直接 JIT 的运行时断言）
        MonoObject* scoreData = MonoHost::New(g_clsScoreData);
        if (!scoreData)
        {
            SetError("ScoreData alloc failed");
            return;
        }
        {
            void* jsonArgs[2] = { MonoHost::Str(s_pendingScoreJson.c_str()), scoreData };
            MonoHost::Inv sd = MonoHost::InvokeStatic(g_clsJsonUtility, "FromJsonOverwrite", 2, jsonArgs);
            if (!sd.ok)
            {
                SetError("ScoreData parse: " + sd.err);
                return;
            }
        }
        MonoObject* sdRet = scoreData;

        // 2) ScoreInfo
        MonoObject* info = MonoHost::New(g_clsScoreInfo);
        if (!info)
        {
            SetError("ScoreInfo alloc failed");
            return;
        }
        auto setField = [](MonoObject* obj, MonoClass* cls, const char* name, void* value) {
            MonoClassField* f = mono_class_get_field_from_name(cls, name);
            if (f)
                mono_field_set_value(obj, f, value);
        };
        setField(info, g_clsScoreInfo, "Id", MonoHost::Str("YunYunLove_Custom"));
        setField(info, g_clsScoreInfo, "MusicName", MonoHost::Str(s_pendingTitle.c_str()));
        {
            int lv = 1;
            setField(info, g_clsScoreInfo, "Level", &lv);
        }
        long long ts = (long long)time(nullptr);
        setField(info, g_clsScoreInfo, "Timestamp", &ts);

        // 3) ScoreDataSet
        MonoObject* sds = MonoHost::New(g_clsScoreDataSet);
        if (!sds)
        {
            SetError("ScoreDataSet alloc failed");
            return;
        }
        setField(sds, g_clsScoreDataSet, "MusicName", MonoHost::Str(s_pendingTitle.c_str()));
        setField(sds, g_clsScoreDataSet, "Info", info);
        setField(sds, g_clsScoreDataSet, "LevelData", (MonoObject*)nullptr);  // null：跳过排行/本地化
        setField(sds, g_clsScoreDataSet, "Clip", clip);
        setField(sds, g_clsScoreDataSet, "ScoreData", sdRet);

        // 4) ParameterHolder
        MonoObject* ph = MonoHost::New(g_clsParamHolder);
        if (!ph)
        {
            SetError("ParameterHolder alloc failed");
            return;
        }
        {
            void* a[2] = { MonoHost::Str("Mode"), MonoHost::EnumVal(g_clsInGameMode, 0) };  // Normal
            MonoHost::Inv r = MonoHost::InvokeByName(g_clsParamHolder, ph, "Set", 2, a);
            if (!r.ok)
            {
                SetError("PH.Set Mode: " + r.err);
                return;
            }
            void* a2[2] = { MonoHost::Str("Data"), sds };
            r = MonoHost::InvokeByName(g_clsParamHolder, ph, "Set", 2, a2);
            if (!r.ok)
            {
                SetError("PH.Set Data: " + r.err);
                return;
            }
        }

        // 5) 进入 InGameScene —— 必须与游戏自身的进场方式一致：
        //      · 菜单界面开新歌 = HUDScene.OnSong() / TitleScene.PushInGameScene()
        //          的 Manager.Push<InGameScene>(menuState, ph) —— 父状态必须保留；
        //      · 结算/暂停/重玩再进场 = InGameScene.ResultComplete(retry) 的
        //          Manager.GoTo<InGameScene>(inGameState, ph)。
        //    早期版本一律 GoTo(leaf)：GoTo 会 ExitAll 掉 leaf 并销毁它 Context 拥有的
        //    对象（Unity 的 Destroy 延迟到帧末），菜单状态持有的 UIMenuHeader / Sound
        //    等注入服务随之失效 —— 下一局进场时 InGameScene.OnPreEnter() 直接空引用
        //    → TransitionHangException → 状态树卡死、第二首歌怎么点都进不去。
        MonoObject* mgr = FindObjectOfType(g_clsManager);
        if (!mgr)
        {
            SetError("GameStateManager not found");
            return;
        }
        MonoObject* topInGame = nullptr;
        MonoObject* leaf = WalkActiveChain(mgr, &topInGame);
        if (!leaf)
        {
            SetError("current leaf state not found");
            return;
        }
        MonoObject* from = topInGame ? topInGame : leaf;
        const char* verb = topInGame ? "GoTo" : "Push";
        void* goArgs[2] = { from, ph };
        Log::Printf("[Bridge] launch: %s from %s (leaf=%s)", verb,
                    MonoHost::ClassName(mono_object_get_class(from)).c_str(),
                    MonoHost::ClassName(mono_object_get_class(leaf)).c_str());
        MonoHost::Inv r = MonoHost::InvokeGenericManaged(g_clsManager, verb, 2,
                                                         g_clsInGameScene, mgr, goArgs);
        if (!r.ok)
        {
            SetError(std::string(verb) + ": " + r.err);
            return;
        }
        Log::Printf("[Bridge] %s InGameScene scheduled OK", verb);
        s_titleFixDelay = 3;   // N 帧后修正标题
        // 进场后停掉菜单 BGM。原生 HUDScene.OnSong() 是在等淡入结束才停一次；
        // 我们直接 GoTo 没有这一步，因此在这里补上，并隔几帧重试若干次，
        // 避免淡入/场景进入流程把 HUD BGM 又拉起来。
        s_stopMenuAudioDelay = 3;
        s_stopMenuAudioRepeats = 4;
    }

    // SEH 守卫（无对象展开的函数承载）
    static void TitleFixGuarded()
    {
        __try
        {
            ApplyCustomTitle();
        }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
                      ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
        {
            Log::Printf("[Bridge] title fix crashed (swallowed)");
        }
    }

    // ---------- 停掉菜单音频 ----------
    // 游戏原生流程 App.HUDScene.OnSong() 在 Push 完 InGameScene 之后会调用
    //   Sound.StopSong(); Sound.Bgm.Stop();
    // 我们直接 GoTo 跳过了这一步。标题 BGM 挂在常驻音频服务（SoundMediator）
    // 上，不会随场景切换自动停止，因此必须手动补上——否则标题 BGM 会和自定义
    // 谱面音频同时播放，听感上就是“音画对不上”。
    static void StopMenuAudioImpl()
    {
        MonoObject* snd = nullptr;

        // 首选：当前活动 GameState 的 Sound 属性（AppGameState.get_Sound）。
        //   该 getter 在托管侧执行 ServInjector.Resolve<ISound>()，比我们自己去
        //   膨胀开放泛型更可靠（旧的 InflateGeneric 路径在 Mono 上会断言失败）。
        if (g_clsAppGameState)
        {
            MonoObject* mgr = FindObjectOfType(g_clsManager);
            MonoObject* leaf = mgr ? CurrentLeafState(mgr) : nullptr;
            if (leaf)
            {
                MonoHost::Inv r = MonoHost::PropGet(g_clsAppGameState, leaf, "Sound");
                if (r.ok && r.ret)
                    snd = r.ret;
                else
                    Log::Printf("[Bridge] stop menu audio: Sound property failed (%s)", r.err.c_str());
            }
        }
        // 兜底：ServInjector.Resolve<ISound>()（走托管 MethodInfo.Invoke）
        if (!snd && g_clsServInjector && g_clsISound)
        {
            MonoHost::Inv r = MonoHost::InvokeGenericManaged(g_clsServInjector, "Resolve", 0,
                                                             g_clsISound, nullptr, nullptr);
            if (r.ok && r.ret)
                snd = r.ret;
            else
                Log::Printf("[Bridge] stop menu audio: Resolve<ISound> failed (%s)", r.err.c_str());
        }
        if (!snd)
        {
            Log::Printf("[Bridge] stop menu audio: ISound unavailable");
            return;
        }

        // 直接用实例自己的类调用：即使接口 MonoClass* 解析不到，
        // FindMethod/PropGet 也会沿「父类 → 接口」链找到 StopSong / get_Bgm。
        MonoClass* sndCls = MonoHost::ClassOf(snd);
        if (!sndCls) sndCls = g_clsISound;
        if (!sndCls)
        {
            Log::Printf("[Bridge] stop menu audio: no class for ISound instance");
            return;
        }

        MonoHost::Inv song = MonoHost::InvokeByName(sndCls, snd, "StopSong", 0, nullptr);
        if (!song.ok && g_clsISound && sndCls != g_clsISound)
            song = MonoHost::InvokeByName(g_clsISound, snd, "StopSong", 0, nullptr);

        MonoHost::Inv bgm = MonoHost::PropGet(sndCls, snd, "Bgm");
        if ((!bgm.ok || !bgm.ret) && g_clsISound && sndCls != g_clsISound)
            bgm = MonoHost::PropGet(g_clsISound, snd, "Bgm");

        int bgmStop = -1;
        if (bgm.ok && bgm.ret)
        {
            MonoClass* bcls = MonoHost::ClassOf(bgm.ret);
            if (!bcls) bcls = g_clsBgmController;
            float zero = 0.f;
            void* a0[1] = { &zero };
            // BgmController 只有 Stop(Single)；先试无参，再退回单参重载。
            MonoHost::Inv st = MonoHost::InvokeByName(bcls, bgm.ret, "Stop", 0, nullptr);
            if (!st.ok)
                st = MonoHost::InvokeByName(bcls, bgm.ret, "Stop", 1, a0);
            bgmStop = st.ok ? 1 : 0;
            if (!st.ok)
                Log::Printf("[Bridge] stop menu audio: Bgm.Stop failed (%s)", st.err.c_str());
        }
        else
        {
            Log::Printf("[Bridge] stop menu audio: Bgm property failed (%s)", bgm.err.c_str());
        }
        Log::Printf("[Bridge] menu audio stopped (cls=%s StopSong=%d Bgm=%p BgmStop=%d retries=%d)",
                    MonoHost::ClassName(sndCls).c_str(), song.ok ? 1 : 0,
                    bgm.ok ? (void*)bgm.ret : nullptr, bgmStop, s_stopMenuAudioRepeats);
        if (!song.ok)
            Log::Printf("[Bridge] stop menu audio: StopSong failed (%s)", song.err.c_str());
    }

    static void StopMenuAudioGuarded()
    {
        __try
        {
            StopMenuAudioImpl();
        }
        __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
                      ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
        {
            Log::Printf("[Bridge] stop menu audio crashed (swallowed)");
        }
    }

    // ---------- 主线程 Tick ----------
    void TickMainThread()
    {
        if (!g_ready.load(std::memory_order_acquire))
        {
            if (!ResolveClasses())
                return;
        }

        // 0) 启动日志（一次）
        LogAudioInfoOnce();

        // 1) 播放请求
        ProcessPlayQueue();

        // 2) 音频装载推进：解码完成 → 建 clip → 进场
        if (s_audio.stage == AudioJob::Decoding)
        {
            MonoObject* clip = PollAudioDecode();
            if (clip)
                LaunchWithClip(clip);
            else if (s_audio.stage == AudioJob::Failed)
                SetError("audio failed: " + s_audio.error);
        }

        // 3) 标题修正（延迟数帧等 UI 起来）
        if (s_titleFixDelay > 0 && --s_titleFixDelay == 0)
        {
            TitleFixGuarded();
        }

        // 3b) 进场后停菜单 BGM（延迟数帧 + 重试，避免与谱面音频重叠）
        if (s_stopMenuAudioDelay > 0 && --s_stopMenuAudioDelay == 0)
        {
            StopMenuAudioGuarded();
            if (--s_stopMenuAudioRepeats > 0)
                s_stopMenuAudioDelay = 15;   // 约 0.25s 后再补一次
        }

        // 4) 状态发布（游玩中自动隐藏覆盖层）
        {
            static DWORD s_last = 0;
            DWORD now = GetTickCount();
            if (now - s_last > 250)
            {
                s_last = now;
                MonoObject* mgr = FindObjectOfType(g_clsManager);
                MonoObject* leaf = mgr ? CurrentLeafState(mgr) : nullptr;
                bool inGame = leaf ? (mono_object_get_class(leaf) == g_clsInGameScene) : false;
                bool prev = g_inGameplay.exchange(inGame, std::memory_order_relaxed);
                if (prev != inGame)
                    Log::Printf("[Bridge] state -> %s (leaf=%s)", inGame ? "IN-GAME" : "menu",
                                leaf ? MonoHost::ClassName(mono_object_get_class(leaf)).c_str() : "(none)");
            }
        }
    }
}
