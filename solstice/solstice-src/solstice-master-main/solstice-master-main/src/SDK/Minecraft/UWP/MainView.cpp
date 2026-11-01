//
// Created by vastrakai on 6/24/2024.
//

#include "MainView.hpp"
#include <SDK/OffsetProvider.hpp>
#include <SDK/SigManager.hpp>

class BedrockPlatformUWP* MainView::getBedrockPlatform()
{
    // MainView_bedrockPlatform does not resolve on 1.21.120. With the offset at 0
    // member_at() hands back the vtable pointer, which is non-null and therefore
    // looks like success to every caller.
    const int offset = OffsetProvider::MainView_bedrockPlatform;
    if (offset <= 0)
        return nullptr;
    return hat::member_at<BedrockPlatformUWP*>(this, offset);
}

MainView* MainView::getInstance()
{
    // A failed signature leaves the pointer at 0; dereferencing that is a plain
    // null read. The value stored there is not validated here -- callers must use
    // InstanceLocator::isPlausibleObject() before touching the object, because on
    // 1.21.120 this signature resolves to the wrong global entirely.
    if (!SigManager::MainView_instance)
        return nullptr;

    static auto mainView = reinterpret_cast<MainView**>(SigManager::MainView_instance);
    return *mainView;
}