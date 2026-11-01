# COMPATIBILITY_MATRIX — 模块级兼容状态

状态口径：

* **OK** — 依赖的符号全部唯一命中，且已用镜像字节核对过调用形态。
* **DEGRADED** — 符号能解析，但**调用约定或常量归属被证明不对**，已修或已披露。
* **DEAD** — 依赖符号 0 命中，功能整体不成立。
* **UNKNOWN** — 缺运行时证据，不能判定。

## 1. 四个目标模块

| 模块 | 状态 | 依赖 | 迁移方法 | 测试 |
|---|---|---|---|---|
| ViewModel | OK | `GameRenderer_bobHurt`（唯一命中，prologue 与 `glm::mat4*` 出参一致） | 无需改 | 无构建验证；静态：目标地址可反汇编、出参寄存器为 `rdx` |
| TargetHUD | DEGRADED | `Actor` 生命/护甲经 SDK；`Actor_mHurtTimeComponent` **DEAD** | 受伤组件相关分支已自行判空返回 | 静态：`getMobHurtTimeComponent()` 在 `OffsetProvider::Actor_mHurtTimeComponent==0` 时返回 nullptr，不会崩 |
| HudEditor | OK（依赖渲染链） | 纯 ImGui | 无 | 静态：不引用任何游戏符号 |
| Animations | DEGRADED → 已修 2 处 | `FluxSwing` **DEAD**；`TapSwingAnim` 指错常量；`NoSwitchAnimation` 越界 4 字节 | 见 `CHANGELOG.md` | 静态：常量值 `-80.000°` 与 `-14.000°` 已逐字节核对；补丁边界已用 capstone 核对 |

## 2. 渲染/钩子层

| 组件 | 状态 | 说明 |
|---|---|---|
| `D3DHook`（D3D11/DX12 Present） | UNKNOWN | 代码在，但本机运行时日志从未出现 Present 记录 → 该路径不被执行 |
| `OpenGLGui`（`gdi32!SwapBuffers` / `opengl32!wglSwapBuffers`） | DEGRADED | 唯一实际生效的 UI 出口；窗口校验逻辑已改（历史 commit），**未经本轮运行验证** |
| `SetupAndRenderHook` | DEAD | `ScreenView_setupAndRender` 0 命中 |
| `ItemRendererHook` | DEAD | `ItemRenderer_render` 0 命中 |
| `HoverTextRendererHook` | DEAD | `HoverTextRenderer_render` 0 命中 |
| `AnimationHooks`（骨骼/姿态） | DEAD | `ActorAnimationControllerPlayer_applyToPose` 0 命中；`Mob_getCurrentSwingDuration` 0 命中 |
| `ActorRenderDispatcherHook` | OK | 唯一命中 |
| `NametagRenderHook` | OK | `LevelRendererPlayer_renderNametag` 唯一命中 |
| `BobHurtEvent` | OK | 见 ViewModel |

## 3. 世界/实体 SDK

| 接口 | 状态 | 迁移方法 |
|---|---|---|
| `Level::getHitResult()` | DEAD（返回 null） | 已改为“调用方自己做射线检测”（`Animations` 里的 `rayHitsBox`）；其余模块仍需自行处理 |
| `Level::getLevelData()` | DEAD | 返回 null；`setTick()` 增加了范围校验 |
| `Level::getPlayerList()` | DEAD | 返回 null，调用方需判空 |
| `Level::getPlayerMovementSettings()` | DEGRADED | 槽号 72 指向的函数实为 `void f(Level*, shared_ptr*)`（读 `[rcx+0x8E0]`），按 `T* f()` 调用会把 `rdx` 当出参写 → **调用约定不匹配** |
| `Level::getBlockPalette()` | UNKNOWN | 槽号 436 由同一宏推出，未验证 |
| `Actor::getMobHurtTimeComponent()` | DEAD | 偏移未解析，返回 null |
| `Container::getItem()` | OK（已固化） | `DEFINE_FIELD(Container_getItem, 8)`，依据 RTTI `.?AVInventory@@` 槽 8 反汇编 |
| `ClientInstance::mLevelRenderer` | OK（已固化） | `DEFINE_FIELD(..., 0xF0)` |

## 4. 字节补丁矩阵（本轮新增审计）

`tools/re/audit_patches.py` 对每个补丁点做两项机械判定：是否切断指令、被删掉的寄存器是否
仍被读取。

| 补丁 | 站点 | 原始指令 | 判定 |
|---|---|---|---|
| `NoSwitchAnimation`（4B） | `0x58EB4EC` | `mulss xmm0, xmm7` | **OK**：对齐；`xmm0` 由补丁自身重新定义 |
| `NoSwitchAnimation`（旧 8B 形态） | 同上 | 额外覆盖 `movaps xmm1, xmm9` | **已修**：`xmm1` 在 `0x58EB4FF` 被读 |
| `DefaultSwing` | `0x58FB1B0` | `movss xmm3, [rcx+0x8D0]` | **OPEN**：`xmm3` 在 `0x58FB1C3` 被 `subss` 读，补丁未定义它 |
| `DefaultSwing2` | `0x58FB1BB` | `movss xmm0, [rcx+0x87C]` | **OPEN**：`xmm0` 同上被读 |
| `FluxSwing` | 未解析 | — | **DEAD** |
| `patchInHandSlot`（`Solstice.cpp:69`） | `0x1CC86E7` | `mov rcx,[rsi+0x60]` 的 **disp 低字节** | **OPEN**：签名命中 `0x1CC86E5`，比真正指令起点晚 1 字节；`+2` 落在 disp 上属巧合 |

`DefaultSwing*` 与 `patchInHandSlot` 的处置建议见 `REVERSE_ENGINEERING_REPORT.md §4`，
本轮**没有改**，因为改完无法在本机验证（见 `BUILD_BASELINE.md §3`）。

## 5. 计数总览

```
signatures 115：ok 74 / ambiguous 21 / failed 20
offsets     58：ok 25 / ambiguous 12 / failed 21
未解析合计（failed + ambiguous）= 74 条声明；去重后 72 个名字
（ClientInstance_grabMouse / ClientInstance_releaseMouse 在两张表里各声明一次）
其中被源码实际引用 = 54 个，纯声明未用 = 18 个
```

> 客户端日志里那行 `Failed to find 43 signatures/offsets!` 与 `sigreport.json` 的
> 41（20+21）不一致，差 2；这属于客户端计数口径问题，不影响本表——本表逐名核对过两次
> （线上 `sigreport.json` ↔ 离线 `baseline_offline.json`，四个集合完全相同）。

## 6. 编译验证状态（阶段二，2026-11-01）

**所有模块的编译状态一律为 `NOT COMPILED`**，没有例外。理由：本机无 MSVC/SDK/Ninja，
两次干净副本配置都停在工具链探测阶段（`logs/configure-ninja.log`、`logs/configure-default.log`，exit 1）。

| 模块组 | 编译 | 单模块可独立编译性 | 备注 |
|---|---|---|---|
| `Features/Modules/Visual/**`（含 Animations/TargetHUD/HudEditor/ViewModel） | NOT COMPILED | 否 | 依赖 `Solstice` 目标整体（单文件无法脱离 `pch.hpp` + 全局单例） |
| `Features/Configs/**` | NOT COMPILED | 否 | 同上 |
| `Utils/SysUtils/Logger`、`ExceptionHandler` | NOT COMPILED | 否 | 依赖 `build_info.h`（配置期生成） |
| `Utils/Resources` | NOT COMPILED | 否 | 依赖 `ld -r -b binary` 生成的资源符号 |
| `SDK/**`、`Hook/**` | NOT COMPILED | 否 | 依赖 `libhat`/`minhook` 子工程 |

关于“对独立模块做编译测试”：本工程是**单一 SHARED 目标**（`add_library(Solstice SHARED ${sourceFiles})`，
`sourceFiles` 为全量 `src/**` 的 GLOB），没有 per-module target，也没有 `Tests/`。
在不动 `CMakeLists.txt` 的前提下，**“只编译 UI 模块”这个动作在物理上不存在**；
拆 target 属于会改变构建产物的结构性改动，本阶段不做（并已在 `MIGRATION_REPORT.md §3.1` 说明理由）。

因此本阶段对“独立模块”的可做验证降级为两条**非编译**检查，且都不升级模块状态：

1. include 闭包：2152 处工程内引用 0 断链（`TEST_REPORT.md §7` 测试 26）。
2. 改动文件引用的外部标识符均有声明（测试 27）。

## 7. 状态口径再确认

* `OK` / `DEGRADED` / `DEAD` 仍然只描述**符号解析层面**的事实，不是“功能可用”。
* 本阶段没有把任何模块从 UNKNOWN 升级，也没有因为“能解析 include”而下调风险。
* 任何“编译通过/运行正常”的结论，必须等构建机日志与注入日志，二者缺一不可。
