# MinecraftGame 对象校验 — Batch 3.6.2-D 第三工作组

## 1. 对象是怎么被校验的

`playUi` 调用前，对象已经过一轮校验链：

```
ClientInstance::getMinecraftGame()           (ClientInstance.cpp:159)
  → MinecraftGame::getInstance()
      → InstanceLocator::locateGame()         (InstanceLocator.cpp:542)
          → locate(&game, nullptr)            (InstanceLocator.cpp:423-540)
              → isPlausibleGame(game)         (InstanceLocator.cpp:418)
```

`isPlausibleGame` 的实现：
```cpp
return firstClientFromGame(game, OffsetProvider::MinecraftGame_mClientInstances) != nullptr;
```
即：从 `game` 对象按字段偏移 `MinecraftGame_mClientInstances` 取出客户端实例链表头，
能取到非空客户端实例才算「看起来像个合理的游戏对象」。

`locate()` 还做了堆扫描 + 结果缓存 + 重新校验，所以传给 `playUi` 的 `this`
不是随便一个指针，而是被结构校验过的真实对象。

## 2. 校验的边界：必要但不充分

`isPlausibleGame` 是**结构性 plausibility 检查**，不是「对象一定存活 / vtable 一定匹配当前游戏版本」的保证。
以下两种情况都能通过 `isPlausibleGame`，却仍可能在虚调用里崩：

1. **版本错配的 vtable 槽位**：对象结构合理（字段还在），但运行中的
   `Minecraft.Windows.exe` 版本与当初扫描签名的版本不同，导致 `vtable[164]`
   指向了另一个虚函数——调用会跳进错误/垃圾代码。
2. **可读但已脱钩的 `this`**：对象内存仍可读、结构仍像样，但语义上已失效
   （例如单例缓存未及时失效），虚表或字段内容不再可信。

## 3. 3.6.2-D 的加固

为补齐「校验不充分」的缺口，在 `MinecraftGame::playUi` 虚调用前新增
`MemUtils::isValidVirtualCall(this, index)` 闸门（见 `minimal-fix-summary.md`）：
它额外检查对象可读、vtable 可读、slot 目标可读、且目标是可执行代码
（`isPatchableCode(target, 1)`）。这覆盖了上面两类「对象看似合理却仍会崩」的情形，
而**不改变任何映射、不删除调用**。

## 4. 结论

- 对象校验链确实存在且能拦掉「完全无效指针」这类最明显的问题。
- 但它拦不掉「版本错配槽位」和「可读但脱钩对象」这两类与本次 AV 最相关的情形。
- 因此本次最小修复把判定从「对象合理」提升到「对象 + vtable + slot 目标全部真实可执行」，
  与任务「不删调用、不盲改映射」的约束一致。
