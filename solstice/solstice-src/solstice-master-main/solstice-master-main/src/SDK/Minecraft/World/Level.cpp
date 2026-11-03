//
// Created by vastrakai on 7/10/2024.
//

#include "Level.hpp"

#include <libhat.hpp>
#include <libhat/Access.hpp>
#include <cstring>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <SDK/OffsetProvider.hpp>
#include <SDK/Minecraft/InstanceLocator.hpp>
#include <SDK/Minecraft/Actor/SyncedPlayerMovementSettings.hpp>
#include <SDK/Minecraft/World/HitResult.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/Rendering/LevelRenderer.hpp>
#include <Utils/GameUtils/ActorUtils.hpp>

std::unordered_map<mce::UUID, PlayerListEntry>* Level::getPlayerList()
{
    // Slot 427 of Level's vtable (measured from the RTTI vtable at 0x921FA60) is
    // `mov rcx,[rcx+0x918]; jmp <mov rax,rcx; ret>` -- an accessor that returns the
    // member unchanged. So the container is reachable without the virtual call, and
    // the Index signature that never resolved was only ever trying to find it.
    // Level's copy constructor allocates 0x928 bytes, which puts 0x918 on the last
    // pointer-sized member, consistent with a container pointer.
    constexpr int kPlayerListOffset = 0x918;

    auto* map = hat::member_at<std::unordered_map<mce::UUID, PlayerListEntry>*>(this, kPlayerListOffset);
    if (!InstanceLocator::isReadableMemory(map, 0x40))
        return nullptr;

    // It is only "the player list" by inference, so the container is validated
    // before it is handed out. MSVC's unordered_map opens with the node-list head
    // and its size: either a non-readable head or an absurd size means this member
    // is something else, and answering null keeps the social modules idle instead
    // of walking random memory.
    const auto* words = reinterpret_cast<const uintptr_t*>(map);
    const size_t size = static_cast<size_t>(words[1]);
    if (size > 100000)
        return nullptr;
    if (words[0] != 0 &&
        !InstanceLocator::isReadableMemory(reinterpret_cast<const void*>(words[0]), 0x10))
        return nullptr;

    // One line, once: if the guess is wrong the first words say what the member
    // actually holds, which is cheaper than another round of blind searching.
    static bool logged = false;
    if (!logged)
    {
        logged = true;
        spdlog::info("[level] playerList +0x{:X}: head={:X} size={} [{:X} {:X}]",
                     kPlayerListOffset, words[0], size, words[2], words[3]);
    }
    return map;
}

namespace {

// Crosshair range. Vanilla survival reach is a little over 5 blocks for blocks
// and 6 for entities; one number is enough here because the caller decides what
// to do with the target.
constexpr float kHitRayLength = 6.0f;

// Entry distance of a ray against an axis-aligned box, or false when it misses.
// A component of the direction that is exactly zero would divide by zero, so it
// is nudged; the resulting t stays finite because the box is finite too.
bool rayAgainstBox(const glm::vec3& origin, const glm::vec3& dir,
                   const glm::vec3& boxMin, const glm::vec3& boxMax,
                   float maxDistance, float& distance)
{
    float tMin = 0.f;
    float tMax = maxDistance;
    for (int axis = 0; axis < 3; ++axis)
    {
        const float d = std::fabs(dir[axis]) < 1e-6f ? (dir[axis] < 0.f ? -1e-6f : 1e-6f) : dir[axis];
        float t1 = (boxMin[axis] - origin[axis]) / d;
        float t2 = (boxMax[axis] - origin[axis]) / d;
        if (t1 > t2) std::swap(t1, t2);
        tMin = std::max(tMin, t1);
        tMax = std::min(tMax, t2);
        if (tMin > tMax) return false;
    }
    distance = tMin;
    return true;
}

// One instance, overwritten per call: every caller reads fields immediately and
// none of them keep the pointer. Returning null when the ray hits nothing keeps
// the "no target" answer honest instead of handing back a stale hit.
HitResult sRaycastHit{};

HitResult* raycastHitResult()
{
    auto* client = ClientInstance::get();
    if (!client) return nullptr;
    auto* player = client->getLocalPlayer();
    if (!player) return nullptr;

    glm::vec3 origin{};
    bool haveOrigin = false;
    if (auto* renderer = client->getLevelRenderer())
        if (auto* rendererPlayer = renderer->getRendererPlayer())
            if (auto* camera = rendererPlayer->getCameraPos())
            {
                origin = *camera;
                haveOrigin = true;
            }

    if (!haveOrigin)
    {
        auto* renderPosition = player->getRenderPositionComponent();
        if (!renderPosition) return nullptr;
        origin = renderPosition->mPosition + PLAYER_HEIGHT_VEC;
    }

    auto* rotation = player->getActorRotationComponent();
    if (!rotation) return nullptr;

    // Same basis the projection uses: yaw 0 faces +Z, positive pitch looks down.
    constexpr float kDeg2Rad = 3.14159265358979323846f / 180.f;
    const float yaw = rotation->mYaw * kDeg2Rad;
    const float pitch = rotation->mPitch * kDeg2Rad;
    const float cp = std::cos(pitch);
    const float sp = std::sin(pitch);
    const glm::vec3 direction{ -std::sin(yaw) * cp, -sp, std::cos(yaw) * cp };

    float nearest = kHitRayLength;
    Actor* target = nullptr;
    for (auto* actor : ActorUtils::getActorList(false, true))
    {
        if (!actor || actor == player) continue;
        const AABB box = actor->getAABB();
        float distance = 0.f;
        if (!rayAgainstBox(origin, direction, box.mMin, box.mMax, nearest, distance)) continue;
        nearest = distance;
        target = actor;
    }

    if (!target) return nullptr;

    sRaycastHit = HitResult{};
    sRaycastHit.mStartPos = origin;
    sRaycastHit.mRayDir = direction;
    sRaycastHit.mType = HitType::ENTITY;
    sRaycastHit.mPos = origin + direction * nearest;
    sRaycastHit.mBlockPos = glm::ivec3(glm::floor(sRaycastHit.mPos));
    sRaycastHit.mEntity.id = target->mContext.mEntityId;
    return &sRaycastHit;
}

} // namespace

HitResult* Level::getHitResult()
{
    static auto vIndex = OffsetProvider::Level_getHitResult;
    // The vtable index is unusable on 1.21.120 (the signature is ambiguous and
    // resolves negative), so every call used to answer null. Ten modules read the
    // crosshair target through here; TriggerBot, SkinStealer and MidclickAction
    // dereference the result without checking, which is where the null crash came
    // from, and the others silently did nothing.
    //
    // The accessor is only ever a raycast, so perform it: cast from the camera
    // along the view direction and report the nearest actor box in the same
    // HitResult layout. Block hits are deliberately not synthesised -- reading the
    // level's voxels needs a BlockSource this SDK does not have on this build --
    // so a ray that hits no entity still returns null.
    if (vIndex >= 2 && vIndex < 4096)
        if (auto* hit = MemUtils::callVirtualFunc<HitResult*>(vIndex, this))
            return hit;
    return raycastHitResult();
}

SyncedPlayerMovementSettings* Level::getPlayerMovementSettings()
{
    static auto vIndex = OffsetProvider::Level_getPlayerMovementSettings;
    return MemUtils::callVirtualFunc<SyncedPlayerMovementSettings*>(vIndex, this);
}

bool Level::isRuntimeActorListAvailable()
{
    const auto func = SigManager::Level_getRuntimeActorList;
    if (!func) return false;

    // The 1.21.120 match copies strings with stride 0x20, not Actor*.
    constexpr unsigned char stringStride[] = {0x48, 0x83, 0x47, 0x08, 0x20};
    const auto* strideInstruction = reinterpret_cast<const void*>(func + 0x58);
    if (!InstanceLocator::isReadableMemory(strideInstruction, sizeof(stringStride)) ||
        std::memcmp(strideInstruction, stringStride, sizeof(stringStride)) == 0) {
        static std::atomic_flag warned = ATOMIC_FLAG_INIT;
        if (!warned.test_and_set())
            spdlog::warn("Level::getRuntimeActorList unavailable: unverified or incompatible target");
        return false;
    }
    return true;
}

std::vector<Actor*> Level::getRuntimeActorList()
{
    std::vector<Actor*> actors;
    if (!isRuntimeActorListAvailable()) return actors;
    MemUtils::callFastcall<void>(SigManager::Level_getRuntimeActorList, this, &actors);
    return actors;
}

LevelData* Level::getLevelData()
{
    static auto vIndex = OffsetProvider::Level_getLevelData;
    // Near-miss candidate in 1.21.120 was a shared_ptr release sequence, so the
    // offset stays unresolved; a virtual call through slot 0 is a wild jump.
    if (vIndex < 2 || vIndex >= 4096)
        return nullptr;
    return MemUtils::callVirtualFunc<LevelData*>(vIndex, this);
}

class BlockPalette* Level::getBlockPalette()
{
    static auto vIndex = OffsetProvider::Level_getBlockPalette;
    return MemUtils::callVirtualFunc<class BlockPalette*>(vIndex, this);
}
