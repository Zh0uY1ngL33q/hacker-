#pragma once
#include <Utils/SysUtils/xorstr.hpp>
//
// Created by vastrakai on 6/25/2024.
//

enum class OffsetType {
    Index,
    FieldOffset,
};

// kinda aids ik stfu
#define DEFINE_INDEX_FIELD_TYPED(type, name, str, index_offset, offset_type) \
public: \
static inline type (name) = 0; \
private: \
static void name##_initializer() { \
    static bool initialized = false; \
    if (initialized) return; \
    initialized = true; \
    auto result = scanSig(hat::compile_signature<str>(), xorstr_(#name), index_offset); \
    if (!result.has_result()) { SigFuzz::onFailed(xorstr_(#name), hat::compile_signature<str>()); return; } \
    if ((offset_type) == OffsetType::Index) name = *reinterpret_cast<type*>(reinterpret_cast<uintptr_t>(result.get()) + index_offset) / 8; \
    else (name) = *reinterpret_cast<type*>(reinterpret_cast<uintptr_t>(result.get()) + index_offset); \
} \
private: \
static inline std::function<void()> name##_function = (mSigInitializers.emplace_back(name##_initializer), std::function<void()>()); \
public:


#define DEFINE_INDEX_FIELD(name, str, index_offset, offset_type) \
public: \
static inline int (name) = 0; \
private: \
static void name##_initializer() { \
    static bool initialized = false; \
    if (initialized) return; \
    initialized = true; \
    auto result = scanSig(hat::compile_signature<str>(), xorstr_(#name), index_offset); \
    if (!result.has_result()) { SigFuzz::onFailed(xorstr_(#name), hat::compile_signature<str>()); return; } \
    if ((offset_type) == OffsetType::Index) (name) = *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(result.get()) + (index_offset)) / 8; \
    else (name) = *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(result.get()) + (index_offset)); \
} \
private: \
static inline std::function<void()> name##_function = (mSigInitializers.emplace_back(name##_initializer), std::function<void()>()); \
public:

// Defines a direct offset for a field (for offsets/indexes that can't be sigged)
#define DEFINE_FIELD(name, offset) \
public: \
static constexpr inline int (name) = offset;

#include <future>
#include <libhat/Scanner.hpp>
#include <mutex>

#include <SDK/SigFuzz.hpp>

class OffsetProvider {
    static hat::scan_result scanSig(hat::signature_view sig, const std::string& name, int offset = 0);

    static inline std::vector<std::function<void()>> mSigInitializers;
    static inline int mSigScanCount;
public:
    static inline bool mIsInitialized = false;
    static inline std::unordered_map<std::string, uintptr_t> mSigs;

    /// Number of places each signature matched, capped. Anything above 1 means
    /// the signature is not specific enough to be trusted blindly.
    static inline std::unordered_map<std::string, int> mSigMatchCounts;

    /// True when the signature produced no match at all, as opposed to matching
    /// somewhere the offset arithmetic then made meaningless.
    static inline std::unordered_map<std::string, bool> mSigMissing;

    /// initialize() runs the signature initializers under `#pragma omp parallel
    /// for`, and every initializer writes into the maps above. std::unordered_map
    /// is not safe for concurrent insertion, so the writes are serialised here
    /// while the (much more expensive) scanning itself stays parallel.
    static inline std::mutex mSigMutex;

    DEFINE_INDEX_FIELD(ClientInstance_getLocalPlayer, "48 8B 80 ? ? ? ? FF 15 ? ? ? ? 48 85 C0 74 10 48 8B C8 E8 ? ? ? ? 48 8B 0F 48 39 08 74", 3, OffsetType::Index);
    DEFINE_INDEX_FIELD(ClientInstance_getBlockSource, "48 8B ? ? ? ? ? FF 15 ? ? ? ? 48 85 ? 75 ? 48 83 C4 ? 5B C3 48 8B", 3, OffsetType::Index);
    DEFINE_INDEX_FIELD(ClientInstance_getOptions, "48 8B ? ? ? ? ? FF 15 ? ? ? ? 48 8B ? 48 8B ? 48 8B ? ? ? ? ? 48 8B ? FF 15 ? ? ? ? 3C ? 75 ? 48 8B ? 48 8B ? 48 8B ? ? ? ? ? FF 15 ? ? ? ? 3C", 3, OffsetType::Index);
    //DEFINE_INDEX_FIELD(ClientInstance_getScreenName, "48 8B ? ? ? ? ? FF 15 ? ? ? ? 90 48 85 ? 74 ? 48 8B ? E8 ? ? ? ? 90 48 89 ? ? ? 48 8B ? ? ? 48 89 ? ? ? 48 85 ? 74 ? E8 ? ? ? ? 48 8B ? ? ? 48 85 ? 74 ? 48 8B ? E8 ? ? ? ? 48 8B ? 48 8B ? ? ? 48 8B ? ? ? 48 83 C4 ? 5F C3 E8 ? ? ? ? 90 CC CC CC CC CC 48 89 ? ? ? 48 89 ? ? ? 57 48 83 EC ? 48 8B ? ? 48 83 C1", 3, OffsetType::Index);
    DEFINE_INDEX_FIELD(ClientInstance_getMouseGrabbed, "48 8B ? ? ? ? ? FF 15 ? ? ? ? 84 C0 75 ? 48 8B ? ? ? ? ? 48 85 ? 0F 84 ? ? ? ? 80 38 ? 0F 84 ? ? ? ? 48 8B ? ? ? ? ? 48 85 ? 74 ? F0 FF ? ? 0F 10 ? ? ? ? ? 0F 11 ? ? ? ? ? F2 0F", 3, OffsetType::Index); // PLEASE CHECK AFTER IDK IF THIS IS CORRECT!!!
    DEFINE_INDEX_FIELD(ClientInstance_setDisableInput, "48 8B ? ? ? ? ? 48 8B ? FF 15 ? ? ? ? 48 85 ? 74 ? 48 8B ? 48 8B ? 48 8B", 3, OffsetType::Index);
    DEFINE_INDEX_FIELD(ClientInstance_grabMouse, "48 8B 01 48 8B ? ? ? ? ? EB ? 48 8B", 3, OffsetType::Index);
    DEFINE_INDEX_FIELD(ClientInstance_releaseMouse, "48 8B ? ? ? ? ? 48 8B ? FF 15 ? ? ? ? 48 85 ? 74 ? 48 8B ? 48 8B ? 48 8B", 3, OffsetType::Index);
    DEFINE_INDEX_FIELD(ClientInstance_mMinecraftSim, "48 8B 89 ? ? ? ? 48 85 C9 74 0C 48 8B 89 ? ? ? ? E9 ? ? ? ? 0F 57 C0", 3, OffsetType::FieldOffset);
    // 1.21.120: three-candidate ambiguity adjudicated offline (0x1A26D50 access
    // shape, xref pick and baseline value all agree on 0xF0); the runtime scan
    // stays ambiguous and would otherwise pin 0, making getLevelRenderer report
    // the vtable as the object.
    DEFINE_FIELD(ClientInstance_mLevelRenderer, 0xF0);
    DEFINE_INDEX_FIELD(ClientInstance_mPacketSender, "48 8B ? ? ? ? ? FF 15 ? ? ? ? 4C 8B C0 48 8B ? 48 8B ? 10 48 8B ? 49 8B ? FF 15 ? ? ? ?", 3, OffsetType::FieldOffset);
    //DEFINE_INDEX_FIELD(ClientInstance_mGuiData, "48 8B ? ? ? ? ? 48 8B ? 48 85 ? 74 ? 33 C9 48 89 ? 48 89 ? ? 48 8B ? ? 48 85 ? 74 ? F0 FF ? ? 48 8B ? ? 48 8B ? ? 48 8B ? ? 48 89 ? 48 89 ? ? 48 85 ? 74 ? E8 ? ? ? ? 48 8B ? 48 85 ? 74 ? 48 83 38 ? 74 ? 48 8B ? 48 83 C4 ? 5B C3 E8 ? ? ? ? CC CC CC CC CC CC CC CC CC CC CC CC 48 8B ? ? ? ? ? C3 CC CC CC CC CC CC CC CC 48 89 ? ? ? 57", 3, OffsetType::FieldOffset);
    DEFINE_INDEX_FIELD(MinecraftGame_playUi, "48 8B 49 08 48 8B 01 48 8B 80 ? ? ? ? FF 15 ? ? ? ? 48 83 C4 38", 3, OffsetType::Index);
    // Confirm the node+0x28 ClientInstance and getLocalPlayer vslot 35. The
    // shorter tree-only pattern also matched an unrelated map at offset 0x300.
    DEFINE_INDEX_FIELD(MinecraftGame_mClientInstances, "48 8B 99 ? ? ? ? 48 8B FA 48 8B 1B 80 7B 19 00 0F 85 ? ? ? ? 48 8B 4B 28 48 8B 01 48 8B 80 18 01 00 00 FF 15", 3, OffsetType::FieldOffset);
    DEFINE_INDEX_FIELD(MinecraftGame_mProfanityContext, "49 8B ? ? ? ? ? ? 49 8B ? 48 8B ? ? ? ? ? FF 15 ? ? ? ? 48 8D ? ? E8", 4, OffsetType::FieldOffset);
    DEFINE_INDEX_FIELD(MinecraftGame_mMouseGrabbed, "45 ? ? 0F 85 ? ? ? ? 41 C6 ? ? ? ? ?", 2, OffsetType::FieldOffset);
    DEFINE_INDEX_FIELD(MainView_bedrockPlatform, "48 8B ? ? ? ? ? 48 8B ? B2 ? 48 8B ? ? ? ? ? FF 15 ? ? ? ? 84 C0", 3, OffsetType::FieldOffset);
    // The old pattern started inside the function body (0x593C6B2), so byte 3
    // was an instruction byte, not a vtable displacement. Offline convergence:
    // 13 of 14 Actor-family RTTI vtables (LocalPlayer/Player/Mob/Monster/...)
    // install 0x58F1A80 at slot 29 and the ctor at 0x5937905 installs it too.
    DEFINE_FIELD(Actor_baseTick, 29);
    DEFINE_INDEX_FIELD(MinecraftSim_mGameSim, "49 8B 85 ? ? ? ? 8B 40", 3, OffsetType::FieldOffset);
    DEFINE_INDEX_FIELD(MinecraftSim_mRenderSim, "48 8B ? ? ? ? ? F3 0F ? ? ? F3 41 ? ? ? ? 48 8D", 3, OffsetType::FieldOffset);
    DEFINE_INDEX_FIELD(MinecraftSim_mGameSession, "48 8B 99 ? ? ? ? 48 85 DB 74 ? 48 8D ? ? E8 ? ? ? ? 84 C0 74", 3, OffsetType::FieldOffset);
    DEFINE_INDEX_FIELD_TYPED(uint8_t, GameSession_mEventCallback, "48 8B 4B ? 48 85 C9 74 0D 48 8B 01 48 8B 40 20 FF 15 ? ? ? ? 48 ? ? ?", 3, OffsetType::FieldOffset);
    DEFINE_INDEX_FIELD_TYPED(uint8_t, BedrockPlatformUWP_mcGame, "48 8B ? ? 48 8B ? 48 8B ? ? FF 15 ? ? ? ? 84 C0 74 ? 48 8B ? ? 48 8B ? 48 8B ? ? FF 15 ? ? ? ? 84 C0", 3, OffsetType::FieldOffset);
    DEFINE_INDEX_FIELD(bgfx_d3d12_RendererContextD3D12_m_commandQueue, "48 8B ? ? ? ? ? 4C 8B ? ? 48 8B ? 48 8B ? ? 48 8B ? ? FF 15 ? ? ? ? 48 FF ? ? 48 8B ? ? 48 8B ? 48 3B ? 74 ? 48 8B", 3, OffsetType::FieldOffset);
    DEFINE_INDEX_FIELD(bgfx_context_m_renderCtx, "48 8B ? ? ? ? ? 48 8B ? 48 8B ? ? FF 15 ? ? ? ? 48 89 ? ? ? ? ? 48 63", 3, OffsetType::FieldOffset);
    DEFINE_INDEX_FIELD(BlockLegacy_mBlockId, "44 0F ? ? ? ? ? ? B8 ? ? ? ? 48 8B ? 48 8B", 4, OffsetType::FieldOffset);
    DEFINE_INDEX_FIELD(BlockLegacy_mayPlaceOn, "48 8B ? ? ? ? ? FF 15 ? ? ? ? 48 8B ? 0F B6 ? E8 ? ? ? ? 48 85 ? 74 ? 44 0F", 3, OffsetType::Index);
    DEFINE_INDEX_FIELD(Actor_mContainerManagerModel, "49 8B ? ? ? ? ? 48 89 ? ? ? 48 89 ? ? ? F0 FF ? ? 66 0F ? ? ? ? EB ? 0F 57 ? 66 0F 73 D8 ? 66 48 ? ? ? 0F 57 ? 66 0F ? ? ? ? ? ? 48 85", 3, OffsetType::FieldOffset);
    DEFINE_INDEX_FIELD(Actor_mGameMode, "49 8B 8E ? ? ? ? 45 84 FF 74 ? E8 07", 3, OffsetType::FieldOffset);
    DEFINE_INDEX_FIELD(Actor_mSupplies, "48 8B ? ? ? ? ? 80 BA 18 01 00 00 ? 75 ? 48 8B ? ? ? ? ? 8B 52 ? 48 8B ? 48 8B ? ? 48 FF ? ? ? ? ? 48 8D ? ? ? ? ? C3 8B 81", 3, OffsetType::FieldOffset);
    DEFINE_INDEX_FIELD(PlayerInventory_mContainer, "48 8B 89 ? ? ? ? 8B 52 ? 48 8B 01 48 8B 40 ? 48 FF 25 ? ? ? ? 48 8D 05 ? ? ? ?", 3, OffsetType::FieldOffset);
    // 1.21.120: the old pattern captured the displacement of a field load
    // (`mov rsi,[r14+0x1230]`) rather than a vtable slot, so the derived index
    // became 0x1230/8 = 582 and Container::getItem() called vtable[582], which
    // reads unrelated data -- every slot returned the same module pointer.
    // Offline: RTTI ".?AVInventory@@" -> vtable 0x91DE118 (ImageBase 0x7FF714E80000),
    // slot 8 is `test edx,edx / js / mov r8,[rcx+0x198] / mov rax,[rcx+0x1a0] /
    // sar rax,8 / cmp edx,eax / movsxd rax,edx / shl rax,8 / add rax,r8 / ret`,
    // i.e. `return &mItemStacks[slot]` over a 0x100-byte element vector.
    // getItem is a Container virtual, so the slot is shared by all subclasses.
    DEFINE_FIELD(Container_getItem, 8);
    DEFINE_INDEX_FIELD(LevelRenderer_mRendererPlayer, "48 8B ? ? ? ? ? F3 0F ? ? ? ? ? ? 0F AF", 3, OffsetType::FieldOffset);
    DEFINE_INDEX_FIELD(LevelRendererPlayer_mCameraPos, "F3 0F ? ? ? ? ? ? F3 0F ? ? F3 0F ? ? F3 0F ? ? F3 0F ? ? F3 0F ? ? F3 0F ? ? 0F 2F ? ? ? ? ? 0F 83 ? ? ? ? 80 BB 63 13 00 00", 4, OffsetType::FieldOffset);
    DEFINE_INDEX_FIELD(LevelRendererPlayer_mFovX, "0F 10 ? ? ? ? ? 0F 11 ? ? 0F 10 ? ? ? ? ? 0F 11 ? ? 0F 10 ? ? ? ? ? 0F 11 ? ? 0F 10 ? ? ? ? ? 0F 11 ? ? 80 B9 58 0F 00 00", 3, OffsetType::FieldOffset);
    DEFINE_INDEX_FIELD(LevelRendererPlayer_mFovY, "89 87 ? ? ? ? 41 8B ? ? ? 89 87 ? ? ? ? 41 8B ? ? ? 89 87 ? ? ? ? 41 8B ? ? ? 89 87 ? ? ? ? 41 8B ? ? ? 89 87 ? ? ? ? 41 8B ? ? ? 89 87 ? ? ? ? 41 8B ? ? ? F3 0F", 2, OffsetType::FieldOffset);
    DEFINE_INDEX_FIELD(Actor_mSwinging, "80 ? ? ? ? ? 00 74 1C 8B ? ? ? ? ? FF C1 89 ? ? ? ?", 2, OffsetType::FieldOffset);
    DEFINE_INDEX_FIELD(Actor_swing, "48 8B ? ? ? ? ? FF 15 ? ? ? ? 83 7C 24 ? ? 74", 3, OffsetType::Index);
    DEFINE_INDEX_FIELD(Actor_mLevel, "48 8B ? ? ? ? ? 48 89 ? ? ? F3 0F ? ? ? 50 48", 3, OffsetType::FieldOffset);
    DEFINE_INDEX_FIELD(Level_getHitResult, "E8 ? ? ? ? E8 ? ? ? ? 48 ? ? E8 ? ? ? ? E8 ? ? ?", 3, OffsetType::Index);
    DEFINE_INDEX_FIELD(Level_getPlayerList, "48 8B 80 ? ? ? ? FF 15 ? ? ? ? 8B 48 ? 39 8E ? ? ? ? 0F 84", 3, OffsetType::Index);
    DEFINE_INDEX_FIELD(Level_getPlayerMovementSettings, "48 8B 80 ? ? ? ? FF 15 ? ? ? ? 49 8B 7E ? 33 ED", 3, OffsetType::Index);
    DEFINE_INDEX_FIELD(Level_getLevelData, "48 8B ? ? ? ? ? FF 15 ? ? ? ? 0F 10 ? ? ? ? ? 0F 11 ? ? 8B 88", 3, OffsetType::Index); // Also works on .40
    DEFINE_INDEX_FIELD(Level_getBlockPalette, "48 8B ? ? ? ? ? 48 8B ? FF 15 ? ? ? ? 4C 8B ? 48 8B ? 48 8B ? ? 48 8D ? ? 49 8B ? FF 15 ? ? ? ? 48 8B ? 30 E8", 3, OffsetType::Index);
    DEFINE_INDEX_FIELD(LevelData_mTick, "48 8B ? ? ? ? ? 4C 8B ? ? ? ? ? E8 ? ? ? ? 44 8B", 3, OffsetType::FieldOffset);
    DEFINE_INDEX_FIELD(Actor_mDestroying, "0F B6 ? ? ? ? ? 80 7B ? 00 0F ? ? ? ?", 3, OffsetType::FieldOffset);
    DEFINE_INDEX_FIELD_TYPED(uint8_t, UIProfanityContext_mEnabled, "80 79 ? ? 74 ? 80 79 ? ? 74 ? 45 ? ? 75 ? 4C 8D", 2, OffsetType::FieldOffset);
    //DEFINE_INDEX_FIELD(Bone_mPartModel, "8B 81 ? ? ? ? 89 82 ? ? ? ? F3 0F ? ? ? ? ? ? 0F 57", 2, OffsetType::FieldOffset); // this isn't even correct lmao
    DEFINE_INDEX_FIELD(Actor_mHurtTimeComponent, "48 8B ? ? ? ? ? 48 85 ? 74 ? 66 0F ? ? ? ? ? ? 0F 5B ? 0F 2F", 3, OffsetType::FieldOffset);
    DEFINE_INDEX_FIELD(BlockSource_getChunk, "48 8B ? ? ? ? ? 44 89 ? ? ? FF 15 ? ? ? ? 48 83 C4", 3, OffsetType::Index);
    DEFINE_INDEX_FIELD(BlockSource_setBlock, "48 8B ? ? ? ? ? FF 15 ? ? ? ? EB ? 41 83 FF", 3, OffsetType::Index);
    DEFINE_INDEX_FIELD(LevelChunk_mSubChunks, "48 2B ? ? ? ? ? 48 8B ? 48 C1 FF ? 48 0F", 3, OffsetType::FieldOffset);
    DEFINE_INDEX_FIELD_TYPED(uint8_t, BlockSource_mBuildHeight, "0F BF ? ? 3B C8 0F 8D ? ? ? ? 8B 0F", 3, OffsetType::FieldOffset);
    DEFINE_INDEX_FIELD_TYPED(uint8_t, ContainerManagerModel_getContainerType, "48 8B ? ? FF 15 ? ? ? ? 84 C0 75 ? 48 8B ? 48 8B ? 48 8B ? ? FF 15 ? ? ? ? 84 C0 74 ? B0", 3, OffsetType::Index);
    DEFINE_INDEX_FIELD_TYPED(uint8_t, ContainerManagerModel_getSlot, "48 8B ? ? FF 15 ? ? ? ? 0F B6 ? ? 44 89", 3, OffsetType::Index); // what the actual fcuk is wrong with me.
    DEFINE_INDEX_FIELD(Actor_mSerializedSkin, "4C 8B ? ? ? ? ? 48 8B ? 48 8B ? ? 89 44", 3, OffsetType::FieldOffset);
    DEFINE_INDEX_FIELD_TYPED(uint8_t, BlockLegacy_getCollisionShape, "48 8B ? ? FF 15 ? ? ? ? 0F 10 ? 0F 11 ? ? ? ? ? ? F2 0F ? ? ? F2 0F", 3, OffsetType::Index);
    DEFINE_INDEX_FIELD(BlockSource_clip, "48 8B ? ? ? ? ? FF 15 ? ? ? ? 90 48 8D ? ? ? ? ? 48 8D ? ? ? ? ? E8 ? ? ? ? 48 8B", 3, OffsetType::Index);

    DEFINE_INDEX_FIELD(ClientInstance_getInputHandler, "48 8B 80 ? ? ? ? FF 15 ? ? ? ? 48 8B C8 E8 ? ? ? ? 83 F8 ? 0F 85 ? ? ? ? 48 8B 8D ? ? ? ? 48 8B 01 48 8B 80 ? ? ? ?", 3, OffsetType::Index);
    DEFINE_INDEX_FIELD(ClientInputMappingFactory_mKeyboardMouseSettings, "48 8B ? ? ? ? ? FF 15 ? ? ? ? 48 8B C8 E8 ? ? ? ? 48 3B", 3, OffsetType::FieldOffset); // TODO: replace with betta siggy

    //DEFINE_INDEX_FIELD_TYPED(uint8_t, ClientInputHandler_mMappingFactory, "48 8B ? ? EB ? 48 8D ? ? 48 85 ? 74 ? 48 8B ? E8 ? ? ? ? 48 8B ? 48 8B ? ? ? 48 33 ? E8 ? ? ? ? 48 8B ? ? ? 48 8B ? ? ? 48 83 C4 ? 5F C3 E8 ? ? ? ? CC CC CC CC CC CC 48 89", 3, OffsetType::FieldOffset);
    DEFINE_FIELD(ClientInputHandler_mMappingFactory, 0x28);

    // TODO: Move all CLASS_FIELD declarations and hat::member_at offsets to here
    DEFINE_FIELD(ContainerScreenController_tryExit, 12);
    DEFINE_FIELD(ItemStack_reInit, 3);
    DEFINE_FIELD(MinecraftUIRenderContext_drawImage, 7);
    DEFINE_FIELD(Actor_mEntityIdentifier, 0x240);
    DEFINE_FIELD(Actor_mLocalName, 0xCB0);
    DEFINE_FIELD(ClientInstance_mViewMatrix, 0x368);
    DEFINE_FIELD(ClientInstance_mFovX, 0x728);
    DEFINE_FIELD(ClientInstance_mFovY, 0x73C);
    DEFINE_FIELD(Block_mRuntimeId, 0xC0);
    DEFINE_FIELD(Block_mLegacy, 0x30);
    DEFINE_FIELD(ClientInstance_getScreenName, 266); // i will make sig for it later (maybe)
    DEFINE_FIELD(ClientInstance_mGuiData, 0x590); // i will make sig for it later (maybe)
    DEFINE_FIELD(BlockPalette_mLevel, 0x80);




    DEFINE_INDEX_FIELD(Actor_mContainerModel, "48 8B ? ? ? ? ? 0F ? ? 48 85 ? 74 ? 48 8B 01 48 8B ? ? FF 15 ? ? ?", 3, OffsetType::FieldOffset);
    DEFINE_INDEX_FIELD(ClientInstance_mStackScene, "48 8B ? ? ? ? ? E8 ? ? ? ? 90 48 8B ? ? 48 8B ? ? 48 ? ? 74", 3, OffsetType::FieldOffset);
    DEFINE_INDEX_FIELD(Minecraft_mRealTimer, "48 8B 81 ? ? 00 00 F3 0F 10 ? ? 0F 5A ?", 3, OffsetType::FieldOffset);
    DEFINE_INDEX_FIELD(gDataUtil_MiscData_Instance, "48 8B ? ? ? ? ? 48 8B 0D ? ? ? ? 48 8D 35 ? ? ?", 3, OffsetType::FieldOffset);
    static void initialize();
    static void deinitialize();
};
