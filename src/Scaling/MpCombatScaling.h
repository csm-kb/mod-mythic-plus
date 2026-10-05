#ifndef MP_COMBAT_SCALING_H
#define MP_COMBAT_SCALING_H

#include "Define.h"
#include "MpScaler.h"

class SpellInfo;
class Unit;

/**
 * Scales the damage and healing Mythic+ creatures deal, for the UnitScript damage/heal hooks.
 *
 * Callers have already checked that target and attacker are set and that target's map is eligible.
 *
 * This is a singleton instance that can be accessed through sMpCombatScaling.
 */
class MpCombatScaling
{
public:
    static MpCombatScaling* instance()
    {
        static MpCombatScaling instance;
        return &instance;
    }

    MpCombatScaling(MpCombatScaling const&) = delete;
    MpCombatScaling& operator=(MpCombatScaling const&) = delete;

    // Periodic aura tick: heal effects scale as a HoT, everything else as DoT spell damage
    void ScalePeriodicTick(Unit* target, Unit* attacker, uint32& damage, SpellInfo const* spellInfo);

    /**
     * @brief This functions processes spell damage for DOTs and Direct Damage Spells it
     * handles special cases for Melee scaling spells and AP scaling spells also so they
     * are not scaled up twice and murder all my friends
     *
     * Defined in MpCombatScaling.cpp, which explicitly instantiates DamageType int32 for other files.
     */
    template<typename DamageType>
    void ProcessSpellDamage(Unit* target, Unit* attacker, DamageType& damage, SpellInfo const* spellInfo,
        MpScaler::MP_UNIT_EVENT_TYPE eventType);

    // Returns the Mythic+ scaled damage or heal for one hit
    uint32 ScaleIncomingDmgHeal(MpScaler::MP_UNIT_EVENT_TYPE eventType, Unit* target, Unit* attacker,
        uint32 damageOrHeal, SpellInfo const* spellInfo = nullptr);

private:
    MpCombatScaling() { }
    ~MpCombatScaling() { }

    /**
     * Handles damage from non-creature sources (GameObjects, Players, etc.)
     * @tparam DamageType Type of damage (int32/uint32)
     * @param target Target of the damage
     * @param attacker The non-creature attacker
     * @param damage Reference to damage value (will be modified)
     * @param spellInfo The spell being cast
     * @param eventType Type of event (spell/melee/etc)
     */
    template<typename DamageType>
    void HandleNonCreatureAttacker(Unit* target, Unit* attacker, DamageType& damage, SpellInfo const* spellInfo,
        MpScaler::MP_UNIT_EVENT_TYPE eventType);

    // Helper function to determine if a spell scales with Attack Power
    bool IsAttackPowerScalingSpell(SpellInfo const* spellInfo);
};

#define sMpCombatScaling MpCombatScaling::instance()

#endif // MP_COMBAT_SCALING_H
