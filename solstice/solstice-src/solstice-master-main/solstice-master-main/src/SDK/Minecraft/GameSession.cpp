// GameSession callback field access must not use ambiguous signature matches.
#include "GameSession.hpp"
#include <SDK/OffsetProvider.hpp>
#include <SDK/Minecraft/InstanceLocator.hpp>

namespace {
    // Keep SEH in a leaf without C++ objects that need unwinding (MSVC /EHsc).
    void* readCallbackSafely(GameSession* session, int offset)
    {
        __try {
            auto* slot = reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(session) + offset);
            if (!InstanceLocator::isReadableMemory(slot, sizeof(void*))) return nullptr;
            return *slot;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return nullptr;
        }
    }
}

void* GameSession::getEventCallback()
{
    // A small decoded offset is not sufficient: the current 1.21.120
    // signature matches multiple unrelated fields, and its uint8 decoder
    // can make a false match look plausible.
    const auto match = OffsetProvider::mSigMatchCounts.find("GameSession_mEventCallback");
    if (match == OffsetProvider::mSigMatchCounts.end() || match->second != 1)
        return nullptr;
    const int off = OffsetProvider::GameSession_mEventCallback;
    if (!this || off <= 0 || off >= 0x8000)
        return nullptr;
    return readCallbackSafely(this, off);
}
