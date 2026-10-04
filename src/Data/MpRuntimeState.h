#ifndef MP_RUNTIME_STATE_H
#define MP_RUNTIME_STATE_H

#include "Define.h"
#include "MpTypes.h"
#include "ObjectGuid.h"
#include "Unit.h"

#include <map>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class Creature;
class Group;
struct CreatureBaseStats;

struct MpPlayerInstanceData
{
    uint32 deaths = 0;
};

// Tracks a player's Mythic+ state. Plain data: resolve the Player with ObjectAccessor::FindPlayer(guid) at use.
struct MpPlayerData
{
    ObjectGuid guid;
    std::string name;
    MpDifficulty difficulty = MP_DIFFICULTY_NORMAL;
    uint32 groupId = 0;

    // {mapId, instanceId} the player is bound to -> Mythic+ data for it
    std::map<std::pair<uint32, uint32>, MpPlayerInstanceData> instanceData;

    MpPlayerData(ObjectGuid playerGuid, std::string playerName, MpDifficulty playerDifficulty, uint32 playerGroupId)
        : guid(playerGuid), name(std::move(playerName)), difficulty(playerDifficulty), groupId(playerGroupId) { }

    // Returns the new death count for the instance.
    uint32 AddDeath(uint32 mapId, uint32 instanceId)
    {
        return ++instanceData[std::make_pair(mapId, instanceId)].deaths;
    }

    uint32 GetDeaths(uint32 mapId, uint32 instanceId) const
    {
        auto itr = instanceData.find(std::make_pair(mapId, instanceId));
        return itr != instanceData.end() ? itr->second.deaths : 0;
    }

    void ResetDeathCount(uint32 mapId, uint32 instanceId)
    {
        auto itr = instanceData.find(std::make_pair(mapId, instanceId));
        if (itr != instanceData.end())
            itr->second.deaths = 0;
    }

    void ResetAllDeathCounts()
    {
        for (auto& [key, data] : instanceData)
            data.deaths = 0;
    }
};

// Tracks a group's Mythic+ difficulty and its members. Member records live in MpRuntimeState.
struct MpGroupData
{
    ObjectGuid groupGuid;
    MpDifficulty difficulty = MP_DIFFICULTY_NORMAL;
    std::vector<ObjectGuid> members;

    MpGroupData() = default;
    MpGroupData(ObjectGuid guid, MpDifficulty groupDifficulty) : groupGuid(guid), difficulty(groupDifficulty) { }

    // Idempotent. Returns false when the member was already present.
    bool AddMember(ObjectGuid guid)
    {
        for (ObjectGuid const& member : members)
            if (member == guid)
                return false;

        members.push_back(guid);
        return true;
    }
};

// Scaling settings for one {mapId, instanceId}. Resolve the map with sMapMgr->FindMap(mapId, instanceId) at use.
struct MpInstanceData
{
    MpDifficulty difficulty = MP_DIFFICULTY_NORMAL;

    // Enemy data
    MpMultipliers boss{};
    MpMultipliers creature{};

    // Instance settings
    bool itemRewards = false;
    uint32 deathLimits = 0;
    uint32 itemOffset = 0;

    std::string ToString() const;
};

// Per-creature Mythic+ state. `creature` is only valid on the creature's own map thread, within the current tick.
struct MpCreatureData
{
    Creature* creature = nullptr;
    uint32 mapId = 0;
    uint32 instanceId = 0;
    uint32 updateTimer = 0; // OnAllCreatureUpdate throttle
    bool scaled = false;
    DeathState lastDeathState = DeathState::Alive; // used to detect a respawn

    // AttackPower calculated based on settings
    uint32 NewAttackPower = 0;

    // New scaling multiplier based on database factors + level growth formula
    float AttackPowerScaleMultiplier = 0.0f;

    // Original information about the creature that was altered
    uint8 originalLevel = 0;
    uint32 originalInstanceHealth = 0; // health the creature would have in the instance before Mythic+ scaling

    CreatureBaseStats const* originalStats = nullptr; // static ObjectMgr data
    MpDifficulty difficulty = MP_DIFFICULTY_NORMAL;

    // Custom difficulty modifiers to creatures at higher difficulties
    std::vector<uint32> auras;
    std::vector<std::string> affixes;

    explicit MpCreatureData(Creature* creature);

    void SetScaled(bool value) { scaled = value; }
    void SetDifficulty(MpDifficulty value) { difficulty = value; }
    bool IsScaled() const { return scaled; }

    std::string ToString() const;
};

struct MpCreatureCounts
{
    uint32 scaled = 0;
    uint32 pending = 0;
};

/**
 * Thread-safe, by-value runtime state shared by all map-update threads.
 *
 * Getters return copies; Update* run the callback under the exclusive lock. Callbacks must only touch the
 * record they are given: no sMpState calls (the lock is not recursive), no logging, no other outside code.
 */
class MpRuntimeState
{
public:
    static MpRuntimeState* instance();

    MpRuntimeState(MpRuntimeState const&) = delete;
    MpRuntimeState& operator=(MpRuntimeState const&) = delete;

    std::optional<MpPlayerData> GetPlayerData(ObjectGuid guid) const;
    void SetPlayerData(ObjectGuid guid, MpPlayerData data);
    template<typename Fn> bool UpdatePlayerData(ObjectGuid guid, Fn&& fn);   // fn(MpPlayerData&), exclusive lock
    void RemovePlayerData(ObjectGuid guid);

    std::optional<MpGroupData> GetGroupData(ObjectGuid groupGuid) const;
    void SetGroupData(Group* group, MpGroupData data);                      // base AddGroupData merge semantics
    template<typename Fn> bool UpdateGroupData(ObjectGuid groupGuid, Fn&& fn);
    void RemoveGroupData(ObjectGuid groupGuid);
    uint32 GetGroupDeaths(ObjectGuid groupGuid, uint32 mapId, uint32 instanceId) const;

    std::optional<MpInstanceData> GetInstanceData(uint32 mapId, uint32 instanceId) const;
    void SetInstanceData(uint32 mapId, uint32 instanceId, MpInstanceData data);
    void RemoveInstanceData(uint32 mapId, uint32 instanceId);

    std::optional<MpCreatureData> GetCreatureData(ObjectGuid guid) const;
    void SetCreatureData(ObjectGuid guid, MpCreatureData data);
    template<typename Fn> bool UpdateCreatureData(ObjectGuid guid, Fn&& fn);
    void RemoveCreatureData(ObjectGuid guid);
    std::vector<ObjectGuid> GetInstanceCreatureGuids(uint32 mapId, uint32 instanceId, bool unscaledOnly) const;
    MpCreatureCounts CountInstanceCreatures(uint32 mapId, uint32 instanceId) const;

private:
    MpRuntimeState() = default;
    ~MpRuntimeState() = default;

    template<typename Records, typename Key, typename Fn>
    bool UpdateRecord(Records& records, Key const& key, Fn&& fn)
    {
        std::unique_lock lock(_lock);
        auto itr = records.find(key);
        if (itr == records.end())
            return false;

        fn(itr->second);
        return true;
    }

    mutable std::shared_mutex _lock;
    std::unordered_map<ObjectGuid, MpPlayerData> _players;
    std::unordered_map<ObjectGuid, MpGroupData> _groups;
    std::map<std::pair<uint32, uint32>, MpInstanceData> _instances;
    std::unordered_map<ObjectGuid, MpCreatureData> _creatures;
};

template<typename Fn>
bool MpRuntimeState::UpdatePlayerData(ObjectGuid guid, Fn&& fn)
{
    return UpdateRecord(_players, guid, std::forward<Fn>(fn));
}

template<typename Fn>
bool MpRuntimeState::UpdateGroupData(ObjectGuid groupGuid, Fn&& fn)
{
    return UpdateRecord(_groups, groupGuid, std::forward<Fn>(fn));
}

template<typename Fn>
bool MpRuntimeState::UpdateCreatureData(ObjectGuid guid, Fn&& fn)
{
    return UpdateRecord(_creatures, guid, std::forward<Fn>(fn));
}

#define sMpState MpRuntimeState::instance()

#endif // MP_RUNTIME_STATE_H
