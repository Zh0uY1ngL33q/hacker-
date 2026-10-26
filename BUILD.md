# 拉取与构建

目标：在一台**没有登录 git、没有配置过任何依赖**的 Windows 机器上，从零把
`Solstice.dll` 编出来。

仓库：<https://github.com/Zh0uY1ngL33q/hacker-> （公开仓库，不需要账号）

---

## 0. 结论先说

不需要 git 账号，也不需要 git 本身：

- **拉代码**：下载 GitHub 的 zip，解压即可。
- **拉依赖**：CMake 自己在配置阶段用 HTTPS 下载（`file(DOWNLOAD)`），缓存在
  `<build>/_deps-local/`，不需要 git、不需要 vcpkg、不需要手工摆目录。

---

## 1. 需要装的东西

| 组件 | 作用 | 备注 |
| --- | --- | --- |
| **Visual Studio 2022** | MSVC v143 编译器 | 必须勾选「使用 C++ 的桌面开发」；版本越新越好（工程用了 C++26 标准） |
| **CMake ≥ 3.28** | 配置/生成 | 4.x 也可以，工程已做兼容 |
| **Ninja** | 生成器 | 单个 exe，解压到 PATH 即可 |
| **MSYS2 的 binutils** | 提供 `ld.exe`，用于把 `resources/` 嵌进 DLL | 装到默认的 `C:\msys64` 就不用改 PATH |
| **Windows SDK** | 提供 `fxc.exe`（HLSL 编译器） | 随 VS 安装，在 VS 开发者命令行里才在 PATH 上 |

一条 `winget` 装完（已装的会跳过）：

```powershell
winget install Microsoft.VisualStudio.2022.Community
winget install Kitware.CMake
winget install Ninja-build.Ninja
winget install MSYS2.MSYS2
```

然后 MSYS2 的 binutils 需要单独装一次（在 PowerShell 里跑，不是 MSYS2 窗口）：

```powershell
C:\msys64\usr\bin\bash.exe -lc "pacman -S --noconfirm mingw-w64-x86_64-binutils"
```

> **不要**把 `C:\msys64\mingw64\bin` 加进 PATH。CMakeLists 自己会去
> `C:/msys64/mingw64/bin` 找 `ld`；把整个目录塞进 PATH 反而可能让 CMake 误选
> MinGW 的 gcc 当编译器。装到别处的话用 `-DLD_COMMAND=<路径>\ld.exe` 指过去。

### 装完自检

打开 **「x64 Native Tools Command Prompt for VS 2022」**（开始菜单能搜到），
逐条跑：

```bat
cl
cmake --version
ninja --version
fxc
ld --version
```

五个都要有输出。`cl` / `fxc` 只在**开发者命令行**里可用，普通 PowerShell 里找不到。

---

## 2. 把代码弄到机器上

### 方式 A：下载 zip（不需要 git，推荐）

浏览器打开，或 PowerShell 直接下：

```powershell
Invoke-WebRequest -Uri "https://github.com/Zh0uY1ngL33q/hacker-/archive/refs/heads/main.zip" -OutFile "$env:USERPROFILE\Downloads\hacker.zip"
Expand-Archive "$env:USERPROFILE\Downloads\hacker.zip" -DestinationPath "C:\work" -Force
```

解压出来的**根目录名是 `hacker--main`**（中间是两个连字符），不是 `hacker-`。

### 方式 B：装了 git 的话

公开仓库匿名 clone 即可，**不需要登录**：

```powershell
git clone https://github.com/Zh0uY1ngL33q/hacker-.git C:\work\hacker
```

以后更新用 `git pull`。注意 HTTPS 地址匿名可用，但如果本机存过别的 GitHub 凭据、
弹窗要求登录，直接选「Cancel」也能继续——公开仓库不需要凭据。

### 方式 C：已经有旧副本

`git pull` 即可。如果是旧 zip 解压的、没有 `.git`，直接重新下载覆盖。

> 仓库根目录的 `solstice-src.zip` 是 2026-09-19 的旧快照，**不要拿它构建**。

---

## 3. 进入项目目录

仓库里的工程是**嵌套**的，CMakeLists.txt 在第四层：

```
<解压目录>
└── solstice
    └── solstice-src
        └── solstice-master-main
            └── solstice-master-main   <-- 这里，有 CMakeLists.txt
```

方式 A 解压后：

```powershell
cd C:\work\hacker--main\solstice\solstice-src\solstice-master-main\solstice-master-main
```

确认一下：

```powershell
Test-Path .\CMakeLists.txt          # 必须是 True
Test-Path .\include\libhat          # 必须是 True
```

---

## 4. 配置

在**「x64 Native Tools Command Prompt for VS 2022」**里（不是普通 PowerShell）：

```bat
cmake -S . -B out/build/x64-Release -G Ninja -DCMAKE_BUILD_TYPE=Release
```

首次配置会下载约 20 MB 依赖，屏幕上会看到：

```
-- dependency spdlog: downloading https://codeload.github.com/gabime/spdlog/...
-- dependency spdlog: extracting into .../out/build/x64-Release/_deps-local
-- dependency magic_enum: ...
...
-- dependency cryptopp: installing the CMake wrapper into .../cryptopp-CRYPTOPP_8_7_0
-- linking against cryptopp-static
-- Configuring done
```

最后一行必须是 **`Configuring done`**。

关于那条 `installing the CMake wrapper`：`cryptopp-cmake` 这个仓库本身不含
Crypto++ 源码（只有 15 KB 的 CMakeLists），所以 CMake 会下载 Crypto++ 8.7.0
源码再把包装层放进去，这是正常现象。

### 常用变体

```bat
:: 依赖下载慢/被墙，先在本机预置一份再指过去（不需要联网也行）
powershell -ExecutionPolicy Bypass -File tools\setup-deps.ps1 -Dest D:\deps
cmake -S . -B out/build/x64-Release -G Ninja -DCMAKE_BUILD_TYPE=Release -DSOLSTICE_DEPS_DIR=D:/deps

:: MSYS2 装在别处
cmake -S . -B out/build/x64-Release -G Ninja -DCMAKE_BUILD_TYPE=Release -DLD_COMMAND=D:\msys64\mingw64\bin\ld.exe

:: 想调试就编 Debug（会生成 solstice.log）
cmake -S . -B out/build/x64-Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug
```

---

## 5. 编译

```bat
cmake --build out/build/x64-Release
```

Release 开了 `/O2 /GL /LTCG /MT`，全量首次编译会比较久（几分钟）。
后续增量编译只重编改动的文件。

> 自己的 `src/` 用 unity build（把多个 .cpp 合成一个翻译单元）；Crypto++ 例外，
> 它是逐文件编译的（见 §7）。所以第一次编译时 cryptopp 那一百多个 `.obj` 是正常的。

想并行快一点可以加 `--parallel`：

```bat
cmake --build out/build/x64-Release --parallel
```

---

## 6. 产物

```
out/build/x64-Release/Solstice.dll      <-- 要注入的就是这个
```

确认一下：

```powershell
Get-Item out\build\x64-Release\Solstice.dll | Select-Object Name, Length, LastWriteTime
```

---

## 7. 报错对照

| 报错 | 原因 / 解决 |
| --- | --- |
| `The ld command was not found.` | 没装 MSYS2 的 binutils，或不在默认 `C:\msys64`。装完后用 `-DLD_COMMAND=` 指路 |
| `'fxc.exe' 不是内部或外部命令` | 用了普通 PowerShell。改用 **x64 Native Tools Command Prompt** |
| `CMake Error: could not find CMAKE_CXX_COMPILER` | 同上，开发者命令行里才有 `cl` |
| `Could not download <dep>` | 网络问题。用 `tools/setup-deps.ps1` 预置，或挂代理后重新配置 |
| `Compatibility with CMake < 3.5 has been removed` | 不该出现（工程已用 `CMAKE_POLICY_VERSION_MINIMUM` 兜住）。若出现请贴完整日志 |
| `The current CMakeCache.txt is different...` | 你复用了别人的 build 目录。删掉 `out\` 重新配置 |
| `CXX_STANDARD 26 is not supported` | VS 2022 版本太旧，更新到最新 |
| `error C1001: internal compiler error` | MSVC 版本与 `/GL` 组合的已知问题，先更新 VS 再试 |
| `c1xx: fatal error C1083: 无法打开源文件 "CRYPTOPP_DATA_DIR=..."` | Crypto++ 的 CMake 包装层把宏定义又当编译选项传了一遍，`cl` 就把 `CRYPTOPP_DATA_DIR=...` 当成源文件。**已由 `cmake/Dependencies.cmake` 自动打补丁**；若还出现，删掉 `out\build\<配置>\_deps-local` 重新配置即可 |
| `error C2371: "CryptoPP::Block": 重定义` / `error C2084: 函数已有主体` | Crypto++ 被 unity build 合并了。**已对 cryptopp 关掉 unity**；若还出现，删掉 `out\` 重新配置 |
| `error C2653: "stdext": 不是类或命名空间名称` | 新版 MSVC 删掉了非标准的 `stdext::make_unchecked_array_iterator` / `make_checked_array_iterator`（`zdeflate.cpp`、`integer.cpp`）。**已自动打补丁**改回裸指针；如果上游还有别的地方用到，配置阶段会直接报出文件名并停下，而不是编到一半才炸 |

### 关于「编译器太新」

MSVC 14.4x / 14.5x（VS 2022 17.1x、VS 18）**不需要降级**。上面那三类报错一条都不是
「编译器不兼容」，而是两个依赖的旧代码/旧 CMake 脚本在新环境里暴露出来的问题：

1. Crypto++ 的 CMake 包装层（`noloader/cryptopp-cmake`）自 2017 年起就有一行
   `add_compile_options(${CRYPTOPP_COMPILE_DEFINITIONS})`，把宏定义丢掉了 `-D` 前缀；
2. `zdeflate.cpp` / `integer.cpp` 里用了微软早就标废弃、如今已删除的非标准迭代器辅助函数
   （`stdext::make_unchecked_array_iterator`、`make_checked_array_iterator`）；
3. 我们自己的 `CMAKE_UNITY_BUILD TRUE` 恰好把 Crypto++ 里那些同名匿名命名空间函数
   合并到了一起。

前两条是上游代码的问题，由 `cmake/Dependencies.cmake` 在每次配置时按文本模式自动打补丁
（幂等，重复配置不会重复改）；第三条在 `CMakeLists.txt` 里只对 cryptopp 的三个 target
关掉 unity。换老编译器只会绕开第 3 条里的表象、并不能解决，而且 C++26 的新标准反倒编不过。

配置阶段失败是**快**的（几秒到几十秒），编译阶段失败才慢。所以看清是
`Configuring done` 之前还是之后报的错。

---

## 8. 依赖说明

| 目录名 | 来源 |
| --- | --- |
| `spdlog-1.9.2` | `gabime/spdlog` tag `v1.9.2` |
| `magic_enum-0.9.5` | `Neargye/magic_enum` tag `v0.9.5` |
| `glm-1.0.1` | `g-truc/glm` tag `1.0.1` |
| `json-3.11.3` | `nlohmann/json` tag `v3.11.3` |
| `DirectXTK-main` | `microsoft/DirectXTK` branch `main` |
| `cryptopp-cmake-master` | **组装目录**：Crypto++ 8.7.0 源码 + 包装层 CMakeLists |

- 缓存位置：`out/build/<配置>/_deps-local/`。想强制重新下载就删掉它。
- 依赖全部走 HTTPS，**不需要 git**。
- `tools/setup-deps.ps1` 用于离线预置；CMake 会自动识别其中的目录。
- 每次配置都会对 Crypto++ 的上游缺陷打补丁（包装层 `CMakeLists.txt` 的宏定义、
  `zdeflate.cpp` / `integer.cpp` 的 `stdext` 调用），补丁按文本匹配、幂等，重复配置无害。
  万一换个 Crypto++ 版本后又冒出新的 `stdext` 调用，配置阶段会直接指出是哪个文件。

---

## 9. 编完之后

构建成功不代表能用：源码目标版本是 **1.21.44**，实际游戏是 **1.21.120**，
签名和偏移几乎全部对不上。下一步是抓一份运行内存镜像再离线比对，流程见
`tools/DUMP_PROCEDURE.md`，比对工具是 `tools/sigscan/scan.py`（在仓库根目录）。

最短路径：

```powershell
# 1. 注入前设好，让 DLL 自己 dump
$env:SOLSTICE_DUMP     = '1'
$env:SOLSTICE_DUMP_DIR = 'G:\dumps'

# 2. 注入并进一次游戏世界，等 dump 文件生成

# 3. 离线扫签名（把 <...> 换成实际路径）
python tools\sigscan\scan.py G:\dumps\Minecraft.Windows_*.image `
  --header solstice\solstice-src\solstice-master-main\solstice-master-main\src\SDK\OffsetProvider.hpp `
  --json report.json --md report.md
```
