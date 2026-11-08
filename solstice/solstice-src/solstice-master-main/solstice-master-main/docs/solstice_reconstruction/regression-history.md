# 回归史与本轮决策依据

本文记录导致本次「稳定运行时 × 现代 SDK」重建的故障链、各轮诊断结论，以及它们如何转化为本分支的具体取舍。历史长文本结论只作背景，不作为当前状态判据。[STATICALLY_CONFIRMED]

## 1. 时间线

| 阶段 | 事件 | 对本次融合的影响 | 等级 |
| --- | --- | --- | --- |
| 稳定基线 | `f8c3541` → `b32d185`（含 `fa0930f` Aura2 重写、`811d324` setPosition 消歧、`9a2f1a8` Aura 静默退出移除） | 选作运行时基础 | `STATICALLY_CONFIRMED` |
| 第一轮故障 | GL / RenderEvent / GUI 首帧崩溃 | 后续所有 GL/Dispatcher/视觉诊断不进本分支 | `STATICALLY_CONFIRMED` |
| 第二轮故障 | `RenderItemInHandHook::init()` 附近 `Created detour` 后卡死，怀疑初始化期单独提前 `mDetour->enable()` | 形成「禁止重新引入提前 enable」约束；本分支未新增，冲突见 `runtime-restoration.md` §3 | `STATICALLY_CONFIRMED` |
| `1f95ebc` / `5ecf458` / `e58ed3e` / `07b3d86` / `73da588` / `c2bd7ad` / `d2dd463` | 一系列 GUI / GL / Aura / Hook 实验性诊断与回退 | 全部不移植 | `STATICALLY_CONFIRMED` |
| `4141241` | `revert: restore last known playable build (f8c3541)`，树与 `f8c3541` 逐字相同 | 用户指出它「更早更稳定」，但作为**运行时基础**会撤销 b32d185 的 8 文件 / −284 行 SDK 修复；改作 SDK 差分起点 | `STATICALLY_CONFIRMED` |
| `4b93323` → `84e8160` → `9b8f7d8` → `47284b7` | 映射可信度状态机链 | 必须整体移植 13 文件闭包，不能逐条 cherry-pick | `STATICALLY_CONFIRMED` |
| Batch 1/2/3 `517646e` / `1d2c493` / `1ec0571` | 目标版本证据映射修复 | 在状态机之上移植；`OffsetProvider.hpp` 与 `1ec0571` 黄金快照一致 | `STATICALLY_CONFIRMED` |
| `411fa4e` | MinecraftSim 探针失败后重试 | 唯一可独立移植的 SDK 项 | `STATICALLY_CONFIRMED` |
| `9229523` | `getBlockSource()` 增加 nullptr gate | 暂缓：约 55～57 调用点 / 21 文件、大量无保护链式 deref | `DEFERRED` |
| `669ce2c` | 3.6.2-D playUi AV 最小防御 + 故障上下文 | 移植故障上下文；playUi 调用改成本分支更严格的默认拒绝 | `STATICALLY_CONFIRMED` |
| `2429e6e` / `b879cb1` | 3.6.2-E.1 守卫诊断、Build Info、处理器空指针保护 | 空指针保护移植并修正缺陷；拒绝诊断与 Build Info 不移植 | `STATICALLY_CONFIRMED` |
| `67f82f0` / `794fc65` | E.1.2 / E.1.2.1 文档纠错，ABI 最终裁定 `RCX/RDX/XMM2/XMM3` | 纯文档，无代码可移植；不作为调用许可 | `STATICALLY_CONFIRMED` |
| Batch 3.6.2-F | RenderEvent 订阅者级诊断，收口 `NEEDS_RUNTIME_CAPTURE` | 不进稳定运行时；根因仍 `UNVERIFIED` | `DEFERRED` |
| Batch 3.7 | 稳定基线 SDK 选择性移植，结论 `SDK_BATCH_INTEGRATION_READY` | 提供 31 提交 / 122 文件分类清单与排除依据 | `STATICALLY_CONFIRMED` |
| 本轮 | 策略 B 完整融合 | 6 功能组 + 9 交付物 | `STATICALLY_CONFIRMED` |

## 2. 关键回归教训

1. **把「映射解析成功」等同于「值可用」是根因之一**：过拟合的偏移/签名会给出非零但错误的值，随后在虚调用或成员 deref 处变成 AV。状态机的价值是让 non-Ok 结果不被消费，而不是提高命中率。[STATICALLY_CONFIRMED]
2. **修正映射可能悄悄激活原本惰性的调用**：b32d185 的 `playUi` 因 operand 3 得到 3238145 而在 `index >= 4096` 早退，改为 slot 164 后若不加闸门就等于新增一个未验证虚调用。本轮把「映射事实」与「调用许可」拆成两层。[STATICALLY_CONFIRMED]
3. **诊断代码本身可以制造新故障**：Batch 3.6.2-F 的订阅者级诊断刻意绕开 `spdlog` 热路径、用 `WriteFile` + `FlushFileBuffers`、不在 AV 后继续运行、用 `dynamic_cast<void*>` 地址查表避免 deref 未知 `void*`。这些谨慎设计仍不足以让诊断进入稳定构建。[STATICALLY_CONFIRMED]
4. **上游「空指针保护」也可能是缺陷**：E.1 给 `StackWalker::ShowCallstack()` 传 `nullptr`，而该函数在 `StackWalker.cpp:77` 无条件读 `pContext->Rip`。Commit 5 改为「无 ContextRecord 就跳过栈回溯并明确记录」，同时移除 b32d185 里 `sw.~StackWalker()` 的重复析构。[STATICALLY_CONFIRMED]
5. **构建失败不是代码问题**：`CPMAddPackage` 未定义源于 configure 期下载写空文件；修复方式是固定版本 + vendored + 哈希 + command guard，而不是改业务代码。[STATICALLY_CONFIRMED]
6. **nullptr gate 的收益依赖消费者**：`getBlockSource()` 返回 nullptr 只有在所有调用点都检查时才安全；现状下引入它反而扩大崩溃面。这是本轮最典型的「有证据但不能独立移植」案例。[STATICALLY_CONFIRMED]

## 3. 从历史结论中刻意不继承的内容

- 不继承「XMM0/XMM1」旧 ABI 结论作为调用依据；E.1.2.1 已把层①类型推导修订为 `XMM2/XMM3`，层②③仍 `BINARY_ABI_CORROBORATION_PENDING`。本整合的 playUi 默认拒绝使该争议不影响发布行为。[STATICALLY_CONFIRMED]
- 不继承 `RUNTIME_IMAGE_MISMATCH` 的强断言，沿用 `RUNTIME_IMAGE_IDENTITY_UNVERIFIED`（无法访问 ≠ 不匹配）。[STATICALLY_CONFIRMED]
- 不继承「无 .rdata = 无 RTTI」的否定结论，沿用 `RTTI_RESOLUTION_UNVERIFIED`。[STATICALLY_CONFIRMED]
- 不继承「离线扫描器与运行时一致 94/95」作为本轮验收证据。[DEFERRED]

## 4. 状态结论

历史故障链中的运行时层问题没有被「修好」——本整合采取的是**回到没有这些实验性改动的稳定实现**；SDK 层问题则通过状态机与调用闸门被约束为「不可信即不使用」。因此综合状态是 `STABLE_RUNTIME_WITH_CURRENT_SDK / BUILD_UNVERIFIED`，而不是 `ALL_ISSUES_RESOLVED`。[STATICALLY_CONFIRMED]
