# 构建系统修复 — `CPMAddPackage` 未定义

## 1. 故障与根因

```text
CMake Error at CMakeLists.txt:63 (CPMAddPackage):
Unknown CMake command "CPMAddPackage".
```

b32d185 的 `cmake/CPM.cmake` 在 configure 阶段用 `file(DOWNLOAD ...)` 拉取 CPM v0.38.7。离线 / 代理失败时 `file(DOWNLOAD)` 可能写出一个**空文件**而不报错；后续 `include()` 一个空脚本不会定义 `CPMAddPackage`，于是 `CMakeLists.txt:63` 调用时报错。[STATICALLY_CONFIRMED]

核查还发现：`cmake/CPM.cmake` 与 `cmake/Dependencies.cmake` 在 `b32d185` 和 `origin/main` 的对象哈希完全相同 —— **main 线并不存在另一套「已修好」的 CPM 初始化方式**；main 的 CMakeLists diff 只有诊断选项与 Build Info 改动。因此正确做法不是复制 main 的 CMakeLists，而是把依赖引导本身做成离线确定性的。[STATICALLY_CONFIRMED]

## 2. 修复内容（Commit 1 / `9427362`）

| 文件 | 改动 | 等级 |
| --- | --- | --- |
| `cmake/CPM_0.38.7.cmake` | 新增 vendored 官方 CPM v0.38.7 本体（1161 行，含 `function(CPMAddPackage)`）；是 CMake 代码，不进入 DLL | `STATICALLY_CONFIRMED` |
| `cmake/CPM.cmake` | 重写引导器：优先 include 本地副本并校验 SHA-256；下载逻辑降级为回退；include 后检查 `COMMAND CPMAddPackage`，缺失即 `FATAL_ERROR` | `STATICALLY_CONFIRMED` |
| `tools/verify_cpm_bootstrap.cmake` | 新增脚本模式验收：include 引导器后断言 `CPMAddPackage` 已定义 | `BUILD_VERIFIED` |

关键片段：

```cmake
set(CPM_DOWNLOAD_VERSION 0.38.7)
set(CPM_HASH_SUM "83e5eb71b2bbb8b1f2ad38f1950287a057624e385c238f6087f94cdfc44af9c5")
set(CPM_VENDORED_LOCATION "${CMAKE_CURRENT_LIST_DIR}/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
file(SHA256 "${CPM_VENDORED_LOCATION}" _cpm_vendored_hash)
if (NOT _cpm_vendored_hash STREQUAL CPM_HASH_SUM)
    message(FATAL_ERROR "Vendored CPM.cmake hash mismatch.")
endif ()
include("${CPM_EFFECTIVE_LOCATION}")
if (NOT COMMAND CPMAddPackage)
    message(FATAL_ERROR "... CPMAddPackage is not defined.")
endif ()
```

- 固定版本 + 固定哈希消除了「下载到空文件却静默通过」这一类失败。[STATICALLY_CONFIRMED]
- `COMMAND` guard 把「脚本被 include 但函数不存在」变成 configure 期明确报错，而不是延后到第 63 行报 `Unknown CMake command`。[STATICALLY_CONFIRMED]

## 3. 验证结果

```text
$ sha256sum cmake/CPM_0.38.7.cmake
83e5eb71b2bbb8b1f2ad38f1950287a057624e385c238f6087f94cdfc44af9c5 *...

$ cmake -P tools/verify_cpm_bootstrap.cmake
-- CPM v0.38.7: using verified vendored copy (offline-safe)
-- VERIFY_PASS: CPMAddPackage is defined (CPM v0.38.7)
```

`BUILD_VERIFIED` 仅覆盖 **CMake script mode 的引导器验证**。[BUILD_VERIFIED]

## 4. Build Info 构建图边（分流结论）

3.6.2-E.1 P1-1 添加了 `add_dependencies(Solstice generate_build_info)`，声称保证 `build_info.h` 先于 `Solstice` 编译。核查 b32d185：[STATICALLY_CONFIRMED]

```cmake
# CMakeLists.txt:4
project(Solstice)
# CMakeLists.txt:285
add_dependencies(${PROJECT_NAME} generate_build_info SolsticeShaders)
```

`PROJECT_NAME` 就是 `Solstice`，所以这条构建图边在稳定基线里**已经存在**；`b879cb1` 只是把 `${PROJECT_NAME}` 改写成字面量 `Solstice`，语义等价。2429e6e 额外再加一条同名依赖属于冗余。因此本整合不移植这些 hunk，CMakeLists 保持逐字不变。[STATICALLY_CONFIRMED]

`GenerateBuildInfo.cmake` 的 E.1 差分（`SOLSTICE_SOURCE_DIRTY` → `SOLSTICE_WORKTREE_DIRTY`、两个诊断选项恒记 0/1、指纹哈希范围注释）针对的是 main 线较新版本的脚本；b32d185 的脚本里根本没有这两个诊断选项，也不输出 dirty 宏，`ExceptionHandler::makeCrashLog()` 里也没有对应读取。移植会产生指向不存在选项的伪溯源信息，故不移植。[DEFERRED]

## 5. 本机无法完成的构建验证

| 项 | 状态 | 说明 |
| --- | --- | --- |
| `cmake --version` | 4.4.4 | 本机可用 |
| `cl` / `ld` / `ninja` / `g++` | 全部 MISSING | 无 MSVC / 链接器 / 生成器 |
| `CMakeLists.txt:30` | 强制检查 `ld` | 即使有 CMake 也无法完整 configure |
| 完整 CMake configure | `DEFERRED` | 需在构建机执行 |
| Release x64 编译与 DLL link | `DEFERRED` | 未编译，不伪造成功 |
| DLL SHA-256 / Build provenance | `DEFERRED` | 无产物 |
| 运行镜像 EXE SHA / PE ImageBase / SizeOfImage | `DEFERRED` | 本分支未采集 |

**本整合不声明 `BUILD_VERIFIED`（完整构建）或任何 DLL 哈希。** 只在构建机完成 configure + Release x64 + 注入运行后，才能把综合判定从 `BUILD_UNVERIFIED` 升级。[DEFERRED]

## 6. 构建机命令

```bat
cmake -S solstice\solstice-src\solstice-master-main\solstice-master-main -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

**回传**：configure 全文（确认无 `Unknown CMake command "CPMAddPackage"`）、build 全文、DLL 路径与 SHA-256、`build_info.h` 内容（核对 `SOLSTICE_BUILD_VERSION` / `SOLSTICE_SOURCE_FINGERPRINT_SHORT` / `SOLSTICE_BUILD_BRANCH`）。
**自查口径**：configure 通过、编译链接无缺失符号/重复定义、DLL 成功生成且哈希可复算；任一不满足则维持 `BUILD_UNVERIFIED`。[DEFERRED]
