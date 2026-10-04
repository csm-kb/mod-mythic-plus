#include "Chat.h"
#include "MpConfig.h"
#include "MpLog.h"
#include "Map.h"
#include "MapMgr.h"
#include "MpScaler.h"
#include "Player.h"
#include "ScriptMgr.h"

#include <optional>

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
        if (!sMpScaler->IsMapEligible(map))
        {
            return;
        }

        if (!sMpScaler->IsDifficultySet(player))
        {
            return;
        }

        Group* group = player->GetGroup();
        if (group)
        {
            MpLog::Debug(MpLog::Area::Instance, "Player {} entered map {} in groupLeader {}", player->GetName(), map->GetMapName(), group->GetLeaderName());
        }
        else
        {
            return;
        }

        // if there is not any group data for this group then just bail
        std::optional<MpGroupData> groupData = sMpState->GetGroupData(group->GetGUID());
        if (!groupData)
        {
            return;
        }

        // Check if we already have mythic instance data set for this map and group
        if (sMpState->GetInstanceData(map->GetId(), map->GetInstanceId()))
        {
            if (player->GetName() == group->GetLeaderName())
            {
                MpLog::Debug(MpLog::Area::Instance, "Instance data already set for Map: {} InstanceId: {} for GroupLeader: {} ",
                    map->GetMapName(),
                    map->GetInstanceId(),
                    group->GetLeaderName()
                );
            }
            return;
        }

        MpInstanceData instanceData;
        MpTierConfig const* tier = sMpConfig->GetTier(groupData->difficulty);
        if (!tier)
        {
            MpLog::Debug(MpLog::Area::Instance, "No difficulty set for group {}", group->GetGUID().GetCounter());
            return;
        }
        instanceData.boss = tier->boss;
        instanceData.creature = tier->dungeon;
        instanceData.itemRewards = sMpConfig->enableItemRewards;
        instanceData.deathLimits = tier->deathAllowance;
        instanceData.itemOffset = tier->itemOffset;

        instanceData.difficulty = groupData->difficulty;

        // Make sure the map resolves to an InstanceMap; the record keeps only {mapId, instanceId}
        if (!dynamic_cast<InstanceMap*>(sMapMgr->FindMap(map->GetId(), map->GetInstanceId())))
        {
            MpLog::Error(MpLog::Area::Instance, "Failed to find InstanceMap for map ID {} and instance ID {}.", map->GetId(), map->GetInstanceId());
            return;
        }

        MpLog::Debug(MpLog::Area::Instance, "Setting up instance data for group {} for map {} instance {} data {}",
            group->GetGUID().GetCounter(),
            map->GetMapName(),
            map->GetInstanceId(),
            instanceData.ToString()
        );
        sMpState->SetInstanceData(map->GetId(), map->GetInstanceId(), instanceData);

        // Save the instance data for the user to the database
        if (player)
        {
            sMpRepo->DBUpdatePlayerInstanceData(player->GetGUID(), groupData->difficulty, map->GetId(),
                map->GetInstanceId(), 0);
        }

        // Once we have instance data set we can scale the remaining characters in our instance
        sMpScaler->ScaleRemaining(player, instanceData);
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
