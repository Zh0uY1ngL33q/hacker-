# 异常上下文分析 — Batch 3.6.2-D 第五工作组

## 1. 旧处理器缺什么

原 `ExceptionHandler.cpp` 只记录了 `ExceptionCode` + StackWalker 栈回溯，
**没有** `ExceptionAddress`、`ExceptionInformation`、也没有寄存器。
这就是为什么历史 `crash.log` 只有「崩了 + 栈」却缺「崩在哪、怎么崩的」——无法区分
读/写/执行，也无法定位故障地址，更分不清故障现场与处理器自身栈帧。

## 2. 3.6.2-D 新增的故障上下文捕获

在 `ExceptionHandler.cpp` 构造 `text` 之前，新增从 `EXCEPTION_POINTERS` 提取：

| 字段 | 来源 | 含义 |
|---|---|---|
| `ExceptionAddress` | `rec->ExceptionAddress` | 触发异常的指令地址 |
| `AccessType` | `rec->ExceptionInformation[0]` | 0=读, 1=写, 8=执行 |
| `FaultAddress` | `rec->ExceptionInformation[1]` | 被访问（出错的）地址 |
| `NumberParameters` | `rec->NumberParameters` | 参数个数 |
| `RIP/RSP/RBP/RCX/RDX/R8/R9/RAX` | `ctx->ContextRecord` | 故障瞬间寄存器 |

输出片段：
```
ExceptionAddress: 0x...
AccessType: read|write|execute
FaultAddress: 0x...
NumberParameters: 2
RIP=0x... RSP=0x... RBP=0x...
RCX=0x... RDX=0x... R8=0x... R9=0x... RAX=0x...
```

## 3. 怎么读（针对 playUi AV）

- **AccessType=read/write，FaultAddress 落在 `this`/vtable 附近** → 对象或虚表解引用失败
  （对象脱钩 / vtable 指针坏）。
- **AccessType=execute，FaultAddress == vtable[164] 目标，且落在非可执行/无效页** →
  槽位版本错配，跳进了无效代码。
- **故障现场在 `ExceptionAddress`，不是处理器的栈帧**：StackWalker / memcpy / spdlog 等
  是处理器的自有栈帧，**不能**误当成根因。根因看 `ExceptionAddress` + `FaultAddress`。

## 4. 与本批崩溃的关系

现有崩溃栈 `initThread → Solstice::init → MinecraftGame::playUi → TopLevelExceptionHandler`
只告诉我们「故障发生在 playUi 路径内部」，但**只有**新的 `ExceptionAddress / FaultAddress /
AccessType` 才能告诉我们是「坏虚表解引用」还是「跳进无效代码」。
下一次构建复现后，凭这段上下文即可把根因收敛到具体一类。

## 5. 注意

本次崩溃日志来自**尚未包含本修复**的构建（两次编辑未提交）。
新故障上下文捕获只在下一次构建生效——它价值在于「下一次复现时能精确分类」，
而非解释已经产生的旧日志。
