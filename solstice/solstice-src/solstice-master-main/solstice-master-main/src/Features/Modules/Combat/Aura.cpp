//
// Created by vastrakai on 7/8/2024.
//

#include "Aura.hpp"
#include <atomic>

#include <Features/FeatureManager.hpp>
#include <Features/Events/BaseTickEvent.hpp>
#include <Features/Events/BobHurtEvent.hpp>
#include <Features/Events/BoneRenderEvent.hpp>
#include <Features/Events/PacketInEvent.hpp>
#include <Features/Events/PacketOutEvent.hpp>
#include <Features/Events/RenderEvent.hpp>
#include <Features/Modules/Misc/Friends.hpp>
#include <Features/Modules/Combat/LegitCriticals.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/InstanceLocator.hpp>
#include <SDK/Minecraft/Options.hpp>
#include <Utils/GameUtils/ActorUtils.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/Actor/GameMode.hpp>
#include <SDK/Minecraft/World/Level.hpp>
#include <SDK/Minecraft/World/HitResult.hpp>
#include <SDK/Minecraft/Inventory/PlayerInventory.hpp>
#include <SDK/Minecraft/Network/LoopbackPacketSender.hpp>
#include <SDK/Minecraft/Network/Packets/MovePlayerPacket.hpp>
#include <SDK/Minecraft/Network/Packets/PlayerAuthInputPacket.hpp>
#include <SDK/Minecraft/Network/Packets/RemoveActorPacket.hpp>
#include <SDK/Minecraft/Rendering/GuiData.hpp>

namespace {
    // The third-person switch is a three-link pointer chain (client -> options ->
    // IntOption). Any link can be unavailable on this build, and the old code
    // dereferenced all three both on enable and on every tick with a target.
    void setThirdPerson(int value)
    {
        auto* ci = ClientInstance::get();
        if (!ci) return;
        auto* options = ci->getOptions();
        if (!options || !options->mThirdPerson) return;
        options->mThirdPerson->value = value;
    }
}

int Aura::getSword(Actor* target) {
    auto* ci = ClientInstance::get();
    if (!ci) return -1;
    auto player = ci->getLocalPlayer();
    if (!player) return -1;
    auto supplies = player->getSupplies();
    if (!supplies) return -1;
    auto container = supplies->getContainer();
    if (!container) return -1;

    if (gFriendManager->isFriend(target) && mFistFriends.mValue)
    {
        // Look for a TROPICAL_FISH in the hotbar
        int fishSlot = -1;
        for (int i = 0; i < 36; i++)
        {
            if (mHotbarOnly.mValue && i > 8) break;
            auto item = container->getItem(i);
            if (!item || !item->mItem) continue;
            auto* descriptor = item->getItem();
            if (!descriptor) continue;
            if (descriptor->mItemId == 267)
            {
                fishSlot = i;
                break;
            }
        }

        if (fishSlot != -1)
        {
            return fishSlot;
        }

        // Find a empty sot, OR an innert item
        for (int i = 0; i < 36; i++)
        {
            if (mHotbarOnly.mValue && i > 8) break;
            auto item = container->getItem(i);
            if (!item || !item->mItem) return i;
            auto* descriptor = item->getItem();
            if (!descriptor || descriptor->mItemId == 0) return i;
        }

        for (int i = 0; i < 36; i++)
        {
            if (mHotbarOnly.mValue && i > 8) break;
            auto item = container->getItem(i);
            if (!item) continue;
            if (item->mItem && !ItemUtils::hasItemType(item))
            {
                return i;
            }
        }

        return supplies->mSelectedSlot;
    }

    int bestSword = ItemUtils::getBestItem(SItemType::Sword, mHotbarOnly.mValue);

    if (shouldUseFireSword(target))
    {
        return ItemUtils::getFireSword(mHotbarOnly.mValue);
    }

    return bestSword;
}

bool Aura::shouldUseFireSword(Actor* target)
{
#ifdef __PRIVATE_BUILD__
    // Called on every attack even though it is off by default. Before, it walked
    // the inventory (four unchecked member reads) just to reach the final
    // `return false` -- one missing component aborted the whole tick inside the
    // SEH wrapper. Bail out before touching the inventory instead.
    if (!mAutoFireSword.mValue || !target) return false;
    auto* ci = ClientInstance::get();
    if (!ci) return false;
    auto* player = ci->getLocalPlayer();
    if (!player) return false;
    auto* supplies = player->getSupplies();
    if (!supplies || !supplies->getContainer()) return false;

    int fireSw = ItemUtils::getFireSword(mHotbarOnly.mValue);
    if (fireSw != -1 && !target->isOnFire())
    {
        return true;
    }
    return false;
#else
    return false;
#endif
}

void Aura::onEnable()
{
    gFeatureManager->mDispatcher->listen<BaseTickEvent, &Aura::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->listen<PacketOutEvent, &Aura::onPacketOutEvent>(this);
    gFeatureManager->mDispatcher->listen<PacketInEvent, &Aura::onPacketInEvent>(this);
    gFeatureManager->mDispatcher->listen<RenderEvent, &Aura::onRenderEvent>(this);
    gFeatureManager->mDispatcher->listen<BobHurtEvent, &Aura::onBobHurtEvent, nes::event_priority::FIRST>(this);
    gFeatureManager->mDispatcher->listen<BoneRenderEvent, &Aura::onBoneRenderEvent, nes::event_priority::FIRST>(this);

    if (mThirdPerson.mValue && !mThirdPersonOnlyOnAttack.mValue) setThirdPerson(1);
}

bool chargingBow = false;

void Aura::onDisable()
{
    gFeatureManager->mDispatcher->deafen<BaseTickEvent, &Aura::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->deafen<PacketOutEvent, &Aura::onPacketOutEvent>(this);
    gFeatureManager->mDispatcher->deafen<PacketInEvent, &Aura::onPacketInEvent>(this);
    gFeatureManager->mDispatcher->deafen<RenderEvent, &Aura::onRenderEvent>(this);
    gFeatureManager->mDispatcher->deafen<BobHurtEvent, &Aura::onBobHurtEvent>(this);
    // onEnable also registers BoneRenderEvent; leaking that listener kept
    // onBoneRenderEvent running after the module was disabled.
    gFeatureManager->mDispatcher->deafen<BoneRenderEvent, &Aura::onBoneRenderEvent>(this);
    sHasTarget = false;
    sTarget = nullptr;
    mRotating = false;

    if (mThirdPerson.mValue && !mThirdPersonOnlyOnAttack.mValue) setThirdPerson(0);
}

void Aura::rotate(Actor* target)
{
    if (mRotateMode.mValue == RotateMode::None) return;
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;

    mTargetedAABB = target->getAABB();
    mRotating = true;

    // Strafe turns the body onto the target, and it has to happen here, once per
    // tick, not in the render handler: rendering runs several times per tick and
    // every write there is undone by the input update, which is exactly the head
    // twitch that showed up once RenderEvent started being dispatched on the GL
    // path.
    if (!mStrafe.mValue) return;

    auto* pos = player->getPos();
    auto* rot = player->getActorRotationComponent();
    if (!pos || !rot) return;

    const glm::vec2 rots = MathUtils::getRots(*pos, mTargetedAABB);
    rot->mPitch = rots.x;
    rot->mYaw = rots.y;
    rot->mOldPitch = rots.x;
    rot->mOldYaw = rots.y;
}

void Aura::shootBow(Actor* target)
{
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;

#ifdef __PRIVATE_BUILD__
    if (!mAutoBow.mValue) return;
#else
    return;
#endif

    auto* supplies = player->getSupplies();
    auto* container = supplies ? supplies->getContainer() : nullptr;
    if (!container) return;

    int bowSlot = -1;
    int arrowSlot = -1;
    for (int i = 0; i < 36; i++)
    {
        auto item = container->getItem(i);
        if (!item || !item->mItem) continue;
        auto* descriptor = item->getItem();
        if (!descriptor) continue;
        if (descriptor->mName.contains("bow"))
        {
            bowSlot = i;
        }
        if (descriptor->mName.contains("arrow"))
        {
            arrowSlot = i;
        }
        if (bowSlot != -1 && arrowSlot != -1) break;
    }

    if (mHotbarOnly.mValue && bowSlot > 8) return;

    if (bowSlot == -1 || arrowSlot == -1) return;

    static int useTicks = 0;
    constexpr int maxUseTicks = 17;

    if (useTicks == 0)
    {
        spdlog::info("Starting to use bow");
        container->startUsingItem(bowSlot);
        chargingBow = true;
        useTicks++;
    }
    else if (useTicks < maxUseTicks)
    {
        useTicks++;
    }
    else if (useTicks >= maxUseTicks)
    {
        spdlog::info("Releasing bow");
        rotate(target);
        container->releaseUsingItem(bowSlot);
        chargingBow = false;
        useTicks = 0;
    }
}

void Aura::throwProjectiles(Actor* target)
{
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;
    auto* supplies = player->getSupplies();
    auto* container = supplies ? supplies->getContainer() : nullptr;
    if (!container) return;

    static uint64_t lastThrow = 0;
    int64_t throwDelay = mThrowDelay.mValue * 50.f;

    if (NOW - lastThrow < throwDelay) return;

    int snowballSlot = -1;
    if (mThrowProjectiles.mValue)
        for (int i = 0; i < 36; i++)
        {
            auto item = container->getItem(i);
            if (!item || !item->mItem) continue;
            auto* descriptor = item->getItem();
            if (!descriptor) continue;
            if (descriptor->mName.contains("snowball"))
            {
                snowballSlot = i;
                break;
            }
        }

    if (mHotbarOnly.mValue && snowballSlot > 8) return;

    if (snowballSlot == -1)
    {
        return;
    }

    int oldSlot = supplies->mSelectedSlot;
    supplies->mSelectedSlot = snowballSlot;
    auto* gameMode = player->getGameMode();
    auto* heldStack = container->getItem(snowballSlot);
    if (gameMode && heldStack) gameMode->baseUseItem(heldStack);
    supplies->mSelectedSlot = oldSlot;

    lastThrow = NOW;

}

float EaseInOutExpo(float pct)
{
    if (pct < 0.5f) {
        return (pow(2.f, 16.f * pct) - 1.f) / 510.f;
    }
    else {
        return 1.f - 0.5f * pow(2.f, -16.f * (pct - 0.5f));
    }
}

void Aura::onRenderEvent(RenderEvent& event)
{
    if (mAPSMin.mValue < 0) mAPSMin.mValue = 0;
    if (mAPSMax.mValue < mAPSMin.mValue + 1) mAPSMax.mValue = mAPSMin.mValue + 1;

    // Strafe used to write the body rotation here, once per rendered frame. The
    // input update owns that field on the tick, so every frame overwrote what the
    // tick had just written and the head visibly twitched. Aura::rotate applies it
    // once per tick now, from the same target box.

    if (mVisuals.mValue) {
        auto* ci = ClientInstance::get();
        if (!ci) return;
        auto player = ci->getLocalPlayer();
        if (!player) return;

        Actor* actor = Aura::sTarget;
        // The old code called actor->isPlayer() inside a SEH wrapper and then
        // refused to draw anything that was not a player. Aura targets mobs too,
        // so that condition made the visuals permanently blank in PvE, and a null
        // target produced an exception log every frame.
        if (!actor || !InstanceLocator::isReadableMemory(actor, 0x8))
        {
            Aura::sTarget = nullptr;
            Aura::sHasTarget = false;
            return;
        }

        auto* playerRenderPos = player->getRenderPositionComponent();
        auto* actorRenderPos = actor->getRenderPositionComponent();
        auto* state = actor->getStateVectorComponent();
        auto* shape = player->getAABBShapeComponent();
        // Four component reads, none of which is guaranteed while the target is
        // being torn down; the old code dereferenced all of them.
        if (!playerRenderPos || !actorRenderPos || !state || !shape) return;

        auto playerPos = playerRenderPos->mPosition;
        auto actorPos = actorRenderPos->mPosition;

        glm::vec3 pos = actorPos - glm::vec3(0.f, 1.62f, 0.f);
        glm::vec3 pos2 = state->mPos - glm::vec3(0.f, 1.62f, 0.f);
        glm::vec3 posOld = state->mPosOld - glm::vec3(0.f, 1.62f, 0.f);
        pos = posOld + (pos2 - posOld) * ImGui::GetIO().DeltaTime;

        float hitboxWidth = shape->mWidth;
        float hitboxHeight = shape->mHeight;

        glm::vec3 aabbMin = glm::vec3(pos.x - hitboxWidth / 2, pos.y, pos.z - hitboxWidth / 2);
        glm::vec3 aabbMax = glm::vec3(pos.x + hitboxWidth / 2, pos.y + hitboxHeight, pos.z + hitboxWidth / 2);

        aabbMin = aabbMin - glm::vec3(0.1f, 0.1f, 0.1f);
        aabbMax = aabbMax + glm::vec3(0.1f, 0.1f, 0.1f);

        float distance = glm::distance(playerPos, actorPos) + 2.5f;
        if (distance < 0) distance = 0;

        float scaledSphereSize = 1.0f / distance * 100.0f * mSpheresSizeMultiplier.mValue;
        if (scaledSphereSize < 1.0f) scaledSphereSize = 1.0f;
        if (scaledSphereSize < mSpheresMinSize.mValue) scaledSphereSize = mSpheresMinSize.mValue;

        glm::vec3 bottomOfHitbox = aabbMin;
        glm::vec3 topOfHitbox = aabbMax;
        bottomOfHitbox.x = pos.x;
        bottomOfHitbox.z = pos.z;
        topOfHitbox.x = pos.x;
        topOfHitbox.z = pos.z;
        topOfHitbox.y += 0.1f;

        static float pct = 0.f;
        static bool reversed = false;
        static uint64_t lastTime = NOW;

        float speed = mUpDownSpeed.mValue;
        uint64_t visualTime = 800 / (speed - 0.2);

        if (NOW - lastTime > visualTime) {
            reversed = !reversed;
            lastTime = NOW;
            pct = reversed ? 1.f : 0.f;
        }

        pct += !reversed ? (speed * ImGui::GetIO().DeltaTime) : -(speed * ImGui::GetIO().DeltaTime);
        pct = MathUtils::lerp(0.f, 1.f, pct);
        pos = MathUtils::lerp(bottomOfHitbox, topOfHitbox, EaseInOutExpo(pct));

        auto corrected = RenderUtils::transform.mMatrix;

        glm::vec2 screenPos = {0, 0};

        static float angleOffset = 0.f;
        angleOffset += (mUpDownSpeed.mValue * 30.f) * ImGui::GetIO().DeltaTime;
        float radius = (float)mSpheresRadius.mValue;

        // getGuiData() is a plain member read with no validation, and the projection
        // below dereferenced whatever it returned once per sphere. Hoisted out of the
        // loop so the plausibility check runs once per frame, and bailing here draws
        // nothing instead of crashing.
        auto* clientInstance = ClientInstance::get();
        auto* guiData = clientInstance ? clientInstance->getGuiData() : nullptr;
        if (!guiData || !InstanceLocator::isPlausibleObject(guiData)) return;
        const glm::vec2 displaySize = guiData->mResolution;
        // A wrong-but-readable GuiData would pass the plausibility check and then feed
        // the projection a nonsense resolution; the draw would land nowhere useful and
        // the failure would be invisible. A screen is positive and smaller than 32k.
        if (!(displaySize.x > 0.f && displaySize.y > 0.f &&
              displaySize.x < 32768.f && displaySize.y < 32768.f))
            return;

        for (int i = 0; i < mSpheresAmount.mValue; i++) {
            float angle = (i / (float)mSpheresAmount.mValue) * 360.f;
            angle += angleOffset;
            angle = MathUtils::wrap(angle, -180.f, 180.f);

            float rad = angle * (PI / 180.0f);

            float x = pos.x + radius * cosf(rad);
            float y = pos.y;
            float z = pos.z + radius * sinf(rad);

            glm::vec3 thisPos = { x, y, z };

            if (!corrected.OWorldToScreen(
                    RenderUtils::transform.mOrigin,
                    thisPos, screenPos, MathUtils::fov,
                    displaySize))
                continue;

            ImColor color = ColorUtils::getThemedColor(0);
            ImColor glowColor = color;
            glowColor.Value.w = 0.3f;

            ImGui::GetBackgroundDrawList()->AddCircleFilled(ImVec2(screenPos.x, screenPos.y), scaledSphereSize * 1.5f, glowColor);
            ImGui::GetBackgroundDrawList()->AddCircleFilled(ImVec2(screenPos.x, screenPos.y), scaledSphereSize, color);
        }
    }

}

void Aura::onBaseTickEvent(BaseTickEvent& event)
{
    auto player = event.mActor; // Local player
    if (!player) return;
    // No liveness gate here on purpose. getHealth() answers 0 both for a dead
    // player and for an actor whose AttributesComponent cannot be fetched on this
    // build, and isValid() reaches back through ClientInstance (which can be null
    // during a world change) from OUTSIDE the SEH wrapper below. Either one can
    // silence the whole module -- or crash it -- for a reason unrelated to combat.
    // BaseTickHook already guarantees this actor is the local player; everything
    // downstream is inside the wrapper.

    // Defense: a target may be freed mid-despawn (use-after-free on a still-committed
    // heap page). isValidPtr() / isPlayer() can BOTH pass on such a pointer, then a later
    // deref (sort comparator, distanceTo, getRuntimeID, attack, getSword,
    // findObstructingActor, ...) AV-crashes with 0xC0000005. Wrap the ENTIRE per-tick
    // entity work in SEH so any access violation is caught and the tick is skipped
    // instead of crashing the game. (C++ try/catch cannot catch 0xC0000005; SEH
    // __try/__except can. The __try lives inside TryCallWrapper, so there is no __try
    // in this function body -> no C2712.)
    bool ok = TryCallWrapper([&]() {
        auto supplies = player->getSupplies();

        auto actors = ActorUtils::getActorList(false, true);
        static std::unordered_map<Actor*, int64_t> lastAttacks = {};
        bool isMoving = Keyboard::isUsingMoveKeys();
        int attacksThisTick = 0;

        // Sort actors by lastAttack if mode is switch
        if (mMode.mValue == Mode::Switch)
        {
            std::ranges::sort(actors, [&](Actor* a, Actor* b) -> bool
            {
                return lastAttacks[a] < lastAttacks[b];
            });
        }
        else if (mMode.mValue == Mode::Multi)
        {
            // Sort actors by distance if mode is multi
            std::ranges::sort(actors, [&](Actor* a, Actor* b) -> bool
            {
                return a->distanceTo(player) < b->distanceTo(player);
            });
        }

        static int64_t lastAttack = 0;
        int64_t now = NOW;
        float aps = mAPS.mValue;
        if (mRandomizeAPS.mValue)
        {
            // Validate min and max APS
            if (mAPSMin.mValue < 0) mAPSMin.mValue = 0;
            if (mAPSMax.mValue < mAPSMin.mValue + 1) mAPSMax.mValue = mAPSMin.mValue + 1;

            aps = MathUtils::random(mAPSMin.mValue, mAPSMax.mValue);
        }
        int64_t delay = 1000 / aps;

        bool foundAttackable = false;

        for (auto actor : actors)
        {
            if (actor == player) continue;
            float range = mDynamicRange.mValue && !isMoving ? mDynamicRangeValue.mValue : mRange.mValue;
            if (actor->distanceTo(player) > range) continue;
            if (!mAttackThroughWalls.mValue && !player->canSee(actor)) continue;

            if (actor->isPlayer() && gFriendManager->mEnabled)
            {
                if (gFriendManager->isFriend(actor) && !mFistFriends.mValue)
                {
                    continue;
                }
            }

            // Rotate is deliberately not called here. The old code called it for
            // every candidate in the loop (and again below for Flick), so a tick
            // with several nearby entities wrote the body rotation several times
            // with different targets -- the head twitch. It happens once, at the
            // actor the attack packet is actually aimed at.
            foundAttackable = true;
            sTarget = actor;
            sTargetRuntimeID = actor->getRuntimeID();

            throwProjectiles(actor);
            shootBow(actor);

            if (now - lastAttack < delay) break;

            if (mSwing.mValue)
            {
                if (mSwingDelay.mValue)
                {
                    if (now - mLastSwing >= mSwingDelayValue.mValue * 1000)
                    {
                        player->swing();
                    }
                }
                else player->swing();
            }

            // getSword walks the inventory through getSupplies()->getContainer().
            // With the default SwitchMode::None its result is unused, so calling it
            // on every attack was a needless trip through four unchecked member
            // reads -- and any one of them aborts the whole tick inside the SEH
            // wrapper, silently. Only look for a weapon when something needs it.
            const bool needWeapon = mSwitchMode.mValue != SwitchMode::None || mHotbarOnly.mValue;
            const int bestWeapon = needWeapon ? getSword(actor) : -1;
            auto* slotSupplies = player->getSupplies();
            if (!slotSupplies) break;
            int slot = mHotbarOnly.mValue ? bestWeapon : slotSupplies->mSelectedSlot;

            auto ogActor = actor;
            actor = findObstructingActor(player, actor);

            if (mSwitchMode.mValue == SwitchMode::Full && supplies)
            {
                supplies->mSelectedSlot = bestWeapon;
            }

            if (mRotateMode.mValue != RotateMode::None)
                rotate(actor);

            // LegitCriticals now listens to outbound attack packets itself to open the crit window; no call from this module needed.
            // 1.21.120: Level::getHitResult() offset is ambiguous/SUSPECT; deleted entirely, do not touch.

            if (mAttackMode.mValue == AttackMode::Synched)
            {

                std::shared_ptr<InventoryTransactionPacket> attackTransaction = ActorUtils::createAttackTransaction(actor, mSwitchMode.mValue == SwitchMode::Spoof ? bestWeapon : -1);

                bool shouldUseFire = shouldUseFireSword(ogActor) && mLastTransaction + 200 < NOW;
                bool spoofed = false;
                int oldSlot = mLastSlot;

#ifdef __PRIVATE_BUILD__
                if (mFireSwordSpoof.mValue && shouldUseFire)
                {
                    spoofed = true;
                    auto pkt = PacketUtils::createMobEquipmentPacket(bestWeapon);
                    PacketUtils::queueSend(pkt, false);
                }
#endif

                if (!attackTransaction)
                {
                    static std::atomic<long long> sLastNoPacket{ 0 };
                    long long prev = sLastNoPacket.load(std::memory_order_relaxed);
                    if (now - prev >= 2000 && sLastNoPacket.compare_exchange_strong(prev, now))
                        spdlog::warn("[aura] attack decided but the transaction could not be built; nothing sent");
                }
                else
                {
                    PacketUtils::queueSend(attackTransaction, false);
                    ++attacksThisTick;
                }

                if (spoofed)
                {
                    auto pkt = PacketUtils::createMobEquipmentPacket(oldSlot);
                    PacketUtils::queueSend(pkt, false);
                }
            } else {
                if (!supplies) continue;
                int oldSlot = supplies->mSelectedSlot;
                if (mSwitchMode.mValue == SwitchMode::Spoof)
                {
                    supplies->mSelectedSlot = bestWeapon;
                }

                bool shouldUseFire = shouldUseFireSword(ogActor) && mLastTransaction + 200 < NOW;
                bool spoofed = false;
                int oldPktSlot = mLastSlot;

#ifdef __PRIVATE_BUILD__
                if (mFireSwordSpoof.mValue && shouldUseFire)
                {
                    auto pkt = PacketUtils::createMobEquipmentPacket(bestWeapon);
                    ClientInstance::get()->getPacketSender()->send(pkt.get());
                    spoofed = true;
                }
#endif

                // getGameMode() is a plain member read and can answer a stale or
                // null pointer; calling attack() through it was an unguarded jump.
                auto* gameMode = player->getGameMode();
                if (gameMode)
                {
                    gameMode->attack(actor);
                    ++attacksThisTick;
                }
                supplies->mSelectedSlot = oldSlot;

                if (spoofed)
                {
                    auto pkt = PacketUtils::createMobEquipmentPacket(oldPktSlot);
                    ClientInstance::get()->getPacketSender()->send(pkt.get());
                }
            }

            lastAttack = now;
            lastAttacks[actor] = now;

            // One line a second while it is actually attacking: separates "never
            // decided to attack" from "attacked and the hit did not land", which look
            // identical from outside but have nothing in common.
            {
                static std::atomic<long long> sLastLog{ 0 };
                long long previous = sLastLog.load(std::memory_order_relaxed);
                if (now - previous >= 1000 && sLastLog.compare_exchange_strong(previous, now))
                {
                    spdlog::info("[aura] attack dist={:.2f} synched={} visible={} sent={} actors={} rotating={}",
                                 actor->distanceTo(player),
                                 mAttackMode.mValue == AttackMode::Synched,
                                 player->canSee(actor),
                                 attacksThisTick > 0,
                                 actors.size(),
                                 mRotating);
                }
            }
            if (mMode.mValue == Mode::Single || mMode.mValue == Mode::Switch) break;
        }

        // Always-on heartbeat, even when nothing was attacked. Without this the
        // only possible observation is "no damage", which cannot distinguish a tick
        // that never ran from a filter that rejected every entity.
        {
            static std::atomic<long long> sLastState{ 0 };
            long long prev = sLastState.load(std::memory_order_relaxed);
            if (now - prev >= 2000 && sLastState.compare_exchange_strong(prev, now))
                spdlog::info("[aura] state actors={} attackable={} attacks={} range={:.1f} aps={:.1f} cooldownLeft={} rotating={}",
                             actors.size(), foundAttackable, attacksThisTick,
                             mRange.mValue, aps, delay - (now - lastAttack), mRotating);
        }

        if (!foundAttackable)
        {
            mRotating = false;
            sTarget = nullptr;
        }
        sHasTarget = foundAttackable;

        if (mThirdPerson.mValue && mThirdPersonOnlyOnAttack.mValue && sHasTarget) {
            if (!mIsThirdPerson) {
                setThirdPerson(1);
                mIsThirdPerson = true;
            }
        }
        else if (mThirdPerson.mValue && mThirdPersonOnlyOnAttack.mValue && !sHasTarget) {
            if (mIsThirdPerson) {
                setThirdPerson(0);
                mIsThirdPerson = false;
            }
        }
    });
    // The SEH wrapper used to swallow the exception in complete silence, so an
    // access violation on a dangling actor looked exactly like "aura does nothing".
    // Skipping the tick is still the right reaction, but it must be observable.
    if (!ok)
    {
        static std::atomic<long long> sLastSeh{ 0 };
        const long long now = NOW;
        long long prev = sLastSeh.load(std::memory_order_relaxed);
        if (now - prev >= 2000 && sLastSeh.compare_exchange_strong(prev, now))
            spdlog::error("[aura] tick aborted by an access violation; this tick sent no packets");
    }
}

void Aura::onPacketOutEvent(PacketOutEvent& event)
{
    if (!mRotating || !event.mPacket) return;

    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;

    // Same null read as in onRenderEvent: getPos() answers null while the actor is
    // between ticks, and this is on the packet path so it runs even with no target.
    auto* pos = player->getPos();
    if (!pos) return;

    if (event.mPacket->getId() == PacketID::PlayerAuthInput) {
        auto pkt = event.getPacket<PlayerAuthInputPacket>();
        if (!pkt) return;
        glm::vec2 rots = MathUtils::getRots(*pos, mTargetedAABB);
        pkt->mRot = rots;
        pkt->mYHeadRot = rots.y;
        if (mRotateMode.mValue == RotateMode::Flick) mRotating = false;
    } else if (event.mPacket->getId() == PacketID::MovePlayer) {
        auto pkt = event.getPacket<MovePlayerPacket>();
        if (!pkt) return;
        glm::vec2 rots = MathUtils::getRots(*pos, mTargetedAABB);
        pkt->mRot = rots;
        pkt->mYHeadRot = rots.y;
    } else if (event.mPacket->getId() == PacketID::Animate)
    {
        mLastSwing = NOW;
    } else if (event.mPacket->getId() == PacketID::InventoryTransaction)
    {
        auto pkt = event.getPacket<InventoryTransactionPacket>();
        if (!pkt) return;
        // Game-generated transactions can arrive with no complex transaction
        // attached; reading cit->type on one of those was an access violation in
        // the outbound hook, outside the tick's SEH wrapper.
        auto* cit = pkt->mTransaction.get();
        if (!cit) return;

        if (cit->type == ComplexInventoryTransaction::Type::ItemUseTransaction)
        {
            const auto iut = reinterpret_cast<ItemUseInventoryTransaction*>(cit);
            if (iut->mActionType == ItemUseInventoryTransaction::ActionType::Place)
                mLastTransaction = NOW;
        }

    } else if (event.mPacket->getId() == PacketID::MobEquipment)
    {
        auto pkt = event.getPacket<MobEquipmentPacket>();
        if (!pkt) return;
        mLastSlot = pkt->mSlot;
    }

}

void Aura::onPacketInEvent(PacketInEvent& event)
{
    if (!event.mPacket) return;
    if (event.mPacket->getId() == PacketID::RemoveActor)
    {
        auto packet = event.getPacket<RemoveActorPacket>();
        if (sTarget && sTargetRuntimeID == packet->mRuntimeID)
        {
            spdlog::debug("Active target was removed from world!! This may lead to a crash!");
            sHasTarget = false;
            sTarget = nullptr;
        }
    }

    if (event.mPacket->getId() == PacketID::ChangeDimension)
    {
        if (mDisableOnDimensionChange.mValue)
        {
            this->setEnabled(false);
        }
    }
}

void Aura::onBobHurtEvent(BobHurtEvent& event)
{
    if (sHasTarget)
    {
        event.mDoBlockAnimation = true;
    }
}

void Aura::onBoneRenderEvent(BoneRenderEvent& event)
{
    if (sHasTarget)
    {
        event.mDoBlockAnimation = true;
    }
}

Actor* Aura::findObstructingActor(Actor* player, Actor* target)
{
    if (mBypassMode.mValue == BypassMode::None) return target;

    bool isMoving = Keyboard::isUsingMoveKeys();
    auto actors = ActorUtils::getActorList(false, false);
    std::ranges::sort(actors, [&](Actor* a, Actor* b) -> bool
    {
        return a->distanceTo(player) < b->distanceTo(player);
    });

    if (mBypassMode.mValue == BypassMode::Raycast)
    {
        std::map<Actor*, AABBShapeComponent> actorHitboxes;
        auto* playerPos = player ? player->getPos() : nullptr;
        auto* targetPos = target ? target->getPos() : nullptr;
        // Both actors can be mid-teardown here; reading the position off either one
        // without a check is the same null write that crashed the render path.
        if (!playerPos || !targetPos) return target;
        glm::vec3 rayStart = *playerPos;
        glm::vec3 rayEnd = *targetPos;
        glm::vec3 currentRayPos = rayStart;

        // Check for obstructing actors
        for (auto actor : actors)
        {
            if (actor == player || actor == target) continue;
            float range = mDynamicRange.mValue && !isMoving ? mDynamicRangeValue.mValue : mRange.mValue;
            if (actor->distanceTo(player) > range) continue;

            auto* aabb = actor->getAABBShapeComponent();
            if (!aabb) continue;
            auto hitbox = *aabb;
            actorHitboxes[actor] = hitbox;

            if (MathUtils::rayIntersectsAABB(currentRayPos, rayEnd, hitbox.mMin, hitbox.mMax))
            {
                if (mDebug.mValue)
                {
                    spdlog::info("Found obstructing actor: {}", actor->mEntityIdentifier);
                    ChatUtils::displayClientMessage("Attacking obstructing actor: " + actor->mEntityIdentifier);
                }
                target = actor;
                break;
            }
        }

        return target;
    } else if (mBypassMode.mValue != BypassMode::None)
    {
        auto actors = ActorUtils::getActorList(false, false);
        std::ranges::sort(actors, [&](Actor* a, Actor* b) -> bool
        {
            return a->distanceTo(player) < b->distanceTo(player);
        });

        for (auto actor : actors)
        {
            if (actor == player || actor == target) continue;
            float distance = actor->distanceTo(target);
            if (distance > 3.f) continue;

            std::string id = actor->mEntityIdentifier;
            auto* aabb = actor->getAABBShapeComponent();
            if (!aabb) continue;
            float hitboxWidth = aabb->mWidth;
            float hitboxHeight = aabb->mHeight;

            if (id == "hivecommon:shadow" && distance < 3.f && mBypassMode.mValue == BypassMode::FlareonV2)
            {
                if (hitboxWidth != 0.86f || hitboxHeight != 2.32f) // Identify Correct Shadow
                {
                    continue;
                }

                if (mDebug.mValue)
                {
                    spdlog::info("Found shadow: {}", actor->mEntityIdentifier);
                    ChatUtils::displayClientMessage("Found shadow: " + actor->mEntityIdentifier);
                }


                return actor;
            }
        }

        return target;
    }

    return target;
}