# 3.7 — SDK 移植白名单与分阶段计划

## 0. 结论速览

| 阶段 | 内容 | 判定 |
|---|---|---|
| S0 | 纯 `b32d185` 基线 | 无代码改动；构建/验收由构建机执行 |
| S1 | Batch 1/2/3 映射移植 | **BLOCKED** —— 依赖整整套映射状态机，不可独立移植 |
| S2 | MinecraftSim 失败重试（`411fa4e`） | **唯一可独立移植**；已作为独立提交应用到本分支，待构建验证 |
| S3 | BlockSource plausibility gate（`9229523`） | **DEFERRED** —— 57 个消费者未证明空值安全 |
| S4 | MinecraftGame 虚调用防御 / ExceptionHandler 诊断 | 分开评估后均 **不移植** |

**本轮不追求移植数量**。11 个候选中仅 1 个（`411fa4e`）满足"独立、有证据、可验证、不牵连非 SDK 代码"。

---

## 1. 稳定基线 `b32d185` 的实际能力（实测，非假设）

| 机制 | b32d185 | origin/main |
|---|---|---|
| `src/SDK/ResolveState.hpp` | **不存在** | 存在 |
| `ResolveStatus::Ok/Missing/Implausible/...` | **不存在** | 存在 |
| `rejectOffset()` / `entryStatus()` | **不存在** | 存在 |
| 全局 `usableOffset()` 门控 | **不存在** | 存在 |
| `DEFINE_INDEX_FIELD` 行为 | 扫描结果**直接写入** `static int`，无 plausibility gate | 状态机裁决后再写入 |
| `MemUtils::isValidVirtualCall` | **不存在** | 存在 |
| `InstanceLocator::isPlausibleObject` | 存在 | 存在 |
| `MinecraftGame_playUi` | operand 3 → 值 `3238145`，被 `index>=4096` 挡住 → **playUi 实际 inert** | operand 10 → slot 164（+ 后续 isValidVirtualCall 守卫） |
| `MinecraftSim.cpp` | 与 `411fa4e^` **逐字相同** | 含重试机制 |

`b32d185` 只有一个**文件内**的 `usableOffset(int offset)`（`MinecraftGame.cpp:21`，
`return offset > 0 && offset < 0x8000;`），与 `OffsetProvider` 的映射状态机无关。

## 2. 依赖链（本轮最关键的发现）

```
4b93323  ─┐
84e8160   ├─ 引入 ResolveState.hpp / ResolveStatus / rejectOffset / usableOffset()
9b8f7d8   │   + 重写 OffsetProvider.cpp/.hpp、SigManager.cpp/.hpp、CodeSection、
47284b7  ─┘     SigFuzz、Solstice.cpp(启动/DEBUG 放行)、ExceptionHandler、CMakeLists
                        │
                        │  （全部是 517646e 的祖先，git merge-base 已验证）
                        v
517646e  Batch-1  ──> 1d2c493 Batch-2 ──> 1ec0571 Batch-3
                        │
                        └──> 9229523 getBlockSource gate（S3）
```

`517646e^:OffsetProvider.hpp` 已含 `ResolveStatus::Missing`、`rejectOffset()`、
`#include <SDK/ResolveState.hpp>`、`usableOffset()`；`b32d185` 全无。

**因此**：把 517646e / 1d2c493 / 1ec0571 的签名 hunks 单独打到 `b32d185` 上，只有两种结果：

1. **上下文不匹配**而无法应用（这些 hunks 的上下文行含状态机成员，如
   `static inline std::unordered_map<std::string,long long> mSigImplausible;`）；
2. 手工适配后**语义退化**："UNVERIFIED 保持 rejected" 在旧宏里没有
   `rejectOffset` 承接，扫描到的垃圾值会被**直接当作有效 offset 使用** —— 这正是
   规格里明令禁止的「把 UNVERIFIED 伪装成 OK」。

Batch-1 注释里反复出现的「Consumers gate on `usableOffset()`」、
「`Actor::swing()` is inert via `callVirtualFunc` index guard」在 `b32d185` 上
**都不成立**。

## 3. 逐候选评估

### 3.1 Batch 1 — `517646e`（EXCLUDE）

- 唯一 confirmed fix：`MinecraftGame_playUi` operand `3 → 10`
  （1.21.120 hit `0x1CBC3E4` → disp32 `0x520` → slot 164；1.21.44 `0x003DF624` → `0x4C0` → slot 152）。
  证据是**充分**的，但恰恰因为这个修正在 `b32d185` 上会**把一条被安全挡住的调用激活**：
  旧值 `3238145` 被 `index >= 4096` 拒绝（不发声、不崩），改成 164 后守卫放行，
  而 `b32d185` **没有** `isValidVirtualCall` 防御 → 直接对 slot 164 做虚调用。
  这正是 3.6.2-D/E 调查的 playUi AV 来源，且规格明确「slot 164 不得盲目修改」。
  → **CRITICAL 回归风险，禁止移植。**
- 另外 5 个 "retired as UNVERIFIED" 依赖 `rejectOffset` 才能保持 rejected，
  旧宏下会退化成垃圾值直接使用 → **禁止移植。**

### 3.2 Batch 2 — `1d2c493` / `61ec623` / `0ece302` / `b28e1d2`

- `61ec623`（完整候选证据）、`0ece302`（报告格式）、`b28e1d2`（消费者/模块就绪度）
  是**非运行时证据资产**，作为 S1 将来重启时的证据库保留（REFERENCE_ONLY），
  不算"已移植功能"。
- `1d2c493` 的 3 个 STATIC VERIFIED（slot 318 wrapper、`GameSession_mEventCallback`
  0x50、`Actor_mDestroying`、`BlockSource_setBlock` slot 34）同样依赖状态机；
  其中 `ClientInstance_getBlockSource` 的新 slot-318 签名还与 `9229523` 的
  plausibility gate 耦合（见 3.4）。两个刻意保留的 UNVERIFIED 声明在旧宏下会失去
  rejection 语义。→ **EXCLUDE。**

### 3.3 Batch 3 — `1ec0571`（DEFER）

`BlockSource_mBuildHeight` 的 1.21.120 目标版本证据是**本轮最扎实的**：
BlockSource RTTI `.?AVBlockSource@@` → vtable RVA `0x921B490`（66 slots），
slot 38 为 `movzx eax, word ptr [rcx+0x38]; ret`，唯一命中 `0x5D3B130 → 0x38`，
且 1.21.44 有同构 `0x38/0x3A` 佐证（未照搬 RVA）。

该 hunk 是**纯签名替换**，在旧宏下能编译。但仍判 DEFER：
① 其注释「New status OK; STATIC VERIFIED」属于状态机语义；
② 唯一收益是 BlockESP，而规格 §六 规定「补齐消费者空指针保护前，不允许将 S3
直接纳入正式整合版本，完成后才允许构建验证 BlockESP 相关能力」；
③ 单独移植一个映射而不带状态机，收益低、风险非零。

### 3.4 Batch 3.5.5 — `9229523`（DEFER_BLOCKED）

`getBlockSource` 的离线证据很硬（ClientInstance 初始化器 `0x1945160` 把
`0xF0/0x108/0x110/0x118` 一起清零；541 个 `[reg+0x118]` 写入无一来自 BlockSource
vtable 安装器；73 处构造函数全部在栈上就地构造）。**问题不在证据，在消费者。**

`b32d185` 实测：

- **57 处** `getBlockSource()` 调用，分布在 **21 个文件**（历史统计为 55 处 / 49 处缺保护）；
- 宽松词法启发式下 28 处有检查证据、**29 处无**，且该启发式是**上界**（会把
  `AntiImmobile.cpp:63` 这类 `getBlockSource()->getBlock(...)` 直接链式解引用误判为有保护）；
- 典型无保护：`TestModule.cpp:132`、`AntiImmobile.cpp:63`、`AutoKick.cpp:77`、
  `InfiniteAura.cpp:66`（作为参数直接传入）、`AutoPath.cpp:342/363`、`Jesus.cpp:84/150`、
  `Regen.cpp`(10 处)、`BlockUtils.cpp`(10 处)、`BlockESP.cpp`(7 处)。

把「野指针当 `BlockSource*`」改成返回 `nullptr`，会把一次野 vtable 跳转变成
**跨 21 个文件的立即空解引用**。规格 §四 明确禁止。

**解除条件**（二选一，且必须作为独立批次单独评审/构建）：
- 为所有实际可达消费者补齐空值保护；或
- 用运行期配置证明这些路径在测试配置下不会执行。

在此之前 S3 **不纳入正式整合版本**。

### 3.5 Batch 3.5.5 — `411fa4e`（PORT ✅，S2）

唯一满足全部条件的候选：

- `git diff --quiet b32d185 411fa4e^ -- src/SDK/Minecraft/MinecraftSim.cpp` → **完全一致**，
  hunk 零漂移、可直接应用；
- 只改 1 个 SDK 文件，不碰 GUI/GL/ESP/Aura/事件系统/构建系统；
- 无 `ResolveState` / `MemUtils` / 状态机依赖；
- 规格明确授权「MinecraftSim 重试机制可以作为独立移植批次」。

**锁 / 退避 / 间隔 / 初始化时机复核：**

| 项 | 结论 |
|---|---|
| 锁 | `std::mutex sProbeMutex` 覆盖 `resolveGameSimOffset` 全函数；所有共享状态都在锁内访问 |
| 退避 | `kProbeRetryMs = 2000`，`GetTickCount64()` 计时，`sLastProbeTick` 为 `std::atomic<unsigned long long>`（relaxed） |
| 失败 | 缓存 `-1` **仅在 2000 ms 窗口内**为最终态，之后重新探测 → 满足「不得在首个失败后永久缓存 -1」 |
| 成功 | `cached >= 0` 永久返回（对象存活期间已定位的偏移不会失效） |
| 日志洪泛 | `sLayoutDumped`（普通 bool，仅在 mutex 内访问）保证 layout dump 一次性 |
| 初始化时机 | 首帧 `LevelInfo`/`Nametags` 的 `getSimTimer()` 早于世界创建；延迟重试正是修复目标 |
| RenderSim | `sGameSimOffset`/`sRenderSimOffset` 在成功与失败路径**成对**写入，重试窗口一致 |

### 3.6 Batch 3.6.2-D/E — `669ce2c` / `2429e6e`（分开评估）

| 改动 | 类型 | 判定 |
|---|---|---|
| `MinecraftGame::playUi` 加 `MemUtils::isValidVirtualCall` | SDK 防御 | **DEFER**：b32d185 无该函数，需连带移植 84e8160 的 MemUtils 改动；且基线 playUi 本就 inert，加它收益为零 |
| `ExceptionHandler` 原始故障上下文捕获（`669ce2c`） | **诊断** | **EXCLUDE**：非 SDK 功能，规格要求与守卫分开评估且禁止自动移植诊断 |
| `diagnosePlayUiRejection` + handler 空指针加固 + build provenance（`2429e6e`） | **诊断/构建身份** | **EXCLUDE**：依赖未移植的守卫；本轮目标正是停止叠加诊断 |

「不得为了让游戏不崩而静默跳过必需初始化操作」——本轮未新增任何静默跳过。

### 3.7 依赖根 — `4b93323/84e8160/9b8f7d8/47284b7`（待用户决策）

这是让 Batch-1/2/3 按设计工作的**唯一途径**，但它是一次 10+ 文件的大改，
且牵连 `src/Solstice.cpp`（启动流程 / DEBUG 人工放行）与 `ExceptionHandler.cpp`，
实质上等于把 `b32d185` 之后那批大改搬回来 —— 与 3.7「稳定优先、只做选择性 SDK
移植」直接冲突。它同时会改动映射判据（此前约定不修改判据）。

**不擅自应用。** 留给用户两个明确选项：

- **A（推荐）**：S1 保持暂缓，接受 `b32d185` 原始映射，本轮只交付 S2 →
  最易达成 `STABLE_BASELINE_RESTORED`。
- **B**：另立一个隔离批次单独移植整套状态机，有独立 S 阶段与验收，
  并明确接受 `Solstice.cpp` / `ExceptionHandler.cpp` 的连带改动。

---

## 4. 阶段计划与 diff 来源

| 阶段 | 代码动作 | 完整 Diff 来源 | 状态 |
|---|---|---|---|
| S0 | 无（纯基线） | `git diff b32d185..b32d185` = 空 | 分支起点已建立；构建/验收待构建机 |
| S1 | **不应用** | 候选：`git show 517646e`、`git show 1d2c493`、`git show 1ec0571` | BLOCKED，见 §2 |
| S2 | 应用 `411fa4e` 到 `MinecraftSim.cpp` | 见 §5 全文 | 已应用，待构建验证 |
| S3 | **不应用** | 候选：`git show 9229523` | DEFER_BLOCKED |
| S4 | **不应用** | 候选：`git show 669ce2c`、`git show 2429e6e` | EXCLUDE/DEFER |

每个阶段使用**相同**的游戏版本 1.21.120.0、游戏配置文件、注入流程、诊断条件、
场景与测试时长（见 `staged-validation-matrix.md`），避免条件变化造成错误归因。

## 5. S2 完整 Git Diff（本分支实际应用的唯一代码改动）

```diff
diff --git a/solstice/solstice-src/solstice-master-main/solstice-master-main/src/SDK/Minecraft/MinecraftSim.cpp b/solstice/solstice-src/solstice-master-main/solstice-master-main/src/SDK/Minecraft/MinecraftSim.cpp
index 88cd823..b819df9 100644
--- a/solstice/solstice-src/solstice-master-main/solstice-master-main/src/SDK/Minecraft/MinecraftSim.cpp
+++ b/solstice/solstice-src/solstice-master-main/solstice-master-main/src/SDK/Minecraft/MinecraftSim.cpp
@@ -48,8 +48,23 @@ constexpr int kDumpLast = 0x100;
 // MinecraftSim object.
 std::atomic<int> sGameSimOffset{ -2 };
 std::atomic<int> sRenderSimOffset{ -2 };
+std::atomic<unsigned long long> sLastProbeTick{ 0 };
+bool sLayoutDumped = false;
 std::mutex sProbeMutex;
 
+// A failed probe used to be final. The first caller is whichever module reads the
+// simulation clock first -- LevelInfo and Nametags both call getSimTimer() from
+// their RenderEvent, i.e. on the very first rendered frame -- and at that point
+// the world does not exist yet, so the paired Simulation pointers are not there.
+// The failure was cached as -1 for the rest of the session, and every later
+// setSimTimer() returned before writing anything. Timer, Speed, Fly, LongJump and
+// the animation swing rate were therefore inert with a probe that was working
+// correctly; it simply was not allowed to look again.
+//
+// Retry on a backoff instead of pinning a negative result. A success stays cached
+// forever, because a located offset cannot become wrong while the object lives.
+constexpr unsigned long long kProbeRetryMs = 2000;
+
 void dumpLayout(const void* self, size_t available)
 {
     const auto base = reinterpret_cast<uintptr_t>(self);
@@ -74,7 +89,17 @@ int resolveGameSimOffset(const void* self)
 {
     std::lock_guard<std::mutex> lock(sProbeMutex);
     const int cached = sGameSimOffset.load(std::memory_order_relaxed);
-    if (cached != -2) return cached;
+    // A located offset is permanent.
+    if (cached >= 0) return cached;
+    // A previous failure is only final for the backoff window: the Simulation
+    // pair appears once the world is created, which is later than the first
+    // caller. See kProbeRetryMs.
+    if (cached == -1)
+    {
+        const unsigned long long last = sLastProbeTick.load(std::memory_order_relaxed);
+        if (last != 0 && GetTickCount64() - last < kProbeRetryMs) return -1;
+    }
+    sLastProbeTick.store(GetTickCount64(), std::memory_order_relaxed);
 
     const auto base = reinterpret_cast<uintptr_t>(self);
     MEMORY_BASIC_INFORMATION mbi{};
@@ -110,9 +135,15 @@ int resolveGameSimOffset(const void* self)
         // the layout describes or the Simulation layout moved. Dump the fields
         // around the two offsets that are known independently (GameSession at
         // 0xD8 via its own signature, another object pointer at 0xF0 via the
-        // getter at 0x1A25A30) so the next round starts from data.
-        spdlog::warn("[sim] no Simulation pair in the first 0x{:X} bytes; known pointers:", kScanBytes);
-        dumpLayout(self, available);
+        // getter at 0x1A25A30) so the next round starts from data. Dump once:
+        // the probe now retries every kProbeRetryMs and would otherwise repeat
+        // this block for the whole session.
+        if (!sLayoutDumped)
+        {
+            sLayoutDumped = true;
+            spdlog::warn("[sim] no Simulation pair in the first 0x{:X} bytes; known pointers:", kScanBytes);
+            dumpLayout(self, available);
+        }
         sGameSimOffset.store(-1, std::memory_order_relaxed);
         sRenderSimOffset.store(-1, std::memory_order_relaxed);
         return -1;
```

> 复现：`git show 411fa4e -- src/SDK/Minecraft/MinecraftSim.cpp`。
> 应用方式：因 `b32d185` 该文件与 `411fa4e^` 完全一致，直接采用 `411fa4e` 版本文件内容
> （等价于精确应用该提交的唯一 hunk），未 cherry-pick 任何历史提交。

## 6. 证据等级

- 本文件全部结论为**静态 git/源码证据**：提交关系、文件内容、依赖符号、调用点统计。
- `runtimeVerified` 在 manifest 中**全部为 false**。
- 编译、注入、进入世界、持续运行等验收由构建机执行；本机无 MSVC/ninja/g++，
  目标 EXE 与 `CMakeCache.txt` 不在本机，**本轮未产生编译产物或运行日志**。
- 不把"静态有 1.21.120 证据"等同于"运行期已验证"。
