# 虚调用守卫审查 — Batch 3.6.2-E 第八工作组（669ce2c MinecraftGame.cpp）

## 1. 守卫代码（当前 `src/SDK/Minecraft/MinecraftGame.cpp:66-82`）

```cpp
void MinecraftGame::playUi(const std::string& soundName, float volume, float pitch)
{
    // SUSPECT in 1.21.120: a vtable slot that far out is a jump into unmapped memory.
    int index = OffsetProvider::MinecraftGame_playUi;
    if (index < 2 || index >= 4096) return;
    if (!MemUtils::isValidVirtualCall(this, index)) return;   // 3.6.2-D 新增
    MemUtils::callVirtualFunc<void, const std::string&, float, float>(index, this, soundName, volume, pitch);
}
```

## 2. isValidVirtualCall 真实检查范围（`src/Utils/MemUtils.hpp:130-144`）

- `object` 非空且 `index ∈ (0,4096)`；
- `isReadableRange(object, 8)` —— 读 vptr；
- `isReadableRange(vtable, 8)`；
- `slot = vtable + index*8`；`isReadableRange(slot, 8)`；
- `target = *slot`；`isPatchableCode(target, 1)` —— 目标是**可执行代码**。

**它覆盖**：坏 vtable 解引用、槽位指向不可读/非可执行页、对象可读但 vtable/slot 损坏。即同时拦掉「slot 164 对运行期版本指向错误函数」与「this 可读但脱钩」两类最相关情形。

**它不证明**（必须在文档中讲清，不得冒充根因修复）：
- 不证明 `target` 就是 `playUi`（正确函数）；
- 不证明对象生命周期有效（stale-but-readable 仍可能通过）；
- 不检查 `target` 是否**属于游戏模块**（理论上可执行的非游戏页也能过）。

## 3. 守卫失败后的行为

- 失败时 **静默 `return`**。由于 `playUi` 是**可选音效**（见 `playui-callsite-and-abi.md`），`Solstice::init` 早已在调用前完成初始化、`Initialized!` 在调用后打印——**守卫拒绝不会把初始化虚报为成功**，符合「不得静默 return 后把初始化标记为成功」的约束。
- 但按任务建议，建议**仅在实际拒绝时**输出明确拒绝原因（不每次调用刷日志），便于下次运行直接判定 `PLAYUI_GUARD_REJECTED_WITH_REASON`：
  ```cpp
  if (!MemUtils::isValidVirtualCall(this, index)) {
      spdlog::warn("[playUi] virtual-call guard rejected: this={:X} index={} vtable={:X} target={:X} tid={}",
                   (uintptr_t)this, index, *(uintptr_t*)this, /*target*/, GetCurrentThreadId());
      return;
  }
  ```
  此日志为普通代码路径（非 SEH 过滤上下文），使用 spdlog 安全；仅拒绝时触发，不刷屏。**本批未将此日志写入源**（保持最小改动），列为建议项，由构建机编译测试。

## 4. 审查结论

- 守卫逻辑正确、范围恰当，且因 `playUi` 可选，失败行为不损害初始化状态判定。
- 拒绝时必须能产出可解释的拒绝原因——建议在下一版补一条「仅拒绝时」的 warn 日志。
- 不得删 `playUi` 调用、不得盲改 slot 164——本批均未做。
