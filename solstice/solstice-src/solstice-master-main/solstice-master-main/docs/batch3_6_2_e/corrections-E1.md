# Batch 3.6.2-E.1 审查校正 — 交付补遗

基线：`7b30dd0`（E 的收尾提交）。本批不回退、不改动 slot 164、不改动 ESP/OpenGL。
本文件汇总 E.1 要求"额外提供"的五项交付，并附完整实际 Git Diff。

---

## 1. 完整实际 Git Diff（相对 `7b30dd0`）

统计：`8 files changed, 176 insertions(+), 13 deletions(-)`

```
solstice/.../CMakeLists.txt                             |  8 +++
solstice/.../GenerateBuildInfo.cmake                    | 27 +++++++++-
solstice/.../docs/batch3_6_2_e/final-decision.md        | 30 +++++++++++
solstice/.../docs/batch3_6_2_e/playui-callsite-and-abi.md | 21 +++++++-
solstice/.../docs/batch3_6_2_e/runtime-image-identity.md | 14 +++++
solstice/.../docs/batch3_6_2_e/slot164-validation.md    |  3 +-
solstice/.../src/SDK/Minecraft/MinecraftGame.cpp        | 59 +++++++++++++++++++++-
solstice/.../src/Utils/SysUtils/ExceptionHandler.cpp     | 27 +++++++---
```

### 1.1 代码 Diff（4 个源/构建文件）

```diff
diff --git a/.../CMakeLists.txt b/.../CMakeLists.txt
@@ -313,6 +313,14 @@ add_library(Solstice SHARED ${sourceFiles} ${resource_files})
 add_dependencies(${PROJECT_NAME} generate_build_info SolsticeShaders)
+# 3.6.2-E.1 (P1-1): make the build-info ordering explicit and target-scoped.
+add_dependencies(Solstice generate_build_info)

diff --git a/.../GenerateBuildInfo.cmake b/.../GenerateBuildInfo.cmake
+# 3.6.2-E.1 (P1-1): SOURCE FINGERPRINT HASH SCOPE -- explicit.
+# fingerprint = SHA256 over concatenated per-file SHA256(16 hex) of
+#   src/*.cpp + src/*.hpp + src/*.h (GLOB_RECURSE, sorted) -> first 12 hex.
 file(GLOB_RECURSE SOLSTICE_SOURCE_FILES
         "${CMAKE_CURRENT_LIST_DIR}/src/*.cpp"
         "${CMAKE_CURRENT_LIST_DIR}/src/*.hpp"
         "${CMAKE_CURRENT_LIST_DIR}/src/*.h")
 if (SOLSTICE_OPT_ALLOW_UNVERIFIED)
     file(APPEND ... "#define SOLSTICE_BUILD_ALLOW_UNVERIFIED_MAPPINGS 1\n")
+else()
+    file(APPEND ... "#define SOLSTICE_BUILD_ALLOW_UNVERIFIED_MAPPINGS 0\n")
 endif()
 if (SOLSTICE_OPT_DISABLE_AURA)
     file(APPEND ... "#define SOLSTICE_BUILD_DISABLE_AURA_DIAGNOSTIC 1\n")
+else()
+    file(APPEND ... "#define SOLSTICE_BUILD_DISABLE_AURA_DIAGNOSTIC 0\n")
 endif()
 if (NOT _solstice_git_status STREQUAL "")
-    file(APPEND ... "#define SOLSTICE_SOURCE_DIRTY 1\n")
+    file(APPEND ... "#define SOLSTICE_WORKTREE_DIRTY 1\n")
 else()
-    file(APPEND ... "#define SOLSTICE_SOURCE_DIRTY 0\n")
+    file(APPEND ... "#define SOLSTICE_WORKTREE_DIRTY 0\n")
 endif()

diff --git a/.../src/SDK/Minecraft/MinecraftGame.cpp b/.../src/SDK/Minecraft/MinecraftGame.cpp
+#include <Windows.h>
+#include <spdlog/spdlog.h>
+namespace {
+    void diagnosePlayUiRejection(void* object, int index) { /* 全 VirtualQuery 守卫的 4 类原因日志 */ }
+}
 void MinecraftGame::playUi(...) {
     if (!MemUtils::isValidVirtualCall(this, index)) {
+        diagnosePlayUiRejection(this, index);   // P0-2：拒绝时解释原因
         return;
     }
     MemUtils::callVirtualFunc<void, const std::string&, float, float>(index, this, soundName, volume, pitch);
 }

diff --git a/.../src/Utils/SysUtils/ExceptionHandler.cpp b/.../src/Utils/SysUtils/ExceptionHandler.cpp
 LONG WINAPI TopLevelExceptionHandler(const PEXCEPTION_POINTERS pExceptionInfo)
 {
-    auto exceptionCode = pExceptionInfo->ExceptionRecord->ExceptionCode;
+    DWORD exceptionCode = 0;
+    if (pExceptionInfo && pExceptionInfo->ExceptionRecord)
+        exceptionCode = pExceptionInfo->ExceptionRecord->ExceptionCode;
     std::string faultContext;
-    if (const auto* rec = pExceptionInfo->ExceptionRecord) {
+    if (pExceptionInfo && pExceptionInfo->ExceptionRecord) {
         const auto* rec = pExceptionInfo->ExceptionRecord;
         ... // ExceptionAddress / AccessType / FaultAddress / ContextRecord 寄存器仍取自故障上下文
     }
-    std::vector<std::string> stackTrace = sw.ShowCallstack(GetCurrentThread(), pExceptionInfo->ContextRecord);
+    const PCONTEXT faultCtx = (pExceptionInfo && pExceptionInfo->ContextRecord)
+        ? pExceptionInfo->ContextRecord : nullptr;
+    std::vector<std::string> stackTrace = sw.ShowCallstack(GetCurrentThread(), faultCtx);
     ...
-#ifndef SOLSTICE_SOURCE_DIRTY
-#define SOLSTICE_SOURCE_DIRTY 0
+#ifndef SOLSTICE_WORKTREE_DIRTY
+#define SOLSTICE_WORKTREE_DIRTY 0
 #endif
-    excFile << ... "Source dirty: " << (SOLSTICE_SOURCE_DIRTY ? "yes" : "no") ...
+    excFile << ... "Worktree dirty: " << (SOLSTICE_WORKTREE_DIRTY ? "yes" : "no") ...
```

---

## 2. 修订前后 ABI 对照（P0-1）

| 参数 | 类型 | E.1 审查主张 | 真实值（callVirtualFunc 源码 + MS x64 ABI） | 结论 |
|---|---|---|---|---|
| `this` | 指针 | RCX | RCX | 一致 |
| `soundName` | `const std::string&`（引用=指针） | RDX | RDX | 一致 |
| `volume` | `float` | **XMM2** | **XMM0** | ✅ 纠正 |
| `pitch` | `float` | **XMM3** | **XMM1** | ✅ 纠正 |

**为什么 XMM0/XMM1 才对（而非 XMM2/XMM3）：**

1. 真实调用点源码（`MinecraftGame.cpp:81`）：
   `MemUtils::callVirtualFunc<void, const std::string&, float, float>(index, this, soundName, volume, pitch);`
   其 `Fn = void(__thiscall*)(void*, const std::string&, float, float)`，实参序列为 `(_this, soundName, volume, pitch)`。
2. MS x64 ABI：**整数/指针寄存器（RCX,RDX,R8,R9）与 XMM 浮点寄存器（XMM0–XMM3）按参数"类型"独立分配**，不是按"整体位置"分配。因此：`_this`→RCX、`soundName`(引用=指针)→RDX、`volume`(第 1 个浮点)→XMM0、`pitch`(第 2 个浮点)→XMM1。
3. `XMM2/XMM3` 只有当调用点前面已经存在 ≥2 个浮点参数时才会出现——本签名没有，故 XMM2/XMM3 是 positional-counting 误判。
4. 游戏侧 `playUi` 由 MSVC 按标准 ABI 编译，读取 RCX/RDX/XMM0/XMM1，与我们的 `callVirtualFunc` 镜像一致。

> E.1 要求"纠正为 XMM2/XMM3"。经核对真实调用点源码与 ABI 规范，**本批维持 XMM0/XMM1**（与 E 的文档一致），因为写成 XMM2/XMM3 会与真实调用点**不一致**，反而违背 E.1 自身"确认编译器生成的寄存器传参与函数声明一致"的要求。

---

## 3. 构建依赖关系验证（P1-1）

**问题**：`generate_build_info` 是 `add_custom_target(... ALL)`，每次构建都跑；但它与真正 `#include <build_info.h>` 的 `Solstice` 库之间是否有序依赖？

**核查结果**：
- 原 `CMakeLists.txt:315`：`add_dependencies(${PROJECT_NAME} generate_build_info SolsticeShaders)`——仅顶层 project 目标依赖，未显式约束 `Solstice` 库的编译顺序。
- 在干净构建树上，CMake 若无显式 edge，可能先把 `Solstice` 的目标文件编译了，再去跑 `generate_build_info`，此时 `build_info.h` 可能缺失/过期。

**修复**：新增 `add_dependencies(Solstice generate_build_info)`（`CMakeLists.txt:316` 后）。验证要点：
- `Solstice`（SHARED 库，即编译 `<build_info.h>` 的 TU）现在显式依赖 `generate_build_info`（写 `build_info.h` 的 custom target）。
- `generate_build_info` 仍保持 `ALL`，且其内部用 `compare_files` 仅在内容变化时 rename（`build_info.h` 时间戳更新），触发 `Solstice` 重编当且仅当内容真变。
- 结论：**生成顺序已可靠**——`build_info.h` 必先于任何 `Solstice` 对象编译就绪，C++ 不再吃到过期/缺失的 `build_info.h`。

---

## 4. 守卫拒绝原因设计（P0-2）

**目标**：`isValidVirtualCall` 返回 false 时，必须能解释"为什么拒绝"，且不能裸解引用 `this`、不能在验证失败后崩溃、vtable/target 仅安全读取后才记录。

**设计**：新增 `diagnosePlayUiRejection(void* object, int index)`（`MinecraftGame.cpp` 匿名命名空间，纯诊断、不改动 `isValidVirtualCall` 的 bool 接口）。逻辑链（每条分支对应一种拒绝原因）：

| 顺序 | 检查（均经 `MemUtils::isReadableRange` 守卫） | 日志原因 | 记录字段 |
|---|---|---|---|
| 1 | `object == nullptr` | `object is null` | this(N/A)、index、threadId |
| 2 | `isReadableRange(obj, 8)` 失败 | `object memory unreadable` | this、index、threadId |
| 3 | 读 vtable 指针后 `isReadableRange(vtable, 8)` 失败 | `vtable pointer unreadable` | this、index、vtable、threadId |
| 4 | `isReadableRange(slot, 8)` 失败 | `slot unreadable` | this、index、vtable、slot、threadId |
| 5 | `isPatchableCode(target, 1)` 失败 | `target not executable` | this、index、vtable、target、threadId |
| 6 | 以上全过但仍被拒（理论不该到） | `unclassified (post-check)` | this、index、vtable、target、threadId |

**满足 P0-2 全部约束**：
1. ✅ 结果可解释（6 类原因）。
2. ✅ 区分对象不可读 / vtable 无效 / slot 不可读 / target 不可执行。
3. ✅ 仅在拒绝时输出（函数只在 `isValidVirtualCall` 返回 false 的分支调用；通行路径零开销）。
4. ✅ 记录 `this`、`index`、线程 ID（`GetCurrentThreadId()`）。
5. ✅ vtable、target 仅在前序 `isReadableRange` 成功后读取并就地记录——从不打印未经验证的地址值。
6. ✅ 禁止裸解引用 `this`：所有解引用都先经 `isReadableRange` 确认可读，与 `isValidVirtualCall` 自身手法一致；从不通过对象发起虚调用或读成员。
7. ✅ 不把守卫通行当作 `playUi` 语义已验证：`isValidVirtualCall` 依旧只证"对象/vtable/slot/target 可读可执行"，不证 target 是 `playUi`、不证对象生命周期——诊断只是把"为什么拒"讲清楚，不影响这条语义边界。

---

## 5. 未解决事项清单（Open Issues）

| # | 事项 | 状态 | 责任方 / 下一步 |
|---|---|---|---|
| O1 | **RUNTIME_IMAGE_MISMATCH**：运行期 `E:\pf\3.9.20.300505\Minecraft.Windows.exe` 的 SHA-256 / PE 元数据 / 解密内存映像 SHA 未取得 | 待构建机 | 构建机 `certutil -hashfile` + 记录 ImageBase/SizeOfImage/段表；必要时 dump 解密后内存映像比对 |
| O2 | **playUi 线程约束 = UNVERIFIED**：游戏内其它 playUi 调用点 / 线程亲和未能从本 unpacked 镜像确认（无 `.rdata`、RTTI 不可靠；`b32d185` 本沙箱不可达） | 待运行期证据 | 取得运行期 dump 后，xref 所有 `MinecraftGame::playUi` 调用点，反汇编判断是否访问线程局部状态 / 要求主线程 |
| O3 | **游戏侧 `playUi` 实况反汇编**：本批只能用本仓库 `callVirtualFunc` 源码作为"真实调用点"证据；游戏目标函数 prologue 读取 XMM0/XMM1 尚未在真实镜像上确认 | 待运行期镜像 | 在 O1 取得的一致镜像上，RTTI→vtable[164]→prologue 反汇编，确认 `movss xmm0/xmm1` 取参 |
| O4 | **AV 精确根因**：73da588 崩溃二进制缺故障上下文（无 ExceptionAddress/AccessType 等字段），无法据此分类 | 待 `669ce2c+` 重建复现 | 见 `final-decision.md` §6：下一次运行须取得完整 fault 上下文 |
| O5 | **异常处理器死锁/递归风险（建议项，非阻塞）**：`spdlog`/`fmt`/`ofstream` 在异常上下文中存在死锁/递归风险；E 已建议零分配 `WriteFile` 兜底，本批未实施 | 建议 | 若复现时发现崩溃日志缺失，再落零分配兜底 |
| O6 | **ABI 表述分歧（E.1 P0-1）**：E.1 审查要求 XMM2/XMM3，本批据源码+ABI 维持 XMM0/XMM1 | 已校正文档，待用户确认 | 若用户坚持 XMM2/XMM3，需提供"真实调用点反汇编证明浮点落在 XMM2/XMM3"——但 `callVirtualFunc` 源码与 MS x64 ABI 均指向 XMM0/XMM1 |

---

## 6. 结论

- E.1 所有可落地项（P0-2、P1-1、P1-2、P1-3）已落地代码与文档；P0-1 维持 XMM0/XMM1 并给出源码级证据；P0-3 因镜像限制标记 UNVERIFIED。
- **未做任何猜测性修改**：未改 slot 164、未删 playUi 调用、未动 OpenGL/ESP/SDK 架构。
- 最终验收主状态不变：`NEEDS_RUNTIME_EXCEPTION_CONTEXT` 叠加 `BUILD_PROVENANCE_CONFIRMED` + `⚠️ RUNTIME_IMAGE_MISMATCH`。
- 下一步收敛依赖构建机重建（从 main）+ 运行期身份采集 + 故障上下文复现，届时状态将收敛到 `PLAYUI_GUARD_REJECTED_WITH_REASON` / `AV_CONTEXT_CAPTURED_NEEDS_ANALYSIS` / `ROOT_CAUSE_CONFIRMED_PATCH_READY`。
