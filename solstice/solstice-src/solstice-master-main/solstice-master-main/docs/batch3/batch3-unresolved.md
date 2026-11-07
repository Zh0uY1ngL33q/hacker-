# Batch 3 未确认映射与停止条件

10 条映射保持 UNVERIFIED。逐条停止理由如下；这不是"跳过"，而是按任务书第 14 节停止条件得出的结论。

| 映射 | 停止条件 | 具体理由 | 替代路径 |
|---|---|---|---|
| `ClientInstance_getInputHandler` | 目标版本差异无法解释 / 多候选无法区分 | Signature has 0 hits in 1.21.120. The 1.21.44 call site contradicts the SDK callVirtualFunc(index,this) shape, and no 1.21.120 getter can be tied to ClientInputHandler by independent evidence. | getInputHandler() guards index<2//>=4096 and returns nullptr. |
| `ClientInstance_getMouseGrabbed` | 无消费者 / 建议废弃 | Dead mapping with no consumer. Distinct from MinecraftGame_mMouseGrabbed (still IMPLAUSIBLE); merging them or copying a field offset is forbidden. | Screen-name / cursor fallback is already authoritative. |
| `MinecraftSim_mGameSim` | 目标版本差异无法解释 / 多候选无法区分 | No RTTI/vtable to anchor a constructor scan; 1.21.44 value 0xE0 cannot be copied. No independent 1.21.120 member-initialisation evidence. | resolveGameSimOffset() performs a runtime structure probe and is used when the offset is 0. |
| `MinecraftSim_mRenderSim` | 目标版本差异无法解释 / 多候选无法区分 | GameSim and RenderSim must not be assumed to be the same type; no independent 1.21.120 evidence distinguishes two Simulation pointers. | getRenderSim() reuses the probed adjacent field via sRenderSimOffset. |
| `Level_getLevelData` | 目标版本差异无法解释 / 多候选无法区分 | Signature has 0 hits in 1.21.120; the 1.21.44 slot 160 cannot be copied. Comment in Level.cpp records a shared_ptr release sequence as a rejected near-miss. | getLevelData() guards vIndex<2//>=4096 and returns nullptr. |
| `Level_getPlayerList` | 目标版本差异无法解释 / 多候选无法区分 | The alleged member 0x918 is not a Level getter in 1.21.120; a similar STL-shaped member is not sufficient to identify the player list. | getPlayerList() guards the index and validates the returned map with isReadableMemory. |
| `Actor_mHurtTimeComponent` | 无消费者 / 建议废弃 | Dead mapping: the component is no longer stored as a raw Actor field. Finding an old offset would reintroduce a wrong access path. | entt component lookup is authoritative. |
| `Actor_mContainerManagerModel` | 目标版本差异无法解释 / 多候选无法区分 | Signature has 0 hits in 1.21.120. The two similarly named fields belong to different container systems and must not be merged. | Actor.cpp falls back to Actor_mContainerModel when this one is 0. |
| `BlockSource_getChunk` | 目标版本差异无法解释 / 多候选无法区分 | Un-explained divergence between versions: the 1.21.44 short wrapper is gone/inlined and slot 39 changed meaning. Selecting a slot would be a guess. | callVirtualFunc index guard keeps the unresolved value inert. |
| `BlockSource_clip` | 目标版本差异无法解释 / 多候选无法区分 | Several clip-like collision/ray functions exist; no independent evidence identifies which is the SDK target. | callVirtualFunc index guard keeps the unresolved value inert. |

## 建议废弃（不是修复目标）

- `ClientInstance_getMouseGrabbed`：源码中无任何 `OffsetProvider::` 引用；`ClientInstance::getMouseGrabbed()` 已改为屏幕名 + 系统光标可见性兜底。它与 `MinecraftGame_mMouseGrabbed`（仍 IMPLAUSIBLE）是不同对象，禁止合并或复制字段偏移。
- `Actor_mHurtTimeComponent`：源码中无任何引用；`Actor::getMobHurtTimeComponent()` 在 1.21.120 已走 entt 组件。强行找旧偏移会重新引入错误访问路径。

## 已解除

- `BlockSource_mBuildHeight` -> OK / 0x38（见 `batch3-report.md` 第 4 节）。

## 下一批建议

1. 用本轮建立的 120 RTTI vtable getter 表 + 构造函数数据流，独立定位 `Level_getPlayerList` / `Level_getLevelData` 的真实成员或槽位；不要沿用 44 的 160 / 302。
2. `ClientInstance_getInputHandler`：先确认 120 里 `ClientInputHandler` 的构造与持有者，再判断 getter 属于 ClientInstance 还是其成员对象。
3. `BlockSource_getChunk` / `clip`：在 120 里重新寻找调用点（44 的短包装器已被内联或改写），不要按槽号猜测。
4. `MinecraftSim_mGameSim` / `mRenderSim`：现有 `resolveGameSimOffset()` 运行时探测是替代路径；若要用静态偏移，需先找到 120 的 MinecraftSim 构造函数并确认两个 Simulation 指针的相邻关系。
5. 任何一条要转为 OK，都必须有目标版本自身证据 + 唯一命中 + 值由二进制解析；不得硬编码未经确认的数字。
