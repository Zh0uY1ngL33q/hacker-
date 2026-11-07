# Solstice 1.21.120 Batch 3 - 核心对象链恢复与 Missing Offset 修复

## 1. 范围与证据边界

- 基线：`0ece30280170a3794a1cae860b769a659629addf`（`main`）。Batch 1 (`517646e`) 与 Batch 2 (`61ec623` / `1d2c493` / `0ece302`) 均为其祖先，**没有被回退**。
- 目标：Minecraft `1.21.120.0`, Build `3.9.20.300505`, `Minecraft.Windows.exe`, Windows x64。
- 目标镜像：`G:/hacker/Dump/Minecraft.Windows_1.21.120.0_20261027_005946.image`, 226,144,256 B, SHA-256 `11147749...fbd2ff9b70`, ImageBase `0x7FF714E80000`, SizeOfImage `0xD7AB000`。
- 参考镜像：`G:/hacker/_cmp/Minecraft.Windows_1.21.44.image`（**仅作结构参考**；没有复制其 RVA、偏移或虚函数索引）。该镜像无 RTTI 类型描述符，因此参考证据来自 `docs/batch3-reference-candidates.json` 的调用点候选。
- 本文全部为 **STATIC 证据**。`OK` 只表示通过扫描与解析判据，不等于语义验证或运行时验证。
- 本轮未修改任何功能模块算法、Hook 执行逻辑、网络发送逻辑、渲染控制流、CMake 构建策略。

## 2. 版本确认（本轮第一道门槛）

| 检查项 | 结论 |
|---|---|
| 当前源码 HEAD | `0ece302`，与任务书基线一致 |
| 祖先关系 | `517646e`(B1)、`47284b7`(审计)、`9b8f7d8` 均为 HEAD 祖先 |
| 运行时数据 | `solstice.log` / `sigreport.json` 均为 2026-11-07 00:25，Build 行 `0ece302... source 0b48bfd17467 uncommitted 0`，**与当前基线同源** |
| 运行时状态 | offset `ok=25/57 missing=25 ambiguous=6 implausible=1`；signature `ok=73/115 missing=20 ambiguous=21 out-of-range=1`。与任务书数字完全一致 |
| `crash.log` | `0xFF01 Failed to find signatures/offsets! (FeatureManager not initialized)` —— **默认拒绝机制按预期工作，不是崩溃**。因此本轮运行只产生映射报告，没有验证任何对象形状 |
| 默认拒绝 | `SOLSTICE_ALLOW_UNVERIFIED_MAPPINGS=OFF` 保持不变；本轮未开启人工放行 |

## 3. 目标版本 RTTI vtable 基线（本轮新增能力）

从 1.21.120 镜像的 RTTI type descriptor / COL 直接解析，槽内存的是 **VA**，需减 ImageBase 才是文件偏移（RVA）。

| 类 | RTTI | vtable RVA | 槽数 | 短 getter 数 |
|---|---|---|---:|---:|
| ClientInstance | `.?AVClientInstance@@` | `0x8DDB590` | 400 | 42 |
| Level | `.?AVLevel@@` | `0x921FA60` | 400 | 42 |
| BlockSource | `.?AVBlockSource@@` | `0x921B490` | 66 | 6 |
| Actor | `.?AVActor@@` | `0x9170898` | 155 | 3 |
| MinecraftSim | **无类型描述符** | — | — | — |

`MinecraftSim` 没有 RTTI/vtable：它是**非多态纯数据对象**，因此不能通过 vtable 槽定位，构造函数/成员读写才是正确路径。

**校验**：ClientInstance 槽 308 = `mov rax,[rcx+0x108]; ret`（`getPacketSender`），槽 318 = `mov rax,[rcx+0x118]; ret`（`getBlockSource`）—— 与 Batch 2 已运行时验证的五条映射完全吻合，证明 vtable 解析正确。

## 4. 已确认修复：`BlockSource_mBuildHeight = 0x38`

- 原始状态 MISSING；原始签名 `0F BF ? ? 3B C8 0F 8D ? ? ? ? 8B 0F` 在 1.21.120 里 **0 命中**。
- 目标版本证据：BlockSource vtable 槽 38 = `movzx eax, word ptr [rcx+0x38]; ret`，槽 37 = `movzx eax, word ptr [rcx+0x3a]; ret`。SDK `BlockSource.hpp` 把 `getBuildHeight()` / `getBuildDepth()` 声明为 `mBuildHeight` 与 `mBuildHeight+2` 两个相邻 `int16_t`。该类的 `CLASS_FIELD`（`mChunkSource 0x28`, `mDimension 0x30`）也被 vtable 槽 54 (`[rcx+0x28]`) 与槽 39-41 (`[rcx+0x30]`) 独立证实。
- 新签名把 **disp8 留成通配符 `?`**，锚定该 getter 及其后 BlockSource 特有的 getter 序列；在 1.21.120 可执行段中**唯一命中 `0x5D3B130`**，解析出 `0x38`。值由目标二进制读出，不是硬编码，也没有把答案写进 pattern。
- 1.21.44 参照镜像在 `0x337BA7D` 出现同形态的 `movsx eax, word ptr [rbx+0x3a]`；仅作为结构佐证。
- 新状态 **OK / 0x38**；语义结论 **STATIC VERIFIED**；运行时状态 **PENDING**。

## 5. 11 条映射的证据与处置

完整机器可读记录（每条含 Name / Owner / Original Status / Original Signature / Candidate RVAs / Instruction / Xrefs / Data Flow / Type / Evidence / Consumer / New Value / New Status / Runtime Status）见 [`batch3-mapping.json`](batch3-mapping.json)。逐候选反汇编为本机参考素材 `docs/batch3-candidates.json`（120，全部 0 命中）与 `docs/batch3-reference-candidates.json`（44，各 1 候选）；按"不提交临时逆向文件"要求未随仓库交付，其候选 RVA 与提取值已整合进 `batch3-mapping.json`。

| 映射 | 44 参照值 | 120 命中 | 实际消费者 | 处置 |
|---|---:|---:|---|---|
| ClientInstance_getInputHandler | 331 | 0 | `ClientInstance.cpp:136` | UNVERIFIED |
| ClientInstance_getMouseGrabbed | 162 | 0 | **无** | 建议废弃（死映射） |
| MinecraftSim_mGameSim | 224 | 0 | `MinecraftSim.cpp:137` | UNVERIFIED（有探测替代） |
| MinecraftSim_mRenderSim | 232 | 0 | `MinecraftSim.cpp:149` | UNVERIFIED（有探测替代） |
| Level_getLevelData | 160 | 0 | `Level.cpp:182` | UNVERIFIED |
| Level_getPlayerList | 302 | 0 | `Level.cpp:23` | UNVERIFIED |
| Actor_mHurtTimeComponent | 1144 | 0 | **无** | 建议废弃（已 entt 化） |
| Actor_mContainerManagerModel | 1552 | 0 | `Actor.cpp:263` | UNVERIFIED（有 fallback） |
| BlockSource_getChunk | 39 | 0 | `BlockSource.cpp:19` | UNVERIFIED |
| BlockSource_clip | 45 | 0 | `BlockSource.cpp:42` | UNVERIFIED |
| BlockSource_mBuildHeight | 56 | 0 -> 1 | `BlockSource.hpp:22,26` | **OK / 0x38** |

停止理由逐条见 [`batch3-unresolved.md`](batch3-unresolved.md)。

## 6. 修复前后状态变化

| 项 | 修复前 | 修复后（离线重算） |
|---|---|---|
| 扫描型 Offset OK | 25 | 26 |
| Offset MISSING | 25 | 24 |
| 扫描映射总数 | 57 | 57（不变，仍为扫描型签名） |
| 通过扫描判据合计 | 98 | 99 |

`BlockSource_mBuildHeight` 仍由签名解析，没有改成硬编码常量，因此 `total=57` 不变、hardcoded 审计清单不变。

## 7. 新增或修改的偏移

仅一处：`src/SDK/OffsetProvider.hpp` 的 `BlockSource_mBuildHeight` 签名与 `disp_off=3`（保持 `uint8_t` 读取宽度、`FieldOffset` 类型）。无其他偏移、无功能模块、无 Hook、无 CMake 改动。

## 8. 实际消费者

见 [`batch3-consumers.json`](batch3-consumers.json)。追踪了 `OffsetProvider::`、`SigManager::`、`listen<Event>`、`hat::member_at`、`callVirtualFunc`、`CLASS_FIELD`，以及**间接 SDK getter 调用**（`getLocalPlayer` 234 次、`getBlockSource` 45、`getMouseGrabbed` 14、`getBuildHeight/Depth` 14、`getMobHurtTimeComponent` 6、`getPlayerList` 5、`getGameSim` 2、`getContainerManagerModel` 1、`getChunk` 1）。

## 9. 模块依赖关系与 READY 清单

见 [`batch3-module-readiness.md`](batch3-module-readiness.md)。共 131 个模块：READY 29 / NEEDS_RUNTIME_TEST 83 / BLOCKED 19；默认启用状态单独记录（`true` 8 / `false` 118 / unknown 5）。

## 10. 测试结果

- Python 语法检查、离线目标映射局部扫描：新签名在 `verify_sigs.py` 中解析出 **1 命中 @ 0x5D3B130，值 56 (0x38)**，`cpp_plausible_offset` 通过；定义数 172、hardcoded 18 均不变。
- `unittest test_verify_sigs`：**37/37 通过**（本轮未改动扫描器逻辑）。
- C++ 括号、宏续行、`git diff --check`：**复核通过**。
- **本机没有 MSVC，未做正式编译**；Release 编译交给指定构建机。
- 完整 226MB 全量离线扫描未重复执行（本轮只做目标映射局部扫描）。

## 11. 构建机编译需求与下一批建议

- 构建机：`git pull` -> Release 编译 -> 注入，保持 `SOLSTICE_ALLOW_UNVERIFIED_MAPPINGS=OFF`。
- 第一次运行只核对 `sigreport.json`：`BlockSource_mBuildHeight` 应进入 `ok`（值 56），offset `ok` 25 -> 26，无新增 ambiguous / implausible / out-of-range，Batch 2 五条不回归。
- **不得**因默认拒绝运行就判定对象语义已验证；如需对象形状验证，须先提交严格隔离的白名单方案并获人工批准。
- 下一批建议：优先用本轮建立的 vtable getter 表 + 构造函数数据流，独立定位 `Level_getPlayerList` / `Level_getLevelData` 与 `ClientInstance_getInputHandler`；`BlockSource_getChunk` / `clip` 需要重新找目标版本调用点而不是沿用 44 槽号。

## 12. 交付文件

`batch3-report.md`（本文）、`batch3-mapping.json`、`batch3-consumers.json`、`batch3-module-readiness.md`、`batch3-unresolved.md`。每组修改独立提交，完整 Git Diff 见对应 commit。
