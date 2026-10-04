#ifndef MP_CONFIG_H
#define MP_CONFIG_H

#include "Define.h"
#include "MpTypes.h"

#include <string>

struct MpTierConfig
{
    MpMultipliers dungeon;
    MpMultipliers boss;
    uint32 deathAllowance;
    uint32 itemOffset;
    uint32 diminishingThreshold;
};

// Validated module settings. The defaults in MpConfig.cpp are the single source of truth;
// conf/mod-mythic-plus.conf.dist mirrors them and mod-mythic-plus.cmake checks the two stay in sync.
class MpConfig
{
public:
    static MpConfig* instance();

    // Reads every setting. Invalid values fall back to the default and log event=config_invalid.
    void Load();

    bool enabled = true;
    bool enableItemRewards = true;
    bool enableDeathLimits = true;
    MpTierConfig mythic{}, legendary{}, ascendant{};
    float diminishingExponent = 0.96f;
    float elementalMeleeReducer = 0.5f;
    float normalEnemyReducer = 0.5f;
    float nonCreatureSpellReducer = 0.5f;

    // nullptr for NORMAL/HEROIC/EPIC
    MpTierConfig const* GetTier(MpDifficulty d) const;
    uint32 GetWarningCount() const;
    // Comma-separated, empty if none
    std::string const& GetInvalidKeys() const;
};

#define sMpConfig MpConfig::instance()

#endif
