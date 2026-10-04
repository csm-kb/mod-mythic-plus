#ifndef MP_BOTS_H
#define MP_BOTS_H

class Creature;
class Player;
class Unit;

// The only place bot frameworks are known (compile-time providers: MOD_PLAYERBOTS, MOD_PRESENT_NPCBOTS;
// MP_NO_BOT_PROVIDERS disables all). With no provider, nothing is a bot.
namespace MpBots
{
    // The unit is a bot of any compiled-in framework. Used for log-level/gating decisions only.
    bool IsBot(Unit const* unit);
    // NPCBots only: the unit is an NPCBot. Legacy sites; false without NPCBots.
    bool IsNpcBot(Unit const* unit);
    // NPCBots only: the unit is an NPCBot or an NPCBot's pet. Legacy sites; false without NPCBots.
    bool IsNpcBotOrPet(Unit const* unit);
    // NPCBots only: the unit is an NPCBot, or a pet/summon/hunter pet whose owner is an NPCBot.
    // Legacy compound site (EligibleHealTarget / EligibleDamageTarget); false without NPCBots.
    bool IsNpcBotOrOwnedSummon(Unit const* unit);
    // NPCBots only: the NPCBot's owning player. Legacy GetBotOwner site; nullptr without NPCBots.
    Player* GetNpcBotOwner(Creature const* creature);
    // Compiled-in providers, for the module_loaded log line.
    char const* ProvidersString();
}

#endif
