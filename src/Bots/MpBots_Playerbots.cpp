#include "MpBotsProviders.h"

#if defined(MP_PROVIDER_PLAYERBOTS)

#include "Player.h"
#include "Playerbots.h"

// A playerbot is a Player with a PlayerbotAI attached, including self-bots (see IsRealPlayer in
// mod-playerbots/src/Bot/PlayerbotAI.cpp).
bool MpBots::Detail::PlayerbotsIsBot(Unit const* unit)
{
    Player const* player = unit->ToPlayer();
    return player && GET_PLAYERBOT_AI(const_cast<Player*>(player)) != nullptr;
}

#endif
