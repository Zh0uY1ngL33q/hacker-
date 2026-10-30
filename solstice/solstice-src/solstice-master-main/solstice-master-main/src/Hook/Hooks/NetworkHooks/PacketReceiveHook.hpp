#pragma once
//
// Created by vastrakai on 7/2/2024.
//

#include <Hook/Hook.hpp>
#include <SDK/Minecraft/Network/PacketID.hpp>

class PacketReceiveHook : public Hook {
public:
    PacketReceiveHook() : Hook() {
        mName = "PacketReceiveHook";
    }

    // Keyed by the detoured handler-function address, not by PacketID:
    // packet ids outside the 0x136 table ride other handlers through this
    // callback, and per-id lookup created an empty entry and deref-crashed.
    static std::unordered_map<uintptr_t, std::shared_ptr<Detour>> mDetours;
    static inline void* NetworkIdentifier = nullptr;

    static void* onPacketSend(void* _this, void* networkIdentifier, void* netEventCallback, std::shared_ptr<class Packet> packet);
    static void handlePacket(std::shared_ptr<Packet> packet);
    void init() override;
};

