# 初始化影响分析（Initialization Impact）

分析 `SOLSTICE_DISABLE_AURA_DIAGNOSTIC=ON` 时，对启动/初始化的影响。对比基线：`bf1522f`（未定义该宏）。

## 宏未定义（默认 / 正式构建）

- `ModuleManager::init()` 执行 `make_shared<Aura>()`，Aura 实例进入 `mModules`，注册表含 "Aura"。
- Aura 默认 `enabled=false`，故 `onEnable()` 不在启动期调用，但其类型/静态成员存在，`getModule<Aura>()` 返回非 null。
- 其他模块（含 Scaffold）读取 `Aura::sHasTarget/sTarget` 或 `getModule<Aura>()->sHasTarget` 均正常。

## 宏定义（诊断隔离构建）

### 模块注册阶段
- `ModuleManager::init()` **跳过** `make_shared<Aura>()`：
  - Aura 实例不被创建（构造函数不执行）。
  - Aura 不进入 `mModules` 注册表 → `getModule("Aura")/getModule<Aura>()` 返回 `nullptr`。
  - `onEnable/onDisable/onTick/onRenderEvent/onPacket*Event/onBobHurtEvent/onBoneRenderEvent` 永不被调用。
  - Aura 的 6 个 dispatcher 监听永不在 `onEnable` 注册，也无 `onDisable` 注销（无需注销，因为从未注册）。
- 其余所有 `make_shared<...>()` 不变，全部照常注册。

### Aura 静态成员（头文件 inline 静态）
- `sHasTarget=false / sTarget=nullptr / sTargetRuntimeID=0` 保持默认值。
- 这些符号由 `Aura.hpp` 声明；TargetStrafe/AutoBoombox/TargetHUD 仍 include 该头文件并读取，值为默认，各读取点均有 `if(!Aura::sHasTarget ...) return/跳过` 守卫 → 安全 no-op。
- `Aura.cpp` 仍参与编译（未从构建移除），但其方法不再被任何执行路径调用；文件作用域 `chargingBow=false` 为平凡初始化，无副作用。

### 依赖 Aura 指针的模块
- **Scaffold**：`getModule<Aura>()` 返回 null，已加 `auraMod &&` 守卫 → 不崩溃，走默认旋转逻辑。
- **AutoSpellBook**：原本 `if(!aura || !aura->sHasTarget) return;` 已空判断 → 安全。
- 其余模块直接读 inline 静态成员 → 安全。

### 共享层 / 其他系统（均不改动、不受影响）
- `OffsetProvider / SigManager / ResolveTable / InstanceLocator / ClientInstance / MinecraftSim`：未触碰。
- `RenderEvent Dispatcher / OpenGLGui / OWorldToScreen / FrameTransform`：未触碰。
- `ESP.cpp` 及诊断探针：未触碰。
- `FeatureManager::init` / `HookManager` / MinHook：未触碰；Aura 本就不安装任何钩子。
- 全局 DEBUG 人工放行（`SOLSTICE_ALLOW_UNVERIFIED_MAPPINGS` 逻辑）：未触碰。
- 默认映射拒绝机制：未触碰。

## 结论

诊断构建与正式构建的**唯一语义差异**是："Aura 模块不存在于运行实例中"。所有其他模块、共享 SDK、初始化流程、DEBUG 放行机制均逐字节等价（宏默认 OFF，正式构建行为不变）。Scaffold 的空指针守卫是隔离后能运行对照实验的必要且最小的附加修复，对 Aura 存在时零行为变化。

## 验证方法（静态）
- 见 `final-decision.md` 第六工作组静态证据清单；运行期对照见 `hang-comparison.md`。
