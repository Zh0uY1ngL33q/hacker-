# 构建产物溯源 — Batch 3.6.2-D 第一工作组

## 1. 现象：运行期 DLL 显示 `73da588`，但最新提交是 `07b3d86`

运行中的 Solstice.dll 在 `build_info.h` 里上报 `SOLSTICE_BUILD_VERSION = "73da588"`，
而仓库 HEAD 已经是 `07b3d86`。第一反应容易误判为「把运行结果归因到了错误的提交」。
结论：**这不是归因错误，而是 `SOLSTICE_BUILD_VERSION` 字符串在 CMake 配置期被冻结导致的滞后。**

## 2. 根因：`SOLSTICE_BUILD_VERSION` 在 CMake 配置期烘焙，增量重编不刷新

`GenerateBuildInfo.cmake` 的关键逻辑：

- 第 48 行：`solstice_git(SOLSTICE_BUILD_VERSION "unknown" rev-parse HEAD)`
  —— 在 **CMake configure 阶段**执行 `git rev-parse HEAD`，把当时的 HEAD 写死进 `build_info.h`。
- 第 159–170 行：用 `cmake -E compare_files` 比较新旧 `build_info.h`；只有文件差异时才重命名覆盖。
- 增量重编（只跑 `msbuild`/`ninja`，不重新 `cmake`）**不会**触发 `GenerateBuildInfo.cmake`，
  因此 `SOLSTICE_BUILD_VERSION` 字符串保持上次配置时的值。

## 3. 真正的二进制钉：用 `SOLSTICE_SOURCE_FINGERPRINT`，不靠提交号

第 75–97 行：

- `file(GLOB_RECURSE ... src/*.cpp *.hpp *.h)`，对每个源文件做 SHA256，拼接后整体再 SHA256，
  得到 `SOLSTICE_SOURCE_FINGERPRINT`（取前 12 位）。
- 这是**基于被编译源码本身**的指纹，与 git HEAD 字符串无关，能可靠地把二进制钉到具体源码状态。

## 4. 提交血缘：73da588 是 07b3d86 的祖先

`git log` 确认：

```
07b3d86 (HEAD -> feat/gl-render-hang-root-cause, origin/main, main)  diag(gl): 3.6.2-C ...
73da588 (origin/feat/aura-isolation-hang-diagnosis)                 diag(aura): 3.6.2-A ...
```

即：DLL 在 `73da588` 时完成**配置**，之后源码推进到 `07b3d86` 并被重编/链接，但**未重新配置**，
于是 `SOLSTICE_BUILD_VERSION` 字符串停留在 `73da588`，而实际编译进二进制的代码是 `07b3d86` 的源码
（外加本批 3.6.2-D 尚未提交的两次编辑——崩溃日志来自这两次编辑之前的构建）。

> 结论：不要把运行期结果错误归因到 `73da588`；二进制实际对应 `07b3d86` 源码。
> 滞后的是版本字符串，不是代码。

## 5. 两个诊断宏是否参与构建

| 宏 | 定义位置 | 消费位置 | 默认 | 参与条件 |
|---|---|---|---|---|
| `SOLSTICE_ALLOW_UNVERIFIED_MAPPINGS` | `CMakeLists.txt:34-37` → `add_compile_definitions` | `src/Solstice.cpp:295`（DEBUG 人工放行提示） | `OFF` | 仅当构建机传 `-DSOLSTICE_ALLOW_UNVERIFIED_MAPPINGS=ON` |
| `SOLSTICE_DISABLE_AURA_DIAGNOSTIC` | `CMakeLists.txt:46-49` → `add_compile_definitions` | `src/Features/Modules/ModuleManager.cpp:149`（`#ifndef` 包裹 `make_shared<Aura>()`） | `OFF` | 仅当构建机传 `-DSOLSTICE_DISABLE_AURA_DIAGNOSTIC=ON` |

两者都是 `option(... OFF)`，**默认不参与**。是否参与完全取决于构建机的 cmake 命令行，
从崩溃日志本身无法反推。本批崩溃栈 `initThread → Solstice::init → playUi` 说明映射校验门
（默认拒绝）已放行运行进入 `init`，与 `SOLSTICE_ALLOW_UNVERIFIED_MAPPINGS=OFF`（默认拒绝）一致——
因为该宏只影响「拒绝 / 人工放行」分支，运行既然走到了 `init→playUi` 就说明没在拒绝分支退出。

> 确认方法：检查构建机的 `CMakeCache.txt`（`grep` 两个宏名），或核对构建日志里 CMake 的
> `Build version:` / 选项状态输出。当前会话无法读取构建机缓存，故只能确认「宏已注册、默认 OFF、消费点存在」，
> 不能断言本次崩溃构建开了哪个宏。

## 6. 建议

1. **发版前重新配置 CMake**（或在 CI 里让 `build_info.h` 依赖 git 状态），避免版本字符串滞后。
2. 用 `SOLSTICE_SOURCE_FINGERPRINT` 作为二进制钉，比提交号字符串更可靠。
3. 构建机保留 `CMakeCache.txt` 归档，便于事后溯源宏开关与版本。
