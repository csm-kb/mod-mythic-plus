#ifndef MP_REPOSITORY_H
#define MP_REPOSITORY_H

#include "Define.h"
#include "MpTypes.h"
#include "ObjectGuid.h"

#include <map>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <utility>

class Creature;
class Group;
class Player;

/**
 * Database access and the tables loaded from it (scale factors, player health averages).
 *
 * The loaded tables are written by Load* / Set*ScaleFactor (world thread, commands) and read from map threads,
 * so they sit behind a std::shared_mutex. DB queries and logging never run while it is held.
 */
class MpRepository
{
public:
    static MpRepository* instance();

    MpRepository(MpRepository const&) = delete;
    MpRepository& operator=(MpRepository const&) = delete;

    // Scale factors are used to determine a base bonus for enemies based on the instance difficulty
    float GetHealthScaleFactor(int32 mapId, int32 difficulty) const;
    float GetMeleeScaleFactor(int32 mapId, int32 difficulty) const;
    float GetSpellScaleFactor(int32 mapId, int32 difficulty) const;
    float GetHealScaleFactor(int32 mapId, int32 difficulty) const;
    MpScaleFactor GetScaleFactor(int32 mapId, int32 difficulty) const;

    void SetMeleeScaleFactor(int32 mapId, int32 difficulty, float value);
    void SetHealthScaleFactor(int32 mapId, int32 difficulty, float value);
    void SetSpellScaleFactor(int32 mapId, int32 difficulty, float value);
    void SetHealScaleFactor(int32 mapId, int32 difficulty, float value);

    // Retrieves the average player hp pool for a player level
    uint32 GetPlayerHealthAvg(uint32 level) const;

    // Used at initial server load
    int32 LoadScaleFactors();

    // Load the player health average from the database
    void LoadPlayerHealthAvg();

    // Database API calls
    void DBUpdatePlayerInstanceData(ObjectGuid playerGuid, MpDifficulty difficulty, uint32 mapId = 0,
        uint32 instanceId = 0, uint32 deaths = 0);

    void DBResetPlayerDeaths(Player* player);
    void DBAddPlayerDeath(Player* player, Creature* killer, MpDifficulty difficulty);
    void DBAddPlayerDeath(Player* player);

    void DBRemovePlayerData(ObjectGuid playerGuid);
    void DBRemovePlayerInstanceData(uint32 instanceId);
    void DBRemoveGroupInstanceData(uint32 instanceId);
    void DBUpdateGroupData(ObjectGuid groupGuid, MpDifficulty difficulty, uint32 mapId, uint32 instanceId,
        uint32 deaths);
    void DBUpdateGroupTimerDeaths(ObjectGuid groupGuid, uint32 mapId, uint32 instanceId, uint32 timer,
        uint32 deaths);
    void DBRemoveGroupData(ObjectGuid groupGuid);
    void DBAddGroupDeath(Group* group, uint32 mapId, uint32 instanceId, MpDifficulty difficulty);

private:
    MpRepository() = default;
    ~MpRepository() = default;

    static std::pair<int32, int32> GetScaleFactorKey(int32 mapId, int32 difficulty)
    {
        return std::make_pair(mapId, difficulty);
    }

    // Applies fn to an existing scale factor entry; missing entries are left alone.
    template<typename Fn>
    void UpdateScaleFactor(int32 mapId, int32 difficulty, Fn&& fn)
    {
        std::unique_lock lock(_lock);
        auto itr = _scaleFactors.find(GetScaleFactorKey(mapId, difficulty));
        if (itr != _scaleFactors.end())
            fn(itr->second);
    }

    mutable std::shared_mutex _lock;

    // Mimics the normal-to-heroic scaling pattern, {mapId, difficulty} (loaded at server start)
    std::map<std::pair<int32, int32>, MpScaleFactor> _scaleFactors;

    // Player level -> average health for that level. Used to scale spells against percentages of a player's
    // health pool so spell damage and creature healing scale more consistently.
    std::unordered_map<uint32, uint32> _playerHealthAvg;
};

#define sMpRepo MpRepository::instance()

#endif // MP_REPOSITORY_H
