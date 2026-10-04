#ifndef MP_TYPES_H
#define MP_TYPES_H

#include "Define.h"

#include <string>

// Persisted in DB (mp_scale_factors.difficulty, mp_group_data, mp_player_instance_data,
// mp_player_death_stats). Never renumber.
enum MpDifficulty
{
    MP_DIFFICULTY_NORMAL    = 0,
    MP_DIFFICULTY_HEROIC    = 1,
    MP_DIFFICULTY_EPIC      = 2,
    MP_DIFFICULTY_MYTHIC    = 3,
    MP_DIFFICULTY_LEGENDARY = 4,
    MP_DIFFICULTY_ASCENDANT = 5
};
static_assert(MP_DIFFICULTY_MYTHIC == 3 && MP_DIFFICULTY_LEGENDARY == 4 && MP_DIFFICULTY_ASCENDANT == 5,
    "MpDifficulty values are persisted in the database");

inline char const* MpDifficultyName(MpDifficulty d)
{
    switch (d)
    {
        case MP_DIFFICULTY_NORMAL:    return "Normal";
        case MP_DIFFICULTY_HEROIC:    return "Heroic";
        case MP_DIFFICULTY_EPIC:      return "Epic";
        case MP_DIFFICULTY_MYTHIC:    return "Mythic";
        case MP_DIFFICULTY_LEGENDARY: return "Legendary";
        case MP_DIFFICULTY_ASCENDANT: return "Ascendant";
    }
    return "Unknown";
}

/**
 * @brief Struct used for internal scaling of mythic+ difficulty. Fine-tuned in
 * database.
 */
struct MpScaleFactor
{
    float meleeBonus;
    float spellBonus;
    float healBonus;
    float healthBonus;

    std::string ToString() const {
        return "MpScaleFactor: { meleeBonus: " + std::to_string(meleeBonus) +
               ", healthBonus: " + std::to_string(healthBonus) +
               ", spellBonus: " + std::to_string(spellBonus) +
               ", healBonus: " + std::to_string(healBonus) + "}";
    }

};

struct MpMultipliers
{
    float health;
    float melee;
    float baseDamage;
    float spell;
    float armor;
    uint8 avgLevel;

    std::string ToString() const {
    return "MpMultipliers: { health: " + std::to_string(health) +
            ", melee: " + std::to_string(melee) +
            ", melee: " + std::to_string(baseDamage) +
            ", spell: " + std::to_string(spell) +
            ", armor: " + std::to_string(armor) +
            ", avgLevel: " + std::to_string(avgLevel) + " }";
    }
};

#endif
