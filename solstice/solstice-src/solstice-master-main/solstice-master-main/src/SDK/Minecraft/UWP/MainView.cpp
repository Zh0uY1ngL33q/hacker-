//
// Created by vastrakai on 6/24/2024.
//

#include "MainView.hpp"
#include <SDK/OffsetProvider.hpp>
#include <SDK/SigManager.hpp>

class BedrockPlatformUWP* MainView::getBedrockPlatform()
{
    return hat::member_at<BedrockPlatformUWP*>(this, OffsetProvider::MainView_bedrockPlatform);
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