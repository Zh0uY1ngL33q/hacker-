# SOURCE_MAP — 现有 Solstice 源码结构

审计对象：`solstice/solstice-src/solstice-master-main/solstice-master-main`（下文简称 `<root>`）

规模（脚本统计，非估计）：

```
src 下文件 623 个：260 .cpp / 362 .hpp / 1 .h，合计 61,307 行
Modules 目录 126 个 .cpp（Combat 7 / Misc 33 / Movement 25 / Player 24 / Visual 37）
```

## 1. 目录职责

| 路径 | 职责 | 备注 |
|---|---|---|
| `src/dllmain.cpp` | DLL 入口 | 31 行；`DllMain` → `CreateThread(initThread)` → `Solstice::init(hModule)` |
| `src/Solstice.cpp` | 初始化主机 | 日志 → MinHook → `OffsetProvider::initialize` → `SigManager::initialize` → `FeatureManager` → `HookManager::init(false)`；tick 线程里再 `HookManager::init(true)` |
| `src/SDK/` | 游戏对象镜像（头文件 + 极薄 .cpp） | 全部偏移来自 `OffsetProvider`，全部函数地址来自 `SigManager` |
| `src/SDK/Minecraft/` | 按游戏类分文件：`Actor/ World/ Inventory/ Network/ Rendering/ UWP/` | |
| `src/Hook/` | detour 层：`Detour.cpp`、`HookManager.cpp`、`Hooks/**` | 分 `ActorHooks/ ContainerHooks/ MiscHooks/ NetworkHooks/ RenderHooks/` |
| `src/Features/` | 功能层：`Modules/`（5 类 126 个）、`Events/`、`Command/`、`Configs/`、`GUI/`、`IRC/`、`Auth/` | |
| `src/Utils/` | `MemUtils`、`SysUtils`（日志/异常/进程）、`GameUtils`、`MiscUtils`、`Shaders` | |
| `include/` | 第三方：`ImGui/ Kiero/ entt/ libhat/ minhook/ nes/` | 随仓库自带，不走网络 |
| `cmake/` | `Dependencies.cmake`（CPM + `file(DOWNLOAD)`）、`CPM.cmake` | |

## 2. 初始化与资源释放（实际代码顺序）

```
DllMain(DLL_PROCESS_ATTACH)
  └ DisableThreadLibraryCalls + CreateThread(initThread)      // 不用 UWP dispatcher，见文件内注释
      └ Solstice::init(hModule)
          Logger::initialize()
          MinHook 初始化（失败则 critical 并退出）
          OffsetProvider::initialize()      // 59 个字段/槽位
          SigManager::initialize()          // 115 个函数地址
          gFeatureManager = make_shared<FeatureManager>(); init()
          HookManager::init(false)          // 非 tick 钩子
          ...
          tick 线程首次进入后 HookManager::init(true)   // BaseTick 钩子
```

释放路径：`Solstice::shutdown()` → `HookManager::shutdown()` → `SigManager::deinitialize()` /
`OffsetProvider::deinitialize()` → `Logger::deinitialize()`。

## 3. 依赖方向（谁可以调谁）

```
Features/Modules  ──► Features/FeatureManager ──► Events(Dispatcher)
        │                                              ▲
        ├──► SDK/Minecraft/**  ──► OffsetProvider / SigManager
        │            ▲                                 │
        └──► Utils/**└──────── Hook/Hooks/** ──────────┘
```

规则上只有一条硬约束：**`SDK/` 不许反向依赖 `Features/`**（当前代码满足）。

## 4. 已识别的问题区（不是“过时”，是结构性风险）

| 位置 | 问题 | 证据 |
|---|---|---|
| `SDK/OffsetProvider.hpp` | `DEFINE_INDEX_FIELD(..., OffsetType::Index)` 把 `mov rax,[rax+disp]; call [rip+cfgd]` 的 `disp/8` 当作虚表槽号 | CFG 派发形态见 `PE_ANALYSIS.md §2`；该机制**只对“返回裸指针的虚函数”成立** |
| 同上 | 槽号正确 ≠ 调用约定正确 | `Level` 槽 72 的实体是 `void f(Level*, shared_ptr*)`（读 `[rcx+0x8E0]` 并写 `rdx[0..0x10]`），而 `Level::getPlayerMovementSettings()` 按 `T* f()` 调用它 |
| `SDK/SigManager.hpp` | 若干签名匹配到**指令中间**而非指令边界 | `ItemInHandRenderer_renderItem_bytepatch2` 命中 `0x1CC86E5`，而真正指令从 `0x1CC86E4` 开始（`48 8B 4E 60`） |
| `Features/Modules/Visual/Animations.cpp` | 用签名地址当 `float*` 直接写常量 | 已在本次修复，见 `CHANGELOG.md` |
| `Solstice.cpp:69` | `gBpBytes = {0x1c}` 单字节改位移，`+2` 的落点靠巧合 | 同上，`+2` 正好是 disp 低字节；`0x1C` 不是 8 字节对齐，是否真为指针字段 **UNKNOWN** |

## 5. 原始源码保全

* 备份：`_backup/`（zip，git 忽略）+ `origin/main` 历史。
* 本次所有改动都在 4 个 commit 内，可逐个 `git revert`。
* **未做**任何目录搬迁/重写——原因见 `MIGRATION_REPORT.md §3`（本机无编译器，搬迁无法验证）。
