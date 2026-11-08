# 稳定运行时恢复与零差异核验

## 1. 核验方式

本分支严格从 `b32d185` 新建，从未合入 `origin/main`，也从未把 main 线的运行时文件复制进来。因此「恢复稳定实现」的判据是**净差异为零**，而不是逐文件比对内容描述：[STATICALLY_CONFIRMED]

```bash
git diff --name-only b32d185..HEAD -- \
  src/Hook src/Client src/Features src/Utils/Render include resources
# -> 空输出
```

补充核查：按名称枚举 `OpenGLGui` / `D3DHook` / `SwapBuffers` / `ImGui` / `ClickGui` / `event_dispatcher` / `ModuleManager` / `FeatureManager` / `ModuleBase` / `ESP` / `Tracers` / `Nametags` / `Watermark` / `Arraylist` / `Notifications` / `Interface` / `Keystrokes` / `ChinaHat` / `TargetHUD` / `MathUtils` / `FrameTransform` / `OWorldToScreen` / `FOV` / `HookManager` / `Detour` / `Aura` 等 25 类运行时文件，与 `b32d185..HEAD` 改动集合取交集为空。[STATICALLY_CONFIRMED]

`git diff --name-only b32d185..HEAD` 的全部 18 个代码/工具文件均落在 `cmake/`、`src/SDK/`、`src/Solstice.cpp`、`src/Utils/MemUtils.*`、`src/Utils/SysUtils/ExceptionHandler.*`、`tools/` 允许集内；加入本组交付文档后新增路径全部在 `docs/solstice_reconstruction/` 与 `tools/`。[STATICALLY_CONFIRMED]

## 2. 明确保留的稳定实现

| 领域 | 保留内容 | 等级 |
| --- | --- | --- |
| GL 渲染出口 | `OpenGLGui::render(HDC)` 为唯一呈现路径；`glGuiActive` 后 D3D Present 短路 | `STATICALLY_CONFIRMED` |
| D3D / SwapBuffers | `D3DHook.cpp` 及相关 Hook 原样 | `STATICALLY_CONFIRMED` |
| ImGui | OpenGL / Win32 backend、Modern ClickGUI 原样 | `STATICALLY_CONFIRMED` |
| 事件系统 | `include/nes/include/nes/event_dispatcher.hpp` 与 RenderEvent Dispatcher 原样，未引入 `diagnostic_trigger()` | `STATICALLY_CONFIRMED` |
| 模块系统 | `ModuleManager` / `FeatureManager` / `ModuleBase` 原样 | `STATICALLY_CONFIRMED` |
| 视觉模块 | ESP / Tracers / Nametags / Watermark / Arraylist / Notifications / Interface / Keystrokes / ChinaHat / TargetHUD 原样 | `STATICALLY_CONFIRMED` |
| 数学/投影 | `MathUtils` / `FrameTransform` / `OWorldToScreen` / FOV 原样 | `STATICALLY_CONFIRMED` |
| Hook 管理 | `HookManager::init()`、MinHook 初始化、Detour 生命周期、Aura 原始实现原样 | `STATICALLY_CONFIRMED` |
| CMakeLists | 与 b32d185 逐字一致，未引入 `SOLSTICE_RENDER_DIAG` / `SOLSTICE_DISABLE_AURA_DIAGNOSTIC` 等诊断选项 | `STATICALLY_CONFIRMED` |

被明确排除的 main 线实验性改动：`07b3d86` GL 三阶段诊断标记、`73da588` Aura 诊断隔离、`5ecf458` 首帧 RenderEvent 故障包含与 stage markers、`e58ed3e` 移除 per-frame detour、`1f95ebc` 停用 RenderEvent fan-out / 默认启用 Aura2、`bf1522f` / `11ff965` / `a7158d4` / `54f9188` ESP 投影探针、`4141241` 的 26 文件运行时 revert。[STATICALLY_CONFIRMED]

## 3. Hook 初始化冲突 — 正面处理

用户明确区分两轮故障：① GL / RenderEvent / GUI 崩溃；② `RenderItemInHandHook::init()` 附近 `Created detour` 后卡死，怀疑初始化期间单独提前 `mDetour->enable()`。用户要求恢复稳定 Hook 初始化和统一 `MH_EnableHook(MH_ALL_HOOKS)`，并**禁止重新引入**初始化期间的单独提前 enable。[STATICALLY_CONFIRMED]

实测事实与冲突：

| 事实 | 证据 | 等级 |
| --- | --- | --- |
| `HookManager::init(false)` 在全部 `hook->init()` 之后统一调用 `MH_EnableHook(MH_ALL_HOOKS)` | `HookManager.cpp:77` | `STATICALLY_CONFIRMED` |
| `HookManager::init(false)` 由 `Solstice.cpp:382` 调用 | 源码 | `STATICALLY_CONFIRMED` |
| `HookManager::init(true)` 由 `Solstice.cpp:461` 调用，**该分支没有统一 enable** | 源码 | `STATICALLY_CONFIRMED` |
| `RenderItemInHandHook.cpp:28` 与 `BaseTickHook.cpp:106` 各自含 `mDetour->enable()` | 源码 | `STATICALLY_CONFIRMED` |
| 这两处在 `b32d185` 与 `origin/main` 中**逐字相同**，不是本整合引入的 | `git diff b32d185 HEAD -- <file>` 为空；`git diff --stat b32d185..origin/main -- src/Hook` 仅含 `BaseTickHook.cpp` 的一行 `if (!mPacket) continue;` 删除和 `OpenGLGui.cpp` 诊断 | `STATICALLY_CONFIRMED` |

**裁定与风险说明**：

1. 本整合**没有重新引入**任何提前 enable；两处都是稳定基线自带，且 Commit 6 的零差异要求禁止擅自改动。[STATICALLY_CONFIRMED]
2. `BaseTickHook` 的提前 enable **不能删除**：它属于 `init(true)` 路径（LocalPlayer 出现时才安装），该路径没有统一 `MH_EnableHook`，删掉会让 `Actor::baseTick` 永远不生效。[STATICALLY_CONFIRMED]
3. `RenderItemInHandHook` 的提前 enable 理论上可由 `init(false)` 末尾的 `MH_EnableHook(MH_ALL_HOOKS)` 覆盖，删除它在稳态下行为等价；但用户描述的第二轮卡死发生在**非本分支的构建**上，本分支没有该运行期证据，无法证明删除能消除故障，也无法证明删除不会改变首帧 Hook 生效时序。[UNVERIFIED]
4. 因此该项**不改动、明确列为待决风险 R-04**（见 `compatibility-risks.md`），并把「是否需要单独删除这一行」作为需要用户确认的独立变更，而不是塞进「零差异」的 Commit 6。[DEFERRED]

若用户确认要移除，最小改动是删除 `src/Hook/Hooks/RenderHooks/RenderItemInHandHook.cpp:28` 一行，并在有运行期 A/B 证据的前提下单独提交；本分支不预先执行。[DEFERRED]

## 4. RenderEvent 派发相关说明

Batch 3.6.2-F 的静态考古结论（Dispatcher 同步遍历、无异常隔离、`shared_lock` 下回调内 `listen/deafen` 可能锁升级死锁、注册顺序不等于执行顺序、`patchFullStack at 0x0` 为宏安全早返回的 `BENIGN_SIGNATURE_MISS`）在本整合中**没有被当作运行时修复依据**，也未引入任何订阅者级诊断代码。[STATICALLY_CONFIRMED]

因缺少本分支的运行期日志 / Dump，GL 第一帧 RenderEvent 卡死的具体订阅者仍是 `NEEDS_RUNTIME_CAPTURE` / `UNVERIFIED`；本整合的立场是「不把实验性诊断堆进稳定运行时」，而不是宣称根因已消除。[DEFERRED]

## 5. 结论

Commit 6 的验收目标是「非 SDK 运行时代码 = b32d185」，该目标以 git 净差异为空直接证明，而不是以人工阅读代码推断。唯一未闭合项是上文 Hook 提前 enable 的语义冲突，已明确标为 `DEFERRED` 并交回用户决策。[STATICALLY_CONFIRMED]
