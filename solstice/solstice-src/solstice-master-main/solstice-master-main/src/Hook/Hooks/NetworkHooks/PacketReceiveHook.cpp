//
// Created by vastrakai on 7/2/2024.
//

#include "PacketReceiveHook.hpp"

#include <magic_enum.hpp>
#include <SDK/Minecraft/Network/MinecraftPackets.hpp>
#include <Features/Events/PacketInEvent.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/InstanceLocator.hpp>
#include <SDK/Minecraft/GameSession.hpp>
#include <SDK/Minecraft/MinecraftSim.hpp>
#include <Utils/MiscUtils/ColorUtils.hpp>
#include <atomic>
#include <omp.h>

std::unordered_map<uintptr_t, std::shared_ptr<Detour>> PacketReceiveHook::mDetours;

namespace {
    // The detour whose function this packet came through, recovered from the
    // dispatcher's own vtable. Validated reads only.
    Detour* detourFor(const std::shared_ptr<Packet>& packet)
    {
        if (!packet) return nullptr;
        auto* dispatcher = packet->mDispatcher;
        if (!InstanceLocator::isPlausibleObject(dispatcher) ||
            !InstanceLocator::isReadableMemory(dispatcher, 0x10))
            return nullptr;
        const uintptr_t handler = dispatcher->getPacketHandler();
        const auto it = PacketReceiveHook::mDetours.find(handler);
        return it != PacketReceiveHook::mDetours.end() ? it->second.get() : nullptr;
    }
}

void* PacketReceiveHook::onPacketSend(void* _this, void* networkIdentifier, void* netEventCallback, std::shared_ptr<Packet> packet)
{
    // Never mutate the map: an id outside the hooked set has no owner here.
    auto* detour = detourFor(packet);
    if (!detour)
    {
        // Unknown id through an unregistered handler: the packet flows through
        // this callback untouched; log once so new packet types get noticed.
        static std::atomic<bool> warned{ false };
        if (!warned.exchange(true) && packet &&
            InstanceLocator::isPlausibleObject(packet.get()))
        {
            spdlog::warn(
                "packet receive: no detour owner for id 0x{:X}; passing packet through unhooked",
                static_cast<unsigned>(packet->getId()));
        }
        return nullptr;
    }
    auto ofunc = detour->getOriginal<&PacketReceiveHook::onPacketSend>();

    NetworkIdentifier = networkIdentifier;

    // Every hop below reads members whose offsets may be unresolved; any doubt
    // means "this callback does nothing", the packet flows through untouched.
    auto* clientInstance = ClientInstance::get();
    auto* sim = clientInstance ? clientInstance->getMinecraftSim() : nullptr;
    if (!InstanceLocator::isPlausibleObject(sim))
        return ofunc(_this, networkIdentifier, netEventCallback, packet);

    auto* session = sim->getGameSession();
    if (!InstanceLocator::isPlausibleObject(session))
        return ofunc(_this, networkIdentifier, netEventCallback, packet);

    auto actualEventCallback = session->getEventCallback();
    if (!InstanceLocator::isReadableMemory(actualEventCallback, 1) ||
        actualEventCallback != netEventCallback) {
        return ofunc(_this, networkIdentifier, netEventCallback, packet);
    }

    auto holder = nes::make_holder<PacketInEvent>(packet, networkIdentifier, netEventCallback);
    gFeatureManager->mDispatcher->trigger(holder);
    if (holder->isCancelled()) return nullptr;

    return ofunc(_this, networkIdentifier, netEventCallback, packet);
}

void PacketReceiveHook::handlePacket(std::shared_ptr<Packet> packet)
{
    if (!NetworkIdentifier) return;
    auto* clientInstance = ClientInstance::get();
    if (!InstanceLocator::isPlausibleObject(clientInstance)) return;
    auto* sim = clientInstance->getMinecraftSim();
    if (!InstanceLocator::isPlausibleObject(sim)) return;
    auto* session = sim->getGameSession();
    if (!InstanceLocator::isPlausibleObject(session)) return;
    onPacketSend(packet ? packet->mDispatcher : nullptr, NetworkIdentifier, session->getEventCallback(), packet);
}

void PacketReceiveHook::init()
{
    static bool called = false;
    if (called) return;
    called = true;

    auto packetIds = magic_enum::enum_values<PacketID>();

    uint64_t start = NOW;
    spdlog::info("Hooking {} packets", packetIds.size());

    // Several packet IDs may use the same native handler. MinHook cannot
    // create two detours at one address, so one owner/trampoline per target --
    // and entries are keyed by the TARGET ADDRESS so packet ids outside the
    // scan table (new packet types) resolve safely at runtime instead of
    // creating an empty map entry that deref-crashes in the callback.
    std::unordered_map<uintptr_t, std::shared_ptr<Detour>> byTarget;
    for (int i = 0; i < 0x136; i++) {
        auto id = static_cast<PacketID>(i);
        auto packet = MinecraftPackets::createPacket(id);
        if (!packet || !packet->mDispatcher) continue;

        auto packetFunc = packet->mDispatcher->getPacketHandler();
        if (!packetFunc) continue;
        if (byTarget.find(packetFunc) != byTarget.end())
            continue;

        auto detour = std::make_shared<Detour>("PacketHandlerDispatcherInstance<" + std::string(magic_enum::enum_name<PacketID>(id)) + "Packet,0>::handle", reinterpret_cast<void*>(packetFunc), &onPacketSend, true);
        if (!detour->mCreated || !detour->mOriginalFunc) {
            spdlog::warn("Failed to create packet handler detour: {} (0x{:X})", magic_enum::enum_name<PacketID>(id), i);
            continue;
        }
        byTarget.emplace(packetFunc, detour);
        mDetours[packetFunc] = std::move(detour);
    }

    uint64_t timeTaken = NOW - start;
    spdlog::info("Created {} packet handler detours ({} targets) in {}ms", mDetours.size(), byTarget.size(), timeTaken);
}