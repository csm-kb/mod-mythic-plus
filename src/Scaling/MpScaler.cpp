#include "MpScaler.h"
#include "Group.h"
#include "MpBots.h"
#include "MpConfig.h"
#include "MpLog.h"
#include "ObjectMgr.h"
#include "World.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

// Special case for Headless Horseman Event
const uint32 HEADLESS_HORSEMAN = 23682;

bool MpScaler::IsMapEligible(Map* map)
{
    if (!sMpConfig->enabled)
    {
        return false;
    }

    if (map->IsDungeon())
    {
        return true;
    }

    return false;
}

bool MpScaler::IsDifficultySet(Player const* player)
{
    Group const* group = player->GetGroup();
    if (!group)
    {
        return false;
    }

    return sMpState->GetGroupData(group->GetGUID()).has_value();
}

bool MpScaler::EligibleHealTarget(Unit* target)
{
    if (!target)
    {
        return false;
    }

    if (target->GetTypeId() == TYPEID_CORPSE || target->GetTypeId() == TYPEID_GAMEOBJECT)
    {
        return false;
    }

    if (MpBots::IsNpcBotOrOwnedSummon(target))
    {
        return false;
    }

    // Ensure target is a valid creature before checking eligibility
    Creature* creatureTarget = target->ToCreature();
    if (creatureTarget && sMpScaler->IsCreatureEligible(creatureTarget))
    {
        return true;
    }

    return false;
}

bool MpScaler::EligibleDamageTarget(Unit* target)
{
    if (!target)
    {
        return false;
    }

    if (target->GetTypeId() == TYPEID_PLAYER)
    {
        return true;
    }

    if (MpBots::IsNpcBotOrOwnedSummon(target))
    {
        return true;
    }

    Creature* creature = target->ToCreature();
    if (creature && (creature->IsPet() || creature->IsSummon() || creature->IsHunterPet()) && creature->GetOwner() && creature->IsControlledByPlayer())
    {
        return true;
    }

    return false;
}

bool MpScaler::IsCreatureEligible(Creature* creature)
{
    if (!creature)
    {
        return false;
    }

    std::string scriptName = creature->GetScriptName();
    if (scriptName.starts_with("boss_"))
    {
        return true;
    }

    if (creature->IsDungeonBoss())
    {
        return true;
    }

    if (creature->GetEntry() == HEADLESS_HORSEMAN)
    {
        return true;
    }

    // Check if the creature is a pet or summon controlled by a player
    if ((creature->IsHunterPet() || creature->IsPet() || creature->IsSummon()) && creature->IsControlledByPlayer())
    {
        return false;
    }

    // Skip critters, totems, and triggers
    if (creature->IsCritter() || creature->IsTotem() || creature->IsTrigger())
    {
        return false;
    }

    // Safely check if the creature is an NPC Bot
    if (MpBots::IsNpcBot(creature))
    {
        return false;
    }

    if (MpBots::GetNpcBotOwner(creature))
    {
        return false;
    }

    // Check for NPC-related flags (vendor, gossip, quest giver, trainer, etc.)
    if ((creature->IsVendor() ||
         creature->HasNpcFlag(UNIT_NPC_FLAG_GOSSIP) ||
         creature->HasNpcFlag(UNIT_NPC_FLAG_QUESTGIVER) ||
         creature->HasNpcFlag(UNIT_NPC_FLAG_TRAINER) ||
         creature->HasNpcFlag(UNIT_NPC_FLAG_TRAINER_PROFESSION) ||
         creature->HasNpcFlag(UNIT_NPC_FLAG_REPAIR) ||
         creature->HasUnitFlag(UNIT_FLAG_IMMUNE_TO_PC) ||
         creature->HasUnitFlag(UNIT_FLAG_NOT_SELECTABLE)) &&
        (!creature->IsDungeonBoss()))
    {
        return false;
    }

    return true;
}

void MpScaler::AddCreatureForScaling(Creature* creature)
{
    if (!IsCreatureEligible(creature))
    {
        return;
    }

    sMpState->SetCreatureData(creature, MpCreatureData(creature));
}

void MpScaler::AddScaledCreature(Creature* creature, MpInstanceData const& instanceData)
{
    MpCreatureData creatureData = MpCreatureData(creature);
    creatureData.SetScaled(true);
    creatureData.SetDifficulty(instanceData.difficulty);
    creatureData.lastDeathState = creature->getDeathState();

    sMpState->SetCreatureData(creature, std::move(creatureData));

    // allow small variance in level for non-boss creatures
    uint8 level = uint8(urand(instanceData.creature.avgLevel - 1, instanceData.creature.avgLevel + 1));
    if (creature->IsDungeonBoss() || creature->GetEntry() == 23682)
    {
        ScaleCreature(instanceData.boss.avgLevel, creature, &instanceData.boss, instanceData.difficulty);
    }
    else
    {
        ScaleCreature(level, creature, &instanceData.creature, instanceData.difficulty);
    }
}

// Runs on the player's map thread: creatures are resolved on that map, unknown guids are skipped.
void MpScaler::ScaleRemaining(Player* player, MpInstanceData const& instanceData)
{
    Map* map = player->GetMap();
    for (ObjectGuid const& guid : sMpState->GetInstanceCreatureGuids(map->GetId(), map->GetInstanceId(), true))
        if (Creature* creature = map->GetCreature(guid))
            AddScaledCreature(creature, instanceData);
}

// Runs on the player's map thread: creatures are resolved on that map, unknown guids are skipped.
void MpScaler::ScaleAll(Player* player, MpInstanceData const& instanceData)
{
    Map* map = player->GetMap();
    for (ObjectGuid const& guid : sMpState->GetInstanceCreatureGuids(map->GetId(), map->GetInstanceId(), false))
    {
        // Only scale living creatures
        Creature* creature = map->GetCreature(guid);
        if (creature && creature->IsAlive())
            ScaleCreature(creature->GetLevel(), creature, &instanceData.creature, instanceData.difficulty);
    }
}

void MpScaler::ScaleCreature(uint8 level, Creature* creature, MpMultipliers const* multipliers,
    MpDifficulty difficulty)
{
    CreatureTemplate const* cInfo = creature->GetCreatureTemplate();
    uint32 mapId = creature->GetMapId();

    // get the map difficulty from the map instance to see if it is a heroic or normal set instance
    InstanceMap *instanceMap = creature->GetMap()->ToInstanceMap();
    if (!instanceMap)
    {
        MpLog::Error(MpLog::Area::Scaling, "Invalid instance map ScaleCreature()");
        return;
    }

    creature->SetLevel(level);
    CreatureBaseStats const* stats = sObjectMgr->GetCreatureBaseStats(
        level,
        cInfo->unit_class
    );

    // Scale the creatures base health
    uint32 basehp = stats->BaseHealth[EXPANSION_WRATH_OF_THE_LICH_KING];
    uint32 health = CalculateNewHealth(creature, cInfo, mapId, difficulty, basehp, multipliers->health);

    creature->SetCreateHealth(health);
    creature->SetMaxHealth(health);
    creature->SetHealth(health);
    creature->ResetPlayerDamageReq();
    creature->SetStatFlatModifier(UNIT_MOD_HEALTH, BASE_VALUE, (float)health);

    /**
     * @TODO: Figure out mana later for unit_types 2 and 8
     */
    // Scale the creatures mana pool
    uint32 mana = uint32(std::ceil(stats->BaseMana * cInfo->ModMana));
    creature->SetCreateMana(mana);
    creature->SetMaxPower(POWER_MANA, mana);
    creature->SetPower(POWER_MANA, mana);

    if (cInfo->unit_class == CLASS_MAGE)
    {
        creature->SetStatFlatModifier(UNIT_MOD_MANA, BASE_VALUE, (float)mana * 10.0f);
    }

    if (cInfo->unit_class == CLASS_PALADIN)
    {
        creature->SetStatFlatModifier(UNIT_MOD_MANA, BASE_VALUE, (float)mana * 3.0f);
    }

    // The stored record's difficulty is used, as before; the caller's difficulty only covers a missing record.
    std::optional<MpInstanceData> instanceData = sMpState->GetInstanceData(creature->GetMapId(),
        creature->GetInstanceId());
    MpDifficulty instanceDifficulty = instanceData ? instanceData->difficulty : difficulty;

    // Handle new melee/range scaling with simple formula (for simplicity range will just be 80% of melee bonus)
    float meleeMultiplier = sMpRepo->GetMeleeScaleFactor(creature->GetMapId(), instanceDifficulty);

    // Since Heroic Scaling can get out of hand. Reduce the instance multiplier by way too much 10%
    if (instanceMap->IsHeroic() || instanceMap->Is25ManRaid())
    {
        // if the enemy is a boss reduce it by less
        meleeMultiplier *= 0.9f;
    }

    // Scale up the attack power based on the instance set in the database difficulty table.
    uint32 ap = std::ceil(stats->AttackPower * meleeMultiplier);
    uint32 rangeAp = std::ceil(stats->RangedAttackPower * meleeMultiplier * 0.4f);

    // Additionally need to add in a decrease in attack power for normal non elite enemies
    if (creature->GetCreatureTemplate()->rank == CREATURE_ELITE_NORMAL)
    {
        // Reduced scaling for elite/boss spells to prevent them from hitting too hard
        ap *= sMpConfig->normalEnemyReducer;
        rangeAp *= sMpConfig->normalEnemyReducer;
    }

    sMpState->UpdateCreatureData(creature, [ap, meleeMultiplier](MpCreatureData& data)
    {
        data.NewAttackPower = ap;
        data.AttackPowerScaleMultiplier = meleeMultiplier;
    });

    // Set scaled attack power
    creature->SetStatFlatModifier(UNIT_MOD_ATTACK_POWER, BASE_VALUE, ap);
    creature->SetStatFlatModifier(UNIT_MOD_ATTACK_POWER_RANGED, BASE_VALUE, rangeAp);

    // set the base weapon damage
    creature->SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, stats->BaseDamage[EXPANSION_WRATH_OF_THE_LICH_KING], 0);
    creature->SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, stats->BaseDamage[EXPANSION_WRATH_OF_THE_LICH_KING] * 1.5f, 0);

    creature->SetBaseWeaponDamage(RANGED_ATTACK, MINDAMAGE, stats->BaseDamage[EXPANSION_WRATH_OF_THE_LICH_KING] * 0.5f, 0);
    creature->SetBaseWeaponDamage(RANGED_ATTACK, MAXDAMAGE, stats->BaseDamage[EXPANSION_WRATH_OF_THE_LICH_KING] * 0.8f, 0);

    // Update all stats to apply the new damage values
    creature->UpdateAllStats();

    // Scale up the armor with some variance also to make some tougher enemies in the mix
    uint32 armor = uint32(std::ceil(stats->BaseArmor * multipliers->armor * cInfo->ModArmor));
    creature->SetArmor(armor);
}

int32 MpScaler::CalculateSpellDamage(uint32 baseDamage, int originalLevel, int targetLevel)
{
    float origHpPool = sMpRepo->GetPlayerHealthAvg(originalLevel);
    float targetHpPool = sMpRepo->GetPlayerHealthAvg(targetLevel);

    // Using a % of expected damage of the average player pool creates a better consistent experience when scaling spells
    float percentDamage = baseDamage / origHpPool;

    // If the percentage damage is less than 2% cap it at 2% to prevent spells from being too powerful
    if (percentDamage < 0.02f)
    {
        percentDamage = 0.02f;
    }

    MpLog::Debug(MpLog::Area::Combat, "OrigHpPool: {} TargetHpPool: {} Percent Damage: {}", origHpPool, targetHpPool, percentDamage);
    MpLog::Debug(MpLog::Area::Combat, "Original Damage: {} Scaled Damage: {}", baseDamage, static_cast<int32>(std::ceil(percentDamage * targetHpPool)));
    int32 scaledDamage = static_cast<int32>(std::ceil(percentDamage * targetHpPool));

    return scaledDamage;
}

int32 MpScaler::CalculateHealScaling(uint32 baseHeal, uint32 originalTargetHealth, uint32 targetMaxHealth)
{
    if (originalTargetHealth == 0)
    {
        MpLog::Debug(MpLog::Area::Combat, "Original target health is 0, returning base heal: {}", baseHeal);
        return baseHeal;
    }

    // Calculate the percentage of the original heal relative to original creature health
    float percentHeal = static_cast<float>(baseHeal) / static_cast<float>(originalTargetHealth);

    if (percentHeal < 0.01f)
    {
        percentHeal = 0.01f;
    }

    // Cap the percentage at 100% aka full heal of current max health of caster
    if (percentHeal > 1.0f)
    {
        percentHeal = 1.0f;
    }

    // Scale the heal based on the current creature's max health
    int32 scaledHeal = static_cast<int32>(std::ceil(percentHeal * targetMaxHealth));

    MpLog::Debug(MpLog::Area::Combat, "HEALING: >>> OrigHealth: {} CurrentMaxHealth: {} Percent Heal: {} Original Heal: {} Scaled Heal: {}",
                   originalTargetHealth, targetMaxHealth, percentHeal, baseHeal, scaledHeal);

    return scaledHeal;
}

int32 MpScaler::ScaleDamageSpell(SpellInfo const* spellInfo, uint32 damage, MpCreatureData const* creatureData,
    Creature* creature, Unit* /* target */, float damageMultiplier)
{
    if (!spellInfo)
    {
        MpLog::Debug(MpLog::Area::Combat, "Invalid spell info ScaleDamageSpell()");
        return damage;
    }

    std::optional<MpInstanceData> instanceData = sMpState->GetInstanceData(creature->GetMapId(),
        creature->GetInstanceId());
    if (!instanceData)
    {
        MpLog::Debug(MpLog::Area::Combat, "No instance data found for spell scaling, using original damage");
        return damage;
    }

    float scaleFactor = sMpRepo->GetSpellScaleFactor(creature->GetMapId(), instanceData->difficulty);

    MpLog::Debug(MpLog::Area::Combat, "DAMAGE SPELL: >> ScaleFactor: {} DamageMultiplier: {}", scaleFactor, damageMultiplier);

    // calculate the global modifier x instance modifier
    float totalModifier = damageMultiplier * scaleFactor;

    // If for some reason there is not a creature, just use the global modifier x instance modifier
    if (!creature)
    {
        MpLog::Debug(MpLog::Area::Combat, "Invalid creature ScaleDamageSpell()");
        return damage * totalModifier;
    }

    // Use the already calculated damage as the base for scaling
    int32 newDamage = damage;

    // Handle Summoned unit modifiers as
    if (!creatureData)
    {
        // handle if bot pets if NPCBot is installed.
        if (MpBots::IsNpcBotOrPet(creature))
        {
            return damage;
        }

        // Handle totems and summons - scale based on owner's details because they will not have creature data
        if (creature->IsTotem() || creature->IsSummon())
        {
            Unit* owner = creature->GetOwner();
            if (owner && owner->IsCreature())
            {
                Creature* ownerCreature = owner->ToCreature();

                // Look up the owner creature's original level from the runtime state
                std::optional<MpCreatureData> ownerCreatureData = sMpState->GetCreatureData(ownerCreature);
                if (ownerCreatureData)
                {
                    MpLog::Debug(MpLog::Area::Combat, "DAMAGE SPELL: >> Creature is a totem or summon Creature Name {} and owner {} owner original level {} owner level {}", creature->GetName(), ownerCreature->GetName(), ownerCreatureData->originalLevel, ownerCreature->GetLevel());
                    int32 ownerOriginalLevel = ownerCreatureData->originalLevel;

                    if (ownerCreature->GetCreatureTemplate()->rank == CREATURE_ELITE_NORMAL)
                    {
                        totalModifier = totalModifier * sMpConfig->normalEnemyReducer;
                    }
                    newDamage = CalculateSpellDamage(damage, ownerOriginalLevel, ownerCreature->GetLevel());
                }
                else
                {
                    // Fallback if no creature data found - use current level
                    if (ownerCreature->GetCreatureTemplate()->rank == CREATURE_ELITE_NORMAL)
                    {
                        totalModifier = totalModifier * sMpConfig->normalEnemyReducer;
                    }
                    newDamage = CalculateSpellDamage(damage, ownerCreature->GetLevel(), ownerCreature->GetLevel());
                    MpLog::Debug(MpLog::Area::Combat, "No creature data found for owner {}, using current level for scaling", ownerCreature->GetGUID().ToString());
                }
            }
        }
        else
        {
            MpLog::Debug(MpLog::Area::Combat, "Invalid creature data ScaleDamageSpell()");
            return damage * totalModifier;
        }
    }
    else
    {
        newDamage = CalculateSpellDamage(damage, creatureData->originalLevel, creature->GetLevel());
        if (creature->GetCreatureTemplate()->rank == CREATURE_ELITE_NORMAL)
        {
            // Reduced scaling for elite/boss spells to prevent them from hitting too hard
            totalModifier = totalModifier * 0.85f;
        }
    }

    // Calculate the additional damage from scaling (scaled damage - original damage)
    int32 additionalDamage = newDamage - damage;

    // Apply the scaling modifier to the additional damage only
    int32 scaledAdditionalDamage = additionalDamage * totalModifier;

    // Use the diminishing return values from the configuration
    MpTierConfig const* tier = sMpConfig->GetTier(instanceData->difficulty);
    uint32 threshold = tier ? tier->diminishingThreshold : 0;
    float diminishingExponent = sMpConfig->diminishingExponent;

    // Apply diminishing returns only to the additional scaled damage if it exceeds threshold
    if (static_cast<uint32>(scaledAdditionalDamage) > threshold)
    {
        // Calculate the diminished excess additional damage
        float excess = scaledAdditionalDamage - threshold;
        float diminishedExcess = pow(excess, diminishingExponent);
        scaledAdditionalDamage = threshold + diminishedExcess;

        MpLog::Debug(MpLog::Area::Combat, "DAMAGE SPELL: >> Above Diminishing Threshold for Spell {} - Original: {}, Additional: {}, Diminished Additional: {}, Final: {}",
                       spellInfo->SpellName[0], damage, additionalDamage * totalModifier, scaledAdditionalDamage, damage + scaledAdditionalDamage);
    }
    else
    {
        MpLog::Debug(MpLog::Area::Combat, "DAMAGE SPELL: >> Below Diminishing Threshold for Spell {} - Original: {}, Additional: {}, Final: {}",
                       spellInfo->SpellName[0], damage, scaledAdditionalDamage, damage + scaledAdditionalDamage);
    }

    // If this is a heroic instance the additional spell damage should be increased by 50%
    InstanceMap* instanceMap = creature->GetMap()->ToInstanceMap();
    if (instanceMap && (instanceMap->IsHeroic() || instanceMap->Is25ManRaid()))
    {
        scaledAdditionalDamage = scaledAdditionalDamage * 1.5f;
    }

    // Return original damage + scaled additional damage (with potential diminishing returns)
    return damage + scaledAdditionalDamage;
}

int32 MpScaler::ScaleHealSpell(SpellInfo const* spellInfo, uint32 heal, MpCreatureData const* creatureData,
    Creature* creature, Creature* target, float healMultiplier)
{
    if (!spellInfo)
    {
        MpLog::Debug(MpLog::Area::Combat, "Invalid spell info ScaleHealSpell()");
        return heal;
    }

    std::optional<MpInstanceData> instanceData = sMpState->GetInstanceData(creature->GetMapId(),
        creature->GetInstanceId());
    if (!instanceData)
    {
        MpLog::Debug(MpLog::Area::Combat, "No instance data found for heal scaling, using original heal");
        return heal;
    }

    float scaleFactor = sMpRepo->GetHealScaleFactor(creature->GetMapId(), instanceData->difficulty);

    MpLog::Debug(MpLog::Area::Combat, "HEALING: >>> HealScaleFactor: {} HealMultiplier: {}", scaleFactor, healMultiplier);

    // calculate the global modifier x instance modifier
    float totalModifier = healMultiplier * scaleFactor;

    // If for some reason there is not a creature, just use the global modifier x instance modifier
    if (!creature)
    {
        MpLog::Debug(MpLog::Area::Combat, "Invalid creature ScaleHealSpell()");
        return heal * totalModifier;
    }

    // Use the already calculated heal as the base for scaling
    int32 newHeal = heal;

    // Handle Summoned unit modifiers
    if (!creatureData)
    {
        // handle if bot pets if NPCBot is installed.
        if (MpBots::IsNpcBotOrPet(creature))
        {
            return heal;
        }

        MpLog::Debug(MpLog::Area::Combat, "HEALING: >>> Scaling heal to target: {} with spell: {}", target->GetName(), spellInfo->SpellName[0]);

        // Handle totems and summons - scale based on owner's details because they will not have creature data
        if (creature->IsTotem() || creature->IsSummon())
        {
            Unit* owner = creature->GetOwner();
            if (owner && owner->IsCreature())
            {
                Creature* ownerCreature = owner->ToCreature();

                // Look up the owner creature's original level from the runtime state
                std::optional<MpCreatureData> ownerCreatureData = sMpState->GetCreatureData(ownerCreature);
                if (ownerCreatureData)
                {
                    if (ownerCreature->GetCreatureTemplate()->rank == CREATURE_ELITE_NORMAL)
                    {
                        totalModifier = totalModifier * sMpConfig->normalEnemyReducer; // Less reduction for heals than damage
                    }
                    // Scale heal based on target's health, not caster's health
                    if (target)
                    {
                        std::optional<MpCreatureData> targetCreatureData = sMpState->GetCreatureData(target);
                        uint32 targetOriginalHealth = targetCreatureData && targetCreatureData->originalInstanceHealth > 0 ?
                            targetCreatureData->originalInstanceHealth : target->GetMaxHealth();
                            MpLog::Debug(MpLog::Area::Combat, "HEALING: >>> Scaling heal to target: {} Original Instance Health: {} New Health: {}", target->GetName(), targetOriginalHealth, target->GetMaxHealth());
                        newHeal = CalculateHealScaling(heal, targetOriginalHealth, target->GetMaxHealth());
                    }
                    else
                    {
                        newHeal = heal;
                    }
                }
                else
                {
                    // Fallback if no creature data found - use current level
                    if (ownerCreature->GetCreatureTemplate()->rank == CREATURE_ELITE_NORMAL)
                    {
                        totalModifier = totalModifier * sMpConfig->normalEnemyReducer; // Less reduction for heals than damage
                    }
                    // Scale heal based on target's health, not caster's health
                    if (target)
                    {
                        std::optional<MpCreatureData> targetCreatureData = sMpState->GetCreatureData(target);
                        uint32 targetOriginalHealth = targetCreatureData && targetCreatureData->originalInstanceHealth > 0 ?
                            targetCreatureData->originalInstanceHealth : target->GetMaxHealth();

                        MpLog::Debug(MpLog::Area::Combat, "HEALING: >>> Scaling heal to target: {} Original Instance Health: {} New Health: {}", target->GetName(), targetOriginalHealth, target->GetMaxHealth());

                        newHeal = CalculateHealScaling(heal, targetOriginalHealth, target->GetMaxHealth());
                    }
                    else
                    {
                        newHeal = heal;
                    }
                    MpLog::Debug(MpLog::Area::Combat, "No creature data found for owner {}, using current level for scaling", ownerCreature->GetGUID().ToString());
                }
            }
        }
        else
        {
            MpLog::Debug(MpLog::Area::Combat, "Invalid creature data ScaleHealSpell()");
            return heal * totalModifier;
        }
    }
    else
    {
        // Scale heal based on target's health, not caster's health
        if (target)
        {
            // Get target's original instance health for scaling comparison
            std::optional<MpCreatureData> targetCreatureData = sMpState->GetCreatureData(target);
            uint32 targetOriginalHealth = targetCreatureData && targetCreatureData->originalInstanceHealth > 0 ?
                targetCreatureData->originalInstanceHealth : target->GetMaxHealth();
                MpLog::Debug(MpLog::Area::Combat, "HEALING: >>> Scaling heal to target: {} Original Instance Health: {} New Health: {}", target->GetName(), targetOriginalHealth, target->GetMaxHealth());
            newHeal = CalculateHealScaling(heal, targetOriginalHealth, target->GetMaxHealth());
        }
        else
        {
            // No target available, use original heal
            newHeal = heal;
        }
    }

    // Calculate the additional heal from scaling (scaled heal - original heal)
    int32 additionalHeal = newHeal - heal;

    // Apply the scaling modifier to the additional heal only
    int32 scaledAdditionalHeal = additionalHeal * totalModifier;

    // Use the diminishing return values from the configuration (same as damage)
    MpTierConfig const* tier = sMpConfig->GetTier(instanceData->difficulty);
    uint32 threshold = tier ? tier->diminishingThreshold : 0;
    float diminishingExponent = sMpConfig->diminishingExponent;

    // Apply diminishing returns only to the additional scaled heal if it exceeds threshold * 2 since enemies have much more health.
    if (scaledAdditionalHeal > threshold * 2.0f)
    {
        // Calculate the diminished excess additional heal
        float excess = scaledAdditionalHeal - threshold;
        float diminishedExcess = pow(excess, diminishingExponent * 0.95f); // slightly reduce the diminishing returns for heals
        scaledAdditionalHeal = threshold + diminishedExcess;

        MpLog::Debug(MpLog::Area::Combat, "HEALING: >>> Above Diminishing Threshold for Heal Spell {} - Original: {}, Additional: {}, Diminished Additional: {}, Final: {}",
                       spellInfo->SpellName[0], heal, additionalHeal * totalModifier, scaledAdditionalHeal, heal + scaledAdditionalHeal);
    }
    else
    {
        MpLog::Debug(MpLog::Area::Combat, "HEALING: >>> Below Diminishing Threshold for Heal Spell {} - Original: {}, Additional: {}, Final: {}",
                       spellInfo->SpellName[0], heal, scaledAdditionalHeal, heal + scaledAdditionalHeal);
    }

    // If this is a heroic instance the additional heal be only slightly increased
    InstanceMap* instanceMap = creature->GetMap()->ToInstanceMap();
    if (instanceMap && (instanceMap->IsHeroic() || instanceMap->Is25ManRaid()))
    {
        scaledAdditionalHeal = scaledAdditionalHeal * 1.15f;
    }

    // Return original heal + scaled additional heal (with potential diminishing returns)
    return heal + scaledAdditionalHeal;
}

bool MpScaler::IsFinalBoss(Creature* creature)
{
    std::array<uint32, 128> finalBosses = {
        // --- WoW Classic Dungeons ---
        11519,  /* Bazzalan Ragefire */
        639,    /* Edwin VanCleef Deadmines */
        3654,   /* Mutanus the Devourer Wailing Caverns */
        4275,   /* Archmage Arugal Shadowfang Keep */
        1716,   /* Bazil Thredd Stockades */
        4829,   /* Aku'mai blackfathom Deeps */
        7800,   /* Mekgineer Thermaplugg Gnomeregan */
        4421,   /* Charlga Razorflank Razorfen Kraul */
        4543,   /* Bloodmage Thalnos Scarlet Monastery */
        3975,   /* Herod Scarlet Monastery */
        3977,   /* High Inquisitor Whitemane Scarlet Monastery */
        7350,   /* Amnennar the Coldbringer Razorfen Downs */
        2748,   /* Archaedas Uldaman */
        7267,   /* Chief Ukorz Sandscalp Zul'Farrak */
        12201,  /* Princess Theradras Maraudon */
        5709,   /* Shade of Eranikus Sunken Temple */
        9019,   /* Emperor Dagran Thaurissan Blackrock Depths */
        9568,   /* Overlord Wyrmthalak Lower Blackrock Spire */
        10363,  /* General Drakkisath Upper Blackrock Spire */
        11492,  /* alzzin the Wildshaper Dire Maul */
        11496,  /* Immol'thar Dire Maul */
        11501,  /* King Gordok Dire Maul */
        1853,   /* Darkmaster Gandling Scholomance */
        10812,  /* Grand Crusader Dathrohan Stratholme */
        10440,  /* Baron Rivendare Stratholme */

        // --- WoW Classic Raids ---
        11583,  /* Nefarian Blackwing Lair */
        11502,  /* Ragnaros Molten Core */
        14834,  /* Hakkar Zul'Gurub */
        15727,  /* C'Thun Temple of Ahn'Qiraj */
        15339,  /* Ossirian the Unscarred Ruins of Ahn'Qiraj */

        // --- The Burning Crusade ---
        17536,  /* Nazan - Hellfire Ramparts Normal */
        17536,  /* Nazan - Hellfire Ramparts Normal */
        17377,  /* Kelidan the Breaker - Blood Furnace Normal */
        18607,  /* Kelidan the Breaker - Blood Furnace Heroic */
        17942,  /* Quagmirran - The Slave Pens Normal */
        19894,  /* Quagmirran - The Slave Pens Heoric */
        17882,  /* The Black Stalker - The Underbog Normal */
        20184,  /* The Black Stalker - The Underbog Heroic */
        24420,  /* The Black Stalker - The Underbog Heroic(2) */
        18344,  /* Nexus-Prince Shaffar - Mana-Tombs Normal */
        20256,  /* Nexus-Prince Shaffar - Mana-Tombs Heroic */
        18373,  /* Exarch Maladaar - Auchenai Crypts Normal */
        20306,  /* Exarch Maladaar - Auchenai Crypts Heoric */
        18096,  /* Epoch Hunter - Old Hillsbrad Foothills Normal */
        20531,  /* Epoch Hunter - Old Hillsbrad Foothills Heroic */
        18473,  /* Talon King Ikiss - Sethekk Halls Normal */
        20706,  /* Talon King Ikiss - Sethekk Halls Heroic */
        17798,  /* Warlord Kalithresh - The Steamvault Normal */
        20633,  /* Warlord Kalithresh - The Steamvault Heroic */
        18708,  /* Murmur - Shadow Labyrinth Normal */
        20657,  /* Murmur - Shadow Labyrinth Heroic */
        16808,  /* Warchief Kargath Bladefist - The Shattered Halls Normal */
        20597,  /* Warchief Kargath Bladefist - The Shattered Halls Heroic */
        17881,  /* Aeonus - The Black Morass Normal */
        20737,  /* Aeonus - The Black Morass Heroic */
        17977,  /* Warp Splinter - The Botanica Normal */
        21582,  /* Warp Splinter - The Botanica Heroic */
        19220,  /* Pathaleon the Calculator - The Mechanar Normal */
        21537,  /* Pathaleon the Calculator - The Mechanar Heroic */
        20912,  /* Harbinger Skyriss - The Arcatraz Normal */
        21601,  /* Harbinger Skyriss - The Arcatraz Heroic */
        19622,  /* Kael'thas Sunstrider - Magisters' Terrace Normal */
        24857,  /* Kael'thas Sunstrider - Magisters' Terrace Heroic */

        // --- Burning Crusade Raids ---
        15690,  /* Prince Malchezaar - Karazhan */
        23863,  /* Zul'jin - Zul'Aman */
        19044,  /* Gruul the Dragonkiller - Gruul's Lair */
        17257,  /* Magtheridon - Magtheridon's Lair */
        21212,  /* Lady Vashj - Serpentshrine Cavern */
        24664,  /* Kael'thas Sunstrider - The Eye */
        24855,  /* Kael'thas Sunstrider - The Eye */
        17968,  /* Archimonde - Hyjal Summit */
        22917,  /* Illidan Stormrage - Black Temple */
        25315,  /* Kil'jaeden - Sunwell Plateau */

        // --- Wrath of the Lich King ---
        23954,  /* Ingvar the Plunderer - Utgarde Keep Normal */
        31673,  /* Ingvar the Plunderer - Utgarde Keep Heroic */
        26861,  /* King Ymiron - Utgarde Pinnacle Normal */
        30788,  /* King Ymiron - Utgarde Pinnacle Heroic */
        26723,  /* Keristrasza - The Nexus Normal */
        30540,  /* Keristrasza - The Nexus Heroic */
        26632,  /* The Prophet Tharon'ja - Drak'Tharon Keep Normal */
        31360,  /* The Prophet Tharon'ja - Drak'Tharon Keep Heroic */
        27656,  /* Ley-Guardian Eregos - The Oculus Normal */
        31561,  /* Ley-Guardian Eregos - The Oculus Heroic */
        29311,  /* Herald Volazj - Ahn'kahet: The Old Kingdom Normal */
        31464,  /* Herald Volazj - Ahn'kahet: The Old Kingdom Heroic */
        29120,  /* Anub'arak - Azjol-Nerub Normal */
        31610,  /* Anub'arak - Azjol-Nerub Heroic */
        29306,  /* Gal'darah - Gundrak Normal */
        31368,  /* Gal'darah - Gundrak Heroic */
        26533,  /* Mal'Ganis - Culling of Stratholme Normal */
        31217,  /* Mal'Ganis - Culling of Stratholme Heroic */
        31134,  /* Cyanigosa - Violet Hold Normal */
        31506,  /* Cyanigosa - Violet Hold Heroic */
        27978,  /* Sjonnir the Ironshaper - Halls of Stone Normal */
        31386,  /* Sjonnir the Ironshaper - Halls of Stone Heroic */
        28923,  /* Loken - Halls of Lightning Normal */
        31538,  /* Loken - Halls of Lightning Heroic */
        35451,  /* The Black Knight - Trial of the Champion Normal */
        35490,  /* The Black Knight - Trial of the Champion Heroic */
        36502,  /* Devourer of Souls - The Forge of Souls Normal */
        37677,  /* Devourer of Souls - The Forge of Souls Heroic */
        36658,  /* Scourgelord Tyrannus - Pit of Saron Normal */
        36938,  /* Scourgelord Tyrannus - Pit of Saron Heroic */
        37226,  /* The Lich King Encounter - Halls of Reflection Normal */
        39166,  /* The Lich King Encounter - Halls of Reflection Heroic */

        // --- Wrath of the Lich King Raids ---
        15990,  /* Kel'Thuzad - Naxxramas */
        28859,  /* Malygos - The Eye of Eternity */
        28860,  /* Sartharion - Obsidian Sanctum */
        31125,  /* Archavon the Stone Watcher - Vault of Archavon */
        33993,  /* Emalon the Storm Watcher - Vault of Archavon */
        35013,  /* Koralon the Flame Watcher - Vault of Archavon */
        38433,  /* Toravon the Ice Watcher - Vault of Archavon */
        33288,  /* Yogg-Saron - Ulduar */
        34564,  /* Anub'arak - Trial of the Crusader */
        10184,  /* Onyxia - Onyxia's Lair (re-released in Wrath) */
        36597,  /* The Lich King - Icecrown Citadel */
        39863,  /* Halion - Ruby Sanctum */
    };

    return std::find(finalBosses.begin(), finalBosses.end(), creature->GetEntry()) != finalBosses.end();
}

/**
 * Function is copied because was not accessible in core creature class
 */
float MpScaler::GetTypeHealthModifier(int32 Rank)
{
    switch (Rank)
    {
        case CREATURE_ELITE_NORMAL:
            return sWorld->getRate(RATE_CREATURE_NORMAL_HP);
        case CREATURE_ELITE_ELITE:
            return sWorld->getRate(RATE_CREATURE_ELITE_ELITE_HP);
        case CREATURE_ELITE_RAREELITE:
            return sWorld->getRate(RATE_CREATURE_ELITE_RAREELITE_HP);
        case CREATURE_ELITE_WORLDBOSS:
            return sWorld->getRate(RATE_CREATURE_ELITE_WORLDBOSS_HP);
        case CREATURE_ELITE_RARE:
            return sWorld->getRate(RATE_CREATURE_ELITE_RARE_HP);
        default:
            return sWorld->getRate(RATE_CREATURE_ELITE_ELITE_HP);
    }
}

// This takes the orignal health and scales flat based on the factor then applies the configuration modifier from the conf file
uint32 MpScaler::CalculateNewHealth(Creature* creature, CreatureTemplate const* cInfo, uint32 mapId, MpDifficulty difficulty, uint32 origHealth, float confHPMod)
{
    //
    int32 rank = 0;
    if (cInfo && cInfo->rank > 0)
    {
        rank = cInfo->rank;
    }

    // These Factors that increase or decrease health based on different settings applied to the creature

    // Health Variation is used to create some random element to HP so not all creatures of the same level
    // have the same HP for more variety.
    float healthVariation;

    //  This is the fine grained hpScaleFactor set for the instance (and/or) creature overrides in the database.
    int32 hpScaleFactor = sMpRepo->GetHealthScaleFactor(mapId, difficulty);

    // Add some variance to the healthpool so enemies are not all the same
    if (creature->IsDungeonBoss() || creature->isWorldBoss() || creature->isElite() || cInfo->rank == CREATURE_ELITE_RARE)
    {
        healthVariation = frand(1.0f, 1.15f);
    }
    else // This addresses Normals and other trash from getting to big a HP bonus
    {
        healthVariation = frand(1.0f, 1.05f);
        hpScaleFactor *= 0.50;
    }

    // Add in special overrides here as necessary:
    if (creature->GetEntry() == HEADLESS_HORSEMAN)
    {
        healthVariation = frand(1.0f, 1.1f);
    }

    float unitTypeMod = GetTypeHealthModifier(rank);
    uint32 basehp;

    // Only apply unitTypeMod for non-normal enemies
    if (rank != CREATURE_ELITE_NORMAL)
    {
        basehp = uint32(std::ceil(origHealth * healthVariation * unitTypeMod));
    }
    else
    {
        basehp = uint32(std::ceil(origHealth * healthVariation));
    }

    // if it is a heroic instance give the enemy an additional 20% boost
    InstanceMap* instanceMap = creature->GetMap()->ToInstanceMap();
    if (instanceMap && instanceMap->IsRaidOrHeroicDungeon())
    {
        basehp *= 1.25f;
    }

    /**
     * @brief Calculating the final creature health encompasses all the potential modifiers
     * CreatureTemplate.HealthModifier (ModHealth) - Creatures that are capable of being in a Heroic instance get a boost here
     * even though they are the same.  In this case we allow
     *
     * hpScaleFactor:  allows to tweak the bonus modifier more directly at a creature or instance level, since
     * you can not override it in cInfo directly as it is loaded statically from database
     *
     * confHPMod: is from the mythic settings directly.
     */
    if (cInfo->ModHealth > 0.0f)
    {
        return uint32(basehp * (cInfo->ModHealth + hpScaleFactor) * confHPMod);
    }
    else
    {
        return uint32(basehp * hpScaleFactor * confHPMod);
    }
}
