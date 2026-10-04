#include "MpConfig.h"
#include "Config.h"
#include "MpLog.h"
#include "StringConvert.h"

MpConfig* MpConfig::instance()
{
    static MpConfig inst;
    return &inst;
}

namespace
{
    uint32 sWarnings = 0;
    std::string sInvalidKeys;

    void Invalid(char const* key, std::string const& raw, char const* rule, std::string const& def)
    {
        ++sWarnings;
        if (!sInvalidKeys.empty())
            sInvalidKeys += ',';
        sInvalidKeys += key;
        MpLog::Warn(MpLog::Area::Config, "event=config_invalid key={} value=\"{}\" rule=\"{}\" default={}",
            key, raw, rule, def);
    }

    // Reads a numeric key. Absent -> default. Unparsable or failing the rule -> default + config_invalid.
    template<typename T, typename Pred>
    T Read(char const* key, T def, Pred valid, char const* rule)
    {
        std::string raw = sConfigMgr->GetOption<std::string>(key, "", false);
        if (raw.empty())
            return def;
        Optional<T> value = Acore::StringTo<T>(raw);
        if (!value || !valid(*value))
        {
            Invalid(key, raw, rule, fmt::format("{}", def));
            return def;
        }
        return *value;
    }

    float Mult(char const* key, float def) { return Read<float>(key, def, [](float v) { return v > 0.0f; }, "> 0"); }
    uint8 Level(char const* key, uint32 def)
    {
        return uint8(Read<uint32>(key, def, [](uint32 v) { return v >= 1 && v <= 255; }, "1-255"));
    }
    uint32 Positive(char const* key, uint32 def)
    {
        return Read<uint32>(key, def, [](uint32 v) { return v > 0; }, "> 0");
    }
    uint32 Any(char const* key, uint32 def) { return Read<uint32>(key, def, [](uint32) { return true; }, "uint32"); }
    bool Flag(char const* key, bool def) { return sConfigMgr->GetOption<bool>(key, def); }
}

void MpConfig::Load()
{
    sWarnings = 0;
    sInvalidKeys.clear();
    enabled           = Flag("MythicPlus.Enabled", true);
    enableItemRewards = Flag("MythicPlus.EnableItemRewards", true);
    enableDeathLimits = Flag("MythicPlus.EnableDeathLimits", true);

    mythic.dungeon = {
        .health     = Mult("MythicPlus.Mythic.DungeonHealth", 1.25f),
        .melee      = Mult("MythicPlus.Mythic.DungeonMelee", 1.25f),
        .baseDamage = Mult("MythicPlus.Mythic.DungeonBaseDamage", 1.5f),
        .spell      = Mult("MythicPlus.Mythic.DungeonSpell", 1.15f),
        .armor      = Mult("MythicPlus.Mythic.DungeonArmor", 1.25f),
        .avgLevel   = Level("MythicPlus.Mythic.DungeonAvgLevel", 83)
    };
    mythic.boss = {
        .health     = Mult("MythicPlus.Mythic.DungeonBossHealth", 1.5f),
        .melee      = Mult("MythicPlus.Mythic.DungeonBossMelee", 1.35f),
        .baseDamage = Mult("MythicPlus.Mythic.DungeonBossBaseDamage", 2.0f),
        .spell      = Mult("MythicPlus.Mythic.DungeonBossSpell", 1.25f),
        .armor      = Mult("MythicPlus.Mythic.DungeonBossArmor", 1.35f),
        .avgLevel   = Level("MythicPlus.Mythic.DungeonBossLevel", 85)
    };

    legendary.dungeon = {
        .health     = Mult("MythicPlus.Legendary.DungeonHealth", 2.25f),
        .melee      = Mult("MythicPlus.Legendary.DungeonMelee", 2.25f),
        .baseDamage = Mult("MythicPlus.Legendary.DungeonBaseDamage", 2.75f),
        .spell      = Mult("MythicPlus.Legendary.DungeonSpell", 2.25f),
        .armor      = Mult("MythicPlus.Legendary.DungeonArmor", 2.25f),
        .avgLevel   = Level("MythicPlus.Legendary.DungeonAvgLevel", 85)
    };
    legendary.boss = {
        .health     = Mult("MythicPlus.Legendary.DungeonBossHealth", 2.25f),
        .melee      = Mult("MythicPlus.Legendary.DungeonBossMelee", 2.25f),
        .baseDamage = Mult("MythicPlus.Legendary.DungeonBossBaseDamage", 4.0f),
        .spell      = Mult("MythicPlus.Legendary.DungeonBossSpell", 2.25f),
        .armor      = Mult("MythicPlus.Legendary.DungeonBossArmor", 3.25f),
        .avgLevel   = Level("MythicPlus.Legendary.DungeonBossLevel", 87)
    };

    ascendant.dungeon = {
        .health     = Mult("MythicPlus.Ascendant.DungeonHealth", 3.25f),
        .melee      = Mult("MythicPlus.Ascendant.DungeonMelee", 3.25f),
        .baseDamage = Mult("MythicPlus.Ascendant.DungeonBaseDamage", 4.75f),
        .spell      = Mult("MythicPlus.Ascendant.DungeonSpell", 3.25f),
        .armor      = Mult("MythicPlus.Ascendant.DungeonArmor", 3.25f),
        .avgLevel   = Level("MythicPlus.Ascendant.DungeonAvgLevel", 87)
    };
    ascendant.boss = {
        .health     = Mult("MythicPlus.Ascendant.DungeonBossHealth", 3.25f),
        .melee      = Mult("MythicPlus.Ascendant.DungeonBossMelee", 3.25f),
        .baseDamage = Mult("MythicPlus.Ascendant.DungeonBossBaseDamage", 6.0f),
        .spell      = Mult("MythicPlus.Ascendant.DungeonBossSpell", 3.25f),
        .armor      = Mult("MythicPlus.Ascendant.DungeonBossArmor", 3.25f),
        .avgLevel   = Level("MythicPlus.Ascendant.DungeonBossLevel", 90)
    };

    mythic.deathAllowance    = Any("MythicPlus.Mythic.DeathAllowance", 1000);
    legendary.deathAllowance = Any("MythicPlus.Legendary.DeathAllowance", 30);
    ascendant.deathAllowance = Any("MythicPlus.Ascendant.DeathAllowance", 15);
    mythic.itemOffset    = Positive("MythicPlus.Mythic.ItemOffset", 20000000);
    legendary.itemOffset = Positive("MythicPlus.Legendary.ItemOffset", 21000000);
    ascendant.itemOffset = Positive("MythicPlus.Ascendant.ItemOffset", 22000000);
    mythic.diminishingThreshold    = Any("MythicPlus.DiminishingThreshold.Mythic", 10000);
    legendary.diminishingThreshold = Any("MythicPlus.DiminishingThreshold.Legendary", 20000);
    ascendant.diminishingThreshold = Any("MythicPlus.DiminishingThreshold.Ascendant", 40000);
    diminishingExponent     = Mult("MythicPlus.DiminishingExponent", 0.96f);
    elementalMeleeReducer   = Mult("MythicPlus.ElementalMeleeReducer", 0.50f);
    normalEnemyReducer      = Mult("MythicPlus.NormalEnemyReducer", 0.50f);
    nonCreatureSpellReducer = Mult("MythicPlus.NonCreatureSpellReducer", 0.50f);
}

MpTierConfig const* MpConfig::GetTier(MpDifficulty d) const
{
    switch (d)
    {
        case MP_DIFFICULTY_MYTHIC:    return &mythic;
        case MP_DIFFICULTY_LEGENDARY: return &legendary;
        case MP_DIFFICULTY_ASCENDANT: return &ascendant;
        default:                      return nullptr;
    }
}

uint32 MpConfig::GetWarningCount() const { return sWarnings; }
std::string const& MpConfig::GetInvalidKeys() const { return sInvalidKeys; }
