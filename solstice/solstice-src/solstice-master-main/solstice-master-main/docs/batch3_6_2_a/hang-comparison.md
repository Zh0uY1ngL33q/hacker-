# 卡死对照实验（Hang Comparison）

本文件定义构建机/人工在隔离构建上执行的对照实验协议与观测矩阵。运行时结果由构建机填写；本交付物提供**协议**与**预期观测格式**。代码侧隔离已完成，状态见 `final-decision.md` 的 `AURA_ISOLATED_READY_FOR_DEBUG_TEST`。

## 实验前提

- 分支：`feat/aura-isolation-hang-diagnosis`（含 `SOLSTICE_DISABLE_AURA_DIAGNOSTIC` 开关）。
- 对照基线：`bf1522f`（未隔离，已知 f8c3541 后引入卡死）。
- 原始 DEBUG 人工放行保持不变：`Solstice should not be used in this state. / Type 'DEBUG' to continue, or press ENTER to exit.` 输入 `DEBUG` 后继续。

## 构建方式

```bash
# 隔离构建（仅此构建选项与基线不同）
cmake -S <src> -B <build-aura-off> -DSOLSTICE_DISABLE_AURA_DIAGNOSTIC=ON
cmake --build <build-aura-off> --config Release

# 对照构建（保持现状，复现卡死）
cmake -S <src> -B <build-baseline>        # 不传该选项
cmake --build <build-baseline> --config Release
```

## 观测矩阵（每次运行填写）

| 观测项 | 基线构建（Aura 在） | 隔离构建（Aura 不在） |
|--------|--------------------|----------------------|
| 控制台是否打印 DEBUG 放行提示 | ✅/❌ | ✅/❌ |
| 输入 DEBUG 后进程是否继续 | ✅/❌ | ✅/❌ |
| 游戏画面是否继续更新 | ✅/❌ | ✅/❌ |
| 游戏音效是否继续更新 | ✅/❌ | ✅/❌ |
| FeatureManager 是否完成初始化 | ✅/❌ | ✅/❌ |
| ModuleManager 是否完成初始化 | ✅/❌ | ✅/❌ |
| HookManager 是否完成初始化 | ✅/❌ | ✅/❌ |
| ESP 诊断是否有机会运行 | ✅/❌ | ✅/❌ |
| crash.log 是否更新 | ✅/❌ | ✅/❌ |

## 若隔离后仍卡死（HANG_PERSISTS_OUTSIDE_AURA）

不扩大禁用范围。抓取**两份完整 Hang Dump**（不依赖 crash.log），按第八工作组继续排查：
- 其他模块构造函数
- HookManager / MinHook 线程冻结与恢复
- Dispatcher 事件分发
- 初始化锁 / 同步对象扫描
根据真实调用栈定位阻塞线程。

## 若隔离后不再卡死（指向 Aura 为诱因）

- 初步结论：卡死与 Aura 执行路径存在因果关系。
- 进入第九工作组"选择性恢复 Aura 原始状态"：比较 `f8c3541` 与当前版本，逐文件/逐代码块审查，仅恢复 Aura 专属且有明确证据的代码（见 `selective-rollback-proposal.md`）。
- 注意：本任务明确规定**禁止整体恢复多个历史提交、禁止恢复整个 Features 目录、禁止覆盖共享 SDK/ESP 修复**。

## 结果记录（构建机填写）

```
基线构建结果: <HANG | OK>
隔离构建结果: <HANG | OK>
结论: <AURA_ISOLATED_HANG_GONE | HANG_PERSISTS_OUTSIDE_AURA | INCONCLUSIVE>
阻塞线程(若仍卡死): <调用栈摘要/线程 id>
```

> 注：本文件为协议模板。实际运行结果待构建机执行后回填；本交付物当前状态为"隔离已实现、待 DEBUG 对照测试"。
