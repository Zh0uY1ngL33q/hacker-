#pragma once
#include <cstdint>
#include <string>
#include <atomic>
#include <type_traits>   // std::is_void_v, used by callVirtualFunc's guard
#define _AMD64_
#include <minwindef.h>
#include <unordered_map>
#include <Features/Events/SendImmediateEvent.hpp>
//
// Created by vastrakai on 6/25/2024.
//


template<typename Func, typename... Args>
bool TryCallWrapper(Func func, Args&&... args) {
    __try
    {
        func(std::forward<Args>(args)...);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// Refactored TRY_CALL using lambdas and spdlog
#define TRY_CALL(func, ...) \
[&]() { \
bool result = TryCallWrapper([&]() { func(__VA_ARGS__); }); \
if (!result) { \
spdlog::error("Exception thrown in {} at line {} in {}", __FUNCTION__, __LINE__, __FILE__); \
} \
return result; \
}()

// lol
namespace serenity::utils::ptr {

    using ptr_t = union ptr_data_t {
        uintptr_t address;
        void *v_ptr;
        uintptr_t hat_module;

        template <typename pointing_t>
        pointing_t as() {
            return reinterpret_cast<pointing_t>(this->v_ptr);
        }

        [[nodiscard]] ptr_data_t offset(const uintptr_t p_offset) const {
            return ptr_data_t { .address = this->address + p_offset };
        }
    };

    inline ptr_t ptr(const uintptr_t p_address) {
        ptr_data_t data {};
        data.address = p_address;
        return data;
    }

    template <typename pointing_t>
    ptr_t ptr(pointing_t p_ptr) {
        return ptr_data_t { .v_ptr = reinterpret_cast<void*>(p_ptr) };
    }
}

using namespace serenity::utils::ptr;


class MemUtils {
public:
    template<typename Ret, typename... Args>
    static auto getFunc(void* func);
    template<typename Ret, typename... Args>
    static auto getVirtualFunc(void* _this, int index);

    template <typename TRet, typename... TArgs>
    static TRet callFastcall(void* func, TArgs... args)
    {
        using Fn = TRet(__fastcall*)(TArgs...);
        Fn f = reinterpret_cast<Fn>(func);
        return f(args...);
    }

    template <typename TRet, typename... TArgs>
    static TRet callFastcall(uintptr_t func, TArgs... args)
    {
        using Fn = TRet(__fastcall*)(TArgs...);
        Fn f = reinterpret_cast<Fn>(func);
        return f(args...);
    }

    template<typename Ret, typename... Args>
    static auto callVirtualFunc(int index, void* _this, Args... args)
    {
        using Fn = Ret(__thiscall*)(void*, Args...);
        // Slot 0 is the scalar-deleting destructor in the MSVC ABI, never a
        // business method, and OffsetProvider leaves the index at 0 whenever a
        // signature fails or resolves to an implausible value. Calling through
        // slot 0 would destroy the object while the caller is still using it, so
        // a rejected index has to become a no-op rather than a different crash.
        // Zero-cost guards only. isValidVirtualCall() below is the full check,
        // but it issues VirtualQuery calls and must not run on every per-tick
        // virtual call. 4096 is the same ceiling plausibleOffset() applies to
        // vtable indices, so an implausible index cannot read past the table.
        if (_this == nullptr || index <= 0 || index >= 4096) {
            if constexpr (std::is_void_v<Ret>) {
                return;
            } else if constexpr (std::is_reference_v<Ret>) {
                // A caller can ask for a reference -- ClientInstance::getPlayerName
                // returns std::string& -- and Ret{} cannot initialise a reference,
                // which is what C2440 here meant. Return a function-local static
                // default instead: only the failure path reaches this, and a
                // reference has to point at something that outlives the call.
                static std::remove_cv_t<std::remove_reference_t<Ret>> s_fallback{};
                return static_cast<Ret>(s_fallback);
            } else {
                return Ret{};
            }
        }
        auto vtable = *reinterpret_cast<uintptr_t**>(_this);
        return reinterpret_cast<Fn>(vtable[index])(_this, args...);
    }

    /// Unified validation entry for the two raw-access patterns in this SDK.
    /// Deliberately NOT wired into every existing call site: isValidPtr() issues
    /// a VirtualQuery, and doing that on each per-tick virtual call stalls the
    /// main thread. Use these from newly written getters and from diagnostics;
    /// callVirtualFunc keeps its zero-cost index guard above.
    static bool isValidVirtualCall(void* object, int index)
    {
        if (!object || index <= 0 || index >= 4096) return false;
        const uintptr_t obj = reinterpret_cast<uintptr_t>(object);
        if (!isReadableRange(obj, sizeof(uintptr_t))) return false;
        const uintptr_t vtable = *reinterpret_cast<uintptr_t*>(object);
        if (!isReadableRange(vtable, sizeof(uintptr_t))) return false;
        const uintptr_t slot = vtable + static_cast<size_t>(index) * sizeof(uintptr_t);
        if (!isReadableRange(slot, sizeof(uintptr_t))) return false;
        const uintptr_t target = *reinterpret_cast<uintptr_t*>(slot);
        // A vtable slot must point at executable code. isValidPtr() only asked
        // for MEM_COMMIT, which also accepts ordinary read/write data pages and
        // PAGE_NOACCESS / PAGE_GUARD regions.
        return isPatchableCode(target, 1);
    }

    template <typename T>
    static bool readMemberChecked(void* object, int offset, T& out)
    {
        // POLYMORPHIC objects only: the first eight bytes are the vtable
        // pointer, so offset < 8 is never a member here. This is NOT the same
        // predicate as OffsetProvider::plausibleOffset(), which is a static
        // gate over all field kinds and deliberately accepts offset 0 (e.g. the
        // first member of a non-polymorphic struct). Do not assume the two
        // accept the same offsets. 0x8000 is the shared exclusive upper bound,
        // so offset == 0x8000 is refused just as plausibleOffset() refuses it.
        if (!object || offset < 8 || offset >= 0x8000) return false;
        const uintptr_t addr = reinterpret_cast<uintptr_t>(object) + offset;
        if (!isReadableRange(addr, sizeof(T))) return false;
        out = *reinterpret_cast<T*>(addr);
        return true;
    }

    template<typename T>
    static void Write(uintptr_t address, T value) {
        // A field pointer derived from an unresolved offset is null; storing
        // through it is an access violation, not a no-op.
        if (address < 0x10000) return;
        // oldProtect is a pointer to the old protection
        DWORD oldProtect;
        VirtualProtect((void*)address, sizeof(T), PAGE_EXECUTE_READWRITE, &oldProtect);
        *(T*)address = value;
        VirtualProtect((void*)address, sizeof(T), oldProtect, &oldProtect);
    }

    /// <summary>
    /// Defines a function to byte-patch an address with a given byte array
    /// </summary>
    /// <remarks>
    /// This should only be used in .cpp files
    /// </remarks>
    /// <param name="name">The name of the function</param>
    /// <param name="addr">The address to patch</param>
    /// <param name="bytes">The bytes to replace the address with</param>
    #define DEFINE_PATCH_FUNC(name, addr, bytes) \
        void name(bool patch) { \
            static std::vector<unsigned char> ogBytes = { bytes }; \
            static bool wasPatched = false; \
            static bool reportedInvalid = false; \
            if (addr == 0) { \
                if (!reportedInvalid) { \
                    reportedInvalid = true; \
                    spdlog::error("Failed to patch {} at 0x{:X} [Invalid address]", #name, addr); \
                } \
                return; \
            } \
            if (patch) { \
                 if (!wasPatched) { \
                     ogBytes = MemUtils::readBytes(addr, bytes.size()); \
                     MemUtils::writeBytes(addr, bytes); \
                     spdlog::trace("Patched {} at 0x{:X}", #name, addr); \
                     wasPatched = true; \
                } \
            } else { \
                if (wasPatched) { \
                    MemUtils::writeBytes(addr, ogBytes); \
                    spdlog::trace("Unpatched {} at 0x{:X}", #name, addr); \
                    wasPatched = false; \
                } \
            } \
        }

    /// <summary>
    /// Defines a function to NOP-patch an address with a given size
    /// </summary>
    /// <remarks>
    /// This should only be used in .cpp files
    /// </remarks>
    /// <param name="name">The name of the function</param>
    /// <param name="addr">The address to patch</param>
    /// <param name="size">The amount of bytes to replace with NOP instructions</param>
    #define DEFINE_NOP_PATCH_FUNC(name, addr, size) \
        void name(bool patch) { \
            static std::vector<unsigned char> ogBytes; \
            static bool wasPatched = false; \
            static bool reportedInvalid = false; \
            if (addr == 0) { \
                if (!reportedInvalid) { \
                    reportedInvalid = true; \
                    spdlog::error("Failed to patch {} at 0x{:X} [Invalid address]", #name, addr); \
                } \
                return; \
            } \
            if (patch) { \
                if (!wasPatched) { \
                    ogBytes = MemUtils::readBytes(addr, size); \
                    std::vector<unsigned char> bytes(size, 0x90); \
                    MemUtils::writeBytes(addr, bytes); \
                    spdlog::trace("Patched {} at 0x{:X}", #name, addr); \
                    wasPatched = true; \
                } \
            } else { \
                if (wasPatched) { \
                    MemUtils::writeBytes(addr, ogBytes); \
                    spdlog::trace("Unpatched {} at 0x{:X}", #name, addr); \
                    wasPatched = false; \
                } \
            } \
        }


    #define PATCH_BYTES(addr, bytes) MemUtils::writeBytes(addr, bytes)


    #define AS_FIELD(type, name, fn) __declspec(property(get = fn, put = set##name)) type name

    #define CLASS_FIELD(type, name, offset) \
        AS_FIELD(type, name, get##name); \
        type get##name() const { return *reinterpret_cast<type*>(reinterpret_cast<uintptr_t>(this) + offset); } \
        void set##name(type v) const { *reinterpret_cast<type*>(reinterpret_cast<uintptr_t>(this) + offset) = std::move(v); }

    #define TOKENPASTE(x, y) x ## y
    #define TOKENPASTE2(x, y) TOKENPASTE(x, y)
    #define PAD(size) \
        private: \
            char TOKENPASTE2(padding_, __LINE__) [size] = {0}; \
        public:
    /// <summary>
    /// Defines a padding field of a given size
    /// </summary>
    /// <param name="size">The size of the padding</param>
    #define PADDING(size) char TOKENPASTE2(padding_, __LINE__) [size]

    static uintptr_t getAddressByIndex(uintptr_t _this, int index);

    static const std::string getMbMemoryString(uintptr_t addr);
    static int32_t GetRelativeAddress(uintptr_t ptr, uintptr_t endAddress);
    static uintptr_t GetVTableFunction(void *_this, int index);
    static std::string getModulePath(HMODULE handle);
    static void writeBytes(uintptr_t ptr, const std::vector<unsigned char>& bytes, size_t size);
    static void writeBytes(uintptr_t ptr, const void* bytes, size_t size);
    static void writeBytes(uintptr_t ptr, const std::vector<unsigned char>& bytes);
    static void copyBytes(uintptr_t dest, uintptr_t src, size_t size);
    static std::vector<unsigned char> readBytes(uintptr_t ptr, size_t size);
    static void ReadBytes(void *address, void *buffer, size_t size);
    static void setProtection(uintptr_t ptr, size_t size, DWORD protection);
    static std::string bytesToHex(char* bytes, int length);
    static std::vector<uintptr_t> findPattern(const std::string& pattern);
    static void NopBytes(uintptr_t address, size_t size);
public:
    static uintptr_t findString(const std::string& string);
    static uintptr_t findReference(uintptr_t address);
    static std::vector<uintptr_t> findReferences(uintptr_t address);
    static uintptr_t getTopOfFunction(uintptr_t address);
    static bool isValidPtr(uintptr_t address);

    /// True when a committed page's base protection actually grants READ.
    /// PAGE_EXECUTE alone does not, and PAGE_GUARD / PAGE_NOACCESS fault on read.
    /// Shared by isReadableRange() and CodeSection::csReadableChunks() so the
    /// runtime-readable and validation-readable verdicts cannot drift apart.
    static bool isReadableProtect(DWORD protect);

    /// Committed AND readable for the whole address .. address+size range,
    /// which must lie inside one region. isValidPtr() only checks
    /// State == MEM_COMMIT, so it accepts PAGE_EXECUTE / PAGE_NOACCESS /
    /// PAGE_GUARD pages that would still fault when read. Not for per-tick hot
    /// paths - it issues a VirtualQuery.
    static bool isReadableRange(uintptr_t address, size_t size);

    /// True if `address` is code inside the host module and safe to overwrite.
    ///
    /// Signature-resolution failures leave the patch address at zero, and the
    /// resolvers that "succeed" on the wrong instruction leave a data offset
    /// (0x50B3069 and friends). Both were passed straight to writeBytes, which
    /// VirtualProtects, ignores the failure and memcpys -- an access violation
    /// inside a module's onEnable, which reads as the game crashing when a switch
    /// is flipped. Anything that writes into the game's code should ask first.
    static bool isPatchableCode(uintptr_t address, size_t size);

    /// True if `address` is committed memory inside the host module.
    ///
    /// Data patches need the same question answered as code patches, minus the
    /// executable requirement: a resolver that lands on the wrong instruction
    /// makes the offset arithmetic that follows point outside the image, and a
    /// float written there corrupts whatever happens to live at that address.
    static bool isInHostModule(uintptr_t address, size_t size);

    /// True if `address` looks like the first instruction of a function.
    ///
    /// MSVC pads the gap between functions with int3 and emits unwind data for
    /// everything it compiles, so a real entry is reached across 0xCC padding.
    /// This is the cheapest way to reject a signature that resolved into the
    /// middle of an unrelated function -- which is what Actor_getNameTag does on
    /// 1.21.120, where the refitted pattern starts with four wildcards.
    static bool isFunctionEntry(uintptr_t address);

private:
    template<typename T>
    static T readMemory(uintptr_t address);

    static uintptr_t findStringInRange(const std::string& string, uintptr_t start, uintptr_t end);
    static uintptr_t findReferenceInRange(uintptr_t ptr, uintptr_t start, uintptr_t end);
};


