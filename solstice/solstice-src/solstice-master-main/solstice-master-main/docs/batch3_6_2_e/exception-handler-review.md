# 异常处理器审查 — Batch 3.6.2-E 第六/八工作组（669ce2c ExceptionHandler.cpp）

## 1. 新增字段审查（故障上下文捕获）

`TopLevelExceptionHandler` 现在在构造崩溃文本前提取（`ExceptionHandler.cpp:32-59`）：

- `ExceptionAddress`、`AccessType`（`ExceptionInformation[0]`：0=read / 1=write / 8=execute）、`FaultAddress`（`ExceptionInformation[1]`）、`NumberParameters`；
- 寄存器 `RIP/RSP/RBP/RCX/RDX/R8/R9/RAX`，取自 `pExceptionInfo->ContextRecord`（**故障瞬间上下文，非 Handler 当前上下文**）。

逐项核对：

| 检查 | 结论 |
|---|---|
| `ExceptionPointers` 有效性 | `TopLevelExceptionHandler` 是 Windows SEH 顶层过滤器，Windows 必提供有效 `pExceptionInfo`，实践中安全 |
| `ExceptionRecord` 是否为空 | `faultContext` 块内 `if (const auto* rec = pExceptionInfo->ExceptionRecord)` 已守卫；但第 25 行 `auto exceptionCode = pExceptionInfo->ExceptionRecord->ExceptionCode` **未守卫**（实践中不过，建议改成先判空再取 code） |
| `ContextRecord` 是否为空 | 第 52 行 `if (const auto* ctx = pExceptionInfo->ContextRecord)` 已守卫 ✅ |
| `NumberParameters` 是否足够 | `AccessType` 仅当 `>=1`、`FaultAddress` 仅当 `>=2` 才解析 ✅（AV 恰有 2 个参数） |
| 仅 AV 时解析 AccessType/FaultAddress | 是，只在 `rec` 存在时解析 ✅ |
| `ExceptionInformation[0]/[1]` 解释正确 | 0/1/8 映射 read/write/execute；`[1]` 为出错地址 ✅ |
| 寄存器取自故障 ContextRecord | 是，非 Handler 当前上下文 ✅ |

## 2. 处理器自身可靠性（任务重点）

`TopLevelExceptionHandler` 在故障上下文中继续做：

- `std::string text = ... + faultContext + "Stack Trace:\n"` —— `std::string` 分配 + `fmt::format`；
- `StackWalker sw;` + `sw.ShowCallstack(...)` —— 分配、可能加锁；
- `spdlog::error(text)` —— spdlog 取内部 registry/mutex；若故障恰发生在该 mutex 持有时 → **死锁**；spdlog 也分配内存；
- `makeCrashLog(...)` —— `std::ofstream` 打开、遍历 `gFeatureManager->mModuleManager->getModules()` 等——更多分配；若故障源于堆损坏/OOM → 进一步失败，可能**递归触发过滤器**（二次机会）；
- `MessageBoxA` —— 若无非交互会话（服务/无桌面）会挂起。

这些都是「在 SEH 顶层过滤器里做重活」的真实风险，与本次 playUi AV 无直接因果，但会在未来任何崩溃里降低崩溃日志的可靠性。

## 3. 最小独立异常记录方案（建议，未写入源）

任务要求：若现有日志路径可能死锁，提出最小独立方案（如预开文件句柄 + 低开销 `WriteFile`）。建议如下，**不重写整个 ExceptionHandler**，仅在处理器最顶部、重活之前加一段**零分配**原始记录：

```cpp
// 在 TopLevelExceptionHandler 顶部，spdlog/fmt 路径之前
namespace {
    void WriteFaultRaw(PEXCEPTION_POINTERS p) {
        if (!p || !p->ExceptionRecord) return;
        const auto* r = p->ExceptionRecord;
        char b1[160];
        int n1 = wsprintfA(b1, "FAULT raw code=0x%X addr=0x%p type=%d fault=0x%p\r\n",
                           (unsigned)r->ExceptionCode, (void*)r->ExceptionAddress,
                           (unsigned)(r->NumberParameters>=1?r->ExceptionInformation[0]:0),
                           (void*)(r->NumberParameters>=2?r->ExceptionInformation[1]:0));
        char b2[200]; int n2 = 0;
        if (p->ContextRecord) {
            const auto* c = p->ContextRecord;
            n2 = wsprintfA(b2, "FAULT regs RIP=0x%p RSP=0x%p RCX=0x%p RDX=0x%p R8=0x%p R9=0x%p RAX=0x%p\r\n",
                           (void*)c->Rip,(void*)c->Rsp,(void*)c->Rcx,(void*)c->Rdx,(void*)c->R8,(void*)c->R9,(void*)c->Rax);
        }
        // 路径：<Solstice 目录>/crash.log（可用 GetSolsticeDir 取得，失败则回退固定名）
        HANDLE h = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD w; if (n1>0) WriteFile(h, b1, (DWORD)n1, &w, nullptr);
            if (n2>0) WriteFile(h, b2, (DWORD)n2, &w, nullptr);
            CloseHandle(h);
        }
    }
}
```
- 全程 `wsprintfA` + `WriteFile`，**不碰 spdlog、不分配 std::string、不抛 C++ 异常**；
- 路径若用 `FileUtils::getSolsticeDir()` 仍会分配，可改为在 `init()` 时**预打开并缓存一个 HANDLE**（最稳），或回退到已知固定路径；
- 现有 spdlog/fmt/makeCrashLog 路径保留为 best-effort。

> 本批**未将此方案写入源**：因为沙箱无法用 MSVC 编译验证，且任务要求「不要重写整个 ExceptionHandler」。它作为经过审查的**建议**交付，由构建机应用并编译/测试。

## 4. 审查结论

- 3.6.2-D 新增的故障上下文字段**正确且完整**（寄存器取自故障 ContextRecord，AV 才解析 AccessType/FaultAddress）。
- 唯一小硬化点：第 25 行取 `ExceptionCode` 前也应判空 `ExceptionRecord`（实践中安全，建议顺手补）。
- 处理器在故障上下文里做 spdlog/fmt/ofstream 重活存在死锁/递归风险，已给出零分配 `WriteFile` 替代方案（建议项，未在源中实现）。
