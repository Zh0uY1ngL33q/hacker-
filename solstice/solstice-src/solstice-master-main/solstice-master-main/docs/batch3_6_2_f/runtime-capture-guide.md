# 3.6.2-F — Runtime Capture Guide（工作组七）

## 原则

本轮的**首选证据**是现有日志 + 新增的 `SOLSTICE_RENDER_DIAG` 订阅者 `BEGIN/END`
诊断（见 `subscriber-instrumentation.md`）。Dump 只在前两者不足时启用。

- **无 Dump / 无异常上下文，不得宣称找到了具体故障指令。**
- **只有最后一个 `BEGIN` 没有对应 `END`，只能标记 `FIRST_UNFINISHED_SUBSCRIBER`，
  不能直接等同于"已证明死锁"。** 回调可能正在阻塞、被换出、执行长循环，或已经
  触发 AV 但尚未写 WER。
- 日志"最后一行"只是**最后一个成功记录的阶段**，不等于故障指令位置。

## A. 诊断构建（构建机执行）

**要跑的命令**

```bat
cmake -S . -B build -DSOLSTICE_RENDER_DIAG=ON
cmake --build build --config Release
```

确认 `CMakeCache.txt` 中 `SOLSTICE_RENDER_DIAG:BOOL=ON`，并记录同一次构建的
`SOLSTICE_DISABLE_AURA_DIAGNOSTIC`、`SOLSTICE_ALLOW_UNVERIFIED_MAPPINGS` 取值，
以及生成的 `build_info.h` 内容（对应 `runtime-build-identity.md` 的身份字段）。

**回传什么**

- 构建日志（成功/失败、警告）。
- `CMakeCache.txt` 中全部 `SOLSTICE_*` 项。
- EXE / 注入 DLL 的 SHA-256、`dumpbin /headers` 的 `ImageBase` / `SizeOfImage`。
- 运行后 `%TEMP%\solstice_render_diag.log` 全文。
- 同一次运行的 `solstice.log`、`crash.log`、`sigreport.json`。

**自查口径**

- 诊断日志出现 `[RENDER-DIAG] opened ... handle=OK` 才算记录器生效；
  `handle=INVALID` 说明文件打开失败，BEGIN/END 未落盘，不能用于判定。
- 日志中 `modules=` 数量应与 `render-subscriber-inventory.json` 的 39 个量级相当
  （实际启用集由 config 决定，不要求完全相等）。

## B. 若再次卡死：保持进程存活并取现场

1. **不要结束游戏进程**（否则丢失线程现场）。
2. System Informer（原 Process Hacker）→ 进程属性 → Threads：
   - 记录 **GL `wglSwapBuffers` / OpenGLGui::render 线程**（上次日志 `tid=0x34d8`）。
   - 记录 **WndProc / 主线程**。
   - 记录各订阅者执行线程（诊断日志每行的 `tid=` 字段）。
   - 关注 `Wait:Executive` / `Wait:UserRequest` 与 CPU 占用（`!runaway` 对应项）。
3. WinDbg 附加（非侵入优先，避免改变时序）或 ProcDump 抓全量 dump：

```bat
procdump -ma -e -w Minecraft.Windows.exe <输出目录>
procdump -ma <PID> <输出目录>\render_hang.dmp
```

4. WinDbg 现场命令：

```text
.symfix
.reload
!analyze -hang
~* kb
!locks
!runaway 7
```

补充（针对本轮 GL 路径）：

```text
~<GL线程号>s
kb
!address 0x13134090        // 仅用于工作组五的独立 playUi 对象核查
```

5. 若随后闪退：保存 WER 事件（`事件查看器 → Windows 日志 → 应用程序`，
   `Windows Error Reporting`，含异常码 `0xC0000005`、异常地址、faulting module），
   并保留 `%LOCALAPPDATA%\CrashDumps` 下的 dump。

**回传什么**

- 每个线程的栈（`~* kb` 全文，或至少 GL 线程 + 主线程）。
- `!locks` 输出（判断是否为锁竞争 / 递归锁升级）。
- `!analyze -hang` 结论。
- WER 事件截图/文本（异常码、异常地址、模块 + 偏移）。
- 诊断日志最后 20 行（含最后一个 `BEGIN`）。

**自查口径**

- **只有拿到 dump / WER 异常地址，才能谈"具体故障指令"**；否则只能说
  "停在哪个订阅者"。
- 注意区分：`tid=0x34d8`（RenderEvent）与 `threadId=21392`（playUi guard）
  是两个线程，后者见 `minecraftgame-object-followup.md`，**不得合并根因**。

## C. 判定矩阵

| 观察到的证据 | 允许的状态标记 | 不允许的结论 |
|---|---|---|
| 最后一条为 `phase=BEGIN`，无对应 `END` | `FIRST_UNFINISHED_SUBSCRIBER` | "已证明死锁" / "已找到故障指令" |
| 所有订阅者 `BEGIN/END` 配对，但无 `HUD_DISPATCH_END` | `DISPATCHER_INTERNAL_FAILURE`（查 `trigger` 返回后、`glHangMarker`、日志路径、锁） | 猜某个模块 |
| 日志仍停在 `HUD_DISPATCH_BEGIN`，且**无任何**订阅者记录 | `DISPATCHER_INTERNAL_FAILURE` 优先（查 Dispatcher 自身 / `mMutex` / `mWriteLock` / 诊断文件打开失败） | 猜测具体模块 |
| 全部 `BEGIN/END` 配对且 `HUD_DISPATCH_END` 出现，但停在 `IMGUI_RENDER_BEGIN` | 转查 **ImGui Render / GL Submit**（下一阶段） | 归咎订阅者 |
| 进程闪退、有 WER/dump 异常地址 | 结合 `BEGIN` + 异常地址定位指令 | 仅凭最后一个 BEGIN 断言崩溃点 |
| 诊断日志为空 / `handle=INVALID` | `NEEDS_RUNTIME_CAPTURE` | 任何根因结论 |

## D. 静态证据 vs 运行时证据（本轮强制区分）

| 内容 | 类型 | 可否作为根因依据 |
|---|---|---|
| `render-subscriber-order.json` / `inventory.json` 的顺序与审计 | 静态源码解析 | **否**；仅给出候选与检查清单 |
| `mapping-risk-for-render-subscribers.json` | 静态关联 | **否**；映射状态不可信 ≠ 模块必崩 |
| `patchfullstack-callchain.md` 的线程推断 | 静态推断 | **否**；`[error]` 行无 tid |
| `Interface` 已执行的推断 | 静态推断（依赖日志归属假设） | **否** |
| `phase=BEGIN/END` 日志 | 运行时记录（条件：诊断文件已成功打开且已 flush） | 可定位到**订阅者边界**，非指令 |
| Dump / WER 异常地址 + 栈 | 运行时证据 | **是**（唯一可谈具体故障指令者） |

> 本轮仓库侧工作全部属于**静态证据 + 诊断工具交付**。根因判定必须等构建机回传
> A/B 的运行时材料后再收口，当前最终状态为 `NEEDS_RUNTIME_CAPTURE`。
