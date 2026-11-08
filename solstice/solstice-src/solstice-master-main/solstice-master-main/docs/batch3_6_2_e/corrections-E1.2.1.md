# Batch 3.6.2-E.1.2.1 ABI 最终裁定 — 交付文档

基线：`67f82f0`。本批**仅改文档**，不回退、不修改 slot 164 / playUi 调用逻辑 / ExceptionHandler / InstanceLocator / OpenGLGui / ESP / SDK 映射，不新增防御性 return。

---

## 1. 目标与范围

E.1.2.1 是 E.1.2 之后的小范围 ABI 文档修订。唯一目标：**把层 ①（按当前 C++ 函数类型推导的 ABI）由 XMM0/XMM1 修订为 XMM2/XMM3**，并明确三层区分，同时保留 E.1.2 已确认的所有其他结论（RTTI / 运行期镜像身份 / thunk 证据缺口 / slot / 代码不变）。

### 1.1 用户裁定（E.1.2.1）

目标声明：
```cpp
void MinecraftGame::playUi(
    const std::string& soundName,
    float volume,
    float pitch
);
```
函数指针：`Ret(__thiscall*)(void*, const std::string&, float, float)`。

Windows x64 参数位置（用户最终裁定）：
1. `this`：RCX（隐式第一参数）
2. `soundName`：RDX（引用=指针）
3. `volume`：**XMM2**（参数序位第 3）
4. `pitch`：**XMM3**（参数序位第 4）

判定要点（来自任务 §二）：寄存器类型不同，不代表寄存器编号独立分配；`XMM` 编号仍对应参数在**整个调用参数列表**中的位置；成员函数的隐式 `this` 是第一个参数；`__thiscall` 在 x64 被忽略，使用默认 Windows x64 ABI。

### 1.2 三层区分（任务明确要求）

| 层 | 含义 | 结论 |
|---|---|---|
| ① 按当前 C++ 函数类型推导的 ABI | 由声明 + 用户裁定推导 | **`volume=XMM2 / pitch=XMM3`**（确定性） |
| ② 游戏二进制调用者的参数准备 | 调用方在 `call` 前把 volume/pitch 装进哪个 XMM | **PENDING**（待验证） |
| ③ 游戏目标函数实际读取的寄存器 | playUi 实现从哪个 XMM 取参 | **PENDING**（待验证） |

**不得把二进制证据缺失解释为 ABI 规则不确定**：层 ②③ 的待验证状态**不改变**层 ① 的 ABI 推导结论（XMM2/XMM3）。

---

## 2. 与早期文档的冲突与解决

早期 E.1.1/E.1.2 以"浮点按类型独立编号"推导 `volume=XMM0 / pitch=XMM1`，与任务 §二 请求的 `XMM2/XMM3` 冲突。E.1.2.1 由用户最终裁定**解决该冲突**——采用按参数整体序位编号（XMM2/XMM3），与任务 §二 最初请求一致。

E.1.2 撤回的"thunk `0x1CBC3E4` 不装载 XMM 即证明 XMM0/XMM1"论证，在 E.1.2.1 同样适用其反命题：thunk 透传**不构成任何寄存器编号证据**（既不支持也不否定 XMM2/XMM3）。

---

## 3. 修改文件清单（相对 `67f82f0`，仅文档）

```
solstice/.../docs/batch3_6_2_e/playui-callsite-and-abi.md   (§4/§4.1/§4.2/§5：层 ① XMM2/XMM3 + 三层区分)
solstice/.../docs/batch3_6_2_e/slot164-validation.md        (§4：层 ① XMM2/XMM3 + PENDING)
solstice/.../docs/batch3_6_2_e/final-decision.md            (§2/§8.1/§9.1/§9.5/§10.1/§10.5/O7 + 新增 §11)
solstice/.../docs/batch3_6_2_e/corrections-E1.1.2.md        (§1.3/§1.4/§6 O7/§8：XMM2/XMM3 + PENDING)
solstice/.../docs/batch3_6_2_e/corrections-E1.md            (新增 §8：E.1.2.1 修订补遗)
solstice/.../docs/batch3_6_2_e/corrections-E1.1.md          (新增 §9：E.1.2.1 修订补遗)
solstice/.../docs/batch3_6_2_d/slot164-runtime-analysis.md  (§4：层 ① XMM2/XMM3 + PENDING)
solstice/.../docs/batch3_6_2_d/playui-callchain.md          (§5：层 ① XMM2/XMM3 + PENDING)
solstice/.../docs/batch3_6_2_d/final-decision.md            (§7：层 ① XMM2/XMM3 + PENDING)
solstice/.../docs/batch3_6_2_e/corrections-E1.2.1.md        (新增，本文件)
```

**实际 Git Diff（vs `67f82f0`，10 文件，纯文档，+N/-M）：** 见 §5。

---

## 4. 保留的其他结论（不改动）

- **RTTI**：类型名数据**存在**（10,652 个 `Minecraft` 相关 `.?AV` 标记），但完整「COL → vtable → slot 164」链解析可靠性仍 **RTTI_RESOLUTION_UNVERIFIED**。
- **运行期镜像身份**：仍为 **RUNTIME_IMAGE_IDENTITY_UNVERIFIED**（非 MISMATCH）。
- **thunk 证据缺口**：slot-164 thunk `0x1CBC3E4` 不装载 XMM，既不支持也不否定任何寄存器编号，不构成二进制 corroboration 证据；层 ②③ 仍 PENDING。
- **代码不变**：slot 164 不变、playUi 不删除、ExceptionHandler 不改、OpenGLGui 不改、ESP 不改、SDK 映射不改、不新增防御性 return。
- **不回退** `67f82f0`。

---

## 5. 一致性检查（E.1.2.1）

### 5.1 全量扫描结果

对 `docs/batch3_6_2_e/` 与 `docs/batch3_6_2_d/` 中所有仍声称 `volume=XMM0 / pitch=XMM1` 作为**当前结论**的文档进行核对：

- 权威当前结论文档（`playui-callsite-and-abi.md`、`slot164-validation.md`、`final-decision.md` §11、`corrections-E1.1.2.md`、`batch3_6_2_d` 三份）均已将层 ① 更新为 **XMM2/XMM3**。
- 历史归档文档（`corrections-E1.md` §2、`corrections-E1.1.md` §2、`final-decision.md` §9.1/§10.1 表格、`corrections-E1.1.2.md` §1.3 原记录）中的 XMM0/XMM1 仅作为**当时决策记录**保留，且每处均附 **E.1.2.1 修订提示 / 修订补遗**，明确指向当前权威结论（XMM2/XMM3）。
- 无任何文档将 XMM0/XMM1 作为**当前层 ① 类型推导 ABI** 的最终结论。
- 二进制层（层 ②③）在所有文档中统一标为 **BINARY_ABI_CORROBORATION_PENDING**，且与层 ① 明确区分，未把缺失解释为 ABI 规则不确定。

### 5.2 三层区分落实

全部相关文档均落实了任务要求的三个明确区分：
1. 层 ① 类型推导 ABI = XMM2/XMM3（确定性，用户裁定）。
2. 层 ② 游戏二进制调用方参数准备 = PENDING。
3. 层 ③ 游戏目标函数实际读取寄存器 = PENDING。

---

## 6. 主状态

**NEEDS_RUNTIME_EXCEPTION_CONTEXT**（叠加 `BUILD_PROVENANCE_CONFIRMED` + `⚠️ RUNTIME_IMAGE_IDENTITY_UNVERIFIED`；原 `RUNTIME_IMAGE_MISMATCH` 已于 E.1.2 更正为 UNVERIFIED）。层 ① 类型推导 ABI 现为 `XMM2/XMM3`（E.1.2.1 用户裁定），二进制 caller/target 寄存器读取仍 PENDING，不影响运行期行为。

> 收敛仍依赖：构建机从 main 重建 + 运行期身份采集（`O4`）+ 故障上下文复现（`O3` 根因分类）+ 运行期 dump 补齐层 ②③ 字节级寄存器证据（`O1/O2`）。
