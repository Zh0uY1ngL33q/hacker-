# 最终判定 — Batch 3.6.2-E 第十五工作组

## 1. 最终验收状态

主状态：**NEEDS_RUNTIME_EXCEPTION_CONTEXT**（需下一版构建的故障上下文才能对 AV 精确分类）

并叠加两个子状态（均已达成/已标记）：
- ✅ **BUILD_PROVENANCE_CONFIRMED** —— 运行二进制身份已精确确认：它是**货真价实的 73da588 构建**（`BUILD_VERSION` 前缀与 `SOURCE_FINGERPRINT` `e8175b9f0699` 都与 `73da588` 源码树逐字节一致），构建机制可靠；问题是构建机用的是陈旧 checkout，已从 main(669ce2c) 重建即可。
- ⚠️ **RUNTIME_IMAGE_IDENTITY_UNVERIFIED** —— 运行期 EXE 的 SHA 尚未取得、尚未比较，因此仅标记为「未验证」（**不是**「不匹配」；MISMATCH 只有在取得运行期 SHA 并比对不一致后才成立）。slot-164 对运行期的适用性待比对（静态 unpacked 镜像 SHA `5fb2e893...026deb`）。

> 说明：任务允许从给定清单选一个主状态。选 NEEDS_RUNTIME_EXCEPTION_CONTEXT 作为主状态，是因为 AV 本身的精确根因尚未取得直接证据——我们只确认了「崩溃二进制 = 73da588（缺守卫与诊断）」。下一版（669ce2c + 两个选项）复现后，状态会收敛到 `PLAYUI_GUARD_REJECTED_WITH_REASON` 或 `AV_CONTEXT_CAPTURED_NEEDS_ANALYSIS` 或 `ROOT_CAUSE_CONFIRMED_PATCH_READY`。

## 2. 各工作组结论速览

| 工作组 | 结论 |
|---|---|
| 一/三 构建身份 | 运行二进制 = 73da588（指纹精确匹配），构建机制可靠；构建机陈旧 checkout 导致缺 3.6.2-C/D；已补身份字段 |
| 二/四 两编译选项 | `SOLSTICE_ALLOW_UNVERIFIED_MAPPINGS` 保留人工 DEBUG 提示（Enter 退出、不自动放行、不改 Missing→OK）；`SOLSTICE_DISABLE_AURA_DIAGNOSTIC` 经 `#ifndef` 阻止 Aura 构造/注册/生命周期/事件，隔离成功时 Aura 不出现在模块清单；本批把两选项写入 Build Info |
| 五 运行期镜像 | unpacked(1.21.120.0) SHA `5fb2e893...026deb` 已核对；运行期 SHA 不可达 → RUNTIME_IMAGE_IDENTITY_UNVERIFIED（待验证，非 MISMATCH） |
| 六 调用链/ABI | 崩溃 = 调用点 A（Solstice::init 尾音效，initThread）；playUi 可选音效；ABI（层 ① 类型推导）= RCX/RDX/XMM2/XMM3（E.1.2.1 用户裁定；二进制 caller/target 寄存器读取仍 PENDING） |
| 七 slot 164 | unpacked 镜像唯一命中 RVA `0x1CBC3E4`，disp32 `0x520` → 164，四层通过；不盲改数字；运行期待 SHA 比对 |
| 八 守卫审查 | `isValidVirtualCall` 检查对象/vtable/slot/target 可读可执行；不证 target==playUi、不证生命周期；失败静默 return 因 playUi 可选不虚报初始化成功 |
| 六/八 处理器审查 | 故障上下文字段正确（寄存器取自故障 ContextRecord，AV 才解析 AccessType/FaultAddress）；处理器内 spdlog/fmt/ofstream 存在死锁/递归风险，给出零分配 WriteFile 方案（建议项） |
| 九 patchFullStack | BENIGN_SIGNATURE_MISS，与 playUi AV 无关，不合并 |

## 3. 本批修改文件清单（3.6.2-E）

```
solstice/.../CMakeLists.txt
solstice/.../GenerateBuildInfo.cmake
solstice/.../src/Utils/SysUtils/ExceptionHandler.cpp
```

## 4. 完整 Git Diff（3.6.2-E 三文件）

```diff
diff --git a/.../CMakeLists.txt b/.../CMakeLists.txt
@@ -193,8 +193,14 @@
 # 3.6.2-E: also forward the build configuration and the two diagnostic CMake
 # options so a crash log can show exactly how the binary was built.
 add_custom_target(generate_build_info ALL
-        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_SOURCE_DIR}/GenerateBuildInfo.cmake
+        COMMAND ${CMAKE_COMMAND}
+            -DSOLSTICE_CMAKE_BUILD_TYPE=$<CONFIG>
+            -DSOLSTICE_OPT_ALLOW_UNVERIFIED=${SOLSTICE_ALLOW_UNVERIFIED_MAPPINGS}
+            -DSOLSTICE_OPT_DISABLE_AURA=${SOLSTICE_DISABLE_AURA_DIAGNOSTIC}
+            -P ${CMAKE_SOURCE_DIR}/GenerateBuildInfo.cmake
         COMMENT "Generating build info [${CMAKE_BINARY_DIR}/build_info.h]")

diff --git a/.../GenerateBuildInfo.cmake b/.../GenerateBuildInfo.cmake
@@ -156,6 +156,32 @@
 file(APPEND "${TEMP_BUILD_INFO_PATH}" "#define SOLSTICE_INTENDED_VERSION ...
+
+# --- Build identity completeness (3.6.2-E) ---
+if (DEFINED SOLSTICE_CMAKE_BUILD_TYPE AND NOT SOLSTICE_CMAKE_BUILD_TYPE STREQUAL "")
+    file(APPEND ... "#define SOLSTICE_BUILD_CONFIG \"${SOLSTICE_CMAKE_BUILD_TYPE}\"\n")
+else()
+    file(APPEND ... "#define SOLSTICE_BUILD_CONFIG \"unknown\"\n")
+endif()
+if (SOLSTICE_OPT_ALLOW_UNVERIFIED)
+    file(APPEND ... "#define SOLSTICE_BUILD_ALLOW_UNVERIFIED_MAPPINGS 1\n")
+endif()
+if (SOLSTICE_OPT_DISABLE_AURA)
+    file(APPEND ... "#define SOLSTICE_BUILD_DISABLE_AURA_DIAGNOSTIC 1\n")
+endif()
+execute_process(COMMAND ${GIT_EXECUTABLE} status --porcelain ...)
+if (NOT _solstice_git_status STREQUAL "")
+    file(APPEND ... "#define SOLSTICE_SOURCE_DIRTY 1\n")
+else()
+    file(APPEND ... "#define SOLSTICE_SOURCE_DIRTY 0\n")
+endif()

diff --git a/.../src/Utils/SysUtils/ExceptionHandler.cpp b/.../src/Utils/SysUtils/ExceptionHandler.cpp
@@ -149,6 +149,20 @@
         excFile << ... "Minecraft version: " ...
+#ifndef SOLSTICE_BUILD_CONFIG
+#define SOLSTICE_BUILD_CONFIG "unknown"
+#endif
+        excFile << ... "Build config: " << SOLSTICE_BUILD_CONFIG ...
+#ifdef SOLSTICE_BUILD_ALLOW_UNVERIFIED_MAPPINGS
+        excFile << ... "Build opt: ALLOW_UNVERIFIED_MAPPINGS=ON\n";
+#endif
+#ifdef SOLSTICE_BUILD_DISABLE_AURA_DIAGNOSTIC
+        excFile << ... "Build opt: DISABLE_AURA_DIAGNOSTIC=ON\n";
+#endif
+#ifndef SOLSTICE_SOURCE_DIRTY
+#define SOLSTICE_SOURCE_DIRTY 0
+#endif
+        excFile << ... "Source dirty: " << (SOLSTICE_SOURCE_DIRTY ? "yes" : "no") ...
         excFile << ... "----------------------------------------\n";
```

## 5. 构建机验收配置（第十一节命令）

```bat
cmake -S <src> -B <build> ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DSOLSTICE_ALLOW_UNVERIFIED_MAPPINGS=ON ^
  -DSOLSTICE_DISABLE_AURA_DIAGNOSTIC=ON
cmake --build <build> --config Release
```
要点：
1. 保留人工 DEBUG 提示（Enter 退出，不自动放行，不改 Missing/Ambiguous→OK）。
2. 真正隔离 Aura（构建后模块清单不应再出现 Aura，而非仅 "Disabled"）。
3. 保留现有 SDK/ESP 成果，不回退。
4. 确保 Build Info 对应当前源码（从 main=669ce2c 配置；`generate_build_info` 每次构建重算）。
5. 构建后记录 DLL SHA-256。
6. 记录运行期 `Minecraft.Windows.exe` SHA-256（补齐 RUNTIME_IMAGE_IDENTITY_UNVERIFIED：比对一致则验证通过，不一致则升级为 MISMATCH）。

## 6. 下一次运行必须取得的证据

- `solstice.log` / `crash.log` / `sigreport.json`。
- 若再 AV：须出现 `ExceptionCode/ExceptionAddress/AccessType/FaultAddress/RIP/RSP/RBP/RCX/RDX/R8/R9/RAX`。
- 若守卫拒绝 playUi：须出现明确拒绝原因（建议项落地后）。
- 若守卫放行仍 AV：用 `ExceptionAddress`/`FaultAddress` 定位具体指令。
- 优先取得完整 Dump（但 Dump 不作为唯一证据）。

## 7. 核心原则重申

本轮不继续猜 slot 164、不改动 OpenGL。先让构建身份可信（已确认 73da588 + 补身份字段），再让异常上下文完整（669ce2c 已带），最后才决定修改什么。保留当前所有适配成果，不全项目回退，不以防御性 return 冒充根因修复。

## 8. Batch 3.6.2-E.1 审查校正补遗（2026-11-08）

E.1 是对 E 的独立审查修正，基线 `7b30dd0`，不回退、不改动 slot 164、不动 ESP/OpenGL。

### 8.1 已完成修正（对照 E 的九份原文档）

| 项 | E.1 要求 | 实际处理 | 状态 |
|---|---|---|---|
| P0-1 ABI | 改为 XMM2/XMM3 | **E.1.2.1 用户最终裁定采纳 XMM2/XMM3**（层 ① 类型推导：volume=XMM2 / pitch=XMM3）；E.1/E.1.1 曾维持 XMM0/XMM1，该结论被 E.1.2.1 修订。二进制 caller/target 寄存器读取仍 PENDING | 见 `playui-callsite-and-abi.md` §4、§4.1、§4.2 与 `corrections-E1.2.1.md` |
| P0-2 守卫诊断 | 拒绝时可解释、区分 4 类原因、仅拒绝时日志、记录 this/index/线程、vtable/target 仅安全读取后记录、失败后不裸解引用 | 新增 `diagnosePlayUiRejection()`（`MinecraftGame.cpp` 匿名命名空间），全 VirtualQuery 守卫，仅拒绝路径触发 | ✅ 已落地代码 |
| P0-3 线程约束 | 查游戏内其它 playUi 调用点 / 反汇编 / 线程亲和 / 对比 b32d185；无证据标记 UNVERIFIED | 该 unpacked 镜像无 `.rdata`（自定义段 `.winlice/.boot/.SCY`），RTTI/xref 不可靠；`b32d185` 在本沙箱不可达 → **标记 UNVERIFIED**，不写"无线程限制" | ✅ 已文档标记 |
| P1-1 构建身份 | 确认 `generate_build_info` 对 Solstice 的依赖；`SOURCE_DIRTY→WORKTREE_DIRTY`；两选项始终记录 ON/OFF；明确指纹哈希范围 | 新增 `add_dependencies(Solstice generate_build_info)`；重命名为 `SOLSTICE_WORKTREE_DIRTY`；两选项改为始终输出 0/1；补指纹哈希范围注释 | ✅ 已落地代码+注释 |
| P1-2 处理器 | `ExceptionCode` 空指针保护；仍从故障上下文取 ExceptionAddress/AccessType/FaultAddress/寄存器 | `pExceptionInfo`/`ExceptionRecord` 双重守卫；`StackWalker` 上下文 null 安全 | ✅ 已落地代码 |
| P1-3 运行期镜像 | 明确 `5fb2e893…` 仅为 unpacked 静态 SHA；要求运行 EXE SHA+PE 元数据+解密内存映像比对 | 已文档澄清（见 `runtime-image-identity.md`） | ✅ 已文档 |

### 8.2 E.1 代码修改文件清单（相对 `7b30dd0`）

```
solstice/.../src/SDK/Minecraft/MinecraftGame.cpp   // +diagnosePlayUiRejection, 拒绝时调用
solstice/.../CMakeLists.txt                        // +add_dependencies(Solstice generate_build_info)
solstice/.../GenerateBuildInfo.cmake               // WORKTREE_DIRTY 重命名；两选项始终 0/1；指纹范围注释
solstice/.../src/Utils/SysUtils/ExceptionHandler.cpp // ExceptionCode/ContextRecord null 保护；WORKTREE_DIRTY 重命名
```

### 8.3 主状态维持

E.1 未改变 E 的最终验收主状态：**NEEDS_RUNTIME_EXCEPTION_CONTEXT** 叠加 `BUILD_PROVENANCE_CONFIRMED` + `⚠️ RUNTIME_IMAGE_IDENTITY_UNVERIFIED`。ABI 校正不触发任何代码层 slot/OpenGL/ESP 改动。

## 9. Batch 3.6.2-E.1.1 审查纠错补遗（2026-11-08，基线 `2429e6e`）

E.1.1 是针对 E.1 的小范围审查纠错，**不重新实施 E.1**；不回退 `2429e6e`、不改动 slot 164 / playUi / InstanceLocator / OpenGLGui / ESP / 全局映射判据。

### 9.1 ABI 三层交叉核验（P0-1，E.1.1 当时结论 —— 已被 E.1.2.1 修订）

> **E.1.2.1 修订提示**：本节记录的是 E.1.1 当时的结论（维持 XMM0/XMM1）。E.1.2.1 由用户最终裁定将层 ① 类型推导 ABI 修订为 `volume=XMM2 / pitch=XMM3`（按参数整体序位编号）。本节所述 XMM0/XMM1 现仅作为历史决策记录，当前权威结论见 §11 与 `playui-callsite-and-abi.md` §4。

E.1.1 审查意见在 bullet 中写「`volume→XMM2` / `pitch→XMM3`」并要求据此修订 7 份文档。但按任务自身「三者必须交叉核验」的要求，对三层逐一核验后**当时唯一一致结论是 `volume=XMM0 / pitch=XMM1`**：

| 层 | 证据 | volume | pitch |
|---|---|---|---|
| ① C++ 声明推导 | `callVirtualFunc<void,const std::string&,float,float>` → `Fn=void(__thiscall*)(void*,const std::string&,float,float)`，实参 `(_this, soundName, volume, pitch)` | XMM0 | XMM1 |
| ② 编译器生成 | MSVC 据 MS x64 ABI；整数/指针走 RCX,RDX,R8,R9、浮点走 XMM0–XMM3，两类**按类型独立计数** | XMM0 | XMM1 |
| ③ 游戏二进制 | slot-164 thunk `0x1CBC3E4` 自身**不装载任何 XMM 寄存器**，volume/pitch 由调用方经 XMM0/XMM1 透传 | XMM0 | XMM1 |

`XMM2/XMM3` 是把浮点寄存器按「整体参数位置（第 3、4 个参数）」编号的错误结论（positional counting）；其同段注释「浮点参数……重新从 XMM0 开始编号」反而描述的是正确规则。本批据此让全部 7 份文档**统一收敛到 XMM0/XMM1**，未保留任何矛盾 ABI 表述。D 目录早期文档误写的 `volume=R8/pitch=R9`（浮点进整数寄存器）也一并订正。（该 XMM0/XMM1 结论后被 E.1.2.1 修订为 XMM2/XMM3，见 §11。）

### 9.2 异常处理器最终检查（P0-2）—— 全部项通过，无新增代码缺陷

| 检查项 | 结果 |
|---|---|
| `pExceptionInfo` 空指针 | ✅ `if (pExceptionInfo && pExceptionInfo->ExceptionRecord)` 守卫（L29, L39） |
| `ExceptionRecord` 空指针 | ✅ 同上 |
| `ContextRecord` 空指针 | ✅ `if (const auto* ctx = pExceptionInfo->ContextRecord)`（L59）与 `faultCtx` null 安全（L75） |
| `ExceptionCode` 是否为 AV | ✅ 仅在 `ExceptionRecord` 非空时读取（L29-31），且 `faultContext` 内按 `ExceptionInformation[0]` 区分 read/write/execute |
| `NumberParameters ≥ 2` | ✅ `AccessType` 要求 `>=1`（L42），`FaultAddress` 要求 `>=2`（L50）后才读取 `[0]/[1]` |
| `AccessType`/`FaultAddress` 仅在有效范围读取 | ✅ 如上 |
| 寄存器来自故障 ContextRecord | ✅ RIP/RSP/RBP/RCX/RDX/R8/R9/RAX 全部取自 `pExceptionInfo->ContextRecord`（L59-64），非处理器自身栈帧 |
| 递归/阻塞风险 | ⚠️ 观察项（非本批缺陷）：`spdlog::error` 在异常过滤器中仍可能死锁/递归；`MessageBoxA(RETRY)` 返回 `EXCEPTION_CONTINUE_EXECUTION` 会在同一坏指令上重试致循环。属既有设计，E.1 已给出零分配 `WriteFile` 兜底方案作为建议项，本批不擅自重写 |

结论：**无明确证据指向的新缺陷，未改 `ExceptionHandler.cpp` 逻辑**。

### 9.3 构建身份与依赖（P1-1）—— 验证通过

- `add_dependencies(Solstice generate_build_info)`：本批把 `CMakeLists.txt:315` 从 `${PROJECT_NAME}` 显式写为 `Solstice`（`project(Solstice)` 使其等价，显式化以对齐审查要求）。`generate_build_info` 是 `ALL` 自定义目标，每次构建重算 `build_info.h`；此依赖保证 `Solstice` 翻译单元（即 `#include <build_info.h>` 者）必在其后编译，Ninja 构建图中表现为 `Solstice` → `generate_build_info` 边，顺序正确。
- 两诊断选项始终输出 ON/OFF：`GenerateBuildInfo.cmake` 改为 `if (SOLSTICE_OPT_ALLOW_UNVERIFIED) ... 1 else ... 0` 与同理 AURA，OFF 与 undefined 都落 `0`，崩溃日志可区分。
- `WORKTREE_DIRTY` 实际定义：`GenerateBuildInfo.cmake:202-206` 由 `git status --porcelain` 非空 → `1` 否则 `0`；`ExceptionHandler.cpp` 以 `#ifndef SOLSTICE_WORKTREE_DIRTY #define 0` 兜底。

### 9.4 playUi 运行时防御复查（P1-2）—— 无缺陷，维持

| 检查项 | 结果 |
|---|---|
| 是否对不可读对象再次解引用 | ✅ 每次读 `obj/vtable/slot/target` 前均经 `isReadableRange(...)`（L44, L50, L58, L64） |
| `VirtualQuery` 与读取间 TOCTOU | ⚠️ 理论存在（探针式诊断的固有窗口）；用于崩溃后事后日志、非安全边界，可接受，未改 |
| 是否区分不同拒绝原因 | ✅ 5 类消息：object null / object 不可读 / vtable 不可读 / slot 不可读 / target 不可执行 + 未分类兜底 |
| 是否静默跳过必需操作 | ✅ `playUi` 是可选音效；`Solstice::init` 在调用前已完成初始化并打印 `Initialized!`，拒绝不会虚报初始化成功 |
| 线程 ID 记录 | ✅ `GetCurrentThreadId()` 在每条拒绝日志中记录 |
| 可执行地址验证 ≠ 函数语义验证 | ✅ 守卫仅证 target 可执行，不证 target==playUi；文档与注释已明确此边界 |

### 9.5 主状态维持

E.1.1 不触发任何代码层 slot/OpenGL/ESP 改动，主状态仍为 **NEEDS_RUNTIME_EXCEPTION_CONTEXT**（叠加 `BUILD_PROVENANCE_CONFIRMED` + `⚠️ RUNTIME_IMAGE_IDENTITY_UNVERIFIED`）。E.1.1 当时把 ABI 统一为 XMM0/XMM1 属文档一致性校正；该层 ① 类型推导 ABI 已被 E.1.2.1 用户裁定修订为 XMM2/XMM3（见 §11），不影响运行期行为。

> 详细交付（完整 Git Diff、ABI 前后对照、构建图验证、处理器/防御审查、未解决问题清单）见 `corrections-E1.1.md`。

## 10. Batch 3.6.2-E.1.2 证据纠错与溯源审计补遗（2026-11-08，基线 `b879cb1`）

E.1.2 是针对 E.1.1 的小范围审查纠错，仅改文档；不回退 `b879cb1`、不修改 slot 164 / playUi 调用逻辑 / ExceptionHandler / InstanceLocator / OpenGLGui / ESP / SDK 映射、不新增防御性 return。

### 10.1 ABI 证据纠错（P0 / §二 / §三）

E.1.1 曾据 slot-164 thunk `0x1CBC3E4`"自身不装载任何 XMM 寄存器"推断 volume/pitch 由调用方经 XMM0/XMM1 透传、从而"证明"XMM0/XMM1。**E.1.2 判定该论证方法学不成立并予以撤回**：一个只做 `mov rcx,[rcx+8]` → 取 vtable → `call [rip+...]` 的 trampoline 本就不装载 XMM，它只是**原样透传**调用方设置的 XMM 寄存器——因此"thunk 不装载 XMM"既不支持也不否定 XMM0/XMM1，不能作为二进制 corroboration 证据。

E.1.2 重新完整反汇编该 thunk（RVA `0x1CBC3E4`，6 条指令）确认：

```
0x1cbc3e4: mov rcx, [rcx+8]            ; this 调整后作为新 RCX
0x1cbc3e8: mov rax, [rcx]              ; rax = vtable
0x1cbc3eb: mov rax, [rax+0x520]        ; rax = vtable[164]
0x1cbc3f2: call qword [rip+0x6eb6398]  ; 经 IAT/间接指针调用目标
0x1cbc3f8: add rsp, 0x38
0x1cbc3fc: ret
```

四层证据现状（E.1.2 当时记录；层 ①/② 的 ABI 推导值已由 E.1.2.1 用户裁定修订为 XMM2/XMM3）：

| 层 | 证据 | volume | pitch | 状态 |
|---|---|---|---|---|
| ① 声明推导（E.1.2.1 修订） | `callVirtualFunc<void,const std::string&,float,float>` → `Fn=void(__thiscall*)(void*,const std::string&,float,float)`，实参 `(_this, soundName, volume, pitch)`；按参数整体序位（this=1, soundName=2, volume=3, pitch=4） | XMM2（参数序位第 3） | XMM3（参数序位第 4） | ✅ 确定（用户裁定 + 类型推导） |
| ② 编译器生成（E.1.2.1 修订） | MSVC 据 Windows x64 ABI 编译；XMM 编号对应参数在完整参数列表中的序位（E.1.2.1 用户裁定） | XMM2 | XMM3 | ✅ 确定（ABI 契约，按用户裁定） |
| ③ 游戏调用方参数准备 | thunk 不装载 XMM，仅透传；其调用者（装载 XMM 处）在混淆二进制中未能定位 | — | — | ⏳ **BINARY_ABI_CORROBORATION_PENDING** |
| ④ 目标函数读取寄存器 | `call [rip+...]` 间接跳转至混淆二进制中真实 `playUi` 实现，未能追踪到读取 XMM 处 | — | — | ⏳ **BINARY_ABI_CORROBORATION_PENDING** |

**结论与冲突说明（E.1.2 记录，已由 E.1.2.1 修订）**：

- 层 ①+②（声明 + ABI 契约，按 E.1.2.1 用户裁定）：确定性收敛为 **`volume=XMM2 / pitch=XMM3`**（按 C++ 函数类型与参数整体序位推导）。
- 层 ③+④ 缺失，故**当前没有"完整三层二进制验证"**；不得以层 ①+② 冒充完整验证（任务 §二 明确禁止）。
- 早期 E.1.1/E.1.2 文档以"浮点按类型独立编号"推导的 XMM0/XMM1 与任务 §二请求的 XMM2/XMM3 的冲突，已由 **E.1.2.1 用户最终裁定解决**——采用按参数整体序位编号（XMM2/XMM3）。层 ②③ 二进制 corroboration 仍标 PENDING，但其缺失**不改变**层 ① 的 ABI 推导结论（E.1.2.1 明确要求不得把二进制证据缺失解释为 ABI 规则不确定）。
- 任何情况下**不改动代码**：ABI 槽位不影响 `callVirtualFunc` 的 `vtable[index]` 调用机制（`__thiscall` 由编译器按 ABI 自动布参），文档结论变化不改变运行时行为。

### 10.2 RTTI 结论修正（§四）

E.1/E.1.1 曾表述"无 `.rdata`、RTTI 不可靠/不存在"。**E.1.2 纠正：不存在 `.rdata` 不等于不存在 RTTI。** E.1.2 在全镜像扫描中确认存在 **10,652 个 `Minecraft` 相关 `.?AV` TypeDescriptor 名称标记**（位于未命名大段 INITDATA/WRITE/EXECUTE 节区，而非 `.rdata`）。因此：

- RTTI 类型名数据**存在**；"RTTI 不存在"的否定性结论**撤销**。
- 但在混淆/加密二进制（自定义 `.winlice/.boot/.SCY`，大量 CODE+WRITE+EXECUTE 数据段）中做"COL → vtable → slot 164"的完整链解析**可靠性 UNVERIFIED**。
- 标记：**RTTI_RESOLUTION_UNVERIFIED**（不否定存在，也不主张已解析成功）。游戏侧 `playUi` 实况反汇编留待运行期 dump 补齐。

### 10.3 运行时镜像身份修正（§五）

原标记 `RUNTIME_IMAGE_MISMATCH` 被纠正为 **`RUNTIME_IMAGE_IDENTITY_UNVERIFIED`**：

- 运行期 EXE（`E:\pf\3.9.20.300505\Minecraft.Windows.exe`）在沙箱内**不可达**，其 SHA-256 **尚未取得、尚未与 `5fb2e893...026deb` 比对**。
- "无法访问"**不等于**"不匹配"。MISMATCH 只有在取得运行期 SHA 并比对**不一致**之后才能成立。现仅能标记 UNVERIFIED。
- 构建机后续需提供：① 运行 EXE SHA-256；② 静态分析镜像 SHA-256（`5fb2e893...026deb`）；③ PE ImageBase；④ SizeOfImage；⑤ 必要时解密后内存映像比对。

### 10.4 Ninja 构建依赖验证（§六）

- `CMakeLists.txt` 已显式写 `add_dependencies(Solstice generate_build_info SolsticeShaders)`（E.1.1 把 `${PROJECT_NAME}` 显式化为 `Solstice`，因 `project(Solstice)` 等价）。`generate_build_info` 是 `ALL` 自定义目标，每次构建重算 `build_info.h`；此依赖保证 `Solstice` 翻译单元（即 `#include <build_info.h>` 者）必在其后编译。在 Ninja 构建图中表现为 `Solstice` → `generate_build_info` 边，顺序正确（CMake 对 `add_dependencies` 的跨目标顺序约束 Ninja 会如实生成）。
- 实际 `ninja -t query Solstice` / `ninja -t query generate_build_info` 输出**无法在本沙箱取得**（无构建机、无 build.ninja）。故标记：**BUILD_GRAPH_RUNTIME_VERIFICATION_PENDING**。CMake 源级依赖正确，但运行时查询证据待构建机补齐。

### 10.5 主状态维持

E.1.2 仅改文档、不改动代码，主状态仍为 **NEEDS_RUNTIME_EXCEPTION_CONTEXT**（叠加 `BUILD_PROVENANCE_CONFIRMED` + `⚠️ RUNTIME_IMAGE_IDENTITY_UNVERIFIED`，原 `RUNTIME_IMAGE_MISMATCH` 已据 §10.3 更正为 UNVERIFIED）。E.1.2 维持 XMM0/XMM1；**该层 ① 类型推导 ABI 已由 E.1.2.1 用户裁定修订为 XMM2/XMM3**（详见 §11），二进制 caller/target 寄存器读取仍标 PENDING，不影响运行期行为。

### 10.6 未解决问题清单（E.1.2）

| 编号 | 问题 | 状态 | 阻塞/依赖 |
|---|---|---|---|
| O1 | 游戏调用方在 `call` 前把 volume/pitch 装进哪个 XMM（层 ③） | BINARY_ABI_CORROBORATION_PENDING | 需混淆二进制中定位 thunk `0x1CBC3E4` 的调用者（运行期 dump + 符号/脱壳） |
| O2 | playUi 目标函数从哪个 XMM 读取参数（层 ④） | BINARY_ABI_CORROBORATION_PENDING | 需追踪 `call [rip+0x6eb6398]` 间接目标到真实 `playUi` 实现 |
| O3 | 完整四层交叉验证是否一致 | 缺层 ③④，不成立 | O1+O2 |
| O4 | 运行期 EXE 与 unpacked 镜像是否同一构建 | RUNTIME_IMAGE_IDENTITY_UNVERIFIED | 构建机提供运行 EXE SHA-256 比对 `5fb2e893...026deb` |
| O5 | RTTI → vtable → slot 164 完整链在混淆二进制中是否可解析 | RTTI_RESOLUTION_UNVERIFIED | 需脱壳/解密后干净镜像或运行期 dump |
| O6 | build_info.h 先于 C++ 编译的 Ninja 边是否成立 | BUILD_GRAPH_RUNTIME_VERIFICATION_PENDING | 构建机 `ninja -t query` 输出 |
| O7 | 早期 E.1.1/E.1.2 的 XMM0/XMM1 推导与 E.1.2.1 裁定 XMM2/XMM3 的冲突 | **已由 E.1.2.1 用户裁定解决**（采用参数整体序位编号 XMM2/XMM3）；二进制 caller/target 寄存器读取仍 PENDING | O1+O2（补齐层 ②③ 字节级寄存器证据） |

> 详细交付（ABI 纠错、二进制证据缺口、RTTI 结论修正、运行时镜像身份修正、Ninja 依赖验证、未解决清单）见 `corrections-E1.1.2.md`。

## 11. Batch 3.6.2-E.1.2.1 ABI 最终裁定补遗（2026-11-08，基线 `67f82f0`）

E.1.2.1 是针对 E.1.2 的小范围 ABI 文档修订，**仅改文档**；不回退 `67f82f0`、不修改 slot 164 / playUi 调用逻辑 / ExceptionHandler / InstanceLocator / OpenGLGui / ESP / SDK 映射、不新增防御性 return。

### 11.1 ABI 最终裁定（P0 / §二）

- **层 ① 按当前 C++ 函数类型推导的 ABI（确定性结论）**：`this=RCX`、`soundName=RDX`、`volume=XMM2`、`pitch=XMM3`。依据（E.1.2.1 用户裁定）：隐式 `this` 是第一个参数；`XMM` 编号对应参数在**完整调用参数列表**中的序位（`playUi` 实参序位：1=this→RCX, 2=soundName→RDX, 3=volume→XMM2, 4=pitch→XMM3）。`__thiscall` 在 x64 被忽略，使用默认 Windows x64 ABI。与任务 §二 最初请求的 `RCX / RDX / XMM2 / XMM3` 一致。
- **层 ② 游戏二进制调用者的参数准备**（调用方在 `call` 之前把 volume/pitch 装进哪个 XMM）：**待验证（PENDING）**。
- **层 ③ 游戏目标函数实际读取的寄存器**（playUi 实现从哪个 XMM 取参）：**待验证（PENDING）**。
- **不得把二进制证据缺失解释为 ABI 规则不确定**：层 ②③ 的待验证状态**不改变**层 ① 的 ABI 推导结论（XMM2/XMM3）。
- 早期 E.1.1/E.1.2 以"浮点按类型独立编号"推导的 XMM0/XMM1，被 E.1.2.1 用户裁定**修订**为 XMM2/XMM3（按参数整体序位编号）。

### 11.2 保留的其他结论（不改动）

- RTTI 类型名数据**存在**（10,652 个 `Minecraft` 相关 `.?AV` 标记），但完整「COL → vtable → slot 164」链解析可靠性仍 **RTTI_RESOLUTION_UNVERIFIED**。
- 运行期镜像身份仍为 **RUNTIME_IMAGE_IDENTITY_UNVERIFIED**（非 MISMATCH）。
- slot-164 thunk `0x1CBC3E4` 不装载 XMM，既不支持也不否定任何寄存器编号，不构成二进制 corroboration 证据。
- slot 164 不变、playUi 不删除、ExceptionHandler 不改、OpenGLGui 不改、ESP 不改。

### 11.3 主状态维持

E.1.2.1 仅改文档、不改动代码，主状态仍为 **NEEDS_RUNTIME_EXCEPTION_CONTEXT**（叠加 `BUILD_PROVENANCE_CONFIRMED` + `⚠️ RUNTIME_IMAGE_IDENTITY_UNVERIFIED`）。层 ① 类型推导 ABI 现为 XMM2/XMM3（E.1.2.1 用户裁定），二进制 caller/target 寄存器读取仍 PENDING，不影响运行期行为。

> 详细交付（ABI 最终裁定、三层区分、一致性检查、未解决清单）见 `corrections-E1.2.1.md`。
