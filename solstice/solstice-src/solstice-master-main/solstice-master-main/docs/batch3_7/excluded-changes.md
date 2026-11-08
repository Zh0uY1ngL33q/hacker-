# 3.7 — 明确禁止移植的非 SDK 修改（工作组三）

## 总原则

第一轮整合中，`b32d185` 之后**除 `MinecraftSim.cpp` 的 `411fa4e` 重试机制外，
所有运行时源码改动一律不移植**。`git diff --name-status b32d185..origin/main`
共 122 个文件、其中 **41 个是运行时源码**（`.cpp/.hpp/.h`）；本轮只应用 1 个，
其余 40 个保持 `b32d185` 原始实现。

> 有些改动在 main 上其实是「相对 b32d185 的回退」（`4141241 revert` 把 Aura2、
> Actor、OpenGLGui 等恢复成更早的 `f8c3541` 形态）。以 `b32d185` 为分支起点，
> 这些文件**天然就是基线实现**，无需任何反向操作。这正是"起点必须是 b32d185"
> 而不是"从 e875543 改回去"的意义。

---

## 1. GL / GUI（D 类）

| 文件 | 差异 | 处理 |
|---|---|---|
| `src/Hook/Hooks/RenderHooks/OpenGLGui.cpp` | M `+126/-33`（7 个提交：5ecf458、d2dd463、1f95ebc、4141241、54f9188、07b3d86、e875543） | **禁止移植**。含三阶段 GL 诊断标记、`GetDrawData` 判空、ESP 投影探针、per-frame RenderEvent fan-out 的 park/unpark、3.6.2-F 订阅者诊断包装。b32d185 的 GL 渲染路径是本轮要恢复的"正常实现"。 |
| `src/Hook/Hooks/RenderHooks/D3DHook.cpp` | **零差异** | 天然保持基线（已用 `git diff --quiet` 验证） |
| ImGui OpenGL backend / Win32 backend | **零差异**（122 文件清单中无匹配） | 天然保持基线 |
| OpenGL Modern ClickGUI | `ClickGui.cpp` / `HudEditor.cpp` **零差异** | 天然保持基线 |

具体被拒的 GL/GUI 行为：
- 3.6.2-C 的 `HUD_DISPATCH_BEGIN/IMGUI_RENDER_BEGIN/GL_SUBMIT_BEGIN` 阶段标记；
- `GetDrawData()` 先判空再 `RenderDrawData()` 的容错；
- 3.6.2-F 的 `[RENDER-DIAG]` 订阅者级 `BEGIN/END`、`SOLSTICE_RENDER_DIAG` 构建开关；
- 额外的 GUI 异常隔离 / 首帧 RenderEvent 容错包装。

## 2. 事件系统（F 类）

| 文件 | 差异 | 处理 |
|---|---|---|
| `include/nes/include/nes/event_dispatcher.hpp` | M `+26/-0`（3.6.2-F 的 `diagnostic_trigger`） | **禁止移植** |
| RenderEvent 后续架构修改 | 无独立文件差异 | 保持基线 |
| 诊断订阅者包装 / 额外同步机制与锁修改 | 随上 | **禁止移植** |

理由：3.7 要恢复的是 `b32d185` 的**正常事件派发**，不是在稳定版上再叠一层
诊断或额外的 shared_mutex 逻辑。`diagnostic_trigger` 属于 3.6.2-F 定位工具，
不是 SDK 适配成果。

## 3. 视觉 / ESP（G 类）

| 文件 | 差异 | 处理 |
|---|---|---|
| `src/Features/Modules/Visual/ESP.cpp` | M `+452/-69`（4 个 ESP 探针提交 + revert） | **禁止移植** |
| `src/Utils/MiscUtils/MathUtils.cpp` | **零差异** | 天然保持基线 |
| `src/Utils/MiscUtils/RenderUtils.hpp` | **零差异** | 天然保持基线 |
| `FrameTransform` / `OWorldToScreen` / FOV / 投影矩阵实验 | 122 文件清单中无独立匹配；相关实验集中在 `ESP.cpp` 与 `OpenGLGui.cpp` | 随上述两文件一并**禁止移植** |

被拒的具体内容：同帧配对采样、八角点、灵敏度优选、搜索节流、
`projection-contract.json`、offset `0x358` writers 分析、view matrix 旋转重建等
全部属于投影矩阵实验，与本轮"恢复稳定基础运行"无关。

## 4. Aura / Combat（H 类）

| 文件 | 差异 | 处理 |
|---|---|---|
| `src/Features/Modules/Combat/Aura.cpp` | M `+59/-171`（主要是 `4141241` revert） | 保持 `b32d185` 原始实现 |
| `src/Features/Modules/Combat/Aura2.hpp` | D `-570`（revert 删除） | 保持 `b32d185` 原始实现（即保留该文件） |
| `AutoClicker.cpp` / `InventoryMove.cpp` / `ChestAura.cpp` / `Scaffold.cpp` | 各有少量差异（revert 连锁） | 保持 `b32d185` 原始实现 |

后续针对 Aura 的隔离（`73da588` 的 `SOLSTICE_DISABLE_AURA_DIAGNOSTIC`）、
Aura 渲染实验、执行路径重构（3.6.2-A `docs/batch3_6_2_a/` 6 份文档）
**全部不移植**。构建系统侧 `CMakeLists.txt` 新增的
`SOLSTICE_DISABLE_AURA_DIAGNOSTIC` 选项一并排除。

### Aura 已知遗留问题（单独记录，本轮不处理）

用户确认：`b32d185` 仅存在 **Aura 崩端问题**，且**接受暂时保留**以优先恢复
基础稳定性。本轮：
- 不修复、不禁用、不隔离 Aura；
- 不恢复任何 Aura 历史代码；
- 验收矩阵中 Aura 单列 `KNOWN_ISSUE`，不计为"新增崩溃"；
- 若 S0 复现出与历史描述不同的 Aura 行为，按 §十 先确认环境与基线，
  **不**顺手改 Aura。

## 5. 其他 SDK 层但本轮不移植者

| 文件 | 差异 | 判定 |
|---|---|---|
| `src/SDK/OffsetProvider.cpp/.hpp`、`SigManager.cpp/.hpp`、`ResolveState.hpp`、`CodeSection.*`、`SigFuzz.cpp` | 大改（状态机） | **DEFER**，见 `sdk-port-plan.md` §3.7；不擅自应用 |
| `src/Solstice.cpp` | M `+65/-29`（启动流程 / DEBUG 人工放行） | **禁止移植**：非纯 SDK，且会改变稳定基线启动行为 |
| `src/SDK/Minecraft/ClientInstance.cpp/.hpp` | M `+42/-79` / `+5/-0` | **禁止移植**：含 `9229523` 的 `getBlockSource` nullptr 行为（S3 阻塞） |
| `src/SDK/Minecraft/MinecraftGame.cpp` | M `+65/-0` | **禁止移植**：`isValidVirtualCall` 守卫 + `diagnosePlayUiRejection` 诊断（S4） |
| `src/Utils/SysUtils/ExceptionHandler.cpp/.hpp` | M `+79/-6` / `+4/-0` | **禁止移植**：纯异常上下文诊断，规格要求与 SDK 功能分开评估且不得混入 |
| `src/Utils/MemUtils.cpp/.hpp`、`Structs.hpp` | M（状态机配套） | **禁止移植** |
| `src/SDK/Minecraft/Actor/*`、`World/Level.cpp`、`Network/LoopbackPacketSender.hpp`、`Utils/GameUtils/*` | revert 连锁 / SDK 重写 | 保持 `b32d185` 原始实现 |
| `src/Hook/Hooks/ActorHooks/BaseTickHook.cpp` | M `-1` | 保持基线 |
| `src/Features/Modules/ModuleManager.cpp` | M `+7/-2` | 保持基线 |
| `ChinaHat.cpp` / `DestroyProgress.cpp` / `Keystrokes.cpp` | 少量差异 | 保持基线；不属于 SDK 移植白名单 |

## 6. 构建系统（J 类）

| 文件 | 处理 |
|---|---|
| `CMakeLists.txt` | **禁止移植**（含 `SOLSTICE_DISABLE_AURA_DIAGNOSTIC`、`SOLSTICE_RENDER_DIAG`、状态机相关选项） |
| `GenerateBuildInfo.cmake` | **禁止移植**（3.6.2-E.1 构建身份诊断） |

S0/S2 构建使用 `b32d185` 原始 CMake 配置，不新增任何构建开关。

## 7. 文档 / 审计资产（非运行时）

`docs/batch3_6/*`、`docs/batch3_6_2_[acdef]/*`、`docs/batch2/*`、`docs/batch3/*`、
`docs/mapping-audit/*`、`mapping-audit.json`、`mapping-audit-report.md`、
`tools/*` 共 81 个文件为历史报告与离线工具，**不参与二进制**，
本轮不移植、不删除、不作为"已恢复功能"计数。
`docs/batch3_7/` 是本轮新交付物。

## 8. 明确不自动移植的诊断清单（复核）

- ❌ Batch 3.6.2-C GL 阶段诊断
- ❌ Batch 3.6.2-F 订阅者诊断
- ❌ 额外的 GUI 异常隔离
- ❌ 任何未经验证的视觉模块禁用逻辑
- ❌ 3.6.2-D/E 的 playUi 拒绝诊断与 ExceptionHandler 故障上下文
- ❌ 3.6.2-A 的 Aura 隔离开关

「不要再次把一个稳定版本改成需要不断止血的版本」—— 以上全部排除即为此原则的落实。
