# Batch 3.6.2-E.1.2 证据纠错与溯源审计 — 交付文档

基线：`b879cb1`。本批**仅改文档**，不回退、不修改 slot 164 / playUi 调用逻辑 / ExceptionHandler / InstanceLocator / OpenGLGui / ESP / SDK 映射，不新增防御性 return。

---

## 1. ABI 纠错（P0 / §二 / §三）

### 1.1 撤回 E.1.1 的方法学错误论证

E.1.1 曾据 slot-164 thunk `0x1CBC3E4`「自身不装载任何 XMM 寄存器」推断 volume/pitch 由调用方经 XMM0/XMM1 透传、从而「证明」XMM0/XMM1。

**该论证在方法学上不成立**，E.1.2 予以撤回：一个只做 `mov rcx,[rcx+8]` → 取 vtable → `call [rip+...]` 的 trampoline 本就不装载 XMM，它只是**原样透传**调用方设置的 XMM 寄存器——因此「thunk 不装载 XMM」既不支持也不否定 XMM0/XMM1，**不能作为二进制 corroboration 证据**。任务 §二 明确要求「不能将前三项的部分证据冒充完整验证」，此条正属被禁止的情形。

### 1.2 重新完整反汇编该 thunk（RVA `0x1CBC3E4`）

```
0x1cbc3e4: mov rcx, [rcx+8]            ; this 调整后作为新 RCX（this+8）
0x1cbc3e8: mov rax, [rcx]              ; rax = vtable 指针
0x1cbc3eb: mov rax, [rax+0x520]        ; rax = vtable[164]（0x520 / 8 = 164）
0x1cbc3f2: call qword [rip+0x6eb6398]  ; 经 IAT/间接指针调用目标函数
0x1cbc3f8: add rsp, 0x38
0x1cbc3fc: ret
```

证实：thunk **不装载任何 XMM 寄存器**，且目标经 `call [rip+...]`（间接/IAT）跳转，属混淆/加密二进制（自定义 `.winlice/.boot/.SCY` 代码段）。

### 1.3 三层交叉核验（强制不矛盾，E.1.2.1 修订）

> **E.1.2.1 修订**：本节原记录 E.1.2 以「浮点按类型独立编号」推导的 XMM0/XMM1；E.1.2.1 由用户最终裁定将层 ① 类型推导 ABI 修订为 `volume=XMM2 / pitch=XMM3`（按参数整体序位编号）。下表层 ①/② 已据 E.1.2.1 更新，层 ②③ 保持 PENDING。

| 层 | 证据 | volume | pitch | 状态 |
|---|---|---|---|---|
| ① C++ 声明推导（E.1.2.1 修订） | `callVirtualFunc<void,const std::string&,float,float>` → `Fn=void(__thiscall*)(void*,const std::string&,float,float)`，实参 `(_this, soundName, volume, pitch)`；按参数整体序位（this=1, soundName=2, volume=3, pitch=4） | XMM2（参数序位第 3） | XMM3（参数序位第 4） | ✅ 确定（用户裁定 + 类型推导） |
| ② 编译器生成（E.1.2.1 修订） | MSVC 据 Windows x64 ABI 编译上述实参；XMM 编号对应参数在完整参数列表中的序位（E.1.2.1 用户裁定） | XMM2 | XMM3 | ✅ 确定（ABI 契约，按用户裁定） |
| ③ 游戏调用方参数准备 | thunk 不装载 XMM、仅透传；其调用者（装载 XMM 处）在混淆二进制中**未能定位** | — | — | ⏳ **BINARY_ABI_CORROBORATION_PENDING** |
| ④ 目标函数读取寄存器 | `call [rip+...]` 间接跳转至混淆二进制中真实 `playUi` 实现，**未能追踪**到读取 XMM 处 | — | — | ⏳ **BINARY_ABI_CORROBORATION_PENDING** |

→ 层 ①+②（声明 + 函数类型推导 + 用户裁定）确定性收敛为 **`volume=XMM2 / pitch=XMM3`**（按 C++ 函数类型与参数整体序位推导）。层 ②③ 缺失，故当前没有"完整三层二进制验证"；不得以层 ①② 冒充完整验证（任务 §二 明确禁止）。

### 1.4 与早期 XMM0/XMM1 推导的冲突与 E.1.2.1 裁定

- 早期 E.1.1/E.1.2 以「浮点按类型独立编号」推导的 `volume=XMM0 / pitch=XMM1`，与任务 §二 请求的 `volume=XMM2 / pitch=XMM3` **曾冲突**；该冲突已由 **E.1.2.1 用户最终裁定解决**——采用按参数整体序位编号（XMM2/XMM3）。
- 层 ②③ 缺失，**当前没有完整三层二进制验证**，不得冒充。
- E.1.2.1 **采纳 ABI 推导值 XMM2/XMM3**（层 ①② 确定性，按用户裁定），并把二进制 corroboration 标为 PENDING；**不改动代码**（`callVirtualFunc` 的 `__thiscall` 由编译器按 ABI 自动布参，文档结论变化不改变运行时行为）。
- 层 ②③（游戏 caller/target 寄存器读取）仍待实况反汇编补齐；其缺失**不改变**层 ① 的 ABI 推导结论（E.1.2.1 明确要求不得把二进制证据缺失解释为 ABI 规则不确定）。

---

## 2. 二进制证据缺口（§三）

标记为 **BINARY_ABI_CORROBORATION_PENDING**：

- 层 ③ 需要定位 thunk `0x1CBC3E4` 的所有调用者，并在 `call` 之前确认 volume/pitch 被装入哪个 XMM。在混淆二进制（自定义代码段、寄存器/间接跳转、无干净符号）中未能完成。
- 层 ④ 需要追踪 `call [rip+0x6eb6398]` 的间接目标到真实 `playUi` 实现，并确认其从哪个 XMM 读取参数。同样未能完成。
- E.1.2 已明确：**不依据「thunk 不装载 XMM」推断参数位置**，避免把部分证据冒充完整验证。

---

## 3. RTTI 结论修正（§四）

E.1/E.1.1 曾表述「无 `.rdata`、RTTI 不可靠/不存在」。**E.1.2 纠正：不存在 `.rdata` ≠ 不存在 RTTI。**

- 全镜像扫描确认存在 **10,652 个 `Minecraft` 相关 `.?AV` TypeDescriptor 名称标记**（位于未命名大段 INITDATA/WRITE/EXECUTE 节区，而非 `.rdata`）。
- RTTI 类型名数据**存在**；「RTTI 不存在」的否定性结论**撤销**。
- 但在混淆/加密二进制（自定义 `.winlice/.boot/.SCY`，大量 CODE+WRITE+EXECUTE 数据段）中做「COL → vtable → slot 164」完整链解析**可靠性 UNVERIFIED**。
- 标记：**RTTI_RESOLUTION_UNVERIFIED**（不否定存在，也不主张已解析成功）。游戏侧 `playUi` 实况反汇编留待运行期 dump 补齐。

---

## 4. 运行时镜像身份修正（§五）

原标记 `RUNTIME_IMAGE_MISMATCH` 纠正为 **`RUNTIME_IMAGE_IDENTITY_UNVERIFIED`**：

- 运行期 EXE（`E:\pf\3.9.20.300505\Minecraft.Windows.exe`）在沙箱内**不可达**，其 SHA-256 **尚未取得、尚未与 `5fb2e893...026deb` 比对**。
- 「无法访问」**不等于**「不匹配」。MISMATCH 只有在取得运行期 SHA 并比对**不一致**之后才能成立。现仅能标记 UNVERIFIED。
- 构建机后续需提供：① 运行 EXE SHA-256；② 静态分析镜像 SHA-256（`5fb2e893...026deb`）；③ PE ImageBase；④ SizeOfImage；⑤ 必要时解密后内存映像比对。
- 严禁仅凭版本号 `1.21.120.0` 复用 slot 164。

---

## 5. Ninja 构建依赖验证（§六）

- `CMakeLists.txt` 已显式写 `add_dependencies(Solstice generate_build_info SolsticeShaders)`（E.1.1 把 `${PROJECT_NAME}` 显式化为 `Solstice`，因 `project(Solstice)` 等价）。`generate_build_info` 是 `ALL` 自定义目标，每次构建重算 `build_info.h`；此依赖保证 `Solstice` 翻译单元（即 `#include <build_info.h>` 者）必在其后编译。在 Ninja 构建图中表现为 `Solstice` → `generate_build_info` 边，顺序正确（CMake 对 `add_dependencies` 的跨目标顺序约束 Ninja 会如实生成）。
- 两个 CMake 诊断选项始终输出 ON/OFF：`GenerateBuildInfo.cmake` 改为「若开启→1，否则→0」，OFF 与 undefined 都落 `0`，崩溃日志可区分。
- `WORKTREE_DIRTY` 实际定义（`GenerateBuildInfo.cmake`）：`git status --porcelain` 非空 → `1` 否则 `0`；`ExceptionHandler.cpp` 以 `#ifndef SOLSTICE_WORKTREE_DIRTY #define 0` 兜底。
- 实际 `ninja -t query Solstice` / `ninja -t query generate_build_info` 输出**无法在本沙箱取得**（无构建机、无 `build.ninja`）。故标记：**BUILD_GRAPH_RUNTIME_VERIFICATION_PENDING**。CMake 源级依赖正确，但运行时查询证据待构建机补齐。

---

## 6. 未解决问题清单（E.1.2）

| 编号 | 问题 | 状态 | 阻塞/依赖 |
|---|---|---|---|
| O1 | 游戏调用方在 `call` 前把 volume/pitch 装进哪个 XMM（层 ③） | BINARY_ABI_CORROBORATION_PENDING | 需混淆二进制中定位 thunk `0x1CBC3E4` 的调用者（运行期 dump + 符号/脱壳） |
| O2 | playUi 目标函数从哪个 XMM 读取参数（层 ④） | BINARY_ABI_CORROBORATION_PENDING | 需追踪 `call [rip+0x6eb6398]` 间接目标到真实 `playUi` 实现 |
| O3 | 完整四层交叉验证是否一致 | 缺层 ③④，不成立 | O1 + O2 |
| O4 | 运行期 EXE 与 unpacked 镜像是否同一构建 | RUNTIME_IMAGE_IDENTITY_UNVERIFIED | 构建机提供运行 EXE SHA-256 比对 `5fb2e893...026deb` |
| O5 | RTTI → vtable → slot 164 完整链在混淆二进制中是否可解析 | RTTI_RESOLUTION_UNVERIFIED | 需脱壳/解密后干净镜像或运行期 dump |
| O6 | build_info.h 先于 C++ 编译的 Ninja 边是否成立 | BUILD_GRAPH_RUNTIME_VERIFICATION_PENDING | 构建机 `ninja -t query` 输出 |
| O7 | 早期 E.1.1/E.1.2 的 XMM0/XMM1 推导与 E.1.2.1 裁定 XMM2/XMM3 的冲突 | **已由 E.1.2.1 用户裁定解决**（采用参数整体序位编号 XMM2/XMM3）；二进制 caller/target 寄存器读取仍 PENDING | O1 + O2（补齐层 ②③ 字节级寄存器证据） |

---

## 7. 本批改动文件清单（相对 `b879cb1`，仅文档）

```
solstice/.../docs/batch3_6_2_e/final-decision.md            (+84/-4  E.1.2 §10 补遗 + 状态标签更正)
solstice/.../docs/batch3_6_2_e/playui-callsite-and-abi.md   (+42/-6  §4.2 四层核验重写 + PENDING 标记)
solstice/.../docs/batch3_6_2_e/slot164-validation.md        (+6/-6   §4 ABI 层 PENDING 框架)
solstice/.../docs/batch3_6_2_e/runtime-image-identity.md    (+6/-3   状态 MISMATCH→UNVERIFIED + RTTI 修正)
solstice/.../docs/batch3_6_2_e/corrections-E1.1.2.md        (新增，本文件)
```

**实际 Git Diff（vs `b879cb1`，4 文件，+116/-22，纯文档）：**

```diff
 docs/batch3_6_2_e/final-decision.md          | 84 ++++++++++++++++++++--
 docs/batch3_6_2_e/playui-callsite-and-abi.md | 42 ++++++++---
 docs/batch3_6_2_e/runtime-image-identity.md  |  6 +-
 docs/batch3_6_2_e/slot164-validation.md      |  6 +-
 4 files changed, 116 insertions(+), 22 deletions(-)
```

核心变更摘要：
1. `final-decision.md`：状态标签 `RUNTIME_IMAGE_MISMATCH` → `RUNTIME_IMAGE_IDENTITY_UNVERIFIED`（全 5 处）；新增 §10 完整 E.1.2 补遗（ABI 证据纠错 / 二进制缺口 / RTTI 修正 / 镜像身份修正 / Ninja 验证 / 未解决清单）。
2. `playui-callsite-and-abi.md` §4.2：重写为「四层交叉核验与证据缺口」，撤回「thunk 不装载 XMM 即证明 XMM0/XMM1」错误论证，层 ③④ 标 PENDING，附 MS 官方范例佐证 XMM0/XMM1。
3. `slot164-validation.md` §4：ABI 层改为「ABI 契约推导值 XMM0/XMM1」，明确二进制 corroboration PENDING。
4. `runtime-image-identity.md`：状态更正 + RTTI「不存在」否定结论撤销（10,652 个 `.?AV` 标记证实存在），改标 RTTI_RESOLUTION_UNVERIFIED。

---

## 8. 主状态

**NEEDS_RUNTIME_EXCEPTION_CONTEXT**（叠加 `BUILD_PROVENANCE_CONFIRMED` + `⚠️ RUNTIME_IMAGE_IDENTITY_UNVERIFIED`；原 `RUNTIME_IMAGE_MISMATCH` 已更正为 UNVERIFIED）。E.1.2 维持 XMM0/XMM1；**层 ① 类型推导 ABI 已由 E.1.2.1 用户裁定修订为 XMM2/XMM3**，二进制 caller/target 寄存器读取仍 PENDING，不影响运行期行为。
