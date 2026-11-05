//
// Created by vastrakai on 7/6/2024.
//

#include "ItemUtils.hpp"

#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/Actor/GameMode.hpp>
#include <SDK/Minecraft/Inventory/Item.hpp>
#include <SDK/Minecraft/Inventory/ItemStack.hpp>
#include <SDK/Minecraft/Inventory/PlayerInventory.hpp>
#include <SDK/Minecraft/World/Block.hpp>
#include <SDK/Minecraft/World/BlockLegacy.hpp>
#include <Utils/StringUtils.hpp>
#include <windows.h>

// 1.21.120: the obfuscated build's Block / BlockLegacy / ItemStack class layout
// offsets (Block_mLegacy 0x30, BlockLegacy_mName 0xA0, ItemStack mBlock 0x18) are
// unreliable, so dereferencing legacy->mName / legacy->mayPlaceOn() can AV. These
// helpers read defensively: validate memory is committed+readable before touching
// it, and only treat the region as a name when it is clean printable ASCII.
static BlockLegacy* safeLegacy(Block* block) {
    if (!block) return nullptr;
    BlockLegacy* legacy = nullptr;
    __try { legacy = block->toLegacy(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { legacy = nullptr; }
    if (!legacy) return nullptr;
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(legacy, &mbi, sizeof(mbi)) != sizeof(mbi)) return nullptr;
    if (!(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) return nullptr;
    return legacy;
}

static bool looksLikeMinecraftName(const char* buf, size_t maxlen) {
    static const char prefix[] = "minecraft:";
    size_t len = 0;
    while (len < maxlen && buf[len] != 0) len++;
    if (len < (sizeof(prefix) - 1)) return false;
    if (len == maxlen) return false;          // unterminated
    for (size_t i = 0; i < len; i++) {
        unsigned char c = static_cast<unsigned char>(buf[i]);
        if (c < 0x20 || c > 0x7E) return false;
    }
    for (size_t i = 0; i < (sizeof(prefix) - 1); i++)
        if (buf[i] != prefix[i]) return false;
    return true;
}

static std::string safeBlockName(Block* block) {
    BlockLegacy* legacy = safeLegacy(block);
    if (!legacy) return "";
    // mName is a std::string at offset 0xA0. MSVC std::string is either SSO (16-byte
    // inline buffer) or a heap pointer in the first 8 bytes; handle both.
    const char* namePtr = reinterpret_cast<const char*>(legacy) + 0xA0;
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(namePtr, &mbi, sizeof(mbi)) != sizeof(mbi)) return "";
    if (!(mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) return "";
    char hdr[16] = {};
    memcpy(hdr, namePtr, 16);
    if (looksLikeMinecraftName(hdr, 16)) return std::string(hdr);
    // non-SSO: first 8 bytes are a pointer to the actual character buffer
    uintptr_t bufPtr = *reinterpret_cast<const uintptr_t*>(hdr);
    if (bufPtr >= 0x7FF714E80000ULL && bufPtr < 0x7FF714E80000ULL + 0xC100000ULL) {
        if (VirtualQuery(reinterpret_cast<const void*>(bufPtr), &mbi, sizeof(mbi)) == sizeof(mbi) &&
            (mbi.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) {
            char txt[64] = {};
            memcpy(txt, reinterpret_cast<const char*>(bufPtr), 64);
            if (looksLikeMinecraftName(txt, 64)) return std::string(txt);
        }
    }
    return "";
}

// Calls BlockLegacy::mayPlaceOn() defensively: with the obfuscated layout the
// mLegacy offset may be off, handing us a garbage vtable, so the virtual call is
// wrapped in SEH. Kept in its own function (only POD locals) to avoid MSVC C2712.
static bool safeMayPlaceOn(Block* block, glm::vec3 blockPos) {
    if (!block) return false;
    bool mayPlace = false;
    __try { mayPlace = block->mLegacy->mayPlaceOn(blockPos); }
    __except (EXCEPTION_EXECUTE_HANDLER) { mayPlace = false; }
    return mayPlace;
}

int ItemUtils::getItemValue(ItemStack* item) {
    int value = 0;
    if (!item->mItem) return -1;
    switch (item->getItem()->getItemType())
    {
    case SItemType::Helmet:
    case SItemType::Chestplate:
    case SItemType::Leggings:
    case SItemType::Boots:
        if (item->getItem()->getItemTier() < 4) break;
        value = item->getEnchantValue(Enchant::PROTECTION) + item->getEnchantValue(Enchant::FIRE_PROTECTION);
        break;
    case SItemType::Sword:
        value = item->getEnchantValue(Enchant::SHARPNESS);
        break;
    case SItemType::Pickaxe:
    case SItemType::Axe:
    case SItemType::Shovel:
        value = item->getEnchantValue(Enchant::EFFICIENCY);
        break;
    case SItemType::None:
        break;
    }

    // If the item is armor, add the getArmorValue function
    if (item->getItem()->getItemType() >= SItemType::Helmet && item->getItem()->getItemType() <= SItemType::Boots)
        value += item->getItem()->mProtection;

    // If the item is a weapon, add the getItemTier function
    if (item->getItem()->getItemType() == SItemType::Sword || item->getItem()->getItemType() == SItemType::Pickaxe || item->getItem()->getItemType() == SItemType::Axe || item->getItem()->getItemType() == SItemType::Shovel)
        value += item->getItem()->getItemTier();

    return value;
}

int ItemUtils::getBestItem(SItemType type, bool hotbarOnly)
{
    auto player = ClientInstance::get()->getLocalPlayer();
    auto supplies = player->getSupplies();
    auto container = supplies->getContainer();

    int bestSlot = supplies->mSelectedSlot;
    int bestValue = 0;

    for (int i = 0; i < 36; i++)
    {
        auto item = container->getItem(i);
        if (!item->mItem) continue;

        if (hotbarOnly && i > 8) break;

        if (item->getItem()->getItemType() == type)
        {
            int value = getItemValue(item);
            if (value > bestValue)
            {
                bestValue = value;
                bestSlot = i;
            }
        }
    }

    return bestSlot;
}

bool ItemUtils::hasItemType(ItemStack* item)
{
    if (!item->mItem) return false;
    return item->getItem()->getItemType() != SItemType::None;
}

std::unordered_map<SItemType, int> ItemUtils::getBestItems()
{
    std::unordered_map<SItemType, int> bestItemsResult;
    std::vector<std::pair<SItemType, int>> bestItems;

    // instead, you need to iterate over the inventory once manually.
    auto player = ClientInstance::get()->getLocalPlayer();
    auto supplies = player->getSupplies();
    auto container = supplies->getContainer();

    for (int i = 0; i < 36; i++)
    {
        auto item = container->getItem(i);
        if (!item->mItem) continue;

        switch (item->getItem()->getItemType())
        {
        case SItemType::Helmet:
        case SItemType::Chestplate:
        case SItemType::Leggings:
        case SItemType::Boots:
        case SItemType::Sword:
        case SItemType::Pickaxe:
        case SItemType::Axe:
        case SItemType::Shovel:
            bestItems.emplace_back(item->getItem()->getItemType(), i);
            break;
        }
    }

    for (const auto& [type, slot] : bestItems)
    {
        auto item = container->getItem(slot);
        if (!item->mItem) continue;

        int value = getItemValue(item);

        if (value > getItemValue(container->getItem(bestItemsResult[type])))
        {
            bestItemsResult[type] = slot;
        }
    }

    return bestItemsResult;
}

// Blacklisted block name
constexpr std::array<const char*, 5> blacklistedBlocks = {
    "netherreactor",
    "boombox",
    "lilypad",
    "torch",
    "fence"
};

int ItemUtils::getAllPlaceables(bool hotbarOnly)
{
    auto player = ClientInstance::get()->getLocalPlayer();
    int placeables = 0;

    for (int i = 0; i < 36; i++)
    {
        ItemStack* stack = player->getSupplies()->getContainer()->getItem(i);
        if (!stack->mItem) continue;
        Item* item = stack->getItem();
        if (hotbarOnly && i > 8) continue;
        if (stack->mBlock)
        {
            // If the string contains any of the blacklisted blocks, skip it (compare using StringUtils::containsIgnoreCase
            bool skip = false;

            for (const auto& blacklistedBlock : blacklistedBlocks)
            {
                if (StringUtils::containsIgnoreCase(safeBlockName(stack->mBlock), blacklistedBlock))
                {
                    skip = true;
                    break;
                }
            }
            if (skip) continue;


            placeables += stack->mCount;
        }
    }

    return placeables;
}

int ItemUtils::getFirstPlaceable(bool hotbarOnly)
{
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return -1;
    auto supplies = player->getSupplies();

    for (int i = 0; i < 36; i++)
    {
        ItemStack* stack = supplies->getContainer()->getItem(i);
        if (!stack->mItem) continue;
        Item* item = stack->getItem();
        if (hotbarOnly && i > 8) continue;
        if (stack->mBlock)
        {
            // If the string contains any of the blacklisted blocks, skip it (compare using StringUtils::containsIgnoreCase
            bool skip = false;

            for (const auto& blacklistedBlock : blacklistedBlocks)
            {
                if (StringUtils::containsIgnoreCase(safeBlockName(stack->mBlock), blacklistedBlock))
                {
                    skip = true;
                    break;
                }
            }
            if (skip) continue;

            return i;
        }
    }

    return -1;
}

int ItemUtils::getPlaceableItemOnBlock(glm::vec3 blockPos, bool hotbarOnly, bool prioHighest)
{
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return -1;
    auto supplies = player->getSupplies();

    int slot = -1;
    // slot, count
    std::map<int, int> placeables;
    for (int i = 0; i < 36; i++)
    {
        ItemStack* stack = supplies->getContainer()->getItem(i);
        if (!stack->mItem) continue;
        Item* item = stack->getItem();
        if (hotbarOnly && i > 8) continue;
        if (stack->mBlock)
        {
            // If the string contains any of the blacklisted blocks, skip it (compare using StringUtils::containsIgnoreCase
            bool skip = false;

            for (const auto& blacklistedBlock : blacklistedBlocks)
            {
                if (StringUtils::containsIgnoreCase(safeBlockName(stack->mBlock), blacklistedBlock))
                {
                    skip = true;
                    break;
                }
            }
            if (skip) continue;

            Block* block = stack->mBlock;
            if (!safeMayPlaceOn(block, blockPos)) continue;

            if (!prioHighest)
            {
                slot = i;
                break;
            }

            placeables[i] = stack->mCount;
        }
    }

    if (prioHighest)
    {
        int highest = 0;
        for (const auto& [pSlot, count] : placeables)
        {
            if (count > highest)
            {
                highest = count;
                slot = pSlot;
            }
        }
    }

    return slot;
}

bool ItemUtils::isUsableBlock(ItemStack* stack)
{
    if (!stack->mItem) return false;
    return stack->mBlock && !StringUtils::containsAnyIgnoreCase(safeBlockName(stack->mBlock), std::vector<std::string>(blacklistedBlocks.begin(), blacklistedBlocks.end()));
}

int ItemUtils::getSwiftnessSpellbook(bool hotbarOnly)
{
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return -1;

    int slot = -1;

    for (int i = 0; i < 36; i++)
    {
        ItemStack* stack = player->getSupplies()->getContainer()->getItem(i);
        if (!stack->mItem) continue;
        if (hotbarOnly && i > 8) break;
        Item* item = stack->getItem();
        if (StringUtils::containsIgnoreCase(stack->getCustomName(), "Spell of Swiftness"))
        {
            slot = i;
            break;
        }
    }

    return slot;
}

int ItemUtils::getBoombox(bool hotbarOnly, bool tnt)
{
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return -1;

    int slot = -1;

    std::string name = "boombox";
    if (tnt) name = "tnt";

    for (int i = 0; i < 36; i++)
    {
        ItemStack* stack = player->getSupplies()->getContainer()->getItem(i);
        if (!stack->mItem) continue;
        if (hotbarOnly && i > 8) break;
        Item* item = stack->getItem();
        if (StringUtils::containsIgnoreCase(item->getmName(), name))
        {
            slot = i;
            break;
        }
    }

    return slot;
}

int ItemUtils::getEmptyHotbarSlot()
{
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return -1;

    for (int i = 0; i < 9; i++)
    {
        ItemStack* stack = player->getSupplies()->getContainer()->getItem(i);
        if (!stack->mItem) return i;
    }

    return -1;
}

void ItemUtils::useItem(int slot)
{
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;

    ItemStack* stack = player->getSupplies()->getContainer()->getItem(slot);
    if (!stack->mItem) return;

    auto supplies = player->getSupplies();

    int currentSlot = supplies->mSelectedSlot;
    supplies->mSelectedSlot = slot;
    player->getGameMode()->baseUseItem(stack);
    supplies->mSelectedSlot = currentSlot;
}

int ItemUtils::getHardestBlock(int slot, bool hotbarOnly)
{
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return -1;
    auto supplies = player->getSupplies();

    int hardestBlockSlot = -1;
    float slowestDestroySpeed = INT_MAX;
    for (int i = 0; i < 36; i++)
    {
        ItemStack* stack = supplies->getContainer()->getItem(i);
        if (!stack->mItem) continue;
        Item* item = stack->getItem();
        if (hotbarOnly && i > 8) continue;
        if (stack->mBlock)
        {
            // If the string contains any of the blacklisted blocks, skip it (compare using StringUtils::containsIgnoreCase
            bool skip = false;

            for (const auto& blacklistedBlock : blacklistedBlocks)
            {
                if (StringUtils::containsIgnoreCase(safeBlockName(stack->mBlock), blacklistedBlock))
                {
                    skip = true;
                    break;
                }
            }
            if (skip) continue;
            float destroySpeed = getDestroySpeed(slot, stack->mBlock);
            if (destroySpeed < slowestDestroySpeed) {
                hardestBlockSlot = i;
                slowestDestroySpeed = destroySpeed;
            }
        }
    }

    return hardestBlockSlot;
}

int ItemUtils::getBestBreakingTool(Block* block, bool hotbarOnly)
{
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return -1;

    auto supplies = player->getSupplies();
    auto container = supplies->getContainer();

    int bestSlot = supplies->mSelectedSlot;
    float bestSpeed = 0.0f;

    for (int i = 0; i < 36; i++)
    {
        auto item = container->getItem(i);
        if (!item->mItem) continue;

        float speed = getDestroySpeed(i, block);
        if (speed > bestSpeed)
        {
            bestSpeed = speed;
            bestSlot = i;
        }

        if (hotbarOnly && i > 8) break;
    }

    return bestSlot;
}

float ItemUtils::getDestroySpeed(const int slot, const Block* block, const float divisor)
{
    float result = 1.0f;
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return result;

    int oldSlot = player->getSupplies()->mSelectedSlot;
    player->getSupplies()->mSelectedSlot = slot;
    result = player->getGameMode()->getDestroyRate(*block);
    player->getSupplies()->mSelectedSlot = oldSlot;

    return result / divisor;
}

bool ItemUtils::isFireSword(ItemStack* stack)
{
    if (!stack->mItem) return false;
    return StringUtils::containsAnyIgnoreCase(stack->getCustomName(), {"§6Sword of §eEmbers", "§cFire Sword"});
}

int ItemUtils::getFireSword(bool hotbarOnly)
{
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return -1;

    int slot = -1;

    for (int i = 0; i < 36; i++)
    {
        ItemStack* stack = player->getSupplies()->getContainer()->getItem(i);
        if (!stack->mItem) continue;
        if (isFireSword(stack))
        {
            slot = i;
            break;
        }

        if (hotbarOnly && i > 8) break;
    }

    return slot;
}