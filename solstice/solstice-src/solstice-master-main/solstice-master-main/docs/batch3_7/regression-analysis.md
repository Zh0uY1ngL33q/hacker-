# 3.7 — 回归分析与合并策略（工作组四 / 六）

## 1. 回归判定原则

- **一次只验证一个功能组**；每个阶段一个独立提交，可单独撤销。
- 若某 SDK 修改引入回归，**只撤销该批次**，不影响其他已验证批次。
- 归因时只比较该阶段相对其前一阶段的**新增差异**，不把历史差异混进来。
- 不得以"移植数量"为目标强行合并（§十）。
- `b32d185` 的稳定运行表现是第一判据；任何阶段若破坏它，该阶段判失败。

## 2. 静态回归风险预测（非运行期结论）

| 阶段 | 风险 | 依据 |
|---|---|---|
| S0 | **基准** | 用户确认的稳定基线；唯一已知问题是 Aura 崩端（`KNOWN_ISSUE`，本轮接受） |
| S1 | **CRITICAL（已阻塞，未应用）** | ① 依赖状态机，单独应用会语义退化；② Batch-1 的 `playUi` 修复会把 `index>=4096` 挡住的 inert 调用**激活为 slot 164 真实虚调用**，而 `b32d185` 无 `isValidVirtualCall` 防御 → 可能重现 3.6.2-D/E 的 playUi AV；③ 整套状态机会连带改写 `Solstice.cpp` 启动/DEBUG 放行与 `ExceptionHandler`，实质重建 post-b32d185 变更面 |
| S2 | **LOW（已应用，待验证）** | 单文件、零上下文漂移、无跨模块依赖；重试逻辑在 mutex 内串行，失败有 2 s 退避、成功永久缓存、日志 dump 一次性。主要运行期待确认点：世界创建前首帧失败 → 2 s 后重试是否真能定位到 Simulation 对 |
| S3 | **CRITICAL（已暂缓，未应用）** | `getBlockSource` 返回 nullptr 会把野 vtable 跳转变成 21 个文件 / 57 处调用点中的立即空解引用 |
| S4 | **LOW 但零收益（未应用）** | 基线 playUi 已 inert，`isValidVirtualCall` 无对象可保护；ExceptionHandler 改动纯诊断 |

**最重要的一条因果链**（解释为何不能"顺手把映射修好"）：

```
b32d185: MinecraftGame_playUi 扫描值 3238145
   → playUi() 的 index>=4096 守卫拒绝  → 不发声、不崩（稳定基线的一部分）
517646e: operand 3→10  → slot 164  → 守卫放行  → 真实虚调用
   + b32d185 没有 isValidVirtualCall  → 错误对象/错误版本时直接 AV
```

即：**在稳定基线上"修正"这个映射本身就是引入回归**。

## 3. 阶段撤销策略

| 触发 | 撤销动作 | 不影响 |
|---|---|---|
| S2 引入卡死/闪退 | `git revert <S2 提交>`（仅 `MinecraftSim.cpp`），回到 S0 基线 | 分析文档提交、S0 基线、`main` |
| S1 若将来启用并回归 | 撤销 S1 批次提交，只检查其新增差异（状态机 / 映射 hunks） | S0、S2 |
| S3 若将来启用并回归 | 撤销 S3 批次，检查 `9229523` 与消费者空值保护 | S0、S1、S2 |

分支 `recovery/b32d185-sdk-integration` 与 `main` 完全隔离；本轮**不合并 main、
不修改 main、不 force push**。任何撤销都在整合分支内进行。

## 4. 每阶段完整 Git Diff

| 阶段 | 状态 | Diff 位置 |
|---|---|---|
| S0 | 无改动 | `git diff b32d185..b32d185` = 空 |
| S1 | **未应用** | 候选 `git show 517646e` / `git show 1d2c493` / `git show 1ec0571`；依赖根 `git show 4b93323` / `84e8160` / `9b8f7d8` / `47284b7` |
| S2 | **已应用** | `git show <S2 提交>`；全文内嵌于 `sdk-port-plan.md` §5 |
| S3 | **未应用** | 候选 `git show 9229523` |
| S4 | **未应用** | 候选 `git show 669ce2c` / `git show 2429e6e` |
| 全量索引 | — | `file-change-inventory.json`（43 个源码/构建文件内嵌完整 unified diff） |

## 5. 构建结果 / 运行日志 / DLL SHA-256

| 项 | 本轮状态 |
|---|---|
| 编译结果 | **PENDING** —— 本机无 MSVC / `ninja` / `g++`，仅有 Python wheel 的 `cmake`；未产生编译产物 |
| 运行日志 | **PENDING** —— 未注入、未运行；`solstice.log` / `crash.log` / `sigreport.json` 为历史文件，不作为本轮证据 |
| DLL SHA-256 | **PENDING** —— 无本轮构建产物 |
| 游戏 EXE SHA-256 | **PENDING** —— `E:\pf\3.9.20.300505\Minecraft.Windows.exe` 在本机不可访问 |
| PE ImageBase / SizeOfImage | **PENDING** |
| CMakeCache / build_info.h | **PENDING** —— 本机无构建目录 |
| 12 项验收矩阵 | 全部 `PENDING` / `NOT_TESTED`（见 `staged-validation-matrix.md`） |

> 按项目工作流，构建与注入由构建机执行。本轮**不伪报编译成功或运行通过**；
> 所有静态结论与运行期证据在 `final-decision.md` 中明确分级。

## 6. 当前未解决的映射（在 b32d185 基线上）

**口径说明**：main 上的统计（`Offsets OK=26/57, MISSING=24, AMBIGUOUS=6,
IMPLAUSIBLE=1`；`Signatures OK=73/115, MISSING=20, AMBIGUOUS=21,
OUT_OF_RANGE=1`；合计 73 条不可用）来自**状态机**，而 `b32d185`
**没有状态机**，不产生 `OK/MISSING/AMBIGUOUS/IMPLAUSIBLE` 标签。
因此该统计**不能套用到本分支**。以下是 `b32d185` 上"保持原始声明、未修复"
的、被 Batch 1/2/3 明确点名的映射：

| 映射 | b32d185 状态 | 后续证据 | 本轮处理 |
|---|---|---|---|
| `MinecraftGame_playUi` | operand 3 → `3238145`（被 `index>=4096` 挡住，inert） | Batch-1 确认 operand 应为 10 → slot 164 | **不修**（会激活未防御虚调用，见 §2） |
| `MinecraftGame_mProfanityContext` | 旧签名取到立即数 | Batch-1 UNVERIFIED（无 1.21.120 命中锚点） | 保持 |
| `MinecraftGame_mMouseGrabbed` | 旧签名 IMPLAUSIBLE | Batch-1 UNVERIFIED（0x118 未语义验证） | 保持 |
| `Actor_swing` | 旧签名取到下一条指令字节 | Batch-1 UNVERIFIED | 保持 |
| `BlockSource_getChunk` | 旧签名命中 RIP-relative 全局加载 | Batch-1 UNVERIFIED | 保持 |
| `ClientInputMappingFactory_mKeyboardMouseSettings` | 旧签名无 1.21.44 锚点 | Batch-1 UNVERIFIED | 保持 |
| `ClientInstance_getBlockSource` | 短签名 4 个候选（318/35/35/69） | Batch-2 STATIC VERIFIED slot 318 | 保持（且与 S3 gate 耦合） |
| `GameSession_mEventCallback` | 6 个候选（0x08×4 / 0x20 / 0x50） | Batch-2 STATIC VERIFIED 0x50 | 保持 |
| `Actor_mDestroying` | 8 个候选，多数为指令字节 | Batch-2 STATIC VERIFIED | 保持 |
| `BlockSource_setBlock` | 短签名含 ref-count 假命中 | Batch-2 STATIC VERIFIED slot 34 | 保持 |
| `ClientInstance_setDisableInput` / `grabMouse` / `releaseMouse` / `Level_getHitResult` / `LevelData_mTick` | 歧义/错误操作数 | Batch-2 刻意保留 UNVERIFIED | 保持（不得伪装成 OK） |
| `BlockSource_mBuildHeight` | 旧签名（MISSING 等价） | Batch-3 明确 1.21.120 vtable getter → 0x38 | **暂缓**（S3 前置条件未满足） |

**判定**：`b32d185` 的映射整体仍是 `NOT TRUSTWORTHY`；本轮**不修改映射判据、
不修改任何映射数值、不修改 DEBUG 人工放行逻辑**。保留原有稳定行为。

## 7. 被暂缓的 SDK 修改及原因（汇总）

| 修改 | 阶段 | 原因 | 解除条件 |
|---|---|---|---|
| Batch-1 六个 Implausible | S1 | 状态机依赖 + playUi 激活风险 | 用户选 B（隔离状态机批次）且单独验收 |
| Batch-2 五个 ambiguous | S1 | 状态机依赖 + UNVERIFIED 语义丢失 + 与 S3 gate 耦合 | 同上 |
| Batch-3 `BlockSource_mBuildHeight` | S3 | 收益仅 BlockESP，需先完成消费者保护 | S3 解除后才允许构建验证 BlockESP |
| `9229523` getBlockSource gate | S3 | 57 处消费者未证明空值安全 | 全量消费者空值保护补丁（独立批次）或运行期不可达证明 |
| 整套映射状态机（`4b93323` 等） | S1 | 10+ 文件、牵连启动流程与 ExceptionHandler、改动映射判据 | **用户明确授权**并另立独立阶段 |
| `isValidVirtualCall` 守卫 | S4 | 基线无该函数且 playUi 已 inert，零收益 | 若将来移植 Batch-1 playUi，必须同步引入 |
| ExceptionHandler 上下文捕获 / `diagnosePlayUiRejection` | S4 | 纯诊断，规格禁止与 SDK 功能混组 | 未来单独评估，本轮不做 |

## 8. 合并 main 的条件（本轮不执行）

规格 §八：**只有 S1/S2/S3 全部稳定，才提出合并 main 的方案**。本轮：

- S1 = BLOCKED（未应用）；
- S2 = 代码已应用但**未经构建/运行验证**；
- S3 = DEFERRED（未应用）；
- S0 = 待构建机复现稳定状态。

→ **不满足合并条件**。本轮结束前不合并 main、不修改 main、不 force push，
只在 `recovery/b32d185-sdk-integration` 留下逐阶段提交与分析交付物。
待构建机回填 S0/S2 验收、并就 S1 依赖根给出选项后，再决定下一步。
