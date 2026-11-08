# SDK 成果保留清单

本文回答「哪些 SDK 成果被完整带进整合版本、依据是什么、哪些没有带」。所有条目按代码块（hunk）判定，不按文件名判定。[STATICALLY_CONFIRMED]

## 1. 差分源与范围

| 项 | 值 | 等级 |
| --- | --- | --- |
| SDK 差分起点 | `4141241`（树等价于 `f8c3541`） | `STATICALLY_CONFIRMED` |
| 状态机黄金快照 | `1ec0571`（`OffsetProvider.hpp` 逐字比对，忽略 CRLF） | `STATICALLY_CONFIRMED` |
| `4141241..1ec0571` 代码文件 | 14（含 `CMakeLists.txt`） | `STATICALLY_CONFIRMED` |
| 实际移植闭包 | 13（排除 `CMakeLists.txt`） | `STATICALLY_CONFIRMED` |
| 排除理由 | `CMakeLists.txt` 在该差分里携带 main 线专属诊断选项；本整合保持 b32d185 的 CMakeLists 逐字不变 | `STATICALLY_CONFIRMED` |
| 运行时区域污染 | `Hook` / `Features` / `include/nes` / `Client` 在该差分中均为 0 文件 | `STATICALLY_CONFIRMED` |

`OffsetProvider.hpp` 与 `1ec0571` 的对比结果：内容一致，仅工作树 CRLF 与 `git show` 的 LF 差异（`diff --strip-trailing-cr -q`）。[STATICALLY_CONFIRMED]

## 2. 已保留的 SDK 成果

### 2.1 映射可信度状态机

- `src/SDK/ResolveState.hpp`：header-only，六种状态 `Ok / Missing / Ambiguous / Inconsistent / Implausible / OutOfRange`，`ResolveEntry { status, address, value, matches }`，`ResolveTable` 提供 `set / get / allOk / count / namesWith / size / clear`。[STATICALLY_CONFIRMED]
- `OffsetProvider` / `SigManager` 各自持有权威 `mStatus`，通过 `statusTable()` 暴露；初始化结论来自 `mStatus.allOk()`，不再用「指针 != 0」当作解析成功。[STATICALLY_CONFIRMED]
- `rejectOffset()`、索引 8 字节对齐、`plausibleOffset()` / `usableOffset()`、`hardcodedFields()` 登记机制完整保留；字面量 `DEFINE_FIELD` 常量一律标 `UNVERIFIED`，不参与乐观统计。[STATICALLY_CONFIRMED]
- `SigFuzz::classify()` 直接消费两个 `statusTable()`，`sigreport.json` 输出与状态机一致。[STATICALLY_CONFIRMED]
- 依赖闭包 13 文件的项目内 include 全部可解析，无缺失符号、无重复定义、无失效 include。[STATICALLY_CONFIRMED]
- `ResolveState.hpp` 是 header-only，不进入 CMake `src/*.cpp` unity 收集，无需新增 `add_library` 源项。[STATICALLY_CONFIRMED]

### 2.2 Batch 1～3 证据映射

| 映射 | 来源提交 | 当前定义 | 等级 |
| --- | --- | --- | --- |
| `ClientInstance_getBlockSource` | Batch 1 `517646e` | `DEFINE_INDEX_FIELD(..., "... A8 01 00 00 ...", 22, OffsetType::Index)` | `STATICALLY_CONFIRMED` |
| `MinecraftGame_playUi` | Batch 1 `517646e` | `DEFINE_INDEX_FIELD(..., "48 8B 49 08 ...", 10, OffsetType::Index)` → slot 164 | `STATICALLY_CONFIRMED` |
| `BlockSource_mBuildHeight` | Batch 3 `1ec0571` | `DEFINE_INDEX_FIELD_TYPED(uint8_t, ..., "0F B7 41 ? C3 ...", 3, OffsetType::FieldOffset)` | `STATICALLY_CONFIRMED` |
| 其余 Batch 1/2 的 Implausible 退役项与 Ambiguous 收敛项 | `517646e` / `1d2c493` | 随 `OffsetProvider.hpp` 黄金快照整体带入 | `STATICALLY_CONFIRMED` |

静态证据链 `0x1CBC3E4 → disp32 0x520 → /8 → slot 164` 保留为**映射事实**；它不构成「调用安全」结论。[STATICALLY_CONFIRMED]

### 2.3 MinecraftSim 探针重试

`411fa4e` 可独立移植（不依赖状态机）：`sGameSimOffset` / `sRenderSimOffset` 初值 `-2`；成功 `cached >= 0` 永久缓存；首次失败不再永久缓存 `-1`，仅在 `kProbeRetryMs = 2000` 退避窗口内视为最终态；`sLastProbeTick` 用 `GetTickCount64()`；`sLayoutDumped` 在 mutex 内一次性 dump，避免每 2 秒重复日志。[STATICALLY_CONFIRMED]

### 2.4 必要的 SDK 兼容防御

- `MemUtils::isReadableRange` / `isInHostModule` / `isPatchableCode` / `isValidVirtualCall` / `readMemberChecked` 全部带入；`callVirtualFunc` 保留零成本 index guard，未在每 tick 虚调用路径插入 `VirtualQuery`。[STATICALLY_CONFIRMED]
- `ExceptionHandler::shutdown()` 在 SDK 早退路径恢复旧 `SetUnhandledExceptionFilter` 回调，避免 DLL 卸载后残留指向已释放内存的过滤函数。[STATICALLY_CONFIRMED]
- Commit 5 移植 3.6.2-D 的故障上下文捕获和 3.6.2-E.1 P1-2 的空指针保护，并修正上游把 `nullptr` 传给 `StackWalker::ShowCallstack()` 的缺陷（详见 `compatibility-risks.md` R-07）。[STATICALLY_CONFIRMED]

## 3. 启动门控语义（必须保留的行为）

b32d185 的原始人工门控被逐字恢复，而不是上游状态机的 `#ifdef SOLSTICE_ALLOW_UNVERIFIED_MAPPINGS`（默认 OFF、不给操作员选择）：[STATICALLY_CONFIRMED]

```cpp
console->critical("Solstice should not be used in this state.");
console->info("Type 'DEBUG' to continue, or press ENTER to exit.");
std::getline(std::cin, input);
if (input != "DEBUG") { /* deinit + FreeLibraryAndExitThread */ }
else { /* log OPERATOR OVERRIDE */ }
```

- `__DEBUG__` 只决定是否**提供**该选择，不是自动放行。[STATICALLY_CONFIRMED]
- 人工放行不改变任何 `ResolveStatus`，并在日志里明确记为 override；扫描结果不变。[STATICALLY_CONFIRMED]
- 启动汇总从 `sigTable` / `offTable` 的 non-Ok 计数得出，硬编码常量单独以 `UNVERIFIED` 报告，不并入「全部解析成功」。[STATICALLY_CONFIRMED]

## 4. 明确未带入的 SDK 相关变更

| 变更 | 判定 | 理由 | 等级 |
| --- | --- | --- | --- |
| `9229523` `getBlockSource()` nullptr gate | 暂缓 | 约 55～57 个调用点 / 21 文件，多数是 `getBlockSource()->getBlock(...)` 直接链式 deref；提前引入 nullptr 会新增崩溃面 | `DEFERRED` |
| `669ce2c` playUi 直接 `isValidVirtualCall` 后即调用 | 替换为更严格闸门 | 本整合默认拒绝调用，只在未定义的 `SOLSTICE_PLAYUI_SEMANTIC_VERIFIED` 下才走校验+调用 | `STATICALLY_CONFIRMED` |
| `2429e6e` `diagnosePlayUiRejection()` | 不移植 | 默认拒绝路径在编译期即返回，没有运行期 rejection 可诊断；且该函数所属 E.1 ABI 论证链后被 E.1.2.1 修订 | `DEFERRED` |
| `2429e6e` `WORKTREE_DIRTY` 与诊断选项 0/1 记录 | 不移植 | b32d185 的 `GenerateBuildInfo.cmake` 根本没有这些选项与宏，移植会产生指向不存在选项的伪溯源字段 | `DEFERRED` |
| `2429e6e` / `b879cb1` 的 `add_dependencies(Solstice ...)` | 无需移植 | `CMakeLists.txt:4 project(Solstice)`，`:285 add_dependencies(${PROJECT_NAME} generate_build_info SolsticeShaders)` 已是同一条构建图边；b879cb1 只是把 `${PROJECT_NAME}` 写成字面量 | `STATICALLY_CONFIRMED` |
| `67f82f0` / `794fc65` / `b879cb1` | 纯文档修订 | 无 C++ 代码可移植；ABI 最终裁定 `this=RCX / soundName=RDX / volume=XMM2 / pitch=XMM3` 已属文档层结论，本整合不引用其作为调用许可 | `STATICALLY_CONFIRMED` |

## 5. 结论

SDK 侧的融合在**代码层完整、在证据层自洽**：状态机闭包、Batch1/2/3 映射、MinecraftSim 重试、必要防御全部带入；所有「不可信」结果都不会被当有效值使用；所有调用安全缺口仍以 `UNVERIFIED` / `DEFERRED` 显式标注，没有用静态证据冒充运行期验证。[STATICALLY_CONFIRMED]
