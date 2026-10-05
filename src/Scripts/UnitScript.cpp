#include "MpBots.h"
#include "MpConfig.h"
#include "MpConstants.h"
#include "MpLog.h"
#include "Player.h"
#include "MpScaler.h"
#include "ScriptMgr.h"
#include "SpellAuraEffects.h"

#include <optional>

class MythicPlus_UnitScript : public UnitScript
{
public:
    MythicPlus_UnitScript() : UnitScript("MythicPlus_UnitScript", true) { }

private:
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
        MpScaler::MP_UNIT_EVENT_TYPE eventType)
    {
        // Try to find a creature attacker from target's attackers list if we have one use it for scaling
        Unit::AttackerSet const& attackers = target->getAttackers();
        if (!attackers.empty())
        {
            if (Creature* creatureAttacker = (*attackers.begin())->ToCreature())
            {
                if (sMpState->GetCreatureData(creatureAttacker))
                    damage = static_cast<DamageType>(modifyIncomingDmgHeal(eventType, target, creatureAttacker,
                        static_cast<uint32>(damage), spellInfo)) * sMpConfig->nonCreatureSpellReducer;

                return;
            }

            MpLog::Debug(MpLog::Area::Combat,
                "====== SPELL SCALING: Non-Creature attacker - Name: {}, Spell: {}({}), Damage: {}",
                attacker->GetName(),
                spellInfo ? spellInfo->SpellName[0] : "No Spell",
                spellInfo ? spellInfo->Id : 0,
                damage);
        }

        // Otherwise (or with no attackers at all) fall back to instance-based scaling
        Map* map = target->GetMap();
        if (std::optional<MpInstanceData> instanceData = sMpState->GetInstanceData(map->GetId(), map->GetInstanceId()))
            damage = static_cast<DamageType>(damage * instanceData->creature.spell *
                sMpConfig->nonCreatureSpellReducer);
    }

    // Helper function to determine if a spell scales with Attack Power
    bool IsAttackPowerScalingSpell(SpellInfo const* spellInfo)
    {
        if (!spellInfo)
            return false;

        SpellEffectInfo const& mainEffect = spellInfo->Effects[0];

        // Check 1: Direct weapon damage effects
        bool isWeaponEffect = (mainEffect.Effect == SPELL_EFFECT_WEAPON_DAMAGE ||
                              mainEffect.Effect == SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL ||
                              mainEffect.Effect == SPELL_EFFECT_NORMALIZED_WEAPON_DMG ||
                              mainEffect.Effect == SPELL_EFFECT_WEAPON_PERCENT_DAMAGE);

        // Check 2: Damage class indicates melee/ranged (scales with AP)
        bool isMeleeOrRanged = (spellInfo->DmgClass == SPELL_DAMAGE_CLASS_MELEE ||
                               spellInfo->DmgClass == SPELL_DAMAGE_CLASS_RANGED);

        // Check 3: Requires weapon equipment
        bool requiresWeapon = (spellInfo->EquippedItemClass == ITEM_CLASS_WEAPON);

        // Check 4: Specific spell families known to scale with AP
        bool isKnownAPSpell = false;
        if (spellInfo->SpellFamilyName == SPELLFAMILY_ROGUE)
        {
            // Rogue poisons and weapon-based abilities
            isKnownAPSpell = (spellInfo->SpellFamilyFlags[0] & 0x10000) || // Deadly Poison flag
                            (spellInfo->SpellFamilyFlags[1] & 0x80000);   // Other poison flags
        }

        // Return true if any indicator suggests AP scaling
        return (isWeaponEffect || isMeleeOrRanged || requiresWeapon || isKnownAPSpell);
    }

    /**
     * @brief This functions processes spell damage for DOTs and Direct Damage Spells it
     * handles special cases for Melee scaling spells and AP scaling spells also so they
     * are not scaled up twice and murder all my friends
     *
     * @tparam DamageType
     * @param target
     * @param attacker
     * @param damage
     * @param spellInfo
     * @param eventType
     */
    template<typename DamageType>
    void ProcessSpellDamage(Unit* target, Unit* attacker, DamageType& damage, SpellInfo const* spellInfo,
        MpScaler::MP_UNIT_EVENT_TYPE eventType)
    {
        if (damage == 0)
            return;

        // If this is a special case where the attacker is not a creature
        Creature* creatureCaster = attacker->ToCreature();
        if (!creatureCaster)
            return HandleNonCreatureAttacker(target, attacker, damage, spellInfo, eventType);

        if (MpBots::IsNpcBotOrPet(attacker))
            return;

        std::optional<MpCreatureData> creatureData = sMpState->GetCreatureData(creatureCaster);
        if (!creatureData)
        {
            MpLog::Debug(MpLog::Area::Combat, "Failed to get creature data for {} in map {}", creatureCaster->GetName(),
                attacker->GetMap()->GetId());
            return;
        }

        // Check if this spell scales with Attack Power (true only with a spellInfo)
        if (IsAttackPowerScalingSpell(spellInfo))
        {
            // need another special case here to determine if a spell was not scaled up by AP meaning the incoming
            // damage is close to the original effect of the spell and therefore should instead use spell effect
            // scaling it should be no more than 15% of the original effect
            int32 baseEffect = spellInfo->Effects[0].CalcValue(attacker, nullptr, nullptr);
            bool notScaledByAP = damage <= (baseEffect * 1.15f);

            // if the effect type of the spell is not physical (aka not mitigated by armor/defense) then it needs to
            // instead have the typical spell damage multiplier applied instead of melee damage scaling
            if (!notScaledByAP &&
                (spellInfo->SchoolMask == SPELL_SCHOOL_NORMAL || spellInfo->SchoolMask == SPELL_SCHOOL_MASK_NORMAL))
            {
                uint32 meleeDamage = static_cast<uint32>(std::max(0, static_cast<int32>(damage)));

                damage = modifyIncomingDmgHeal(MpScaler::UNIT_EVENT_MELEE, target, attacker, meleeDamage);
            }
            else
            {
                // get the creatures original attack power
                SpellEffectInfo const& effect = spellInfo->Effects[0];
                uint32 spellDmg = static_cast<uint32>(effect.CalcValue(attacker, nullptr, nullptr) *
                    effect.CalcDamageMultiplier(attacker, nullptr));

                // now take the original attack power * 0.08 and add it to the spell damage
                uint32 apDmg = static_cast<uint32>(creatureData->originalStats->AttackPower * 0.10f);
                uint32 finalDmg = spellDmg + apDmg;

                damage = modifyIncomingDmgHeal(MpScaler::UNIT_EVENT_SPELL, target, attacker, finalDmg, spellInfo);

                // need to take into consideration if this is a stacking spell and multiply the final damage by the
                // number of stacks
                if (spellInfo->AttributesEx3 & SPELL_ATTR3_DOT_STACKING_RULE)
                    if (Aura* aura = target->GetAura(spellInfo->Id, attacker->GetGUID()))
                        damage *= uint32(aura->GetStackAmount());
            }
            return;
        }

        // Reverse the CalcValue scaling to get original base damage
        int32 originalDamage = static_cast<int32>(damage);

        if (creatureData->originalLevel < 70)
        {
            CreatureTemplate const* cInfo = creatureCaster->GetCreatureTemplate();

            // Get the scaling factors used in CalcValue
            CreatureBaseStats const* pCBS = sObjectMgr->GetCreatureBaseStats(creatureCaster->GetLevel(),
                creatureCaster->getClass());
            float CBSPowerCreature = pCBS->BaseDamage[cInfo->expansion];

            uint32 tempLevel = spellInfo->SpellLevel;
            if (tempLevel == 0)
                tempLevel = creatureData->originalLevel;

            CreatureBaseStats const* spellCBS = sObjectMgr->GetCreatureBaseStats(tempLevel, creatureCaster->getClass());
            float CBSPowerSpell = spellCBS->BaseDamage[cInfo->expansion];

            // Reverse the CalcValue scaling: originalDamage = scaledDamage / (CBSPowerCreature / CBSPowerSpell)
            if (CBSPowerCreature > 0.0f)
                originalDamage = static_cast<int32>(static_cast<int32>(damage) * (CBSPowerSpell / CBSPowerCreature));
        }

        // Apply Mythic+ scaling to the original base damage
        damage = static_cast<DamageType>(modifyIncomingDmgHeal(eventType, target, attacker,
            static_cast<uint32>(originalDamage), spellInfo));
    }

public:

    void ModifyPeriodicDamageAurasTick(Unit* target, Unit* attacker, uint32& damage,
        SpellInfo const* spellInfo) override
    {
        if (!target || !attacker)
            return;

        if (!sMpScaler->IsMapEligible(target->GetMap()))
            return;

        bool isHot = false;
        for (SpellEffectInfo const& effect : spellInfo->Effects)
        {
            switch (effect.Effect)
            {
                case SPELL_EFFECT_HEAL:
                case SPELL_EFFECT_HEAL_MAX_HEALTH:
                case SPELL_EFFECT_HEAL_MECHANICAL:
                case SPELL_EFFECT_HEAL_PCT:
                case SPELL_EFFECT_SPIRIT_HEAL:
                    isHot = true;
                    break;
            }
        }

        if (isHot)
            damage = modifyIncomingDmgHeal(MpScaler::UNIT_EVENT_HOT, target, attacker, damage, spellInfo);
        else
            ProcessSpellDamage(target, attacker, damage, spellInfo, MpScaler::UNIT_EVENT_DOT);
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
        ProcessSpellDamage(target, attacker, damage, spellInfo, MpScaler::UNIT_EVENT_SPELL);
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

    // Callers have already checked that target and attacker are set and that target's map is eligible.
    uint32 modifyIncomingDmgHeal(MpScaler::MP_UNIT_EVENT_TYPE eventType, Unit* target, Unit* attacker,
        uint32 damageOrHeal, SpellInfo const* spellInfo = nullptr)
    {
        int32 alteredDmgHeal = 0;

        if (attacker->IsPlayer())
            return damageOrHeal;

        if (MpBots::IsNpcBotOrPet(attacker))
            return damageOrHeal;

        Creature* creature = attacker->ToCreature();
        if (!creature)
        {
            MpLog::Debug(MpLog::Area::Combat, "Attacker was considered not a creature");
            return damageOrHeal;
        }

        Map* map = target->GetMap();
        std::optional<MpInstanceData> instanceData = sMpState->GetInstanceData(map->GetId(), map->GetInstanceId());
        if (!instanceData)
            return damageOrHeal;

        // Only spell scaling reads the attacker's record; melee hits skip the lookup
        std::optional<MpCreatureData> attackerData;
        if (spellInfo)
            attackerData = sMpState->GetCreatureData(creature);

        MpCreatureData const* attackerDataPtr = attackerData ? &*attackerData : nullptr;

        // If the target is the enemy then increase the amount of healing by the instance data modifier for spell
        // output.
        if (sMpScaler->EligibleDamageTarget(target))
        {
            /**
             * @TODO: Allow more granular control over the scaling of DOT, HOT, and other spell effects
             * in the future if needed
             */
            switch (eventType)
            {
                case MpScaler::UNIT_EVENT_MELEE:

                    // Damage that is not mitigated by armor needs to be debuffed as it hits too hard and without
                    // resists
                    // it hits too hard give everyone a benefit of 30% armor reduction
                    if (creature->GetMeleeDamageSchoolMask() != SPELL_SCHOOL_MASK_NORMAL
                        && creature->GetMeleeDamageSchoolMask() != SPELL_SCHOOL_MASK_NONE)
                    {
                        damageOrHeal = static_cast<uint32>(damageOrHeal * 0.50f);
                    }
                    if (creature->IsDungeonBoss() || creature->isWorldBoss()
                        || creature->GetEntry() == MpConstants::HEADLESS_HORSEMAN)
                    {
                        alteredDmgHeal = damageOrHeal * instanceData->boss.melee;
                    }
                    else
                    {
                        alteredDmgHeal = damageOrHeal * instanceData->creature.melee;
                    }

                    // Only log if damage was actually modified
                    // Same comparison as before: the int32 operand is converted to uint32 either way.
                    if (static_cast<uint32>(alteredDmgHeal) != damageOrHeal)
                    {
                        std::string damageType = (creature->GetMeleeDamageSchoolMask() == SPELL_SCHOOL_MASK_NORMAL) ?
                            "Melee Damage" : "Elemental Damage";
                        MpLog::Debug(MpLog::Area::Combat,
                            ">>> Modify {}: Creature Name: {} originalDmg: {} alteredDmg: {} School Mask: {}",
                            damageType, creature->GetName(), damageOrHeal, alteredDmgHeal,
                                creature->GetMeleeDamageSchoolMask());
                    }

                    break;
                case MpScaler::UNIT_EVENT_DOT:
                case MpScaler::UNIT_EVENT_SPELL:
                    if (creature->IsDungeonBoss() || creature->isWorldBoss()
                        || creature->GetEntry() == MpConstants::HEADLESS_HORSEMAN)
                    {
                        if (spellInfo)
                        {
                            alteredDmgHeal = sMpScaler->ScaleDamageSpell(spellInfo, damageOrHeal, attackerDataPtr,
                                creature, target, instanceData->boss.spell);
                        }
                        else
                        {
                            alteredDmgHeal = damageOrHeal * instanceData->boss.spell;
                        }
                    }
                    else
                    {
                        if (spellInfo)
                        {
                            alteredDmgHeal = sMpScaler->ScaleDamageSpell(spellInfo, damageOrHeal, attackerDataPtr,
                                creature, target, instanceData->creature.spell);
                        }
                        else
                        {
                            alteredDmgHeal = damageOrHeal * instanceData->creature.spell;
                        }
                    }

                    break;
                case MpScaler::UNIT_EVENT_HEAL:
                case MpScaler::UNIT_EVENT_HOT:
                    break;
            }
        }

        /**
         * @TODO: Add more granular control over the scaling of healing spells
         */
        if (sMpScaler->EligibleHealTarget(target) && (eventType == MpScaler::UNIT_EVENT_HEAL
            || eventType == MpScaler::UNIT_EVENT_HOT))
        {
            // ScaleHealSpell's target is the healing creature itself (as before; see DESIGN §9)
            if (creature->IsDungeonBoss())
            {
                if (spellInfo)
                {
                    alteredDmgHeal = sMpScaler->ScaleHealSpell(spellInfo, damageOrHeal, attackerDataPtr,
                        creature, creature, instanceData->boss.spell * 0.7f);
                }
                else
                {
                    alteredDmgHeal = damageOrHeal * instanceData->boss.spell * 0.7f;
                }
            }
            else
            {
                if (spellInfo)
                {
                    alteredDmgHeal = sMpScaler->ScaleHealSpell(spellInfo, damageOrHeal, attackerDataPtr,
                        creature, creature, instanceData->creature.spell * 0.7f);
                }
                else
                {
                    alteredDmgHeal = damageOrHeal * instanceData->creature.spell * 0.70f;
                }
            }
            MpLog::Debug(MpLog::Area::Combat, "Incoming heal: {}({}) {} hits {}",
                alteredDmgHeal,
                damageOrHeal,
                attacker->GetName(),
                target->GetName());
        }

        return alteredDmgHeal > 0 ? alteredDmgHeal : damageOrHeal;
    }
};

void Add_MP_UnitScripts()
{
    new MythicPlus_UnitScript();
}
