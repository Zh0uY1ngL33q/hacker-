# BUILD_BASELINE — 构建基线与环境证据

> 本文件的每个数字都来自命令输出或文件内容，来源写在各节末尾。
> 更新：2026-11-01（工程验证阶段）

## 1. 工程声明（读自 CMakeLists.txt）

| 项目 | 值 | 出处 |
|---|---|---|
| 最低 CMake | 3.28 | `cmake_minimum_required(VERSION 3.28)` |
| project | `Solstice` | `project(Solstice)` |
| C++ 标准 | **C++26** | `set(CMAKE_CXX_STANDARD 26)`（行 179） |
| 产物 | `Solstice`（SHARED / DLL） | `add_library(Solstice SHARED ...)`（行 284） |
| 生成器 | Ninja | 构建机缓存 `CMAKE_GENERATOR:INTERNAL=Ninja` |
| 额外工具 1 | MSYS2 `ld`（`C:/msys64/mingw64/bin`） | `find_program(LD_COMMAND ld)`（行 28-31），`ld -r -b binary` 嵌 `resources/`（行 263） |
| 额外工具 2 | Windows SDK `fxc.exe` | 行 147/158，HLSL → `.inc` |
| 依赖 | 配置期 `file(DOWNLOAD)` 到 `<build>/_deps-local` | `cmake/Dependencies.cmake` |

## 2. 本机（分析机）环境：无构建能力——已穷举确认

| 检查 | 命令 | 结果 |
|---|---|---|
| VS 实例 | `vswhere -all -products *` | **无输出**（磁盘上 `C:\Program Files\Microsoft Visual Studio\18\` 是空目录） |
| 编译器/构建工具 | `find "C:\Program Files" "C:\Program Files (x86)" -maxdepth 7 -iname cl.exe / nmake.exe / ninja.exe / rc.exe` | **0 命中** |
| 其他盘 | `find /d /e /g -maxdepth 3 -iname cl.exe / ninja.exe / vsvarsall.bat` | **0 命中** |
| Git Bash 自带 mingw64 | `ls /mingw64/bin`（90 个文件） | 只有 git 工具，**无 gcc/g++/ld** |
| Windows SDK | `ls "C:\Program Files (x86)\Windows Kits\10\bin"` | 不存在 |
| CMake | `where cmake` | `E:\PY\Scripts\cmake.exe`（4.4.4，Python 包） |
| `G:\bypass\Windows SDK` | `ls` | 只有 `Catalogs/ Debuggers/ Licenses/ windbg.exe.lnk` —— WinDbg 包，**不是**编译 SDK |

结论：**本机不能编译**。这不是“没找到”，是穷举搜索后的 0 命中。

## 3. 构建机工具链（来自备份里的 CMake 缓存，真实证据）

`_backup/solstice-before-adapt-20261025-191721.zip` 内含上次配置留下的
`out/build/x64-Release/CMakeCache.txt` 与 `CMakeFiles/4.2.3-msvc3/CMakeCXXCompiler.cmake`：

```
CMAKE_CXX_COMPILER  = H:/vs/VC/Tools/MSVC/14.51.36231/bin/Hostx64/x64/cl.exe
CMAKE_LINKER        = H:/vs/VC/Tools/MSVC/14.51.36231/bin/Hostx64/x64/link.exe
CMAKE_RC_COMPILER   = H:/Windows Kits/10/bin/10.0.26100.0/x64/rc.exe
CMAKE_MAKE_PROGRAM  = H:\VS\COMMON7\IDE\COMMONEXTENSIONS\MICROSOFT\CMAKE\Ninja\ninja.exe
CMAKE_GENERATOR     = Ninja
CMAKE_BUILD_TYPE    = RelWithDebInfo          <-- 注意不是 Release
CMAKE_CXX_COMPILER_VERSION = 19.51.36246.0
CMAKE_HOST_SYSTEM   = Windows-10.0.26200
CMake 版本（目录名 4.2.3-msvc3）= 4.2.3
```

也就是说：构建机是另一台机器（工具链在 `H:\vs`），**MSVC 19.51.36246.0（工具集 14.51.36231）
+ Windows SDK 10.0.26100.0 + Ninja + CMake 4.2.3，构建类型 RelWithDebInfo**。

这解释了 `CMAKE_CXX_STANDARD 26` 为什么能编过：19.51 是当前最新工具集。
`BUILD.md` 里写的 `-DCMAKE_BUILD_TYPE=Release` 与实际缓存不一致，以 **RelWithDebInfo** 为准时
才和上次成功的构建同构。

## 3.1 构建机实测（2026-11-01 21:13，`verify-build.ps1` v1 的 env.log）

host `MORELINK` / Windows 11 `10.0.22631.0`：

| 项 | 实际值 | 判定 |
|---|---|---|
| cmake | `C:\Program Files\CMake\bin\cmake.exe`，4.4.4，在 PATH 上 | ✅ |
| Visual Studio | `C:\Program Files\Microsoft Visual Studio\18\Community`（vswhere 能查到） | ✅ 已安装 |
| `cl.exe` | 不在 PATH | ⚠️ 普通 PowerShell 未加载 MSVC 环境 |
| `rc.exe` | 不在 PATH | ⚠️ Windows SDK 未加载 |
| `ninja` | 不在 PATH | ⚠️ |
| configure 结果 | `exit 1`：`unable to find a build program corresponding to "Ninja"` + 编译器未设置 | 环境问题，**未进入源码编译** |

**结论**：构建机上有 VS 18 Community，但普通 PowerShell 会话不会自动 `vcvars`；
Ninja 也不是 PATH 工具。这不是源码问题，也不是“没装 VS”。

修正后的脚本因此自建环境：`vswhere -requires ...VC.Tools.x86.x64` 定位 VS →
`call VC\Auxiliary\Build\vcvars64.bat` 并导入 `set` 输出 → 从
`<VS>\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe` 取 Ninja，
与 §3 里历史缓存记录的构建方式（VS 自带 Ninja）一致。

## 4. 干净副本 + 配置尝试（本机执行，结果留档）

用 `git archive HEAD` 导出提交树到独立目录（932 文件，23 MB），**不触碰工作区**：

```
git archive HEAD | tar -x -C _buildtest/clean
```

然后在该副本上配置两次，日志见 `docs/migration/logs/`：

| 尝试 | 命令 | 结果 | 分类 |
|---|---|---|---|
| A | `cmake -S <clean> -B <b> -G Ninja` | `CMake was unable to find a build program corresponding to "Ninja". CMAKE_MAKE_PROGRAM is not set.` + `CMAKE_C/CXX_COMPILER not set` ，`exit 1` | **环境问题** |
| B | `cmake -S <clean> -B <b>`（默认 NMake） | `'nmake' '-?' failed with: no such file or directory` + 编译器未设置，`exit 1` | **环境问题** |

两次都停在工具链探测阶段，**没有进入源码编译**，因此这两份日志不能用来判断源码是否可编译。

## 5. 依赖解析（本机唯一跑通的构建环节）

`cmake -P cmake/Dependencies.cmake` 是工程自己的“无编译器可运行”入口（文件头注释明确写了这个用途）。
日志：`docs/migration/logs/deps-resolve-partial.log`。

| 依赖 | 下载 | 解压 | 二次运行命中缓存 |
|---|---|---|---|
| spdlog 1.9.2 | ✅ | ✅ | ✅ `cached at ...` |
| magic_enum 0.9.5 | ✅ | ✅ | ✅ |
| glm 1.0.1 | ✅ | ✅ | ✅ |
| nlohmann_json 3.11.3 | 开始下载 | 未完成 | — |
| DirectXTK | 未开始 | — | — |
| cryptopp 8.7.0 + 包装 CMakeLists | 未开始 | — | — |

后三项**未验证**：按“不要在本机安装/准备环境”的要求，下载到第 4 项就中止了。
已验证的部分说明 `codeload.github.com` 可达、`file(DOWNLOAD)`+解压逻辑正确、缓存判定幂等。

## 6. 复现构建的标准步骤（在构建机上）

```bat
:: 1) 拉最新提交（HEAD 应 >= a93ff55）
git -C G:\hacker pull

:: 2) 双击 tools\build\verify-build.cmd（推荐）
::    它用 -ExecutionPolicy Bypass 只对本次进程生效，结束后一定 pause

:: 3) 或手工
cmake -S solstice\solstice-src\solstice-master-main\solstice-master-main ^
      -B out\build\x64-Release -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build out\build\x64-Release -- -k 0
```

**为什么必须有那个 `.cmd` 包装**：Windows 默认执行策略是 `Restricted`，直接双击或直接执行 `.ps1` 会报
「在此系统上禁止运行脚本」（本机实测同一报错，见 `TEST_REPORT.md §11`），而且控制台窗口会随脚本
结束立刻关闭，报错一闪而过。`.cmd` 做三件事：用 `-ExecutionPolicy Bypass` 绕过（**只影响本次进程，
不改系统设置**）、把脚本参数原样透传、结束时 `pause`。

脚本会自动做这些（不需要手工传参）：

* 从自身位置向上找 `solstice\solstice-src\...\CMakeLists.txt`（其中含 `project(Solstice)`），
  最多 6 层，找不到再在自身目录下浅搜 5 层，最后才报错并要求 `-Source`；
* 向上最多 8 层找 `.git`，把 HEAD 和未提交改动记进 `env.log`；
* 复用已有的 `out\build\x64-Release\_deps-local` 作为依赖缓存，避免重新下载；
* 任何早期失败都写 `verify-build-error.log` 并停住窗口。

`verify-build.ps1` 会产出 `env.log / configure.log / build.log / summary.txt`，
外加 `vcvars.log`（`where cl` / `where rc` / cl 版本）与 `run.log`（脚本自身输出）；
其中 `summary.txt` 已按 `C####` / `LNK####` / `MSB####` 归并错误码并列出首个出现位置和失败文件清单。
**回传这 4 个文件即可定位**，不需要整份滚屏日志。

前置（一次即可，本机不执行）：VS2022 含「使用 C++ 的桌面开发」、Ninja、MSYS2 binutils、
Windows SDK（`fxc.exe`）。这些都写在 `BUILD.md`，本轮未做任何安装。

## 7. “已验证”的口径（防止把静态分析当运行时）

| 说法 | 含义 |
|---|---|
| **已验证（静态）** | 在内存镜像上可复现、可反汇编、可核对字节 |
| **已验证（构建）** | 在 MSVC 上编译通过。**本阶段仍为 0 项** |
| **已验证（运行时）** | 注入后日志/画面确认。**本阶段仍为 0 项** |
| **UNKNOWN** | 缺样本或缺运行时证据 |

## 8. 已知构建风险（沿用上一阶段，未有新证据推翻）

| 风险 | 依据 |
|---|---|
| 中文 commit message 触发 `C2001` | 历史问题；`GenerateBuildInfo.cmake` 已剔除非 ASCII/换行 |
| `build_info.h` 宏缺失 | `ExceptionHandler.cpp` 有 `#ifndef` 兜底，缺宏输出 `unknown` |
| C++26 需最新 MSVC | 构建机 19.51 满足；旧 VS 会在标准设置处失败 |
| 首次配置需网络 | 依赖走 `codeload.github.com`；离线机器需先 `tools/setup-deps.ps1` |
| `rc.exe` / `fxc.exe` 不在 PATH | 必须在 VS 开发者命令行或已配置 SDK 的环境里构建 |
