#include "Group.h"
#include "MpLog.h"
#include "MpRepository.h"
#include "MpRuntimeState.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "ScriptMgr.h"

#include <optional>

// this handles updating custom group difficulties used in auto balancing mobs and
// scripts that enable buffs on mobs randomly
class MythicPlus_GroupScript : public GroupScript
{
    public:
    MythicPlus_GroupScript() : GroupScript("MythicPlus_GroupScript") { }

    void OnAddMember(Group* group, ObjectGuid guid) override
    {
        if (!group || !guid)
        {
            return;
        }

        Player* player = ObjectAccessor::FindPlayer(guid);
        if (!player)
        {
            MpLog::Warn(MpLog::Area::Instance, "Player not found for guid {}", guid.GetCounter());
            return;
        }

        // If the player is joining a new group then reset the death counters otherwise let them ride
        uint32 groupId = group->GetGUID().GetCounter();
        bool known = sMpState->UpdatePlayerData(guid, [groupId](MpPlayerData& pd)
        {
            if (pd.groupId != groupId)
            {
                pd.groupId = groupId;
                pd.ResetAllDeathCounts();
            }
        });

        if (!known)
        {
            MpDifficulty difficulty = GetPlayerDifficulty(player);
            sMpState->SetPlayerData(guid, MpPlayerData(guid, player->GetName(), difficulty, groupId));
        }

        bool added = false;
        auto addMember = [guid, &added](MpGroupData& gd) { added = gd.AddMember(guid); };
        if (!sMpState->UpdateGroupData(group->GetGUID(), addMember))
        {
            MpLog::Warn(MpLog::Area::Instance, "Group data not found for group {}", group->GetGUID().GetCounter());
            return;
        }

        if (!added)
        {
            MpLog::Warn(MpLog::Area::Instance, "PlayerData for player {} is already in the players vector",
                player->GetName());
        }
    }

    void OnCreate(Group* group, Player* leader) override
    {
        if (!group)
        {
            return;
        }

        if (!leader)
        {
            return;
        }

        // Start a group and set the data up for the group, with the leader as its first member
        MpDifficulty difficulty = GetPlayerDifficulty(leader);
        MpGroupData gd = MpGroupData(group->GetGUID(), difficulty);
        gd.AddMember(leader->GetGUID());

        // Store the data into our memory store
        sMpState->SetGroupData(group, std::move(gd));
    }

    void OnDisband(Group* group) override
    {
        sMpState->RemoveGroupData(group->GetGUID());
        sMpRepo->DBRemoveGroupData(group->GetGUID());
    }

    // Get the difficulty for a player that is assigned
    // Callers pass a non-null player
    MpDifficulty GetPlayerDifficulty(Player* player)
    {
        if (std::optional<MpPlayerData> pd = sMpState->GetPlayerData(player->GetGUID()))
            return pd->difficulty;

        return player->GetDifficulty(false) == Difficulty::DUNGEON_DIFFICULTY_NORMAL ? MP_DIFFICULTY_NORMAL :
            MP_DIFFICULTY_HEROIC;
    }
};

void Add_MP_GroupScripts()
{
    MpLog::Debug(MpLog::Area::Instance, "Add_MP_GroupScripts()");
    new MythicPlus_GroupScript();
}
