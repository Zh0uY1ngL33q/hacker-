# BUILD_BASELINE — 构建基线

## 1. 工程声明（读自 CMakeLists.txt）

| 项目 | 值 | 出处 |
|---|---|---|
| 最低 CMake | 3.28 | `cmake_minimum_required(VERSION 3.28)` |
| project | `Solstice` | `project(Solstice)` |
| C++ 标准 | **C++26** | `set(CMAKE_CXX_STANDARD 26)`（行 179） |
| 产物 | `Solstice`（SHARED，即 DLL） | `add_library(Solstice SHARED ${sourceFiles} ${resource_files})`（行 284） |
| 编译器 | MSVC v143（VS2022）“使用 C++ 的桌面开发” | `BUILD.md §1`；`CMAKE_CXX_STANDARD 26` 只有 MSVC 19.4x 能开 |
| 生成器 | Ninja | `BUILD.md §1` |
| 额外工具 1 | MSYS2 `ld`（`C:/msys64/mingw64/bin/ld`） | `find_program(LD_COMMAND ld ...)`（行 28-31）；用于把 `resources/` 用 `ld -r -b binary` 嵌进 DLL（行 263） |
| 额外工具 2 | Windows SDK `fxc.exe` | 行 147/158，把 `resources/shaders/*.hlsl` 编成 `.inc` |
| 依赖获取 | 配置期 `file(DOWNLOAD)` 到 `<build>/_deps-local` | `cmake/Dependencies.cmake`；首次配置需要网络，之后离线可用 |

## 2. 构建命令（与 `BUILD.md` 一致）

```bat
cmake -S . -B out\build\x64-Release -G Ninja ^
      -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
cmake --build out\build\x64-Release
```

依赖已预置时加 `-DSOLSTICE_DEPS_DIR=<dir>` 可完全离线（`Dependencies.cmake` 的第一优先级）。

## 3. 本机环境实测（2026-11-01）

| 检查 | 命令 | 结果 |
|---|---|---|
| VS 安装 | `vswhere -all -products *` | **无输出** → 没有任何 VS 实例 |
| MSVC | `where cl` | 未找到 |
| clang / g++ | `where clang/clang++/g++` | 未找到 |
| Ninja | `where ninja` | 未找到 |
| MSYS2 ld | `ls C:/msys64/mingw64/bin/ld.exe` | 不存在 |
| CMake | `where cmake` | `E:\PY\Scripts\cmake.exe`（4.4.4） |
| 结论 | — | **本机无法编译** |

> 这是阶段五的硬阻塞。用户的构建机在另一台机器（`C:\work\hacker-git\...`），
> 由用户执行 `cmake --build`。本机只做离线逆向与静态核验。

## 4. 因此本报告中的“已验证”指什么

| 说法 | 含义 |
|---|---|
| **已验证（静态）** | 在内存镜像上可复现、可反汇编、可定位字节；有命令输出为证 |
| **已验证（构建）** | 在 MSVC 上编译通过。**本次没有一项属于此类** |
| **UNKNOWN** | 无法从现有样本确认，或需要运行时才能确认 |

## 5. 构建风险的已知来源

| 风险 | 依据 |
|---|---|
| 中文 commit message 触发 `C2001` | 历史问题，`GenerateBuildInfo.cmake` 已剔除非 ASCII/换行（仍在树内） |
| `build_info.h` 宏缺失 | `ExceptionHandler.cpp` 已加 `#ifndef` 兜底，缺宏时输出 `unknown` |
| C++26 特性可用性 | 只有最新 MSVC 支持；旧版 VS 会直接在 `set(CMAKE_CXX_STANDARD 26)` 处失败 |
| 依赖下载失败 | 首次配置需 `codeload.github.com` 可达；离线机器必须先跑 `tools/setup-deps.ps1` |
