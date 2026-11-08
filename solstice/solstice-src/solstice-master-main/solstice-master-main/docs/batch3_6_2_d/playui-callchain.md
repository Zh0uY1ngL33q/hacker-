# MinecraftGame::playUi 调用链追踪 — Batch 3.6.2-D 第二工作组

## 1. 完整调用链（静态追踪）

```
Solstice::init                          (src/Solstice.cpp:377)
  └─ ClientInstance::get()              (静态单例入口)
       └─ ClientInstance::getMinecraftGame()   (src/SDK/Minecraft/ClientInstance.cpp:159)
            └─ MinecraftGame::getInstance()    (单例)
                 └─ InstanceLocator::locateGame()
                      └─ InstanceLocator::locate(&game, nullptr)
                           └─ isPlausibleGame(game)   (校验对象)
            └─ MinecraftGame::playUi("beacon.activate", 1, 1.0f)  (src/SDK/Minecraft/MinecraftGame.cpp:66)
                 └─ index 范围守卫
                 └─ MemUtils::isValidVirtualCall(this, index)   ← 3.6.2-D 新增
                 └─ MemUtils::callVirtualFunc<void,...>(index, this, ...)  (vtable[index] __thiscall)
```

第二个调用点：`src/Solstice.cpp:499`
```cpp
if (auto* game = ci->getMinecraftGame(); InstanceLocator::isPlausibleGame(game))
    game->playUi(...);
```

## 2. 直接调用 vs callVirtualFunc（虚表分发）

`MinecraftGame::playUi` 是我们 SDK 包装类的成员函数；它**不直接**持有游戏原函数指针，
而是进入 `playUi` 后通过 `MemUtils::callVirtualFunc<void, const std::string&, float, float>(index, this, ...)`
在**真实游戏对象 `this` 的 vtable** 上做虚表分发（`vtable[index]`）。
因此本质是：对我们定位到的真实 `MinecraftGame` 对象做一次 `__thiscall` 虚调用，
而不是一个写死的裸函数指针调用。

## 3. 哪个 MinecraftGame 对象

`getMinecraftGame()` → `MinecraftGame::getInstance()` → `InstanceLocator::locateGame()`
返回的是**真实游戏进程里的 `MinecraftGame` 单例**（经堆扫描定位并校验）。
`playUi` 收到的 `this` 就是该真实对象，`isPlausibleGame` 已在校验链上确认其「看起来是一个合理的游戏对象」。

## 4. InstanceLocator 路径

- `getInstance()` → `locateGame()` → `locate(&game, nullptr)`（`src/SDK/Minecraft/InstanceLocator.cpp:423-540`）
- `locate`：堆扫描 + 结果缓存 + 重新校验。
- `isPlausibleGame(game)`（:418）：`return firstClientFromGame(game, OffsetProvider::MinecraftGame_mClientInstances) != nullptr;`
  即通过一个客户端实例字段来确认对象结构合理。

## 5. 初始化阶段 / 线程 / ABI

- **阶段**：`Solstice::init` 内、映射校验放行之后、主循环之前（init 收尾的 beacon 提示音）。
- **线程**：崩溃栈显示为 `initThread`（见第六工作组，与 GL 渲染线程 `tid=0x1c20` 不同线程）。
- **ABI（E.1.2.1）**：`__thiscall` 在 x64 被忽略，使用默认 Windows x64 ABI。`callVirtualFunc` 中 `Fn = Ret(__thiscall*)(void*, Args...)`，`vtable[index](_this, args...)`。隐式 `this` 是第一个参数；XMM 编号对应参数在完整参数列表中的序位（E.1.2.1 用户裁定）。
  参数布局（层 ① 类型推导）：`this=RCX`、`const std::string& soundName=RDX`、`float volume=XMM2`（参数序位第 3）、`float pitch=XMM3`（参数序位第 4），返回 `void`。
  与任务 §二 最初请求的 `RCX / RDX / XMM2 / XMM3` 一致。游戏二进制 caller/target 寄存器读取（层 ②③）仍标 **BINARY_ABI_CORROBORATION_PENDING**。详见 `../batch3_6_2_e/playui-callsite-and-abi.md` §4 三层交叉核验与 `corrections-E1.2.1.md`（E.1.1 曾订正原 D 文档误写 `volume=R8/pitch=R9`；E.1.1/E.1.2 一度维持 XMM0/XMM1，被 E.1.2.1 修订为 XMM2/XMM3）。

## 6. 小结

调用链本身是「标准且正确」的：对象先经 `isPlausibleGame` 校验，再经范围守卫，再虚调用。
问题不在「调了错误对象」或「ABI 写错」，而在虚调用落点（slot 164）对于**运行中的实际游戏版本**
是否仍指向正确的函数——这是第四工作组 slot164 分析的核心。
