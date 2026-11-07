# 最终决策（Final Decision）— Batch 3.6.2-A

分支：`feat/aura-isolation-hang-diagnosis`（自 `bf1522f` 切出，未改 main）。
目标：将 Aura 执行路径干净隔离，利用现有 DEBUG 人工放行做卡死回归对照实验。不全项目回退、不盲目修 Aura、不改动共享层。

## 最终状态

```
AURA_ISOLATED_READY_FOR_DEBUG_TEST
```

（隔离已实现并通过静态证据核验；运行期卡死对照实验由构建机在开启 `SOLSTICE_DISABLE_AURA_DIAGNOSTIC=ON` 的构建上执行。若运行中仍卡死则更新为 `HANG_PERSISTS_OUTSIDE_AURA`。）

## 验收问答

### 1. Aura 的完整执行范围是什么？
- **类**：`Combat/Aura`（`ModuleBase<Aura>`，默认 `enabled=false`）。
- **生命周期回调**：构造函数、`onEnable`、`onDisable`、`onBaseTickEvent`、`onRenderEvent`、`onPacketOutEvent`、`onPacketInEvent`、`onBobHurtEvent`、`onBoneRenderEvent`。
- **专属辅助方法**：`getSword`、`shouldUseFireSword`、`rotate`、`shootBow`、`throwProjectiles`、`findObstructingActor`。
- **文件局部符号**：`EaseInOutExpo`（自由函数）、`chargingBow`（全局 bool=false，平凡初始化）。
- **静态成员**：`sHasTarget / sTarget / sTargetRuntimeID`（头文件 inline，被 TargetStrafe/AutoBoombox/AutoSpellBook/Scaffold/TargetHUD 只读引用，非 Aura 代码）。
- **事件/Packet 监听（仅在 `onEnable` 注册）**：BaseTickEvent、PacketOutEvent、PacketInEvent、RenderEvent、BobHurtEvent(FIRST)、BoneRenderEvent(FIRST)。**Aura 不安装任何 MinHook 钩子。**
- **唯一注册点**：`ModuleManager::init()` 的 `mModules.emplace_back(std::make_shared<Aura>());`（全库唯一实例化处）。
- **非独立类**：代码库中**不存在** `TargetSelector` / `Rotation` 类（选敌内联、旋转走共享 `MathUtils::getRots`）；`InfiniteAura`/`ChestAura`/`LegitCriticals` 为独立模块，不在本任务隔离范围。

### 2. 是否实现了真正的构造和注册隔离？
是。在唯一注册点用 `#ifndef SOLSTICE_DISABLE_AURA_DIAGNOSTIC` 包裹 `make_shared<Aura>()`：
- 宏定义时：Aura 实例不创建、不进 `mModules` 注册表、`getModule<Aura>()` 返回 null、`onEnable` 永不被调用，从而 6 个 dispatcher 监听与全部攻击/旋转/可视化逻辑都不执行。
- 经核查 `registerModule()` 接口存在但从未被调用，实际注册均经 `mModules.emplace_back`；全库仅此一处 `make_shared<Aura>()`。
- 这超越了 `defaultEnabled=false`（后者实例仍存在、仍在注册表、指针非 null）；本方案从构造与注册根路径移除。

### 3. 是否保留了所有其他模块？
是。除 Aura 的注册行外，其余全部 `make_shared<...>()` 未动；`InfiniteAura`/`ChestAura`/`LegitCriticals` 等 Combat/Player 模块原样保留。仅 `Scaffold.cpp` 加 `auraMod &&` 空指针守卫（隔离后 `getModule<Aura>()` 返回 null 的防御，Aura 存在时零行为变化），以保证其他模块在 Aura 缺席时仍正常工作。

### 4. 是否保留原始 DEBUG 人工放行？
是。`Solstice.cpp` 中 `SOLSTICE_ALLOW_UNVERIFIED_MAPPINGS` 触发的 `Type 'DEBUG' to continue, or press ENTER to exit.` 逻辑完全未触碰，放行交互原样保留。

### 5. 隔离后游戏是否仍然卡死？
待构建机运行期实验（见 `hang-comparison.md`）。代码侧隔离已完成并静态核验通过。本交付物状态为"隔离已实现、待 DEBUG 对照测试"。

### 6. 如果卡死，实际阻塞线程在哪里？
待运行期 Hang Dump（两份完整 dump，不依赖 crash.log）。若仍卡死，按第八工作组从其他模块构造函数 / HookManager / MinHook 线程冻结 / Dispatcher / 初始化锁 / 同步对象扫描继续定位，不扩大禁用 Aura 范围、不禁用全部 Combat。

### 7. 是否有必要单独恢复 Aura 专属代码？
待对照实验结论。当前**不恢复**。仅当隔离构建"不卡死"且基线"卡死"时，进入第二阶段：比较 `f8c3541` 与当前版本，按"最小 Diff + 仅 Aura 专属 + 有明确证据"恢复（见 `selective-rollback-proposal.md`），禁止整体回退/覆盖共享 SDK/ESP 修复。

### 8. ESP 诊断是否能够继续推进？
隔离构建不影响 `ESP.cpp` 及诊断探针代码（均未改动）。若隔离后卡死消失，ESP 诊断可在无 Aura 干扰的执行路径下继续推进；若仍卡死，ESP 诊断同样需在 Hang Dump 定位后再继续。

## 交付物清单
- `aura-execution-inventory.json` — Aura 执行范围清单（含外部静态读者、共享依赖、独立模块边界）。
- `aura-isolation-diff.md` — 三处最小改动的实际 diff 与理由。
- `initialization-impact.md` — 宏 ON/OFF 对初始化的影响分析。
- `hang-comparison.md` — 构建机对照实验协议与观测矩阵（结果待填）。
- `selective-rollback-proposal.md` — 第二阶段选择性恢复预案（未执行）。
- `final-decision.md` — 本文件。

## 约束遵守情况
- ✅ 从 `bf1522f` 创建独立分支，未改 main、未 force push、未删提交。
- ✅ 保留 Batch 1/2/3 映射修复、3.5 SDK、3.6 ESP 诊断、3.6.1a/b/c 探针。
- ✅ 未改默认映射拒绝机制 / 全局 DEBUG 放行 / 其他模块默认启用状态 / 其他视觉模块 / 共享层。
- ✅ 隔离选项默认 OFF，不改变正式构建默认行为。
- ✅ 未用 `defaultEnabled=false` 冒充隔离。
