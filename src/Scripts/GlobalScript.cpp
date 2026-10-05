#include "MpLog.h"
#include "MpRewards.h"
#include "MpScaler.h"
#include "ScriptMgr.h"
#include "Player.h"
#include "Map.h"

#include <optional>

class MythicPlus_GlobalScript : public GlobalScript
{
public:

    MythicPlus_GlobalScript() : GlobalScript("MythicPlus_GlobalScript") { }

    // This adds the mythic+ item scaling to the loot table for enemies
    void OnBeforeDropAddItem(Player const* player, Loot& loot, bool /*canRate*/, uint16 /*lootMode*/,
        LootStoreItem* LootStoreItem, LootStore const& store) override
    {
        if (LootStoreItem->itemid == 0)
        {
            return;
        }

        // Not playing on mythic+ difficulty skip doing loot
        if (!sMpScaler->IsDifficultySet(player))
        {
            return;
        }

        // Not on an eligible map skip doing loot
        Map* map = player->GetMap();
        if (!sMpScaler->IsMapEligible(map))
        {
            return;
        }

        std::optional<MpInstanceData> mythicSettings = sMpState->GetInstanceData(map->GetId(), map->GetInstanceId());

        // if there are not mythic settings set for this group and map skip
        if (!mythicSettings)
        {
            MpLog::Warn(MpLog::Area::Loot,
                "event=loot_failed reason=no_instance_settings map={} instance={} player={} guid={}", map->GetId(),
                map->GetInstanceId(), player->GetName(), player->GetGUID().ToString());
            return;
        }

        // if the item rewards are disabled skip
        if (!mythicSettings->itemRewards)
        {
            return;
        }

        sMpRewards->SubstituteLootItem(loot, LootStoreItem, store, mythicSettings->itemOffset);
    }
};

void Add_MP_GlobalScripts()
{
    MpLog::Debug(MpLog::Area::Loot, "Add_MP_GlobalScripts()");
    new MythicPlus_GlobalScript();
}
