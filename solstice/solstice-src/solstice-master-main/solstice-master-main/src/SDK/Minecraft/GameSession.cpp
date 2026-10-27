//
// Created by vastrakai on 6/25/2024.
//

#include "GameSession.hpp"

#include <libhat.hpp>
#include <SDK/OffsetProvider.hpp>

void* GameSession::getEventCallback()
{
    // GameSession_mEventCallback is SUSPECT in 1.21.120 (-914012152); reading a
    // member that far out is the crash the handler saw at +1900E5. Report null
    // and let callers fall back to passing the packet through untouched.
    const int off = OffsetProvider::GameSession_mEventCallback;
    if (off <= 0 || off >= 0x8000)
        return nullptr;
    return hat::member_at<void*>(this, off);
}
