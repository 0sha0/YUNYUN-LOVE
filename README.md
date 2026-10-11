# YUN-YUN-LOVE

云云症候群（Yunyun Syndrome ~Rhythm Psychosis~ / Steam，Unity 6 · Mono · D3D11/D3D12）
的自定义谱面商城外挂式辅助（注入 DLL + DXGI/ImGui 覆盖层 + Mono 逆向桥）。

导入 4K 谱面包（`.zip`：Malody `.mc` 或 osu!mania `.osu`）后，以**商城卡片**形式浏览，
点开曲目 → 选择难度 → **进入游戏原生游玩界面**游玩你导入的谱面
（打击反馈、连击、晕晕动画、结算全部为游戏原生逻辑）。

> 作者：**是ShaShen吖**

> 仅供学习与个人使用，请支持正版游戏。请勿用于联机/排行等破坏他人体验的场景。

## 功能

- **谱面商城**：顶部 `搜索框 + 搜索 + 导入 Malody 谱面`，右上角 `✕`（点击收成悬浮球，
  再点还原；设置入口在左侧栏）；下方 5×3 大卡片网格
  （封面 + 曲名 + 艺术家/版本）；底部居中的分页 `< 1 2 3 … N >`。
  封面按页懒加载、翻页回收显存（不会一次性加载全部图片）。
- **导入谱面包**：选择 `.zip` → 自动解包 → 递归解析全部 `.mc`（Malody，JSON）与
  `.osu`（osu!mania，纯文本）→ **自动过滤非 4K 谱面**（Malody `meta.mode == 0` 且
  `meta.mode_ext.column == 4`；osu `Mode:3` 且 `CircleSize:4`；taiko / catch /
  osu!std / 5K+ 一律丢弃，日志记 `[Library] filter non-4K chart`）→ 每张谱面作为一张
  独立难度卡 → 提取 BGM（.ogg/.mp3/.wav）与封面。
- **曲目详情页**：左上角 `← 返回`；左侧封面 + 曲目信息，右侧难度列表（谱师 / 物量 /
  BPM / 时长），选中难度后 **▶ 开始游玩**。
- **删除曲包**：商城卡片右上角悬停出现垃圾桶角标（详情页右上角同样有 `删除曲包`）→
  弹出窗口内自绘的确认框（非系统对话框）→ 确认后递归删除 `Store\<曲包id>` 并重新扫描。
  条目 ID 经过白名单 + 路径穿越校验，非法 ID 一律拒绝。
- **原生游玩**：构建云云症 `YYNoteEditor.ScoreData`（经 `JsonUtility` 反序列化，走游戏
  自身装载通道）+ 真实采样的 `AudioClip` → `GameStateManager.GoTo<InGameScene>`。
  进入游玩时会**补上原生 `HUDScene.OnSong()` 的停乐步骤**（`Sound.StopSong()` +
  `Sound.Bgm.Stop()`），否则 HUD/标题 BGM 会与自定义谱面音频同时播放，听感上就是
  “音画对不上”。
- **界面**：完全自绘的 1xSOFT 风格控件（`UiKit`，深灰分层 / 细描边 / 浅蓝主色 #80C0FF /
  缓动）；标题栏可拖动；可**缩成一个可拖动的小球**（点击还原）。
  窗口默认 1024×640，可**直接拖边框自由缩放**（右下角有三条斜线提示），松手后尺寸写入
  `config.json` 下次启动沿用；设置页提供 `重置窗口尺寸（默认 1024×640）`。
  界面整体会按窗口尺寸/屏幕分辨率自动适配（`Theme::TextScale`）。
  封面解码上限 1024px、每帧最多装载 1 张，避免翻页/切换曲包时的卡顿。
- **侧栏三页设置**：`游玩设置`（自动隐藏悬浮窗 / 谱面同步微调 / 界面缩放）、
  `商城设置`（重新扫描 / 清理封面缓存 / 重置窗口尺寸）、`关于`（居中应用图标 + 版本号 +
  作者 + 信息表）。`谱面同步微调` 滑块（−0.3 ~ +0.3 秒，负 = 音符提前）用于对齐个别
  作者同步不准的谱面（见下）。
- **快捷键**：`INSERT` 显隐界面，`END` 卸载。

## 使用

**便携版（推荐）**：`StartYunYunLove.bat` 一把梭 —— 自动开游戏、等窗口、注入覆盖层。
注入失败时右键「以管理员身份运行」。

**手动**：

1. 启动游戏（Steam），进入任意界面。
2. 运行 `Injector.exe`（自动寻找 `Yunyun_Syndrome.exe` 并注入**同目录**的
   `YunYunLove.dll`；也可 `Injector.exe <PID|进程名> <DLL路径>`）。
   源码构建产物在 `bin\Release\`。
3. 屏幕中央出现商城界面：
   - 点击 **导入 Malody 谱面** 选择 4K 谱面包（Malody `.mc` / osu!mania `.osu` 的 `.zip`）；
   - 点击卡片进入详情页 → 选择难度 → **▶ 开始游玩**。
4. 游玩中悬浮窗按设置自动隐藏；结束后自动恢复。

> 运行数据：`<游戏目录>\YunYunLove\`（`Store\<曲包id>\` 解包后的谱面 + `entry.txt`、
> `config.json` 设置）。日志：`%LOCALAPPDATA%\YunYunLove\log.txt`。

## 谱面包处理（Malody `.mc` / osu!mania `.osu`）

- **格式嗅探**：按内容分派 —— 前 15 字节为 `osu file format` 走 osu 解析器，否则按
  Malody JSON 解析（自动去 UTF-8 BOM）。
- 仅收录 4K 谱面（Malody `mode == 0 && column == 4`；osu `Mode:3 && CircleSize:4`）；
  同一 `.zip` 内的多张谱面全部收录为不同难度。
- 音符：`type` 缺省/`0` 为按键音符；带 `endbeat` 的为长条 → `HoldNotes`；`type == 1`
  且含 `sound` 的为声音轨注释（主 BGM 或打击音效）。
- **拍位**：`beat = [整拍, 分子, 分母]` → 绝对拍 `整拍 + 分子/分母`（**不是** `小节 × 4`）。
  全库离线实测：按 `整拍 + 分子/分母` 计算时，谱面末尾稳定落在音频结束前 0~1 秒（谱长/音频
  的全库中位数 0.978）；按 `小节 × 4` 计算会得到音频长度的约 3.9 倍（严重错位）。
- **BPM**：`time[]` 的全部时间点 → `InitBpm` + `BpmChangeEvents`（按拍位排序；单谱最多
  223 个 BPM 事件），逐段积分得到每个 tick 的秒数。
- **主 BGM 选取**：优先 `meta.song.file`；否则取最早且**不带 `column`** 的 `type == 1`
  声音（铺在谱面里的打击音效通常带 `column`）。导入时若该文件缺失，则在全部 `type == 1`
  声音里取**体积最大的实际文件**兜底。
- **音频偏移**：`type == 1` 的 `offset`（毫秒）是音频相对谱面的播放偏移，等价于
  `音频位置 = 谱面秒 − offset`（正值通常来自音频前导静音 / 编码延迟）。以 BGM 音符所在
  拍位为基准整体平移拍位。实测校准：把音符落点与音频 onset 能量包络做对齐打分，
  `谱面秒 − offset` 在 28 张含 offset 的 4K 谱中 23 张优于反号（0.284 vs 0.243）；
  对具体曲目逐拍复核（如 `1735529706.mc`，offset = 91 ms）音符落点与音频 onset
  的偏差 ≤ 10 ms，offset 为 0 的谱则为 0 ms 偏差 —— 即**偏移换算本身是对的**。
  个别谱面作者同步不准（同曲另一难度无 offset 字段，整体早约 100 ms）属于谱面自身
  问题，用设置页的 `谱面同步微调` 处理。
- 键位：Malody 列 `0..3` → 云云症 `S/D/K/L`（lane `2..5`）。
- **osu!mania（`.osu`）**：`HitObjects` 里 `type & 128` 为长条（结束时间取第 6 字段
  冒号前）、`type & 1` 为单点，滑条 / 转盘跳过；列号 `col = x * keys / 512`。
  BPM 表取全部 `uninherited == 1`（第 7 字段）且 `beatLength > 0` 的 `TimingPoints`：
  `bpm = 60000 / beatLength`，以音频 0 ms 为第 0 拍分段积分；`AudioFilename` 作为 BGM
  （大小写不敏感地在 zip 内匹配实际文件）。

- **超长路径**：Store 目录 + zip 内层目录 + 长文件名很容易超过 `MAX_PATH`(260)。
  读取侧统一走 `PathUtil::Long()` 加 `\\?\` 前缀；`std::filesystem` 的
  `remove_all` / `copy` 在超长路径下会失败（实测 `ERROR_DIR_NOT_EMPTY` /
  `ERROR_PATH_NOT_FOUND`），因此落盘、覆盖、删除曲包都改用自实现的
  `RemoveTree` / `CopyTree`（Win32 + `\\?\`）。
- 时间轴：`1 拍 = 480 tick`（与游戏 `TPQN` 一致）；输出保证 `Tick ≥ 0`（负 Tick 会让
  游戏的区间索引越界，落在音频起点之前的个别拍位会被贴到 0）。
- **兼容性**：解析器对 Malody 官方谱面库中的全部 **179 张 4K Key 谱**（31.7 万条音符、
  3.45 万条长条、36 张多 BPM 谱）离线校验；osu 侧另取多 BPM / 长条混合的 4K mania 谱
  逐项核对（物件数、长条数、BPM 段、时长），非针对单一谱面；旧版 `entry.txt` 会在加载时
  按新算法自动重算（`v=2` 元数据迁移）。

## 构建

- Visual Studio 2022（v143），x64，C++20，静态 CRT（/MT，产物不依赖 VC 运行库）。
- 直接构建 `YunYunLove.sln`（Release | x64），产物在 `bin\Release\`。

```
YunYunLove.sln
├── YunYunLove/     覆盖层 DLL（ImGui + 自绘 UI + Mono 桥 + 谱面/音频管线）
├── Injector/       注入器
├── assets/         程序图标（favicon.ico，由 Injector/YunYunLove 的 .rc 引用）
└── deps/           imgui 1.92.9 / Detours / miniz / stb_vorbis / dr_mp3 / freetype
```

> `deps/` 为随仓库一起提交的第三方源码 / 预编译静态库（imgui 1.92.9，
> upstream commit `01380c5`，已去掉其内部 `.git`，直接 `git add` 后克隆即可构建）；
> `freetype.lib` / `detours.lib` 分别来自 `deps\freetype\lib`、`deps\detours\lib`。
> 仓库不含构建产物：`bin/`、`obj/`、`.re/` 均在 `.gitignore` 中。

## 实现要点（逆向所得）

- 游戏：Unity 6000.0.58f2 · Mono（MonoBleedingEdge）· D3D11/D3D12。
- Unity 6 Mono 为协作式 GC：**所有 mono API 只在游戏主线程调用**（由 DXGI 钩子经
  `PostMessage` 驱动 WndProc 泵执行）；跨线程只读内存字段是安全的。
- 泛型方法（`GoTo<T>`）通过 `MakeGenericMethod` + 托管 `MethodInfo.Invoke` 调用；直接
  JIT 开放泛型会触发 mono `method-to-ir.c:6288` 致命断言。
- `mono_class_get_method_from_name` 只按"名字 + 参数个数"匹配，会误中同名开放泛型，
  需按首参类型精确过滤。
- **值类型参数约定**：`mono_runtime_invoke` 的 `int/bool/float` 等值类型参数必须传
  "指向裸值的指针"（`mono_object_unbox`），传装箱对象指针会让被调方读到对象头 →
  `System.ArgumentException`（曾是"没声音"的根因）。
- 音频：不再走 `UnityWebRequestMultimedia`（实测 `file://` ogg 解出 `samples=0` 的空 clip）；
  改为本地解码 PCM → `AudioClip.Create` + `SetData` 构造**有真实采样**的离线 clip。
  游戏用 `AudioSource.time`（`AudioFrameWatch.SmoothAudioSourceTime`）驱动判定时间轴，
  clip 无采样时时间不推进 → 表现为"没音乐、晕晕不动、无判定"。
- 停 HUD/菜单 BGM：原生 `HUDScene.OnSong()` 在 `PushInGameScene` 之后会
  `Sound.StopSong(); Sound.Bgm.Stop();`；我们直接 `GoTo` 跳过了这一步，必须自行补上，
  否则两首曲子叠着放。`App.ISound` 由 `AppGameState.get_Sound` 取得（托管侧走
  `ServInjector.Resolve<ISound>()`），实际实例是 `App.Audio.SoundMediator`；
  `StopSong()` 无参、`Bgm` 为属性、`BgmController.Stop(Single)` 单参。
  `MonoHost::FindMethod/PropGet` 会沿「类 → 接口 → 父类」链查找，因此拿不到接口
  `MonoClass*` 也能用实例类直接调用。
- 自定义谱面 `ScoreDataSet.LevelData = null`，结果页自动跳过排行/本地化路径；标题经
  `UIInGamePlayInfo.set_Title(LocalizeTextKey.GetText(...))` 修正为原始文本。
- **进场/重玩必须走对状态机入口**（这一条决定了"第二首歌进不去"）：`GameStateManager.GoTo<T>`
  会先 `ExitAll(stayLeaf)` 并**销毁除了指定叶子以外的所有状态**，而菜单状态持有的
  `UIMenuHeader` / `Sound` 等注入服务由 `Context` 负责注入 —— 被销毁后下一局
  `InGameScene.OnPreEnter()` 里的 `UIMenuHeader.SetVisible(false)` 会抛
  `NullReferenceException`，被 `AsyncLock` 吞成 `TransitionHangException`，表现就是
  **打完一首后点开始毫无反应**。所以：
  - 当前活动链上**没有** `InGameScene` 时（从标题/菜单进入）→ `Push`（与原生
    `HUDScene.OnSong()` / `TitleScene.PushInGameScene()` 一致，菜单状态得以保留）；
  - 链上**已有** `InGameScene` 时（结算页重玩）→ `GoTo`，父状态取链上最靠根部的
    那个 `InGameScene`，等价于原生 `InGameResultScene.ResultComplete` 的重玩路径。
  走错入口（一律 `GoTo`）会正常打完第一首，然后第二首永久卡死。
  活动链由 `GameStateManager` 的 `_states` 栈 + 每个状态的 `Parent` 反查（`WalkActiveChain`）。

## 第三方组件

| 组件 | 许可 |
| --- | --- |
| Dear ImGui | MIT |
| Microsoft Detours | MIT |
| miniz | MIT |
| stb_vorbis | Public Domain / MIT（双许可） |
| dr_mp3 | Public Domain / MIT-0 |
| FreeType | FTL / GPLv2（本工程按 FTL 使用） |

## License

本仓库代码：MIT（见 `LICENSE`）。第三方组件见上方表格与 `deps/` 内各自许可。
