# Batch 3.7 — Final Decision

## 1. 最终状态

```text
SDK_BATCH_INTEGRATION_READY
```

**限定含义**：整合分支、完整差异清单、SDK 白名单、禁止移植清单、逐阶段验收矩阵
与**第一个独立 SDK 批次（S2）的代码**均已就绪，可供构建机开始 S0/S2 逐阶段验证。

**不等于**：
- 不等于 `STABLE_BASELINE_RESTORED` —— S0 尚未由构建机复现稳定状态；
- 不等于 `SDK_BATCH_REGRESSION_IDENTIFIED` —— 本轮无运行期回归证据；
- 不等于任何功能已通过构建或运行验收。

状态迁移规则：
- S0 复现失败 → 停止移植，先确认环境与基线（§十）；
- S2 运行期回归 → 转 `SDK_BATCH_REGRESSION_IDENTIFIED`，只撤销 S2；
- S0 + S2 全部通过、且 S1/S3 决策落定后 → 才可能收口 `STABLE_BASELINE_RESTORED`。

## 2. 硬性 Git 要求执行情况

| 要求 | 执行结果 |
|---|---|
| `git status -sb` / `rev-parse HEAD` / `rev-parse origin/main` / `cat-file -e b32d185^{commit}` | 已全部执行。基线前：分支 `feat/gl-render-hang-root-cause`，HEAD = `origin/main` = `e875543`，`b32d185` 确认存在 |
| 独立分支 `recovery/b32d185-sdk-integration` | **已创建并切换**，`git switch -c ... b32d185` |
| 分支起点必须是 `b32d185` | **是**，HEAD 初始 = `b32d1856702ef8f1d3e4f92bb17b5a286736932f` |
| 不得从 `e875543` 修改后整体回退 | 未采用；直接以 `b32d185` 建分支，工作树天然为基线实现 |
| 不得修改 main | `main` / `origin/main` 仍为 `e875543`，未动 |
| 不得 force push | 未执行任何 force 操作 |
| 不得删除已有日志 / ZIP / Dump / 未跟踪文件 | 全部保留（`git status --short` 抽查确认） |
| 本轮结束前不合并 main | 未合并、未推送 main |

> 本轮按当轮明确指示使用独立分支且不碰 main，覆盖常规「`git branch -f main HEAD`
> + push main」工作流。

## 3. 三个决定性发现

### 3.1 映射修复不能独立移植（S1 阻塞）

`4b93323 / 84e8160 / 9b8f7d8 / 47284b7` 引入的映射信任状态机
（`ResolveState.hpp`、`ResolveStatus`、`rejectOffset()`、全局 `usableOffset()`）
**全部是 `517646e` 的祖先**；`517646e^:OffsetProvider.hpp` 已含这些符号，
而 `b32d185` **全都没有**（其 `DEFINE_INDEX_FIELD` 把扫描原始值直接写入 static int）。

→ 单独打 Batch-1/2/3 的签名 hunks 要么上下文不匹配无法应用，要么语义退化：
「UNVERIFIED 保持 rejected」变成「垃圾值被当作有效 offset 直接使用」，
即规格禁止的「把 UNVERIFIED 伪装成 OK」。整套移植状态机则会牵连
`src/Solstice.cpp`（启动 / DEBUG 人工放行）与 `ExceptionHandler.cpp`，
实质重建 post-b32d185 变更面，与稳定优先冲突 → **不擅自应用，留给用户决策**。

### 3.2 在稳定基线上"修正 playUi 映射"本身就是引入回归

```
b32d185: MinecraftGame_playUi = 3238145 → index>=4096 守卫拒绝 → inert（不崩）
517646e: operand 3→10 → slot 164 → 守卫放行 → 真实虚调用
         + b32d185 无 isValidVirtualCall → 错误对象时直接 AV
```

Batch-1 那个"唯一 confirmed fix"有充分 1.21.120 二进制证据，但在 `b32d185` 上
会**激活一条被有意挡住的调用路径**。规格明确「slot 164 不得盲目修改」→ **禁止移植**。

### 3.3 S3 的阻塞不在证据，在消费者

`b32d185` 实测 **57 处** `getBlockSource()` 调用、分布在 **21 个文件**
（历史统计 55 / 49）。宽松词法启发式下 28 处有检查证据、**29 处无**，
且该启发式是上界 —— `AntiImmobile.cpp:63`、`TestModule.cpp:132`、
`AutoKick.cpp:77` 等是 `getBlockSource()->getBlock(...)` 直接链式解引用。
返回 nullptr 会把一次野 vtable 跳转变成跨 21 文件的立即空解引用 → **S3 暂缓**。

## 4. 阶段决策表

| 阶段 | 内容 | 判定 | 代码动作 |
|---|---|---|---|
| S0 | 纯 `b32d185` | 分支起点已建立，验收待构建机 | 无 |
| S1 | Batch 1/2/3 映射 | **BLOCKED**（依赖状态机 + playUi 激活风险） | 未应用 |
| S2 | MinecraftSim 重试 `411fa4e` | **PORT** —— 唯一独立、零依赖、规格明确授权 | **已应用**（+35/-4） |
| S3 | `9229523` getBlockSource gate | **DEFERRED**（57 消费者未证明空值安全） | 未应用 |
| S4 | `isValidVirtualCall` / ExceptionHandler 诊断 | 分开评估后均不移植（守卫零收益、诊断越界） | 未应用 |

11 个候选：1 PORT / 4 DEFER / 4 EXCLUDE / 1 REFERENCE_ONLY / 1 待用户决策。
**不追求移植数量。**

`411fa4e` 可独立移植的实证：
`git diff --quiet b32d185 411fa4e^ -- src/SDK/Minecraft/MinecraftSim.cpp` → 完全一致；
应用后工作树文件与 `411fa4e` 版本逐字相同；`<Windows.h>` / `<atomic>` / `<mutex>`
在基线文件中已包含，`GetTickCount64()` 可用（静态检查）。
并发复核：mutex 内串行、2 s 退避、失败不再永久缓存 -1、成功永久缓存、
layout dump 一次性。

## 5. 提交结构（每个提交只含一个功能组）

| # | 功能组 | 内容 |
|---|---|---|
| 1 | S0 分析与交付物 | `docs/batch3_7/`（8 份）+ `tools/batch3_7_classify.py`；无运行时代码 |
| 2 | S2 SDK 移植 | 仅 `src/SDK/Minecraft/MinecraftSim.cpp`（`411fa4e` 的精确 hunk） |

复现完整 Diff：
```bat
git log --oneline b32d185..HEAD
git show <提交1> -- docs/batch3_7/ tools/batch3_7_classify.py
git show <提交2> -- src/SDK/Minecraft/MinecraftSim.cpp
```
S2 全文 diff 亦内嵌于 `sdk-port-plan.md` §5；全量 122 文件差异索引见
`file-change-inventory.json`（43 个源码/构建文件内嵌完整 unified diff）。

## 6. 构建 / 运行 / DLL SHA-256（如实标注）

| 项 | 状态 |
|---|---|
| 编译结果 | **未产生编译产物** —— 本机无 MSVC / `ninja` / `g++`，仅有 Python wheel 的 `cmake` |
| 运行日志 | **无本轮运行** —— 历史 `solstice.log` / `crash.log` / `sigreport.json` 不作本轮证据 |
| DLL SHA-256 | **PENDING** |
| 游戏 EXE SHA-256 / PE ImageBase / SizeOfImage | **PENDING** —— `E:\pf\3.9.20.300505\Minecraft.Windows.exe` 本机不可访问 |
| CMakeCache / build_info.h | **PENDING** —— 本机无构建目录 |
| 12 项验收矩阵 | 全部 `PENDING` / `NOT_TESTED`（`staged-validation-matrix.md`） |

按项目工作流，构建与注入由构建机执行。**本轮不伪报编译成功或运行通过。**

## 7. 未解决映射 / 暂缓 SDK 修改

- `b32d185` 无映射状态机，**不产生** `OK/MISSING/AMBIGUOUS/IMPLAUSIBLE` 标签；
  main 上的 `73 条不可用映射` 统计**不能套用**到本分支。
- 保持原始声明的 12+ 个映射（含 `MinecraftGame_playUi`、`BlockSource_mBuildHeight`、
  `ClientInstance_getBlockSource` 等）逐一列于 `regression-analysis.md` §6。
- 映射整体仍 `NOT TRUSTWORTHY`；本轮**不修改映射数值、不修改映射判据、
  不修改 DEBUG 人工放行逻辑**。
- 暂缓项与解除条件见 `regression-analysis.md` §7。

## 8. 交付物（8/8）

```
docs/batch3_7/
    commit-classification.json      31 个提交、A-J 分类、混合提交标记
    file-change-inventory.json      122 个文件、来源提交、完整 diff / diffCommand
    sdk-port-plan.md                依赖链、逐候选评估、S2 完整 diff
    sdk-port-manifest.json          11 个候选的结构化判定 / 证据 / 依赖 / 风险
    excluded-changes.md             禁止移植清单（41 个运行时源码仅 1 个移植）
    staged-validation-matrix.md     12 项 × 5 阶段、统一测试条件、采集清单
    regression-analysis.md          回归预测、撤销策略、未解决映射、合并条件
    final-decision.md               本文件
```
附 `tools/batch3_7_classify.py`（静态分类生成器，不参与编译）。

## 9. 证据等级

| 类别 | 可否作验收依据 |
|---|---|
| git 提交关系 / 文件内容 / 符号依赖 / 调用点统计 | **静态证据**，可作移植决策依据，**不可**作编译或运行验收依据 |
| `411fa4e` 与基线的逐字一致性、include 完整性 | 静态证据（源码级复核），仍需真实编译确认 |
| S0/S2 的 12 项验收、DLL SHA、运行日志 | **运行期证据，本轮全部 PENDING** |

## 10. 停止条件与下一步

| 触发（§十） | 动作 |
|---|---|
| S0 无法复现稳定状态 | **停止全部 SDK 移植**，先确认环境与基线 |
| S2 引入卡死 | 撤销 S2 单个提交，只检查 `MinecraftSim.cpp` 新增差异 |
| S1/S3 若将来启用并回归 | 只撤销该批次，检查对应新增差异 |
| 无法证明根因 | 停止修改，不扩大禁用范围 |

**要跑的命令**（构建机，使用 `b32d185` 原始 CMake 配置，不新增开关）：
```bat
git switch recovery/b32d185-sdk-integration
git log --oneline b32d185..HEAD
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```
先以 S0（撤销 S2 或 `git checkout b32d185^` 之外的方式：临时以 `b32d185` 树构建）
确认稳定，再以当前 HEAD（含 S2）构建并对照。

**回传什么**：每阶段的构建日志、DLL SHA-256、游戏 EXE SHA-256、
`CMakeCache.txt` 的 `SOLSTICE_*`、`solstice.log` / `crash.log` / `sigreport.json` /
`Hook.log`，以及 12 项验收结果、测试时长与场景（须与
`staged-validation-matrix.md` §1 完全一致）；S2 另附 7 个重试子项结果。

**自查口径**：
1. 各阶段测试条件必须一致，否则结果作废重测，不得跨阶段对比；
2. Aura 崩端是 `KNOWN_ISSUE`，**不计为新增崩溃**；
3. 只有运行期验收通过才能谈 `STABLE_BASELINE_RESTORED`；
4. S1 依赖根（整套状态机）需**用户明确授权**才会启用，本轮绝不擅自应用；
5. 不同线程的故障（playUi guard 与 GL 渲染）不得合并归因。

**待用户决策**：S1 依赖根选 A（继续暂缓，只交付 S2，推荐）还是
B（另立隔离批次单独移植整套状态机并验收）。在 S0/S2 验收回填前，
不提出任何合并 main 的方案。
