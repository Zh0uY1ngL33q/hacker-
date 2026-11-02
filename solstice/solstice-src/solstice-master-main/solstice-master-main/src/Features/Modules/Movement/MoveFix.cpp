//
// Created for the 1.21.120 port.
//

#include "MoveFix.hpp"

#include <cmath>
#include <Features/FeatureManager.hpp>
#include <Features/Events/BaseTickEvent.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <Utils/MiscUtils/MathUtils.hpp>

void MoveFix::onEnable()
{
    gFeatureManager->mDispatcher->listen<BaseTickEvent, &MoveFix::onBaseTickEvent>(this);
    mHasLastYaw = false;
}

void MoveFix::onDisable()
{
    gFeatureManager->mDispatcher->deafen<BaseTickEvent, &MoveFix::onBaseTickEvent>(this);
    mHasLastYaw = false;
}

void MoveFix::onBaseTickEvent(BaseTickEvent& event)
{
    auto* player = event.mActor ? event.mActor : ClientInstance::get()->getLocalPlayer();
    if (!player) return;

    auto* rotation = player->getActorRotationComponent();
    auto* input = player->getMoveInputComponent();
    if (!rotation || !input) return;

    // The first tick only records where the aim started: without a previous sample
    // there is no delta, and inventing one would kick the walk on the first tick
    // after enabling.
    if (!mHasLastYaw)
    {
        mLastYaw = rotation->mYaw;
        mHasLastYaw = true;
        return;
    }

    float delta = rotation->mYaw - mLastYaw;
    mLastYaw = rotation->mYaw;

    // Yaw wraps at 180, so a turn across the seam is a small step, not a full one.
    while (delta > 180.f) delta -= 360.f;
    while (delta < -180.f) delta += 360.f;

    if (delta == 0.f) return;

    const float strength = mStrength.mValue;
    if (strength <= 0.f) return;

    // Prefer what the game already built this tick; fall back to the key state when
    // the vector is still zero (the game may rebuild it later in the tick, and a
    // zero vector cannot carry a direction).
    glm::vec2 move = input->mMoveVector;
    if (move.x == 0.f && move.y == 0.f)
        move = MathUtils::getMovement();
    if (move.x == 0.f && move.y == 0.f) return;

    // The game builds the world move direction from the camera, and Aura only
    // rewrites the actor's rotation, so by default the body faces the target while
    // the walk keeps the camera heading -- a straight line along where the player
    // was originally aiming, with the body pointed somewhere else. Vanilla cannot
    // produce that: it keeps facing and walking direction locked together.
    //
    // Rotating the local input by +delta re-locks them: W now means "forward
    // relative to where the body is facing", so the walk follows the aim toward the
    // target instead of the camera.
    //
    // MoveInputComponent packs the vector as (x = strafe, y = forward) -- see
    // MathUtils::getMovement, which ends in ret.x = side; ret.y = forward -- so the
    // two components are rotated in that order, not the other way round.
    const float radians = glm::radians(delta * strength);
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    input->mMoveVector = glm::vec2(move.x * c - move.y * s,
                                   move.y * c + move.x * s);
}
