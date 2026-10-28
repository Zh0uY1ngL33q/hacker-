#pragma once
#include <Hook/Hook.hpp>
//
// Created by vastrakai on 6/25/2024.
//


class KeyHook : public Hook {
public:
    KeyHook() : Hook() {
        mName = "Keyboard::feed";
    }

    static std::unique_ptr<Detour> mDetour;

    static void onKey(uint8_t key, uint32_t state);
    void init() override;
};

