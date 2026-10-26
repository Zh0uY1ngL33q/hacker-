# 运行时 dump 采集操作手册

本文档用于在**有 C++ 环境的机器**上编译 Solstice，并采集解密后的运行时镜像，
供离线分析签名与偏移。整个流程约 15–30 分钟。

---

## ⚠️ 0. 先确认代码版本（最容易出错的一步）

`CMakeLists.txt` 中的六条 `CPMAddPackage(...)` 依赖 **`D:\dev\deps\` 下的固定路径**，
且**只写死了一种路径**。如果你的机器上这些目录不存在，CMake 会在配置阶段直接失败。

本次修改过的文件只有这四个：

```
新增：src\Utils\SysUtils\ModuleDump.hpp
新增：src\Utils\SysUtils\ModuleDump.cpp
修改：src\Solstice.cpp          （只加了 1 个 #include + 1 段 dump 调用）
新增：tools\verify_sigs.py
```

**请从修改后的目录取代码，不要用原始的 `solstice-src.zip`**（那个压缩包是未修改的）。

---

## 1. 环境检查清单

在目标机器上依次确认：

```cmd
:: CMake（必须 ≥ 3.28，CMakeLists 开头有硬性要求）
cmake --version

:: MSVC 编译器（在 VS 的 Developer Command Prompt 里执行才有效）
cl

:: Ninja（CMakeSettings.json 用的是 Ninja 生成器）
ninja --version

:: binutils（资源内嵌依赖 ld.exe）
where ld
:: 若无结果，后面第 3 步必须先装
```

工具链要求：

| 组件 | 版本要求 | 备注 |
| --- | --- | --- |
| Visual Studio 2022 | v143 工具集 | 必须勾选「使用 C++ 的桌面开发」 |
| MSVC | 支持 C++26 | 太老的 v143 版本会因 `CMAKE_CXX_STANDARD 26` 报错 |
| Windows SDK | 任一较新版本 | `fxc.exe` 着色器编译需要 |
| MSYS2 | binutils | 仅用于 `ld -r -b binary` 内嵌资源 |

---

## 2. 准备六个第三方依赖

CMakeLists 硬编码了以下路径，**目录名必须一字不差**：

| 依赖 | 版本 | 目标路径 |
| --- | --- | --- |
| spdlog | 1.9.2 | `D:\dev\deps\spdlog-1.9.2` |
| magic_enum | 0.9.5 | `D:\dev\deps\magic_enum-0.9.5` |
| glm | 1.0.1 | `D:\dev\deps\glm-1.0.1` |
| nlohmann_json | 3.11.3 | `D:\dev\deps\json-3.11.3` |
| DirectXTK | main | `D:\dev\deps\DirectXTK-main` |
| cryptopp | cmake master | `D:\dev\deps\cryptopp-cmake-master` |

准备方式（二选一）：

**A. 从已有构建机器直接拷贝**（最省事）
把上面六个目录整个打包拷过去，解压到 `D:\dev\deps\` 下。

**B. 重新获取**
`spdlog` / `magic_enum` / `glm` / `nlohmann_json` 从 GitHub 按版本号 checkout 或下载对应 tag。
`DirectXTK` 与 `cryptopp-cmake` 拉 master 分支即可。

> **如果你不想放 D 盘**，直接改 `CMakeLists.txt` 里对应的 `SOURCE_DIR`，
> 改成本机实际路径即可，其余代码不用动。

验证：

```cmd
dir D:\dev\deps
:: 应能看到上面 6 个子目录
```

---

## 3. 安装 binutils（资源内嵌必需）

CMakeLists 第 17–28 行会强制检测 `ld`，**找不到直接 `FATAL_ERROR` 退出**。

装好 MSYS2 后执行：

```bash
pacman -S mingw-w64-x86_64-binutils
```

然后把 `C:\msys64\mingw64\bin` 加入系统 `PATH`，**并重启 Visual Studio**
（环境变量改动需要重启 IDE 才生效）。

验证：

```cmd
ld --version
```

---

## 4. 配置并编译

推荐用 VS 的「打开文件夹 / Open Folder」模式，VS 会自动读取 `CMakeSettings.json`，
提供 `x64-Debug` / `x64-Release` 两个预设。

命令行方式（需在 VS Developer Command Prompt 中执行）：

```cmd
cd /d <项目根目录>

:: 配置
cmake -S . -B out\build\x64-Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug

:: 编译
cmake --build out\build\x64-Debug --config Debug
```

### 编译期会看到这些输出（都是正常的）

```
Build version: <commit hash>          ← 来自 GenerateBuildInfo.cmake
Build version (short): xxxxxxx
Build branch: <branch>
Minecraft install location: ...
Intended version: ...
Embedding resource: resources\cash.wav
...
Generated Resources.hpp               ← CMake 自动生成，勿手改
Generated Resources.cpp
Building 2 shaders                    ← 调用 fxc.exe 编译 HLSL
[offsets] ...                         ← 编译期不会有，这是运行期输出
```

### 常见编译错误

| 报错 | 原因 | 处理 |
| --- | --- | --- |
| `ld command is not available` | binutils 没装或不在 PATH | 回到第 3 步 |
| `SOURCE_DIR ... does not exist` | 六个依赖缺失 | 回到第 2 步 |
| `fxc.exe 不是内部或外部命令` | 装了 DirectX 但没装 HLSL 工具 | 运行 `dxsdk` 安装器，或只改 Debug 构建暂时注释着色器 target |
| `'uint64_t' does not name a type` 等 C++ 标准错误 | MSVC 版本过老 | 升级 VS 2022 至最新 |
| `ModuleDump.cpp` 相关报错 | 本次新增代码 | **把报错发我**，这是未经验证的部分 |

产物：`<build>\Solstice.dll`

---

## 5. 开启 dump 并注入

### ⚠️ 关键：环境变量必须被「游戏进程」继承

DLL 是在 `Minecraft.Windows.exe` 内部运行的，环境变量必须在**游戏进程启动前**就存在。
如果只在注入器的控制台里 `set`，游戏是注入器拉起的话**可能继承不到**。

**推荐做法（最稳）**：设置成用户级环境变量

```cmd
setx SOLSTICE_DUMP 1
```

然后**重新登录或重启 VS / 启动器**，确保新环境生效。

> **这个变量现在不是必须的。** 从加入 `SigFuzz` 的那一版起，只要签名扫描失败
> （也就是版本不匹配、最需要镜像的时候），`Solstice.cpp` 会无条件强制 dump 一次，
> 并额外写出 `Dump\sigreport.json`。环境变量只用于「扫描全部成功、但仍想主动抓一份
> 镜像」的情况。

验证游戏进程确实拿到了：

```cmd
:: 游戏运行中，另开一个窗口
powershell -Command "Get-Process Minecraft.Windows | ForEach-Object { $_.StartInfo }"
:: 更直接的办法：设完后手动开游戏，看 dump 目录有没有文件生成
```

### 操作顺序

1. `setx SOLSTICE_DUMP 1`（或在同一控制台里 `set SOLSTICE_DUMP=1` 后再启动游戏）
2. **完全重启游戏**（旧进程不会带上新环境变量）
3. 注入 `Solstice.dll`
4. **等游戏加载到主菜单后，再等 10~20 秒** —— 让壳把代码段完全解密
   （这一条很重要：熵没降下来 dump 就是废的，见第 6 步）
5. 如果有需要，可以进一个世界再等一会，确保游戏逻辑代码也已解密
6. 按 `END` 卸载 DLL

> 版本不匹配时不需要额外操作：扫描一失败就会**自动**写出镜像和 `sigreport.json`，
> 然后才进入失败处理（Debug 宏开着，会停在 `Type 'DEBUG' to continue`，按回车即可
> 卸载）。看到 `Failed to find N signatures/offsets!` 之后稍等几秒再按键，镜像有
> 200 MB 要落盘。

---

## 6. 确认 dump 成功（必做，别跳过）

dump 输出目录：

```
%APPDATA%\..\Local\Packages\Microsoft.MinecraftUWP_8wekyb3d8bbwe\RoamingState\Solstice\Dump\
```

注意路径里有 `..\..`，实际展开是：

```
C:\Users\<你的用户名>\AppData\Local\Packages\Microsoft.MinecraftUWP_8wekyb3d8bbwe\RoamingState\Solstice\Dump\
```

应生成 3 个文件（每跑一次新增一组）：

```
Minecraft.Windows_1.21.120.0_20261018_143052.image    ← 主产物，约 220 MB
Minecraft.Windows_1.21.120.0_20261018_143052.sections.json
Minecraft.Windows_1.21.120.0_20261018_143052.offsets.json
```

同一目录还会多一个 `sigreport.json`（不是镜像的一部分，可以单独发）：
签名失败清单、多匹配清单，以及每个「只差几个字节」的签名的实际字节和差异位置。
修签名时先看它，比截图有用得多。格式见 `tools/DIAGNOSTICS.md`。

### 第一件事：看熵（这是成败判据）

用**任何文本编辑器**打开 `sections.json`，找到第一个代码段（NetEase 段名被清空，
会显示成 `"<blank#0>"`）：

```json
{
  "name": "<blank#0>",
  "rva": "0x1000",
  "virtualSize": "0x8B6E8A9",
  "entropy": 6.42,          ← 看这个数
  "dumpedBytes": 146279850,
  "skippedBytes": 0         ← 应该是 0
}
```

| entropy | 结论 | 怎么办 |
| --- | --- | --- |
| **6.0 ~ 6.8** | ✅ 解密完成，dump 可用 | 继续第 7 步 |
| **≈ 7.9 ~ 8.0** | ❌ 壳还没解完 | 在游戏里多待 30~60 秒再触发；或进世界后再 dump |
| 任意值但 `skippedBytes > 0` | ⚠️ 部分页面未提交 | 把 `skippedBytes` 的值告诉我 |

对照参考：**磁盘上的 exe 该段熵是 7.984**（完全密文）。dump 后如果还是 7.9+，说明没抓到。

### 第二件事：看控制台日志

注入时的控制台会输出签名扫描结果，**请完整保存**：

```
[offsets] found ClientInstance_getLocalPlayer @ 0x7FF6A1234567
[offsets] failed to find XxxXxx              ← 这些行直接发我，非常有价值
[offsets] initialized in 812ms, 63 total sigs scanned
[dump] sec[0] <blank#0> RVA=0x1000 vsize=0x8B6E8A9 entropy=6.421 skipped=0x0
[dump] done. image=... (220 MB)
```

日志文件在同目录的 `solstice.log`。

---

## 7. 离线校验（在有 Python 的任意机器上都行）

```cmd
:: 把 .image 和项目根目录放一起
python tools\verify_sigs.py "Dump\Minecraft.Windows_1.21.120.0_20261018_143052.image" ^
       --src . ^
       --json report.json
```

需要 `pip install pefile`（capstone 可选，装了更好）。

输出示例：

```
image        : Minecraft.Windows_1.21.120.0_20261018_143052.image
sizeOfImage  : 0xD7AB000
code section : <blank#0>  [0x1000, 0x8B6F8A9)  139 MB

==============================================================================
KIND           STATUS     HITS   NAME / NOTES
==============================================================================
offset         OK         1      ClientInstance_getLocalPlayer
offset         MISSING    0      Actor_mContainerModel
offset         AMBIGUOUS  3      BlockLegacy_mayPlaceOn
offset         SUSPECT    1      Actor_mSwinging   <- implausible vtable index
==============================================================================

summary: OK=142, MISSING=31, AMBIGUOUS=9, SUSPECT=3
```

四种状态的含义：

| 状态 | 含义 | 危害 |
| --- | --- | --- |
| `OK` | 唯一匹配 | 正常 |
| `AMBIGUOUS` | 匹配多处，运行时取第一个 | **静默取错，最难排查** |
| `MISSING` | 没匹配到，offset = 0 | 读地址 0 附近 → 崩溃 |
| `SUSPECT` | 匹配到了但偏移值不合理 | 匹配到了同构代码，读到垃圾数据 |

工具存在非 OK 项时返回退出码 1，可用于脚本判断。

---

## 8. 需要回传给我的东西

| # | 文件 | 是否必须 | 说明 |
| --- | --- | --- | --- |
| 1 | `*.image` | **必须** | 离线签名分析的唯一输入，约 220 MB，可用 7z 压缩（大概率压不下来，因为大部分是可执行代码） |
| 2 | `*.sections.json` | 必须 | 判断 dump 是否有效的关键 |
| 3 | `*.offsets.json` | 必须 | 本次解析结果，用于跨版本 diff |
| 4 | `report.json` | 建议 | 工具输出的结构化清单 |
| 5 | 控制台日志 / `solstice.log` | **必须** | 特别是 `[offsets] failed to find ...` 那些行 |
| 6 | 崩溃日志 / dump（若有） | 可选 | 崩溃点能直接锁定坏掉的指针链 |

> 传输困难的话，可以先只发 2 / 3 / 5（都很小），我先判断 dump 是否有效、失效清单长什么样，
> 再决定 220 MB 的 `.image` 怎么传。

---

## 9. 拿到 dump 后我会做什么

1. 用 `verify_sigs.py` 交叉验证，确认失效清单
2. 对照 `.offsets.json` 与 1.21.44 的旧值做 diff，量化漂移
3. 对每个 `MISSING` / `AMBIGUOUS` / `SUSPECT` 项：
   - 用 capstone 反汇编命中点上下文，重写更长、更唯一的签名
   - 或改为从已命中的稳定函数内部做交叉引用定位
4. 重算那 14 个纯硬编码 `DEFINE_FIELD`（当前值来自 1.21.44，已完全失效）：
   `Actor_mEntityIdentifier`、`Actor_mLocalName`、`ClientInstance_mViewMatrix`、
   `ClientInstance_mFovX/Y`、`Block_mRuntimeId/Legacy`、`BlockPalette_mLevel` 等
5. 产出针对 **1.21.120** 的完整补丁
6. 顺带把 `OffsetProvider::scanSig` 里硬编码的 `0x7a7baa9` 改成从 PE 头动态解析段边界

---

## 附：为什么必须走运行时 dump

静态分析这条路是**确定走不通**的，实测证据：

```
磁盘 exe 上扫描项目现存的签名：
  ClientInstance_getLocalPlayer  -> 0 hits
  ClientInstance_getOptions       -> 0 hits
  Actor_baseTick                  -> 0 hits
  Level_getPlayerList             -> 0 hits
  BlockSource_getChunk            -> 0 hits

代码段熵           = 7.984 （理论上限 8.0）
字节种类           = 256 / 256 （完全均匀分布，无代码统计特征）
段名               = 前 7 个全部被清空
.winlice 段        = 存在（WinLicense / Oreans 壳）
.boot 段           = 0x82F000
```

磁盘上不存在任何可反编译的指令。代码只有运行时才解密到内存 ——
这正是 `Solstice.cpp` 里 "NetEase decrypts its code lazily" 那段注释描述的机制。
