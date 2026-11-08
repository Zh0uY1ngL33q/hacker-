# Batch 3.6.2-F — Final Decision

## 1. 最终状态

```text
NEEDS_RUNTIME_CAPTURE
```

**理由**：本轮只取得静态源码证据 + 交付了订阅者级诊断能力，**没有**运行期
`BEGIN/END` 日志、**没有** Dump / WER 异常上下文。按 §十三「如果没有 Dump：
不能宣称找到了具体故障指令」「如果无法证明根因：停止修改，不扩大禁用范围」，
不得收口为 `RENDER_SUBSCRIBER_IDENTIFIED` 或 `FIRST_UNFINISHED_SUBSCRIBER`。

针对用户给出的旧日志「停在 `HUD_DISPATCH_BEGIN` 且无任何订阅者记录」，本轮已按
停止条件**优先检查了 Dispatcher 自身 / 锁 / 日志路径**（工作组一，见 §3），
**没有猜测具体模块**。静态检查确实发现 `trigger()` 存在 shared_lock→unique_lock
升级死锁风险（问题 7/8），但**无证据表明本次命中**，故也不标记
`DISPATCHER_INTERNAL_FAILURE`。

## 2. 基线确认（§二 / §十一）

| 项 | 值 |
|---|---|
| 基线 | `794fc65`；`git fetch origin --dry-run` 为空，**无后续提交** |
| 当前分支 | `feat/gl-render-hang-root-cause`（按项目工作流，不切换分支） |
| 目标版本 | **Minecraft 1.21.120.0**（唯一基线；`1.21.90.1` 属另一次运行，排除） |
| 游戏 EXE | `E:\pf\3.9.20.300505\Minecraft.Windows.exe`（本机不可访问，SHA/PE 字段 PENDING） |
| 映射可信度 | `NOT TRUSTWORTHY`（73 条不可用）；**未修改任何映射数值 / 判据 / DEBUG 人工放行** |
| 运行期镜像身份 | `RUNTIME_IMAGE_IDENTITY_UNVERIFIED` |

## 3. 工作组一：Dispatcher 源码考古（10 问必答，全部来自实际源码）

**已完整读取**：`include/nes/include/nes/event_dispatcher.hpp`、
`src/Features/Events/RenderEvent.hpp`、`Event.hpp`、
`src/Features/Modules/Module.hpp`（含 `ModuleBase<T>`）、`ModuleManager.hpp/.cpp`、
`FeatureManager.hpp`、`src/pch.hpp`（`EventPriorities`）、
`OpenGLGui.cpp`、`D3DHook.cpp`。

| # | 问题 | 结论（静态源码证据） |
|---|---|---|
| 1 | 实际订阅容器 | `nes::dispatcher<RenderEvent>::mListeners` = `std::map<event_priority, listener_list<event_listener<RenderEvent>>>`；每个 `listener_list` 内含 `std::vector<event_listener>` + 独立 `mutable std::shared_mutex mWriteLock`。`event_listener` = `{ void* mInstance; std::function<void(RenderEvent&)> mMethod; entt::id_type mMethodHash; }`（三个成员 public）。`event_dispatcher::get<RenderEvent>()` 返回**函数内 static 单例**。 |
| 2 | 如何注册 | `mDispatcher->listen<RenderEvent, &T::onRenderEvent, priority>(this)` → `unique_lock(mMutex)`，构造捕获 `instance` 的 lambda，`mListeners[priority].emplace_back(instance, wrapper, type_hash)`（内部再取 `mWriteLock` 的 `unique_lock`）。自由函数形态 `listen<RenderEvent, priority>(handler)` 存 `mInstance = nullptr`。 |
| 3 | 注册顺序 = 执行顺序？ | **不等价**。跨优先级**不等**：`std::map` 按 `EventPriorities` 枚举值升序（`ABSOLUTE_FIRST=0 … ABSOLUTE_LAST=8`，`src/pch.hpp`）。**同一优先级内相等**：`vector::emplace_back` 顺序 + 顺序遍历。但注册时机分散在 **构造函数 / `onEnable()` / `onInit()`**；`onEnable()` 由 `ModuleManager::onClientTick` 按 `mModules` 顺序、且仅在 `mWantedState != mEnabled` 时触发 → **静态构造顺序 ≠ 运行期实际顺序**。 |
| 4 | 同步执行？ | **是**。`trigger()` 取 `shared_lock(mMutex)`，两层 range-for 直接 `listener.invoke(holder)` → `mMethod(holder.ref())`。无线程切换、无任务队列、无 async。 |
| 5 | 异常隔离？ | **无**。`trigger()` 内无 try/catch。`OpenGLGui::render(HDC) try{...}catch(...)` 只捕获 C++ 异常；MSVC `/EHsc` 下**不捕获 SEH AV (`0xC0000005`)**。订阅者内 AV 会直接终止进程/触发 WER——与"卡死后闪退"症状一致。 |
| 6 | 递归派发？ | 源码不主动递归，但 **dispatcher 无重入防护**。回调若自行 `trigger`/`listen`，会重入遍历或再次取锁。39 个 `onRenderEvent` 的 `recursiveEvent` 词法扫描命中 **0**，但不排除经间接函数触发。 |
| 7 | 执行期修改容器？ | 静态扫描 39 个回调**未发现** `listen`/`deafen`。但这是**最高风险点**：`trigger` 持有 `mMutex` 的 `shared_lock`，任何回调内 `listen`/`deafen`（需 `unique_lock(mMutex)`）→ **锁升级死锁**；且 `listener_list::begin()` 的迭代器持有 `mWriteLock` 的 `shared_lock`，`emplace_back`/`erase_if` 需 `unique_lock(mWriteLock)` → 同样死锁 + 迭代器失效。 |
| 8 | 锁竞争？ | 两把 `shared_mutex`：`mMutex`（map 级）与每个 `listener_list` 的 `mWriteLock`。`trigger` 取 shared/shared；`listen`/`deafen` 取 unique。跨线程注册与派发会竞争。审计发现 4 个回调含 mutex 关键词，需人工核实是否与 dispatcher 锁相关。 |
| 9 | 跨线程派发？ | **RenderEvent 只从 GL `OpenGLGui::render()` 派发**（`D3DHook::present` 在 `glGuiActive` 后永久短路，绝不派发）。`trigger` 不切线程 → **无跨线程派发**。但 `listen`/`deafen` 可由任意线程（如 client tick 的 `onEnable`）发起 → **跨线程注册存在**。 |
| 10 | 首帧实际执行了哪些回调？ | **静态不可判定，本轮不回答为已确认**。受 4 个运行期条件支配：① 多数模块在 `onEnable()` 注册；② `Prefs->mDefaultConfigName` 启动自动加载 → `deserialize` 写 `mWantedState` → `onClientTick` 才应用；③ 需 `ClientInstance::get()` 非空才进入派发；④ 首帧可能早于 `onClientTick`。静态 39 个候选见 `render-subscriber-order.json`（`runtimeObserved: false`）。**必须由 `SOLSTICE_RENDER_DIAG` 日志确认**。 |

**类型擦除下模块名关联（§三 特别要求）**：利用 `event_listener::mInstance`（public
`void*`，即注册时的 `this`），在 `renderDiagOpen()` 一次性用
`dynamic_cast<void*>(Module*)` 构建地址→名称表；`nameFn` 只查表、不 deref 未知指针，
未命中返回 `(non-module)`。**未改变任何注册/派发语义**（详见
`subscriber-instrumentation.md`）。

**配置覆盖链（工作组三新增发现）**：
`Solstice.cpp:454-458` → `ConfigManager::loadConfig` → `ModuleManager::deserialize`
写 `mod->mWantedState = enabled` → `onClientTick:431-435` → `setEnabled()` →
`onEnable()` → `listen<RenderEvent>`。**用户配置直接决定首帧订阅者名单**，
运行机 config 不在仓库，静态无法确认。

## 4. 实际订阅顺序

- **静态顺序**（`render-subscriber-order.json`，39 个订阅者 / 9 个优先级层级）：
  `BlockESP(VERY_FIRST) → HudEditor → Aura → AutoClicker → … →
  Interface (dispatchOrder 15) → Arraylist → LevelInfo → DestroyProgress → ESP →
  ItemESP → Nametags → FullBright → Keystrokes → SessionInfo → Tracers →
  ChinaHat → Zoom → … → ClickGui → MotionBlur → TargetHUD → Notifications →
  UpdateForm`。
- **这不是运行期顺序**（问题 3/10）。所有条目 `runtimeObserved: false`。
- **唯一间接运行期线索**：`patchFullStack at 0x0` 的 error 打印自
  `Interface::onRenderEvent`，且 `DEFINE_PATCH_FUNC` 在 `addr==0` 安全早返回 → 若该行
  确属本次 `frame=1` 的 GL 派发，则 Interface 已进入（静态 `dispatchOrder=15`），
  故障更可能在其后。**这是推断，不是证据**（该 error 行无 tid，且日志归属未证实）。

## 5. 各工作组结论摘要

| 工作组 | 交付物 | 结论 |
|---|---|---|
| 一 Dispatcher 考古 | 本文件 §3 | 同步、无异常隔离、两把 shared_mutex、存在锁升级死锁风险；首帧名单静态不可判定 |
| 二 订阅者诊断 | `subscriber-instrumentation.md` + 3 处源码 | 已实现 `SOLSTICE_RENDER_DIAG`（默认 OFF）；每订阅者 flush-on-write `BEGIN/END`，8 帧上限，不捕获 AV，不经 spdlog |
| 三 视觉订阅者审查 | `render-subscriber-order.json`、`render-subscriber-inventory.json` | 39 个候选；**39/39 成功隔离回调体**；13 类构造词法扫描；配置覆盖机制；`runtimeObserved` 全为 false |
| 四 patchFullStack | `patchfullstack-callchain.md` | **`BENIGN_SIGNATURE_MISS`**：宏对 `addr==0` 安全早返回，不修改；线程仅静态推断为 `tid=0x34d8`；不与卡死合并根因 |
| 五 MinecraftGame 对象 | `minecraftgame-object-followup.md` | 独立问题（不同线程 21392）；`isPlausibleGame` 不以 vtable 为判据 → 通过定位但 offset0 不可读；守卫**安全拒绝**，无 AV；不修改 playUi/slot164/InstanceLocator |
| 六 映射依赖 | `mapping-risk-for-render-subscribers.json` | 8 类 getter 家族 + 39 个订阅者关联；标记 OK/MISSING/AMBIGUOUS/IMPLAUSIBLE/OUT_OF_RANGE/HARDCODED_UNVERIFIED；不遗漏硬编码 `ClientInstance+0x358`；**映射不可信 ≠ 模块必崩** |
| 七 运行时证据 | `runtime-capture-guide.md` | WinDbg/ProcDump 命令集、判定矩阵、静态 vs 运行时证据分级 |
| 构建身份 | `runtime-build-identity.md` | 本机实测：EXE/CMakeCache/工具链均不在本机 → 全部身份字段 PENDING，不代填 |
| 修改与验证 | `minimal-fix-summary.md` | **无根因修复**；仅诊断交付 + 诊断代码自身 3 处正确性修正；编译/测试实况 |

## 6. 停止条件判定（§十三）

| 条件 | 是否触发 | 本轮动作 |
|---|---|---|
| 订阅者诊断找到未完成回调 | 否（诊断未运行） | 已交付诊断，等构建机日志 |
| 所有订阅者都完成 → 查 ImGui/GL Submit | 否 | 待日志 |
| 停在 `HUD_DISPATCH_BEGIN` 且无订阅者记录 → 优先查 Dispatcher/锁/日志路径 | **旧日志符合** | **已执行工作组一**；发现潜在锁升级风险但未证实，不猜测模块 |
| 游戏闪退 → 取异常上下文/Dump | 无 Dump/WER | 已写捕获指南 |
| 无法证明根因 → 停止修改、不扩大禁用范围 | **是** | **已停止**，仅交付诊断与静态结论 |

## 7. 证据等级（本轮强制区分）

| 类别 | 内容 | 可否作根因依据 |
|---|---|---|
| 静态源码证据 | Dispatcher 结构/锁/同步性、10 问答复、patchFullStack 宏分支、playUi 守卫、配置覆盖链 | **否**（可给候选与检查项） |
| 静态解析数据 | 39 个订阅者顺序、13 类构造审计、getter↔映射关联 | **否** |
| 静态推断 | `[error]` 线程、Interface 已执行、静态 dispatchOrder | **否** |
| 运行时记录 | `phase=BEGIN/END` 日志（条件：诊断文件已打开并 flush） | 可定位**订阅者边界**，非指令 |
| Dump / WER | 异常地址 + 线程栈 + `!locks` | **是**（唯一可谈具体故障指令者） |

## 8. 交付物清单（10/10 齐全）

```
docs/batch3_6_2_f/
    runtime-build-identity.md
    render-subscriber-order.json
    render-subscriber-inventory.json
    subscriber-instrumentation.md
    patchfullstack-callchain.md
    minecraftgame-object-followup.md
    mapping-risk-for-render-subscribers.json
    runtime-capture-guide.md
    minimal-fix-summary.md
    final-decision.md
```
附：`tools/batch3_6_2_f_gen.py`、`tools/batch3_6_2_f_audit.py`、
`tools/batch3_6_2_f_mapping_risk.py`（静态数据生成器，不参与编译）。

## 9. 未解决问题 / 下一步

**要跑的命令**（构建机）：
```bat
cmake -S . -B build -DSOLSTICE_RENDER_DIAG=ON
cmake --build build --config Release
```
运行至第一帧卡死/闪退，若再次卡死按 `runtime-capture-guide.md` §B 保持进程并
取 dump（`.symfix / .reload / !analyze -hang / ~* kb / !locks / !runaway 7`）。

**回传什么**：构建日志、`CMakeCache.txt` 的 `SOLSTICE_*` 项、EXE/DLL SHA-256 与
PE `ImageBase`/`SizeOfImage`、生成的 `build_info.h`、
`%TEMP%\solstice_render_diag.log` 全文、`solstice.log`/`crash.log`/`sigreport.json`、
若闪退另附 WER 事件。

**自查口径**：
1. 诊断日志有 `[RENDER-DIAG] opened ... handle=OK` 才生效；`handle=INVALID` 不可用。
2. 最后一个 `phase=BEGIN` 无 `END` → 只能标 `FIRST_UNFINISHED_SUBSCRIBER`，**不等于
   已证明死锁**。
3. 全部配对但无 `HUD_DISPATCH_END` → 查 Dispatcher/锁/日志路径（工作组一问题 7/8）。
4. 无 Dump 不谈具体故障指令；`tid=0x34d8` 与 `threadId=21392` 不得合并根因。

**未收敛项**：O1/O2（二进制 ABI 佐证）、O4（运行 EXE SHA 比对 /
`RUNTIME_IMAGE_IDENTITY_UNVERIFIED`）、73 条不可用映射（本轮不修复）、
运行机 config 对启用集的实际覆盖、本机构建结果回填。

---

## 附录 A — 完整实际 Git Diff（源码 3 文件，+136 行）

> 生成命令：
> `git diff -- CMakeLists.txt include/nes/include/nes/event_dispatcher.hpp src/Hook/Hooks/RenderHooks/OpenGLGui.cpp`

```diff
diff --git a/solstice/solstice-src/solstice-master-main/solstice-master-main/CMakeLists.txt b/solstice/solstice-src/solstice-master-main/solstice-master-main/CMakeLists.txt
index 5a0429f..6c377e0 100644
--- a/solstice/solstice-src/solstice-master-main/solstice-master-main/CMakeLists.txt
+++ b/solstice/solstice-src/solstice-master-main/solstice-master-main/CMakeLists.txt
@@ -49,6 +49,17 @@ if (SOLSTICE_DISABLE_AURA_DIAGNOSTIC)
     add_compile_definitions(SOLSTICE_DISABLE_AURA_DIAGNOSTIC)
 endif ()
 
+# 3.6.2-F: subscriber-level RenderEvent fault localization. When ON, the GL HUD
+# dispatch is instrumented with per-subscriber BEGIN/END markers written to a
+# dedicated flush-on-write diagnostic file (solstice_render_diag.log). The
+# production dispatch path is compiled out entirely when this is OFF, so the
+# released binary is identical. Default OFF.
+option(SOLSTICE_RENDER_DIAG
+       "Instrument RenderEvent dispatch with per-subscriber BEGIN/END diagnostics (3.6.2-F fault localization)" OFF)
+if (SOLSTICE_RENDER_DIAG)
+    add_compile_definitions(SOLSTICE_RENDER_DIAG)
+endif ()
+
 find_program(LD_COMMAND ld)
 
 if (NOT LD_COMMAND)
diff --git a/solstice/solstice-src/solstice-master-main/solstice-master-main/include/nes/include/nes/event_dispatcher.hpp b/solstice/solstice-src/solstice-master-main/solstice-master-main/include/nes/include/nes/event_dispatcher.hpp
index ae1a59e..42b198e 100644
--- a/solstice/solstice-src/solstice-master-main/solstice-master-main/include/nes/include/nes/event_dispatcher.hpp
+++ b/solstice/solstice-src/solstice-master-main/solstice-master-main/include/nes/include/nes/event_dispatcher.hpp
@@ -202,6 +202,32 @@ namespace nes {
             }
         }
 
+#ifdef SOLSTICE_RENDER_DIAG
+        // 3.6.2-F subscriber-level fault localization. Mirrors trigger() exactly
+        // (same shared_lock(mMutex) and the same per-list listener_list shared_lock
+        // acquired by the range-for iterators) but invokes caller-supplied nameFn /
+        // logFn around each listener so the call site can attribute callbacks to
+        // modules and record BEGIN/END without altering registration, ordering, or
+        // callback arguments. Passing the type-erased listener.mInstance (void*)
+        // through nameFn is the only way to attribute a callback to a module name;
+        // this header stays decoupled from Module and never dereferences the pointer
+        // itself -- the call site resolves it via an address lookup table.
+        template <typename NameFn, typename LogFn>
+        void diagnostic_trigger(holder_t& holder, NameFn nameFn, LogFn logFn) {
+            std::shared_lock lock(mMutex);
+            unsigned index = 0;
+            for (auto& [priority, listeners] : mListeners) {
+                for (const auto& listener : listeners) {
+                    const char* name = listener.mInstance ? nameFn(listener.mInstance) : "(free-function)";
+                    logFn(index, name, 0);        // BEGIN
+                    listener.invoke(holder);
+                    logFn(index, name, 1);        // END
+                    ++index;
+                }
+            }
+        }
+#endif
+
     private:
         std::map<event_priority, listener_list<listener_t>> mListeners;
         mutable std::shared_mutex mMutex;
diff --git a/solstice/solstice-src/solstice-master-main/solstice-master-main/src/Hook/Hooks/RenderHooks/OpenGLGui.cpp b/solstice/solstice-src/solstice-master-main/solstice-master-main/src/Hook/Hooks/RenderHooks/OpenGLGui.cpp
index f3dca9c..6c9cf51 100644
--- a/solstice/solstice-src/solstice-master-main/solstice-master-main/src/Hook/Hooks/RenderHooks/OpenGLGui.cpp
+++ b/solstice/solstice-src/solstice-master-main/solstice-master-main/src/Hook/Hooks/RenderHooks/OpenGLGui.cpp
@@ -7,6 +7,7 @@
 #include <exception>
 #include <mutex>
 #include <thread>
+#include <unordered_map>
 #include <vector>
 #include <gl/GL.h>
 #include <imgui.h>
@@ -14,6 +15,7 @@
 #include <imgui_impl_win32.h>
 #include <Solstice.hpp>
 #include <Features/FeatureManager.hpp>
+#include <Features/Modules/Module.hpp>
 #include <Features/Events/KeyEvent.hpp>
 #include <Features/Events/RenderEvent.hpp>
 #include <Features/Events/WindowResizeEvent.hpp>
@@ -30,6 +32,7 @@
 #include <Utils/Keyboard.hpp>
 #include <Utils/MiscUtils/MathUtils.hpp>
 #include <Utils/ProcUtils.hpp>
+#include <cstdio>
 
 extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
 
@@ -153,6 +156,79 @@ namespace {
                 stage, phase, static_cast<unsigned long>(tid), glGuiFrameCounter, ctx);
         }
     }
+    // 3.6.2-F subscriber-level fault localization diagnostics.
+    // A dedicated, low-overhead diagnostic file (flush-on-write) records one
+    // BEGIN/END line per RenderEvent subscriber on the GL presentation thread for
+    // the first kRenderDiagMaxFrames frames. Every record is flushed immediately,
+    // so a hang or AV inside a subscriber leaves the last BEGIN without a matching
+    // END on disk. The diagnostic path never goes through spdlog: render() already
+    // holds glGuiMutex in this scope, so re-entering the spdlog sink (which takes
+    // its own lock) from inside a dispatcher callback is deliberately avoided.
+    // Module names come from an address table built once at open time. listen()
+    // stores the most-derived object pointer as void* in listener.mInstance, and
+    // dynamic_cast<void*>(Module*) reproduces exactly that address, so an unknown
+    // or non-module listener pointer is never dereferenced as a Module*.
+#ifdef SOLSTICE_RENDER_DIAG
+    constexpr unsigned kRenderDiagMaxFrames = 8;
+    HANDLE gRenderDiagFile = INVALID_HANDLE_VALUE;
+    unsigned gRenderDiagFramesRemaining = kRenderDiagMaxFrames;
+    bool gRenderDiagOpened = false;
+    std::unordered_map<void*, std::string> gRenderDiagNames;
+
+    void renderDiagWrite(const char* data, DWORD len) {
+        if (gRenderDiagFile == INVALID_HANDLE_VALUE || len == 0) return;
+        DWORD written = 0;
+        WriteFile(gRenderDiagFile, data, len, &written, nullptr);
+        FlushFileBuffers(gRenderDiagFile);
+    }
+
+    void renderDiagOpen() {
+        if (gRenderDiagOpened) return;
+        gRenderDiagOpened = true;
+        char path[MAX_PATH] = {0};
+        DWORD n = GetEnvironmentVariableA("TEMP", path, MAX_PATH);
+        std::string full;
+        if (n > 0 && n < MAX_PATH) full = std::string(path) + "\\solstice_render_diag.log";
+        else full = "solstice_render_diag.log";
+        gRenderDiagFile = CreateFileA(full.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
+                                     OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
+        // Build the address -> name table once, outside the per-callback path.
+        if (gFeatureManager && gFeatureManager->mModuleManager) {
+            for (const auto& mod : gFeatureManager->mModuleManager->getModules()) {
+                if (!mod) continue;
+                void* mostDerived = dynamic_cast<void*>(mod.get());
+                gRenderDiagNames.emplace(mostDerived, mod->mName);
+                gRenderDiagNames.emplace(static_cast<void*>(mod.get()), mod->mName);
+            }
+        }
+        char hdr[512];
+        int len = snprintf(hdr, sizeof(hdr),
+            "[RENDER-DIAG] opened maxFrames=%u modules=%zu file=%s handle=%s\n",
+            kRenderDiagMaxFrames, gRenderDiagNames.size(), full.c_str(),
+            gRenderDiagFile == INVALID_HANDLE_VALUE ? "INVALID" : "OK");
+        if (len > 0) {
+            const int capped = len < (int)sizeof(hdr) - 1 ? len : (int)sizeof(hdr) - 1;
+            renderDiagWrite(hdr, static_cast<DWORD>(capped));
+        }
+    }
+
+    const char* renderDiagName(void* inst) {
+        auto it = gRenderDiagNames.find(inst);
+        return it != gRenderDiagNames.end() ? it->second.c_str() : "(non-module)";
+    }
+
+    void renderDiagRecord(unsigned frame, unsigned index, const char* module, int phase) {
+        char buf[256];
+        int len = snprintf(buf, sizeof(buf),
+            "[RENDER-DIAG] frame=%u index=%u module=%s phase=%s tid=%#x\n",
+            frame, index, module, phase == 0 ? "BEGIN" : "END",
+            static_cast<unsigned>(GetCurrentThreadId()));
+        if (len < 0) return;
+        const int capped = len < (int)sizeof(buf) - 1 ? len : (int)sizeof(buf) - 1;
+        renderDiagWrite(buf, static_cast<DWORD>(capped));
+    }
+#endif
+
     ImGuiContext* glGuiContext = nullptr;
     HGLRC glGuiOwner = nullptr;
     HWND glGuiWindow = nullptr;
@@ -543,7 +619,30 @@ void OpenGLGui::render(HDC dc) try
         if (ClientInstance::get()) {
             glHangMarker(1, "HUD_DISPATCH_BEGIN", reinterpret_cast<uintptr_t>(current), GetCurrentThreadId());
             auto holder = nes::make_holder<RenderEvent>();
+#ifdef SOLSTICE_RENDER_DIAG
+            // 3.6.2-F: per-subscriber BEGIN/END fault localization. The diagnostic
+            // path mirrors the production trigger() exactly (same lock + ordering);
+            // it only adds flush-on-write BEGIN/END markers. After the capped window
+            // of frames it falls back to the unchanged production trigger().
+            renderDiagOpen();
+            if (gRenderDiagFramesRemaining > 0) {
+                --gRenderDiagFramesRemaining;
+                const unsigned frame = glGuiFrameCounter;
+                auto& disp = gFeatureManager->mDispatcher->get<RenderEvent>();
+                disp.diagnostic_trigger(
+                    holder,
+                    [](void* inst) -> const char* {
+                        return renderDiagName(inst);
+                    },
+                    [frame](unsigned index, const char* name, int phase) {
+                        renderDiagRecord(frame, index, name, phase);
+                    });
+            } else {
+                gFeatureManager->mDispatcher->trigger(holder);
+            }
+#else
             gFeatureManager->mDispatcher->trigger(holder);
+#endif
             glHangMarker(1, "HUD_DISPATCH_END", reinterpret_cast<uintptr_t>(current), GetCurrentThreadId());
         }
         ImGui::End();
```

> 工具脚本（3 个）与 10 个文档为新增文件，完整内容见本轮提交：
> `git show --stat <commit>` / `git show <commit> -- docs/batch3_6_2_f/ tools/`。
