# Solstice Mapping Audit

> This is an offline static audit. **STATIC CANDIDATE is not RUNTIME VERIFIED** and does not prove semantic identity.

## Provenance

- Audited base commit: `bd532e1614283c241c530934606be5135c5f1058` (branch `codex/mapping-audit-121120`)
- The generated files live in the later review commit; use `git log -1` for that containing commit ID.
- Target image: `G:\hacker\Dump\Minecraft.Windows_1.21.120.0_20261027_005946.image`
- Size: `226144256 bytes`
- SHA-256: `11147749f1a4085b15b13cec4f45952d297acb275c0966954bdff4cbd2ff9b70`
- Generated (UTC): `2026-11-06T05:16:37.146287+00:00`

## Counts

- Mapping declaration records inventoried: **520**
- Unique logical `(kind, name)` mappings: **515** (conditional declarations can share both)
- Scanned mappings: **172**
- STATIC VERIFIED: **0**
- STATIC CANDIDATE: **92**
- RUNTIME VERIFIED: **0**
- UNVERIFIED: **428**

### Kinds

| Kind | Count |
|---|---:|
| CLASS_FIELD | 84 |
| DEFINE_FIELD | 18 |
| DEFINE_INDEX_FIELD | 50 |
| DEFINE_INDEX_FIELD_TYPED | 7 |
| DEFINE_SIG | 115 |
| DERIVED_LITERAL_OFFSET | 5 |
| DIRECT_LITERAL_OFFSET | 37 |
| DIRECT_OFFSET_TABLE | 2 |
| DIRECT_VTABLE_INDEX | 2 |
| LAYOUT_COMMENT_OFFSET | 199 |
| STATIC_ASSERT_OFFSET | 1 |

### Inventory scope

The inventory includes active DEFINE_SIG, DEFINE_INDEX_FIELD, DEFINE_INDEX_FIELD_TYPED, DEFINE_FIELD and CLASS_FIELD declarations; direct literal arguments to mapping APIs; derived `OffsetProvider + literal` expressions; literal offset tables; `offsetof` assertions; and active `this+offset` layout annotations (including explicit padding records). Commented-out declarations and `.bak` files are excluded.

### Scanner verdicts

| Status | Count |
|---|---:|
| AMBIGUOUS | 32 |
| IMPLAUSIBLE | 6 |
| INCONSISTENT | 0 |
| MISSING | 41 |
| OK | 92 |
| OUT_OF_RANGE | 1 |

## Verification levels

- **STATIC VERIFIED**: independent static semantic evidence; none were automatically promoted.
- **STATIC CANDIDATE**: exactly one structurally plausible image match; semantic identity remains unproved.
- **RUNTIME VERIFIED**: confirmed in the target process; this offline run assigns none.
- **UNVERIFIED**: rejected scan or hardcoded mapping without sufficient independent evidence.

## Scanner consistency

Core verdict differences: **0**.

## Reproducible scan runs

- tools/verify_sigs.py: 222.502 s, exit 1 because rejected mappings were found (report produced normally)
- tools/re/scan_sigs.py: 56.34 s, exit 0
- Both scanners evaluated 172 scanned mappings and agreed on all core verdict fields.

## Tests

- `tools/test_verify_sigs.py: 37/37 targeted tests passed (process 0.543 s; see checked-in test log)`
- `two scanner implementations compared for every scanned mapping`

Covered matrices: zero/one/many/overlapping hits; 64-KiB boundary; distinct and split executable sections; empty ranges; int8/int32/int64 signs and widths; truncated operands; unsupported typed offsets; vtable alignment/index zero; PE VirtualSize/RawSize/SizeOfImage/truncation/order/no-exec cases; both-scanner equivalence; C++ guarded-read source contracts.

## C++ runtime audit findings

- ResolveTable is the authoritative status source; initialization verdicts use status, not cached nonzero addresses.
- Missing, Ambiguous, Inconsistent, Implausible and OutOfRange remain distinct.
- Ambiguous mappings are refused before operand reads.
- Offset operands and rel32 operands now require exact module bounds, readable memory and guarded copies.
- Every non-Ok scanned mapping leaves its published SDK value at zero; Solstice stops initialization by default unless the explicit unverified-mappings override is compiled in.
- Signature/offset map mutations are serialized during parallel initialization.
- Decryption timeout stops SDK initialization; release rejects untrusted mappings unless explicitly overridden.
- CodeSection find/count share ranges and ordering; runtime readable-region splitting cannot be reproduced from a dump.

## Offline/runtime boundary

- Offline scanners inspect bytes present in executable PE ranges; runtime additionally observes committed/readable VirtualQuery regions.
- Runtime match counting caps at 2 because uniqueness is the verdict; offline reports preserve up to 64 hits. A runtime count of 2 therefore means at least 2, while the rejection status is equivalent.
- Unique matching only establishes a static candidate. It does not establish function or field semantics.
- No mapping in this report is labeled RUNTIME VERIFIED; that requires target-process evidence.
- Hardcoded DEFINE_FIELD, CLASS_FIELD, and direct literals are UNVERIFIED unless independently adjudicated.

## Unresolved issues

- STATIC CANDIDATE entries still require semantic disassembly or target-runtime confirmation.
- All literal CLASS_FIELD/direct offsets and vtable indices without independent evidence remain UNVERIFIED.
- Runtime VirtualQuery page state is absent from the dump, so offline/runtime hit counts can legitimately differ.
- The C++ changes require MSVC 14.51 compilation on the designated build machine.
- Only a target-game run can establish RUNTIME VERIFIED status and validate consumers.

## Modified files and reasons

- `tools/re/scan_sigs.py` — overlap-safe matching, exact typed reads, PE bounds/order, early ambiguity rejection and canonical reasons
- `tools/scan_sigs.py` — byte-identical in-tree mirror of the authoritative scanner
- `tools/verify_sigs.py` — final-verdict evaluator and matching/PE/operand correctness fixes
- `tools/test_verify_sigs.py` — 37-case boundary and C++ source-contract regression suite
- `tools/mapping_audit.py` — reproducible full mapping inventory and report generator
- `src/SDK/OffsetProvider.hpp/.cpp` — exact-width guarded operand reads and preserved rejection evidence
- `src/SDK/SigManager.hpp/.cpp` — single guarded rel32 decode and authoritative resolved-address publication
- `src/SDK/SigFuzz.cpp` — fix undefined report counters using authoritative status totals
- `docs/mapping-audit/*` — raw scanner outputs, timing and unit-test evidence
- `mapping-audit.json / mapping-audit-report.md` — machine-readable and human-readable final audit

## Build-machine validation

```powershell
# Run only after human review and an explicit push of the audit branch:
cd C:\work\hacker-git
git fetch origin codex/mapping-audit-121120
git checkout -B codex/mapping-audit-121120 origin/codex/mapping-audit-121120
cd solstice\solstice-src\solstice-master-main\solstice-master-main
cmake --build out/build/x64-Release
```

## Full inventory

The machine-readable inventory, including every consumer location and rejection reason, is in `mapping-audit.json`.

| Name | Owner | Kind | Value | Hits | Static status | Evidence level | Rejection reason |
|---|---|---|---:|---:|---|---|---|
| `GameSession_mEventCallback` | GameSession | DEFINE_INDEX_FIELD_TYPED |  | 6 | AMBIGUOUS | UNVERIFIED | pattern matched 6 executable locations |
| `BedrockPlatformUWP_mcGame` | BedrockPlatformUWP | DEFINE_INDEX_FIELD_TYPED | 64 | 1 | OK | STATIC CANDIDATE |  |
| `UIProfanityContext_mEnabled` | UIProfanityContext | DEFINE_INDEX_FIELD_TYPED |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `BlockSource_mBuildHeight` | BlockSource | DEFINE_INDEX_FIELD_TYPED |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `ContainerManagerModel_getContainerType` | ContainerManagerModel | DEFINE_INDEX_FIELD_TYPED |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `ContainerManagerModel_getSlot` | ContainerManagerModel | DEFINE_INDEX_FIELD_TYPED |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `BlockLegacy_getCollisionShape` | BlockLegacy | DEFINE_INDEX_FIELD_TYPED |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `ClientInstance_getLocalPlayer` | ClientInstance | DEFINE_INDEX_FIELD | 35 | 1 | OK | STATIC CANDIDATE |  |
| `ClientInstance_getBlockSource` | ClientInstance | DEFINE_INDEX_FIELD |  | 4 | AMBIGUOUS | UNVERIFIED | pattern matched 4 executable locations |
| `ClientInstance_getOptions` | ClientInstance | DEFINE_INDEX_FIELD | 186 | 1 | OK | STATIC CANDIDATE |  |
| `ClientInstance_getMouseGrabbed` | ClientInstance | DEFINE_INDEX_FIELD |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `ClientInstance_setDisableInput` | ClientInstance | DEFINE_INDEX_FIELD |  | 64 | AMBIGUOUS | UNVERIFIED | pattern matched at least 64 executable locations |
| `ClientInstance_grabMouse` | ClientInstance | DEFINE_INDEX_FIELD |  | 8 | AMBIGUOUS | UNVERIFIED | pattern matched 8 executable locations |
| `ClientInstance_releaseMouse` | ClientInstance | DEFINE_INDEX_FIELD |  | 64 | AMBIGUOUS | UNVERIFIED | pattern matched at least 64 executable locations |
| `ClientInstance_mMinecraftSim` | ClientInstance | DEFINE_INDEX_FIELD | 224 | 1 | OK | STATIC CANDIDATE |  |
| `MinecraftGame_playUi` | MinecraftGame | DEFINE_INDEX_FIELD | 3238145 | 1 | IMPLAUSIBLE | UNVERIFIED | implausible vtable index 3238145 (runtime accepts 0 <= v < 4096) |
| `MinecraftGame_mClientInstances` | MinecraftGame | DEFINE_INDEX_FIELD | 2912 | 1 | OK | STATIC CANDIDATE |  |
| `MinecraftGame_mProfanityContext` | MinecraftGame | DEFINE_INDEX_FIELD | 1 | 1 | IMPLAUSIBLE | UNVERIFIED | field offset 1 lies inside the MSVC vtable pointer |
| `MinecraftGame_mMouseGrabbed` | MinecraftGame | DEFINE_INDEX_FIELD | -964358145 | 1 | IMPLAUSIBLE | UNVERIFIED | implausible field offset -964358145 (runtime accepts 0 <= v < 32768) |
| `MainView_bedrockPlatform` | MainView | DEFINE_INDEX_FIELD |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `MinecraftSim_mGameSim` | MinecraftSim | DEFINE_INDEX_FIELD |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `MinecraftSim_mRenderSim` | MinecraftSim | DEFINE_INDEX_FIELD |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `MinecraftSim_mGameSession` | MinecraftSim | DEFINE_INDEX_FIELD | 216 | 1 | OK | STATIC CANDIDATE |  |
| `bgfx_d3d12_RendererContextD3D12_m_commandQueue` | bgfx | DEFINE_INDEX_FIELD |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `bgfx_context_m_renderCtx` | bgfx | DEFINE_INDEX_FIELD |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `BlockLegacy_mBlockId` | BlockLegacy | DEFINE_INDEX_FIELD |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `BlockLegacy_mayPlaceOn` | BlockLegacy | DEFINE_INDEX_FIELD | 84 | 1 | OK | STATIC CANDIDATE |  |
| `Actor_mContainerManagerModel` | Actor | DEFINE_INDEX_FIELD |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `Actor_mGameMode` | Actor | DEFINE_INDEX_FIELD | 2808 | 1 | OK | STATIC CANDIDATE |  |
| `Actor_mSupplies` | Actor | DEFINE_INDEX_FIELD | 3328 | 1 | OK | STATIC CANDIDATE |  |
| `PlayerInventory_mContainer` | PlayerInventory | DEFINE_INDEX_FIELD | 288 | 1 | OK | STATIC CANDIDATE |  |
| `LevelRenderer_mRendererPlayer` | LevelRenderer | DEFINE_INDEX_FIELD | 1448 | 1 | OK | STATIC CANDIDATE |  |
| `LevelRendererPlayer_mCameraPos` | LevelRendererPlayer | DEFINE_INDEX_FIELD | 3780 | 1 | OK | STATIC CANDIDATE |  |
| `LevelRendererPlayer_mFovX` | LevelRendererPlayer | DEFINE_INDEX_FIELD |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `LevelRendererPlayer_mFovY` | LevelRendererPlayer | DEFINE_INDEX_FIELD |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `Actor_mSwinging` | Actor | DEFINE_INDEX_FIELD | 2212 | 1 | OK | STATIC CANDIDATE |  |
| `Actor_swing` | Actor | DEFINE_INDEX_FIELD | 17305961 | 1 | IMPLAUSIBLE | UNVERIFIED | implausible vtable index 17305961 (runtime accepts 0 <= v < 4096) |
| `Actor_mLevel` | Actor | DEFINE_INDEX_FIELD | 144 | 1 | OK | STATIC CANDIDATE |  |
| `Level_getHitResult` | Level | DEFINE_INDEX_FIELD |  | 64 | AMBIGUOUS | UNVERIFIED | pattern matched at least 64 executable locations |
| `Level_getPlayerList` | Level | DEFINE_INDEX_FIELD |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `Level_getPlayerMovementSettings` | Level | DEFINE_INDEX_FIELD | 72 | 1 | OK | STATIC CANDIDATE |  |
| `Level_getLevelData` | Level | DEFINE_INDEX_FIELD |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `Level_getBlockPalette` | Level | DEFINE_INDEX_FIELD | 436 | 1 | OK | STATIC CANDIDATE |  |
| `LevelData_mTick` | LevelData | DEFINE_INDEX_FIELD |  | 2 | AMBIGUOUS | UNVERIFIED | pattern matched 2 executable locations |
| `Actor_mDestroying` | Actor | DEFINE_INDEX_FIELD |  | 8 | AMBIGUOUS | UNVERIFIED | pattern matched 8 executable locations |
| `Actor_mHurtTimeComponent` | Actor | DEFINE_INDEX_FIELD |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `BlockSource_getChunk` | BlockSource | DEFINE_INDEX_FIELD |  | 1 | IMPLAUSIBLE | UNVERIFIED | vtable displacement 64123367 is not 8-byte aligned |
| `BlockSource_setBlock` | BlockSource | DEFINE_INDEX_FIELD |  | 2 | AMBIGUOUS | UNVERIFIED | pattern matched 2 executable locations |
| `LevelChunk_mSubChunks` | LevelChunk | DEFINE_INDEX_FIELD |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `Actor_mSerializedSkin` | Actor | DEFINE_INDEX_FIELD | 5608 | 1 | OK | STATIC CANDIDATE |  |
| `BlockSource_clip` | BlockSource | DEFINE_INDEX_FIELD |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `ClientInstance_getInputHandler` | ClientInstance | DEFINE_INDEX_FIELD |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `ClientInputMappingFactory_mKeyboardMouseSettings` | ClientInputMappingFactory | DEFINE_INDEX_FIELD | 138447688 | 1 | IMPLAUSIBLE | UNVERIFIED | implausible field offset 138447688 (runtime accepts 0 <= v < 32768) |
| `Actor_mContainerModel` | Actor | DEFINE_INDEX_FIELD | 3304 | 1 | OK | STATIC CANDIDATE |  |
| `ClientInstance_mStackScene` | ClientInstance | DEFINE_INDEX_FIELD |  | 8 | AMBIGUOUS | UNVERIFIED | pattern matched 8 executable locations |
| `Minecraft_mRealTimer` | Minecraft | DEFINE_INDEX_FIELD | 248 | 1 | OK | STATIC CANDIDATE |  |
| `gDataUtil_MiscData_Instance` | gDataUtil | DEFINE_INDEX_FIELD |  | 31 | AMBIGUOUS | UNVERIFIED | pattern matched 31 executable locations |
| `Actor_setPosition` | Actor | DEFINE_SIG |  | 2 | AMBIGUOUS | UNVERIFIED | pattern matched 2 executable locations |
| `Actor_getNameTag` | Actor | DEFINE_SIG | 0x4AE984E9 | 1 | OUT_OF_RANGE | UNVERIFIED | resolved address lies outside image |
| `Actor_setNameTag` | Actor | DEFINE_SIG | 0x584CF80 | 1 | OK | STATIC CANDIDATE |  |
| `ActorRenderDispatcher_render` | ActorRenderDispatcher | DEFINE_SIG | 0x2827560 | 1 | OK | STATIC CANDIDATE |  |
| `ClientInstance_mBgfx` | ClientInstance | DEFINE_SIG | 0x665A211 | 1 | OK | STATIC CANDIDATE |  |
| `ClientInstance_grabMouse` | ClientInstance | DEFINE_SIG |  | 8 | AMBIGUOUS | UNVERIFIED | pattern matched 8 executable locations |
| `ClientInstance_releaseMouse` | ClientInstance | DEFINE_SIG |  | 2 | AMBIGUOUS | UNVERIFIED | pattern matched 2 executable locations |
| `ContainerScreenController_tick` | ContainerScreenController | DEFINE_SIG | 0x1E26600 | 1 | OK | STATIC CANDIDATE |  |
| `ContainerScreenController_handleAutoPlace` | ContainerScreenController | DEFINE_SIG | 0x6C077A0 | 1 | OK | STATIC CANDIDATE |  |
| `ComplexInventoryTransaction_vtable` | ComplexInventoryTransaction | DEFINE_SIG | 0x8D46BC8 | 1 | OK | STATIC CANDIDATE |  |
| `EnchantUtils_getEnchantLevel` | EnchantUtils | DEFINE_SIG |  | 2 | AMBIGUOUS | UNVERIFIED | pattern matched 2 executable locations |
| `GameMode_getDestroyRate` | GameMode | DEFINE_SIG | 0x5BF54B0 | 1 | OK | STATIC CANDIDATE |  |
| `HoverTextRenderer_render` | HoverTextRenderer | DEFINE_SIG |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `GameMode_baseUseItem` | GameMode | DEFINE_SIG | 0x5BEC700 | 1 | OK | STATIC CANDIDATE |  |
| `GuiData_displayClientMessage` | GuiData | DEFINE_SIG |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `InventoryTransaction_addAction` | InventoryTransaction | DEFINE_SIG | 0x5F67370 | 1 | OK | STATIC CANDIDATE |  |
| `ItemStack_vTable` | ItemStack | DEFINE_SIG |  | 64 | AMBIGUOUS | UNVERIFIED | pattern matched at least 64 executable locations |
| `ItemStack_getCustomName` | ItemStack | DEFINE_SIG | 0x5D0AED0 | 1 | OK | STATIC CANDIDATE |  |
| `ItemStack_fromDescriptor` | ItemStack | DEFINE_SIG |  | 6 | AMBIGUOUS | UNVERIFIED | pattern matched 6 executable locations |
| `ItemUseInventoryTransaction_vtable` | ItemUseInventoryTransaction | DEFINE_SIG | 0x8D46C00 | 1 | OK | STATIC CANDIDATE |  |
| `ItemUseOnActorInventoryTransaction_vtable` | ItemUseOnActorInventoryTransaction | DEFINE_SIG | 0x91F9320 | 1 | OK | STATIC CANDIDATE |  |
| `ItemReleaseInventoryTransaction_vtable` | ItemReleaseInventoryTransaction | DEFINE_SIG | 0x91E0230 | 1 | OK | STATIC CANDIDATE |  |
| `Keyboard_feed` | Keyboard | DEFINE_SIG | 0x308B1E0 | 1 | OK | STATIC CANDIDATE |  |
| `MainView_instance` | MainView | DEFINE_SIG | 0xA88F2D0 | 1 | OK | STATIC CANDIDATE |  |
| `MinecraftPackets_createPacket` | MinecraftPackets | DEFINE_SIG | 0x13054C0 | 1 | OK | STATIC CANDIDATE |  |
| `Mob_getJumpControlComponent` | Mob | DEFINE_SIG | 0xE6BB60 | 1 | OK | STATIC CANDIDATE |  |
| `Mob_getCurrentSwingDuration` | Mob | DEFINE_SIG |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `MouseDevice_feed` | MouseDevice | DEFINE_SIG | 0x346EEB0 | 1 | OK | STATIC CANDIDATE |  |
| `NetworkStackItemDescriptor_ctor` | NetworkStackItemDescriptor | DEFINE_SIG |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `PlayerMovement_clearInputStateInlined` | PlayerMovement | DEFINE_SIG |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `PlayerMovement_clearInputStateInlined2` | PlayerMovement | DEFINE_SIG | 0xBFC1C9 | 1 | OK | STATIC CANDIDATE |  |
| `RakNet_RakPeer_runUpdateCycle` | RakNet | DEFINE_SIG | 0x21319A0 | 1 | OK | STATIC CANDIDATE |  |
| `RakNet_RakPeer_sendImmediate` | RakNet | DEFINE_SIG | 0x6D327B0 | 1 | OK | STATIC CANDIDATE |  |
| `ScreenView_setupAndRender` | ScreenView | DEFINE_SIG |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `SimulatedPlayer_simulateJump` | SimulatedPlayer | DEFINE_SIG | 0x1A15F90 | 1 | OK | STATIC CANDIDATE |  |
| `ItemInHandRenderer_render_bytepatch` | ItemInHandRenderer | DEFINE_SIG | 0x58EB4EC | 1 | OK | STATIC CANDIDATE |  |
| `SneakMovementSystem_tickSneakMovementSystem` | SneakMovementSystem | DEFINE_SIG | 0xD9E37A | 1 | OK | STATIC CANDIDATE |  |
| `ConnectionRequest_create` | ConnectionRequest | DEFINE_SIG | 0x91EAD0 | 1 | OK | STATIC CANDIDATE |  |
| `CameraDirectLookSystemUtil_handleLookInput` | CameraDirectLookSystemUtil | DEFINE_SIG | 0x1103EDA | 1 | OK | STATIC CANDIDATE |  |
| `ItemRenderer_render` | ItemRenderer | DEFINE_SIG |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `ItemPositionConst` | (global) | DEFINE_SIG | 0x7CFF273 | 1 | OK | STATIC CANDIDATE |  |
| `BlockSource_fireBlockChanged` | BlockSource | DEFINE_SIG |  | 2 | AMBIGUOUS | UNVERIFIED | pattern matched 2 executable locations |
| `ActorAnimationControllerPlayer_applyToPose` | ActorAnimationControllerPlayer | DEFINE_SIG |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `JSON_parse` | JSON | DEFINE_SIG |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `Actor_getStatusFlag` | Actor | DEFINE_SIG |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `Level_getRuntimeActorList` | Level | DEFINE_SIG | 0x1D99FF0 | 1 | OK | STATIC CANDIDATE |  |
| `ConcreteBlockLegacy_getCollisionShapeForCamera` | ConcreteBlockLegacy | DEFINE_SIG | 0x863DA0 | 1 | OK | STATIC CANDIDATE |  |
| `WaterBlockLegacy_getCollisionShapeForCamera` | WaterBlockLegacy | DEFINE_SIG |  | 5 | AMBIGUOUS | UNVERIFIED | pattern matched 5 executable locations |
| `mce_framebuilder_RenderItemInHandDescription_ctor` | mce | DEFINE_SIG | 0x2FCE9A | 1 | OK | STATIC CANDIDATE |  |
| `ResourcePackManager_composeFullStackBp` | ResourcePackManager | DEFINE_SIG |  | 4 | AMBIGUOUS | UNVERIFIED | pattern matched 4 executable locations |
| `ClientInstance_isPreGame` | ClientInstance | DEFINE_SIG | 0x1A3CFC0 | 1 | OK | STATIC CANDIDATE |  |
| `tickEntity_ItemUseSlowdownModifierComponent` | tickEntity | DEFINE_SIG |  | 4 | AMBIGUOUS | UNVERIFIED | pattern matched 4 executable locations |
| `checkBlocks` | (global) | DEFINE_SIG | 0x1006335 | 1 | OK | STATIC CANDIDATE |  |
| `Unknown_renderBlockOverlay` | Unknown | DEFINE_SIG | 0x5FA990 | 1 | OK | STATIC CANDIDATE |  |
| `FastEat` | (global) | DEFINE_SIG | 0x6A01A4B | 1 | OK | STATIC CANDIDATE |  |
| `Unknown_renderNametag` | Unknown | DEFINE_SIG | 0x27FADB0 | 1 | OK | STATIC CANDIDATE |  |
| `Reach` | (global) | DEFINE_SIG |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `BlockReach` | (global) | DEFINE_SIG | 0x7B08ED0 | 1 | OK | STATIC CANDIDATE |  |
| `GetSpeedInAirWithSprint` | (global) | DEFINE_SIG | 0x7F42373 | 1 | OK | STATIC CANDIDATE |  |
| `ConnectionRequest_create_DeviceModel` | ConnectionRequest | DEFINE_SIG |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `ConnectionRequest_create_DeviceOS` | ConnectionRequest | DEFINE_SIG | 0x804C309 | 1 | OK | STATIC CANDIDATE |  |
| `ConnectionRequest_create_DefaultInputMode` | ConnectionRequest | DEFINE_SIG |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `ConnectionRequest_create_CurrentInputMode` | ConnectionRequest | DEFINE_SIG |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `InputModeBypass` | (global) | DEFINE_SIG |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `InputModeBypassFix` | (global) | DEFINE_SIG | 0x5BC6D2E | 1 | OK | STATIC CANDIDATE |  |
| `TapSwingAnim` | (global) | DEFINE_SIG | 0x8F5A000 | 1 | OK | STATIC CANDIDATE |  |
| `Unknown_updatePlayerFromCamera` | Unknown | DEFINE_SIG | 0x7AC45F4 | 1 | OK | STATIC CANDIDATE |  |
| `FluxSwing` | (global) | DEFINE_SIG |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `BobHurt` | (global) | DEFINE_SIG | 0xCE0260 | 1 | OK | STATIC CANDIDATE |  |
| `CameraComponent_applyRotation` | CameraComponent | DEFINE_SIG |  | 3 | AMBIGUOUS | UNVERIFIED | pattern matched 3 executable locations |
| `FireRender` | (global) | DEFINE_SIG |  | 2 | AMBIGUOUS | UNVERIFIED | pattern matched 2 executable locations |
| `Actor_canSee` | Actor | DEFINE_SIG |  | 11 | AMBIGUOUS | UNVERIFIED | pattern matched 11 executable locations |
| `ItemInHandRenderer_renderItem_bytepatch` | ItemInHandRenderer | DEFINE_SIG | 0x58FB1B0 | 1 | OK | STATIC CANDIDATE |  |
| `ItemInHandRenderer_renderItem_bytepatch2` | ItemInHandRenderer | DEFINE_SIG | 0x1CC86E5 | 1 | OK | STATIC CANDIDATE |  |
| `Actor_getGameMode` | Actor | DEFINE_SIG | 0x2E0210 | 1 | OK | STATIC CANDIDATE |  |
| `Actor_getLevel` | Actor | DEFINE_SIG |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `BaseActorRenderContext_ctor` | BaseActorRenderContext | DEFINE_SIG | 0x29283E0 | 1 | OK | STATIC CANDIDATE |  |
| `ClientInstance_getCamera` | ClientInstance | DEFINE_SIG | 0x289D03B | 1 | OK | STATIC CANDIDATE |  |
| `ClientInstance_getItemRenderer` | ClientInstance | DEFINE_SIG | 0x2928485 | 1 | OK | STATIC CANDIDATE |  |
| `ClientInstance_switchControlMode` | ClientInstance | DEFINE_SIG |  | 19 | AMBIGUOUS | UNVERIFIED | pattern matched 19 executable locations |
| `ComplexInventoryTransaction_fromType` | ComplexInventoryTransaction | DEFINE_SIG | 0x5F6B980 | 1 | OK | STATIC CANDIDATE |  |
| `ContainerManagerController_handleSwap` | ContainerManagerController | DEFINE_SIG |  | 35 | AMBIGUOUS | UNVERIFIED | pattern matched 35 executable locations |
| `ContainerManagerController_transferItems` | ContainerManagerController | DEFINE_SIG |  | 2 | AMBIGUOUS | UNVERIFIED | pattern matched 2 executable locations |
| `ContainerScreenController_handleDropItem` | ContainerScreenController | DEFINE_SIG | 0x5B8F040 | 1 | OK | STATIC CANDIDATE |  |
| `ContainerScreenController_handlePlaceAll` | ContainerScreenController | DEFINE_SIG |  | 5 | AMBIGUOUS | UNVERIFIED | pattern matched 5 executable locations |
| `ContainerScreenController_handleTakeAll` | ContainerScreenController | DEFINE_SIG | 0x1DD0CB6 | 1 | OK | STATIC CANDIDATE |  |
| `Dimension_getBrightnessDependentFogColor` | Dimension | DEFINE_SIG | 0x6428F00 | 1 | OK | STATIC CANDIDATE |  |
| `GameRenderer_bobHurt` | GameRenderer | DEFINE_SIG | 0x2D72340 | 1 | OK | STATIC CANDIDATE |  |
| `GuiData_getOptions` | GuiData | DEFINE_SIG | 0x7DCD190 | 1 | OK | STATIC CANDIDATE |  |
| `HIDController_onKeyDown` | HIDController | DEFINE_SIG | 0x34724E0 | 1 | OK | STATIC CANDIDATE |  |
| `HIDController_onKeyUp` | HIDController | DEFINE_SIG | 0x34725B0 | 1 | OK | STATIC CANDIDATE |  |
| `HudScreenController_shouldRenderScoreboardSidebar` | HudScreenController | DEFINE_SIG |  | 2 | AMBIGUOUS | UNVERIFIED | pattern matched 2 executable locations |
| `ItemInHandRenderer_FluxSwingCallsite` | ItemInHandRenderer | DEFINE_SIG |  | 3 | AMBIGUOUS | UNVERIFIED | pattern matched 3 executable locations |
| `ItemInHandRenderer_TapSwingAngleRadians` | ItemInHandRenderer | DEFINE_SIG | 0x2D4CAE0 | 1 | OK | STATIC CANDIDATE |  |
| `ItemInHandRenderer_applyTransform` | ItemInHandRenderer | DEFINE_SIG | 0x2DA7BC4 | 1 | OK | STATIC CANDIDATE |  |
| `ItemRenderer_renderGuiItemNew` | ItemRenderer | DEFINE_SIG |  | 3 | AMBIGUOUS | UNVERIFIED | pattern matched 3 executable locations |
| `ItemStack_ctor_ItemStack` | ItemStack | DEFINE_SIG | 0x5CE76C0 | 1 | OK | STATIC CANDIDATE |  |
| `ItemStack_ctor_void` | ItemStack | DEFINE_SIG | 0x5CE78A0 | 1 | OK | STATIC CANDIDATE |  |
| `LevelRendererCamera_updateCameraTransform` | LevelRendererCamera | DEFINE_SIG | 0xB0C93C | 1 | OK | STATIC CANDIDATE |  |
| `LevelRendererPlayer_renderNametag` | LevelRendererPlayer | DEFINE_SIG | 0x317350 | 1 | OK | STATIC CANDIDATE |  |
| `LocalPlayer_applyTurnDelta` | LocalPlayer | DEFINE_SIG |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `MainView_getInstance` | MainView | DEFINE_SIG | 0x1A90510 | 1 | OK | STATIC CANDIDATE |  |
| `MobEquipmentPacket_ctor_ActorRuntimeID` | MobEquipmentPacket | DEFINE_SIG | 0x1C6E210 | 1 | OK | STATIC CANDIDATE |  |
| `Mob_getInterpolatedSwingProgress` | Mob | DEFINE_SIG | 0x58FB1B0 | 1 | OK | STATIC CANDIDATE |  |
| `MoveInputComponent_ClearMoveState1` | MoveInputComponent | DEFINE_SIG |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `NoItemRotation_PatchSig` | NoItemRotation | DEFINE_SIG | 0xE9A763 | 1 | OK | STATIC CANDIDATE |  |
| `OverworldDimension_getBrightnessDependentFogColor` | OverworldDimension | DEFINE_SIG | 0x8C41C0 | 1 | OK | STATIC CANDIDATE |  |
| `Player_getSupplies` | Player | DEFINE_SIG | 0x34CC5D0 | 1 | OK | STATIC CANDIDATE |  |
| `PythonRuntime_DeleteThread` | PythonRuntime | DEFINE_SIG | 0x7E72230 | 1 | OK | STATIC CANDIDATE |  |
| `PythonRuntime_Execute` | PythonRuntime | DEFINE_SIG | 0x7EB7F80 | 1 | OK | STATIC CANDIDATE |  |
| `PythonRuntime_NewThread` | PythonRuntime | DEFINE_SIG | 0x7E72180 | 1 | OK | STATIC CANDIDATE |  |
| `PythonRuntime_SetReleaseCheck` | PythonRuntime | DEFINE_SIG | 0x7EE6C30 | 1 | OK | STATIC CANDIDATE |  |
| `RenderController_getHurtColor` | RenderController | DEFINE_SIG |  | 0 | MISSING | UNVERIFIED | pattern did not match any executable range |
| `ScoreboardScreenController_getScoreboardSidebarTitle` | ScoreboardScreenController | DEFINE_SIG | 0x7CD6F00 | 1 | OK | STATIC CANDIDATE |  |
| `Scoreboard_getDisplaySlotEntries` | Scoreboard | DEFINE_SIG |  | 4 | AMBIGUOUS | UNVERIFIED | pattern matched 4 executable locations |
| `ClientInstance_mLevelRenderer` | ClientInstance | DEFINE_FIELD | 240 |  | UNVERIFIED | UNVERIFIED | constant is not scanned; semantic evidence requires manual review |
| `ClientInstance_mPacketSender` | ClientInstance | DEFINE_FIELD | 264 |  | UNVERIFIED | UNVERIFIED | constant is not scanned; semantic evidence requires manual review |
| `Actor_baseTick` | Actor | DEFINE_FIELD | 29 |  | UNVERIFIED | UNVERIFIED | constant is not scanned; semantic evidence requires manual review |
| `Container_getItem` | Container | DEFINE_FIELD | 8 |  | UNVERIFIED | UNVERIFIED | constant is not scanned; semantic evidence requires manual review |
| `ClientInputHandler_mMappingFactory` | ClientInputHandler | DEFINE_FIELD | 40 |  | UNVERIFIED | UNVERIFIED | constant is not scanned; semantic evidence requires manual review |
| `ContainerScreenController_tryExit` | ContainerScreenController | DEFINE_FIELD | 12 |  | UNVERIFIED | UNVERIFIED | constant is not scanned; semantic evidence requires manual review |
| `ItemStack_reInit` | ItemStack | DEFINE_FIELD | 3 |  | UNVERIFIED | UNVERIFIED | constant is not scanned; semantic evidence requires manual review |
| `MinecraftUIRenderContext_drawImage` | MinecraftUIRenderContext | DEFINE_FIELD | 7 |  | UNVERIFIED | UNVERIFIED | constant is not scanned; semantic evidence requires manual review |
| `Actor_mEntityIdentifier` | Actor | DEFINE_FIELD | 576 |  | UNVERIFIED | UNVERIFIED | constant is not scanned; semantic evidence requires manual review |
| `Actor_mLocalName` | Actor | DEFINE_FIELD | 3248 |  | UNVERIFIED | UNVERIFIED | constant is not scanned; semantic evidence requires manual review |
| `ClientInstance_mViewMatrix` | ClientInstance | DEFINE_FIELD | 856 |  | UNVERIFIED | UNVERIFIED | constant is not scanned; semantic evidence requires manual review |
| `ClientInstance_mFovX` | ClientInstance | DEFINE_FIELD | 1824 |  | UNVERIFIED | UNVERIFIED | constant is not scanned; semantic evidence requires manual review |
| `ClientInstance_mFovY` | ClientInstance | DEFINE_FIELD | 1828 |  | UNVERIFIED | UNVERIFIED | constant is not scanned; semantic evidence requires manual review |
| `Block_mRuntimeId` | Block | DEFINE_FIELD | 192 |  | UNVERIFIED | UNVERIFIED | constant is not scanned; semantic evidence requires manual review |
| `Block_mLegacy` | Block | DEFINE_FIELD | 48 |  | UNVERIFIED | UNVERIFIED | constant is not scanned; semantic evidence requires manual review |
| `ClientInstance_getScreenName` | ClientInstance | DEFINE_FIELD | 266 |  | UNVERIFIED | UNVERIFIED | constant is not scanned; semantic evidence requires manual review |
| `ClientInstance_mGuiData` | ClientInstance | DEFINE_FIELD | 1728 |  | UNVERIFIED | UNVERIFIED | constant is not scanned; semantic evidence requires manual review |
| `BlockPalette_mLevel` | BlockPalette | DEFINE_FIELD | 128 |  | UNVERIFIED | UNVERIFIED | constant is not scanned; semantic evidence requires manual review |
| `src/Features/Modules/Movement/AirSpeed.cpp:11:pointer-add#1` | AirSpeed | DIRECT_LITERAL_OFFSET | 4 |  | UNVERIFIED | UNVERIFIED | hardcoded literal has no scanner or semantic proof |
| `src/Features/Modules/Movement/AirSpeed.cpp:12:pointer-add#1` | AirSpeed | DIRECT_LITERAL_OFFSET | 17 |  | UNVERIFIED | UNVERIFIED | hardcoded literal has no scanner or semantic proof |
| `src/Features/Modules/Visual/Animations.cpp:159:pointer-add#1` | Animations | DIRECT_LITERAL_OFFSET | 9 |  | UNVERIFIED | UNVERIFIED | hardcoded literal has no scanner or semantic proof |
| `src/Features/Modules/Visual/Animations.cpp:187:pointer-add#1` | Animations | DIRECT_LITERAL_OFFSET | 208 |  | UNVERIFIED | UNVERIFIED | hardcoded literal has no scanner or semantic proof |
| `src/Features/Modules/Visual/Animations.cpp:188:pointer-add#1` | Animations | DIRECT_LITERAL_OFFSET | 16 |  | UNVERIFIED | UNVERIFIED | hardcoded literal has no scanner or semantic proof |
| `src/Features/Modules/Visual/Animations.cpp:189:pointer-add#1` | Animations | DIRECT_LITERAL_OFFSET | 24 |  | UNVERIFIED | UNVERIFIED | hardcoded literal has no scanner or semantic proof |
| `src/Features/Modules/Visual/Animations.cpp:607:pointer-add#1` | Animations | DIRECT_LITERAL_OFFSET | 2176 |  | UNVERIFIED | UNVERIFIED | hardcoded literal has no scanner or semantic proof |
| `src/Features/Modules/Visual/Animations.cpp:608:pointer-add#1` | Animations | DIRECT_LITERAL_OFFSET | 2172 |  | UNVERIFIED | UNVERIFIED | hardcoded literal has no scanner or semantic proof |
| `src/Hook/Hooks/ActorHooks/ActorModelHook.cpp:22:pointer-add#1` | ActorModelHook | DIRECT_LITERAL_OFFSET | 56 |  | UNVERIFIED | UNVERIFIED | hardcoded literal has no scanner or semantic proof |
| `HoverTextRendererHook::mFilteredContent` | HoverTextRendererHook | LAYOUT_COMMENT_OFFSET | 48 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `HoverTextRendererHook::mCursorPosition` | HoverTextRendererHook | LAYOUT_COMMENT_OFFSET | 80 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `HoverTextRendererHook::mOffset` | HoverTextRendererHook | LAYOUT_COMMENT_OFFSET | 88 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `HoverTextRendererHook::mBoxDimensions` | HoverTextRendererHook | LAYOUT_COMMENT_OFFSET | 96 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `RenderItemInHandHook::mGlintColor` | RenderItemInHandHook | CLASS_FIELD | 140 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `RenderItemInHandHook::mGlintAlpha` | RenderItemInHandHook | CLASS_FIELD | 156 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `RenderItemInHandHook::mOverlayColor` | RenderItemInHandHook | CLASS_FIELD | 172 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `RenderItemInHandHook::mOverlayAlpha` | RenderItemInHandHook | CLASS_FIELD | 188 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `RenderItemInHandHook::mChangeColor` | RenderItemInHandHook | CLASS_FIELD | 204 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `RenderItemInHandHook::mChangeAlpha` | RenderItemInHandHook | CLASS_FIELD | 220 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `RenderItemInHandHook::mMultiplicativeTintColor` | RenderItemInHandHook | CLASS_FIELD | 236 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `RenderItemInHandHook::mMultiplicativeTintColorAlpha` | RenderItemInHandHook | CLASS_FIELD | 252 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `src/SDK/Minecraft/Actor/Actor.cpp:160:derived#1` | Actor | DERIVED_LITERAL_OFFSET | Actor_mSwinging + 0x4 |  | UNVERIFIED | UNVERIFIED | derived literal adjustment has no independent semantic proof |
| `src/SDK/Minecraft/Actor/Actor.cpp:165:derived#1` | Actor | DERIVED_LITERAL_OFFSET | Actor_mSwinging + 0x8 |  | UNVERIFIED | UNVERIFIED | derived literal adjustment has no independent semantic proof |
| `src/SDK/Minecraft/Actor/Actor.cpp:170:derived#1` | Actor | DERIVED_LITERAL_OFFSET | Actor_mSwinging + 0x4 |  | UNVERIFIED | UNVERIFIED | derived literal adjustment has no independent semantic proof |
| `Actor::vtable` | Actor | CLASS_FIELD | 0 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `Actor::mContext` | Actor | CLASS_FIELD | 8 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `Actor::mEntityIdentifier` | Actor | CLASS_FIELD | OffsetProvider::Actor_mEntityIdentifier |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `ActorPartModel::mBoneStr` | ActorPartModel | CLASS_FIELD | 16 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `src/SDK/Minecraft/Actor/ActorPartModel.hpp:27:pointer-add#1` | ActorPartModel | DIRECT_LITERAL_OFFSET | 236 |  | UNVERIFIED | UNVERIFIED | hardcoded literal has no scanner or semantic proof |
| `CameraComponent::mRadius` | CameraComponent | CLASS_FIELD | 40 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `CameraComponent::mRotRads` | CameraComponent | CLASS_FIELD | 44 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `CameraComponent::mRotRads` | CameraComponent | CLASS_FIELD | 48 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `CameraComponent::mOrigin` | CameraComponent | CLASS_FIELD | 64 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `CameraComponent::mFov` | CameraComponent | CLASS_FIELD | 76 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `CameraComponent::mNearClippingPlane` | CameraComponent | CLASS_FIELD | 84 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `CameraComponent::mFarClippingPlane` | CameraComponent | CLASS_FIELD | 88 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `JumpControlComponent::mNoJumpDelay` | JumpControlComponent | CLASS_FIELD | 16 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `MoveInputComponent::mIsMoveLocked` | MoveInputComponent | CLASS_FIELD | 138 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `MoveInputComponent::mIsSneakDown` | MoveInputComponent | CLASS_FIELD | 32 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `MoveInputComponent::mIsJumping` | MoveInputComponent | CLASS_FIELD | 38 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `MoveInputComponent::mIsJumping2` | MoveInputComponent | CLASS_FIELD | 128 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `MoveInputComponent::mIsSprinting` | MoveInputComponent | CLASS_FIELD | 39 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `MoveInputComponent::mForward` | MoveInputComponent | CLASS_FIELD | 44 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `MoveInputComponent::mBackward` | MoveInputComponent | CLASS_FIELD | 45 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `MoveInputComponent::mLeft` | MoveInputComponent | CLASS_FIELD | 46 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `MoveInputComponent::mRight` | MoveInputComponent | CLASS_FIELD | 47 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `MoveInputComponent::mMoveVector` | MoveInputComponent | CLASS_FIELD | 72 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `src/SDK/Minecraft/Actor/Components/MoveInputComponent.hpp:27:pointer-add#1` | MoveInputComponent | DIRECT_LITERAL_OFFSET | 38 |  | UNVERIFIED | UNVERIFIED | hardcoded literal has no scanner or semantic proof |
| `src/SDK/Minecraft/Actor/Components/MoveInputComponent.hpp:28:pointer-add#1` | MoveInputComponent | DIRECT_LITERAL_OFFSET | 128 |  | UNVERIFIED | UNVERIFIED | hardcoded literal has no scanner or semantic proof |
| `RawMoveInputComponent::mIsMoveLocked` | RawMoveInputComponent | CLASS_FIELD | 130 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `RawMoveInputComponent::mIsSneakDown` | RawMoveInputComponent | CLASS_FIELD | 32 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `RawMoveInputComponent::mIsJumping` | RawMoveInputComponent | CLASS_FIELD | 38 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `RawMoveInputComponent::mIsJumping2` | RawMoveInputComponent | CLASS_FIELD | 128 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `RawMoveInputComponent::mIsSprinting` | RawMoveInputComponent | CLASS_FIELD | 39 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `RawMoveInputComponent::mForward` | RawMoveInputComponent | CLASS_FIELD | 44 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `RawMoveInputComponent::mBackward` | RawMoveInputComponent | CLASS_FIELD | 45 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `RawMoveInputComponent::mLeft` | RawMoveInputComponent | CLASS_FIELD | 46 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `RawMoveInputComponent::mRight` | RawMoveInputComponent | CLASS_FIELD | 47 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `RawMoveInputComponent::mMoveVector` | RawMoveInputComponent | CLASS_FIELD | 72 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `src/SDK/Minecraft/Actor/Components/MoveInputComponent.hpp:65:pointer-add#1` | RawMoveInputComponent | DIRECT_LITERAL_OFFSET | 38 |  | UNVERIFIED | UNVERIFIED | hardcoded literal has no scanner or semantic proof |
| `src/SDK/Minecraft/Actor/Components/MoveInputComponent.hpp:66:pointer-add#1` | RawMoveInputComponent | DIRECT_LITERAL_OFFSET | 128 |  | UNVERIFIED | UNVERIFIED | hardcoded literal has no scanner or semantic proof |
| `src/SDK/Minecraft/Actor/EntityContext.hpp:134:pointer-add#1` | EntityContext | DIRECT_LITERAL_OFFSET | 1 |  | UNVERIFIED | UNVERIFIED | hardcoded literal has no scanner or semantic proof |
| `src/SDK/Minecraft/Actor/EntityContext.hpp:187:pointer-add#1` | EntityContext | DIRECT_LITERAL_OFFSET | 1 |  | UNVERIFIED | UNVERIFIED | hardcoded literal has no scanner or semantic proof |
| `GameMode::mPlayer` | GameMode | CLASS_FIELD | 8 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `GameMode::mBreakProgress` | GameMode | CLASS_FIELD | 36 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `SerializedSkin::skinWidth` | SerializedSkin | CLASS_FIELD | 164 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `SerializedSkin::skinHeight` | SerializedSkin | CLASS_FIELD | 168 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `SerializedSkin::skinData` | SerializedSkin | CLASS_FIELD | 192 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `SerializedSkin::mId` | SerializedSkin | LAYOUT_COMMENT_OFFSET | 0 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `SerializedSkin::mPlayFabId` | SerializedSkin | LAYOUT_COMMENT_OFFSET | 32 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `SerializedSkin::mFullId` | SerializedSkin | LAYOUT_COMMENT_OFFSET | 64 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `SerializedSkin::mResourcePatch` | SerializedSkin | LAYOUT_COMMENT_OFFSET | 96 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `SerializedSkin::mDefaultGeometryName` | SerializedSkin | LAYOUT_COMMENT_OFFSET | 128 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `SerializedSkin::mSkinImage` | SerializedSkin | LAYOUT_COMMENT_OFFSET | 160 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `SerializedSkin::mCapeImage` | SerializedSkin | LAYOUT_COMMENT_OFFSET | 192 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `SerializedSkin::mSkinAnimatedImages` | SerializedSkin | LAYOUT_COMMENT_OFFSET | 224 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `SerializedSkin::mGeometryData` | SerializedSkin | LAYOUT_COMMENT_OFFSET | 256 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `SerializedSkin::mGeometryDataEngineVersion` | SerializedSkin | LAYOUT_COMMENT_OFFSET | 288 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `SerializedSkin::mGeometryDataMutable` | SerializedSkin | LAYOUT_COMMENT_OFFSET | 296 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `SerializedSkin::mAnimationData` | SerializedSkin | LAYOUT_COMMENT_OFFSET | 328 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `SerializedSkin::mCapeId` | SerializedSkin | LAYOUT_COMMENT_OFFSET | 360 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `SerializedSkin::mPersonaPieces` | SerializedSkin | LAYOUT_COMMENT_OFFSET | 392 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `SerializedSkin::mArmSizeType` | SerializedSkin | LAYOUT_COMMENT_OFFSET | 424 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `SerializedSkin::mPieceTintColors` | SerializedSkin | LAYOUT_COMMENT_OFFSET | 432 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `SerializedSkin::mSkinColor` | SerializedSkin | LAYOUT_COMMENT_OFFSET | 464 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `SerializedSkin::mIsTrustedSkin` | SerializedSkin | LAYOUT_COMMENT_OFFSET | 472 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `SerializedSkin::mIsPremium` | SerializedSkin | LAYOUT_COMMENT_OFFSET | 476 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `SerializedSkin::mIsPersona` | SerializedSkin | LAYOUT_COMMENT_OFFSET | 477 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `SerializedSkin::mIsPersonaCapeOnClassicSkin` | SerializedSkin | LAYOUT_COMMENT_OFFSET | 478 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `SerializedSkin::mIsPrimaryUser` | SerializedSkin | LAYOUT_COMMENT_OFFSET | 479 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `SerializedSkin::mOverridesPlayerAppearance` | SerializedSkin | LAYOUT_COMMENT_OFFSET | 480 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `src/SDK/Minecraft/InstanceLocator.cpp:123:pointer-add#1` | InstanceLocator | DIRECT_LITERAL_OFFSET | 12 |  | UNVERIFIED | UNVERIFIED | hardcoded literal has no scanner or semantic proof |
| `src/SDK/Minecraft/InstanceLocator.cpp:187:pointer-add#1` | InstanceLocator | DIRECT_LITERAL_OFFSET | 24 |  | UNVERIFIED | UNVERIFIED | hardcoded literal has no scanner or semantic proof |
| `src/SDK/Minecraft/InstanceLocator.cpp:189:pointer-add#1` | InstanceLocator | DIRECT_LITERAL_OFFSET | 25 |  | UNVERIFIED | UNVERIFIED | hardcoded literal has no scanner or semantic proof |
| `src/SDK/Minecraft/InstanceLocator.cpp:192:pointer-add#1` | InstanceLocator | DIRECT_LITERAL_OFFSET | 8 |  | UNVERIFIED | UNVERIFIED | hardcoded literal has no scanner or semantic proof |
| `src/SDK/Minecraft/InstanceLocator.cpp:197:pointer-add#1` | InstanceLocator | DIRECT_LITERAL_OFFSET | 8 |  | UNVERIFIED | UNVERIFIED | hardcoded literal has no scanner or semantic proof |
| `src/SDK/Minecraft/InstanceLocator.cpp:200:pointer-add#1` | InstanceLocator | DIRECT_LITERAL_OFFSET | 8 |  | UNVERIFIED | UNVERIFIED | hardcoded literal has no scanner or semantic proof |
| `src/SDK/Minecraft/InstanceLocator.cpp:238:valueOffsets#1` | InstanceLocator | DIRECT_OFFSET_TABLE | 40 |  | UNVERIFIED | UNVERIFIED | literal layout table has no independent semantic proof |
| `src/SDK/Minecraft/InstanceLocator.cpp:254:pointer-add#1` | InstanceLocator | DIRECT_LITERAL_OFFSET | 8 |  | UNVERIFIED | UNVERIFIED | hardcoded literal has no scanner or semantic proof |
| `src/SDK/Minecraft/InstanceLocator.cpp:565:pointer-add#1` | InstanceLocator | DIRECT_LITERAL_OFFSET | 25 |  | UNVERIFIED | UNVERIFIED | hardcoded literal has no scanner or semantic proof |
| `src/SDK/Minecraft/InstanceLocator.cpp:567:pointer-add#1` | InstanceLocator | DIRECT_LITERAL_OFFSET | 8 |  | UNVERIFIED | UNVERIFIED | hardcoded literal has no scanner or semantic proof |
| `src/SDK/Minecraft/InstanceLocator.cpp:571:pointer-add#1` | InstanceLocator | DIRECT_LITERAL_OFFSET | 8 |  | UNVERIFIED | UNVERIFIED | hardcoded literal has no scanner or semantic proof |
| `src/SDK/Minecraft/InstanceLocator.cpp:574:pointer-add#1` | InstanceLocator | DIRECT_LITERAL_OFFSET | 8 |  | UNVERIFIED | UNVERIFIED | hardcoded literal has no scanner or semantic proof |
| `src/SDK/Minecraft/InstanceLocator.cpp:580:pointer-add#1` | InstanceLocator | DIRECT_LITERAL_OFFSET | 25 |  | UNVERIFIED | UNVERIFIED | hardcoded literal has no scanner or semantic proof |
| `src/SDK/Minecraft/InstanceLocator.cpp:585:valueOffsets#1` | InstanceLocator | DIRECT_OFFSET_TABLE | 40 |  | UNVERIFIED | UNVERIFIED | literal layout table has no independent semantic proof |
| `src/SDK/Minecraft/InstanceLocator.cpp:592:pointer-add#1` | InstanceLocator | DIRECT_LITERAL_OFFSET | 8 |  | UNVERIFIED | UNVERIFIED | hardcoded literal has no scanner or semantic proof |
| `ContainerManagerModel::mContainerType` | ContainerManagerModel | CLASS_FIELD | 81 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `Item::mItemId` | Item | CLASS_FIELD | 162 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `Item::mArmorItemType` | Item | CLASS_FIELD | 580 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `Item::mProtection` | Item | CLASS_FIELD | 612 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `Item::mName` | Item | CLASS_FIELD | 208 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `ItemEntry::mItem` | ItemEntry | LAYOUT_COMMENT_OFFSET | 0 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `ItemEntry::mAuxValue` | ItemEntry | LAYOUT_COMMENT_OFFSET | 8 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `BaseDescriptor::mItem` | BaseDescriptor | LAYOUT_COMMENT_OFFSET | 0 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `NetworkItemStackDescriptor::mStackSize` | NetworkItemStackDescriptor | LAYOUT_COMMENT_OFFSET | 16 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `PlayerInventory::mSelectedSlot` | PlayerInventory | CLASS_FIELD | 16 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `PlayerInventory::mInHandSlot` | PlayerInventory | CLASS_FIELD | 28 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `src/SDK/Minecraft/Inventory/SimpleContainer.cpp:18:derived#1` | SimpleContainer | DERIVED_LITERAL_OFFSET | Container_getItem + 5 |  | UNVERIFIED | UNVERIFIED | derived literal adjustment has no independent semantic proof |
| `Value::mInt` | Value | LAYOUT_COMMENT_OFFSET | 0 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `Value::mUInt64` | Value | LAYOUT_COMMENT_OFFSET | 0 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `Value::mReal` | Value | LAYOUT_COMMENT_OFFSET | 0 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `Value::mBool` | Value | LAYOUT_COMMENT_OFFSET | 0 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `Value::mString` | Value | LAYOUT_COMMENT_OFFSET | 0 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `Value::mMap` | Value | LAYOUT_COMMENT_OFFSET | 0 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `Value::mValue` | Value | LAYOUT_COMMENT_OFFSET | 0 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `Value::mType` | Value | LAYOUT_COMMENT_OFFSET | 40 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `Value::mAllocated` | Value | LAYOUT_COMMENT_OFFSET | 44 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `Blob::mBlob` | Blob | LAYOUT_COMMENT_OFFSET | 0 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `Blob::mSize` | Blob | LAYOUT_COMMENT_OFFSET | 16 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `Image::imageFormat` | Image | LAYOUT_COMMENT_OFFSET | 0 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `Image::mWidth` | Image | LAYOUT_COMMENT_OFFSET | 4 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `Image::mHeight` | Image | LAYOUT_COMMENT_OFFSET | 8 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `Image::mDepth` | Image | LAYOUT_COMMENT_OFFSET | 12 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `Image::mUsage` | Image | LAYOUT_COMMENT_OFFSET | 16 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `Image::mImageBytes` | Image | LAYOUT_COMMENT_OFFSET | 24 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `src/SDK/Minecraft/MinecraftSim.cpp:95:pointer-add#1` | MinecraftSim | DIRECT_LITERAL_OFFSET | 8 |  | UNVERIFIED | UNVERIFIED | hardcoded literal has no scanner or semantic proof |
| `LoopbackPacketSender::vtable` | LoopbackPacketSender | CLASS_FIELD | 0 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `LoopbackPacketSender::mNetworkSystem` | LoopbackPacketSender | CLASS_FIELD | 32 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `src/SDK/Minecraft/Network/LoopbackPacketSender.hpp:35:callVirtualFunc#1` | LoopbackPacketSender | DIRECT_VTABLE_INDEX | 2 |  | UNVERIFIED | UNVERIFIED | hardcoded literal has no scanner or semantic proof |
| `ActorLink::mType` | ActorLink | LAYOUT_COMMENT_OFFSET | 0 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `ActorLink::mA` | ActorLink | LAYOUT_COMMENT_OFFSET | 8 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `ActorLink::mB` | ActorLink | LAYOUT_COMMENT_OFFSET | 16 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `ActorLink::mImmediate` | ActorLink | LAYOUT_COMMENT_OFFSET | 24 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `ActorLink::mPassengerInitiated` | ActorLink | LAYOUT_COMMENT_OFFSET | 25 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `CommandRequestPacket::mCommand` | CommandRequestPacket | LAYOUT_COMMENT_OFFSET | 48 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `CommandRequestPacket::mOrigin` | CommandRequestPacket | LAYOUT_COMMENT_OFFSET | 80 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `CommandRequestPacket::mInternalSource` | CommandRequestPacket | LAYOUT_COMMENT_OFFSET | 144 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `InventoryContentPacket::mSlots` | InventoryContentPacket | STATIC_ASSERT_OFFSET | 160 |  | UNVERIFIED | UNVERIFIED | local struct-layout assertion does not prove target-game semantic layout |
| `ContainerClosePacket::mContainerId` | ContainerClosePacket | LAYOUT_COMMENT_OFFSET | 48 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `ContainerClosePacket::mServerInitiatedClose` | ContainerClosePacket | LAYOUT_COMMENT_OFFSET | 49 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `ContainerClosePacket::mContainerId` | ContainerClosePacket | LAYOUT_COMMENT_OFFSET | 48 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `ContainerClosePacket::mType` | ContainerClosePacket | LAYOUT_COMMENT_OFFSET | 49 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `ContainerClosePacket::mPos` | ContainerClosePacket | LAYOUT_COMMENT_OFFSET | 52 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `ContainerClosePacket::mEntityUniqueID` | ContainerClosePacket | LAYOUT_COMMENT_OFFSET | 64 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `InteractPacket::mAction` | InteractPacket | LAYOUT_COMMENT_OFFSET | 48 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `InteractPacket::mTargetId` | InteractPacket | LAYOUT_COMMENT_OFFSET | 56 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `InteractPacket::mPos` | InteractPacket | LAYOUT_COMMENT_OFFSET | 64 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `InventorySource::InvalidInventory` | InventorySource | LAYOUT_COMMENT_OFFSET | 0 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `InventorySource::field_at_0x4` | InventorySource | LAYOUT_COMMENT_OFFSET | 4 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `InventorySource::NoFlag` | InventorySource | LAYOUT_COMMENT_OFFSET | 8 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `InventoryTransactionPacket::mSource` | InventoryTransactionPacket | LAYOUT_COMMENT_OFFSET | 0 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `InventoryTransactionPacket::Slot` | InventoryTransactionPacket | LAYOUT_COMMENT_OFFSET | 12 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `InventoryTransactionPacket::mFromItemDescriptor` | InventoryTransactionPacket | LAYOUT_COMMENT_OFFSET | 16 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `InventoryTransactionPacket::mToItemDescriptor` | InventoryTransactionPacket | LAYOUT_COMMENT_OFFSET | 112 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `InventoryTransactionPacket::mFromItem` | InventoryTransactionPacket | LAYOUT_COMMENT_OFFSET | 208 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `InventoryTransactionPacket::mToItem` | InventoryTransactionPacket | LAYOUT_COMMENT_OFFSET | 368 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `InventoryTransactionItemGroup::mItemId` | InventoryTransactionItemGroup | LAYOUT_COMMENT_OFFSET | 0 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `InventoryTransactionItemGroup::mItemAux` | InventoryTransactionItemGroup | LAYOUT_COMMENT_OFFSET | 4 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `InventoryTransactionItemGroup::mTag` | InventoryTransactionItemGroup | LAYOUT_COMMENT_OFFSET | 8 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `InventoryTransactionItemGroup::mCount` | InventoryTransactionItemGroup | LAYOUT_COMMENT_OFFSET | 16 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `InventoryTransactionItemGroup::mOverflow` | InventoryTransactionItemGroup | LAYOUT_COMMENT_OFFSET | 20 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `ComplexInventoryTransaction::vtable` | ComplexInventoryTransaction | LAYOUT_COMMENT_OFFSET | 0 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `InventoryTransactionPacket::pad` | InventoryTransactionPacket | LAYOUT_COMMENT_OFFSET | 48 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `InventoryTransactionPacket::mLegacySetItemSlots` | InventoryTransactionPacket | LAYOUT_COMMENT_OFFSET | 64 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `InventoryTransactionPacket::mTransaction` | InventoryTransactionPacket | LAYOUT_COMMENT_OFFSET | 88 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `InventoryTransactionPacket::false` | InventoryTransactionPacket | LAYOUT_COMMENT_OFFSET | 96 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `LevelEventPacket::mEventId` | LevelEventPacket | LAYOUT_COMMENT_OFFSET | 48 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `LevelEventPacket::mPos` | LevelEventPacket | LAYOUT_COMMENT_OFFSET | 52 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `LevelEventPacket::mData` | LevelEventPacket | LAYOUT_COMMENT_OFFSET | 64 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `LoginPacket::mClientNetworkVersion` | LoginPacket | LAYOUT_COMMENT_OFFSET | 48 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `LoginPacket::mConnectionRequest` | LoginPacket | LAYOUT_COMMENT_OFFSET | 56 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `MobEffectPacket::mRuntimeId` | MobEffectPacket | LAYOUT_COMMENT_OFFSET | 48 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `MobEffectPacket::mEffectDurationTicks` | MobEffectPacket | LAYOUT_COMMENT_OFFSET | 56 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `MobEffectPacket::mEventId` | MobEffectPacket | LAYOUT_COMMENT_OFFSET | 60 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `MobEffectPacket::mEffectId` | MobEffectPacket | LAYOUT_COMMENT_OFFSET | 64 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `MobEffectPacket::mEffectAmplifier` | MobEffectPacket | LAYOUT_COMMENT_OFFSET | 68 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `MobEffectPacket::mShowParticles` | MobEffectPacket | LAYOUT_COMMENT_OFFSET | 72 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `MobEffectPacket::mTick` | MobEffectPacket | LAYOUT_COMMENT_OFFSET | 80 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `MobEquipmentPacket::mRuntimeId` | MobEquipmentPacket | LAYOUT_COMMENT_OFFSET | 48 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `MobEquipmentPacket::mItem` | MobEquipmentPacket | LAYOUT_COMMENT_OFFSET | 56 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `MobEquipmentPacket::mSlot` | MobEquipmentPacket | LAYOUT_COMMENT_OFFSET | 152 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `MobEquipmentPacket::mSelectedSlot` | MobEquipmentPacket | LAYOUT_COMMENT_OFFSET | 156 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `MobEquipmentPacket::mContainerId` | MobEquipmentPacket | LAYOUT_COMMENT_OFFSET | 160 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `MobEquipmentPacket::mSlotByte` | MobEquipmentPacket | LAYOUT_COMMENT_OFFSET | 161 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `MobEquipmentPacket::mSelectedSlotByte` | MobEquipmentPacket | LAYOUT_COMMENT_OFFSET | 162 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `MobEquipmentPacket::mContainerIdByte` | MobEquipmentPacket | LAYOUT_COMMENT_OFFSET | 163 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `ModalFormResponsePacket::mFormId` | ModalFormResponsePacket | LAYOUT_COMMENT_OFFSET | 48 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `ModalFormResponsePacket::mJSONResponse` | ModalFormResponsePacket | LAYOUT_COMMENT_OFFSET | 56 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `ModalFormResponsePacket::mFormCancelReason` | ModalFormResponsePacket | LAYOUT_COMMENT_OFFSET | 80 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `ModalFormResponsePacket::mFormId` | ModalFormResponsePacket | LAYOUT_COMMENT_OFFSET | 48 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `ModalFormResponsePacket::mJSON` | ModalFormResponsePacket | LAYOUT_COMMENT_OFFSET | 56 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `MovePlayerPacket::mPlayerID` | MovePlayerPacket | LAYOUT_COMMENT_OFFSET | 48 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `MovePlayerPacket::mPos` | MovePlayerPacket | LAYOUT_COMMENT_OFFSET | 56 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `MovePlayerPacket::mRot` | MovePlayerPacket | LAYOUT_COMMENT_OFFSET | 68 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `MovePlayerPacket::mYHeadRot` | MovePlayerPacket | LAYOUT_COMMENT_OFFSET | 76 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `MovePlayerPacket::mResetPosition` | MovePlayerPacket | LAYOUT_COMMENT_OFFSET | 80 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `MovePlayerPacket::mOnGround` | MovePlayerPacket | LAYOUT_COMMENT_OFFSET | 81 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `MovePlayerPacket::mRidingID` | MovePlayerPacket | LAYOUT_COMMENT_OFFSET | 88 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `MovePlayerPacket::mCause` | MovePlayerPacket | LAYOUT_COMMENT_OFFSET | 96 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `MovePlayerPacket::mSourceEntityType` | MovePlayerPacket | LAYOUT_COMMENT_OFFSET | 100 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `MovePlayerPacket::mTick` | MovePlayerPacket | LAYOUT_COMMENT_OFFSET | 104 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `MovePlayerPacket::mRaw` | MovePlayerPacket | LAYOUT_COMMENT_OFFSET | 0 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `MovePlayerPacket::mRaw` | MovePlayerPacket | LAYOUT_COMMENT_OFFSET | 0 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `MovePlayerPacket::mMoveData` | MovePlayerPacket | LAYOUT_COMMENT_OFFSET | 48 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `MovePlayerPacket::mMoveData` | MovePlayerPacket | LAYOUT_COMMENT_OFFSET | 48 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `NetworkStackLatencyPacket::mCreateTime` | NetworkStackLatencyPacket | LAYOUT_COMMENT_OFFSET | 48 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `NetworkStackLatencyPacket::mFromServer` | NetworkStackLatencyPacket | LAYOUT_COMMENT_OFFSET | 56 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `PacketHandlerDispatcher::vtable` | PacketHandlerDispatcher | CLASS_FIELD | 0 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `src/SDK/Minecraft/Network/Packets/Packet.hpp:42:GetVTableFunction#1` | PacketHandlerDispatcher | DIRECT_VTABLE_INDEX | 1 |  | UNVERIFIED | UNVERIFIED | hardcoded literal has no scanner or semantic proof |
| `PlayerActionPacket::mPos` | PlayerActionPacket | LAYOUT_COMMENT_OFFSET | 48 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `PlayerActionPacket::mResultPos` | PlayerActionPacket | LAYOUT_COMMENT_OFFSET | 60 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `PlayerActionPacket::mFace` | PlayerActionPacket | LAYOUT_COMMENT_OFFSET | 72 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `PlayerActionPacket::mAction` | PlayerActionPacket | LAYOUT_COMMENT_OFFSET | 76 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `PlayerActionPacket::mRuntimeId` | PlayerActionPacket | LAYOUT_COMMENT_OFFSET | 80 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `PlayerActionPacket::mtIsFromServerPlayerMovementSystem` | PlayerActionPacket | LAYOUT_COMMENT_OFFSET | 88 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `PlayerSkinPacket::mUUID` | PlayerSkinPacket | LAYOUT_COMMENT_OFFSET | 48 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `PlayerSkinPacket::mSkin` | PlayerSkinPacket | LAYOUT_COMMENT_OFFSET | 64 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `PlayerSkinPacket::mLocalizedNewSkinName` | PlayerSkinPacket | LAYOUT_COMMENT_OFFSET | 680 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `PlayerSkinPacket::mLocalizedOldSkinName` | PlayerSkinPacket | LAYOUT_COMMENT_OFFSET | 712 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `PlaySoundPacket::mName` | PlaySoundPacket | LAYOUT_COMMENT_OFFSET | 48 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `PlaySoundPacket::mPos` | PlaySoundPacket | LAYOUT_COMMENT_OFFSET | 80 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `PlaySoundPacket::mVolume` | PlaySoundPacket | LAYOUT_COMMENT_OFFSET | 92 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `PlaySoundPacket::mPitch` | PlaySoundPacket | LAYOUT_COMMENT_OFFSET | 96 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `SetPlayerGameTypePacket::mPlayerGameType` | SetPlayerGameTypePacket | LAYOUT_COMMENT_OFFSET | 48 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `TextPacket::mType` | TextPacket | LAYOUT_COMMENT_OFFSET | 48 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `TextPacket::mAuthor` | TextPacket | LAYOUT_COMMENT_OFFSET | 56 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `TextPacket::mMessage` | TextPacket | LAYOUT_COMMENT_OFFSET | 88 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `TextPacket::mFilteredMessage` | TextPacket | LAYOUT_COMMENT_OFFSET | 120 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `TextPacket::mUnknown` | TextPacket | LAYOUT_COMMENT_OFFSET | 152 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `TextPacket::mParams` | TextPacket | LAYOUT_COMMENT_OFFSET | 160 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `TextPacket::mLocalize` | TextPacket | LAYOUT_COMMENT_OFFSET | 136 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `TextPacket::mXuid` | TextPacket | LAYOUT_COMMENT_OFFSET | 192 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `TextPacket::mPlatformId` | TextPacket | LAYOUT_COMMENT_OFFSET | 224 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `UpdateBlockPacket::mPos` | UpdateBlockPacket | LAYOUT_COMMENT_OFFSET | 48 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `UpdateBlockPacket::mLayer` | UpdateBlockPacket | LAYOUT_COMMENT_OFFSET | 60 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `UpdateBlockPacket::mUpdateFlags` | UpdateBlockPacket | LAYOUT_COMMENT_OFFSET | 64 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `UpdateBlockPacket::mBlockRuntimeId` | UpdateBlockPacket | LAYOUT_COMMENT_OFFSET | 68 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `UpdateBlockSyncedPacket::mEntityBlockSyncMessage` | UpdateBlockSyncedPacket | LAYOUT_COMMENT_OFFSET | 72 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `Options::mThirdPerson` | Options | CLASS_FIELD | 48 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `Options::mViewBob` | Options | CLASS_FIELD | 288 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `Options::mGfxFieldOfView` | Options | CLASS_FIELD | 416 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `Options::mGfxGamma` | Options | CLASS_FIELD | 440 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `GuiData::mResolution` | GuiData | CLASS_FIELD | 48 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `GuiData::mResolutionRounded` | GuiData | CLASS_FIELD | 56 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `GuiData::mResolutionScaled` | GuiData | CLASS_FIELD | 64 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `GuiData::mGuiScale` | GuiData | CLASS_FIELD | 76 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `GuiData::mScalingMultiplier` | GuiData | CLASS_FIELD | 80 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `Block::mLegacy` | Block | CLASS_FIELD | OffsetProvider::Block_mLegacy |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `BlockLegacy::mType` | BlockLegacy | CLASS_FIELD | 0 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `BlockLegacy::mIsFlammable` | BlockLegacy | CLASS_FIELD | 4 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `BlockLegacy::mIsNeverBuildable` | BlockLegacy | CLASS_FIELD | 5 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `BlockLegacy::mIsLiquid` | BlockLegacy | CLASS_FIELD | 6 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `BlockLegacy::mIsBlockingMotion` | BlockLegacy | CLASS_FIELD | 12 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `BlockLegacy::mIsBlockingPrecipitation` | BlockLegacy | CLASS_FIELD | 13 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `BlockLegacy::mIsSolid` | BlockLegacy | CLASS_FIELD | 14 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `BlockLegacy::mIsSuperHot` | BlockLegacy | CLASS_FIELD | 15 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `BlockLegacy::mVfTable` | BlockLegacy | CLASS_FIELD | 0 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `BlockLegacy::mTileName` | BlockLegacy | CLASS_FIELD | 40 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `BlockLegacy::mName` | BlockLegacy | CLASS_FIELD | 160 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `BlockLegacy::mMaterial` | BlockLegacy | CLASS_FIELD | 296 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `BlockLegacy::mSolid` | BlockLegacy | CLASS_FIELD | 356 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `src/SDK/Minecraft/World/BlockSource.cpp:50:pointer-add#1` | BlockSource | DIRECT_LITERAL_OFFSET | 3 |  | UNVERIFIED | UNVERIFIED | hardcoded literal has no scanner or semantic proof |
| `BlockSource::mVfTable` | BlockSource | CLASS_FIELD | 0 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `BlockSource::mChunkSource` | BlockSource | CLASS_FIELD | 40 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `BlockSource::mDimension` | BlockSource | CLASS_FIELD | 48 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `src/SDK/Minecraft/World/BlockSource.hpp:26:derived#1` | BlockSource | DERIVED_LITERAL_OFFSET | BlockSource_mBuildHeight + 2 |  | UNVERIFIED | UNVERIFIED | derived literal adjustment has no independent semantic proof |
| `ChunkSource::level` | ChunkSource | LAYOUT_COMMENT_OFFSET | 32 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `ChunkSource::dimension` | ChunkSource | LAYOUT_COMMENT_OFFSET | 40 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `ChunkSource::chunkStorage` | ChunkSource | LAYOUT_COMMENT_OFFSET | 112 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `LevelChunk::pad_0000` | LevelChunk | LAYOUT_COMMENT_OFFSET | 0 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `LevelChunk::chunkPosX` | LevelChunk | LAYOUT_COMMENT_OFFSET | 120 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `LevelChunk::chunkPosY` | LevelChunk | LAYOUT_COMMENT_OFFSET | 124 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `LevelChunk::pad_0080` | LevelChunk | LAYOUT_COMMENT_OFFSET | 128 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `LevelChunk::loadingStage` | LevelChunk | LAYOUT_COMMENT_OFFSET | 216 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `LevelChunk::pad_00D9` | LevelChunk | LAYOUT_COMMENT_OFFSET | 217 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `LevelChunk::mLastTick` | LevelChunk | LAYOUT_COMMENT_OFFSET | 288 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `LevelChunk::pad_0124` | LevelChunk | LAYOUT_COMMENT_OFFSET | 292 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `LevelChunk::subChunks` | LevelChunk | LAYOUT_COMMENT_OFFSET | 312 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `LevelChunk::pad_0150` | LevelChunk | LAYOUT_COMMENT_OFFSET | 336 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `LevelChunk::loadCompleted` | LevelChunk | LAYOUT_COMMENT_OFFSET | 3556 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `LevelChunk::pad_0DE8` | LevelChunk | LAYOUT_COMMENT_OFFSET | 3560 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `LevelChunk::isLoading` | LevelChunk | LAYOUT_COMMENT_OFFSET | 4539 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `LevelChunk::pad_11BC` | LevelChunk | LAYOUT_COMMENT_OFFSET | 4540 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `SubChunk::pad_0000` | SubChunk | LAYOUT_COMMENT_OFFSET | 0 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `SubChunk::needsInitLighting` | SubChunk | LAYOUT_COMMENT_OFFSET | 32 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `SubChunk::needsClientLighting` | SubChunk | LAYOUT_COMMENT_OFFSET | 33 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `SubChunk::pad_0022` | SubChunk | LAYOUT_COMMENT_OFFSET | 34 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `SubChunk::blocks` | SubChunk | LAYOUT_COMMENT_OFFSET | 40 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `SubChunk::pad_0030` | SubChunk | LAYOUT_COMMENT_OFFSET | 48 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `SubChunk::blockReadPtr` | SubChunk | LAYOUT_COMMENT_OFFSET | 56 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `SubChunk::pad_0040` | SubChunk | LAYOUT_COMMENT_OFFSET | 64 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `SubChunk::N000027E5` | SubChunk | LAYOUT_COMMENT_OFFSET | 88 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `SubChunk::subchunkIndex` | SubChunk | LAYOUT_COMMENT_OFFSET | 89 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `SubChunk::pad_005A` | SubChunk | LAYOUT_COMMENT_OFFSET | 90 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `SubChunkBlockStorage::pad_0008` | SubChunkBlockStorage | LAYOUT_COMMENT_OFFSET | 8 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `SubChunkBlockStorage::paletteData` | SubChunkBlockStorage | LAYOUT_COMMENT_OFFSET | 1648 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `SubChunkBlockStorage::pad_0678` | SubChunkBlockStorage | LAYOUT_COMMENT_OFFSET | 1656 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `SubChunkBlockStorage::paletteSize` | SubChunkBlockStorage | LAYOUT_COMMENT_OFFSET | 1712 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `SubChunkBlockStorage::pad_06B8` | SubChunkBlockStorage | LAYOUT_COMMENT_OFFSET | 1720 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `HitResult::mStartPos` | HitResult | LAYOUT_COMMENT_OFFSET | 0 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `HitResult::mRayDir` | HitResult | LAYOUT_COMMENT_OFFSET | 12 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `HitResult::mType` | HitResult | LAYOUT_COMMENT_OFFSET | 24 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `HitResult::mFacing` | HitResult | LAYOUT_COMMENT_OFFSET | 28 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `HitResult::mBlockPos` | HitResult | LAYOUT_COMMENT_OFFSET | 32 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `HitResult::mPos` | HitResult | LAYOUT_COMMENT_OFFSET | 44 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `HitResult::mEntity` | HitResult | LAYOUT_COMMENT_OFFSET | 56 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `HitResult::mIsHitLiquid` | HitResult | LAYOUT_COMMENT_OFFSET | 80 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `HitResult::mLiquidFacing` | HitResult | LAYOUT_COMMENT_OFFSET | 81 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `HitResult::mLiquid` | HitResult | LAYOUT_COMMENT_OFFSET | 84 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `HitResult::mLiquidPos` | HitResult | LAYOUT_COMMENT_OFFSET | 96 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `HitResult::mIndirectHit` | HitResult | LAYOUT_COMMENT_OFFSET | 108 |  | UNVERIFIED | UNVERIFIED | source layout annotation has no target-image semantic proof |
| `src/SDK/Minecraft/World/Level.cpp:161:pointer-add#1` | Level | DIRECT_LITERAL_OFFSET | 88 |  | UNVERIFIED | UNVERIFIED | hardcoded literal has no scanner or semantic proof |
| `Level::mTick` | Level | CLASS_FIELD | OffsetProvider::LevelData_mTick |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `BlockPalette::mLevel` | BlockPalette | CLASS_FIELD | OffsetProvider::BlockPalette_mLevel |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `Level::mVfTable` | Level | CLASS_FIELD | 0 |  | UNVERIFIED | UNVERIFIED | no independent static semantic evidence |
| `src/Utils/GameUtils/ItemUtils.cpp:55:pointer-add#1` | ItemUtils | DIRECT_LITERAL_OFFSET | 160 |  | UNVERIFIED | UNVERIFIED | hardcoded literal has no scanner or semantic proof |
| `src/Utils/MemUtils.cpp:266:pointer-add#1` | MemUtils | DIRECT_LITERAL_OFFSET | 1 |  | UNVERIFIED | UNVERIFIED | hardcoded literal has no scanner or semantic proof |
| `src/Utils/MemUtils.cpp:275:pointer-add#1` | MemUtils | DIRECT_LITERAL_OFFSET | 3 |  | UNVERIFIED | UNVERIFIED | hardcoded literal has no scanner or semantic proof |
| `src/Utils/MemUtils.cpp:357:pointer-add#1` | MemUtils | DIRECT_LITERAL_OFFSET | 1 |  | UNVERIFIED | UNVERIFIED | hardcoded literal has no scanner or semantic proof |
| `src/Utils/MemUtils.cpp:366:pointer-add#1` | MemUtils | DIRECT_LITERAL_OFFSET | 3 |  | UNVERIFIED | UNVERIFIED | hardcoded literal has no scanner or semantic proof |
