//
// Created by alteik on 15/10/2024.
//

#include "ClickTp.hpp"
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/InstanceLocator.hpp>
#include <SDK/Minecraft/World/HitResult.hpp>
#include <SDK/Minecraft/World/Level.hpp>

void ClickTp::onEnable()
{
    auto* instance = ClientInstance::get();
    if (!InstanceLocator::isPlausibleObject(instance)) return;

    auto player = instance->getLocalPlayer();
    if (!player) return;

    auto* level = player->getLevel();
    if (!InstanceLocator::isPlausibleObject(level)) return;

    // getHitResult is SUSPECT-gated on 1.21.120 and yields null; the bare
    // dereference of the result was the +318BD1 crash.
    HitResult* hitResult = level->getHitResult();
    if (!InstanceLocator::isReadableMemory(hitResult, 0x30)) return;

    glm::vec3 newPos = {0, 0, 0};

    if (hitResult->mType == HitType::BLOCK)
    {
        glm::vec3 blockPos = hitResult->mBlockPos;
        newPos = {blockPos.x, blockPos.y + 1.01f + PLAYER_HEIGHT, blockPos.z};

        player->setPosition(newPos);
    }

    setEnabled(false);
}
