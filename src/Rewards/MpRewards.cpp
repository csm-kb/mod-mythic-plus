#include "MpRewards.h"
#include "Creature.h"
#include "Formulas.h"
#include "LootMgr.h"
#include "Map.h"
#include "MpConstants.h"
#include "MpLog.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "Random.h"
#include "World.h"

uint32 MpRewards::RollCreatureGold(Creature const* creature)
{
    // Different gold ranges based on creature rank
    uint32 bossMinGold = 10000;
    uint32 bossMaxGold = 13500;

    uint32 minGold, maxGold;

    // Determine gold range based on creature rank
    if (creature->isWorldBoss() || creature->IsDungeonBoss())
    {
        // Boss: full range
        minGold = bossMinGold;
        maxGold = bossMaxGold;
    }
    else if (creature->GetCreatureTemplate()->rank == CREATURE_ELITE_RARE ||
             creature->GetCreatureTemplate()->rank == CREATURE_ELITE_ELITE)
    {
        // Elite: 70% of boss range
        minGold = uint32(bossMinGold * 0.7f);
        maxGold = uint32(bossMaxGold * 0.7f);
    }
    else
    {
        // Normal: 40% of boss range
        minGold = uint32(bossMinGold * 0.4f);
        maxGold = uint32(bossMaxGold * 0.4f);
    }

    // Generate random gold amount in appropriate range
    uint32 newGold = urand(minGold, maxGold);

    // Apply server money rate
    return uint32(newGold * sWorld->getRate(RATE_DROP_MONEY));
}

uint32 MpRewards::CalculateKillXP(Player const* player, Creature const* creature)
{
    // Recalculate XP using scaled level instead of original level
    uint32 newBaseXP = Acore::XP::BaseGain(
        player->GetLevel(),
        creature->GetLevel(), // This is now the scaled level
        GetContentLevelsForMapAndZone(creature->GetMapId(), creature->GetZoneId())
    );

    // Apply same modifiers as original calculation
    float xpMod = 1.0f;
    if (creature->isElite())
    {
        xpMod *= creature->GetMap()->IsDungeon() ? 2.75f : 2.0f;
    }
    xpMod *= creature->GetCreatureTemplate()->ModExperience;

    return uint32(newBaseXP * xpMod * 1.5f); // flat bonus modifier for mythic dungeons
}

void MpRewards::SubstituteLootItem(Loot const& loot, LootStoreItem* lootStoreItem, LootStore const& store,
    uint32 itemOffset)
{
    // get the item to scale up
    ItemTemplate const* origItem = sObjectMgr->GetItemTemplate(lootStoreItem->itemid);
    if (!origItem)
    {
        // If there is not a scaled up item and the item is a below quality green then set an invalid item_id so it
        // is not added to loot
        ItemTemplate const* nonMythicItem = sObjectMgr->GetItemTemplate(lootStoreItem->itemid);
        if (!nonMythicItem)
        {
            MpLog::Debug(MpLog::Area::Loot, "No item template for loot item {}; keeping the original drop",
                lootStoreItem->itemid);
            return;
        }

        if (nonMythicItem->Quality < 2)
        {
            lootStoreItem->itemid = 0;
            return;
        }

        // otherwise roll a chance to see a shadowy remains item is provided instead only if there is not already a
        // shadowy remains item on the corpse
        bool hasShadowyRemains = false;
        for (auto const& item : loot.items)
        {
            if (item.itemid == MpConstants::SHADOWY_REMAINS)
            {
                hasShadowyRemains = true;
                break;
            }
        }

        if (!hasShadowyRemains)
        {
            lootStoreItem->itemid = MpConstants::SHADOWY_REMAINS;
            return;
        }
        else
        {
            lootStoreItem->itemid = 0;
            return;
        }
    }

    uint32 newItemId = origItem->ItemId + itemOffset;
    ItemTemplate const* newItemTempl = sObjectMgr->GetItemTemplate(newItemId);

    if (!newItemTempl)
    {
        MpLog::Warn(MpLog::Area::Loot, "New Loot Item not found for itemid {} original item: {} ({})", newItemId,
            origItem->Name1, origItem->ItemId);
        return;
    }

    lootStoreItem->itemid = newItemId;

    // Revalidate the LootStoreItem to ensure consistency
    if (!lootStoreItem->IsValid(store, newItemId))
    {
        MpLog::Info(MpLog::Area::Loot,
            "LootStoreItem is not valid after updating itemid to {} in OnBeforeDropAddItem()", newItemId);
        return;
    }
}
