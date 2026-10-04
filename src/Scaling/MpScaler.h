#ifndef MP_SCALER_H
#define MP_SCALER_H

#include "Creature.h"
#include "Define.h"
#include "Map.h"
#include "MpRepository.h"
#include "MpRuntimeState.h"
#include "Player.h"
#include "SpellInfo.h"
#include "Unit.h"

/**
 * Scales instance creatures and their damage/heal output for Mythic+ instances.
 *
 * Runtime state lives in MpRuntimeState (sMpState); database access and loaded tables in
 * MpRepository (sMpRepo).
 *
 * This is a singleton instance that can be accessed through sMpScaler.
 */
class MpScaler
{
public:
    static MpScaler* instance()
    {
        static MpScaler instance;
        return &instance;
    }

    MpScaler(MpScaler const&) = delete;
    MpScaler& operator=(MpScaler const&) = delete;

    enum MP_UNIT_EVENT_TYPE
    {
        UNIT_EVENT_MELEE,
        UNIT_EVENT_HEAL,
        UNIT_EVENT_DOT,
        UNIT_EVENT_SPELL,
        UNIT_EVENT_HOT
    };

    // Map is eligible for mythic+ scaling
    bool IsMapEligible(Map* map);

    // If a player difficulty is set that is eligible for mythic+ scaling
    bool IsDifficultySet(Player const* player);

    // Is it a scaled creature that is being healed
    bool EligibleHealTarget(Unit* target);

    // Validates if the target of an attack should receive mythic+ damage/heal/dot scaling
    bool EligibleDamageTarget(Unit* target);

    // The creature should be given Mythic+ scaling and powers check for pets, npcs, etc
    bool IsCreatureEligible(Creature* creature);

    // Adds the creature's record to sMpState if it is eligible to be scaled
    void AddCreatureForScaling(Creature* creature);

    /**
     * Creatures are added to an instance before a player enter event is fired
     * therefore it is necessary to scan the instance creature information and
     * and scale any creatures that were loaded before the first player using
     * the instance data from the group settings.
     */
    void ScaleRemaining(Player* player, MpInstanceData const& instanceData);

    // Rescales all creatures for an instance based on set data
    void ScaleAll(Player* player, MpInstanceData const& instanceData);

    // Stores a scaled record in sMpState, then scales the creature using instancedata
    void AddScaledCreature(Creature* creature, MpInstanceData const& instanceData);

    // Scales the creature based on the level and the creature base stats
    void ScaleCreature(uint8 level, Creature* creature, MpMultipliers const* multipliers, MpDifficulty difficulty);

    // Scales a damage spell up based on the level increase
    int32 ScaleDamageSpell(SpellInfo const* spellInfo, uint32 damage, MpCreatureData const* creatureData,
        Creature* creature, Unit* target, float damageMultiplier);

    // This scales a heal spell up based on the how much % the original heal spell was
    int32 ScaleHealSpell(SpellInfo const* spellInfo, uint32 heal, MpCreatureData const* creatureData,
        Creature* creature, Creature* target, float healMultiplier);

    // Calculate spell damage based on player health pools
    int32 CalculateSpellDamage(uint32 baseDamage, int originalLevel, int targetLevel);

    // Calculate heal scaling based on creature health percentages
    int32 CalculateHealScaling(uint32 baseHeal, uint32 originalHealth, uint32 currentMaxHealth);

    static bool IsFinalBoss(Creature* creature);

private:
    MpScaler() { }
    ~MpScaler() { }

    static float GetTypeHealthModifier(int32 rank);
    static uint32 CalculateNewHealth(Creature* creature, CreatureTemplate const* cInfo, uint32 mapId,
        MpDifficulty difficulty, uint32 origHealth, float confHPMod);
};

#define sMpScaler MpScaler::instance()

#endif // MP_SCALER_H
