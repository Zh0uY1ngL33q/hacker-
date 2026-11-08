# 3.7 — 分阶段验收矩阵（工作组五）

## 0. 状态说明（重要）

**本轮所有验收格均为 `PENDING`**。本机为代码/分析机：无 MSVC / `ninja` / `g++`，
目标 EXE 与 `CMakeCache.txt` 不在本机，**未产生任何编译产物、DLL 或运行日志**。
编译、注入与逐阶段运行由构建机执行，结果按本文件回填。
静态分析结论见 `sdk-port-plan.md`，不把静态结论冒充运行验收。

## 1. 统一测试条件（每阶段必须完全一致）

| 条件 | 固定值 |
|---|---|
| 游戏版本 | **Minecraft 1.21.120.0**（唯一基线，禁止混用 1.21.90.1 等其他镜像） |
| 游戏 EXE | 用户路径 `E:\pf\3.9.20.300505\Minecraft.Windows.exe`（SHA-256 每阶段记录） |
| 游戏配置文件 | 同一份 `Configs\*.json` / `Preferences`（记录 `mDefaultConfigName`；它决定模块启用集） |
| 注入流程 | 同一注入器、同一时机、同一权限 |
| 诊断条件 | 相同；S0 不启任何新诊断开关（3.7 不加开关） |
| 场景 | 同一世界 / 同一出生点 / 同一操作序列（进世界 → 站立 → 打开 ClickGUI → 关闭 → 观察 HUD） |
| 测试时长 | 统一，建议 ≥10 分钟持续运行（覆盖"延迟闪退"） |
| 构建配置 | `b32d185` 原始 CMake 配置；S2 不改构建系统 |
| 构建产物 | 每阶段记录输出 DLL 的 SHA-256 |

> 条件变化会造成错误归因。若某阶段条件与上述不同，**该阶段结果作废重测**，
> 不得与其他阶段对比。

## 2. 验收矩阵（12 项 × 5 阶段）

| 项目 | S0 纯基线 | S1 映射 | S2 MinecraftSim | S3 BlockSource | S4 防御 |
|---|---|---|---|---|---|
| 编译 | PENDING | NOT_TESTED（阶段阻塞） | PENDING | NOT_TESTED（暂缓） | NOT_TESTED（不移植） |
| DEBUG 人工放行 | PENDING | NOT_TESTED | PENDING | NOT_TESTED | NOT_TESTED |
| 游戏进入世界 | PENDING | NOT_TESTED | PENDING | NOT_TESTED | NOT_TESTED |
| GL 画面更新 | PENDING | NOT_TESTED | PENDING | NOT_TESTED | NOT_TESTED |
| 音效更新 | PENDING | NOT_TESTED | PENDING | NOT_TESTED | NOT_TESTED |
| 游戏持续运行 | PENDING | NOT_TESTED | PENDING | NOT_TESTED | NOT_TESTED |
| Arraylist | PENDING | NOT_TESTED | PENDING | NOT_TESTED | NOT_TESTED |
| Watermark | PENDING | NOT_TESTED | PENDING | NOT_TESTED | NOT_TESTED |
| ClickGUI | PENDING | NOT_TESTED | PENDING | NOT_TESTED | NOT_TESTED |
| ESP | PENDING / NOT_TESTED | NOT_TESTED | PENDING / NOT_TESTED | NOT_TESTED（BlockESP 待 S3 解除） | NOT_TESTED |
| Aura | `KNOWN_ISSUE`（用户接受保留） | — | `KNOWN_ISSUE` | — | — |
| 新增崩溃 | PENDING (YES/NO) | NOT_TESTED | PENDING (YES/NO) | NOT_TESTED | NOT_TESTED |

**取值规范**：`PASS` / `FAIL` / `NOT_TESTED`（仅 ESP 允许）/ `KNOWN_ISSUE` / `PENDING`；
新增崩溃为 `YES` / `NO`。
Aura 的已知崩端**不计为"新增崩溃"**，但必须单独记录发生条件。

## 3. 每阶段独立记录模板

复制以下块，每个阶段填一份：

```text
### S<n> — <名称>
- 分支 HEAD:            <commit sha>
- 完整 Git Diff:        git show <commit>   （或见 docs/batch3_7/sdk-port-plan.md §4/§5）
- 编译:                 PASS / FAIL  （附构建日志路径）
- DLL SHA-256:          <sha256 或 PENDING>
- 游戏 EXE SHA-256:     <sha256 或 PENDING>
- 运行日志:             solstice.log / crash.log / sigreport.json / Hook.log（附路径）
- 测试时长 / 场景:      <与 §1 一致>
- 12 项验收:            <见 §2 回填>
- 新增崩溃:             YES / NO（若 YES，附异常码 / WER / dump 路径）
- Aura 遗留问题表现:    <描述，KNOWN_ISSUE>
- 结论:                 STAGE_STABLE / STAGE_REGRESSION / STAGE_BLOCKED
```

### S0 特别记录项

S0 是**纯 `b32d185` 基线**，无代码改动。除上述 12 项外必须确认：

- 原有 GL 渲染正常、能进入世界、**没有当前所说的全局卡死 / 延迟闪退**；
- Aura 原有遗留问题单独记录（不修）；
- `b32d185` 的 DEBUG 人工放行行为与历史一致；
- 若 **S0 本身无法复现稳定状态** → 按 §十 **停止 SDK 移植**，先确认环境和基线，
  不进入 S1/S2。

### S2 特别记录项（MinecraftSim 重试）

除 12 项外，按规格逐项验证：

| 子项 | 期望 | 结果 |
|---|---|---|
| 首次探测（首帧、世界未创建） | 返回 -1，不缓存为永久 | PENDING |
| 早期失败 | 2 s 退避窗口内不重扫 | PENDING |
| 延迟重试 | ≥2000 ms 后再次探测 | PENDING |
| 成功缓存 | 定位到 `>=0` 后永久返回，不再探测 | PENDING |
| 多线程并发 | `sProbeMutex` 串行，无竞态 / 无重复 dump | PENDING |
| Timer / Speed 基础行为 | 世界创建后功能恢复生效 | PENDING |
| 不在首个失败后永久缓存 -1 | 满足（源码已验证；运行期待确认） | PENDING |

## 4. 停止条件映射（§十）

| 触发 | 动作 | 影响范围 |
|---|---|---|
| S0 无法复现稳定状态 | **停止全部 SDK 移植**，先确认环境与基线 | 全部 |
| S1 引入卡死 | 只检查 S1 的新增差异（状态机 / 映射 hunks） | 仅 S1 |
| S2 引入卡死 | 回到 S1 稳定点，检查 `MinecraftSim.cpp` 修改 | 仅 S2 |
| S3 引入卡死 | 检查 `9229523` 与消费者空值保护 | 仅 S3 |
| 无法证明根因 | 停止修改，不扩大禁用范围 | 全部 |

注：本轮 S1/S3/S4 未应用，因此"回到 S1"在本轮等价于**回到 S0 基线 + S2 之前**；
若 S2 回归，只需在分支上撤销 S2 那一个提交，S0 基线不受影响。

## 5. 每阶段完整 Git Diff 索引

| 阶段 | Diff |
|---|---|
| S0 | 空（`git diff b32d185..b32d185`） |
| S1 | **未应用**。候选：`git show 517646e`、`git show 1d2c493`、`git show 1ec0571`；依赖根 `git show 4b93323` 等 |
| S2 | `git show <S2 提交> -- src/SDK/Minecraft/MinecraftSim.cpp`；全文亦内嵌于 `sdk-port-plan.md` §5 |
| S3 | **未应用**。候选：`git show 9229523` |
| S4 | **未应用**。候选：`git show 669ce2c`、`git show 2429e6e` |

全部 122 个文件的完整差异索引见 `file-change-inventory.json`（43 个运行时源码/构建
文件内嵌完整 unified diff，其余给出 `diffCommand`）。

## 6. 采集清单（构建机回传）

- 每阶段：构建日志、DLL SHA-256、游戏 EXE SHA-256、`build_info.h`（若有）、
  `CMakeCache.txt` 的 `SOLSTICE_*` 项。
- 每阶段：`solstice.log`、`crash.log`、`sigreport.json`、`Hook.log`。
- 若闪退：WER 事件、dump（`!analyze -hang` / `~* kb` / `!locks` / `!runaway 7`）。
- 保持进程存活再取现场；不同线程的故障（`playUi` guard 与 GL 渲染线程）不得合并归因。
