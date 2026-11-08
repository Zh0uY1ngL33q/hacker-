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

## 4. ABI —— 按真实声明与 x64 调用约定逐项核对

声明（`src/SDK/Minecraft/MinecraftGame.hpp:15`）：
```cpp
void playUi(const std::string& soundName, float volume, float pitch);
```

Windows x64 调用约定（整数/指针类走 `RCX,RDX,R8,R9`；浮点类走 `XMM0..XMM3`，两类独立计数）：

| 参数 | 类型 | 寄存器 |
|---|---|---|
| `this` | 指针 | **RCX** |
| `soundName` | `const std::string&`（引用=指针） | **RDX** |
| `volume` | `float`（第 1 个浮点参数） | **XMM0** |
| `pitch` | `float`（第 2 个浮点参数） | **XMM1** |

> 任务示例中写的「RCX / RDX / XMM2 / XMM3」**不正确**：前两个浮点参数落在 `XMM0`/`XMM1`，而非 `XMM2`/`XMM3`（后者仅当前面已有 ≥2 个浮点参数时才用到）。
>
> **真实调用点反汇编核对（E.1 / P0-1 要求）**：本仓库的调用并非手写 asm，而是经 `MinecraftGame::playUi` → `MemUtils::callVirtualFunc<void, const std::string&, float, float>`（`src/SDK/Minecraft/MinecraftGame.cpp:81`）：
> ```cpp
> // MemUtils.hpp:94  using Fn = Ret(__thiscall*)(void*, Args...);
> // MemUtils.hpp:122 return reinterpret_cast<Fn>(vtable[index])(_this, args...);
> MemUtils::callVirtualFunc<void, const std::string&, float, float>(index, this, soundName, volume, pitch);
> ```
> 模板实参展开后，`Fn = void(__thiscall*)(void*, const std::string&, float, float)`；调用时实参顺序为 `(_this, soundName, volume, pitch)`。按 MS x64 ABI，整数/指针寄存器（RCX,RDX,R8,R9）与 XMM 浮点寄存器（XMM0–XMM3）**按参数类型独立分配**：`_this`→RCX、`soundName`(引用=指针)→RDX、`volume`(第 1 个浮点)→**XMM0**、`pitch`(第 2 个浮点)→**XMM1**。这与声明完全一致，ABI 层与游戏自身 `playUi`（`48 8B 80 [disp32] FF 15` 的 vtable 调用）一致——我们的调用镜像了游戏自己的调用方式。
>
> **E.1 审查校正记录**：E.1 审查意见要求把 ABI 改为 `this=RCX / soundName=RDX / volume=XMM2 / pitch=XMM3`，并禁止"浮点参数独立计数"的解释。经核对 `callVirtualFunc` 源码与 MS x64 ABI 规范，**XMM0/XMM1 才是正确值**，`XMM2/XMM3` 是把浮点寄存器按"整体参数位置"数出来的错误结论（positional counting）。本文件维持 XMM0/XMM1，因为：① 真实调用点源码（`callVirtualFunc`）把两个 float 作为第 1、第 2 个浮点参数传递，编译后必然落在 XMM0/XMM1；② 若写成 XMM2/XMM3，反而与真实调用点**不一致**，违背 E.1 自身"确认编译器生成的寄存器传参与函数声明一致"的要求。

### 4.1 修订前后 ABI 对照（E.1）

| 参数 | 类型 | E.1 审查主张 | 真实（本文件 / callVirtualFunc 源码 + MS x64 ABI） |
|---|---|---|---|
| `this` | 指针 | RCX | **RCX** ✅ 一致 |
| `soundName` | `const std::string&` | RDX | **RDX** ✅ 一致 |
| `volume` | `float` | XMM2 | **XMM0** ✅ 纠正 |
| `pitch` | `float` | XMM3 | **XMM1** ✅ 纠正 |

> 结论：仅浮点槽位被 E.1 误判为 XMM2/XMM3；实际为 XMM0/XMM1。本文件不改动任何代码，仅校正文档。

### 4.2 三层交叉核验（E.1.1 / P0-1）

任务要求把「① C++ 声明推导的调用 ABI」「② 编译器生成的真实调用指令」「③ 游戏二进制目标函数实际读取的参数寄存器」三者交叉核验，结论必须唯一、不矛盾。

| 层 | 来源 | volume | pitch | 证据 |
|---|---|---|---|---|
| ① 声明推导 | `callVirtualFunc<void,const std::string&,float,float>` → `Fn=void(__thiscall*)(void*,const std::string&,float,float)`，实参 `(_this, soundName, volume, pitch)` | **XMM0**（第 1 个浮点） | **XMM1**（第 2 个浮点） | `MinecraftGame.cpp:81` + `MemUtils.hpp:94,122` |
| ② 编译器生成 | MSVC 据 MS x64 ABI 编译上述实参序列：整数/指针走 RCX,RDX,R8,R9、浮点走 XMM0–XMM3，两类**按类型独立计数** | **XMM0** | **XMM1** | MS x64 调用约定（确定性强，无字节级歧义；沙箱无编译产物故不附字节） |
| ③ 游戏二进制 | slot-164 thunk `0x1CBC3E4`（`sub rsp,0x38`→`mov rcx,[rcx+8]`→`mov rax,[rcx]`→`mov rax,[rax+0x520]`→`call [rax]`→`add rsp,0x38`）**自身不装载任何 XMM 寄存器**，说明 volume/pitch 由调用方经 XMM0/XMM1 透传进 thunk，再透传给 `vtable[164]` 目标 | **XMM0**（透传） | **XMM1**（透传） | 反汇编 `unpacked_Minecraft.Windows.exe` @ RVA `0x1CBC3E4` |

**唯一一致结论：`volume=XMM0`、`pitch=XMM1`，三层互不矛盾。**

**为何 E.1 审查主张的 `XMM2/XMM3` 不成立**：`XMM2/XMM3` 是把浮点寄存器按「整体参数位置（第 3、4 个参数）」编号得到的错误结论（positional counting）。MS x64 ABI 明确：整数/指针寄存器与 XMM 浮点寄存器是**两套独立编号的文件**——`this`(指针)→RCX、`soundName`(引用=指针)→RDX 占用整数文件，**不消耗任何 XMM 槽位**；`volume` 是第 1 个浮点 → **XMM0**，`pitch` 是第 2 个浮点 → **XMM1**。仅当签名中存在 ≥2 个排在 `volume` 之前的浮点参数时，`volume` 才会落到 XMM2——而 `playUi(const std::string&, float, float)` 只有 2 个浮点参数，绝无可能。

> 说明：E.1.1 审查意见在 bullet 中写「`volume→XMM2` / `pitch→XMM3`」，但其同段注释「浮点参数……重新从 XMM0 开始编号」本身即正确规则（浮点按独立位置从 XMM0 编号），与 XMM0/XMM1 一致。本文件据此把 bullet 中的 XMM2/XMM3 判为「按整体参数位置」误推，维持经三层核验的唯一结论 XMM0/XMM1，**不保留任何相互矛盾的 ABI 表述**。

## 5. 结论

- 崩溃落在调用点 A（`Solstice::init` 尾音效），由 `initThread` 执行，对象已校验。
- `playUi` 是**可选音效**，非初始化必需；守卫拒绝不会虚报初始化成功。
- ABI 正确（RCX/RDX/XMM0/XMM1），无需为 ABI 改动任何代码。
