//
// Created for the 1.21.120 port.
//

#include "MoveFix.hpp"

#include <algorithm>
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

    // Prefer what the game already built this tick; fall back to the key state when
    // the vector is still zero (the game may rebuild it later in the tick, and a
    // zero vector cannot carry a direction).
    glm::vec2 move = input->mMoveVector;
    if (move.x == 0.f && move.y == 0.f)
        move = MathUtils::getMovement();
    if (move.x == 0.f && move.y == 0.f) return;

    // The game builds the world move direction from the camera, while Aura only
    // rewrites the actor's rotation, so by default the body faces the target and the
    // walk keeps the camera heading -- a straight line along the original aim with
    // the body pointed elsewhere. Vanilla cannot produce that: it keeps facing and
    // walking direction locked together.
    //
    // MoveInputComponent packs the vector as (x = strafe, y = forward) -- see
    // MathUtils::getMovement, which ends in ret.x = side; ret.y = forward -- so the
    // two components are rotated in that order, not the other way round.
    const float radians = glm::radians(delta);
    const float c = std::cos(radians);
    const float s = std::sin(radians);

    if (mMode.mValue == Mode::Silent)
    {
        // LiquidBounce's MovementCorrection.SILENT: rotate by the whole delta, then
        // snap to whole -1/0/1 impulses. Rounding is what makes it "silent" -- a
        // small aim change rounds straight back to forward, so the player keeps the
        // line, and only a large one turns into a diagonal. Writing the flags as
        // well matters: the game may rebuild the vector from them later in the tick.
        const float rotatedX = move.x * c - move.y * s;
        const float rotatedY = move.y * c + move.x * s;
        const int side = std::clamp(static_cast<int>(std::lround(rotatedX)), -1, 1);
        const int forwardStep = std::clamp(static_cast<int>(std::lround(rotatedY)), -1, 1);

        input->mForward = forwardStep > 0;
        input->mBackward = forwardStep < 0;
        input->mLeft = side > 0;
        input->mRight = side < 0;

        // A diagonal keeps its unit length; a straight line keeps 1.0 on its axis.
        const float length = (side != 0 && forwardStep != 0) ? 0.70710678f : 1.f;
        input->mMoveVector = glm::vec2(static_cast<float>(side), static_cast<float>(forwardStep)) * length;
        return;
    }

    // Smooth: rotate the local input by delta * strength, which blends the two. At 1
    // the walk is fully re-locked to the body and heads at the target (identical to
    // Aura's Strafe), at 0 it is untouched, and in between it keeps the camera
    // heading with a bend towards the aim.
    const float strength = mStrength.mValue;
    if (strength <= 0.f) return;

    const float smoothed = glm::radians(delta * strength);
    const float sc = std::cos(smoothed);
    const float ss = std::sin(smoothed);
    input->mMoveVector = glm::vec2(move.x * sc - move.y * ss,
                                   move.y * sc + move.x * ss);
}
