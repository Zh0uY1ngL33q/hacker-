# 兼容性风险登记册

每条风险含：证据、等级、当前缓解、剩余动作。等级含义见 `reconstruction-summary.md` §5。

## R-01 — 完整构建未验证

- **风险**：本机无 `cl` / `ld` / `ninja` / `g++`，`CMakeLists.txt:30` 还强制检查 `ld`；无法完成 configure、Release x64 编译与 DLL link。[STATICALLY_CONFIRMED]
- **影响**：可能存在本机静态检查发现不了的缺失符号、重复定义、宏冲突、头文件包含顺序问题。[UNVERIFIED]
- **当前缓解**：13 文件 include 闭包已静态校验；`ResolveState.hpp` header-only 不进 unity 源收集；CPM 引导器以 CMake script mode 实测通过。[STATICALLY_CONFIRMED]
- **剩余动作**：构建机执行 configure + build，回传全文与 DLL SHA-256。[DEFERRED]

## R-02 — 运行镜像身份未确认

- **风险**：本分支没有 EXE SHA-256 / PE ImageBase / SizeOfImage / DLL SHA / `build_info.h` 实测值；沿用 `RUNTIME_IMAGE_IDENTITY_UNVERIFIED`。[UNVERIFIED]
- **影响**：所有「目标版本 1.21.120 证据」都建立在静态镜像上，无法证明与注入时运行的 EXE 一致。[UNVERIFIED]
- **当前缓解**：不把静态证据升级为运行期许可（见 R-03）。[STATICALLY_CONFIRMED]
- **剩余动作**：构建/注入时采集镜像指纹并与 `sigreport.json`、Build Info 对照。[DEFERRED]

## R-03 — playUi slot 164 语义未验证

- **风险**：slot 164 背后函数身份、MinecraftGame 对象生命周期/起始地址、playUi 线程约束、运行镜像身份全部 `UNVERIFIED`；3.6.2-D/E 曾在**已定位**的 MinecraftGame 对象上观察到不可读 vtable（`this=0x13134090`，`vtable=0x3573F2CD48F1B9AF`）。[UNVERIFIED]
- **当前缓解**：静态映射 164 保留为证据事实；虚调用默认拒绝；`SOLSTICE_PLAYUI_SEMANTIC_VERIFIED` 无任何定义点；即便未来显式开启仍需 `MemUtils::isValidVirtualCall()` 通过。[STATICALLY_CONFIRMED]
- **剩余动作**：运行期语义验证 + 明确 opt-in 才能启用；启用前不得把 ABI 文档结论（层① `XMM2/XMM3`）当作调用许可。[DEFERRED]

## R-04 — Hook 初始化期单独提前 enable 的语义冲突

- **风险**：用户明确禁止「重新引入」初始化期单独 `mDetour->enable()`；但 `b32d185` 本身在 `RenderItemInHandHook.cpp:28` 与 `BaseTickHook.cpp:106` 各有一处，且 `origin/main` 完全相同。[STATICALLY_CONFIRMED]
- **影响**：若第二轮 `Created detour` 后卡死确由该提前 enable 引起，稳定基线在这一项上并不「安全」。[UNVERIFIED]
- **当前缓解**：本整合未新增任何提前 enable；`HookManager::init(false)` 的统一 `MH_EnableHook(MH_ALL_HOOKS)` 保留；`BaseTickHook` 属于无统一 enable 的 `init(true)` 路径，其提前 enable 不可删除。[STATICALLY_CONFIRMED]
- **剩余动作**：由用户决定是否单独删除 `RenderItemInHandHook.cpp:28` 一行并做运行期 A/B；本分支保持零差异，不预先改动。[DEFERRED]

## R-05 — GL 首帧 RenderEvent 根因未定位

- **风险**：Batch 3.6.2-F 收口 `NEEDS_RUNTIME_CAPTURE`；本分支不带订阅者级诊断，也没有运行期日志/Dump。[UNVERIFIED]
- **影响**：Dispatcher 同步遍历无异常隔离、`shared_lock` 期间回调内 `listen/deafen` 可能锁升级死锁等静态风险仍在稳定实现里。[STATICALLY_CONFIRMED]
- **当前缓解**：不把实验性诊断堆进稳定运行时；`patchFullStack at 0x0` 已判为宏安全早返回 `BENIGN_SIGNATURE_MISS`，不是崩溃源。[STATICALLY_CONFIRMED]
- **剩余动作**：需要订阅者级 BEGIN/END 日志或 minidump 才能定位；按独立诊断分支处理。[DEFERRED]

## R-06 — `getBlockSource()` nullptr gate 暂缓

- **风险**：约 55～57 个调用点 / 21 文件，大量 `getBlockSource()->getBlock(...)` 直接链式 deref；提前让返回值可能为 null 会新增崩溃面。[STATICALLY_CONFIRMED]
- **当前缓解**：`ClientInstance.cpp` 与 b32d185 逐字一致，`getBlockSource()` 仍直接返回 `callVirtualFunc<BlockSource*>`。[STATICALLY_CONFIRMED]
- **剩余动作**：先完成消费者保护改造，再单独评估 `9229523`。[DEFERRED]

## R-07 — 上游 3.6.2-E.1 空指针保护的自身缺陷

- **风险**：E.1 在 `ContextRecord` 为空时给 `StackWalker::ShowCallstack()` 传 `nullptr`，而该函数在 `StackWalker.cpp:77-82` 无条件读 `pContext->Rip/Rbp/Rsp`，会在崩溃处理器内二次崩溃。[STATICALLY_CONFIRMED]
- **当前缓解**：Commit 5 改为「无 ContextRecord 就跳过栈回溯，并在 crash.log 明确写 `unavailable: no ContextRecord in exception pointers`」，保留 `ExceptionRecord` 的故障上下文记录。[STATICALLY_CONFIRMED]
- **附带修复**：移除 b32d185 中 `sw.~StackWalker()` 显式析构 + 作用域结束自动析构的重复析构（`~StackWalker()` 会 `UnloadModuleSymbols()`）。[STATICALLY_CONFIRMED]
- **剩余动作**：异常路径只有在真实崩溃时才会执行，本机无运行期证据。[UNVERIFIED]

## R-08 — 硬编码字段与未解析映射基线

- **风险**：43 项签名/偏移未解析基线不变（59 offsets 中 21、115 signatures 中 22）；`DEFINE_FIELD` 字面量常量登记为 `UNVERIFIED`，`CLASS_FIELD` 与散落字面量偏移甚至不在登记表内。[STATICALLY_CONFIRMED]
- **当前缓解**：状态机只让 non-Ok 结果不被消费，不伪造解析成功；启动日志单独报告 hardcoded 数量为 `UNVERIFIED`。[STATICALLY_CONFIRMED]
- **剩余动作**：目标版本离线重拟合属独立工作，不在本轮范围。[DEFERRED]

## R-09 — DEBUG 人工放行可能被误解为验证通过

- **风险**：操作员输入 `DEBUG` 后进程继续运行，但映射状态完全不变。[STATICALLY_CONFIRMED]
- **当前缓解**：日志逐字记录 `OPERATOR OVERRIDE: continuing with N unusable mappings; every SDK result in this session is untrusted`；`ENTER` 退出；不自动放行。[STATICALLY_CONFIRMED]
- **剩余动作**：验收时确认该文本原样出现，且无人把它当作「映射已验证」。[DEFERRED]

## R-10 — CPM vendored 副本的维护成本

- **风险**：固定 vendored v0.38.7 + SHA-256 校验意味着未来升级 CPM 必须同时更新 `CPM_HASH_SUM`。[STATICALLY_CONFIRMED]
- **当前缓解**：哈希不匹配直接 `FATAL_ERROR`，不会静默退化到空文件；下载逻辑保留为回退。[STATICALLY_CONFIRMED]
- **剩余动作**：升级时同步更新三处（文件名、版本变量、哈希）并重跑 `cmake -P tools/verify_cpm_bootstrap.cmake`。[DEFERRED]

## R-11 — Build Info 溯源字段缺口

- **风险**：b32d185 的 `GenerateBuildInfo.cmake` 不输出 worktree dirty 标志，也不记录诊断选项 0/1；crash.log 里的 commit/branch/fingerprint 需要与构建机实际状态核对。[STATICALLY_CONFIRMED]
- **当前缓解**：不移植 main 线的伪溯源字段（选项在本构建图里不存在）；`add_dependencies(${PROJECT_NAME} generate_build_info ...)` 构建图边已存在。[STATICALLY_CONFIRMED]
- **剩余动作**：构建机回传 `build_info.h` 全文与 DLL SHA-256 后再做 provenance 绑定。[DEFERRED]

## R-12 — 融合本身未做运行期 A/B

- **风险**：`STABLE_RUNTIME_WITH_CURRENT_SDK` 目前是**代码层与静态证据层**的结论，没有一次成功的注入运行支撑。[STATICALLY_CONFIRMED]
- **当前缓解**：所有结论只用 `STATICALLY_CONFIRMED` / `BUILD_VERIFIED`（仅 script mode）/ `UNVERIFIED` / `DEFERRED`，不出现 `RUNTIME_VERIFIED`。[STATICALLY_CONFIRMED]
- **剩余动作**：构建机完成编译 → 注入 → 回传 `solstice.log` / `crash.log` / `sigreport.json` → 才能升级综合判定。[DEFERRED]

---

## 汇总

| 等级 | 条目数 |
| --- | --- |
| `STATICALLY_CONFIRMED` | 风险事实 / 缓解措施均已静态确认 |
| `UNVERIFIED` | R-01 编译、R-02 镜像、R-03 slot 164、R-04 故障归因、R-05 根因、R-07 异常路径执行 |
| `DEFERRED` | 全部剩余动作 |
| `RUNTIME_VERIFIED` | **0** |

**没有一条风险被标记为已消除。** 本整合交付的是「不再叠加实验性运行时修改、且 SDK 结果不被误用」的可构建候选，而不是根因修复版本。[STATICALLY_CONFIRMED]
