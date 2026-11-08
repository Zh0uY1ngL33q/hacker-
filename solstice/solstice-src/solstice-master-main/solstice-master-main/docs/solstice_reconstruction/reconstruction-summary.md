# Solstice Full Integration — 重建总览

> Stable Runtime × Modern SDK · 策略 B（指定融合新线与旧线）
> 目标状态：`STABLE_RUNTIME_WITH_CURRENT_SDK`

| 项 | 值 | 证据等级 |
| --- | --- | --- |
| 集成分支 | `recovery/b32d185-full-integration` | `STATICALLY_CONFIRMED` |
| 起点 | `b32d1856702ef8f1d3e4f92bb17b5a286736932f` | `STATICALLY_CONFIRMED` |
| `merge-base(HEAD, origin/main)` | `b32d185` | `STATICALLY_CONFIRMED` |
| `origin/main` 是否被合入 | 否 | `STATICALLY_CONFIRMED` |
| `main` / `origin/main` | 均保持 `e875543`，未改动 | `STATICALLY_CONFIRMED` |
| 功能组提交数 | 6（另 1 个纯文档勘误提交，无代码改动） | `STATICALLY_CONFIRMED` |
| 改动文件数（b32d185..HEAD，代码/工具） | 18 + 9 份交付文档 + 1 份验收脚本 | `STATICALLY_CONFIRMED` |
| 非 SDK 运行时代码 diff | 0 文件 | `STATICALLY_CONFIRMED` |
| 完整 configure / MSVC Release x64 / DLL link | 本机不可执行 | `DEFERRED` |
| 运行期日志 / Dump / EXE-DLL SHA | 未采集 | `DEFERRED` |
| **综合判定** | **`STABLE_RUNTIME_WITH_CURRENT_SDK / BUILD_UNVERIFIED`** | `STATICALLY_CONFIRMED` |

---

## 1. 目标与策略

重建要求不是「再打一层诊断补丁」，而是交付一个**完整、可构建的整合候选版本**：以稳定运行时为基础，融合当前已完成且有证据支持的映射 / SDK / 底层适配成果，并把后续非 SDK 的实验性运行时、渲染、Hook 修改恢复为稳定基线的正常实现。[STATICALLY_CONFIRMED]

实现方式：从 `b32d185` 严格新建分支，按实际 diff 代码块（不是按文件名）建立融合矩阵，逐组提交；**不合并不带诊断的 main 线提交，不 cherry-pick 混合提交，不覆盖 `recovery/b32d185-sdk-integration`，不删除任何 worktree / 未跟踪日志 / ZIP / Dump / 审计材料，不 force push。** [STATICALLY_CONFIRMED]

## 2. 基线判断（用户授权自行裁定）

用户补充：「`4141241` 实际上更早更稳定，但同时因为更早也更不好判断怎么融合，改了什么」。核查结论：

| 事实 | 命令/证据 | 等级 |
| --- | --- | --- |
| `f8c3541` 是 `b32d185` 的祖先，`b32d185` 是 `4141241` 的祖先 | `git merge-base --is-ancestor` | `STATICALLY_CONFIRMED` |
| `4141241` 在项目子目录内与 `f8c3541` 树逐字相同 | `git ls-tree -r` 对比输出 IDENTICAL | `STATICALLY_CONFIRMED` |
| `4141241` 是状态机提交 `4b93323` 的直接父提交 | `git log --format="%h %p"` | `STATICALLY_CONFIRMED` |
| `b32d185..4141241` 在 `src/SDK` 上是 **8 文件 / −284 行**的回退（会撤销 b32d185 的 SDK 修复） | `git diff --stat` | `STATICALLY_CONFIRMED` |
| `4141241..1ec0571` 共 36 文件：14 个 CMake/SDK/Utils 代码文件、15 份 docs、5 个 tools、2 份根审计产物；**Hook / Features / include/nes / Client 为 0 文件** | `git diff --name-only` | `STATICALLY_CONFIRMED` |

**裁定**：运行时基础保持 `b32d185`（它已经含稳定 GUI/GL/Hook/Aura 实现且没有 4141241 的大 revert），SDK 新线以 `4141241` 为**差分起点**取净差分。这样既拿到 `4b93323 → 47284b7 → Batch1/2/3` 的 SDK 成果，又不把 4141241 的 26 文件运行时回退混进融合结果。[STATICALLY_CONFIRMED]

SDK 差分源最终取 `4141241..1ec0571` 的 13 文件闭包（14 个代码文件中排除 `CMakeLists.txt`，因为它携带 main 线的诊断选项）。[STATICALLY_CONFIRMED]

## 3. 六个功能组

| # | 提交 | 主题 | 主要文件 | 等级 |
| --- | --- | --- | --- | --- |
| 1 | `9427362` | Build Bootstrap — vendored CPM v0.38.7 | `cmake/CPM.cmake`, `cmake/CPM_0.38.7.cmake`, `tools/verify_cpm_bootstrap.cmake` | `BUILD_VERIFIED`（脚本模式） |
| 2 | `d3a2e27` | Mapping State Machine — 13 文件依赖闭包 | `ResolveState.hpp`, `OffsetProvider.*`, `SigManager.*`, `CodeSection.*`, `SigFuzz.cpp`, `Solstice.cpp`, `MemUtils.*`, `ExceptionHandler.*` | `STATICALLY_CONFIRMED` |
| 3 | `caa03fb` | Batch 1～3 SDK Mapping + playUi 调用闸门 | `OffsetProvider.hpp`, `MinecraftGame.cpp` | `STATICALLY_CONFIRMED` |
| 4 | `90e320f` | MinecraftSim SDK — 探针失败退避重试 | `MinecraftSim.cpp` | `STATICALLY_CONFIRMED` |
| 5 | `3988d6a` | Safe SDK Compatibility — 异常处理器分流审查移植 | `ExceptionHandler.cpp` | `STATICALLY_CONFIRMED` |
| 6 | 本提交（HEAD） | Stable Runtime Integration — 零差异核验 + 9 份交付物 + 验收脚本 | `docs/solstice_reconstruction/*`, `tools/verify_full_integration.py` | `STATICALLY_CONFIRMED` |

## 4. 融合结果速览

- **SDK 保留**：映射可信度状态机完整闭包、Batch1/2/3 证据映射、`getBlockSource` 索引 22、`playUi` slot 164 静态映射、`BlockSource_mBuildHeight` 类型化取值、MinecraftSim 2 秒退避重试、必要的 `MemUtils` 防御。[STATICALLY_CONFIRMED]
- **运行时保留**：`OpenGLGui` / `D3DHook` / SwapBuffers / ImGui backends / ClickGUI / `event_dispatcher` / Dispatcher / `ModuleManager` / `FeatureManager` / `ModuleBase` / ESP 系列 / `HookManager` / MinHook / Detour / Aura 全部与 `b32d185` 逐字一致。[STATICALLY_CONFIRMED]
- **调用安全**：slot 164 的静态映射事实保留，虚调用默认拒绝；`SOLSTICE_PLAYUI_SEMANTIC_VERIFIED` 在任何构建目标都未定义。[STATICALLY_CONFIRMED]
- **构建修复**：`CPMAddPackage` 未定义错误由本地固定版本 CPM + SHA-256 校验 + command guard 解决，`cmake -P` 实测通过。[BUILD_VERIFIED]
- **明确暂缓**：`9229523` 的 `getBlockSource()` nullptr gate、`SOLSTICE_RENDER_DIAG` 订阅者诊断、E.1 的 `WORKTREE_DIRTY` / 诊断选项 0/1 记录、3.6.2-D/E 的 `diagnosePlayUiRejection`。[DEFERRED]

## 5. 证据等级口径

| 等级 | 含义 | 本轮数量（验收脚本 30 项） |
| --- | --- | --- |
| `STATICALLY_CONFIRMED` | 由本机源码 / git 对象直接证明 | 27 |
| `BUILD_VERIFIED` | 由实际执行的构建工具证明（仅 CMake script mode） | 1 |
| `RUNTIME_VERIFIED` | 由捕获的运行期日志 / Dump 证明 | **0** |
| `UNVERIFIED` | 断言检查失败 | 0 |
| `DEFERRED` | 本轮明确不覆盖 | 2 |

**本轮不提出任何 `RUNTIME_VERIFIED` 结论。** [STATICALLY_CONFIRMED]

## 6. 明确不宣称的内容

- 不宣称 DLL 已编译 / 链接成功、不提供 DLL SHA-256。[DEFERRED]
- 不宣称运行镜像身份、slot 164 目标函数身份、MinecraftGame 生命周期或 playUi 线程约束已验证 —— 沿用 `RUNTIME_IMAGE_IDENTITY_UNVERIFIED`。[UNVERIFIED]
- 不宣称 GL 第一帧 RenderEvent 卡死 / 闪退根因已定位或已修复；本分支不携带订阅者级诊断。[DEFERRED]
- 不宣称 43 项未解析签名/偏移基线（59 offsets 中 21、115 signatures 中 22）有任何变化；状态机只让不可信结果**不被当作有效值**，不产生新解析。[STATICALLY_CONFIRMED]

## 7. 交付物

`reconstruction-summary.md`、`integration-manifest.json`、`sdk-preservation.md`、`mapping-state-machine-integration.md`、`runtime-restoration.md`、`build-system-fix.md`、`regression-history.md`、`compatibility-risks.md`、`final-validation.md`。配套可复跑工具：`tools/verify_full_integration.py`、`tools/verify_cpm_bootstrap.cmake`。[STATICALLY_CONFIRMED]

## 8. 要跑的命令 → 回传什么 → 自查口径

**要跑的命令**（在构建机，仓库根 `G:\hacker`）：

```bat
git fetch origin
git checkout recovery/b32d185-full-integration
python solstice\solstice-src\solstice-master-main\solstice-master-main\tools\verify_full_integration.py
cmake -S solstice\solstice-src\solstice-master-main\solstice-master-main -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

**回传什么**：`cmake` configure 全文、`cmake --build` 全文、生成的 DLL 路径与 SHA-256、`build_info.h` 内容；若注入运行，`solstice.log` / `crash.log` / `sigreport.json` 与首帧是否出现 `Created detour` 后卡死。

**自查口径**：configure 不再出现 `Unknown CMake command "CPMAddPackage"`；编译无缺失符号 / 重复定义；`verify_full_integration.py` 退出码 0 且无 `UNVERIFIED`；DLL 能加载且 DEBUG 门控文本原样出现；未出现新的初始化期死锁。上述任何一项未满足，综合判定必须退回 `BUILD_UNVERIFIED`，不得升级为 `RUNTIME_VERIFIED`。[DEFERRED]
