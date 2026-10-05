#include "Chat.h"
#include "Group.h"
#include "MpBots.h"
#include "MpConfig.h"
#include "MpLog.h"
#include "Map.h"
#include "MpScaler.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "StringFormat.h"

#include <optional>
#include <string>

// Why a run did not get a tier. Bots enter in bulk, so their lines stay at DEBUG.
static void LogUntiered(Map* map, Player* player, Group* group, char const* reason)
{
    bool bot = MpBots::IsBot(player);
    if (!MpLog::Enabled(MpLog::Area::Instance, bot ? LOG_LEVEL_DEBUG : LOG_LEVEL_INFO))
        return;

    std::string line = Acore::StringFormat(
        "event=instance_untiered map={} instance={} group={} leader={} player={} guid={} reason={}",
        map->GetId(), map->GetInstanceId(), group ? group->GetGUID().GetCounter() : 0,
        group ? group->GetLeaderName() : "-", player->GetName(), player->GetGUID().ToString(), reason);
    if (bot)
        MpLog::Debug(MpLog::Area::Instance, "{}", line);
    else
        MpLog::Info(MpLog::Area::Instance, "{}", line);
}

class MythicPlus_AllMapScript : public AllMapScript
{
public:
    MythicPlus_AllMapScript() : AllMapScript("MythicPlus_AllMapScript")
    {
    }

    void OnCreateMap(Map* /*map*/) override { }

    /**
     * When a player enters the map check it needs to set up the instance data
     */
    void OnPlayerEnterAll(Map* map, Player* player) override
    {
        // not a dungeon: nothing to report
        if (!map->IsDungeon())
        {
            return;
        }

        if (!sMpConfig->enabled)
        {
            LogUntiered(map, player, nullptr, "module_disabled");
            return;
        }

        Group* group = player->GetGroup();
        if (!group)
        {
            LogUntiered(map, player, nullptr, "no_group");
            return;
        }

        // if there is not any group data for this group then just bail
        std::optional<MpGroupData> groupData = sMpState->GetGroupData(group->GetGUID());
        if (!groupData)
        {
            LogUntiered(map, player, group, "no_group_tier");
            return;
        }

        MpLog::Debug(MpLog::Area::Instance, "Player {} entered map {} in groupLeader {}", player->GetName(),
            map->GetMapName(), group->GetLeaderName());

        // Check if we already have mythic instance data set for this map and group
        if (sMpState->GetInstanceData(map->GetId(), map->GetInstanceId()))
        {
            if (player->GetName() == group->GetLeaderName())
            {
                MpLog::Debug(MpLog::Area::Instance,
                    "Instance data already set for Map: {} InstanceId: {} for GroupLeader: {} ",
                    map->GetMapName(),
                    map->GetInstanceId(),
                    group->GetLeaderName()
                );
            }
            return;
        }

        // a group difficulty without a configured tier is untiered too
        if (!sMpScaler->InitInstance(map, player, group, groupData->difficulty))
        {
            LogUntiered(map, player, group, "no_group_tier");
        }
    }

    // When an instance is destroyed remove the instance data from the data store
    void OnDestroyInstance(MapInstanced* /*mapInstanced*/, Map* map) override
    {
        if (!sMpScaler->IsMapEligible(map))
        {
            return;
        }

        // Removes currenct GroupData Instance Data and removes from database storage
        sMpState->RemoveInstanceData(map->GetId(), map->GetInstanceId());

        // remove group instance and group instance data from database during a reset
        sMpRepo->DBRemovePlayerInstanceData(map->GetInstanceId());
        sMpRepo->DBRemoveGroupInstanceData(map->GetInstanceId());
    }
};

void Add_MP_AllMapScripts()
{
    MpLog::Debug(MpLog::Area::Instance, "Add_MP_AllMapScripts()");
    new MythicPlus_AllMapScript();
}
