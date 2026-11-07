# Aura 隔离构建 Diff

任务：Batch 3.6.2-A。分支 `feat/aura-isolation-hang-diagnosis`，基线 `bf1522f`。
目标：通过独立的诊断构建选项 `SOLSTICE_DISABLE_AURA_DIAGNOSTIC`（默认 OFF）彻底隔离 Aura 的执行路径，用于卡死回归对照实验。不修改 main、不修改默认映射拒绝机制、不修改全局 DEBUG 人工放行、不修改共享 SDK。

## 改动总览（3 处，均为最小改动）

| # | 文件 | 改动 | 性质 |
|---|------|------|------|
| 1 | `CMakeLists.txt` | 新增 `option(SOLSTICE_DISABLE_AURA_DIAGNOSTIC ... OFF)` + `add_compile_definitions` | 诊断构建开关，默认 OFF |
| 2 | `src/Features/Modules/ModuleManager.cpp` | `ModuleManager::init()` 中用 `#ifndef SOLSTICE_DISABLE_AURA_DIAGNOSTIC` 包裹 `make_shared<Aura>()` | 隔离主切入点 |
| 3 | `src/Features/Modules/Player/Scaffold.cpp` | 两处 `auraMod->sHasTarget` 前加 `auraMod &&` 空指针守卫 | 隔离后 `getModule<Aura>()` 返回 nullptr 的防御，Aura 存在时行为不变 |

## 实际 Diff

### 1) CMakeLists.txt
```diff
 if (SOLSTICE_ALLOW_UNVERIFIED_MAPPINGS)
     add_compile_definitions(SOLSTICE_ALLOW_UNVERIFIED_MAPPINGS)
 endif ()
+
+# Diagnostic-only build option. When ON, the Aura module is never constructed or
+# registered, so its entire execution path (constructor, onEnable/onDisable/onTick,
+# event and packet listeners registered through the shared dispatcher) is absent
+# from the running process. Used to test whether Aura is the cause of a startup
+# hang regression. Default OFF keeps the released binary identical; every other
+# module, the shared SDK, and the global DEBUG override are untouched.
+option(SOLSTICE_DISABLE_AURA_DIAGNOSTIC
+       "Build without the Aura module to isolate a suspected hang regression (diagnostics only)" OFF)
+if (SOLSTICE_DISABLE_AURA_DIAGNOSTIC)
+    add_compile_definitions(SOLSTICE_DISABLE_AURA_DIAGNOSTIC)
+endif ()
```

### 2) ModuleManager.cpp（ModuleManager::init()）
```diff
     // Combat
+#ifndef SOLSTICE_DISABLE_AURA_DIAGNOSTIC
+    // Diagnostic isolation: when SOLSTICE_DISABLE_AURA_DIAGNOSTIC is defined, Aura is
+    // never constructed or added to the registry. Because Aura registers all of its
+    // event/packet listeners in onEnable() (and removes them in onDisable()), and it
+    // installs no exclusive MinHook detours, not constructing it removes the whole
+    // Aura execution path. Other modules below are unaffected.
     mModules.emplace_back(std::make_shared<Aura>());
+#endif
     mModules.emplace_back(std::make_shared<TriggerBot>());
```

### 3) Scaffold.cpp（onPacketOutEvent）
```diff
             auto auraMod = gFeatureManager->mModuleManager->getModule<Aura>();

-            if (auraMod->sHasTarget && mFlickMode.mValue == FlickMode::Combat || mFlickMode.mValue == FlickMode::Always) flickRotate = true;
+            if (auraMod && auraMod->sHasTarget && mFlickMode.mValue == FlickMode::Combat || mFlickMode.mValue == FlickMode::Always) flickRotate = true;
...
-            if (auraMod->sHasTarget && mFlickMode.mValue == FlickMode::None)
+            if (auraMod && auraMod->sHasTarget && mFlickMode.mValue == FlickMode::None)
```

## 为什么这一处注册点守卫就足够

- `make_shared<Aura>()` 是全代码库**唯一**实例化 Aura 的语句（`registerModule()` 接口存在但从未被调用，实际注册都用 `mModules.emplace_back`）。
- Aura 的全部事件/Packet 监听（BaseTickEvent / PacketOutEvent / PacketInEvent / RenderEvent / BobHurtEvent / BoneRenderEvent）**只在 `onEnable()` 通过共享 dispatcher 注册**，`onDisable()` 注销；Aura 不安装任何 MinHook 钩子。
- 因此不构造 Aura → 不在 `mModules` 注册表 → 永不 `onEnable` → 所有回调/监听/攻击逻辑都不执行。无需改动 Aura 自身代码，也无需改动共享 SDK。
- 这超越了"仅把 defaultEnabled 改为 false"：defaultEnabled=false 只是默认不启用，实例仍然创建、Aura 仍在注册表（其静态成员/类型参与 ODR、其他模块 `getModule("Aura")` 仍非 null）。本方案从实际构造与注册路径根除了 Aura 的执行。

## 为什么必须改动 Scaffold（且只是空指针守卫）

隔离后 `gFeatureManager->mModuleManager->getModule<Aura>()` 返回 `nullptr`。
- `Scaffold.cpp` 原本 `auraMod->sHasTarget` **无空判断**，隔离后会空解引用崩溃。
- 补 `auraMod &&` 后：Aura 存在时 `auraMod` 非 null，行为与原版完全一致；Aura 缺席时短路为 false，走既有默认旋转逻辑，不崩溃。
- 这是隔离生效、对照实验构建可运行的前提，属于"让其他模块在 Aura 缺席时仍能正常工作"的必要防御，不改变其他模块默认启用状态，不触碰视觉模块。
- 其余读取 `Aura::sTarget/sHasTarget` 的模块（TargetStrafe/AutoBoombox/TargetHUD）直接读 inline 静态成员（始终存在、默认 false/nullptr）且都有状态守卫；AutoSpellBook 已是 `if(!aura || ...)` 空判断。故无需改动。

## 不在本任务隔离范围（已确认）

- `InfiniteAura`（Combat/InfiniteAura）— 独立 `ModuleBase<InfiniteAura>`，与 Aura 无继承。
- `ChestAura`（Player/ChestAura）— 独立 `ModuleBase<ChestAura>`，与 Aura 无继承。
- `LegitCriticals`（Combat）— 独立模块，Aura 仅 include 其头文件、无调用。
- 任务点名的 `TargetSelector` / `Rotation` 在代码库中**不存在独立类**：选敌内联在 `onBaseTickEvent`（ActorUtils::getActorList + 排序 + 距离/可见性判定），旋转走共享 `MathUtils::getRots`。无可额外隔离的辅助类。
