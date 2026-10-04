#include "MpRepository.h"
#include "CharacterDatabase.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "MpLog.h"
#include "Player.h"

#include <string_view>

MpRepository* MpRepository::instance()
{
    static MpRepository instance;
    return &instance;
}

MpScaleFactor MpRepository::GetScaleFactor(int32 mapId, int32 difficulty) const
{
    {
        std::shared_lock lock(_lock);
        auto itr = _scaleFactors.find(GetScaleFactorKey(mapId, difficulty));
        if (itr != _scaleFactors.end())
            return itr->second;
    }

    // Just send back untouched bonus database will override.
    return MpScaleFactor
    {
        .meleeBonus = 1.0f,
        .spellBonus = 1.0f,
        .healBonus = 1.0f,
        .healthBonus = 1.0f
    };
}

float MpRepository::GetHealthScaleFactor(int32 mapId, int32 difficulty) const
{
    return GetScaleFactor(mapId, difficulty).healthBonus;
}

float MpRepository::GetMeleeScaleFactor(int32 mapId, int32 difficulty) const
{
    return GetScaleFactor(mapId, difficulty).meleeBonus;
}

float MpRepository::GetSpellScaleFactor(int32 mapId, int32 difficulty) const
{
    return GetScaleFactor(mapId, difficulty).spellBonus;
}

float MpRepository::GetHealScaleFactor(int32 mapId, int32 difficulty) const
{
    return GetScaleFactor(mapId, difficulty).healBonus;
}

uint32 MpRepository::GetPlayerHealthAvg(uint32 level) const
{
    std::shared_lock lock(_lock);
    auto itr = _playerHealthAvg.find(level);
    return itr != _playerHealthAvg.end() ? itr->second : 0;
}

void MpRepository::SetHealthScaleFactor(int32 mapId, int32 difficulty, float newValue)
{
    UpdateScaleFactor(mapId, difficulty, [newValue](MpScaleFactor& factor) { factor.healthBonus = newValue; });
}

void MpRepository::SetMeleeScaleFactor(int32 mapId, int32 difficulty, float newValue)
{
    UpdateScaleFactor(mapId, difficulty, [newValue](MpScaleFactor& factor) { factor.meleeBonus = newValue; });
}

void MpRepository::SetSpellScaleFactor(int32 mapId, int32 difficulty, float newValue)
{
    UpdateScaleFactor(mapId, difficulty, [newValue](MpScaleFactor& factor) { factor.spellBonus = newValue; });
}

// The query runs without the lock; the loaded table replaces the old one in one step.
int32 MpRepository::LoadScaleFactors()
{
    std::map<std::pair<int32, int32>, MpScaleFactor> scaleFactors;

    //                                                 0       1          2              3        4        5
    QueryResult result = WorldDatabase.Query(
        "SELECT mapId, melee_bonus, spell_bonus, heal_bonus, hp_bonus, difficulty FROM mp_scale_factors");
    if (!result)
    {
        {
            std::unique_lock lock(_lock);
            _scaleFactors.clear();
        }

        MpLog::Error(MpLog::Area::Instance, "Failed to load mythic scale factors from database");
        return 0;
    }

    do
    {
        Field* fields = result->Fetch();
        uint32 mapId = fields[0].Get<uint32>();
        float meleeBonus = fields[1].Get<float>();
        float spellBonus = fields[2].Get<float>();
        float healBonus = fields[3].Get<float>();
        float healthBonus = fields[4].Get<float>();
        int32 difficulty = fields[5].Get<int32>();

        MpScaleFactor scaleFactor = {
            .meleeBonus = meleeBonus,
            .spellBonus = spellBonus,
            .healBonus = healBonus,
            .healthBonus = healthBonus
        };

        scaleFactors.emplace(GetScaleFactorKey(mapId, difficulty), scaleFactor);
    } while (result->NextRow());

    int32 size = int32(scaleFactors.size());

    std::unique_lock lock(_lock);
    _scaleFactors = std::move(scaleFactors);
    return size;
}

// The query runs without the lock; the loaded table replaces the old one in one step.
void MpRepository::LoadPlayerHealthAvg()
{
    std::unordered_map<uint32, uint32> playerHealthAvg;

    std::string_view query = R"(
        SELECT
            Level,
            ROUND(CASE
                WHEN Level BETWEEN 1  AND 30 THEN ((AVG(Stamina) - 20) * 10 + AVG(BaseHP) + 20) * 1.5
                WHEN Level BETWEEN 31 AND 50 THEN ((AVG(Stamina) - 20) * 10 + AVG(BaseHP) + 20) * 1.7
                WHEN Level BETWEEN 51 AND 59 THEN ((AVG(Stamina) - 20) * 10 + AVG(BaseHP) + 20) * 2.0
                WHEN Level BETWEEN 60 AND 69 THEN ((AVG(Stamina) - 20) * 10 + AVG(BaseHP) + 20) * 2.3
                WHEN Level BETWEEN 70 AND 79 THEN ((AVG(Stamina) - 20) * 10 + AVG(BaseHP) + 20) * 2.6
                WHEN Level BETWEEN 80 AND 84 THEN ((AVG(Stamina) - 20) * 10 + AVG(BaseHP) + 20) * 3.0
                WHEN Level >= 85           THEN ((AVG(Stamina) - 20) * 10 + AVG(BaseHP) + 20) * 4.0
                ELSE                            ((AVG(Stamina) - 20) * 10 + AVG(BaseHP) + 20)
            END) AS BaseHealth
        FROM
            player_class_stats
        GROUP BY
            Level
        ORDER BY
            Level;
    )";

    bool loaded = false;
    if (QueryResult result = WorldDatabase.Query(query.data()))
    {
        loaded = true;
        do
        {
            Field* fields = result->Fetch();
            uint32 level = fields[0].Get<uint32>();
            uint32 baseHealth = fields[1].Get<uint32>();

            playerHealthAvg[level] = baseHealth;
        } while (result->NextRow());
    }

    {
        std::unique_lock lock(_lock);
        _playerHealthAvg = std::move(playerHealthAvg);
    }

    if (!loaded)
        MpLog::Error(MpLog::Area::Instance, "Failed to load player health averages from database");
}

/**
 * Database Calls below for storing player data.
 * @todo refactor to use prepared statements
*/
void MpRepository::DBUpdatePlayerInstanceData(ObjectGuid playerGuid, MpDifficulty difficulty, uint32 mapId,
    uint32 instanceId, uint32 deaths)
{
    if (!playerGuid)
    {
        MpLog::Error(MpLog::Area::Instance, "DBAddPlayerData called with invalid playerData");
        return;
    }

    CharacterDatabase.Execute(
        "REPLACE INTO mp_player_instance_data (guid, difficulty, mapId, instanceId, deaths) VALUES ({},{},{},{},{}) ",
        playerGuid.GetCounter(),
        difficulty,
        mapId,
        instanceId,
        deaths
    );
}

void MpRepository::DBAddPlayerDeath(Player* player)
{
    if (!player)
    {
        MpLog::Error(MpLog::Area::Instance, "DBAddPlayerDeath called with invalid player");
        return;
    }

    CharacterDatabase.Execute(
        "UPDATE mp_player_instance_data SET deaths = deaths + 1 WHERE guid = {} and mapId = {} and instanceId = {}",
        player->GetGUID().GetCounter(),
        player->GetMapId(),
        player->GetInstanceId()
    );
}

// Logs death for player that occurs by a creature directly.
void MpRepository::DBAddPlayerDeath(Player* player, Creature* creature, MpDifficulty difficulty)
{
    if (!player)
    {
        MpLog::Error(MpLog::Area::Instance, "DBAddPlayerDeath called with invalid player");
        return;
    }

    CharacterDatabase.Execute(
        "UPDATE mp_player_instance_data SET deaths = deaths + 1 WHERE guid = {} and mapId = {} and instanceId = {}",
        player->GetGUID().GetCounter(),
        player->GetMapId(),
        player->GetInstanceId()
    );

    CharacterDatabase.Execute(
        "INSERT INTO mp_player_death_stats (guid, creatureEntry, difficulty, numDeaths, lastUpdated) "
        "VALUES ({}, {}, {}, 1, CURRENT_TIMESTAMP) "
        "ON DUPLICATE KEY UPDATE numDeaths = numDeaths + 1, lastUpdated = CURRENT_TIMESTAMP",
        player->GetGUID().GetCounter(),
        creature->GetEntry(),
        difficulty
    );
}

void MpRepository::DBUpdateGroupData(ObjectGuid groupGuid, MpDifficulty difficulty, uint32 mapId,
    uint32 instanceId, uint32 deaths)
{
    if (!groupGuid)
    {
        MpLog::Error(MpLog::Area::Instance, "DBUpdateGroupData called with invalid groupGuid");
        return;
    }

    CharacterDatabase.Execute(
        "REPLACE INTO mp_group_data (groupId, difficulty, mapId, instanceId, deaths) VALUES ({},{},{},{},{}) ",
        groupGuid.GetCounter(),
        difficulty,
        mapId,
        instanceId,
        deaths
    );
}

void MpRepository::DBAddGroupDeath(Group* group, uint32 mapId, uint32 instanceId, MpDifficulty difficulty)
{
    if (!group)
    {
        MpLog::Error(MpLog::Area::Instance, "DBAddGroupDeath called with invalid group");
        return;
    }

    if (!difficulty)
    {
        MpLog::Error(MpLog::Area::Instance, "DBAddGroupDeath called with invalid difficulty");
        return;
    }

    if (!mapId || !instanceId)
    {
        MpLog::Error(MpLog::Area::Instance, "DBAddGroupDeath called with invalid mapId or instanceId");
        return;
    }

    CharacterDatabase.Execute(
        "UPDATE mp_group_data SET deaths = deaths + 1 "
        "WHERE groupId = {} and mapId = {} and instanceId = {} and difficulty = {}",
        group->GetGUID().GetCounter(),
        mapId,
        instanceId,
        difficulty
    );
}

void MpRepository::DBRemovePlayerInstanceData(uint32 instanceId)
{
    if (!instanceId)
    {
        MpLog::Error(MpLog::Area::Instance,
            "DBRemovePlayerInstanceData: missing instanceId to remove player instance ");
        return;
    }

    CharacterDatabase.Execute("DELETE FROM mp_player_instance_data WHERE instanceId = {} ", instanceId);
}

void MpRepository::DBRemoveGroupData(ObjectGuid groupGuid)
{
    if (!groupGuid)
    {
        MpLog::Error(MpLog::Area::Instance, "DBRemoveGroupData called with invalid groupGuid");
        return;
    }

    CharacterDatabase.Execute("DELETE FROM mp_group_data WHERE groupId = {} ", groupGuid.GetCounter());
}

// Remove instance data using the instanceId
void MpRepository::DBRemoveGroupInstanceData(uint32 instanceId)
{
    if (!instanceId)
    {
        MpLog::Error(MpLog::Area::Instance, "DBRemoveGroupData called with invalid groupGuid");
        return;
    }

    CharacterDatabase.Execute("DELETE FROM mp_group_data WHERE instanceId = {} ", instanceId);
}
