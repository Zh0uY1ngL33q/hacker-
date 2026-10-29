#pragma once
//
// Created by vastrakai on 7/10/2024.
//
#include <Utils/MemUtils.hpp>
#include <libhat/Access.hpp>
#include <SDK/OffsetProvider.hpp>
#include <SDK/Minecraft/mce.hpp>
#include <unordered_map>
#include <SDK/Minecraft/Network/Packets/PlayerListPacket.hpp>

class PlayerListEntry {
public:
    uint64_t mId; // This is the ActorUniqueID
    mce::UUID mUuid;
    std::string mName, mXUID, mPlatformOnlineId;
    BuildPlatform mBuildPlatform;

    PlayerListEntry() {
        mId = 0;
        mUuid = mce::UUID();
        mName = "";
        mXUID = "";
        mPlatformOnlineId = "";
        mBuildPlatform = BuildPlatform::Unknown;
    }
};

class LevelData
{
public:
    CLASS_FIELD(uint64_t, mTick, OffsetProvider::LevelData_mTick);

    // LevelData_mTick is SUSPECT in 1.21.120 (-1219654324); CLASS_FIELD would
    // compute member_at with that value and write to a wild address.
    void setTick(uint64_t tick)
    {
        const int off = OffsetProvider::LevelData_mTick;
        if (off <= 0 || off >= 0x8000)
            return;
        hat::member_at<uint64_t>(this, off) = tick;
    }
};

class BlockPalette {
public:
    virtual ~BlockPalette();
    virtual void getPaletteType();
    virtual void appendBlock(Block const&);
    virtual void getBlock(unsigned int const&);
    virtual void assignBlockNetworkId(Block const&, unsigned long);

    // TODO: Correct these fields lol
    /*std::mutex mLegacyBlockStatesWarningMutex;
    std::set<std::pair<int, int>> mLegacyBlockStatesWarningSet;
    std::map<std::string, const BlockLegacy*> mNameLookup;
    std::vector<const Block*> mBlockFromRuntimeId;
    Level* mLevel;*/
    CLASS_FIELD(Level*, mLevel, OffsetProvider::BlockPalette_mLevel);
};

class Level {
public:
    CLASS_FIELD(uintptr_t**, mVfTable, 0x0);
    //CLASS_FIELD(class BlockPalette*, mBlockPalette, 0x1C8);
    ///CLASS_FIELD(std::unordered_map<mce::UUID, PlayerListEntry>, pl, 0x1BC8); // class_field doesn't support std::unordered_map lol

    std::unordered_map<mce::UUID, PlayerListEntry>* getPlayerList();
    class HitResult* getHitResult();
    class SyncedPlayerMovementSettings* getPlayerMovementSettings();
    static bool isRuntimeActorListAvailable();
    std::vector<Actor*> getRuntimeActorList();
    LevelData* getLevelData();
    class BlockPalette* getBlockPalette();
};