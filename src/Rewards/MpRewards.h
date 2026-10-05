#ifndef MP_REWARDS_H
#define MP_REWARDS_H

#include "Define.h"

class Creature;
class LootStore;
class Player;
struct Loot;
struct LootStoreItem;

/**
 * Mythic+ kill rewards: gold and XP for scaled creatures, and the Mythic+ item substitution on loot drops.
 *
 * Callers have already checked that the creature is Mythic+ scaled, or for loot that the drop is in an eligible
 * Mythic+ instance with item rewards enabled.
 *
 * This is a singleton instance that can be accessed through sMpRewards.
 */
class MpRewards
{
public:
    static MpRewards* instance()
    {
        static MpRewards instance;
        return &instance;
    }

    MpRewards(MpRewards const&) = delete;
    MpRewards& operator=(MpRewards const&) = delete;

    // Gold for a scaled creature's loot: a random amount in its rank's range, at the server money rate
    uint32 RollCreatureGold(Creature const* creature);

    // Kill XP for a scaled creature, recalculated at its scaled level
    uint32 CalculateKillXP(Player const* player, Creature const* creature);

    // Swaps the drop for its Mythic+ item (item id + itemOffset)
    void SubstituteLootItem(Loot const& loot, LootStoreItem* lootStoreItem, LootStore const& store, uint32 itemOffset);

private:
    MpRewards() { }
    ~MpRewards() { }
};

#define sMpRewards MpRewards::instance()

#endif // MP_REWARDS_H
