#include "AdvancementMgr.h"
#include "Group.h"
#include "MpBots.h"
#include "MpLog.h"
#include "MpRewards.h"
#include "MpScaler.h"
#include "Player.h"
#include "ScriptMgr.h"

#include <optional>

class MythicPlus_PlayerScript : public PlayerScript
{
public:
    MythicPlus_PlayerScript() : PlayerScript("MythicPlus_PlayerScript") {}

    /**
     * Mythic+ special actions when a player dies:
     * - Track the death for the group
     * - Update the player death stats
     * - Determine whether or not the Group failed the instance due to death count setting.
     */

    void OnPlayerKilledByCreature(Creature* killer, Player* player) override
    {
        if (!player)
        {
            return;
        }

        Map* map = player->GetMap();
        if (!sMpScaler->IsMapEligible(map))
        {
            return;
        }

        Group* group = player->GetGroup();
        if (!group)
        {
            return;
        }

        std::optional<MpGroupData> data = sMpState->GetGroupData(group->GetGUID());
        if (!data)
        {
            return;
        }

        std::optional<uint32> playerDeaths = sMpState->AddPlayerDeath(player->GetGUID(), map->GetId(),
            map->GetInstanceId());
        if (!playerDeaths)
            return;

        MpLog::Info(MpLog::Area::Instance, "Player {} added death to instance data {}", player->GetName(),
            *playerDeaths);

        if (killer)
        {
            sMpRepo->DBAddPlayerDeath(player, killer, data->difficulty);
        }
        else
        {
            sMpRepo->DBAddPlayerDeath(player);
        }

        sMpRepo->DBAddGroupDeath(group, player->GetMapId(), player->GetInstanceId(), data->difficulty);

        uint32 totalDeaths = sMpState->GetGroupDeaths(group->GetGUID(), player->GetMapId(), player->GetInstanceId());
        MpLog::Info(MpLog::Area::Instance, "Total Deaths: {}", totalDeaths);
        if (totalDeaths > 1)
        {
            // Death-limit enforcement is not implemented yet; see sub-project 2 notes.
            MpLog::Debug(MpLog::Area::Instance, "Group {} death threshold reached in map {} instance {}",
                group->GetGUID().GetCounter(), map->GetId(), map->GetInstanceId());
        }
    }

    void OnPlayerBeforeLootMoney(Player* player, Loot* loot) override
    {
        if (!loot->sourceWorldObjectGUID.IsCreature()) return;

        Creature* creature = player->GetMap()->GetCreature(loot->sourceWorldObjectGUID);
        if (!creature) return;

        if (MpBots::IsNpcBotOrPet(creature))
        {
            return;
        }

        // Check if this is a Mythic+ scaled creature
        std::optional<MpCreatureData> creatureData = sMpState->GetCreatureData(creature);
        if (!creatureData || !creatureData->IsScaled()) return;

        loot->gold = sMpRewards->RollCreatureGold(creature);
    }

    void OnPlayerGiveXP(Player* player, uint32& amount, Unit* victim, uint8 xpSource) override
    {
        if (xpSource != XPSOURCE_KILL || !victim) return;

        Creature* creature = victim->ToCreature();
        if (!creature) return;

        if (MpBots::IsNpcBotOrPet(creature))
        {
            return;
        }

        // Check if this is a Mythic+ scaled creature
        std::optional<MpCreatureData> creatureData = sMpState->GetCreatureData(creature);
        if (!creatureData || !creatureData->IsScaled()) return;

        amount = sMpRewards->CalculateKillXP(player, creature);
    }

    void OnPlayerLogin(Player* player) override
    {
        MpLog::Info(MpLog::Area::Instance, "Player {} logged in", player->GetName());

        // Load the player advancement data for the player when they login
        sAdvancementMgr->LoadPlayerAdvancements(player);

        // Cast all unique advancement spells
        for (uint32 i = 1; i <= 10; ++i)
        {
            uint32 spellId = 80000000 + i;
            MpLog::Info(MpLog::Area::Instance, "Casting spell {} to player {}", spellId, player->GetName());
            player->AddAura(spellId, player);
        }
    }

    // When a player is bound to an instance need to make sure they are saved in the data soure to retrieve later.
    void OnPlayerBindToInstance(Player* player, Difficulty /*difficulty*/, uint32 mapId, bool /*permanent*/) override
    {
        if (!player)
        {
            return;
        }

        Group* group = player->GetGroup();

        // If they are not in a group do nothing.
        if (!group)
        {
            return;
        }

        std::optional<MpGroupData> data = sMpState->GetGroupData(group->GetGUID());

        // If there is not any mythic+ data set for this group do nothing.
        if (!data)
        {
            return;
        }

        Map* map = player->GetMap(); // never null: GetMap() asserts

        // Track the bound instance on the player data, setting the player data up if needed
        ObjectGuid playerGuid = player->GetGUID();
        sMpState->BindPlayerInstance(playerGuid, player->GetName(), data->difficulty, group->GetGUID().GetCounter(),
            mapId, player->GetInstanceId());

        // Add this player to the group data
        bool added = false;
        if (sMpState->AddGroupMember(group->GetGUID(), playerGuid, added) && !added)
        {
            MpLog::Warn(MpLog::Area::Instance, "PlayerData for player {} is already in the players vector",
                player->GetName());
        }

        sMpRepo->DBUpdatePlayerInstanceData(player->GetGUID(), data->difficulty, map->GetId(), map->GetInstanceId());
        sMpRepo->DBUpdateGroupData(group->GetGUID(), data->difficulty, map->GetId(), map->GetInstanceId(), 0);
    }
};

void Add_MP_PlayerScripts()
{
    MpLog::Debug(MpLog::Area::Instance, "Add_MP_PlayerScripts()");
    new MythicPlus_PlayerScript();
}
