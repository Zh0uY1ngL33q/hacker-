# MIGRATION_REPORT — 迁移执行情况

## 1. 做了什么

| 阶段 | 产出 | 状态 |
|---|---|---|
| 一 项目审计 | `SOURCE_MAP.md`、`DEPENDENCY_MAP.md`、`BUILD_BASELINE.md`、`FEATURE_COMPATIBILITY.md` | 完成（数据由脚本产出） |
| 二 PE 静态分析 | `PE_ANALYSIS.md`、`SECTION_REPORT.md`、`BINARY_METADATA.json` | 完成 |
| 三 差异分析 | `VERSION_DIFF.md`、`COMPATIBILITY_MATRIX.md` | 完成，旧版项目全部标 UNKNOWN（无样本） |
| 四 架构迁移 | 见 §3 | **部分完成**，只做了有证据支撑的最小修复 |
| 五 编译与测试 | `TEST_REPORT.md` | **阻塞**（本机无编译器） |
| 六 交付 | `FINAL_REPORT.md`、`REVERSE_ENGINEERING_REPORT.md`、`CHANGELOG.md` | 完成 |

配套工具（新增，可复用）：

```
tools/re/scan_sigs.py        离线复现客户端签名解析（已验证与线上逐名一致）
tools/re/rtti_vtable.py      RTTI → COL → vtable → 槽位
tools/re/pe_report.py        两个样本的 PE 元数据
tools/re/audit_patches.py    字节补丁边界/寄存器活性审计
tools/re/symbol_usage.py     未解析符号 → 源码引用 → 功能
```

## 2. 代码改动（全部有镜像字节支撑）

| 位置 | 改动 | 依据 |
|---|---|---|
| `Animations.cpp` | 新增 `resolveSwingAngleConstant()`：解码 `mulss xmm12,[rip+d]` 的操作数，校验 opcode 与量级，缓存原值 | `0x8F31EF0 = -80.000°`；旧地址 `0x8F5A000 = -14.000°` |
| `Animations.cpp` | `gNoSwitchAnimation` 8 字节 → 4 字节 | `xmm1` 的定义被 8 字节形态删除，读者在 `0x58EB4FF` |
| `Animations.cpp` | `onDisable()` 还原缓存值，不再硬写 `radians(-80)` | 同上，避免关掉模块后仍污染 -14° 条目 |
| `Animations.hpp` | 新增 `mSwingAngleOriginal` | — |
| （已提交，非本轮） | `build_info.h` 增 `SOURCE_FINGERPRINT` / `BUILD_TIMESTAMP` | 让日志能对应到源码 |

## 3. 没有做的事，以及为什么

### 3.1 没有做 Core/UI/Platform 目录搬迁

任务书给了一份 `Solstice/{Core,UI,Platform,Diagnostics,Tests,Documentation}` 目标结构。
**本轮不执行**，理由是可验证性，不是工作量：

1. 本机无编译器（`BUILD_BASELINE.md §3`），627 个文件、260 个 .cpp 的 include 路径搬迁
   一旦出错，用户拿到的是一份**编译不过**的源码，而错误会被归因到“AI 又乱改”。
2. 该搬迁不产生任何**可观测**收益：`SDK/` 与 `Features/` 之间的依赖方向本来就是单向的
   （`SOURCE_MAP.md §3`），真正需要解耦的是“符号解析”这一层，而不是文件位置。
3. 任务书同时要求“尽可能复用原有源码和架构”，与全量重排目录相冲突。

**替而做的是**：在 `DEPENDENCY_MAP.md §3` 里指出了真正的适配层边界——
只有 `OffsetProvider.hpp` / `SigManager.hpp` 两个文件需要抽象后端，其余全部通过它们间接依赖
目标程序。这是有依据（符号引用扫描）的结论，不是泛泛而谈。

### 3.2 没有为 20 个 failed 签名逐个“修复”

因为“修”= 找到一个**新地址**，而新地址必须有语义证据。本轮只对 3 处做到这一点
（见 §2 与 `REVERSE_ENGINEERING_REPORT.md §4/§5/§6`），其余保持未解析。
把未验证的地址塞进去，正是这次要摆脱的做法。

### 3.3 没有改 `DefaultSwing` / `DefaultSwing2` / `patchInHandSlot`

三处都已定位到具体字节、都给出了替代方案（`REVERSE_ENGINEERING_REPORT.md §5`），
但改完本机无法编译验证，属于“未经测试的改动”，按最终原则不提交。

## 4. 明确的能力限制（不是待办，是结论）

| 限制 | 依据 |
|---|---|
| 目标程序**没有可用的官方扩展 API** | `.edata` 只有 28 个 WinRT 激活导出；无 Mod 注册入口 |
| 无法做到“运行时热切换游戏版本” | `libhat::compile_signature` 在编译期展开模式串 |
| 无法从磁盘 exe 读取运行时地址 | 该文件 BASERELOC 仅 0x10 字节、`.rdata` 指针指向模块外 |
| 无法分析 `.winlice` 节 | 镜像未转储（rawSize=0） |

## 5. 建议的下一步（按投入产出排序）

1. **装工具链并把本轮 4 个 commit 编出来**（唯一的前置条件）。若编译失败，把
   `cmake --build` 完整输出贴回，可逐个修。
2. 注入后只看 3 行日志：`[Animations] swing-angle constant 0x... = -1.3963 rad`、
   手持物品默认倾斜是否恢复、快速切物品是否还抽动。这条能一次性判定 §2 的两个修复。
3. 用 `rtti_vtable.py` 把 `Level` 的纯 getter 槽位（`mov rax,[rcx+D]; ret` 形态）全部列出来，
   与 `getHitResult` / `getLevelData` / `getPlayerList` 的返回类型逐个对，能补上
   `FEATURE_COMPATIBILITY.md §3` 里最大的一块空洞。
4. 删掉 18 个“未引用且未解析”的声明，把日志噪声降到 56 项。
