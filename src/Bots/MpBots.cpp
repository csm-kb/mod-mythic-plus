#include "MpBots.h"
#include "MpBotsProviders.h"
#include "Creature.h"
#include "Player.h"

bool MpBots::IsBot(Unit const* unit)
{
    if (!unit)
        return false;
#if defined(MP_PROVIDER_PLAYERBOTS)
    if (Detail::PlayerbotsIsBot(unit))
        return true;
#endif
#if defined(MP_PROVIDER_NPCBOTS)
    if (Detail::NpcBotsIsBot(unit))
        return true;
#endif
    return false;
}

bool MpBots::IsNpcBot(Unit const* unit)
{
#if defined(MP_PROVIDER_NPCBOTS)
    return unit && Detail::NpcBotsIsBot(unit);
#else
    (void)unit;
    return false;
#endif
}

bool MpBots::IsNpcBotOrPet(Unit const* unit)
{
#if defined(MP_PROVIDER_NPCBOTS)
    return unit && Detail::NpcBotsIsBotOrPet(unit);
#else
    (void)unit;
    return false;
#endif
}

bool MpBots::IsNpcBotOrOwnedSummon(Unit const* unit)
{
#if defined(MP_PROVIDER_NPCBOTS)
    return unit && Detail::NpcBotsIsBotOrOwnedSummon(unit);
#else
    (void)unit;
    return false;
#endif
}

Player* MpBots::GetNpcBotOwner(Creature const* creature)
{
#if defined(MP_PROVIDER_NPCBOTS)
    return creature ? Detail::NpcBotsOwner(creature) : nullptr;
#else
    (void)creature;
    return nullptr;
#endif
}

char const* MpBots::ProvidersString()
{
#if defined(MP_PROVIDER_PLAYERBOTS) && defined(MP_PROVIDER_NPCBOTS)
    return "playerbots,npcbots";
#elif defined(MP_PROVIDER_PLAYERBOTS)
    return "playerbots";
#elif defined(MP_PROVIDER_NPCBOTS)
    return "npcbots";
#else
    return "none";
#endif
}
