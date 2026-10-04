#ifndef MP_BOTS_PROVIDERS_H
#define MP_BOTS_PROVIDERS_H

#if !defined(MP_NO_BOT_PROVIDERS)
#  if defined(MOD_PLAYERBOTS)
#    define MP_PROVIDER_PLAYERBOTS 1
#  endif
#  if defined(MOD_PRESENT_NPCBOTS) || defined(NPCBOT) || defined(NPC_BOT)
#    define MP_PROVIDER_NPCBOTS 1
#  endif
#endif

class Creature;
class Player;
class Unit;

namespace MpBots::Detail
{
#if defined(MP_PROVIDER_PLAYERBOTS)
    bool PlayerbotsIsBot(Unit const* unit);
#endif
#if defined(MP_PROVIDER_NPCBOTS)
    bool NpcBotsIsBot(Unit const* unit);
    bool NpcBotsIsBotOrPet(Unit const* unit);
    bool NpcBotsIsBotOrOwnedSummon(Unit const* unit);
    Player* NpcBotsOwner(Creature const* creature);
#endif
}

#endif
