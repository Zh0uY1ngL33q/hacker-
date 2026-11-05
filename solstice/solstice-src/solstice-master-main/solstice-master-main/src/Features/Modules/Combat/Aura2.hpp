#pragma once
//
// Aura2 -- a ground-up rewrite of the attack core, written against the runtime
// facts of Minecraft.Windows 1.21.120 instead of against Aura.cpp's accumulated
// patches.
//
// RUNTIME CONTRACTS (explicit, not inherited from the old file):
//  1. BaseTickEvent::mActor IS the local player. Never look it up again through
//     ClientInstance; that extra chain is a failure point, not an identity check.
//  2. The entity list comes only from ActorUtils::getActorList (entt view).
//     Level::getRuntimeActorList resolves to a string-copy routine on 120, and
//     Level::getHitResult() / Actor::canSee() are unusable here. None are called.
//  3. This build renders through GL only. Aura2 registers NO RenderEvent and does
//     no projection at all, so it cannot inherit the broken view matrix.
//  4. An attack is an ItemUseOnActorInventoryTransaction whose mActorId is the
//     ActorRuntimeID (not the unique id). ActorUtils::createAttackTransaction owns
//     that rule; this module only verifies what is about to go on the wire.
//  5. Sending goes only through PacketUtils::queueSend(pkt, false), which drops
//     null packets and lets the outbound hook see them (LegitCriticals depends on it).
//  6. Every per-tick entity access runs inside TryCallWrapper (SEH). An access
//     violation skips exactly one tick and MUST leave a log line.
//  7. One tick = one candidate pass, one target choice, one aim write, one cooldown
//     gate, one send burst. The cooldown is updated AFTER the burst, so Multi mode
//     really is multi-target (the old loop updated it inside the loop and therefore
//     always broke after the first hit).
//
// Header-only on purpose: CMake globs only src/*.cpp. Adding a .cpp reshuffles the
// unity-build groups and previously broke eight unrelated translation units.
//

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <spdlog/spdlog.h>

#include <Features/FeatureManager.hpp>
#include <Features/Events/BaseTickEvent.hpp>
#include <Features/Events/PacketInEvent.hpp>
#include <Features/Events/PacketOutEvent.hpp>
#include <Features/Modules/Misc/Friends.hpp>
#include <Features/Modules/Module.hpp>

#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/Actor/GameMode.hpp>
#include <SDK/Minecraft/InstanceLocator.hpp>
#include <SDK/Minecraft/Inventory/PlayerInventory.hpp>
#include <SDK/Minecraft/Network/PacketID.hpp>
#include <SDK/Minecraft/Network/Packets/InventoryTransactionPacket.hpp>
#include <SDK/Minecraft/Network/Packets/MovePlayerPacket.hpp>
#include <SDK/Minecraft/Network/Packets/PlayerAuthInputPacket.hpp>
#include <SDK/Minecraft/Network/Packets/RemoveActorPacket.hpp>

#include <Utils/GameUtils/ActorUtils.hpp>
#include <Utils/GameUtils/PacketUtils.hpp>
#include <Utils/MemUtils.hpp>
#include <Utils/MiscUtils/MathUtils.hpp>

class Aura2 : public ModuleBase<Aura2>
{
public:
    enum class Mode { Single, Multi, Switch };
    enum class AttackMode { Synched, Earliest };
    enum class RotateMode { None, Normal, Flick };

    EnumSettingT<Mode> mMode = EnumSettingT<Mode>("Mode", "How many targets each burst covers", Mode::Single, "Single", "Multi", "Switch");
    EnumSettingT<AttackMode> mAttackMode = EnumSettingT<AttackMode>("Attack Mode", "Synched sends an inventory transaction; Earliest calls GameMode::attack", AttackMode::Synched, "Synched", "Earliest");
    EnumSettingT<RotateMode> mRotateMode = EnumSettingT<RotateMode>("Rotate Mode", "Aim handling", RotateMode::Normal, "None", "Normal", "Flick");
    NumberSetting mRange = NumberSetting("Range", "Maximum distance to a target", 4.f, 1.f, 8.f, 0.1f);
    NumberSetting mAPS = NumberSetting("APS", "Attacks per second", 10.f, 1.f, 20.f, 0.5f);
    BoolSetting mRandomizeAPS = BoolSetting("Randomize APS", "Pick a random APS each burst", false);
    NumberSetting mAPSMin = NumberSetting("APS Min", "Lower bound for random APS", 8.f, 1.f, 20.f, 0.5f);
    NumberSetting mAPSMax = NumberSetting("APS Max", "Upper bound for random APS", 12.f, 1.f, 20.f, 0.5f);
    BoolSetting mSwing = BoolSetting("Swing", "Play the arm swing once per burst", true);
    BoolSetting mStrafe = BoolSetting("Strafe", "Write the body rotation onto the target (off by default: use MoveFix)", false);
    BoolSetting mSyncPacketRot = BoolSetting("Sync Packet Rotation", "Put the aim into PlayerAuthInput/MovePlayer so the server sees it", true);
    BoolSetting mRequireFrontFacing = BoolSetting("Require Front Facing", "Reject targets behind the camera (geometric only; no Level raycast on this build)", false);
    BoolSetting mAttackFriends = BoolSetting("Attack Friends", "Do not skip the friends list", false);
    BoolSetting mDisableOnDimensionChange = BoolSetting("Auto Disable", "Disable on ChangeDimension", true);
    BoolSetting mDebug = BoolSetting("Debug", "Extra per-event logging", false);

    // Enabled by default on purpose: the SDK contract probe is the whole point of
    // this build, and it must not depend on someone opening the ClickGUI (the
    // HUD fan-out is parked, so the GUI may not be usable yet).
    Aura2() : ModuleBase<Aura2>("Aura2", "Rewritten attack core for 1.21.120", ModuleCategory::Combat, 0, true)
    {
        addSettings(
            &mMode,
            &mAttackMode,
            &mRotateMode,
            &mRange,
            &mAPS,
            &mRandomizeAPS,
            &mAPSMin,
            &mAPSMax,
            &mSwing,
            &mStrafe,
            &mSyncPacketRot,
            &mRequireFrontFacing,
            &mAttackFriends,
            &mDisableOnDimensionChange,
            &mDebug
        );

        VISIBILITY_CONDITION(mAPS, !mRandomizeAPS.mValue);
        VISIBILITY_CONDITION(mAPSMin, mRandomizeAPS.mValue);
        VISIBILITY_CONDITION(mAPSMax, mRandomizeAPS.mValue);

        mNames = {
            {Lowercase, "aura2"},
            {LowercaseSpaced, "aura2"},
            {Normal, "Aura2"},
            {NormalSpaced, "Aura2"}
        };
    }

    std::string getSettingDisplay() override
    {
        return mMode.mValues[mMode.as<int>()];
    }

    // ---------------------------------------------------------------- lifecycle

    void onEnable() override
    {
        if (!gFeatureManager || !gFeatureManager->mDispatcher)
        {
            spdlog::error("[aura2] enable: no event dispatcher; module will stay idle");
            return;
        }
        auto* d = gFeatureManager->mDispatcher.get();
        d->listen<BaseTickEvent, &Aura2::onBaseTickEvent>(this);
        d->listen<PacketOutEvent, &Aura2::onPacketOutEvent>(this);
        d->listen<PacketInEvent, &Aura2::onPacketInEvent>(this);
        resetState();
        spdlog::info("[aura2] enabled | mode=Single synched=1 rotate=Normal strafe=0 syncpkt=1 range={:.1f} aps={:.1f}",
                     mRange.mValue, mAPS.mValue);
    }

    void onDisable() override
    {
        if (gFeatureManager && gFeatureManager->mDispatcher)
        {
            auto* d = gFeatureManager->mDispatcher.get();
            d->deafen<BaseTickEvent, &Aura2::onBaseTickEvent>(this);
            d->deafen<PacketOutEvent, &Aura2::onPacketOutEvent>(this);
            d->deafen<PacketInEvent, &Aura2::onPacketInEvent>(this);
        }
        resetState();
        spdlog::info("[aura2] disabled");
    }

    // ---------------------------------------------------------------- events

    void onBaseTickEvent(BaseTickEvent& event)
    {
        Actor* player = event.mActor;
        if (!player)
        {
            if (shouldLog("noplayer", 2000)) spdlog::warn("[aura2] tick: BaseTickEvent carried a null actor");
            return;
        }

        ++mTicksSinceLog;
        const bool ok = TryCallWrapper([&]() { runTick(player); });
        if (!ok)
        {
            // A dangling actor must never leave a stale aim that gets written into
            // an outbound packet after the tick that failed.
            mPacketRotPending = false;
            if (shouldLog("seh", 2000))
                spdlog::error("[aura2] tick aborted by an access violation; no packets were sent this tick");
        }
    }

    void onPacketOutEvent(PacketOutEvent& event)
    {
        if (!mSyncPacketRot.mValue || !mPacketRotPending || !mAimValid || !event.mPacket) return;

        const PacketID id = event.mPacket->getId();
        if (id == PacketID::PlayerAuthInput)
        {
            auto* pkt = event.getPacket<PlayerAuthInputPacket>();
            if (!pkt) return;
            pkt->mRot = mAimRot;
            pkt->mYHeadRot = mAimRot.y;
        }
        else if (id == PacketID::MovePlayer)
        {
            auto* pkt = event.getPacket<MovePlayerPacket>();
            if (!pkt) return;
            pkt->mRot = mAimRot;
            pkt->mYHeadRot = mAimRot.y;
        }
        else return;

        // Flick means exactly one outbound packet carries the aim.
        if (mRotateMode.mValue == RotateMode::Flick) mPacketRotPending = false;
    }

    void onPacketInEvent(PacketInEvent& event)
    {
        if (!event.mPacket) return;
        const PacketID id = event.mPacket->getId();

        if (id == PacketID::RemoveActor)
        {
            auto pkt = event.getPacket<RemoveActorPacket>();
            if (pkt && mTargetRuntimeId >= 0 && pkt->mRuntimeID == mTargetRuntimeId)
            {
                mTargetRuntimeId = -1;
                mPacketRotPending = false;
                if (mDebug.mValue) spdlog::info("[aura2] target removed from world");
            }
        }
        else if (id == PacketID::ChangeDimension && mDisableOnDimensionChange.mValue)
        {
            setEnabled(false);
        }
    }

private:
    // ---------------------------------------------------------------- state

    struct Candidate
    {
        Actor* mActor = nullptr;
        float mDistance = 0.f;
        int64_t mRuntimeId = -1;
        AABB mBox;
    };

    struct Rejects
    {
        int mSelf = 0;
        int mDangling = 0;
        int mDistance = 0;
        int mFront = 0;
        int mFriendly = 0;
        int mBadRid = 0;
    };

    int64_t mTargetRuntimeId = -1;
    int64_t mLastAttackMs = 0;
    glm::vec2 mAimRot = {0.f, 0.f};
    bool mAimValid = false;
    bool mPacketRotPending = false;
    bool mContractReported = false;
    uint32_t mTicksSinceLog = 0;
    Rejects mRejects{};
    std::unordered_map<Actor*, int64_t> mLastAttackAt;
    std::unordered_map<std::string, int64_t> mLastLogMs;

    static int64_t nowMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    bool shouldLog(const char* key, int64_t intervalMs)
    {
        const int64_t now = nowMs();
        auto it = mLastLogMs.find(key);
        if (it != mLastLogMs.end() && now - it->second < intervalMs) return false;
        mLastLogMs[key] = now;
        return true;
    }

    void resetState()
    {
        mTargetRuntimeId = -1;
        mLastAttackMs = 0;
        mAimRot = {0.f, 0.f};
        mAimValid = false;
        mPacketRotPending = false;
        mContractReported = false;
        mTicksSinceLog = 0;
        mRejects = {};
        mLastAttackAt.clear();
        mLastLogMs.clear();
    }

    static bool finiteVec(const glm::vec3& v)
    {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z)
            && std::fabs(v.x) < 1e7f && std::fabs(v.y) < 1e7f && std::fabs(v.z) < 1e7f;
    }

    static bool plausibleBox(const AABB& box)
    {
        const glm::vec3 d = box.mMax - box.mMin;
        return finiteVec(box.mMin) && finiteVec(box.mMax)
            && d.x >= 0.f && d.y >= 0.f && d.z >= 0.f
            && d.x < 64.f && d.y < 64.f && d.z < 64.f;
    }

    float currentAps() const
    {
        if (!mRandomizeAPS.mValue) return std::max(1.f, mAPS.mValue);
        const float lo = std::max(1.f, std::min(mAPSMin.mValue, mAPSMax.mValue));
        const float hi = std::max(lo, std::max(mAPSMin.mValue, mAPSMax.mValue));
        return MathUtils::random(lo, hi);
    }

    // Friends::isFriend(Actor*) chains into getRawName() and Teams::instance.
    // Both are outside this module's control, and a fault there used to take the
    // whole tick down silently. Isolate it: on any doubt, treat the target as
    // hostile (do not invent a rejection) and say so once.
    bool isFriendly(Actor* actor)
    {
        if (!gFriendManager || !gFriendManager->mEnabled || mAttackFriends.mValue) return false;
        bool result = false;
        const bool ok = TryCallWrapper([&]()
        {
            result = actor->isPlayer() && gFriendManager->isFriend(actor);
        });
        if (!ok)
        {
            if (shouldLog("friendfault", 2000))
                spdlog::warn("[aura2] friend/team check faulted; treating target as hostile");
            return false;
        }
        return result;
    }

    bool frontFacing(Actor* player, const AABB& box) const
    {
        // Level raycasting is unavailable on this build. This is a purely geometric
        // "in front of the camera" check; unknown values deliberately let the target
        // through rather than guessing a rejection.
        auto* rot = player->getActorRotationComponent();
        auto* pos = player->getPos();
        if (!rot || !pos || !finiteVec(*pos)) return true;
        const glm::vec2 want = MathUtils::getRots(*pos, box);
        if (!std::isfinite(want.x) || !std::isfinite(want.y)) return true;
        return std::fabs(MathUtils::wrap(want.y - rot->mYaw, -180.f, 180.f)) <= 95.f;
    }

    // One-time shape validation of every shared SDK object this module touches.
    // Its output is the first thing to read after a build: it separates "the module
    // never ran" from "a getter returned something that is not the object".
    void probeContracts(Actor* player)
    {
        if (mContractReported) return;
        mContractReported = true;

        const bool readable = InstanceLocator::isReadableMemory(player, 0x10);
        auto* pos = player->getPos();
        const bool posOk = pos && finiteVec(*pos);
        auto* supplies = player->getSupplies();
        const bool suppliesOk = supplies != nullptr;
        const bool slotOk = suppliesOk && supplies->mSelectedSlot >= 0 && supplies->mSelectedSlot < 36;
        auto* container = suppliesOk ? supplies->getContainer() : nullptr;
        const bool containerOk = container != nullptr;
        auto* rot = player->getActorRotationComponent();
        const bool rotOk = rot && std::isfinite(rot->mPitch) && std::isfinite(rot->mYaw);
        const int64_t rid = player->getRuntimeID();

        spdlog::info("[aura2] contract readable={} pos={} supplies={} slot={} container={} rot={} rid={}({})",
                     readable, posOk, suppliesOk, slotOk, containerOk, rotOk, rid > 0, rid);
    }

    // ---------------------------------------------------------------- stage 1: input + predicates

    std::size_t collectCandidates(Actor* player, std::vector<Candidate>& out)
    {
        const std::vector<Actor*> actors = ActorUtils::getActorList(false, true);
        const float range = std::max(0.5f, mRange.mValue);

        for (Actor* actor : actors)
        {
            if (!actor) { ++mRejects.mDangling; continue; }
            if (actor == player) { ++mRejects.mSelf; continue; }

            // getActorList already rejected uncommitted pages; distanceTo is the
            // first real dereference. Anything non-finite means a torn-down actor.
            const float dist = actor->distanceTo(player);
            if (!std::isfinite(dist) || dist < 0.f) { ++mRejects.mDangling; continue; }
            if (dist > range) { ++mRejects.mDistance; continue; }

            if (isFriendly(actor))
            {
                ++mRejects.mFriendly;
                continue;
            }

            const AABB box = actor->getAABB();
            if (!plausibleBox(box)) { ++mRejects.mDangling; continue; }

            if (mRequireFrontFacing.mValue && !frontFacing(player, box))
            {
                ++mRejects.mFront;
                continue;
            }

            const int64_t rid = actor->getRuntimeID();
            // A non-positive runtime id is a warning, not a hard reject: the
            // transaction builder falls back to the unique id and reports it.
            if (rid <= 0) ++mRejects.mBadRid;

            out.push_back({actor, dist, rid, box});
        }

        return actors.size();
    }

    // ---------------------------------------------------------------- stage 2: one target, one aim

    void chooseAndAim(Actor* player, const std::vector<Candidate>& candidates)
    {
        mAimValid = false;
        mPacketRotPending = false;
        mTargetRuntimeId = -1;
        if (candidates.empty()) return;

        const Candidate& chosen = candidates.front();
        mTargetRuntimeId = chosen.mRuntimeId;
        if (mRotateMode.mValue == RotateMode::None) return;

        auto* pos = player->getPos();
        if (!pos || !finiteVec(*pos))
        {
            if (shouldLog("nopos", 2000)) spdlog::warn("[aura2] aim: local position unavailable");
            return;
        }

        mAimRot = MathUtils::getRots(*pos, chosen.mBox);
        mAimValid = std::isfinite(mAimRot.x) && std::isfinite(mAimRot.y);
        if (!mAimValid) return;

        // Body rotation is written ONCE per tick, from the same box that the packet
        // is aimed at. Default off: MoveFix owns movement correction instead.
        if (mStrafe.mValue)
        {
            auto* rot = player->getActorRotationComponent();
            if (rot)
            {
                rot->mPitch = mAimRot.x;
                rot->mYaw = mAimRot.y;
                rot->mOldPitch = mAimRot.x;
                rot->mOldYaw = mAimRot.y;
            }
        }

        if (mSyncPacketRot.mValue) mPacketRotPending = true;
    }

    // ---------------------------------------------------------------- stage 3: one burst

    int executeAttacks(Actor* player, const std::vector<Candidate>& candidates, int64_t now)
    {
        const std::size_t count = (mMode.mValue == Mode::Multi)
            ? std::min<std::size_t>(candidates.size(), 8u)
            : 1u;

        if (mSwing.mValue) player->swing();

        int sent = 0;
        for (std::size_t i = 0; i < count; ++i)
        {
            const Candidate& c = candidates[i];
            bool ok = false;

            if (mAttackMode.mValue == AttackMode::Synched)
            {
                auto pkt = ActorUtils::createAttackTransaction(c.mActor, -1);
                if (!pkt || !pkt->mTransaction)
                {
                    if (shouldLog("txnnull", 1000))
                        spdlog::error("[aura2] transaction could not be built; nothing queued (check the contract line)");
                    continue;
                }

                auto* cit = reinterpret_cast<ItemUseOnActorInventoryTransaction*>(pkt->mTransaction.get());
                if (shouldLog("attack", 1000))
                    spdlog::info("[aura2] attack rid={} wireActorId={} action={} slot={} dist={:.2f} targets={} synched=1",
                                 c.mRuntimeId, cit->mActorId, static_cast<int>(cit->mActionType),
                                 cit->mSlot, c.mDistance, count);

                PacketUtils::queueSend(pkt, false);
                ok = true;
            }
            else
            {
                auto* gameMode = player->getGameMode();
                if (gameMode)
                {
                    gameMode->attack(c.mActor);
                    ok = true;
                    if (shouldLog("attack", 1000))
                        spdlog::info("[aura2] attack rid={} dist={:.2f} targets={} synched=0",
                                     c.mRuntimeId, c.mDistance, count);
                }
                else if (shouldLog("nogamemode", 1000))
                {
                    spdlog::error("[aura2] Earliest mode: GameMode unavailable; nothing sent");
                }
            }

            if (ok)
            {
                ++sent;
                mLastAttackAt[c.mActor] = now;
            }
        }

        // Cooldown is updated after the whole burst. Doing it inside the loop is
        // what made the old Multi mode stop after the first target.
        if (sent > 0) mLastAttackMs = now;
        return sent;
    }

    // ---------------------------------------------------------------- stage 4: output

    void report(std::size_t actorCount, const std::vector<Candidate>& candidates, int sent, int64_t now, int64_t delay, float aps)
    {
        if (!shouldLog("state", 2000)) return;

        const float ticks = static_cast<float>(std::max(1u, mTicksSinceLog));
        const int64_t left = std::max<int64_t>(0, delay - (now - mLastAttackMs));
        spdlog::info("[aura2] state actors={} candidates={} sent={} cooldownLeft={}ms aps={:.1f} range={:.1f} aim={} pktrot={} rid={} | reject/tick self={:.1f} dangling={:.1f} dist={:.1f} front={:.1f} friend={:.1f} badrid={:.1f}",
                     actorCount, candidates.size(), sent, left, aps, mRange.mValue,
                     mAimValid, mPacketRotPending, mTargetRuntimeId,
                     mRejects.mSelf / ticks, mRejects.mDangling / ticks, mRejects.mDistance / ticks,
                     mRejects.mFront / ticks, mRejects.mFriendly / ticks, mRejects.mBadRid / ticks);

        mRejects = {};
        mTicksSinceLog = 0;
    }

    // ---------------------------------------------------------------- tick pipeline

    void runTick(Actor* player)
    {
        const int64_t now = nowMs();
        probeContracts(player);

        const float aps = currentAps();
        const int64_t delay = static_cast<int64_t>(1000.f / aps);
        const bool cooldownReady = (now - mLastAttackMs) >= delay;

        std::vector<Candidate> candidates;
        candidates.reserve(16);
        const std::size_t actorCount = collectCandidates(player, candidates);

        if (mMode.mValue == Mode::Switch)
        {
            std::sort(candidates.begin(), candidates.end(), [&](const Candidate& a, const Candidate& b)
            {
                return mLastAttackAt[a.mActor] < mLastAttackAt[b.mActor];
            });
        }
        else
        {
            std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b)
            {
                return a.mDistance < b.mDistance;
            });
        }

        chooseAndAim(player, candidates);

        int sent = 0;
        if (cooldownReady && !candidates.empty())
            sent = executeAttacks(player, candidates, now);

        report(actorCount, candidates, sent, now, delay, aps);
    }
};
