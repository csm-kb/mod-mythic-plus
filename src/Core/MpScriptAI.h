#include "Creature.h"
#include "CreatureAI.h"
#include "MpLog.h"
#include "MpTypes.h"
#include "ScriptMgr.h"
#include "ScriptedCreature.h"

#ifdef _ELUNA_CREATURE_AI_H
    #include "ElunaCreatureAI.h"
    using BaseAI = ElunaCreatureAI;
#else
    using BaseAI = ScriptedAI;
#endif

class MpScriptAI : public BaseAI
{
    MpDifficulty _difficulty;

public:
    MpScriptAI(Creature* creature, MpDifficulty difficulty) : BaseAI(creature) {
        _difficulty = difficulty;
    }

    MpScriptAI(Creature* creature) : BaseAI(creature) {
        _difficulty = MpDifficulty::MP_DIFFICULTY_MYTHIC;
    }

    void JustDied(Unit* killer) override {
        BaseAI::JustDied(killer);
    }

    void Reset() override {
        BaseAI::Reset();
    }
};
