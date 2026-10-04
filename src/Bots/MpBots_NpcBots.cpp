#include "MpBotsProviders.h"

#if defined(MP_PROVIDER_NPCBOTS)
// Not compiled on the csm-kb core (no NPCBots); verified by review only. Bodies moved verbatim from the
// pre-seam NPCBots blocks in MythicPlus.cpp / UnitScript.cpp / PlayerScript.cpp.

#include "Creature.h"
#include "Player.h"

bool MpBots::Detail::NpcBotsIsBot(Unit const* unit) { return unit->IsNPCBot(); }
bool MpBots::Detail::NpcBotsIsBotOrPet(Unit const* unit) { return unit->IsNPCBotOrPet(); }
Player* MpBots::Detail::NpcBotsOwner(Creature const* creature) { return creature->GetBotOwner(); }

// Compound legacy condition from MpScaler::EligibleHealTarget / EligibleDamageTarget.
bool MpBots::Detail::NpcBotsIsBotOrOwnedSummon(Unit const* unit)
{
    if (unit->IsNPCBot())
        return true;

    // Null check for GetOwner to avoid dereferencing a null pointer
    return (unit->IsPet() || unit->IsSummon() || unit->IsHunterPet()) && unit->GetOwner() &&
           unit->GetOwner()->IsNPCBot();
}

#endif
