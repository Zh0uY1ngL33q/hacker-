//
// Created by vastrakai on 6/25/2024.
//

#include "Actor.hpp"
#include <atomic>   // std::atomic counter in COMPONENT_GET_FUNC's log throttle

#include <SDK/OffsetProvider.hpp>
#include <SDK/SigManager.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/InstanceLocator.hpp>
#include <SDK/Minecraft/MinecraftSim.hpp>
#include <SDK/Minecraft/Network/LoopbackPacketSender.hpp>
#include <SDK/Minecraft/Network/MinecraftPackets.hpp>
#include <SDK/Minecraft/World/Level.hpp>
#include <Utils/MiscUtils/RenderUtils.hpp>
#include <Utils/Buffer.hpp>
#include "SerializedSkin.hpp"

#include "ActorType.hpp"
#include "Components/ActorTypeComponent.hpp"
#include "Components/RuntimeIDComponent.hpp"
#include "Components/FlagComponent.hpp"
#include "Components/FallDistanceComponent.hpp"
#include "Components/ActorGameTypeComponent.hpp"
#include <SDK/Minecraft/Network/Packets/SetPlayerGameTypePacket.hpp>

#include <cstring>

namespace {
    // Copies a std::string member only when the MSVC layout at that offset is
    // plausible. Several actor fields (local name, entity identifier) are still
    // hard-coded offsets for 1.21.120; reading one at the wrong offset turns the
    // copy into a memcpy from arbitrary memory (IrcClient crashed that way on
    // the first live BaseTickEvent).
    bool copyActorStringAt(const void* owner, int offset, std::string& out)
    {
        if (!owner) return false;
        const auto base = reinterpret_cast<const unsigned char*>(owner) + offset;
        if (!InstanceLocator::isReadableMemory(base, 0x20)) return false;
        size_t size = 0, capacity = 0;
        std::memcpy(&size, base + 0x10, sizeof(size));
        std::memcpy(&capacity, base + 0x18, sizeof(capacity));
        // MSVC x64 std::string: buffer/pointer at +0, size at +0x10, capacity at
        // +0x18; capacities above 0xF mean the buffer is heap allocated.
        if (capacity < size || capacity > 0x10000) return false;
        const char* chars = nullptr;
        if (capacity <= 0xF) chars = reinterpret_cast<const char*>(base);
        else std::memcpy(&chars, base, sizeof(chars));
        if (size != 0 && (!chars || !InstanceLocator::isReadableMemory(chars, size))) return false;
        out.assign(size != 0 ? chars : "", size);
        return true;
    }
}

#define COMPONENT_GET_FUNC(funcName, componentType) \
componentType* Actor::funcName() \
{ \
    if (!isValid()) \
    { \
        /* Not an error. An entt view still hands out entities whose destruction  \
           is in flight, so every caller that walks the actor list hits this a few \
           times a second -- the log filled with 141 identical lines and buried   \
           everything that mattered. Report the first hit, then one in every 512, \
           which keeps the count without the noise. */ \
        static std::atomic<uint32_t> sMisses{0}; \
        const uint32_t misses = ++sMisses; \
        if (misses == 1 || misses % 512 == 0) \
            spdlog::debug("Actor::{}: isValid() failed ({} so far)", #funcName, misses); \
        return nullptr; \
    } \
    auto component = mContext.getComponent<componentType>(); \
    if (!component) \
    { \
        spdlog::debug("Actor::{}: No {} found", #funcName, #componentType); \
    } \
    /*if (!MemUtils::isValidPtr(reinterpret_cast<uintptr_t>(component))) \
    { \
        spdlog::critical("Actor::{}: {} is not a valid ptr", #funcName, #componentType); \
        return nullptr; \
    } \*/\
    return component; \
}

COMPONENT_GET_FUNC(getWalkAnimationComponent, ActorWalkAnimationComponent)
COMPONENT_GET_FUNC(getActorTypeComponent, ActorTypeComponent)
COMPONENT_GET_FUNC(getRenderPositionComponent, RenderPositionComponent)
COMPONENT_GET_FUNC(getStateVectorComponent, StateVectorComponent)
COMPONENT_GET_FUNC(getMoveInputComponent, MoveInputComponent)
COMPONENT_GET_FUNC(getRawMoveInputComponent, RawMoveInputComponent)
COMPONENT_GET_FUNC(getActorRotationComponent, ActorRotationComponent)
COMPONENT_GET_FUNC(getAABBShapeComponent, AABBShapeComponent)
COMPONENT_GET_FUNC(getBlockMovementSlowdownMultiplierComponent, BlockMovementSlowdownMultiplierComponent)
COMPONENT_GET_FUNC(getActorUniqueIDComponent, ActorUniqueIDComponent)
COMPONENT_GET_FUNC(getActorHeadRotationComponent, ActorHeadRotationComponent)
COMPONENT_GET_FUNC(getMobBodyRotationComponent, MobBodyRotationComponent)
COMPONENT_GET_FUNC(getCameraDirectLookComponent, CameraDirectLookComponent)
COMPONENT_GET_FUNC(getMaxAutoStepComponent, MaxAutoStepComponent)
COMPONENT_GET_FUNC(getAttributesComponent, AttributesComponent)
COMPONENT_GET_FUNC(getCameraComponent, CameraComponent)
COMPONENT_GET_FUNC(getActorDataFlagComponent, ActorDataFlagComponent)

void Actor::swing()
{
    MemUtils::callVirtualFunc<void>(OffsetProvider::Actor_swing, this);
}

bool Actor::isDestroying()
{
    return hat::member_at<bool>(this, OffsetProvider::Actor_mDestroying);
}

bool Actor::isSwinging()
{
    return hat::member_at<bool>(this, OffsetProvider::Actor_mSwinging);
}

void Actor::setSwinging(bool swinging)
{
    hat::member_at<bool>(this, OffsetProvider::Actor_mSwinging) = swinging;
}

int Actor::getGameType()
{
    return mContext.getComponent<ActorGameTypeComponent>()->mGameType;
}

void Actor::setGameType(int type)
{
    auto pkt = MinecraftPackets::createPacket<SetPlayerGameTypePacket>();
    pkt->mPlayerGameType = static_cast<GameType>(type);
    PacketUtils::sendToSelf(pkt);
    ClientInstance::get()->getPacketSender()->sendToServer(pkt.get()); // on some servers this can server-side set the gamemode
}

bool Actor::isDebugCameraActive()
{
    // use da flag component
    auto storage = mContext.mRegistry->assure_t<DebugCameraIsActiveComponent>();
    return storage->contains(this->mContext.mEntityId);
}

void Actor::setDebugCameraActive(bool active)
{
    auto storage = mContext.mRegistry->assure_t<DebugCameraIsActiveComponent>();
    bool wasActive = isDebugCameraActive();
    if (active && !wasActive)
    {
        storage->emplace(this->mContext.mEntityId);
    }
    else if (!active && wasActive)
    {
        storage->remove(this->mContext.mEntityId);
    }
}


int Actor::getSwingProgress()
{
    return hat::member_at<int>(this, OffsetProvider::Actor_mSwinging + 0x4);
}

int Actor::getOldSwingProgress()
{
    return hat::member_at<int>(this, OffsetProvider::Actor_mSwinging + 0x8);
}

void Actor::setSwingProgress(int progress)
{
    hat::member_at<int>(this, OffsetProvider::Actor_mSwinging + 0x4) = progress;
}

AABB Actor::getAABB(bool normal)
{
    if (normal)
    {
        return AABB(this->getAABBShapeComponent()->mMin, this->getAABBShapeComponent()->mMax);
    }
    // Get all components
    auto renderPositionComponent = getRenderPositionComponent();
    if (!renderPositionComponent) return AABB();
    auto aabbShapeComponent = getAABBShapeComponent();
    if (!aabbShapeComponent) return AABB();

    glm::vec3 renderPos = renderPositionComponent->mPosition - glm::vec3(0, PLAYER_HEIGHT, 0);
    if (!isPlayer()) renderPos = renderPositionComponent->mPosition;

    glm::vec2 size = { aabbShapeComponent->mWidth, aabbShapeComponent->mHeight };
    auto localPlayer = ClientInstance::get()->getLocalPlayer();
    if (this == localPlayer)
    {
        renderPos = RenderUtils::transform.mPlayerPos - glm::vec3(0, PLAYER_HEIGHT, 0);
    }

    float hitboxWidth = size.x;
    float hitboxHeight = size.y;
    glm::vec3 aabbMin = renderPos - glm::vec3(hitboxWidth / 2, 0, hitboxWidth / 2);
    glm::vec3 aabbMax = renderPos + glm::vec3(hitboxWidth / 2, hitboxHeight, hitboxWidth / 2);
    aabbMin = aabbMin - glm::vec3(0.1f, 0.1f, 0.1f);
    aabbMax = aabbMax + glm::vec3(0.1f, 0.1f, 0.1f);
    return AABB(aabbMin, aabbMax, true);
}

void Actor::setAABB(AABB& aabb)
{
    AABBShapeComponent* aabbShapeComponent = getAABBShapeComponent();
    if (!aabbShapeComponent) return;
    aabbShapeComponent->mMin = aabb.mMin;
    aabbShapeComponent->mMax = aabb.mMax;
}


bool Actor::isPlayer()
{
    auto actorType = getActorTypeComponent();
    if (!actorType) return false;
    return actorType->mType == ActorType::Player;
}

ChunkPos Actor::getChunkPos()
{
    glm::vec3 pos = *getPos();
    pos.x = static_cast<int>(floor(pos.x));
    pos.y = static_cast<int>(floor(pos.y - (float)2.0f));
    pos.z = static_cast<int>(floor(pos.z));
    return ChunkPos(pos);
}

glm::vec3* Actor::getPos()
{
    return &getStateVectorComponent()->mPos;
}

glm::vec3* Actor::getPosPrev()
{
    return &getStateVectorComponent()->mPosOld;
}

GameMode* Actor::getGameMode()
{
    return hat::member_at<GameMode*>(this, OffsetProvider::Actor_mGameMode);
}

DebugCameraComponent* Actor::getDebugCameraComponent()
{
    return mContext.try_get<DebugCameraComponent>();
}

CameraPresetComponent* Actor::getCameraPresetComponent()
{
    return mContext.try_get<CameraPresetComponent>();
}

ContainerManagerModel* Actor::getContainerManagerModel()
{
    // Actor_mContainerManagerModel does not resolve, but the same member is
    // declared a second time as Actor_mContainerModel and its signature does:
    // a single-hit match at 0x5AA5F26 that reads [rcx+0xCE8], and the two small
    // functions around it are `cmp [rcx+0xCE8],0; setne` and
    // `mov rcx,[rcx+0xCE8]; test; mov rax,[rcx]; call [rax+0x18]; cmp al,dl`
    // i.e. has-a-container plus a byte-typed validity test on its vtable slot 3.
    // That is this field, so use it when the primary name is unresolved.
    const int offset = OffsetProvider::Actor_mContainerManagerModel
        ? OffsetProvider::Actor_mContainerManagerModel
        : OffsetProvider::Actor_mContainerModel;
    if (offset <= 0) return nullptr;
    auto* model = hat::member_at<ContainerManagerModel*>(this, offset);
    // Consumers read mContainerType out of this, so a wrong field would be read as
    // a container type rather than crash. Only answer when it looks like an object.
    if (!InstanceLocator::isPlausibleObject(model)) return nullptr;
    return model;
}

JumpControlComponent* Actor::getJumpControlComponent()
{
    static auto func = SigManager::Mob_getJumpControlComponent;
    auto id = mContext.mEntityId;
    return MemUtils::callFastcall<JumpControlComponent*>(func, mContext.mRegistry, &id);

}

MobHurtTimeComponent* Actor::getMobHurtTimeComponent()
{
    // This one alone still read a raw field offset. On 1.21.120 the hurt timer
    // lives in the entt registry, not at Actor+0x478 -- which is why the offset
    // does not resolve and this returned null forever, leaving TargetHUD's hurt
    // flash dead and Anticheat without a value.
    //
    // Every other component here (ActorRotationComponent, RenderPositionComponent,
    // ActorTypeComponent, ...) is fetched from mContext and works on this build,
    // so use the same path instead of inventing a new offset for it. If the type
    // is not registered under this name the answer is still null, exactly as
    // before -- but a wrong answer is impossible.
    if (!isValid()) return nullptr;
    return mContext.getComponent<MobHurtTimeComponent>();
}

SimpleContainer* Actor::getArmorContainer()
{
    // The component itself can be absent (entities without equipment, and every
    // actor while the registry is being torn down), and this used to dereference
    // whatever it got without looking.
    auto* equipment = mContext.getComponent<ActorEquipmentComponent>();
    if (!equipment) return nullptr;
    return equipment->mArmorContainer;
}

PlayerInventory* Actor::getSupplies()
{
    return hat::member_at<PlayerInventory*>(this, OffsetProvider::Actor_mSupplies);
}

Level* Actor::getLevel()
{
    return hat::member_at<Level*>(this, OffsetProvider::Actor_mLevel);
}

SerializedSkin* Actor::getSkin()
{
    return hat::member_at<SerializedSkin*>(this, OffsetProvider::Actor_mSerializedSkin);
}

int64_t Actor::getRuntimeID()
{
    auto runtimeIDComponent = mContext.getComponent<RuntimeIDComponent>();
    return runtimeIDComponent ? runtimeIDComponent->mRuntimeID : -1;
}

void Actor::setPosition(glm::vec3 pos)
{
    static uintptr_t func = SigManager::Actor_setPosition;
    MemUtils::callFastcall<void, void*, glm::vec3*>(func, this, &pos);
}

float Actor::distanceTo(Actor* actor)
{
    glm::vec3* actorPos = actor->getPos();
    if (!actorPos) return -1;
    glm::vec3 closestPoint = getAABB().getClosestPoint(*actorPos);
    return distance(closestPoint, *actorPos);
}

float Actor::distanceTo(const glm::vec3& pos)
{
    glm::vec3 closestPoint = getAABB().getClosestPoint(pos);
    return distance(closestPoint, pos);
}

bool Actor::wasOnGround()
{
    return getFlag<WasOnGroundFlagComponent>();
}

bool Actor::isOnGround()
{
    return getFlag<OnGroundFlagComponent>();
}

void Actor::setOnGround(bool flag)
{
    setFlag<OnGroundFlagComponent>(flag);
}

bool Actor::isCollidingHorizontal()
{
    return getFlag<HorizontalCollisionFlagComponent>();
}

bool Actor::isDead()
{
    return getFlag<IsDeadFlagComponent>();
}

void Actor::jumpFromGround()
{
    auto jumpComponent = getJumpControlComponent();
    bool noJumpDelay = jumpComponent->mNoJumpDelay;
    jumpComponent->mNoJumpDelay = false;
    static auto func = reinterpret_cast<void*>(SigManager::SimulatedPlayer_simulateJump);
    MemUtils::callFastcall<void>(func, this);
    jumpComponent->mNoJumpDelay = noJumpDelay;
}

float Actor::getFallDistance()
{
    return mContext.getComponent<FallDistanceComponent>()->mFallDistance;
}

void Actor::setFallDistance(float distance)
{
    mContext.getComponent<FallDistanceComponent>()->mFallDistance = distance;
}

std::string Actor::getRawName()
{
    auto actorType = getActorTypeComponent();
    if (!actorType)
    {
        spdlog::warn("Actor::getRawName: No ActorTypeComponent found");
        return "Unknown?";
    }
    auto actorUniqueID = getActorUniqueIDComponent();
    if (!actorUniqueID)
    {
        spdlog::warn("Actor::getRawName: No ActorUniqueIDComponent found");
        return "What?";
    }

    if (actorType->mType != ActorType::Player)
    {
        // mEntityIdentifier is a hard-coded offset; fall back to a literal
        // instead of copying whatever lives there.
        std::string identifier;
        return copyActorStringAt(this, OffsetProvider::Actor_mEntityIdentifier, identifier)
            ? identifier : std::string("Mob");
    }

    auto* client = ClientInstance::get();
    auto* player = client ? client->getLocalPlayer() : nullptr;
    uint64_t uniqueId = actorUniqueID->mUniqueID;

    // Level_getPlayerList is unresolved on 1.21.120 and now returns null.
    auto* playerList = (player && player->getLevel()) ? player->getLevel()->getPlayerList() : nullptr;
    if (playerList)
    {
        for (auto& [id, entry] : *playerList)
        {
            if (entry.mId == uniqueId)
            {
                return entry.mName;
            }
        }
    }

    std::string name = getNameTag();
    name = name.substr(0, name.find('\n'));
    name = ColorUtils::removeColorCodes(name);
    return name;
}

namespace {

// Dump every std::string-shaped member of one actor. The name field cannot be
// reached through its signature on this build, and guessing offsets has already
// produced one wrong answer, so the field is located by content instead: the
// local player's own name appears verbatim in the log together with its offset.
void dumpStringMembers(Actor* actor, const char* tag)
{
    auto* base = reinterpret_cast<uint8_t*>(actor);
    spdlog::info("[actor] {} std::string-shaped members of 0x{:X}:", tag,
                 reinterpret_cast<uintptr_t>(actor));
    for (int off = 0x8; off < 0x1400; off += 8)
    {
        auto* p = base + off;
        if (!InstanceLocator::isReadableMemory(p, 0x20)) continue;

        uint64_t size = 0, cap = 0;
        std::memcpy(&size, p + 0x10, 8);
        std::memcpy(&cap, p + 0x18, 8);
        if (size == 0 || size > 64 || cap < size || cap > 0x1000) continue;

        const char* data = nullptr;
        if (cap <= 15)
        {
            data = reinterpret_cast<const char*>(p);
        }
        else
        {
            uintptr_t ptr = 0;
            std::memcpy(&ptr, p, 8);
            if (!InstanceLocator::isReadableMemory(reinterpret_cast<void*>(ptr),
                                                   static_cast<size_t>(size)))
                continue;
            data = reinterpret_cast<const char*>(ptr);
        }

        bool printable = true;
        for (uint64_t i = 0; i < size; ++i)
        {
            const auto c = static_cast<unsigned char>(data[i]);
            if (c < 0x20 || c > 0x7E) { printable = false; break; }
        }
        if (!printable) continue;

        spdlog::info("[actor]   +0x{:X} size={} cap={} \"{}\"", off, size, cap,
                     std::string(data, static_cast<size_t>(size)));
    }
}

}  // namespace

const std::string& Actor::getNameTag()
{
    static auto func = SigManager::Actor_getNameTag;
    static const std::string empty;
    // This signature's refitted pattern begins with four wildcards, and on
    // 1.21.120 the rel32 it reads points into the middle of an unrelated 251-byte
    // function -- 0x29D84E9, inside 0x29D84B0..0x29D85AB. Reading it as a
    // std::string& function meant jumping there with `this` in rcx and taking
    // whatever the surrounding code left in rax. Require an actual function
    // entry; otherwise report that there is no name rather than call into a body.
    static const bool usable = MemUtils::isFunctionEntry(func);
    if (!usable) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            spdlog::warn("Actor::getNameTag: Actor_getNameTag resolved to 0x{:X}, which is not a function entry; names unavailable",
                         static_cast<uintptr_t>(func));
        }

        // One dump, for the local player only: the log line above says the name is
        // unavailable, and without this the only way to find the field is to guess
        // another offset.
        static bool dumped = false;
        if (!dumped)
        {
            if (auto* ci = ClientInstance::get())
            {
                if (ci->getLocalPlayer() == this)
                {
                    dumped = true;
                    dumpStringMembers(this, "local player");
                }
            }
        }
        return empty;
    }
    auto* result = MemUtils::callFastcall<std::string*>(func, this);
    if (!result || !InstanceLocator::isReadableMemory(result, sizeof(std::string))) return empty;
    return *result;
}
void Actor::setNametag(const std::string& name)
{
    static auto func = SigManager::Actor_setNameTag;
    MemUtils::callFastcall<void, void*, std::string>(func, this, name);
}

float Actor::getMaxHealth()
{
    auto health = getAttribute(Health);
    if (!health) return 0;
    return health->mMaximumValue;
}

float Actor::getHealth()
{
    auto health = getAttribute(Health);
    if (!health) return 0;
    return health->mCurrentValue;
}

float Actor::getAbsorption()
{
    auto absorption = getAttribute(Absorption);
    if (!absorption) return 0;
    return absorption->mCurrentValue;
}

float Actor::getMaxAbsorption()
{
    auto absorption = getAttribute(Absorption);
    if (!absorption) return 0;
    return absorption->mMaximumValue;
}

AttributeInstance* Actor::getAttribute(AttributeId id)
{
    return getAttribute(static_cast<int>(id));
}

AttributeInstance* Actor::getAttribute(int id)
{
    auto component = getAttributesComponent();
    if (!component) {
        spdlog::critical("Actor::getAttribute: No AttributesComponent found");
        return nullptr;
    }
    // Directly access the map
    auto& map = component->mBaseAttributeMap.mAttributes;
    auto it = map.find(id);
    if (it != map.end()) {
        return &it->second;
    }
    return nullptr;
}

bool Actor::isOnFire()
{
    return getFlag<OnFireComponent>();
}

std::string Actor::getLocalName()
{
    // Actor_mLocalName is a hard-coded offset that is not verified for
    // 1.21.120. A wrong offset used to memcpy from arbitrary memory and killed
    // IrcClient on its first live BaseTickEvent.
    std::string name;
    if (copyActorStringAt(this, OffsetProvider::Actor_mLocalName, name)) return name;

    // The field cannot be read (or written) here, but the identity is still
    // available: getRawName() ends in Actor_getNameTag, whose signature does
    // resolve, and that is what the IRC client, the copy-name command and the
    // session overlay need. Without this fallback all of them saw an empty name
    // and treated the player as unnamed.
    const std::string fallback = getRawName();
    if (!fallback.empty() && fallback != "Unknown?" && fallback != "What?") {
        return fallback;
    }

    static bool warned = false;
    if (!warned) {
        warned = true;
        spdlog::warn("Actor::getLocalName: Actor_mLocalName (0x{:X}) is not a plausible std::string on this build; returning empty",
            OffsetProvider::Actor_mLocalName);
    }
    return {};
}

void Actor::setLocalName(std::string newLocalName)
{
    std::string current;
    if (!copyActorStringAt(this, OffsetProvider::Actor_mLocalName, current)) return;
    auto& localName = hat::member_at<std::string>(this, OffsetProvider::Actor_mLocalName);
    localName = newLocalName;
}

std::string Actor::getXuid()
{
    return "0";
}

bool Actor::isValid()
{
    if (this == nullptr) return false;
    // Make sure mRegistry is a valid ptr
    if (!mContext.mRegistry) return false;

    auto player = ClientInstance::get()->getLocalPlayer();
    return player->mContext.mRegistry->valid(mContext.mEntityId);
}

bool Actor::canSee(Actor* actor)
{
    // The 1.21.120 pattern for this one matches four places, so what got resolved is
    // one of four functions. If it is not the visibility test it answers false for
    // everything, and Aura's "skip targets you cannot see" makes the whole module
    // do nothing while looking perfectly healthy. Being optimistic when the match is
    // not unique costs an attack through a wall; being pessimistic costs the module.
    static const bool reliable = []
    {
        const auto it = SigManager::mSigMatchCounts.find("Actor_canSee");
        return it != SigManager::mSigMatchCounts.end() && it->second == 1;
    }();

    if (!reliable) return true;
    return MemUtils::callFastcall<bool>(SigManager::Actor_canSee, this, actor);
}
