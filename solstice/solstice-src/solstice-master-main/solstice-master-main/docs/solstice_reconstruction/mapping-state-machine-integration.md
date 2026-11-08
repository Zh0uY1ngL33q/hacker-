# 映射可信度状态机 — 融合实现说明

## 1. 为什么必须整体移植而不是 cherry-pick

Batch 3.7 的结论是：Batch1/2/3 的映射修复**不能独立 cherry-pick**。它们写入的 `DEFINE_INDEX_FIELD` / `DEFINE_INDEX_FIELD_TYPED` 宏、索引对齐规则、拒绝逻辑和 `statusTable()` 聚合，全部来自 `4b93323 → 84e8160 → 9b8f7d8 → 47284b7` 的状态机链。只搬映射行而不搬状态机，等于把「已退役的 Implausible 值」重新变成可用的裸偏移。[STATICALLY_CONFIRMED]

因此 Commit 2 一次性搬入 13 文件依赖闭包，Commit 3 才在其上应用 Batch1/2/3 的映射 diff。[STATICALLY_CONFIRMED]

## 2. 依赖闭包（13 文件）

| 文件 | 角色 | 等级 |
| --- | --- | --- |
| `src/SDK/ResolveState.hpp` | 状态枚举、`ResolveEntry`、`ResolveTable`（header-only） | `STATICALLY_CONFIRMED` |
| `src/SDK/OffsetProvider.hpp` | `statusTable()` / `hardcodedFields()` / `rejectOffset()` / `plausibleOffset()` / 宏定义 | `STATICALLY_CONFIRMED` |
| `src/SDK/OffsetProvider.cpp` | 扫描→分类→写表→`allOk()` 结论 | `STATICALLY_CONFIRMED` |
| `src/SDK/SigManager.hpp` / `.cpp` | 签名侧同构状态机 | `STATICALLY_CONFIRMED` |
| `src/SDK/CodeSection.hpp` / `.cpp` | PE 区段边界与范围校验 | `STATICALLY_CONFIRMED` |
| `src/SDK/SigFuzz.cpp` | `classify(statusTable())` 生成 `sigreport.json` | `STATICALLY_CONFIRMED` |
| `src/Utils/MemUtils.hpp` / `.cpp` | `isReadableRange` / `isInHostModule` / `isPatchableCode` / `isValidVirtualCall` / `readMemberChecked` | `STATICALLY_CONFIRMED` |
| `src/Utils/SysUtils/ExceptionHandler.hpp` / `.cpp` | `shutdown()` 供 SDK 早退前恢复异常过滤器 | `STATICALLY_CONFIRMED` |
| `src/Solstice.cpp` | 启动汇总、DEBUG 门控、早退清理 | `STATICALLY_CONFIRMED` |

完整性检查：闭包内所有项目内 include 均解析到现存文件；`ResolveStatus` 的 6 个状态、`statusTable()` 的两个权威入口、`allOk()` 的两处使用、启动侧的两处 `statusTable()` 消费全部存在。[STATICALLY_CONFIRMED]

## 3. 状态语义

```cpp
enum class ResolveStatus {
    Ok,           // 唯一可当有效值使用的状态
    Missing,      // 未命中
    Ambiguous,    // 多命中
    Inconsistent, // 多次扫描结果不一致
    Implausible,  // 形状/对齐/范围不合理
    OutOfRange,   // 越出 PE 区段
};
```

- `ResolveEntry` 同时保留 `status / address / value / matches`，诊断能看到「为什么被拒」，而不仅是一个 0。[STATICALLY_CONFIRMED]
- `allOk()` 要求所有登记项均为 `Ok`；任一 non-Ok 都让 `initialize()` 返回 false。[STATICALLY_CONFIRMED]
- 索引按 8 字节对齐校验；`plausibleOffset()` 的 `0x8000` 上界与 `callVirtualFunc` 的 4096 vtable 槽上界是两套不同判据，未混用。[STATICALLY_CONFIRMED]
- 硬编码 `DEFINE_FIELD` 常量登记在 `hardcodedFields()`，启动日志明确写 `UNVERIFIED`；`CLASS_FIELD` 与散落的字面量偏移甚至不在这份登记表里，属于更弱的证据。[STATICALLY_CONFIRMED]

## 4. 启动判据（Solstice.cpp:257 起）

```cpp
const auto& sigTable = SigManager::statusTable();
const auto& offTable = OffsetProvider::statusTable();
const int badSigs    = sigTable.size() - sigTable.count(ResolveStatus::Ok);
const int badOffsets = offTable.size() - offTable.count(ResolveStatus::Ok);
const int failedSigs = badSigs + badOffsets;
if (!mappingOk || failedSigs > 0) { /* report + DEBUG gate */ }
```

- 计数来源是权威状态表，不是 `mSigs != 0`。[STATICALLY_CONFIRMED]
- `sigreport.json` 同步写出失败项与 near-miss 建议字节；解密镜像 dump 仍需 `SOLSTICE_DUMP=1`，近邻搜索仍需 `SOLSTICE_FUZZ=1`，且安排在 DEBUG 提示之后，避免输出覆盖等待输入行。[STATICALLY_CONFIRMED]
- 早退路径按序 `SigManager::deinitialize()` → `OffsetProvider::deinitialize()` → `SigFuzz::stopAsync()` → `ExceptionHandler::shutdown()` → `spdlog::shutdown()` → `Logger::deinitialize()` → `FreeLibraryAndExitThread()`。[STATICALLY_CONFIRMED]

## 5. DEBUG 人工放行 — 与上游的语义差异

上游状态机把交互替换成 `#ifdef SOLSTICE_ALLOW_UNVERIFIED_MAPPINGS`（CMake 选项，默认 OFF），默认构建会在不提供任何选择的情况下拒绝启动。本整合恢复 b32d185 原样交互，因为重建要求明确：「NOT TRUSTWORTHY 状态仍必须提供人工选择，输入 DEBUG 才继续，ENTER 退出」。[STATICALLY_CONFIRMED]

| 约束 | 实现 | 等级 |
| --- | --- | --- |
| 文本逐字保留 | `"Solstice should not be used in this state."` / `"Type 'DEBUG' to continue, or press ENTER to exit."` | `STATICALLY_CONFIRMED` |
| 只有 `DEBUG` 继续 | `if (input != "DEBUG") → FreeLibraryAndExitThread` | `STATICALLY_CONFIRMED` |
| 不自动放行 | `__DEBUG__` 仅控制是否提供选择 | `STATICALLY_CONFIRMED` |
| 不改变扫描结果 | 状态表在放行前后不变 | `STATICALLY_CONFIRMED` |
| 放行不等于验证通过 | 记 `OPERATOR OVERRIDE: continuing with N unusable mappings; every SDK result in this session is untrusted` | `STATICALLY_CONFIRMED` |

## 6. 与 playUi slot 164 的耦合处理

状态机修正映射后，`MinecraftGame_playUi` 由 b32d185 的惰性值（operand 3 → 3238145 → `index >= 4096` 早退）变成真实的 slot 164。若不处理，等于把「原本不会执行的虚调用」悄悄激活。[STATICALLY_CONFIRMED]

Commit 3 的处理是**把映射事实与调用许可拆开**：

```cpp
const int index = OffsetProvider::MinecraftGame_playUi;   // 164，静态证据保留
if (index < 2 || index >= 4096) return;
#if defined(SOLSTICE_PLAYUI_SEMANTIC_VERIFIED)
    if (!MemUtils::isValidVirtualCall(this, index)) return;
    MemUtils::callVirtualFunc<void, const std::string&, float, float>(index, this, soundName, volume, pitch);
#else
    return; // PLAYUI_SEMANTIC_VALIDATION: UNVERIFIED UNTIL PROVEN
#endif
```

- `SOLSTICE_PLAYUI_SEMANTIC_VERIFIED` 在仓库内只以 `#if defined(...)` 形式出现，没有任何 `add_compile_definitions` / `#define` / `-D` 定义点。[STATICALLY_CONFIRMED]
- 因此发布构建**不可能**走到 slot 164 虚调用路径，稳定基线「playUi 不发声」的可观察行为保持不变（playUi 是可选音效，启动流程不依赖它）。[STATICALLY_CONFIRMED]
- 仍然 `UNVERIFIED`：slot 164 背后函数身份、MinecraftGame 对象生命周期/起始地址、playUi 线程约束、运行镜像身份。[UNVERIFIED]

## 7. 与既有基线的关系

- 43 项签名/偏移未解析基线不变（59 offsets 中 21、115 signatures 中 22）；状态机不新增解析，只让不可信结果不被消费。[STATICALLY_CONFIRMED]
- `RenderUtils::transform` 在 GL 路径恒为零、`Level::getHitResult()` SUSPECT 恒 null 等技术基线不受本次融合影响，因为这些问题所在的运行时文件零改动。[STATICALLY_CONFIRMED]
- 离线扫描器与运行时一致性 94/95 的历史结论不属于本轮验证范围，未重新声明。[DEFERRED]
