# DEPENDENCY_MAP — 外部依赖与内部耦合

## 1. 外部依赖（来自 `CMakeLists.txt` / `cmake/Dependencies.cmake`）

| 名称 | 获取方式 | 用途 | 迁移影响 |
|---|---|---|---|
| `spdlog` | CPM，`file(DOWNLOAD)` 到 `<build>/_deps-local` | 日志 | 无版本耦合 |
| `magic_enum` | 同上 | 枚举名反射（设置/命令） | 无 |
| `glm` | 同上 | 矩阵/向量 | **有**：`RenderUtils::transform` 与相机矩阵全部走 glm 列主序 |
| `nlohmann_json` | 同上 | 配置文件 / JSON 解析 | **有**：`SigManager::JSON_parse` 未解析时相关功能失效 |
| `DirectXTK` | 同上 | D3D 辅助 | 仅 D3D 路径使用 |
| `cryptopp`（8.7.0 + 包装 CMakeLists） | 两个 zip 拼装 | 授权/加密 | 无 |
| `minhook` | 随仓库 `include/minhook` | detour | 无 |
| `kiero` | 随仓库 `include/Kiero` | swapchain 钩子工厂 | 仅 D3D 路径 |
| `libhat` | 随仓库 `include/libhat` | 签名编译与扫描 | **有**：`hat::compile_signature<"...">` 在编译期展开，模式串写死 |
| `nuvola_event_system` (nes) | 随仓库 `include/nes` | 事件分发 | 无 |
| `entt` | 随仓库 `include/entt` | ECS（`ActorUtils` 回退路径） | **有**：`Level::getRuntimeActorList` 不可用时改用它 |

编译器侧硬依赖（`target_link_libraries`，426 行）：`d3d12 d3d11 d2d1 dxgi dxguid dwrite
dbghelp comctl32 ws2_32 ole32 Mmdevapi runtimeobject version`。

## 2. 内部耦合（实测，不是设计文档）

| 调用方 | 被调方 | 耦合强度 | 说明 |
|---|---|---|---|
| `Features/Modules/**` | `OffsetProvider` / `SigManager` | **强** | 126 个模块里只有 7 个直接引用上述符号；其余经 `SDK/` 中转 |
| `SDK/Minecraft/**` | `OffsetProvider` / `SigManager` | **强** | 每个 SDK 类的 .cpp 就是“符号 → 成员访问”的薄封装 |
| `Hook/Hooks/**` | `SigManager` | **强** | detour 目标地址直接来自签名 |
| `Features/GUI/**` + `Hook/Hooks/RenderHooks/OpenGLGui.cpp` | ImGui 后端 | 中 | GL 后端独占；D3D11/DX12 后端代码仍在树内但不被执行 |
| `Utils/MiscUtils/RenderUtils` | `D3DHook` 的 Present 钩子 | **隐式** | `transform` 只在 Present 里赋值 → GL 路径下恒为零（见 `COMPATIBILITY_MATRIX.md`） |

## 3. 迁移视角的三条结论

1. **真正需要“版本适配层”的只有 2 个文件**：`OffsetProvider.hpp`、`SigManager.hpp`。
   其余 600+ 文件都通过这两者间接依赖目标程序。因此“解耦 UI 与底层游戏依赖”的实际工作量
   不在 UI，而在于给这两个文件加一层可替换的解析后端。
2. **`libhat` 的模式串在编译期展开**，无法在运行时替换；换版本只能改头文件重编。这与
   “配置系统独立”无冲突，但与“运行时热切换版本”冲突——后者做不到，应明确放弃。
3. **ImGui 后端与游戏图形栈是两套东西**：D3D11/DX12 后端代码保留不影响正确性，
   但只有 `wglSwapBuffers` / `gdi32!SwapBuffers` 这条链在这个客户端里真的会被调用
   （日志侧证据见 `TEST_REPORT.md §4`）。

## 4. 未确认

| 项目 | 状态 | 原因 |
|---|---|---|
| 目标程序是否同时支持 D3D12 与 GL 呈现 | UNKNOWN | 导入表同时有 `opengl32.dll`(4) 与 D3D 静态链接；只能确认本机运行时走 GL |
| `cohtml`（内嵌 HTML UI）是否影响 ClickGUI 互斥 | UNKNOWN | 无运行时抓取 |
| 依赖库的目标版本是否随 1.21.120 变更 | UNKNOWN | 无 1.21.44 二进制可比对 |
