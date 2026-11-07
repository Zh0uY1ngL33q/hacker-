# Batch 3 模块恢复准备报告

> 状态只反映**静态依赖证据**。全局 `SOLSTICE_ALLOW_UNVERIFIED_MAPPINGS=OFF` 仍在生效，FeatureManager 未初始化，因此 READY 不等于可以启用。

## 分类统计

| 状态 | 模块数 | 含义 |
|---|---:|---|
| READY | 29 | 必要依赖已有充分静态证据 |
| NEEDS_RUNTIME_TEST | 83 | 静态证据足够，实际行为未验证 |
| BLOCKED | 19 | 至少一个依赖映射仍非 OK |

默认启用状态与 readiness **分开记录**：`true` 8 / `false` 118 / unknown 5。
不能因为模块默认 Disabled 就认为它不依赖共享字段。

## 第一组 / 第二组 / 第三组候选模块

| 模块 | 分类 | 默认启用 | SDK getter | 依赖映射 | 状态 | 阻塞原因 / 恢复条件 |
|---|---|---|---|---|---|---|
| Watermark | Visual | true | - | - | **READY** | 静态证据充分 |
| Arraylist | Visual | true | - | - | **READY** | 静态证据充分 |
| Interface | Visual | true | getLocalPlayer | ClientInstance_getLocalPlayer,ResourcePackManager_composeFullStackBp | **BLOCKED** | 解除 ResourcePackManager_composeFullStackBp 并运行时验证 |
| Notifications | Visual | true | - | - | **READY** | 静态证据充分 |
| InventoryMove | Movement | false | getLocalPlayer | ClientInstance_getLocalPlayer,PlayerMovement_clearInputStateInlined,PlayerMovement_clearInputStateInlined2 | **BLOCKED** | 解除 PlayerMovement_clearInputStateInlined 并运行时验证 |
| Keystrokes | Visual | false | getLocalPlayer | ClientInstance_getLocalPlayer | **NEEDS_RUNTIME_TEST** | 行为验证 |
| Sprint | Movement | false | - | - | **READY** | 静态证据充分 |
| Zoom | Visual | false | getLocalPlayer,getMouseGrabbed | ClientInstance_getLocalPlayer | **NEEDS_RUNTIME_TEST** | 行为验证 |
| BlockESP | Visual | false | getBlockSource,getBuildDepth,getBuildHeight,getChunk,getLocalPlayer,getMouseGrabbed | BlockSource_getChunk,BlockSource_mBuildHeight,ClientInstance_getBlockSource,ClientInstance_getLocalPlayer | **BLOCKED** | 解除 BlockSource_getChunk,BlockSource_mBuildHeight 并运行时验证 |
| LevelInfo | Visual | false | getLocalPlayer,getMouseGrabbed | ClientInstance_getLocalPlayer | **NEEDS_RUNTIME_TEST** | 行为验证 |
| ESP | Visual | false | getLocalPlayer | ClientInstance_getLocalPlayer | **NEEDS_RUNTIME_TEST** | 行为验证 |
| Tracers | Visual | false | getLocalPlayer | ClientInstance_getLocalPlayer | **NEEDS_RUNTIME_TEST** | 行为验证 |

> 注意：`sigreport.json` 是**本轮修复前**的基线。`BlockESP` 列出的 `BlockSource_mBuildHeight` 已在本轮修复为 OK，但它仍被 `BlockSource_getChunk` 阻塞，因此仍是 BLOCKED。

## 全部 BLOCKED 模块

| 模块 | 分类 | 默认启用 | 阻塞映射 |
|---|---|---|---|
| Animations | Visual | false | FluxSwing, MinecraftSim_mGameSim |
| AntiBot | Misc | true | Level_getPlayerList |
| AutoSnipe | Misc | false | Level_getPlayerList |
| BlockESP | Visual | false | BlockSource_getChunk, BlockSource_mBuildHeight |
| ChestStealer | Player | false | Actor_mContainerManagerModel |
| DeviceSpoof | Misc | false | ConnectionRequest_create_DeviceModel |
| EditionFaker | Misc | false | ConnectionRequest_create_CurrentInputMode, ConnectionRequest_create_DefaultInputMode, InputModeBypass |
| Freecam | Player | false | MinecraftSim_mGameSim |
| Interface | Visual | true | ResourcePackManager_composeFullStackBp |
| InventoryMove | Movement | false | PlayerMovement_clearInputStateInlined |
| Jesus | Movement | false | BlockLegacy_getCollisionShape, WaterBlockLegacy_getCollisionShapeForCamera |
| NoFilter | Misc | true | MinecraftGame_mProfanityContext, UIProfanityContext_mEnabled |
| NoHurtcam | Visual | false | CameraComponent_applyRotation |
| NoRender | Visual | false | FireRender |
| NoSlowDown | Movement | false | tickEntity_ItemUseSlowdownModifierComponent |
| PartySpammer | Misc | false | Level_getPlayerList |
| Reach | Combat | false | Reach |
| Spammer | Misc | false | Level_getPlayerList |
| StaffAlert | Misc | false | Level_getPlayerList |

## 判定口径

- BLOCKED：存在直接 `OffsetProvider::` / `SigManager::` 或间接 SDK getter 所依赖的非 OK 映射。多数消费者已有空值守卫或替代路径，表现为功能降级而非崩溃。
- NEEDS_RUNTIME_TEST：依赖映射均已 OK（如 `ClientInstance_getLocalPlayer`、`ClientInstance_getBlockSource`），或只使用已验证的原始访问，但实际行为尚未运行验证。
- READY：无 SDK 映射依赖，仅依赖已具备运行时成功记录的 GL/ImGui 渲染链。
- 本轮不修改任何模块的启用状态。
