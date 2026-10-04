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

    auras.reserve(3);
    affixes.reserve(3);
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

void MpRuntimeState::SetPlayerData(ObjectGuid guid, MpPlayerData data)
{
    std::unique_lock lock(_lock);
    _players.insert_or_assign(guid, std::move(data));
}

void MpRuntimeState::RemovePlayerData(ObjectGuid guid)
{
    {
        std::unique_lock lock(_lock);
        _players.erase(guid);
    }

    MpLog::Debug(MpLog::Area::Instance, "RemovePlayerData for player {}", guid.ToString());
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

    Map::PlayerList const players = map->GetPlayers();
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

    Map* map = leader->GetMap();
    if (!map)
    {
        MpLog::Error(MpLog::Area::Instance, "SetGroupData called with null map for group leader");
        return;
    }

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

std::optional<MpCreatureData> MpRuntimeState::GetCreatureData(ObjectGuid guid) const
{
    std::shared_lock lock(_lock);
    auto itr = _creatures.find(guid);
    if (itr == _creatures.end())
        return std::nullopt;

    return itr->second;
}

void MpRuntimeState::SetCreatureData(ObjectGuid guid, MpCreatureData data)
{
    std::unique_lock lock(_lock);
    _creatures.insert_or_assign(guid, std::move(data));
}

void MpRuntimeState::RemoveCreatureData(ObjectGuid guid)
{
    std::unique_lock lock(_lock);
    _creatures.erase(guid);
}

std::vector<ObjectGuid> MpRuntimeState::GetInstanceCreatureGuids(uint32 mapId, uint32 instanceId,
    bool unscaledOnly) const
{
    std::vector<ObjectGuid> guids;

    std::shared_lock lock(_lock);
    for (auto const& [guid, data] : _creatures)
        if (data.mapId == mapId && data.instanceId == instanceId && (!unscaledOnly || !data.scaled))
            guids.push_back(guid);

    return guids;
}

MpCreatureCounts MpRuntimeState::CountInstanceCreatures(uint32 mapId, uint32 instanceId) const
{
    MpCreatureCounts counts;

    std::shared_lock lock(_lock);
    for (auto const& entry : _creatures)
    {
        MpCreatureData const& data = entry.second;
        if (data.mapId != mapId || data.instanceId != instanceId)
            continue;

        if (data.scaled)
            ++counts.scaled;
        else
            ++counts.pending;
    }

    return counts;
}
