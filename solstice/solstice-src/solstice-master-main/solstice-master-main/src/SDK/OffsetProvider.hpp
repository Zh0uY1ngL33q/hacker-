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
// `type` describes the WIDTH OF THE ENCODED DISPLACEMENT (the uint8_t entries
// in this table are disp8), not the type of the derived value. The old macro
// used it for both: `static inline type name` plus `static_cast<type>(value)`.
// plausibleOffset() accepts member offsets up to 0x8000, so every value above
// 255 was silently truncated back into 0..255 and a verified mapping became a
// wrong one. Read with `type`, store in long long.
#define DEFINE_INDEX_FIELD_TYPED(type, name, str, index_offset, offset_type) \
public: \
static inline long long (name) = 0; \
private: \
static void name##_initializer() { \
    static bool initialized = false; \
    if (initialized) return; \
    initialized = true; \
    auto result = scanSig(hat::compile_signature<str>(), xorstr_(#name), index_offset); \
    if (!result.has_result()) { \
        if (entryStatus(xorstr_(#name)).status == ResolveStatus::Missing) \
            SigFuzz::onFailed(xorstr_(#name), hat::compile_signature<str>()); \
        return; \
    } \
    type encodedValue{}; \
    if (!readOffsetOperand(xorstr_(#name), reinterpret_cast<uintptr_t>(result.get()), \
                           index_offset, &encodedValue, sizeof(encodedValue))) return; \
    const long long rawValue = static_cast<long long>(encodedValue); \
    const bool isIndex = ((offset_type) == OffsetType::Index); \
    /* A vtable slot displacement is always a multiple of sizeof(void*).       \
       Dividing a non-multiple by 8 silently turns a wrong byte into a         \
       plausible-looking slot, so refuse it instead of publishing it. */       \
    if (isIndex && (rawValue % sizeof(uintptr_t)) != 0) { \
        rejectOffset(xorstr_(#name), ResolveStatus::Implausible, rawValue); \
        return; \
    } \
    const long long value = isIndex ? rawValue / 8 : rawValue; \
    if (!plausibleOffset(xorstr_(#name), value, isIndex)) return; \
    (name) = value; \
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
    if (!result.has_result()) { \
        if (entryStatus(xorstr_(#name)).status == ResolveStatus::Missing) \
            SigFuzz::onFailed(xorstr_(#name), hat::compile_signature<str>()); \
        return; \
    } \
    int encodedValue{}; \
    if (!readOffsetOperand(xorstr_(#name), reinterpret_cast<uintptr_t>(result.get()), \
                           index_offset, &encodedValue, sizeof(encodedValue))) return; \
    const long long rawValue = encodedValue; \
    const bool isIndex = ((offset_type) == OffsetType::Index); \
    /* A vtable slot displacement is always a multiple of sizeof(void*).       \
       Dividing a non-multiple by 8 silently turns a wrong byte into a         \
       plausible-looking slot, so refuse it instead of publishing it. */       \
    if (isIndex && (rawValue % sizeof(uintptr_t)) != 0) { \
        rejectOffset(xorstr_(#name), ResolveStatus::Implausible, rawValue); \
        return; \
    } \
    const long long value = isIndex ? rawValue / 8 : rawValue; \
    if (!plausibleOffset(xorstr_(#name), value, isIndex)) return; \
    (name) = static_cast<int>(value); \
} \
private: \
static inline std::function<void()> name##_function = (mSigInitializers.emplace_back(name##_initializer), std::function<void()>()); \
public:

// A direct offset for a field that cannot be sigged. Unlike the scanned
// entries these have NO evidence attached at runtime -- they are measurements
// written into the header. Register each one so initialization can print the
// complete list and nothing unverified can hide outside the audit.
#define DEFINE_FIELD(name, offset) \
public: \
static constexpr inline int (name) = offset; \
private: \
static void name##_auditFn() { registerHardcoded(xorstr_(#name), offset); } \
static inline std::function<void()> name##_audit = \
    (mAuditInitializers.emplace_back(name##_auditFn), std::function<void()>()); \
public:

#include <future>
#include <libhat/Scanner.hpp>
#include <mutex>
#include <utility>
#include <vector>

#include <SDK/SigFuzz.hpp>
#include <SDK/ResolveState.hpp>

class OffsetProvider {
    static hat::scan_result scanSig(hat::signature_view sig, const std::string& name, int offset = 0);
    static bool readOffsetOperand(const std::string& name, uintptr_t match,
                                  int operandOffset, void* output, size_t width);
    static void registerHardcoded(const std::string& name, int value);

    /// Records a value that was refused by a structural check (e.g. a vtable
    /// displacement that is not a multiple of 8) and makes sure the entry can
    /// never be mistaken for a resolved mapping.
    static void rejectOffset(const std::string& name, ResolveStatus status, long long value);

    static inline std::vector<std::function<void()>> mSigInitializers;
    static inline std::vector<std::function<void()>> mAuditInitializers;
    static inline std::vector<std::pair<std::string, int>> mHardcoded;
    static inline int mSigScanCount;

    /// The single source of truth for what happened to each mapping. mSigs is
    /// only an address cache now; never use "!= 0" as a success test.
    static inline ResolveTable mStatus;
public:
    // Called from the per-signature initializers, which initialize() runs under
    // an OpenMP parallel for. Other threads are still inserting into mStatus at
    // that moment, so even a read of a different key needs the same mutex:
    // unordered_map is not safe for a concurrent insert + find.
    static ResolveEntry entryStatus(const std::string& name)
    {
        std::lock_guard<std::mutex> lock(mSigMutex);
        return mStatus.get(name);
    }
    static const ResolveTable& statusTable() { return mStatus; }
    static const std::vector<std::pair<std::string, int>>& hardcodedFields() { return mHardcoded; }

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

    /// A resolved vtable index or member offset has to be small. When a
    /// signature matches the wrong instruction the offset arithmetic still
    /// "succeeds" and yields a number in the millions. Measured on 1.21.120:
    /// Actor_swing resolved to 17305961 (raw displacement 138447688) and
    /// BlockSource_getChunk to 8015420, while the same symbols resolve to 114
    /// and 39 in the 1.21.44 reference build. Actor::swing() feeds its value
    /// straight into callVirtualFunc(), which then reads vtable[17305961] --
    /// roughly 133 MB past the vtable, i.e. unmapped memory. That is the crash
    /// seen as Aura::onBaseTickEvent -> Actor::swing, not a hypothetical.
    /// Values outside the ranges below cannot be real, so the initializer keeps
    /// the variable at 0 and records the rejection for the startup report.
    static bool plausibleOffset(const std::string& name, long long value, bool isIndex);
    static inline std::unordered_map<std::string, long long> mSigImplausible;

    DEFINE_INDEX_FIELD(ClientInstance_getLocalPlayer, "48 8B 80 ? ? ? ? FF 15 ? ? ? ? 48 85 C0 74 10 48 8B C8 E8 ? ? ? ? 48 8B 0F 48 39 08 74", 3, OffsetType::Index);
    // Batch-2 STATIC VERIFIED. The former short call-site pattern had four
    // matches (slots 318, 35, 35 and 69).  Only the slot-318 site is the
    // ClientInstance wrapper: it loads the ClientInstance from [this+0x1A8],
    // invokes slot 0x9F0/8 and then consumes the returned pointer.  Independent
    // vtable analysis identifies slot 318 as the one-line [this+0x118] getter;
    // that member is the BlockSource.  The full wrapper is unique in 1.21.120.
    DEFINE_INDEX_FIELD(ClientInstance_getBlockSource, "40 53 48 83 EC 20 48 8B D9 48 8B 89 A8 01 00 00 48 8B 01 48 8B 80 ? ? ? ? FF 15 ? ? ? ? 48 85 C0 75 ?", 22, OffsetType::Index);
    DEFINE_INDEX_FIELD(ClientInstance_getOptions, "48 8B ? ? ? ? ? FF 15 ? ? ? ? 48 8B ? 48 8B ? 48 8B ? ? ? ? ? 48 8B ? FF 15 ? ? ? ? 3C ? 75 ? 48 8B ? 48 8B ? 48 8B ? ? ? ? ? FF 15 ? ? ? ? 3C", 3, OffsetType::Index);
    //DEFINE_INDEX_FIELD(ClientInstance_getScreenName, "48 8B ? ? ? ? ? FF 15 ? ? ? ? 90 48 85 ? 74 ? 48 8B ? E8 ? ? ? ? 90 48 89 ? ? ? 48 8B ? ? ? 48 89 ? ? ? 48 85 ? 74 ? E8 ? ? ? ? 48 8B ? ? ? 48 85 ? 74 ? 48 8B ? E8 ? ? ? ? 48 8B ? 48 8B ? ? ? 48 8B ? ? ? 48 83 C4 ? 5F C3 E8 ? ? ? ? 90 CC CC CC CC CC 48 89 ? ? ? 48 89 ? ? ? 57 48 83 EC ? 48 8B ? ? 48 83 C1", 3, OffsetType::Index);
    DEFINE_INDEX_FIELD(ClientInstance_getMouseGrabbed, "48 8B ? ? ? ? ? FF 15 ? ? ? ? 84 C0 75 ? 48 8B ? ? ? ? ? 48 85 ? 0F 84 ? ? ? ? 80 38 ? 0F 84 ? ? ? ? 48 8B ? ? ? ? ? 48 85 ? 74 ? F0 FF ? ? 0F 10 ? ? ? ? ? 0F 11 ? ? ? ? ? F2 0F", 3, OffsetType::Index); // PLEASE CHECK AFTER IDK IF THIS IS CORRECT!!!
    // Batch-2 UNVERIFIED. setDisableInput and releaseMouse deliberately retain
    // their ambiguous declaration: the patterns are byte-identical and have
    // the same 122 candidates.  The sole plausible extracted slot (436) is in
    // an unrelated wrapper and cannot be assigned to either semantic method.
    DEFINE_INDEX_FIELD(ClientInstance_setDisableInput, "48 8B ? ? ? ? ? 48 8B ? FF 15 ? ? ? ? 48 85 ? 74 ? 48 8B ? 48 8B ? 48 8B", 3, OffsetType::Index);
    // Batch-2 UNVERIFIED. Offset 3 points at the next instruction, not at a
    // vtable displacement, in all eight matches.  No candidate is promoted.
    DEFINE_INDEX_FIELD(ClientInstance_grabMouse, "48 8B 01 48 8B ? ? ? ? ? EB ? 48 8B", 3, OffsetType::Index);
    DEFINE_INDEX_FIELD(ClientInstance_releaseMouse, "48 8B ? ? ? ? ? 48 8B ? FF 15 ? ? ? ? 48 85 ? 74 ? 48 8B ? 48 8B ? 48 8B", 3, OffsetType::Index);
    DEFINE_INDEX_FIELD(ClientInstance_mMinecraftSim, "48 8B 89 ? ? ? ? 48 85 C9 74 0C 48 8B 89 ? ? ? ? E9 ? ? ? ? 0F 57 C0", 3, OffsetType::FieldOffset);
    // 1.21.120: three-candidate ambiguity adjudicated offline (0x1A26D50 access
    // shape, xref pick and baseline value all agree on 0xF0); the runtime scan
    // stays ambiguous and would otherwise pin 0, making getLevelRenderer report
    // the vtable as the object.
    DEFINE_FIELD(ClientInstance_mLevelRenderer, 0xF0);
    // The old signature for this read a disp out of a *virtual call site*
    // (`mov rax,[obj]; mov rax,[rax+slot]; call guard`) and produced 0x228, which
    // is ClientInstance's vtable slot 69 -- a several-hundred-byte function, not a
    // getter. Two independent measurements give 0x108 instead:
    //   1. ClientInstance's own vtable (RVA 0x8ddb590) has `mov rax,[rcx+0x108];
    //      ret` at slot 308, i.e. getPacketSender() exists as a virtual getter of
    //      exactly this member;
    //   2. LoopbackPacketSender's constructor (RVA 0x1352270, vtable 0x8da3230) is
    //      called right after a 0x68-byte allocation, and the result is stored by
    //      `mov [rsi+0x108], rax` inside ClientInstance::initialize.
    // Same table also confirms mLevelRenderer at 0xF0 (slot 197) and the block
    // source at 0x118 (slot 318), so the surrounding layout is unchanged.
    DEFINE_FIELD(ClientInstance_mPacketSender, 0x108);
    //DEFINE_INDEX_FIELD(ClientInstance_mGuiData, "48 8B ? ? ? ? ? 48 8B ? 48 85 ? 74 ? 33 C9 48 89 ? 48 89 ? ? 48 8B ? ? 48 85 ? 74 ? F0 FF ? ? 48 8B ? ? 48 8B ? ? 48 8B ? ? 48 89 ? 48 89 ? ? 48 85 ? 74 ? E8 ? ? ? ? 48 8B ? 48 85 ? 74 ? 48 83 38 ? 74 ? 48 8B ? 48 83 C4 ? 5B C3 E8 ? ? ? ? CC CC CC CC CC CC CC CC CC CC CC CC 48 8B ? ? ? ? ? C3 CC CC CC CC CC CC CC CC 48 89 ? ? ? 57", 3, OffsetType::FieldOffset);
    // Batch-1 FIX (was IMPLAUSIBLE, value 3238145). Root cause: OPERAND
    // POSITION. The pattern starts at "48 8B 49 08 / 48 8B 01 / 48 8B 80", so
    // the disp32 of "mov rax,[rax+disp32]" is at match+10; the old 3 read
    // "08 48 8B 01" (previous disp8 + next opcodes) in BOTH builds.
    //   1.21.120 hit 0x1CBC3E4 -> disp32 0x520 -> slot 164
    //   1.21.44  hit 0x003DF624 -> disp32 0x4C0 -> slot 152
    // Same instruction shape, slot drift only. Unique, 8-byte aligned, in range.
    // Extraction rule CONFIRMED; call-site identity + behaviour NEED_RUNTIME_TEST.
    DEFINE_INDEX_FIELD(MinecraftGame_playUi, "48 8B 49 08 48 8B 01 48 8B 80 ? ? ? ? FF 15 ? ? ? ? 48 83 C4 38", 10, OffsetType::Index);
    // Confirm the node+0x28 ClientInstance and getLocalPlayer vslot 35. The
    // shorter tree-only pattern also matched an unrelated map at offset 0x300.
    DEFINE_INDEX_FIELD(MinecraftGame_mClientInstances, "48 8B 99 ? ? ? ? 48 8B FA 48 8B 1B 80 7B 19 00 0F 85 ? ? ? ? 48 8B 4B 28 48 8B 01 48 8B 80 18 01 00 00 FF 15", 3, OffsetType::FieldOffset);
    // Batch-1 UNVERIFIED (was IMPLAUSIBLE, value 1). Root cause: INSTRUCTION
    // VERSION CHANGE + WRONG OPERAND. 1.21.44 hit 0x1D68EF "mov rdx,[r12+0x190]"
    // and match+4 correctly read 400. 1.21.120's only hit 0x16CD84D is
    // "49 8B 00 / BA 01 00 00 00" (mov edx,1), so match+4 is an immediate.
    // Anchoring to the 1.21.44 instruction shape gives ZERO hits here: the
    // real access site is unknown. Do NOT hardcode 400 or 1. Consumers gate on
    // usableOffset(). New status MISSING; runtime verification PENDING.
    DEFINE_INDEX_FIELD(MinecraftGame_mProfanityContext, "49 8B 94 24 ? ? ? ? 49 8B ? 48 8B ? ? ? ? ? FF 15 ? ? ? ? 48 8D ? ? E8", 4, OffsetType::FieldOffset);
    // Batch-1 UNVERIFIED, deliberately still IMPLAUSIBLE (-964358145). Root
    // cause: SIGNATURE MATCH ERROR + WRONG OPERAND. 7 wildcards; the only hit
    // 0x37E3BF6 is "45 85 FF" (test r15d,r15d), and match+2 is its ModRM plus
    // the following jne opcode. The real byte write is at match+18:
    // "41 C6 85 18 01 00 00 01" = mov byte ptr [r13+0x118], 1. That is the
    // only such write in the image, but r13's type and the bool's identity are
    // NOT proven (same function also touches [r13+0x148]). 1.21.44 has ZERO
    // hits, so there is no cross-version anchor either. Per the stop rule we
    // do NOT promote 0x118: a unique match is not semantic verification.
    // Consumers gate on usableOffset(); the cursor fallback stays authoritative.
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
    // Batch-2 STATIC VERIFIED. Six old matches produced 0x08 (x4), 0x20 and
    // 0x50.  The 0x50 access is the only one bracketed by the GameSession
    // cleanup sequence for adjacent members 0x48/0x50/0x58; 1.21.44 has the
    // same three-member structure.  Anchoring the following 0x58 access makes
    // the 0x50 operand unique without hard-coding the extracted value.
    DEFINE_INDEX_FIELD_TYPED(uint8_t, GameSession_mEventCallback, "48 8B 4B ? 48 85 C9 74 ? 48 8B 01 48 8B 40 20 FF 15 ? ? ? ? 48 8B 4B 58 48 85 C9 74 ?", 3, OffsetType::FieldOffset);
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
    // Batch-1 UNVERIFIED (was IMPLAUSIBLE, value 17305961). Root cause:
    // SIGNATURE MIS-MATCH + INSTRUCTION VERSION CHANGE. The wildcard run spans
    // an instruction boundary: at 0x38161B9 it starts on "48 8B CB"
    // (mov rcx,rbx), so match+3 is the NEXT instruction "48 8B 40 08" and
    // reads its opcode bytes as a disp32 (138447688 / 8 = 17305961).
    // 1.21.44 had TWO hits: the correct "mov rax,[rax+0x390]" (slot 114) and
    // this same garbage hit; in 1.21.120 only the garbage hit survives.
    // Anchoring ModRM to 0x80 gives ZERO hits here -> honest MISSING instead
    // of a plausible-looking wrong slot. Actor::swing() is inert via the
    // callVirtualFunc index guard. Runtime verification PENDING.
    DEFINE_INDEX_FIELD(Actor_swing, "48 8B 80 ? ? ? ? FF 15 ? ? ? ? 83 7C 24 ? ? 74", 3, OffsetType::Index);
    DEFINE_INDEX_FIELD(Actor_mLevel, "48 8B ? ? ? ? ? 48 89 ? ? ? F3 0F ? ? ? 50 48", 3, OffsetType::FieldOffset);
    // Batch-2 UNVERIFIED. All 182 matches begin at a direct CALL and offset 3
    // is part of its rel32 operand, not a vtable displacement.  The declaration
    // stays rejected; no call target has enough evidence to name getHitResult.
    DEFINE_INDEX_FIELD(Level_getHitResult, "E8 ? ? ? ? E8 ? ? ? ? 48 ? ? E8 ? ? ? ? E8 ? ? ?", 3, OffsetType::Index);
    DEFINE_INDEX_FIELD(Level_getPlayerList, "48 8B 80 ? ? ? ? FF 15 ? ? ? ? 8B 48 ? 39 8E ? ? ? ? 0F 84", 3, OffsetType::Index);
    DEFINE_INDEX_FIELD(Level_getPlayerMovementSettings, "48 8B 80 ? ? ? ? FF 15 ? ? ? ? 49 8B 7E ? 33 ED", 3, OffsetType::Index);
    DEFINE_INDEX_FIELD(Level_getLevelData, "48 8B ? ? ? ? ? FF 15 ? ? ? ? 0F 10 ? ? ? ? ? 0F 11 ? ? 8B 88", 3, OffsetType::Index); // Also works on .40
    DEFINE_INDEX_FIELD(Level_getBlockPalette, "48 8B ? ? ? ? ? 48 8B ? FF 15 ? ? ? ? 4C 8B ? 48 8B ? 48 8B ? ? 48 8D ? ? 49 8B ? FF 15 ? ? ? ? 48 8B ? 30 E8", 3, OffsetType::Index);
    // Batch-2 UNVERIFIED. Of the two matches, one extracts instruction bytes
    // and the other reads [rbp+0x138], a stack-frame object rather than the
    // LevelData returned by Level::getLevelData.  Neither is promoted.
    DEFINE_INDEX_FIELD(LevelData_mTick, "48 8B ? ? ? ? ? 4C 8B ? ? ? ? ? E8 ? ? ? ? 44 8B", 3, OffsetType::FieldOffset);
    // Batch-2 STATIC VERIFIED. Seven of the eight old matches are either
    // mid-instruction RIP displacements or a stack-frame byte.  The remaining
    // function is structurally homologous to 1.21.44: it loads the Actor object
    // from [this], reads one byte at 0x166A and immediately gates the destroy
    // path on that byte and [this+0x10].
    DEFINE_INDEX_FIELD(Actor_mDestroying, "48 8B D9 48 8B 09 48 8B 7B 18 0F B6 81 ? ? ? ? 80 7B 10 00", 13, OffsetType::FieldOffset);
    DEFINE_INDEX_FIELD_TYPED(uint8_t, UIProfanityContext_mEnabled, "80 79 ? ? 74 ? 80 79 ? ? 74 ? 45 ? ? 75 ? 4C 8D", 2, OffsetType::FieldOffset);
    //DEFINE_INDEX_FIELD(Bone_mPartModel, "8B 81 ? ? ? ? 89 82 ? ? ? ? F3 0F ? ? ? ? ? ? 0F 57", 2, OffsetType::FieldOffset); // this isn't even correct lmao
    DEFINE_INDEX_FIELD(Actor_mHurtTimeComponent, "48 8B ? ? ? ? ? 48 85 ? 74 ? 66 0F ? ? ? ? ? ? 0F 5B ? 0F 2F", 3, OffsetType::FieldOffset);
    // Batch-1 UNVERIFIED (was IMPLAUSIBLE, unaligned raw 64123367). Root
    // cause: INSTRUCTION TYPE CHANGE + SIGNATURE MIS-MATCH. 1.21.44 hit
    // 0x338488D "mov rax,[rax+0x138]" -> slot 39. 1.21.120's only hit
    // 0x7C84DCA is "48 8B 05 E7 71 D2 03" = mov rax,[rip+0x3D271E7], a
    // RIP-relative GLOBAL load, not a vtable slot; match+3 is that
    // relocation displacement (not even 8-byte aligned). Anchoring ModRM to
    // 0x80 gives ZERO hits, so the real call site is not identified here.
    // BlockSource::getChunk() is inert via callVirtualFunc's index guard.
    DEFINE_INDEX_FIELD(BlockSource_getChunk, "48 8B 80 ? ? ? ? 44 89 ? ? ? FF 15 ? ? ? ? 48 83 C4", 3, OffsetType::Index);
    // Batch-2 STATIC VERIFIED. The correct candidate prepares the complete
    // setBlock argument shape (rcx=BlockSource, rdx=BlockPos, r8=Block,
    // r9d=flags plus two stack arguments) before calling slot 0x110/8 = 34.
    // The second old match was a ref-count cleanup sequence.  1.21.44 uses the
    // same argument flow at slot 32, providing independent structural evidence.
    DEFINE_INDEX_FIELD(BlockSource_setBlock, "48 89 4C 24 28 4C 8B 7D ? 4C 89 7C 24 20 44 8B CF 4D 8B C6 48 8B D6 49 8B CC 48 8B 80 ? ? ? ? FF 15 ? ? ? ? EB ? 41 83 FF 01", 29, OffsetType::Index);
    DEFINE_INDEX_FIELD(LevelChunk_mSubChunks, "48 2B ? ? ? ? ? 48 8B ? 48 C1 FF ? 48 0F", 3, OffsetType::FieldOffset);
    // Batch-3 FIX (was MISSING). Target-version evidence: BlockSource RTTI
    // .?AVBlockSource@@ -> vtable RVA 0x921B490 (66 slots). Slot 38 is
    // "movzx eax, word ptr [rcx+0x38]; ret" and slot 37 reads [rcx+0x3A].
    // BlockSource.hpp declares getBuildHeight()/getBuildDepth() as two adjacent
    // int16 members at base and base+2, and its CLASS_FIELD layout
    // (mChunkSource 0x28, mDimension 0x30) is confirmed by vtable getters at
    // slot 54 ([rcx+0x28]) and slots 39-41 ([rcx+0x30]).
    // The disp8 below is a WILDCARD: the value is parsed from the 1.21.120
    // image, not hardcoded. The pattern anchors that getter plus the following
    // BlockSource-specific getter sequence, giving exactly one hit at
    // 0x5D3B130 -> 0x38. 1.21.44 shows the same word-sized pair at 0x38/0x3A
    // (reference evidence only; no offset or RVA was copied).
    // New status OK; STATIC VERIFIED, runtime behaviour NEEDS_RUNTIME_TEST.
    DEFINE_INDEX_FIELD_TYPED(uint8_t, BlockSource_mBuildHeight, "0F B7 41 ? C3 CC CC CC CC CC CC CC CC CC CC CC 48 8B 41 08 0F B7 40 14 C3", 3, OffsetType::FieldOffset);
    DEFINE_INDEX_FIELD_TYPED(uint8_t, ContainerManagerModel_getContainerType, "48 8B ? ? FF 15 ? ? ? ? 84 C0 75 ? 48 8B ? 48 8B ? 48 8B ? ? FF 15 ? ? ? ? 84 C0 74 ? B0", 3, OffsetType::Index);
    DEFINE_INDEX_FIELD_TYPED(uint8_t, ContainerManagerModel_getSlot, "48 8B ? ? FF 15 ? ? ? ? 0F B6 ? ? 44 89", 3, OffsetType::Index); // what the actual fcuk is wrong with me.
    DEFINE_INDEX_FIELD(Actor_mSerializedSkin, "4C 8B ? ? ? ? ? 48 8B ? 48 8B ? ? 89 44", 3, OffsetType::FieldOffset);
    DEFINE_INDEX_FIELD_TYPED(uint8_t, BlockLegacy_getCollisionShape, "48 8B ? ? FF 15 ? ? ? ? 0F 10 ? 0F 11 ? ? ? ? ? ? F2 0F ? ? ? F2 0F", 3, OffsetType::Index);
    DEFINE_INDEX_FIELD(BlockSource_clip, "48 8B ? ? ? ? ? FF 15 ? ? ? ? 90 48 8D ? ? ? ? ? 48 8D ? ? ? ? ? E8 ? ? ? ? 48 8B", 3, OffsetType::Index);

    DEFINE_INDEX_FIELD(ClientInstance_getInputHandler, "48 8B 80 ? ? ? ? FF 15 ? ? ? ? 48 8B C8 E8 ? ? ? ? 83 F8 ? 0F 85 ? ? ? ? 48 8B 8D ? ? ? ? 48 8B 01 48 8B 80 ? ? ? ?", 3, OffsetType::Index);
    // Batch-1 UNVERIFIED (was IMPLAUSIBLE, value 138447688). Root cause:
    // SIGNATURE MATCH ERROR + WRONG OPERAND. This pattern does NOT match the
    // 1.21.44 reference image at all, so there is no cross-version anchor.
    // 1.21.120's only hit 0x67A5C13 is "48 8B FA" (mov rdi,rdx); match+3
    // lands on the next instruction "48 8B 40 08" and reads its opcode bytes.
    // Anchoring ModRM to 0x80 gives ZERO hits -> MISSING, not a fake offset.
    // getKeyboardMouseSettings() already returns nullptr for offset <= 0.
    DEFINE_INDEX_FIELD(ClientInputMappingFactory_mKeyboardMouseSettings, "48 8B 80 ? ? ? ? FF 15 ? ? ? ? 48 8B C8 E8 ? ? ? ? 48 3B", 3, OffsetType::FieldOffset);

    //DEFINE_INDEX_FIELD_TYPED(uint8_t, ClientInputHandler_mMappingFactory, "48 8B ? ? EB ? 48 8D ? ? 48 85 ? 74 ? 48 8B ? E8 ? ? ? ? 48 8B ? 48 8B ? ? ? 48 33 ? E8 ? ? ? ? 48 8B ? ? ? 48 8B ? ? ? 48 83 C4 ? 5F C3 E8 ? ? ? ? CC CC CC CC CC CC 48 89", 3, OffsetType::FieldOffset);
    DEFINE_FIELD(ClientInputHandler_mMappingFactory, 0x28);

    // TODO: Move all CLASS_FIELD declarations and hat::member_at offsets to here
    DEFINE_FIELD(ContainerScreenController_tryExit, 12);
    DEFINE_FIELD(ItemStack_reInit, 3);
    DEFINE_FIELD(MinecraftUIRenderContext_drawImage, 7);
    DEFINE_FIELD(Actor_mEntityIdentifier, 0x240);
    DEFINE_FIELD(Actor_mLocalName, 0xCB0);
    // 0x368 reads one column into the matrix: what came back was
    // (r1.x, r1.y, r1.z, 0), (r2.x, r2.y, r2.z, 0), (0, 0, 0, 1), so the third
    // column carried no direction and every projection collapsed. A scan of the
    // object found the rotation at 0x358 with its -Z axis matching the player's
    // forward to 1.0000, which is 16 bytes -- exactly one column -- earlier.
    DEFINE_FIELD(ClientInstance_mViewMatrix, 0x358);
    // 0x728/0x73C read as -FLT_MAX, which made every projection fail. The pair
    // actually sits at 0x720/0x724: the client's vtable carries
    // `movss xmm0,[rcx+0x720]; ret` at slot 455, the matching
    // `movss [rcx+0x720],xmm1; ret` at 456 and `movss xmm0,[rcx+0x724]; ret` at
    // 457 -- a get/set/get triple over two adjacent floats, i.e. one property.
    DEFINE_FIELD(ClientInstance_mFovX, 0x720);
    DEFINE_FIELD(ClientInstance_mFovY, 0x724);
    DEFINE_FIELD(Block_mRuntimeId, 0xC0);
    DEFINE_FIELD(Block_mLegacy, 0x30);
    DEFINE_FIELD(ClientInstance_getScreenName, 266); // i will make sig for it later (maybe)
    // Measured, not guessed. GuiData is a 0xB38-byte allocation created by
    // ClientInstance's UI setup (RVA 0x1A37720) and stored by
    // `mov [rbx+0x6C0], rax` right after its constructor returns; the same
    // function reads [rbx+0x6E8], and that byte is also read by the routine that
    // allocates LoopbackPacketSender into [rsi+0x108] -- i.e. both run on
    // ClientInstance, which is what ties the two offsets to the same object.
    // The neighbouring members 0x680/0x688/0x6A0/0x6A8/0x6B0/0x6B8 all have
    // one-line virtual getters in ClientInstance's own vtable, which is what a
    // cluster of subsystem pointers looks like. 0x590 held something else, so
    // every mResolution read was garbage.
    DEFINE_FIELD(ClientInstance_mGuiData, 0x6C0);
    DEFINE_FIELD(BlockPalette_mLevel, 0x80);




    DEFINE_INDEX_FIELD(Actor_mContainerModel, "48 8B ? ? ? ? ? 0F ? ? 48 85 ? 74 ? 48 8B 01 48 8B ? ? FF 15 ? ? ?", 3, OffsetType::FieldOffset);
    // Batch-2 STATIC VERIFIED. Two independent ClientInstance UI functions
    // access 0x640 with the same stack-scene consumer; the selected unique site
    // is additionally tied to the hud_screen transition.  The 0x1788, 0xD8 and
    // RIP-relative candidates operate on different base objects.
    DEFINE_INDEX_FIELD(ClientInstance_mStackScene, "48 8B 06 C7 80 B8 0A 00 00 01 00 00 00 0F 57 C0 33 C0 0F 11 45 ? 48 89 45 ? 48 8B 06 48 8D 55 ? 48 8B 88 ? ? ? ? E8", 36, OffsetType::FieldOffset);
    DEFINE_INDEX_FIELD(Minecraft_mRealTimer, "48 8B 81 ? ? 00 00 F3 0F 10 ? ? 0F 5A ?", 3, OffsetType::FieldOffset);
    // Batch-2 UNVERIFIED. All 31 candidates are RIP-relative global loads;
    // treating their rel32 operands as object field offsets is invalid.  The
    // actual global and its initialization chain are not yet identified, so
    // this remains rejected rather than selecting a convenient relocation.
    DEFINE_INDEX_FIELD(gDataUtil_MiscData_Instance, "48 8B ? ? ? ? ? 48 8B 0D ? ? ? ? 48 8D 35 ? ? ?", 3, OffsetType::FieldOffset);
    /// Returns true only when every mapping is ResolveStatus::Ok. The caller
    /// MUST act on this: a false return means the SDK must not be used, and
    /// continuing produces crashes far away from the actual bad mapping.
    static bool initialize();
    static void deinitialize();
};
