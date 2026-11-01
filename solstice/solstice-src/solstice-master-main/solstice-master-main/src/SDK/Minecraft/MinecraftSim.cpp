//
// Created by vastrakai on 6/25/2024.
//

#include "MinecraftSim.hpp"

#include <libhat/Access.hpp>
#include <SDK/OffsetProvider.hpp>

// MinecraftSim_mGameSim / mRenderSim do not resolve on 1.21.120, so both offsets
// are 0. hat::member_at() at offset 0 reads the first qword of the object, which
// is its vtable pointer: a non-null pointer to a function table, so every caller
// happily carries on and writes into it (setSimTimer does exactly that). Null is
// the only answer that makes the failure visible to the caller.
Simulation* MinecraftSim::getGameSim()
{
    const int offset = OffsetProvider::MinecraftSim_mGameSim;
    if (offset <= 0)
        return nullptr;
    return hat::member_at<Simulation*>(this, offset);
}

Simulation* MinecraftSim::getRenderSim()
{
    const int offset = OffsetProvider::MinecraftSim_mRenderSim;
    if (offset <= 0)
        return nullptr;
    return hat::member_at<Simulation*>(this, offset);
}

void MinecraftSim::setSimTimer(float timer)
{
    if (auto* game = getGameSim()) game->mTimer = timer;
    if (auto* render = getRenderSim()) render->mTimer = timer;
}

void MinecraftSim::setSimSpeed(float speed)
{
    if (auto* game = getGameSim()) game->mTimerMultiplier = speed;
    if (auto* render = getRenderSim()) render->mTimerMultiplier = speed;
}

float MinecraftSim::getSimTimer()
{
    auto* game = getGameSim();
    return game ? game->mTimer : 0.f;
}

float MinecraftSim::getSimSpeed()
{
    auto* game = getGameSim();
    return game ? game->mTimerMultiplier : 1.f;
}

GameSession* MinecraftSim::getGameSession()
{
    return hat::member_at<GameSession*>(this, OffsetProvider::MinecraftSim_mGameSession);
}
