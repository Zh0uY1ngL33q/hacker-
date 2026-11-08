# 最终判定 — Batch 3.6.2-D 第十工作组

## 1. 最终状态枚举

```
ROOT_CAUSE_CONFIRMED_NEEDS_BUILD
```

**含义**：静态分析已锁定「最小防御修复 + 故障诊断」并已落地；但运行期 AV 的精确分类
（槽位版本错配 vs 对象脱钩 vs 其他）需要下一次构建的捕获上下文确认，故标为「已确认方向、待构建验证」。

## 2. 判定依据

1. **对象先经 `isPlausibleGame` 校验**（第三工作组）——对象合理性已确认，不是「乱指针对空调用」。
2. **slot 164 四层静态验证全过**（第四工作组）——映射推导正确，164 是真实 vtable 索引，无需改动。
3. **剩余未知量**：运行期游戏版本的 vtable 在 slot 164 是否仍是正确函数，或 `this` 是否可读但脱钩。
   这两类都能通过 `isPlausibleGame` 却仍会在虚调用内崩——正是 `isValidVirtualCall` 新闸门覆盖的情形。
4. **崩溃栈已排除 GL 死锁误判**：`initThread → Solstice::init → playUi` 是 init 线程，
   与 GL 渲染挂起线程 `tid=0x1c20` 不同线程（第六工作组 / 第七工作组）。
5. **patchFullStack `0x0 [Invalid address]` 是独立良性缺陷**，不并入 playUi AV（第六工作组）。

## 3. 最小修复（已落地，未提交）

- `ExceptionHandler.cpp`：新增故障上下文捕获（ExceptionAddress / AccessType / FaultAddress / 寄存器）。
- `MinecraftGame.cpp`：`playUi` 虚调用前加 `isValidVirtualCall` 闸门。
- 未删调用、未盲改映射、未回退已有成果（第七/九工作组）。

## 4. 构建机下一步

1. **重新配置 CMake**（让 `SOLSTICE_BUILD_VERSION` 对齐 HEAD，消除版本字符串滞后），再构建。
2. 构建并注入，复现 AV。
3. 读取新 `crash.log`：
   - 若 `AccessType=execute` 且 `FaultAddress == vtable[164]` 目标落在无效/非可执行页
     → 槽位版本错配确认；下一步针对**实际运行 exe** 重新推导 disp32。
   - 若 `AccessType=read/write` 且 `FaultAddress` 落在 `this`/vtable 附近
     → 对象脱钩；下一步查 `InstanceLocator` 缓存生命周期。
4. 核对 `SOLSTICE_SOURCE_FINGERPRINT` 是否与所用源码一致；若不一致，说明二进制被错标。

## 5. 验收对照（任务第十一组）

- ✅ 第一工作组：73da588 vs 07b3d86 差异已解释（配置期冻结，祖先关系，非归因错误）；两宏已确认注册/默认 OFF/消费点存在。
- ✅ 第二工作组：playUi 全链追踪完成（声明/实现、direct vs callVirtualFunc、对象、InstanceLocator、init 阶段、线程、ABI）。
- ✅ 第三工作组：对象校验链与边界说明完成。
- ✅ 第四工作组：slot 164 四层验证完成，未盲改数字。
- ✅ 第五工作组：真实 AV 细节捕获机制落地（ExceptionAddress/Information/寄存器）。
- ✅ 第六工作组：patchFullStack 独立良性缺陷确认，未合并。
- ✅ 第七工作组：GL 与 init 不同线程确认；未改 OpenGLGui。
- ✅ 第八工作组：最小修复仅基于证据，未删调用、未盲改映射。
- ✅ 第九工作组：Batch 1-3 / 3.5 / 3.6 / Aura 隔离 / GL 诊断 / DEBUG 放行均保护，无全量回退。
- ✅ 第十工作组：本目录 8 份交付物齐备。

## 6. 一句话结论

运行期 AV 根因被收敛为「对象合理但虚表落点不可信」这一类，已由 `isValidVirtualCall` 防御 +
故障上下文诊断覆盖；slot 164 推导正确不改，根因精确分类留给下一次构建的捕获数据，
现状判定为 `ROOT_CAUSE_CONFIRMED_NEEDS_BUILD`。
