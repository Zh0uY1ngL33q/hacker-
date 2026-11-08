# 构建身份与版本追溯 — Batch 3.6.2-E 第一/三工作组

## 1. 运行期构建身份（来自崩溃运行日志）

```text
Build: 73da58852bef82c30fb36e2b2b38d3f72f96f5ce
branch main
source e8175b9f0699
header 2026-11-08T01:01:54Z      (Configure-time UTC = 本地 09:01)
compiled Nov 8 2026 09:03:22       (本地编译时间)
uncommitted 0
```

## 2. 实测源码指纹，逐提交比对

用与 `GenerateBuildInfo.cmake` 完全相同的算法（GLOB_RECURSE `src/*.cpp/*.hpp/*.h` → 逐文件 SHA256 取前 16 位拼接 → 整体 SHA256 取前 12 位）在仓库三个关键提交上复算：

| 提交 | SOLSTICE_SOURCE_FINGERPRINT | 与运行日志 `e8175b9f0699` 比对 |
|---|---|---|
| `669ce2c`（当前 HEAD / origin/main） | `9d824ddf6223` | ❌ 不匹配 |
| `07b3d86`（3.6.2-C） | `45dd7c0eddaf` | ❌ 不匹配 |
| `73da588`（3.6.2-A） | **`e8175b9f0699`** | ✅ **完全一致** |

## 3. 结论：运行二进制是「货真价实的 73da588 构建」

- 运行日志的 `source` 指纹 `e8175b9f0699` 与 `73da588` 源码树的指纹**逐字节一致**，且 `Build` 前缀 `73da588` 也吻合。
- 这说明：**构建机制本身是可靠的**——`BUILD_VERSION` 与 `SOURCE_FINGERPRINT` 都准确反映了构建时 git HEAD=73da588、源码=73da588 树。它不是误归因，而是构建机用了**陈旧 checkout**：构建机在 `09:03` 编译时，其 git HEAD 仍在 `73da588`，从未拉取/重建 `07b3d86`（3.6.2-C）与 `669ce2c`（3.6.2-D）。
- **直接推论**：`11:19` 崩溃的二进制**早于** 3.6.2-C 和 3.6.2-D。因此：
  - `669ce2c` 新增的故障上下文字段（`ExceptionAddress/AccessType/FaultAddress/RIP/...`）在 73da588 中**根本不存在**——这正是历史 `crash.log` 缺这些字段、只有 `0xC0000005` + 栈回溯的原因。
  - `669ce2c` 的 `playUi` 虚调用前置守卫 `isValidVirtualCall` 也不在 73da588 中——73da588 的 `playUi` 是裸虚调用，所以一旦 vtable 落点不可信就直接 AV。

> 这不是「构建身份不可信」，而是「构建机没跟上 main」。修复是**操作性的**：从当前 `main`(=`669ce2c`) 重新构建，下一版才会带上诊断与守卫。

## 4. 构建身份信息生成时机（确认）

- `CMakeLists.txt:193-205`：`generate_build_info` 是一个 `ALL` 自定义目标，每次构建都执行 `cmake -P GenerateBuildInfo.cmake`（**不是仅配置期**）。该脚本用 `cmake -E compare_files` 仅在内容变化时重写 `build_info.h`（避免无谓重编）。
- 因此 `SOLSTICE_BUILD_VERSION`（git HEAD）与 `SOLSTICE_SOURCE_FINGERPRINT`（源码 SHA）都是**构建期**生成，能反映构建当时的真实状态。
- 但 `build_info.h` 仅当内容变化时才被 `file(RENAME)` 更新其 mtime，而 `ExceptionHandler.cpp` 通过 `#include <build_info.h>` 的依赖扫描会在其变化时重编——所以不存在「C++ 用了旧的 build_info.h」的依赖错配（Ninja/make 按头文件 mtime 触发重编）。

## 5. 现有 Build Info 的缺口（本批已修）

`build_info.h` 原有：commit / branch / fingerprint / timestamp / commitmsg / files_changed_count / pc_username / intended_version。**缺失**：

1. **构建配置**（Debug / Release）——崩溃日志无法区分。
2. **两个诊断 CMake 选项**（`SOLSTICE_ALLOW_UNVERIFIED_MAPPINGS` / `SOLSTICE_DISABLE_AURA_DIAGNOSTIC`）——无法从日志判断 Aura 是否被隔离、是否放行未校验映射。
3. **干净的 dirty 标志**（原有 `FILES_CHANGED_COUNT` 只数未提交文件，非显式 0/1）。

## 6. 本批实施的最小构建身份修复（见 `minimal-fix-summary.md` / `final-decision.md`）

- `CMakeLists.txt`：向 `generate_build_info` 目标转发 `$<CONFIG>`、两个选项值。
- `GenerateBuildInfo.cmake`：追加 `SOLSTICE_BUILD_CONFIG` / `SOLSTICE_BUILD_ALLOW_UNVERIFIED_MAPPINGS` / `SOLSTICE_BUILD_DISABLE_AURA_DIAGNOSTIC` / `SOLSTICE_SOURCE_DIRTY`。
- `ExceptionHandler.cpp`：崩溃日志打印上述字段（全部 `#ifndef/#ifdef` 保护，旧 `build_info.h` 仍可编译）。
- **这些追加字段不改变 `SOLSTICE_SOURCE_FINGERPRINT`**（该指纹只哈希 `src/`），且完全向后兼容。

## 7. 给构建机的操作结论

1. 从当前 `main`（`669ce2c`）重新配置+构建，使二进制带上 3.6.2-D 的诊断与守卫、以及本批的身份字段。
2. 构建前确认 `CMakeCache.txt` 确实含两个选项与构建配置（见 `playui-callsite-and-abi.md` 第十一节命令）。
3. 构建后记录 DLL SHA-256 与运行期 `Minecraft.Windows.exe` SHA-256（见 `runtime-image-identity.md`）。
