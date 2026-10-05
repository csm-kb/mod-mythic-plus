#include "MpRuntimeState.h"
#include "Chat.h"
#include "Creature.h"
#include "Group.h"
#include "Map.h"
#include "MpLog.h"
#include "MpRepository.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"

// Lock discipline: every member below takes _lock for container access only. Logging, DB writes and core calls
// happen before taking it or after releasing it.

std::string MpInstanceData::ToString() const
{
    return "MpInstanceData: { boss: " + boss.ToString() +
           ", creature: " + creature.ToString() +
           ", itemRewards: " + (itemRewards ? "true" : "false") +
           ", deathLimits: " + std::to_string(deathLimits) +
           ", itemOffset: " + std::to_string(itemOffset) + " }";
}

MpCreatureData::MpCreatureData(Creature* c) : creature(c)
{
    if (creature)
    {
        mapId = creature->GetMapId();
        instanceId = creature->GetInstanceId();
        originalLevel = creature->GetLevel();
        originalStats = sObjectMgr->GetCreatureBaseStats(originalLevel, creature->GetCreatureTemplate()->unit_class);
        originalInstanceHealth = creature->GetMaxHealth();
    }
}

std::string MpCreatureData::ToString() const
{
    std::string origStatsStr;
    if (originalStats)
    {
        origStatsStr = "Original Stats: \n Health: " + std::to_string(originalInstanceHealth) + "\n" +
                       "Mana: " + std::to_string(originalStats->BaseMana) + "\n" +
                       "Armor: " + std::to_string(originalStats->BaseArmor) + "\n" +
                       "Attack Power: " + std::to_string(originalStats->AttackPower) + "\n";
    }
    else
        origStatsStr = "Original Stats Display Failed: \n Did you select target or in an non-scaled instance? \n";

    return " MpCreatureData: \n Original level: " + std::to_string(originalLevel) + "\n" +
           origStatsStr +
           " NewAttackPower: " + std::to_string(NewAttackPower) + "\n" +
           " AttackPowerScaleMultiplier: " + std::to_string(AttackPowerScaleMultiplier) + "\n" +
           " Difficulty: " + std::to_string(difficulty) + "\n" +
           " Scaled: " + (scaled ? "true" : "false") + "\n";
}

MpRuntimeState* MpRuntimeState::instance()
{
    static MpRuntimeState instance;
    return &instance;
}

// ---- Players ----

std::optional<MpPlayerData> MpRuntimeState::GetPlayerData(ObjectGuid guid) const
{
    std::shared_lock lock(_lock);
    auto itr = _players.find(guid);
    if (itr == _players.end())
        return std::nullopt;

    return itr->second;
}

// Keeps an existing record, like the base AddPlayerData (emplace): callers use it as the create step of a
// get-or-create, so a record another thread created in between must not be overwritten.
void MpRuntimeState::SetPlayerData(ObjectGuid guid, MpPlayerData data)
{
    std::unique_lock lock(_lock);
    _players.try_emplace(guid, std::move(data));
}

void MpRuntimeState::RemovePlayerData(ObjectGuid guid)
{
    {
        std::unique_lock lock(_lock);
        _players.erase(guid);
    }

    MpLog::Debug(MpLog::Area::Instance, "RemovePlayerData for player {}", guid.ToString());
}

std::optional<uint32> MpRuntimeState::AddPlayerDeath(ObjectGuid guid, uint32 mapId, uint32 instanceId)
{
    uint32 playerDeaths = 0;
    bool known = UpdatePlayerData(guid, [mapId, instanceId, &playerDeaths](MpPlayerData& pd)
    {
        playerDeaths = pd.AddDeath(mapId, instanceId);
    });

    if (!known)
        return std::nullopt;

    return playerDeaths;
}

// Get-or-create in two steps (update, else create), as the OnPlayerBindToInstance hook did.
void MpRuntimeState::BindPlayerInstance(ObjectGuid guid, std::string const& name, MpDifficulty difficulty,
    uint32 groupId, uint32 mapId, uint32 instanceId)
{
    auto mapKey = std::make_pair(mapId, instanceId);
    auto bindInstance = [&mapKey](MpPlayerData& pd)
    {
        pd.instanceData.emplace(mapKey, MpPlayerInstanceData{ .deaths = 0 });
    };

    if (!UpdatePlayerData(guid, bindInstance))
    {
        MpPlayerData playerData(guid, name, difficulty, groupId);
        bindInstance(playerData);
        SetPlayerData(guid, std::move(playerData));
    }
}

bool MpRuntimeState::SetPlayerGroup(ObjectGuid guid, uint32 groupId)
{
    return UpdatePlayerData(guid, [groupId](MpPlayerData& pd)
    {
        if (pd.groupId != groupId)
        {
            pd.groupId = groupId;
            pd.ResetAllDeathCounts();
        }
    });
}

// ---- Groups ----

std::optional<MpGroupData> MpRuntimeState::GetGroupData(ObjectGuid groupGuid) const
{
    std::shared_lock lock(_lock);
    auto itr = _groups.find(groupGuid);
    if (itr == _groups.end())
        return std::nullopt;

    return itr->second;
}

// Resets the leader's instance and notifies its players (the leader changed the difficulty).
static void ResetLeaderInstance(Map* map, InstanceMap* instance)
{
    if (!instance || !instance->HavePlayers())
        return;

    instance->Reset(2); // 2 = reset all

    Map::PlayerList const& players = map->GetPlayers();
    for (auto itr = players.begin(); itr != players.end(); ++itr)
    {
        Player* player = itr->GetSource();
        if (!player)
        {
            MpLog::Error(MpLog::Area::Instance, "SetGroupData called with null player in instance");
            continue;
        }

        ChatHandler(player->GetSession()).SendNotification(
            "The group leader has changed the difficulty setting. You have been removed from the instance.");
    }
}

// Same flow as the base AddGroupData: validate, reset the leader's instance when needed, store (replacing any
// existing record), then persist the difficulty for every online member.
void MpRuntimeState::SetGroupData(Group* group, MpGroupData data)
{
    if (!group)
    {
        MpLog::Error(MpLog::Area::Instance, "SetGroupData called with null group pointer");
        return;
    }

    ObjectGuid guid = group->GetGUID();
    if (!guid)
    {
        MpLog::Error(MpLog::Area::Instance, "SetGroupData called with invalid group GUID");
        return;
    }

    MpDifficulty difficulty = data.difficulty;
    if (!difficulty)
    {
        MpLog::Error(MpLog::Area::Instance, "SetGroupData called with invalid difficulty");
        return;
    }

    Player* leader = group->GetLeader();
    if (!leader)
    {
        MpLog::Error(MpLog::Area::Instance, "SetGroupData called with null group leader");
        return;
    }

    Map* map = leader->GetMap(); // never null: GetMap() asserts

    bool resetInstance = true;
    {
        std::shared_lock lock(_lock);
        auto itr = _groups.find(guid);
        if (itr != _groups.end())
            resetInstance = difficulty == MP_DIFFICULTY_HEROIC || difficulty == MP_DIFFICULTY_NORMAL ||
                difficulty != itr->second.difficulty;
    }

    if (resetInstance)
        ResetLeaderInstance(map, map->ToInstanceMap());

    {
        std::unique_lock lock(_lock);
        _groups.insert_or_assign(guid, std::move(data));
    }

    for (auto const& memberSlot : group->GetMemberSlots())
        if (Player* player = ObjectAccessor::FindPlayer(memberSlot.guid))
            sMpRepo->DBUpdatePlayerInstanceData(player->GetGUID(), difficulty);
}

void MpRuntimeState::RemoveGroupData(ObjectGuid groupGuid)
{
    MpLog::Debug(MpLog::Area::Instance, "RemoveGroupData for group {}", groupGuid.ToString());

    std::unique_lock lock(_lock);
    _groups.erase(groupGuid);
}

uint32 MpRuntimeState::GetGroupDeaths(ObjectGuid groupGuid, uint32 mapId, uint32 instanceId) const
{
    std::shared_lock lock(_lock);
    auto groupItr = _groups.find(groupGuid);
    if (groupItr == _groups.end())
        return 0;

    uint32 deaths = 0;
    for (ObjectGuid const& member : groupItr->second.members)
    {
        auto playerItr = _players.find(member);
        if (playerItr != _players.end())
            deaths += playerItr->second.GetDeaths(mapId, instanceId);
    }

    return deaths;
}

bool MpRuntimeState::AddGroupMember(ObjectGuid groupGuid, ObjectGuid memberGuid, bool& added)
{
    added = false;
    return UpdateGroupData(groupGuid, [memberGuid, &added](MpGroupData& gd) { added = gd.AddMember(memberGuid); });
}

// ---- Instances ----

std::optional<MpInstanceData> MpRuntimeState::GetInstanceData(uint32 mapId, uint32 instanceId) const
{
    std::shared_lock lock(_lock);
    auto itr = _instances.find(std::make_pair(mapId, instanceId));
    if (itr == _instances.end())
        return std::nullopt;

    return itr->second;
}

// Keeps an existing record, like the base AddInstanceData (emplace).
void MpRuntimeState::SetInstanceData(uint32 mapId, uint32 instanceId, MpInstanceData data)
{
    std::unique_lock lock(_lock);
    _instances.emplace(std::make_pair(mapId, instanceId), std::move(data));
}

void MpRuntimeState::RemoveInstanceData(uint32 mapId, uint32 instanceId)
{
    std::unique_lock lock(_lock);
    _instances.erase(std::make_pair(mapId, instanceId));
}

// ---- Creatures ----

MpRuntimeState::CreatureKey MpRuntimeState::KeyOf(Creature const* creature)
{
    return { std::make_pair(creature->GetMapId(), creature->GetInstanceId()), creature->GetGUID() };
}

std::optional<MpCreatureData> MpRuntimeState::GetCreatureData(Creature const* creature) const
{
    CreatureKey const key = KeyOf(creature);

    std::shared_lock lock(_lock);
    auto instanceItr = _creatures.find(key.instance);
    if (instanceItr == _creatures.end())
        return std::nullopt;

    auto itr = instanceItr->second.find(key.guid);
    if (itr == instanceItr->second.end())
        return std::nullopt;

    return itr->second;
}

// Replaces any existing record (base AddCreatureData): scaling and respawns reset the record on purpose.
// The record's mapId/instanceId are set from the key so the two always agree.
void MpRuntimeState::SetCreatureData(Creature const* creature, MpCreatureData data)
{
    CreatureKey const key = KeyOf(creature);
    data.mapId = key.instance.first;
    data.instanceId = key.instance.second;

    std::unique_lock lock(_lock);
    _creatures[key.instance].insert_or_assign(key.guid, std::move(data));
}

void MpRuntimeState::RemoveCreatureData(Creature const* creature)
{
    CreatureKey const key = KeyOf(creature);

    std::unique_lock lock(_lock);
    auto instanceItr = _creatures.find(key.instance);
    if (instanceItr == _creatures.end())
        return;

    instanceItr->second.erase(key.guid);
    if (instanceItr->second.empty())
        _creatures.erase(instanceItr);
}

bool MpRuntimeState::AdvanceCreatureUpdate(Creature const* creature, uint32 diff, DeathState currentState,
    bool& throttled, bool& respawned)
{
    throttled = false;
    respawned = false;
    return UpdateCreatureData(creature, [diff, currentState, &throttled, &respawned](MpCreatureData& data)
    {
        data.updateTimer += diff;
        if (data.updateTimer < 20)
        {
            throttled = true;
            return;
        }

        data.updateTimer = 0;

        if (currentState == DeathState::Corpse && data.lastDeathState != DeathState::Corpse)
            data.lastDeathState = currentState;
        else if (currentState == DeathState::Alive && data.lastDeathState == DeathState::Corpse)
            respawned = true;
    });
}

std::vector<ObjectGuid> MpRuntimeState::GetInstanceCreatureGuids(uint32 mapId, uint32 instanceId,
    bool unscaledOnly) const
{
    std::vector<ObjectGuid> guids;

    std::shared_lock lock(_lock);
    auto instanceItr = _creatures.find(std::make_pair(mapId, instanceId));
    if (instanceItr == _creatures.end())
        return guids;

    for (auto const& [guid, data] : instanceItr->second)
        if (!unscaledOnly || !data.scaled)
            guids.push_back(guid);

    return guids;
}

MpCreatureCounts MpRuntimeState::CountInstanceCreatures(uint32 mapId, uint32 instanceId) const
{
    MpCreatureCounts counts;

    std::shared_lock lock(_lock);
    auto instanceItr = _creatures.find(std::make_pair(mapId, instanceId));
    if (instanceItr == _creatures.end())
        return counts;

    for (auto const& entry : instanceItr->second)
    {
        if (entry.second.scaled)
            ++counts.scaled;
        else
            ++counts.pending;
    }

    return counts;
}
