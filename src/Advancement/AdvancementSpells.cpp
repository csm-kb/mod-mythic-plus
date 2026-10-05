#include "AdvancementMgr.h"
#include "MpLog.h"
#include "Player.h"
#include "SpellAuraEffects.h"
#include "SpellInfo.h"
#include "SpellScript.h"
#include "SpellScriptLoader.h"

/**
 * Passive advancement aura: effect 0's amount is the player's bonus for one advancement (0 without one).
 * One class serves all ten auras; each registration passes its script name, advancement, aura type and the
 * label used in its log line.
 */
class spell_mp_advancement_aura : public AuraScript
{
    PrepareAuraScript(spell_mp_advancement_aura);

public:
    spell_mp_advancement_aura(char const* scriptName, MpAdvancements advancement, AuraType auraType,
        char const* label) : AuraScript(), _scriptName(scriptName), _advancement(advancement), _auraType(auraType),
        _label(label) { }

    void HandleEffectCalcAmount(AuraEffect const* /*aurEff*/, int32& amount, bool& /*canBeRecalculated*/)
    {
        Player* player = GetCaster()->ToPlayer();
        auto rank = sAdvancementMgr->GetPlayerAdvancementRank(player, _advancement);

        if (!rank)
        {
            amount = 0; // player does not have an advancement
            return;
        }

        amount = static_cast<int32>(rank->bonus);
        MpLog::Debug(MpLog::Area::Advancement, "In Calc Amount Advancement {} to Player {} bonus {}", _label,
            player->GetName(), amount);
    }

    void Register() override
    {
        MpLog::Info(MpLog::Area::Advancement, "Registering {}", _scriptName);
        DoEffectCalcAmount += AuraEffectCalcAmountFn(spell_mp_advancement_aura::HandleEffectCalcAmount, EFFECT_0,
            _auraType);
    }

private:
    char const* _scriptName;
    MpAdvancements _advancement;
    AuraType _auraType;
    char const* _label;
};

// Registers one advancement aura; name is the spell_script_names.ScriptName of its spell.
static void RegisterAdvancementAura(char const* name, MpAdvancements advancement, AuraType auraType, char const* label)
{
    RegisterSpellScriptWithArgs(spell_mp_advancement_aura, name, name, advancement, auraType, label);
}

void AddSC_AdvancementSpells()
{
    RegisterAdvancementAura("spell_mp_titans_strength_aura", MP_ADV_STRENGTH, SPELL_AURA_MOD_STAT,
        "Titans Strength");
    RegisterAdvancementAura("spell_mp_steel_forged_aura", MP_ADV_STAMINA, SPELL_AURA_MOD_STAT, "Steel Forged");
    RegisterAdvancementAura("spell_mp_celestial_grace_aura", MP_ADV_SPIRIT, SPELL_AURA_MOD_STAT, "Celestial Grace");
    RegisterAdvancementAura("spell_mp_forbidden_knowledge_aura", MP_ADV_INTELLECT, SPELL_AURA_MOD_STAT,
        "Forbidden Knowledge");
    RegisterAdvancementAura("spell_mp_spectral_reflexes_aura", MP_ADV_AGILITY, SPELL_AURA_MOD_STAT,
        "Spectral Reflexes");
    RegisterAdvancementAura("spell_mp_eldritch_barrier_aura", MP_ADV_RESIST_ARCANE, SPELL_AURA_MOD_RESISTANCE,
        "Eldritch Barrier");
    RegisterAdvancementAura("spell_mp_hellfire_shielding_aura", MP_ADV_RESIST_FIRE, SPELL_AURA_MOD_RESISTANCE,
        "Hellfire Shielding");
    RegisterAdvancementAura("spell_mp_primal_endurance_aura", MP_ADV_RESIST_NATURE, SPELL_AURA_MOD_RESISTANCE,
        "Primal Endurance");
    RegisterAdvancementAura("spell_mp_lichs_bane_aura", MP_ADV_RESIST_SHADOW, SPELL_AURA_MOD_RESISTANCE,
        "Lich's Bane");
    RegisterAdvancementAura("spell_mp_glacial_fortress_aura", MP_ADV_RESIST_FROST, SPELL_AURA_MOD_RESISTANCE,
        "Glacial Fortress");
}
