# 最终验收

## 1. 可复跑验收门

```bash
python solstice/solstice-src/solstice-master-main/solstice-master-main/tools/verify_full_integration.py
# 退出码 0 = 无 UNVERIFIED；--json <path> 可导出机器可读报告
cmake -P solstice/solstice-src/solstice-master-main/solstice-master-main/tools/verify_cpm_bootstrap.cmake
```

脚本只读，不编译 DLL、不启动游戏、不伪造运行期证据。30 项检查结果如下（推送前复跑应与本表一致）。[STATICALLY_CONFIRMED]

## 2. 30 项检查

| ID | 检查项 | 结果 | 等级 |
| --- | --- | --- | --- |
| GIT-01 | HEAD 由 `b32d185` 派生 | PASS | `STATICALLY_CONFIRMED` |
| GIT-02 | 未合入 `origin/main` | PASS | `STATICALLY_CONFIRMED` |
| GIT-03 | `main` / `origin/main` 保持 `e875543` | PASS | `STATICALLY_CONFIRMED` |
| GIT-04 | 恰好 6 个功能组提交 | PASS | `STATICALLY_CONFIRMED` |
| GIT-05 | 全部改动落在 SDK/build/tools/docs 允许集 | PASS | `STATICALLY_CONFIRMED` |
| RT-01 | 非 SDK 运行时目录零差异 | PASS | `STATICALLY_CONFIRMED` |
| RT-02 | 25 类命名运行时模块均为 b32d185 blob | PASS | `STATICALLY_CONFIRMED` |
| RT-03 | `MH_EnableHook(MH_ALL_HOOKS)` 统一启用保留 | PASS | `STATICALLY_CONFIRMED` |
| RT-04 | 提前 `mDetour->enable()` 为继承而非新增 | PASS | `STATICALLY_CONFIRMED` |
| SDK-01 | `ResolveStatus` 六状态齐全 | PASS | `STATICALLY_CONFIRMED` |
| SDK-02 | 两个 `statusTable()` 权威入口 | PASS | `STATICALLY_CONFIRMED` |
| SDK-03 | 初始化结论来自 `allOk()` | PASS | `STATICALLY_CONFIRMED` |
| SDK-04 | 启动从状态表聚合失败数 | PASS | `STATICALLY_CONFIRMED` |
| SDK-05 | DEBUG 门控逐字、非自动、记为 override | PASS | `STATICALLY_CONFIRMED` |
| SDK-06 | `OffsetProvider.hpp` 与 `1ec0571` 黄金快照一致 | PASS | `STATICALLY_CONFIRMED` |
| SDK-07 | Batch1/2/3 证据映射存在 | PASS | `STATICALLY_CONFIRMED` |
| SDK-08 | 不可信/硬编码值被拒绝或登记 | PASS | `STATICALLY_CONFIRMED` |
| SDK-09 | 改动 SDK/Utils 文件的项目内 include 全部可解析 | PASS | `STATICALLY_CONFIRMED` |
| SAFE-01 | 任何构建目标都无法激活 slot 164 虚调用 | PASS | `STATICALLY_CONFIRMED` |
| SAFE-02 | `isValidVirtualCall` 存在且用于 opt-in 路径 | PASS | `STATICALLY_CONFIRMED` |
| SAFE-03 | `getBlockSource()` nullptr gate 未引入 | PASS | `STATICALLY_CONFIRMED` |
| SAFE-04 | 异常过滤器空指针安全 + 原始故障上下文 | PASS | `STATICALLY_CONFIRMED` |
| SAFE-05 | 早退路径恢复异常过滤器 | PASS | `STATICALLY_CONFIRMED` |
| SIM-01 | MinecraftSim 探针失败退避重试 | PASS | `STATICALLY_CONFIRMED` |
| BUILD-01 | vendored CPM SHA-256 匹配 | PASS | `STATICALLY_CONFIRMED` |
| BUILD-02 | include 后 `CPMAddPackage` 已定义 | PASS | `BUILD_VERIFIED` |
| BUILD-03 | `generate_build_info` 构建图边存在 | PASS | `STATICALLY_CONFIRMED` |
| BUILD-04 | 完整 configure + MSVC Release x64 link | — | `DEFERRED` |
| BUILD-05 | 运行镜像身份 / Dump / crash log | — | `DEFERRED` |
| BUILD-06 | main 线非 SDK 诊断选项未导入 | PASS | `STATICALLY_CONFIRMED` |

**分布**：`STATICALLY_CONFIRMED` 27、`BUILD_VERIFIED` 1、`RUNTIME_VERIFIED` **0**、`UNVERIFIED` 0、`DEFERRED` 2。[STATICALLY_CONFIRMED]

## 3. 本机已执行的关键命令与输出

```text
$ git diff --name-only b32d185..HEAD -- src/Hook src/Client src/Features src/Utils/Render include resources
(空)

$ git merge-base HEAD origin/main
b32d1856702ef8f1d3e4f92bb17b5a286736932f

$ sha256sum cmake/CPM_0.38.7.cmake
83e5eb71b2bbb8b1f2ad38f1950287a057624e385c238f6087f94cdfc44af9c5 *...

$ cmake -P tools/verify_cpm_bootstrap.cmake
-- CPM v0.38.7: using verified vendored copy (offline-safe)
-- VERIFY_PASS: CPMAddPackage is defined (CPM v0.38.7)

$ cmake --version
cmake version 4.4.4
$ cl / ld / ninja / g++   ->  全部 MISSING
```

`git diff --numstat b32d185..HEAD`（代码/工具，18 文件）：+2228 / −285（含 vendored CPM 1161 行）；其中 `docs/solstice_reconstruction/` 9 份文档与 `tools/verify_full_integration.py` 由 Commit 6 追加。[STATICALLY_CONFIRMED]

## 4. 综合判定

```text
STABLE_RUNTIME_WITH_CURRENT_SDK / BUILD_UNVERIFIED
```

- 达成：稳定运行时零差异、SDK 状态机与 Batch1/2/3 映射完整、MinecraftSim 重试、必要 SDK 防御、CPM 引导修复、6 功能组提交、9 份交付物。[STATICALLY_CONFIRMED]
- 未达成（且未谎称达成）：完整编译链接、DLL 哈希、运行镜像身份、slot 164 语义验证、GL 首帧 RenderEvent 根因、运行期 A/B。[DEFERRED]
- 本整合**不自动合并 main**，新分支推送后等待用户验收。[STATICALLY_CONFIRMED]

## 5. 要跑的命令 → 回传什么 → 自查口径

**要跑的命令**（构建机，仓库根 `G:\hacker`）：

```bat
git fetch origin
git checkout recovery/b32d185-full-integration
python solstice\solstice-src\solstice-master-main\solstice-master-main\tools\verify_full_integration.py --json verify.json
cmake -P solstice\solstice-src\solstice-master-main\solstice-master-main\tools\verify_cpm_bootstrap.cmake
cmake -S solstice\solstice-src\solstice-master-main\solstice-master-main -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

注入运行时另采：`solstice.log`、`crash.log`、`sigreport.json`、游戏 EXE SHA-256 / PE ImageBase / SizeOfImage、生成 DLL SHA-256、`build_info.h`。

**回传什么**：
1. `verify.json`（确认 `failed: []` 与 `overall`）；
2. configure 全文与 build 全文（尤其确认没有 `Unknown CMake command "CPMAddPackage"`）；
3. DLL 路径 + SHA-256 + `build_info.h` 全文；
4. 若能注入：首帧日志（含 `Created detour` / `MH_EnableHook(all)=` / DEBUG 门控文本）、`sigreport.json` 的 non-Ok 统计、是否复现 GL 首帧卡死或 `RenderItemInHandHook` 附近卡死；
5. 若崩溃：minidump 与 `crash.log` 中的 `ExceptionAddress / AccessType / FaultAddress / RIP..RAX` 段。

**自查口径**：
- configure 通过且无 `CPMAddPackage` 报错；
- 编译链接无缺失符号 / 重复定义 / 宏冲突；
- `verify_full_integration.py` 退出码 0 且 `UNVERIFIED` 计数为 0；
- 非 SDK 运行时仍零差异；
- 启动日志出现逐字 DEBUG 门控，且未自动放行；
- 未出现新的初始化期死锁、未出现 slot 164 相关 AV；
- `sigreport.json` 中 non-Ok 项没有被当作有效映射消费。

任一不满足，综合判定必须退回 `BUILD_UNVERIFIED` 或更低，**不得**升级为 `RUNTIME_VERIFIED`；只有上述全部满足且有运行期证据后，才可讨论升级。[DEFERRED]
