# PlayUi 调用链与 ABI 双调用点核对 — Batch 3.6.2-E 第四工作组

## 1. Solstice::init 中两个 playUi 调用点（必须区分）

### 调用点 A（初始化尾音效）—— 对应崩溃栈 Solstice.dll+2831E4

`src/Solstice.cpp:376-379`：
```cpp
ClientInstance* readyClient = ClientInstance::get();
if (readyClient && readyClient->getMinecraftGame())
    readyClient->getMinecraftGame()->playUi("beacon.activate", 1, 1.0f);   // +2831E4 路径
ChatUtils::displayClientMessage("Initialized!");
```

| 项 | 值 |
|---|---|
| caller | `Solstice::init`（初始化流程尾部） |
| call instruction | 经 `ClientInstance::getMinecraftGame()` → `MinecraftGame::playUi` → `callVirtualFunc` 虚表分发 |
| this source | `getMinecraftGame()` → `MinecraftGame::getInstance()` → `InstanceLocator::locateGame()` → `locate()` → `isPlausibleGame()`（对象已做结构校验） |
| arguments | `soundName="beacon.activate"`，`volume=1`(int→float)，`pitch=1.0f` |
| thread ID | `initThread`（崩溃栈：`initThread → Solstice::init → playUi → TopLevelExceptionHandler`） |
| call timing | 映射校验放行、ClientInstance/FeatureManager 初始化、Hook 安装、MinHook 启用、OpenGL 首帧之后——即一切就绪的初始化尾巴 |
| expected behavior | 播放一个 UI 音效（beacon 激活提示） |

**对应崩溃栈的提问**：`Solstice.dll+2831E4` 正是 `Solstice::init`，`+29240A` 是 `MinecraftGame::playUi` 内部虚调用落点。所以本次 AV 落在**调用点 A**。

### 调用点 B（退出时的去激活音效）

`src/Solstice.cpp:497-501`：
```cpp
if (auto* ci = ClientInstance::get(); ci) {
    if (auto* game = ci->getMinecraftGame(); InstanceLocator::isPlausibleGame(game))
        game->playUi("beacon.deactivate", 1, 1.0f);   // 退出/卸载路径
}
```
- 在 `shutdownThread` / eject 流程，与 initThread **不同线程/不同上下文**，与本次崩溃无关。

## 2. 六个关键问题的回答

1. **哪个调用点对应 Solstice.dll+2831E4？** → 调用点 A（`Solstice::init` 内）。
2. **是否在 initThread 执行？** → 是，崩溃栈明确为 `initThread`。
3. **playUi 是否要求游戏主线程？** → 游戏自身的 `playUi` 通常由其主线程调用；我们是从 Solstice 的 `initThread`（即注入后的主逻辑线程）调用，线程亲和行为与游戏一致，未见显式线程亲和约束。风险不在亲和性，而在 vtable 落点。
4. **该调用在原始稳定版本中是否存在？** → `playUi` 是游戏自身的虚方法（基准版本即有）；我们的封装只是用它播一个提示音，并非新增的初始化必需步骤。
5. **是初始化必需步骤还是单纯声音提示？** → **纯声音提示（可选）**。即使跳过也不影响初始化是否成功（见下）。
6. **调用时 MinecraftGame 对象是否已经完成初始化？** → 到 `init` 末尾时 ClientInstance / MinecraftGame 已被定位（更早阶段就依赖它们），对象即为游戏真实单例；游戏音效系统在初始化尾巴调用属正常时序。

## 3. 生命周期 / 线程亲和性约束

- 未发现显式约束；`playUi` 仅做音效，不触碰渲染主循环。
- 唯一真实约束是「对象必须有效且 vtable 落点可信」——由 669ce2c 的 `isValidVirtualCall` 守卫覆盖（见 `virtual-call-guard-review.md`）。
- 由于 `playUi` 是**可选**音效，`Solstice::init` 在调用它之后才打印 `Initialized!` 并 detach shutdown 线程；守卫若拒绝调用（early return），**不会**把初始化虚报为成功（初始化早已在调用前完成）。

## 4. ABI —— 按目标声明与用户最终裁定（E.1.2.1）逐项核对

声明（`src/SDK/Minecraft/MinecraftGame.hpp:15`）：
```cpp
void playUi(const std::string& soundName, float volume, float pitch);
```

Windows x64 调用约定（整数/指针类走 `RCX,RDX,R8,R9`；浮点类走 `XMM0..XMM3`）。**E.1.2.1 用户最终裁定**：寄存器类型不同，不代表寄存器编号独立分配；`XMM` 编号仍对应参数在**整个调用参数列表**中的位置，成员函数的隐式 `this` 是第一个参数。据此：

| 参数 | 类型 | 参数序位 | 寄存器（层 ① 类型推导） |
|---|---|---|---|
| `this` | 指针（隐式第一参数） | 1 | **RCX** |
| `soundName` | `const std::string&`（引用=指针） | 2 | **RDX** |
| `volume` | `float` | 3 | **XMM2** |
| `pitch` | `float` | 4 | **XMM3** |

> **三层区分（E.1.2.1 明确要求）**：
> 1. **层 ① 按当前 C++ 函数类型推导的 ABI**：`this=RCX`、`soundName=RDX`、`volume=XMM2`、`pitch=XMM3`（按上述参数序位推导，确定性结论；与任务 §二 最初请求的 `RCX / RDX / XMM2 / XMM3` 一致）。
> 2. **层 ② 游戏二进制调用者的参数准备**（调用方在 `call` 之前把 volume/pitch 装进哪个 XMM）：**待验证（PENDING）**。
> 3. **层 ③ 游戏目标函数实际读取的寄存器**（playUi 实现从哪个 XMM 取参）：**待验证（PENDING）**。
>
> **不得把二进制证据缺失解释为 ABI 规则不确定**：层 ②③ 的待验证状态**不改变**层 ① 的 ABI 推导结论（XMM2/XMM3）。
>
> **真实调用点源码（对照一致性）**：本仓库调用经 `MinecraftGame::playUi` → `MemUtils::callVirtualFunc<void, const std::string&, float, float>`（`src/SDK/Minecraft/MinecraftGame.cpp:81`）：
> ```cpp
> // MemUtils.hpp:94  using Fn = Ret(__thiscall*)(void*, Args...);
> // MemUtils.hpp:122 return reinterpret_cast<Fn>(vtable[index])(_this, args...);
> MemUtils::callVirtualFunc<void, const std::string&, float, float>(index, this, soundName, volume, pitch);
> ```
> 模板实参展开后，`Fn = void(__thiscall*)(void*, const std::string&, float, float)`，调用实参序列为 `(_this, soundName, volume, pitch)`，与上述参数序位（1→RCX、2→RDX、3→XMM2、4→XMM3）一致。`__thiscall` 在 x64 被忽略，使用默认 Windows x64 ABI。

### 4.1 修订前后 ABI 对照（E.1 → E.1.2.1）

| 参数 | 类型 | E.1 审查主张（后被 E.1.2.1 采纳） | E.1.1/E.1.2 暂定结论 | E.1.2.1 用户最终裁定（层 ① 类型推导） |
|---|---|---|---|---|
| `this` | 指针 | RCX | RCX | **RCX** ✅ |
| `soundName` | `const std::string&` | RDX | RDX | **RDX** ✅ |
| `volume` | `float` | XMM2 | XMM0 | **XMM2** ✅ |
| `pitch` | `float` | XMM3 | XMM1 | **XMM3** ✅ |

> 结论：E.1 审查主张的 `volume=XMM2 / pitch=XMM3` 经 E.1.1/E.1.2 一度以"浮点按类型独立编号"为由暂改为 XMM0/XMM1；**E.1.2.1 由用户最终裁定采纳 XMM2/XMM3**（按参数在完整参数列表中的序位：第 3 参数 volume→XMM2、第 4 参数 pitch→XMM3）。层 ① 类型推导 ABI 现为 `volume=XMM2 / pitch=XMM3`；游戏二进制 caller/target 寄存器读取（层 ②③）仍标 PENDING（见 §4.2）。本文件不改动任何代码，仅校正文档。

### 4.2 三层区分与证据缺口（E.1.2.1 / P0-1）

任务要求把证据分层核验：① C++ 声明/函数类型推导；② 游戏二进制调用方的参数准备；③ 游戏目标函数实际读取的参数寄存器。结论必须明确区分"类型推导"与"二进制观测"，且**不得把二进制证据缺失解释为 ABI 规则不确定**。

#### 层 ①：按当前 C++ 函数类型推导的 ABI（确定性结论）

- 目标声明 `void playUi(const std::string& soundName, float volume, float pitch)`；函数指针 `Fn = Ret(__thiscall*)(void*, const std::string&, float, float)`；调用实参 `(_this, soundName, volume, pitch)`。
- 隐式 `this` 是第一个参数；按参数在**完整调用参数列表**中的序位分配寄存器（E.1.2.1 用户裁定）：`_this`(参数1,指针)→RCX、`soundName`(参数2,引用=指针)→RDX、`volume`(参数3,float)→**XMM2**、`pitch`(参数4,float)→**XMM3**。
- `__thiscall` 在 x64 被忽略，使用默认 Windows x64 ABI；浮点类走 XMM0..XMM3，但 XMM 编号对应参数整体序位，故第 3、第 4 参数落到 XMM2/XMM3。

→ 层 ① 收敛为 **`volume=XMM2`、`pitch=XMM3`**（按 C++ 函数类型与用户裁定推导，确定性）。

#### 层 ② / ③：游戏二进制（调用方参数准备 + 目标函数读取）—— PENDING

- 层 ②（调用方在 `call` 之前把 volume/pitch 装进哪个 XMM）与层 ③（playUi 目标函数从哪个 XMM 读取）需要定位 slot-164 thunk `0x1CBC3E4` 的调用者并追踪其 `call [rip+0x6eb6398]` 间接目标到真实 `playUi` 实现。在混淆/加密二进制（自定义 `.winlice/.boot/.SCY` 代码段、寄存器/间接跳转、无干净符号）中这两步**未能完成**，故层 ②③ 标记为 **BINARY_ABI_CORROBORATION_PENDING**。
- thunk `0x1CBC3E4` 自身不装载任何 XMM 寄存器（仅 `mov rcx,[rcx+8]` → 取 vtable → `call [rip+...]`），它只**原样透传**调用方设置的 XMM 寄存器——因此"thunk 不装载 XMM"既不支持也不否定 XMM2/XMM3，不能作为二进制 corroboration 证据（E.1.2 已撤回据此证明 XMM0/XMM1 的论证；该撤回在 E.1.2.1 同样适用：thunk 透传不构成任何寄存器编号证据）。

#### 结论（强制不矛盾）

- 层 ①（声明 + 函数类型推导 + 用户裁定）：**`volume=XMM2 / pitch=XMM3`**（确定性推导）。
- 层 ②③（二进制观测）：**PENDING**，未取得调用方/目标函数的字节级寄存器证据。
- 层 ②③ 的待验证状态**不改变**层 ① 的 ABI 规则结论（XMM2/XMM3）；不得把二进制证据缺失解释为 ABI 规则不确定（E.1.2.1 明确要求）。
- 本签名按上述类型推导的 ABI（XMM2/XMM3）与早期 E.1.1/E.1.2 文档中"按浮点类型独立编号"推导的 XMM0/XMM1 **冲突**：该冲突已由 E.1.2.1 用户裁定解决——采用按参数整体序位编号（XMM2/XMM3）。二进制 caller/target 寄存器读取留待运行期 dump 补齐（O1/O2）。

## 5. 结论

- 崩溃落在调用点 A（`Solstice::init` 尾音效），由 `initThread` 执行，对象已校验。
- `playUi` 是**可选音效**，非初始化必需；守卫拒绝不会虚报初始化成功。
- ABI（层 ① 类型推导）：`RCX / RDX / XMM2 / XMM3`（E.1.2.1 用户裁定）；二进制 caller/target 寄存器读取仍 PENDING，无需为 ABI 改动任何代码。
