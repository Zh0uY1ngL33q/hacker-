# FEATURE_COMPATIBILITY — 功能与符号的依赖关系

> 表格由 `tools/re/symbol_usage.py` 从源码树扫描 `SigManager::X` / `OffsetProvider::X`
> 生成（`Dump/symbol_usage.txt`），不是凭印象归类。

## 1. 明确失效的功能（依赖“未解析”符号）

| 功能 | 模块路径 | 死掉的依赖 | 症状 |
|---|---|---|---|
| Animations（FluxSwing） | `Modules/Visual/Animations.cpp` | `sig FluxSwing`（0 命中） | 该开关无任何作用；日志每次 tick 刷 “Failed to patch ... at 0x0” |
| Reach | `Modules/Combat/Reach` | `sig Reach`（0 命中） | 无法改攻击距离 |
| Jesus | `Modules/Movement/Jesus` | `off BlockLegacy_getCollisionShape`（0 命中） | 水面行走失效 |
| EditionFaker | `Modules/Misc/EditionFaker` | `ConnectionRequest_create_CurrentInputMode` / `_DefaultInputMode` / `InputModeBypass`（全 0 命中） | 伪造版本信息失效 |
| DeviceSpoof | `Modules/Misc/DeviceSpoof` | `ConnectionRequest_create_DeviceModel`（0 命中） | 伪造设备型号失效 |
| InventoryMove | `Modules/Movement/InventoryMove` | `sig PlayerMovement_clearInputStateInlined`（0 命中） | 开背包时无法移动 |
| NoFilter | `Modules/Misc/NoFilter` | `off UIProfanityContext_mEnabled`（0 命中） | 聊天过滤关不掉 |
| NoHurtcam | `Modules/Visual/NoHurtcam` | `sig CameraComponent_applyRotation`（4 处命中，多义） | 受伤镜头抖动可能仍在 |
| NoRender | `Modules/Visual/NoRender` | `sig FireRender`（2 处命中，多义） | 火焰遮挡可能仍在 |
| Interface | `Modules/Visual/Interface` | `sig ResourcePackManager_composeFullStackBp`（4 处命中，多义） | 界面相关开关不确定 |
| NoSlowDown | `Modules/Movement/NoSlowDown` | `sig tickEntity_ItemUseSlowdownModifierComponent`（4 处命中，多义） | 用物品减速可能仍在 |

“多义”不等于失效，但等于**地址可能是错的**——这正是本项目以前的失效模式。

## 2. 渲染/动画钩子层（影响面最大）

| 依赖 | 状态 | 影响 |
|---|---|---|
| `sig ScreenView_setupAndRender` | 0 命中 | `SetupAndRenderHook` 无法建立，屏幕级渲染事件缺失 |
| `sig ItemRenderer_render` | 0 命中 | `ItemRendererHook` 无法建立 |
| `sig HoverTextRenderer_render` | 0 命中 | 悬浮文本钩子缺失（Tooltip 类功能） |
| `sig ActorAnimationControllerPlayer_applyToPose` | 0 命中 | `AnimationHooks` 的姿态注入点缺失 → **Animations 的骨骼级效果无法实现** |
| `sig Mob_getCurrentSwingDuration` | 0 命中 | 挥拳时长无法改（`SwingDurationEvent` 无来源） |

这些是**功能整体缺失**，不是“参数不对”。日志里 `Failed to find 43 signatures/offsets!` 之后
没有任何模块级报错，所以从界面上看就是“开关能点、没反应”。

## 3. SDK 级偏移缺口（会被所有模块共享）

| 符号 | 状态 | 后果 |
|---|---|---|
| `Level_getLevelData` | 0 命中 | `Level::getLevelData()` 直接 `return nullptr` → 所有依赖 `LevelData::mTick` 的计时/节流逻辑退化 |
| `Level_getHitResult` | 多义（槽号解出 `-151183361`） | `Level::getHitResult()` 必返回 null → 准星目标类功能（AutoTool/FastMine/DestroyProgress/TriggerBot…）全部空转 |
| `Level_getPlayerList` | 0 命中 | 玩家列表为空 → 相关 UI/命令拿不到名单 |
| `Actor_mHurtTimeComponent` | 0 命中 | 受伤组件恒 null → 受击动画/受伤色失效 |
| `LevelRendererPlayer_mFovX/_mFovY` | 0 命中 | FOV 读不到 → 依赖视场角的投影/ESP 缩放偏差 |
| `MinecraftSim_mGameSim` / `_mRenderSim` | 0 命中 | Sim 链断裂 |
| `ClientInstance_getInputHandler` | 0 命中 | 输入层不可达 |
| `ContainerManagerModel_getSlot` | 0 命中 | 容器槽位映射失效 |
| `BlockSource_clip` / `BlockLegacy_mBlockId` / `BlockSource_mBuildHeight` / `LevelChunk_mSubChunks` | 0 命中 | 世界/碰撞查询退化 |

## 4. 四个目标模块的真实依赖面

| 模块 | 直接依赖的符号 | 结论 |
|---|---|---|
| ViewModel | 无（只监听 `BobHurtEvent`） | 只要 `GameRenderer_bobHurt`（已解析，唯一命中）成立即可工作 |
| TargetHUD | 无直接符号；数据来自 `Actor`/`Level` SDK | 受 §3 的受伤组件、玩家列表影响 |
| HudEditor | 无直接符号；纯 ImGui 布局 | 只依赖渲染链是否存在 |
| Animations | `FluxSwing`（死）、`ItemInHandRenderer_render_bytepatch`（活）、`renderItem_bytepatch`（活）、`TapSwingAnim`（活但**指错常量**） | 见 `REVERSE_ENGINEERING_REPORT.md §3` |

## 5. “未使用”的死符号

按 `SigManager::X` / `OffsetProvider::X` 逐名扫描，**48 个符号在源码里没有任何引用**
（抽查 `ItemInHandRenderer_applyTransform`、`Player_getSupplies`、`PythonRuntime_Execute`、
`Actor_getLevel` 用 grep 复核，引用数均为 0）。其中：

* 未解析的 18 个：`Actor_getLevel`、`Actor_getStatusFlag`、`ClientInstance_getMouseGrabbed`、
  `ContainerManagerModel_getContainerType`、`LocalPlayer_applyTurnDelta`、
  `MoveInputComponent_ClearMoveState1`、`RenderController_getHurtColor`（failed 7）
  + `ClientInstance_mStackScene`、`ClientInstance_switchControlMode`、
  `ContainerManagerController_handleSwap/transferItems`、`ContainerScreenController_handlePlaceAll`、
  `EnchantUtils_getEnchantLevel`、`HudScreenController_shouldRenderScoreboardSidebar`、
  `ItemInHandRenderer_FluxSwingCallsite`、`ItemRenderer_renderGuiItemNew`、
  `Scoreboard_getDisplaySlotEntries`、`gDataUtil_MiscData_Instance`（ambiguous 11）
* 已解析但仍无用的 30 个（`PythonRuntime_*`、`HIDController_*`、`ItemStack_ctor_*` 等）。

删掉这 18 条未解析声明，未解析数会从 **74 降到 56**，日志噪声同比减少。
本轮**没有删**：删声明会改变 `mSigs` 表大小，而 `initialize()` 的失败判定和用户的
“43 项”对账习惯都依赖这张表，属于独立可验证的小任务。
