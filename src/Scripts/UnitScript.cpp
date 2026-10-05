#include "MpCombatScaling.h"
#include "MpScaler.h"
#include "ScriptMgr.h"

class MythicPlus_UnitScript : public UnitScript
{
public:
    MythicPlus_UnitScript() : UnitScript("MythicPlus_UnitScript", true) { }

    void ModifyPeriodicDamageAurasTick(Unit* target, Unit* attacker, uint32& damage,
        SpellInfo const* spellInfo) override
    {
        if (!target || !attacker)
            return;

        if (!sMpScaler->IsMapEligible(target->GetMap()))
            return;

        sMpCombatScaling->ScalePeriodicTick(target, attacker, damage, spellInfo);
    }

    void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage, SpellInfo const* spellInfo) override
    {
        if (!target || !attacker)
            return;

        if (!sMpScaler->IsMapEligible(target->GetMap()))
            return;

        if (!sMpScaler->EligibleDamageTarget(target))
            return;

        // Use the generic ProcessSpellDamage function
        sMpCombatScaling->ProcessSpellDamage(target, attacker, damage, spellInfo, MpScaler::UNIT_EVENT_SPELL);
    }

    /**
     * Directly Modify the melee damage characters and allied creatures will
     * receive from mythic+ scaled enemies.
     */
    void ModifyMeleeDamage(Unit* target, Unit* attacker, uint32& damage) override
    {
        if (!target || !attacker)
            return;

        if (!sMpScaler->IsMapEligible(target->GetMap()))
            return;

        damage = modifyIncomingDmgHeal(MpScaler::UNIT_EVENT_MELEE, target, attacker, damage);
    }

    // When a healing spell hits a mythic+ enemy modify based on the modifiers for the difficulty
    void ModifyHealReceived(Unit* target, Unit* healer, uint32& healing, SpellInfo const* spellInfo) override
    {
        if (!target || !healer)
            return;

        if (!sMpScaler->IsMapEligible(target->GetMap()))
            return;

        healing = modifyIncomingDmgHeal(MpScaler::UNIT_EVENT_HEAL, target, healer, healing, spellInfo);
    }

private:
    // Callers have already checked that target and attacker are set and that target's map is eligible.
    uint32 modifyIncomingDmgHeal(MpScaler::MP_UNIT_EVENT_TYPE eventType, Unit* target, Unit* attacker,
        uint32 damageOrHeal, SpellInfo const* spellInfo = nullptr)
    {
        return sMpCombatScaling->ModifyIncomingDmgHeal(eventType, target, attacker, damageOrHeal, spellInfo);
    }
};

void Add_MP_UnitScripts()
{
    new MythicPlus_UnitScript();
}
