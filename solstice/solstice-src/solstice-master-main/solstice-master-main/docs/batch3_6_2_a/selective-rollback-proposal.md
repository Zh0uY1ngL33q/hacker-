# 选择性恢复 Aura 原始状态（提案框架）

本文件为**第二阶段**预案，仅在"Aura 隔离对照实验完成且指向 Aura 为诱因"后启用。当前（隔离已实现、对照实验待运行）**不执行任何恢复**，此处只给出最小 Diff 的方法论与边界，待人工批准。

## 前置条件（必须满足才进入本阶段）

1. `hang-comparison.md` 的隔离构建观测结果为"不再卡死"，且基线构建"仍卡死" → 因果关系初步成立。
2. 已具备 `f8c3541` 与当前版本的可比快照。

## 比较方法

- 以 `f8c3541` 为历史参照，逐个文件、逐个代码块对照 `Combat/Aura.{hpp,cpp}` 及 Aura 专属辅助逻辑的当前版本。
- 工具：`git diff f8c3541..HEAD -- src/Features/Modules/Combat/Aura.cpp src/Features/Modules/Combat/Aura.hpp` 等，仅限 Aura 专属文件。

## 允许恢复的范围（最小 Diff 原则）

- 仅 Aura 专属代码（`Aura` 类、`onEnable/onDisable/onTick`、攻击/旋转/选敌辅助方法、Aura 专属静态成员）。
- 仅恢复有**明确证据**说明其改动引入了卡死回归的代码块（例如某次提交改动了 `onBaseTickEvent` 的同步/锁定/异步行为、或引入了阻塞调用）。

## 禁止事项（任务第九条）

- ❌ 整体恢复多个历史提交（`git revert` / `git checkout` 一大段历史）。
- ❌ 恢复整个 `Features` 目录。
- ❌ 覆盖共享 SDK（`OffsetProvider/SigManager/InstanceLocator/ClientInstance/MinecraftSim` 等）。
- ❌ 覆盖 ESP 投影修复 / 诊断探针。
- ❌ 恢复未知的 Hook 修改（Aura 本身无专属 Hook，若发现历史 Hook 改动需单独论证）。

## 恢复后验证

- 在 `SOLSTICE_DISABLE_AURA_DIAGNOSTIC=OFF` 的构建上重新跑 DEBUG 放行 + 启动。
- 逐个恢复点回归：仅恢复一处 → 重新构建 → 复现/不复现卡死。二分定位具体引入回归的代码块。
- 若恢复后再次卡死，则该恢复点即为嫌疑点，进一步缩小到具体语句。

## 当前状态

**未执行任何恢复。** 等待 `hang-comparison.md` 的对照实验结果。若实验结果为 `HANG_PERSISTS_OUTSIDE_AURA`，则本阶段不启用，Aura 维持隔离，转向第八工作组其他排查方向。
