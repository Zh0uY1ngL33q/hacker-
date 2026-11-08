# 3.6.2-F — Minimal Fix Summary / 修改与验证结果

## 1. 结论：本轮**没有**实施任何根因修复

原因：按 §十三 停止条件，"如果无法证明根因：停止修改，不扩大禁用范围"。
本轮仓库侧只取得了**静态源码证据**，没有运行期 `BEGIN/END` 日志、没有 Dump、
没有 WER 异常地址，因此**不具备判定具体故障指令/具体订阅者的证据**。
本轮交付的是**定位能力**（订阅者级诊断）+ **静态调查结论**，不是修复。

- `patchFullStack at 0x0`：判定 `BENIGN_SIGNATURE_MISS`，`DEFINE_PATCH_FUNC`
  已对 `addr == 0` 安全早返回 → **不实施最小防御修复**（见
  `patchfullstack-callchain.md`）。
- 未禁用任何视觉模块，未恢复 Aura 历史代码，未通过 SEH 掩盖 AV。

## 2. 修改清单

### 2.1 源码（3 个文件，+135 行，全部为纯新增 / `#ifdef` 保护）

| 文件 | 改动 |
|---|---|
| `CMakeLists.txt` | 新增 `option(SOLSTICE_RENDER_DIAG ... OFF)`；仅 ON 时 `add_compile_definitions`。 |
| `include/nes/include/nes/event_dispatcher.hpp` | 新增 `diagnostic_trigger()`（`#ifdef SOLSTICE_RENDER_DIAG`），与 `trigger()` 同锁同顺序，仅在 `invoke` 前后调用 `logFn`。 |
| `src/Hook/Hooks/RenderHooks/OpenGLGui.cpp` | 新增 3 个 include、flush-on-write 诊断记录器 + 地址查表；GL HUD 派发点按宏分支（前 8 帧走诊断，之后回退原 `trigger()`）。 |

**宏关闭时**：`CMakeLists.txt` 不定义宏 → 两个源文件里的诊断代码全部被预处理器剔除，
派发语句逐字回到 `gFeatureManager->mDispatcher->trigger(holder);`，**发布二进制零差异**。

### 2.2 工具脚本（3 个，仅用于生成静态交付物，不参与编译）

- `tools/batch3_6_2_f_gen.py` — 从 `ModuleManager.cpp` / `listen<>` / `pch.hpp` 解析静态订阅名单与优先级。
- `tools/batch3_6_2_f_audit.py` — 逐回调词法扫描（13 类构造）+ 配置覆盖机制说明。
- `tools/batch3_6_2_f_mapping_risk.py` — 生成订阅者 ↔ SDK getter ↔ 映射状态关联。

### 2.3 文档（10 个，`docs/batch3_6_2_f/`）

见 `final-decision.md` 的交付物清单。

## 3. 诊断代码自身的正确性修正（非游戏根因修复）

在提交前复核源码时发现并修正了两处**会引入新问题的实现缺陷**：

1. **`diagnostic_trigger()` 误加 `const`** → `listener_list::begin()/end()`
   （`event_dispatcher.hpp:141-142`）只有非 const 版本，const 成员函数遍历
   `mListeners` 无法编译。已改为非 const，与 `trigger()` 一致。
2. **把类型擦除的 `void*` 直接 `static_cast<Module*>` 后 deref** → 若某个
   RenderEvent 监听者不是 `Module` 派生对象，会误读内存并制造新的 AV，
   违背"定位而非掩盖异常"。已改为 `renderDiagOpen()` 一次性构建
   `dynamic_cast<void*>` 地址表，`nameFn` 只查表、未命中返回 `(non-module)`。
3. 附带：`renderDiagOpen()` 原本用 `spdlog::info` 报告文件打开；因
   `OpenGLGui::render()` 全程持有 `glGuiMutex`，已改为直接写诊断文件，
   **整条诊断路径不经过 spdlog**，避免"持有未知锁时走可能再取锁的日志路径"。

## 4. 编译结果

**本机（代码/分析机）未产生编译产物**——实测无 `cl` / `ninja` / `g++`，
仅有 Python wheel 提供的 `cmake`；目标 EXE 与 `CMakeCache.txt` 均不在本机。
按项目工作流，**编译与注入由构建机执行**，本轮不伪报编译成功。

构建机命令：

```bat
cmake -S . -B build -DSOLSTICE_RENDER_DIAG=ON
cmake --build build --config Release
```

对照构建（验证宏关闭时零差异）：

```bat
cmake -S . -B build_release -DSOLSTICE_RENDER_DIAG=OFF
cmake --build build_release --config Release
```

**源码级静态复核（已逐项验证）**：

- `diagnostic_trigger()` 非 const，可遍历 `mListeners`；
  `mDispatcher->get<RenderEvent>()` 返回 `nes::dispatcher<RenderEvent>&`
  （`event_dispatcher.hpp:237-241`），可调用非 const 成员。
- `event_listener::invoke()` 为 `const` 方法，`mInstance` / `mMethodHash` 为 public。
- `dynamic_cast<void*>` 要求 `Module` 多态（有纯虚 `getTypeID()`，满足）；
  项目已启用 RTTI（`ModuleBase::TypeID()` 使用 `typeid`）。
- 显式补齐 `<cstdio>` / `<unordered_map>` / `<Features/Modules/Module.hpp>`，
  不依赖间接包含顺序。
- `glGuiFrameCounter` 为 `unsigned`（`OpenGLGui.cpp:146`），仅 GL 线程读写。

## 5. 测试结果（本轮实际执行）

| 检查 | 结果 |
|---|---|
| `#if/#endif` 配对（3 文件） | CMakeLists 0/0、event_dispatcher 3/3、OpenGLGui 2/2，全部平衡 |
| `{}` 括号计数（3 文件） | 145/145、54/54、143/143，全部平衡 |
| `SOLSTICE_RENDER_DIAG` 出现次数 | event_dispatcher 1、OpenGLGui 2（均为 `#ifdef`） |
| 3 个 JSON 交付物 `json.load` | 全部通过 |
| 3 个 Python 脚本 `py_compile` | 全部通过 |
| 新增 include 目标存在性 | `src/Features/Modules/Module.hpp` = True |
| 订阅者名单生成 | 39 个 RenderEvent 订阅者，9 个优先级层级 |
| 逐回调审计 | **39/39 成功隔离 `onRenderEvent` 函数体**（含 Interface / Watermark） |
| 审计构造命中（描述性，非危险度排序） | sdkGetter 33、nullDerefCandidate 29、mutex 4、entityEnumeration 4、imguiBeginEnd 2、fileIO 1；networkIO / sleepWait / glCalls / recursiveEvent / unboundedLoop / threadSync = 0 |
| 未跟踪资产 | 保留：现有日志、ZIP、审计材料均未纳入本轮提交 |

> 审计为**词法扫描**，"命中"只表示该构造出现在回调源码中，**不表示**它会执行、
> 有危险、或是故障点；未命中也不等于不存在。空指针 / 对象生命周期需人工语义复核。
> `nullDerefCandidate` 的 29 项是宽泛的 `get()->` 形态候选，不是已确认的空解引用。

## 6. 约束遵守清单（§十 / §十一）

| 约束 | 状态 |
|---|---|
| 不修改 `OffsetProvider` 映射数值 | 遵守（未触碰） |
| 不修改 slot 164 / `playUi` | 遵守 |
| 不修改 `InstanceLocator` 架构 | 遵守（仅静态审查） |
| 不修改 OpenGL 渲染架构 / ESP 投影 / FOV | 遵守 |
| 不批量禁用视觉模块 / 不恢复 Aura 历史代码 | 遵守 |
| 不修改 DEBUG 人工放行逻辑 | 遵守 |
| 不通过 SEH 捕获后继续运行来掩盖 AV | 遵守（诊断未加任何 try/catch） |
| 保留原有 SDK、映射、ESP、Aura 隔离、GL 诊断成果 | 遵守（全部为纯新增） |
| 不回退 `794fc65` / 不 force push | 遵守 |
| 只暂存本轮相关文件 | 遵守 |

## 7. 未解决问题

1. **`RENDER_SUBSCRIBER_IDENTIFIED` / `FIRST_UNFINISHED_SUBSCRIBER` 尚未成立**：
   需构建机回传 `solstice_render_diag.log`（工作组二诊断产物）。
2. **层②③ 二进制 ABI 佐证**（历史 O1/O2）、**运行期镜像身份**（O4，
   `RUNTIME_IMAGE_IDENTITY_UNVERIFIED`）仍未收敛，需 EXE SHA / dump。
3. **73 条不可用映射**本轮不修复；仅建立"第一帧订阅者 → getter → 映射状态"
   关联（`mapping-risk-for-render-subscribers.json`），且明确"映射不可信 ≠
   模块必崩"。
4. **配置覆盖**：`Prefs->mDefaultConfigName` 在运行机上，静态无法确认哪些模块
   首帧启用；`defaultEnabled` 仅为构造默认值。
5. **本机无编译工具链**，编译/链接结果待构建机回填；若有编译错误需按实际
   诊断修整（当前仅完成源码级复核）。

> 完整实际 Git Diff（源码 3 文件，197 行 diff 输出 / +135 行）见
> `final-decision.md` 附录 A；亦可用
> `git show <本轮提交> -- CMakeLists.txt include/nes/include/nes/event_dispatcher.hpp src/Hook/Hooks/RenderHooks/OpenGLGui.cpp`
> 复现。
