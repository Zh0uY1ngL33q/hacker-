# Solstice

Solstice 是一个基于 C++ / CMake 构建的 Minecraft Windows 版（UWP / Win32）注入式客户端项目，
以 DLL 形式运行在游戏进程内，通过签名扫描定位游戏内部函数，并使用 MinHook 安装钩子。

- 目标游戏版本：**1.21.44**（`GenerateBuildInfo.cmake` 会在配置阶段自动读取本机安装的 `Minecraft.Windows.exe` 版本）
- 构建系统：**CMake ≥ 3.28** + MSVC（x64）+ Ninja
- C++ 标准：**C++26**
- 平台：**仅 Windows x64**（32 位构建会在 CMake 配置阶段直接报错）

---

## 目录结构

```
.
├── CMakeLists.txt              # 主构建脚本（编译定义、资源内嵌、着色器编译、依赖配置）
├── CMakeSettings.json          # VS 的 CMake 配置预设（x64-Debug / x64-Release）
├── GenerateBuildInfo.cmake     # 由 git 信息生成 build_info.h
├── cmake/CPM.cmake             # CPM 依赖管理脚本
├── include/                    # 第三方库（以子目录方式编译）
│   ├── ImGui/                  # 图形界面
│   ├── Kiero/                  # DirectX 12 / 11 图形接口
│   ├── entt/                   # ECS 实体组件系统
│   ├── libhat/                 # 进程内存扫描 / 签名匹配
│   ├── minhook/                # 函数 Hook
│   └── nes/                    # 其他内部依赖库
├── resources/                  # 会被编译进 DLL 的资源（音效、图片、字体）
├── src/
│   ├── dllmain.cpp             # DLL 入口，在独立线程中调用 Solstice::init
│   ├── Solstice.cpp/.hpp       # 客户端主生命周期（初始化 / 卸载）
│   ├── pch.hpp                 # 预编译头
│   ├── Features/               # 功能层
│   │   ├── FeatureManager      # 功能总入口
│   │   ├── Auth/               # 私有构建的授权校验
│   │   ├── Command/            # 命令系统（.h/.cpp 一一对应 Commands/）
│   │   ├── Configs/            # 配置与偏好持久化
│   │   ├── Events/             # 事件定义（Event.hpp 中的事件类型）
│   │   ├── GUI/                # 自定义 GUI 控件
│   │   ├── IRC/                # IRC 客户端
│   │   └── Modules/            # 模块（功能）实现，按分类分子目录
│   ├── Hook/                   # Hook 管理与各个具体 Hook
│   │   └── Hooks/
│   │       ├── ActorHooks/         # 动画、模型、名称标签
│   │       ├── ContainerHooks/     # 容器界面
│   │       ├── MiscHooks/          # 键盘、鼠标、预游戏检查、连接请求
│   │       ├── NetworkHooks/       # 发包 / 收包 / RakNet
│   │       └── RenderHooks/        # D3D、渲染调度、手持物品、物品渲染
│   ├── SDK/                    # 游戏内部结构的封装
│   │   ├── OffsetProvider      # 偏移量扫描
│   │   ├── SigManager          # 函数签名扫描
│   │   └── Minecraft/          # Actor / Inventory / Network / Rendering / UWP / World
│   └── Utils/                  # 工具类
│       ├── GameUtils/ MiscUtils/ Shaders/ SysUtils/
├── out/build/                  # CMake 生成目录（已随仓库附带）
└── .github/workflows/          # GitHub Actions 构建流程
```

### 模块分类

模块位于 `src/Features/Modules/`，按 `src/Features/Modules/ModuleCategory.hpp` 中的分类划分：

| 分类 | 目录 | 数量 |
| --- | --- | --- |
| Combat | `Modules/Combat/` | 6 |
| Movement | `Modules/Movement/` | 25 |
| Player | `Modules/Player/` | 24 |
| Visual | `Modules/Visual/` | 37 |
| Misc | `Modules/Misc/` | 33 |

新增模块时，通常只需在该分类目录下添加 `Xxx.hpp` / `Xxx.cpp`，
并在 `ModuleManager` 中注册。

### 命令

`src/Features/Command/Commands/` 下每个命令对应一对文件，包含 `.help`、`.bind`、`.config`、
`.friend`、`.gamemode`、`.irc`、`.module`、`.queue`、`.set`、`.snipe`、`.teleport`、
`.toggle`、`.unbind`、`.vclip` 等。

---

## 环境依赖

1. **Visual Studio 2022**（含「使用 C++ 的桌面开发」工作负载），MSVC v143 工具集。
2. **CMake ≥ 3.28**。
3. **Ninja**（`CMakeSettings.json` 中使用的生成器）。
4. **MSYS2 / MinGW64 binutils**，用于把 `resources/` 下的文件转成可链接的目标文件：
   ```bash
   pacman -S mingw-w64-x86_64-binutils
   ```
   安装后把 `C:/msys64/mingw64/bin` 加入 `PATH`，否则 `ld` 找不到时 CMake 会直接报错退出。
5. **Windows SDK** 中的 `fxc.exe`（HLSL 编译器），用于把 `src/Utils/Shaders/` 下的
   HLSL 着色器编译成 `.inc`，构建时会自动调用。
6. **CMakeLists 中 CPM 依赖的本地源码目录**，需要事先准备好：
   - `D:/dev/deps/spdlog-1.9.2`
   - `D:/dev/deps/magic_enum-0.9.5`
   - `D:/dev/deps/glm-1.0.1`
   - `D:/dev/deps/json-3.11.3`
   - `D:/dev/deps/DirectXTK-main`
   - `D:/dev/deps/cryptopp-cmake-master`

   这些路径是硬编码的，若本机路径不同，直接修改 `CMakeLists.txt` 中对应的 `SOURCE_DIR` 即可。

---

## 构建

推荐使用 Visual Studio 的「CMake 打开文件夹」功能，直接以 `CMakeSettings.json` 作为预设：

```bash
# 配置
cmake -S . -B out/build/x64-Debug -G Ninja -DCMAKE_BUILD_TYPE=Debug

# 编译
cmake --build out/build/x64-Debug --config Debug
```

也可直接使用 `CMakeSettings.json` 中的两个预设：`x64-Debug` 与 `x64-Release`。

### 构建流程说明

1. `generate_build_info` 目标执行 `GenerateBuildInfo.cmake`，通过 `git` 命令把提交哈希、
   分支、提交信息、修改文件数量、本机用户名以及检测到的 Minecraft 版本写入
   `<build>/build_info.h`。**因此源码需要在 Git 仓库中构建**，否则该步骤的版本信息会为空。
2. `SolsticeShaders` 目标调用 `fxc.exe` 把 `src/Utils/Shaders/*.hlsl` 编译进 `<build>/SolsticeShaders`。
3. 配置阶段会扫描 `resources/`，并自动生成 / 覆盖 `src/Utils/Resources.hpp` 与
   `src/Utils/Resources.cpp`（这两个文件由 CMake 生成，**不要手动编辑**）：
   - 每个资源通过 `ld -r -b binary` 转换为目标文件后参与链接；
   - `resources/fonts/*.ttf` 会被自动注册为 ImGui 字体，并额外注册一份 `_large` 版本。
4. 最终产物为一个名为 `Solstice` 的 SHARED 库（DLL）。

---

## 运行时流程

1. DLL 被注入到游戏进程，`DllMain` 在新线程上调用 `Solstice::init`（避免 loader lock 与
   UWP `CoreApplication` 在 win32 桌面构建下不可用的问题）。
2. 等待模块加载完成 → 初始化日志 → 计算 HWID → 初始化 MinHook → 加载偏好设置 →
   （私有构建时）执行授权校验。
3. 等待游戏代码段解密完成（NetEase 客户端采用惰性解密，过早扫描会导致大量签名解析为 0），
   然后执行 `OffsetProvider::initialize()` 与 `SigManager::initialize()`。
   扫描失败的签名会被逐条打印；Debug 构建下可输入 `DEBUG` 继续，Release 构建则直接退出。
4. 初始化 `FeatureManager` 与 `HookManager`，随后在独立线程中轮询：
   - 检测到本地玩家后再安装 BaseTick 钩子、加载默认配置；
   - 按 `END` 键触发卸载流程：恢复补丁 → 关闭 Hook → 卸载功能 → 释放 DLL。

---

## 离线签名 / 偏移分析

`Minecraft.Windows.exe` 使用 WinLicense 加壳（存在 `.winlice` / `.boot` 段）：磁盘上的代码段
是密文（熵 ≈ 7.98），**只在运行时解密到内存**。因此针对磁盘文件做静态签名分析是不可能的，
必须基于运行时内存。

### 1. 导出解密后的镜像

项目内置了运行时 dump 工具，通过环境变量开启：

```cmd
set SOLSTICE_DUMP=1
:: 然后照常注入 Solstice
```

启用后会在 `Solstice::init()` 完成签名扫描之后自动导出三份文件到
`%APPDATA%\..\Local\Packages\Microsoft.MinecraftUWP_8wekyb3d8bbwe\RoamingState\Solstice\Dump\`：

| 文件 | 内容 |
| --- | --- |
| `Minecraft.Windows_<版本>_<时间戳>.image` | 虚拟内存镜像，**文件偏移 == RVA**，可直接被 pefile 解析 |
| `..._sections.json` | 段表、每个段的熵、实际 dump 字节数与跳过的字节数 |
| `..._offsets.json` | 本次运行解析出的全部 `OffsetProvider` / `SigManager` 结果（存为 RVA，可跨运行 diff） |

熵值是关键验证信号：dump 出来的代码段熵若仍然接近 8.0，说明壳还没解完，需要在游戏里
多等一会儿再触发。

### 2. 离线校验全部签名

```bash
python tools/verify_sigs.py <dump>.image --src .
```

工具直接解析 `OffsetProvider.hpp` / `SigManager.hpp` 中的定义，对每一条给出三种状态：

- **OK** —— 唯一匹配
- **AMBIGUOUS** —— 匹配多处。运行时 `find_pattern` 只取第一个，属于静默取错，是最难排查的失效
- **MISSING** —— 完全没匹配到
- **SUSPECT** —— 匹配到了，但算出的偏移值不合理（例如 vtable index 不在合理范围），说明匹配到了同构代码

同时会列出**没有任何签名、纯硬编码**的 `DEFINE_FIELD`，这些是版本绑定的重灾区。

工具在存在非 OK 项时返回非零退出码，方便脚本化。

### 版本升级后的适配流程

1. `set SOLSTICE_DUMP=1` 跑一次，拿到新版本的 `.image` 与 `.offsets.json`
2. 用 `verify_sigs.py` 跑出 `MISSING` / `AMBIGUOUS` / `SUSPECT` 清单
3. 对照上一版本的 `.offsets.json` 做 diff，定位漂移项
4. 在 `.image` 上用 capstone 反汇编命中点，重写失效签名
5. `CMakeLists.txt` 里硬编码的扫描范围 `0x7a7baa9` 建议一并改为从 PE 头动态解析段边界

---

## 编译宏

| 宏 | 作用 |
| --- | --- |
| `__PRIVATE_DEBUG__` | 私有版 + 调试行为 |
| `__PRIVATE_BUILD__` | 私有版构建，启用授权校验 |
| `__DEBUG__` | Debug 构建（CMakeLists 中始终定义；`CMAKE_BUILD_TYPE=Debug` 时再次定义） |
| `SOLSTICE_VERSION` | 版本号，当前为 `v1.0.0` |

Debug 构建会额外启用文件日志（`solstice.log`）、崩溃处理器以及可选的「必须附加调试器”限制。

---

## 常见问题

- **`The ld command is not available on your system`**：安装 MSYS2 的
  `mingw-w64-x86_64-binutils`，并把 `C:/msys64/mingw64/bin` 加入 `PATH` 后重启 IDE。
- **CMake 找不到 CPM 依赖**：确认 `D:/dev/deps/` 下的依赖目录已存在，或修改
  `CMakeLists.txt` 中的 `SOURCE_DIR`。
- **大量签名/偏移扫描失败**：确认游戏版本与本项目目标版本一致（1.21.44），
  并确保注入时机没有早于代码段解密完成。
- **不支持 x86**：项目仅支持 64 位目标，CMake 会在配置时直接报错。

---

## CI

`.github/workflows/cmake-single-platform.yml` 在 `release` 分支的 push / PR 上触发，
运行在 self-hosted runner 上，通过调用本地 `build.bat` 脚本完成私有版与公开版构建。
