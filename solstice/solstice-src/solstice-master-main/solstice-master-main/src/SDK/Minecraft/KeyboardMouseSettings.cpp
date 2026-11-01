//
// Created by vastrakai on 10/10/2024.
//

#include "KeyboardMouseSettings.hpp"

int KeyboardMouseSettings::operator[](const std::string& key)
{
    for (auto& bind : mKeyTypeA) {
        if (bind.mBindName == key)
            return bind.mBindKey[0];
    }

    spdlog::error("Failed to find keybind: {}", key);
    return -1;
}

KeyboardMouseSettings* ClientInputMappingFactory::getKeyboardMouseSettings()
{
    // The signature for this offset matches the wrong instruction in 1.21.120 and
    // decodes to 138447688, which plausibleOffset() now rejects and leaves at 0.
    // hat::member_at() at offset 0 would return the object's own vtable pointer
    // dressed up as a KeyboardMouseSettings, so the caller would walk a function
    // table believing it is a keybind array. Null is the only honest answer.
    const int offset = OffsetProvider::ClientInputMappingFactory_mKeyboardMouseSettings;
    if (offset <= 0)
        return nullptr;
    return hat::member_at<KeyboardMouseSettings*>(this, offset);
}

ClientInputMappingFactory* ClientInputHandler::getMappingFactory()
{
    return hat::member_at<ClientInputMappingFactory*>(this, OffsetProvider::ClientInputHandler_mMappingFactory);
}
