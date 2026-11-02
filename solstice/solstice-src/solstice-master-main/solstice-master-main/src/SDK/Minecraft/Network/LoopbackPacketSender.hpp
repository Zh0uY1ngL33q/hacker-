#pragma once
//
// Created by vastrakai on 6/28/2024.
//

#include <Utils/MemUtils.hpp>

/// <summary>
/// The client's packet sender. Never instantiated by us -- this is a view of a game
/// object, so every call is a virtual call through that object's own vtable.
///
/// The slot numbers are measured on 1.21.120 rather than inferred from a C++
/// virtual declaration order, because the two candidate slots are not equivalent:
///
///   slot 2  `mov r8,rdx; lea rdx,[rcx+0x20]; ...`  -- (this, packet) only.
///           PacketSendHook detours exactly this slot (packetSendTarget reads
///           table[2]) to fire PacketOutEvent, so it is the send the game uses.
///   slot 3  same shape but also reads r9, i.e. it takes a third argument that
///           this SDK does not have. Calling it with two hands the function an
///           uninitialised register.
///
/// Sending through slot 2 therefore both matches the game and keeps module-sent
/// packets visible to our own outbound hook, which is what several modules listen
/// to. The old declaration put sendToServer on slot 3 and would have gone to the
/// three-argument variant.
class LoopbackPacketSender {
public:
    CLASS_FIELD(uintptr_t**, vtable, 0x0);
    CLASS_FIELD(class NetworkSystem*, mNetworkSystem, 0x20);

    /// Sends a packet to the server. Fires PacketOutEvent, because that hook is
    /// installed on this exact slot.
    void send(void* packet)
    {
        MemUtils::callVirtualFunc<void, void*>(2, this, packet);
    }

    /// The name most call sites use. Routed through the two-argument send for the
    /// reason above.
    void sendToServer(void* packet)
    {
        send(packet);
    }
};
