# 3.6.2-F — MinecraftGame 对象问题独立审查（工作组五）

## 现象（独立问题，不与 RenderEvent 卡死混谈）

```
[playUi] guard rejected: vtable pointer unreadable
    this=0X13134090  index=164  vtable=0X3573F2CD48F1B9AF  threadId=21392
```

而 RenderEvent 线程为 `tid=0x34d8`。**两者不同线程**，属独立问题。

## 守卫逻辑（`src/SDK/Minecraft/MinecraftGame.cpp:35-75`）

`diagnosePlayUiRejection()` 的分支顺序：

1. `this == null` → 拒绝。
2. `isReadableRange(this, 8)` 失败 → "object memory unreadable"。
3. 读 `vtable = *this`（首 8 字节），`isReadableRange(vtable, 8)` 失败 → **"vtable pointer unreadable"**（本案命中）。
4. 否则继续校验 slot / target 可执行性。

本案命中第 3 步：`0x13134090` 自身**可读**（`isReadableRange(this,8)` 通过），但其首
8 字节（`vtable` 字段）指向 `0X3573F2CD48F1B9AF`，而该地址**不可读**。

## 为什么"通过定位但 vtable 不可读"

`MinecraftGame::getInstance()`（`MinecraftGame.cpp:78-84`）：

```cpp
auto* game = InstanceLocator::locateGame();
return InstanceLocator::isPlausibleGame(game) ? game : nullptr;
```

`InstanceLocator` 的候选来源与校验（`src/SDK/Minecraft/InstanceLocator.cpp`）：

- **候选来源**：优先用缓存的 `cachedGame`，走 cheap path；仅当无缓存且距上次全扫描
  超过 10s 才做全堆扫描（`locate()` 的 throttle，见 483-484）。
- **`isPlausibleGame` 条件**（`InstanceLocator.cpp:418-421`）：
  `firstClientFromGame(game, OffsetProvider::MinecraftGame_mClientInstances) != nullptr`
  —— 即该 game 的 client 映射（在配置的偏移处）能取出至少一个 `ClientInstance`。
- **关键**：`locate()` 的 cheap path 重新校验的是 **`isReadable(known, 0x40)`**
  （可读性），而非"首字节是否为 vtable"。源码注释明确写道（462-463 行）：
  *"A MinecraftGame need not start with a vtable, so revalidation is readability,
  not isPlausibleObject."*

因此：一个候选只要"首 0x40 字节可读 + 其 client 映射能取出 client"，就会被
`isPlausibleGame` 接受并返回。**它并不要求 offset 0 处是合法 vtable**。而 `playUi`
自己的守卫却把 offset 0 当作 vtable 来读 → 该 qword 指向不可读地址 → 拒绝。

结论：对象通过了"定位/可读性/有 client"的宽松判定，但其 offset 0 的 vtable 字段
确实不可读——二者判定口径不同，并非矛盾，而是 `InstanceLocator` 对 game 对象**不
以 vtable 作为身份判据**所致。

## `0x13134090` 是否可能是错误对象起始

`vtable` 字段值 `0X3573F2CD48F1B9AF` 是不可读的高地址。几种可能（无运行时 dump 不能
确定哪一种）：

- **(a) 失效/已释放指针**：对象被释放后首 8 字节变成空闲链表指针或填充值，恰好不可读。
- **(b) 对象起始解析错误**：`locateGame` 取到的 `game` 并非真正 `MinecraftGame` 的
  起始，而是从某结构中间读到的地址（offset 偏移错误 / 配置偏移 `MinecraftGame_*` 不可信）。
- **(c) 实为其他类型**：首字段本就不是 vtable。

注意：**本守卫是安全拒绝，未发生 AV。** 它只是把这次 `playUi` 调用挡下，不会崩溃，也
不参与 RenderEvent 卡死。其存在的意义正是防止在坏对象上做 vtable 调用。

## 对象生命周期

`locate()` 使用 `cachedGame` / `cachedClient`（`atomic`，带 `memory_order`），并在每次
调用时重新校验。若 `0x13134090` 是缓存的 game 指针，而该 game 内存在之后变得不可读
（卸载/释放/页换出），就会出现"之前能通过、现在 vtable 不可读"。要确认需运行时 dump
（`!address 0x13134090`、`!heap -p -a 0x13134090`、或比对 `isPlausibleGame` 当时取到的
client）。

## 本轮处理

- **不修改** `MinecraftGame_playUi`（守卫工作正常，拒绝即正确行为）。
- **不修改** slot 164（ABI 已由 3.6.2-E.1.2.1 裁定为 `RCX/RDX/XMM2/XMM3`，与本拒绝无关；
  此处 `index=164` 是守卫在验证的 vtable slot 序号，不是调用约定）。
- **不修改** `InstanceLocator` 架构。
- 标记为**独立问题**：与 RenderEvent 派发故障无因果关联（不同线程；且本身被安全拒绝）。
- 若后续要收敛，需要运行期 dump 把 `0x13134090` 与 `isPlausibleGame` 当时命中的 client
  逐一核对（对应历史 O4：运行 EXE SHA 比对 + 对象身份溯源）。
