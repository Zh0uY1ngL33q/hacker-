# 最小修复总结 — Batch 3.6.2-D 第七工作组

原则：**只做有直接证据支撑的最小改动**；不删 playUi 调用、不盲改映射、不回退已有成果。

## 1. 改动一：崩溃故障上下文捕获（`src/Utils/SysUtils/ExceptionHandler.cpp`）

在构造崩溃文本前，从 `EXCEPTION_POINTERS` 提取并输出：
- `ExceptionAddress`、`AccessType`（读/写/执行）、`FaultAddress`、`NumberParameters`；
- 寄存器 `RIP/RSP/RBP/RCX/RDX/R8/R9/RAX`。

目的：让**下一次**构建复现时，崩溃日志能精确分类 AV（坏虚表解引用 vs 跳进无效代码），
而不仅是一条栈回溯。正常运行零行为变化，只丰富崩溃输出。

## 2. 改动二：playUi 虚调用前加 `isValidVirtualCall` 闸门（`src/SDK/Minecraft/MinecraftGame.cpp`）

```cpp
int index = OffsetProvider::MinecraftGame_playUi;
if (index < 2 || index >= 4096)
    return;
// 3.6.2-D: 跳虚表前，确认对象、其 vtable、slot 目标都是真实可执行代码。
if (!MemUtils::isValidVirtualCall(this, index))
    return;
MemUtils::callVirtualFunc<void, const std::string&, float, float>(index, this, soundName, volume, pitch);
```

`isValidVirtualCall`（MemUtils.hpp:130-144）检查：对象可读、vtable 可读、slot 可读、
且目标是可执行代码（`isPatchableCode(target, 1)`）。它同时拦掉两类与本次 AV 最相关的情形：
- (a) slot 164 对运行期游戏版本指向了错误函数；
- (b) `this` 可读但已脱钩/失效。

**且不改变任何映射、不删除 playUi 调用。**

## 3. 明确未做的事（遵守任务约束）

- ❌ 未删除 `playUi` 调用（不靠掩盖症状绕过）。
- ❌ 未把 slot 164 改成其他数值（盲改无效且更危险，见 `slot164-runtime-analysis.md`）。
- ❌ 未随机改动其他映射。
- ❌ 未触碰 `OpenGLGui` / `OWorldToScreen` / `FrameTransform` / `FOV`。
- ❌ 未批量禁用可视化模块。
- ❌ 未回退 Batch 1-3 映射、3.5 SDK、3.6 ESP 探针、3.6.1a/b/c、Aura 隔离、GL 三阶段诊断、DEBUG 放行。

## 4. 验证

- 两处编辑经 Grep 确认已写入。
- 括号/花括号配平经 PowerShell 计数核对（ExceptionHandler.cpp 29/29、77/77；MinecraftGame.cpp 8/8、31/31）。

## 5. 修复定位

这是**防御性 + 诊断性**修复，而非「已确认并消除根因」。它把「会崩」变成「安全跳过并在下次构建给出精确故障上下文」，
把根因判定权交给下一次复现的捕获数据。
