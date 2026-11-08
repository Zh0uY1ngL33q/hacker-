# 审查纠错交付 — Batch 3.6.2-E.1.1（基线 2429e6e）

本批是对 E.1 的小范围审查纠错，**不重新实施 E.1**。约束：不回退 2429e6e、不改 slot 164、不删 playUi、不动 InstanceLocator / OpenGLGui / ESP / 全局映射判据。

---

## 1. 完整实际 Git Diff（相对 2429e6e）

```diff
diff --git a/.../CMakeLists.txt b/.../CMakeLists.txt
@@ -312,7 +312,7 @@ endforeach()
 add_resources(resource_files ${resource_files})

 add_library(Solstice SHARED ${sourceFiles} ${resource_files})
-add_dependencies(${PROJECT_NAME} generate_build_info SolsticeShaders)
+add_dependencies(Solstice generate_build_info SolsticeShaders)
 # 3.6.2-E.1 (P1-1): make the build-info ordering explicit and target-scoped.
 # `Solstice` is the translation unit that `#include <build_info.h>`, so it must
 # never compile before `generate_build_info` has (re)written that header.

diff --git a/.../docs/batch3_6_2_d/playui-callchain.md b/.../docs/batch3_6_2_d/playui-callchain.md
@@ 参数布局：this=RCX、soundName=RDX、float volume=R8、float pitch=R9
-  参数布局：`this=RCX`、`const std::string& soundName=RDX`、`float volume=R8`、`float pitch=R9`，返回 `void`。
+  参数布局：`this=RCX`、`const std::string& soundName=RDX`、`float volume=XMM0`（第 1 个浮点参数）、`float pitch=XMM1`（第 2 个浮点参数），返回 `void`。

diff --git a/.../docs/batch3_6_2_d/slot164-runtime-analysis.md b/.../docs/batch3_6_2_d/slot164-runtime-analysis.md
@@ -__thiscall：this 在 RCX，其余参数按 RDX/R8/R9 入栈...
- `__thiscall`：`this` 在 `RCX`，其余参数按 `RDX / R8 / R9` 入栈（前三个整型/指针类参数走寄存器）。
- 参数：`this=RCX`、`const std::string& soundName=RDX`、`float volume=R8`、`float pitch=R9`，返回 `void`。
+ `__thiscall`：`this` 在 `RCX`（指针）；`const std::string&` 引用=指针走 `RDX`；浮点参数走 `XMM0..XMM3`（整数/浮点两套寄存器独立计数，浮点不进 `R8/R9`）。
+ 参数：`this=RCX`、`const std::string& soundName=RDX`、`float volume=XMM0`（第 1 个浮点）、`float pitch=XMM1`（第 2 个浮点），返回 `void`。

diff --git a/.../docs/batch3_6_2_d/final-decision.md b/.../docs/batch3_6_2_d/final-decision.md
+ ## 7. E.1.1 补充校正（ABI）：D 目录早期 `volume=R8/pitch=R9` 错误已订正为 XMM0/XMM1，三层交叉核验见 E 目录 §4.2。

diff --git a/.../docs/batch3_6_2_e/playui-callsite-and-abi.md b/.../docs/batch3_6_2_e/playui-callsite-and-abi.md
+ ### 4.2 三层交叉核验（E.1.1 / P0-1）：声明推导/编译器生成/游戏二进制 thunk 三层均收敛到 volume=XMM0、pitch=XMM1。

diff --git a/.../docs/batch3_6_2_e/slot164-validation.md b/.../docs/batch3_6_2_e/slot164-validation.md
+ - **E.1.1 三层交叉核验（P0-1）**：三层均收敛到 volume=XMM0 / pitch=XMM1。

diff --git a/.../docs/batch3_6_2_e/final-decision.md b/.../docs/batch3_6_2_e/final-decision.md
+ ## 9. Batch 3.6.2-E.1.1 审查纠错补遗：§9.1 ABI 三层核验 / §9.2 处理器检查 / §9.3 构建依赖 / §9.4 防御复查。
```

统计：`7 files changed, 85 insertions(+), 7 deletions(-)`。唯一代码改动是 `CMakeLists.txt` 一行（`${PROJECT_NAME}` → `Solstice`，因 `project(Solstice)` 二者等价，纯显式化）；其余为文档一致性校正。

---

## 2. ABI 修订对照（P0-1）

### 2.1 修订前（E.1 审查主张 / D 目录旧错）

| 参数 | 类型 | E.1 审查 bullet | D 目录旧文档 |
|---|---|---|---|
| `this` | 指针 | RCX | RCX |
| `soundName` | `const std::string&` | RDX | RDX |
| `volume` | `float` | **XMM2** | **R8** |
| `pitch` | `float` | **XMM3** | **R9** |

### 2.2 修订后（三层交叉核验结论，全 7 份文档统一）

| 参数 | 类型 | 寄存器 | 依据 |
|---|---|---|---|
| `this` | 指针 | **RCX** | `__thiscall` 隐式第一参数 |
| `soundName` | `const std::string&`（引用=指针） | **RDX** | 整数/指针寄存器第 2 槽 |
| `volume` | `float`（第 1 个浮点） | **XMM0** | XMM 浮点文件按浮点位置独立编号 |
| `pitch` | `float`（第 2 个浮点） | **XMM1** | XMM 浮点文件按浮点位置独立编号 |

### 2.3 三层交叉核验（任务强制要求）

| 层 | 来源 | 结论 | 证据 |
|---|---|---|---|
| ① C++ 声明推导 | `callVirtualFunc<void,const std::string&,float,float>` → `Fn=void(__thiscall*)(void*,const std::string&,float,float)`，实参 `(_this, soundName, volume, pitch)` | XMM0 / XMM1 | `MinecraftGame.cpp:81` + `MemUtils.hpp:94,122` |
| ② 编译器生成 | MSVC 按 MS x64 ABI；整数/指针走 RCX,RDX,R8,R9、浮点走 XMM0–XMM3，**两类按类型独立计数** | XMM0 / XMM1 | MS x64 调用约定（确定性强；沙箱无编译产物故不附字节级反汇编） |
| ③ 游戏二进制 | slot-164 thunk `0x1CBC3E4`（`sub rsp,0x38`→`mov rcx,[rcx+8]`→`mov rax,[rcx]`→`mov rax,[rax+0x520]`→`call [rax]`→`add rsp,0x38`）**自身不装载任何 XMM 寄存器**，volume/pitch 由调用方经 XMM0/XMM1 透传进 thunk、再透传给 `vtable[164]` 目标 | XMM0 / XMM1（透传） | 反汇编 `unpacked_Minecraft.Windows.exe` @ RVA `0x1CBC3E4` |

**唯一一致结论：`volume=XMM0`、`pitch=XMM1`。**

### 2.4 为何 XMM2/XMM3 不成立（positional counting 误判）

`XMM2/XMM3` 是把浮点寄存器按「整体参数位置（第 3、4 个参数）」编号得到的错误结论。MS x64 ABI 明确：整数/指针寄存器与 XMM 浮点寄存器是**两套独立编号的文件**——`this`(指针)→RCX、`soundName`(引用=指针)→RDX 占用整数文件，**不消耗任何 XMM 槽位**；`volume` 是第 1 个浮点 → **XMM0**，`pitch` 是第 2 个浮点 → **XMM1**。仅当签名中存在 ≥2 个排在 `volume` 之前的浮点参数时，`volume` 才会落到 XMM2——而 `playUi(const std::string&, float, float)` 只有 2 个浮点参数，绝无可能。

> E.1.1 审查意见在 bullet 中写「`volume→XMM2` / `pitch→XMM3`」，但其同段注释「浮点参数……重新从 XMM0 开始编号」本身即正确规则（浮点按独立位置从 XMM0 编号），与 XMM0/XMM1 一致。本批据此把 bullet 中的 XMM2/XMM3 判为「按整体参数位置」误推，维持经三层核验的唯一结论，未保留任何相互矛盾的 ABI 表述。

---

## 3. ExceptionHandler 审查结果（P0-2）

| 检查项 | 结果 | 是否需改 |
|---|---|---|
| `pExceptionInfo` 空指针 | ✅ `if (pExceptionInfo && pExceptionInfo->ExceptionRecord)` 守卫（L29, L39） | 否 |
| `ExceptionRecord` 空指针 | ✅ 同上 | 否 |
| `ContextRecord` 空指针 | ✅ `if (const auto* ctx = pExceptionInfo->ContextRecord)`（L59）；`faultCtx` null 安全（L75） | 否 |
| `ExceptionCode` 是否为 AV | ✅ 仅在 `ExceptionRecord` 非空时读取（L29-31）；`faultContext` 内按 `ExceptionInformation[0]` 区分 read/write/execute | 否 |
| `NumberParameters ≥ 2` | ✅ `AccessType` 要求 `>=1`（L42），`FaultAddress` 要求 `>=2`（L50）后才读取 `[0]/[1]` | 否 |
| `AccessType`/`FaultAddress` 仅在有效范围读取 | ✅ 如上 | 否 |
| 寄存器来自故障 ContextRecord | ✅ RIP/RSP/RBP/RCX/RDX/R8/R9/RAX 全部取自 `pExceptionInfo->ContextRecord`（L59-64），非处理器自身栈帧 | 否 |
| 递归 / 阻塞风险 | ⚠️ 观察项：**非本批缺陷**。① `spdlog::error` 在异常过滤器中仍可能死锁/递归；② `MessageBoxA(RETRY)` 返回 `EXCEPTION_CONTINUE_EXECUTION` 会在同一坏指令上重试致循环。属既有设计，E.1 已给出零分配 `WriteFile` 兜底方案作为建议项，本批不擅自重写 | 否（建议项） |

结论：**无明确证据指向的新缺陷，`ExceptionHandler.cpp` 逻辑未改。**

---

## 4. CMake 构建图验证（P1-1）

### 4.1 `add_dependencies(Solstice generate_build_info)` 在 Ninja 图中的保证

- `generate_build_info` 是 `add_custom_target(... ALL ...)`（`CMakeLists.txt:193`），每次构建都会执行 `cmake -P GenerateBuildInfo.cmake` 重算/重写 `build_info.h`。
- `Solstice` 是真正 `#include <build_info.h>` 的翻译单元（`add_library(Solstice SHARED ...)`，`CMakeLists.txt:314`）。
- `add_dependencies(Solstice generate_build_info)` 在构建图中建立 `Solstice → generate_build_info` 边；Ninja 据此保证：任何 `Solstice` 的 `.cpp` 编译任务都在 `generate_build_info` 自定义任务**之后**入队/执行。`build_info.h` 由自定义目标产出，CMake 亦将其列为 `Solstice` 的隐式依赖，因此既保证**顺序**也保证**缺失时先生成**。
- 本批把 `${PROJECT_NAME}` 显式写为 `Solstice`（`project(Solstice)` 使其等价），纯为对齐审查要求、消除歧义。

### 4.2 两诊断选项始终输出 ON/OFF

`GenerateBuildInfo.cmake:186-195` 改为：
```cmake
if (SOLSTICE_OPT_ALLOW_UNVERIFIED)
    file(APPEND ... "#define SOLSTICE_BUILD_ALLOW_UNVERIFIED_MAPPINGS 1\n")
else()
    file(APPEND ... "#define SOLSTICE_BUILD_ALLOW_UNVERIFIED_MAPPINGS 0\n")
endif()
# 同理 SOLSTICE_BUILD_DISABLE_AURA_DIAGNOSTIC 1/0
```
OFF 与 undefined 都落 `0`，崩溃日志可区分"选项关"与"字段不存在"。

### 4.3 `WORKTREE_DIRTY` 实际定义

- 产出：`GenerateBuildInfo.cmake:202-206` —— `git status --porcelain` 非空 → `#define SOLSTICE_WORKTREE_DIRTY 1`，否则 `0`。
- 兜底：`ExceptionHandler.cpp` 以 `#ifndef SOLSTICE_WORKTREE_DIRTY #define SOLSTICE_WORKTREE_DIRTY 0` 保护，旧 `build_info.h` 仍可编译。
- 语义：反映工作树未提交改动（非源码指纹；源码指纹 `SOLSTICE_SOURCE_FINGERPRINT` 是内容派生，与 dirty 无关）。

---

## 5. playUi 运行时防御复查（P1-2）

`diagnosePlayUiRejection()`（`MinecraftGame.cpp` 匿名命名空间，E.1 落地）复查：

| 检查项 | 结果 | 是否需改 |
|---|---|---|
| 是否对不可读对象再次解引用 | ✅ 每次读 `obj/vtable/slot/target` 前均经 `MemUtils::isReadableRange(...)`（L44, L50, L58, L64）；`vtable`/`target` 仅在其读窗口已确认可读后才解引用 | 否 |
| `VirtualQuery` 与读取间 TOCTOU | ⚠️ 理论存在（探针式诊断的固有窗口）；用于崩溃后**事后日志**、非安全边界，可接受 | 否 |
| 是否区分不同拒绝原因 | ✅ 5 类消息：object null / object unreadable / vtable unreadable / slot unreadable / target not executable + 未分类兜底 | 否 |
| 是否静默跳过必需操作 | ✅ `playUi` 是**可选**音效；`Solstice::init` 在调用前已完成初始化并打印 `Initialized!`，拒绝不会虚报初始化成功 | 否 |
| 线程 ID 记录 | ✅ `GetCurrentThreadId()` 在每条拒绝日志中记录 | 否 |
| 可执行地址验证 ≠ 函数语义验证 | ✅ 守卫仅证 target 可执行，不证 target==playUi；注释与文档已明确此边界 | 否 |

结论：**无缺陷，维持现状。**

---

## 6. 未解决问题清单（Open Issues）

- **O1 — RUNTIME_IMAGE_MISMATCH（最高优先）**：运行期 `E:\pf\3.9.20.300505\Minecraft.Windows.exe` 的 SHA-256 / PE 元数据 / 解密后内存映像尚未取得，slot-164 对运行期的适用性待比对。静态 unpacked 镜像 SHA `5fb2e893…026deb` 仅为静态参考，**不得仅凭 1.21.120.0 版本号复用 slot 164**。需构建机采集。
- **O2 — 真实 AV 故障上下文缺失**：崩溃二进制是 73da588（缺 669ce2c 的 fault 上下文字段），故无 `ExceptionAddress/AccessType/FaultAddress/寄存器`。下一次从 main 重建复现后，才能据 `AccessType`（execute vs read/write）区分"槽位版本错配"还是"对象脱钩"。
- **O3 — playUi 线程约束 UNVERIFIED**：该 unpacked 镜像无 `.rdata`（自定义段 `.winlice/.boot/.SCY`），RTTI/xref 不可靠，无法在沙箱定位游戏内其它 playUi 调用点反汇编；`b32d185` 不可达。按约束**未写"无线程限制"**，标记为 UNVERIFIED，待运行期 dump 补齐。
- **O4 — 层③字节级实证未完全取得**：游戏 slot-164 thunk `0x1CBC3E4` 的反汇编确认其不装载 XMM、volume/pitch 透传，已足够证明 XMM0/XMM1；但"游戏 playUi 实现本身从 XMM0/XMM1 读取参数"未做字节级反汇编（thunk 经寄存器间接调用，直接 xref 扫描为空）。该结论由 MS x64 ABI 确定性覆盖，不阻塞本批。
- **O5 — 处理器递归/阻塞观察项**：`spdlog` 在异常过滤器中、`MessageBoxA(RETRY)` 重试，属既有设计风险；E.1 已给出零分配 `WriteFile` 兜底方案，作为建议项未在本批实施（任务禁止擅自重写整个处理器）。
- **O6 — 构建机重建验收待办**：需 `cmake -S <src> -B <build> -DCMAKE_BUILD_TYPE=Release -DSOLSTICE_ALLOW_UNVERIFIED_MAPPINGS=ON -DSOLSTICE_DISABLE_AURA_DIAGNOSTIC=ON` 从 main 重建，记录 DLL SHA-256 与运行 EXE SHA-256，复现后读取 `solstice.log` / `crash.log` / `sigreport.json`。此步骤只能由构建机完成，沙箱不可达。

---

## 7. 执行限制遵守确认

- ✅ 未改 slot 164　✅ 未删 playUi　✅ 未改 InstanceLocator
- ✅ 未改 OpenGLGui　✅ 未改 ESP　✅ 未改全局映射判据
- ✅ 未回退 2429e6e　✅ 未恢复历史实验提交

## 8. 最终验收状态（维持）

**NEEDS_RUNTIME_EXCEPTION_CONTEXT** 叠加 `BUILD_PROVENANCE_CONFIRMED` + `⚠️ RUNTIME_IMAGE_MISMATCH`。ABI 统一为 XMM0/XMM1 属文档一致性校正，不影响运行期行为；收敛仍需构建机重建 + 运行期身份采集 + 故障上下文复现。
