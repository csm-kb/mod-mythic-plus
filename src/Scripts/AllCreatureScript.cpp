#include "CreatureAI.h"
#include "MpLog.h"
#include "MythicPlus.h"
#include "MpScriptAI.h"
#include "ScriptMgr.h"

#include <optional>

class MythicPlus_AllCreatureScript : public AllCreatureScript
{
public:
    MythicPlus_AllCreatureScript() : AllCreatureScript("MythicPlus_AllCreatureScript") {}

    // void OnBeforeCreatureSelectLevel(const CreatureTemplate* /*creatureTemplate*/, Creature* creature, uint8& level) override
    // {
    // }

    void OnCreatureRespawn(Creature* creature)
    {
        Map* map = creature->GetMap();
        if (!sMythicPlus->IsMapEligible(map)) {
            return;
        }

        if (!sMythicPlus->IsCreatureEligible(creature)) {
            return;
        }

        // If we have instance data, scale the creature, otherwise add it to be scaled later
        if (auto instanceData = sMpState->GetInstanceData(map->GetId(), map->GetInstanceId())) {
            sMythicPlus->AddScaledCreature(creature, *instanceData);
        } else {
            sMythicPlus->AddCreatureForScaling(creature);
        }
    }

    /**
     * @brief This hook runs every update for all creatures in the world.
     * We only need to concern ourselves with creatures in the scope of our mythic+ instances.
     * Need to detect the following changes:
     *  - Creature Death State - to trigger respawn scaling.
     *  - Other events where a creature enters the instance that is not scaled, then should be scaled up. Some special events normal enemies will be scripted
     *    to show up in encounters these will not trigger the OnCreatureAddWorld, because they were not during the initial load of the instance. (Though sometimes summons do trigger this?)
     *
     * @param creature
     * @param diff
     */
    void OnAllCreatureUpdate(Creature* creature, uint32 diff) override
    {
        // Skip any creatures not in an instance we are scaling first to avoid unnecessary work
        if (!sMythicPlus->IsMapEligible(creature->GetMap())) {
            return;
        }

        if (!sMythicPlus->IsCreatureEligible(creature)) {
            return;
        }

        // throttle this check per creature to only run if more than 20ms has passed since last check. The timer
        // lives in the creature's record, so a creature without one is checked right away.
        ObjectGuid creatureGuid = creature->GetGUID();
        bool throttled = false;
        bool known = sMpState->UpdateCreatureData(creatureGuid, [diff, &throttled](MpCreatureData& data)
        {
            data.updateTimer += diff;
            if (data.updateTimer < 20)
            {
                throttled = true;
                return;
            }

            data.updateTimer = 0;
        });

        if (throttled) {
            return;
        }

        std::optional<MpInstanceData> instanceData = sMpState->GetInstanceData(creature->GetMapId(),
            creature->GetInstanceId());
        // no instance data yet means dont scale.
        if(!instanceData) {
            return;
        }

        // this is a creature that was not scaled at instance load time, we need to scale it now.
        if(!known) {
            MpLog::Debug(MpLog::Area::Scaling, "OnAllCreatureUpdate: Unknown Creature Add event scaling creature: {}", creature->GetName());
            sMythicPlus->AddScaledCreature(creature, *instanceData);
            return;
        }

        // record the death of our scaled creature; a corpse that comes back alive was respawned and is rescaled
        DeathState currentState = creature->getDeathState();
        bool respawned = false;
        if (!sMpState->UpdateCreatureData(creatureGuid, [currentState, &respawned](MpCreatureData& data)
            {
                if (currentState == DeathState::Corpse && data.lastDeathState != DeathState::Corpse)
                    data.lastDeathState = currentState;
                else if (currentState == DeathState::Alive && data.lastDeathState == DeathState::Corpse)
                    respawned = true;
            }))
        {
            return;
        }

        if(respawned) {
            MpLog::Debug(MpLog::Area::Scaling,
                "OnAllCreatureUpdate: Creature Death event scaling creature: {} level: {} guid: {} event: {}",
                creature->GetName(), creature->GetLevel(), creature->GetGUID().ToString(), creature->getDeathState());
            sMythicPlus->AddScaledCreature(creature, *instanceData);
        }
    }

    // When a new creature is added into a mythic+ map add it to the list of creatures to scale later.
    void OnCreatureAddWorld(Creature* creature) override
    {
        Map* map = creature->GetMap();
        if (!sMythicPlus->IsMapEligible(map)) {
            return;
        }

        if (!sMythicPlus->IsCreatureEligible(creature)) {
            return;
        }

        // if we have instance data about zone then just scale the creature otherwise add to be scaled once we do.
        std::optional<MpInstanceData> instanceData = sMpState->GetInstanceData(map->GetId(), map->GetInstanceId());

        if(instanceData) {
            sMythicPlus->AddScaledCreature(creature, *instanceData);
        } else {
            sMythicPlus->AddCreatureForScaling(creature);
        }
    }

    // Cleanup the creature from custom data used for mythic+ mod
    void OnCreatureRemoveWorld(Creature* creature) override
    {
        sMpState->RemoveCreatureData(creature->GetGUID());
    }

};

void Add_MP_AllCreatureScripts()
{
    MpLog::Debug(MpLog::Area::Scaling, "Add_MP_AllCreatureScripts");
    new MythicPlus_AllCreatureScript();
}
