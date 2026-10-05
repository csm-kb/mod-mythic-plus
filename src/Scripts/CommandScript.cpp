#include "Chat.h"
#include "AdvancementMgr.h"
#include "Group.h"
#include "MpScaler.h"
#include "MpConfig.h"
#include "MpConstants.h"
#include "MpLog.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "StringConvert.h"

#include <chrono>
#include <mutex>
#include <optional>
#include <unordered_map>

using namespace Acore::ChatCommands;

// Why a .mp set was rejected; the chat text to the player is unchanged
static void LogSetFailed(Player* player, char const* reason)
{
    MpLog::Info(MpLog::Area::Instance, "event=command_failed cmd=set reason={} player={} guid={}", reason,
        player->GetName(), player->GetGUID().ToString());
}

class MythicPlus_CommandScript : public CommandScript
{
public:
    MythicPlus_CommandScript() : CommandScript("MythicPlus_CommandScript")
    {
    }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable commandTableMain =
        {
            {"", HandleHelp, SEC_PLAYER, Console::No},
            {"status", HandleStatus, SEC_PLAYER, Console::No},
            {"set", HandleSetDifficulty, SEC_PLAYER, Console::No},
            {"disable", HandleDisable, SEC_ADMINISTRATOR, Console::Yes},
            {"enable", HandleEnable, SEC_ADMINISTRATOR, Console::Yes},
            {"rescale", HandleReScale, SEC_GAMEMASTER, Console::No},
            {"rescale all", HandleReScaleAll, SEC_GAMEMASTER, Console::No},
            {"change melee", HandleChangeMelee, SEC_ADMINISTRATOR, Console::No},
            {"change spell", HandleChangeSpell, SEC_ADMINISTRATOR, Console::No},
            {"change health", HandleChangeHealth, SEC_ADMINISTRATOR, Console::No}
        };

        static ChatCommandTable commandTable =
        {
            {"mp", commandTableMain},
            {"mythicplus", commandTableMain},
            {"mp debug", HandleDebug, SEC_PLAYER, Console::No},
            {"mp reload", HandleReload, SEC_GAMEMASTER, Console::No},
            {"advancement", HandleAdvancement, SEC_PLAYER, Console::No}
        };

        return commandTable;
    }

    static bool HandleHelp(ChatHandler* handler, std::vector<std::string> const& /*args*/)
    {
        std::string helpText = "Mythic+ Commands:\n"
            "  .mp status - show current global settings of Mythic+ mod\n"
            "  .mp set [normal, heroic, mythic,legendary,ascendant] - Set Mythic+ difficulty in current beta only "
                "supports mythic.\n"
            "  .mp [enable,disable] - enable or disable this mod\n"
            "  .mp - Show this help message\n";
        handler->PSendSysMessage(helpText);
        return true;
    }

    static bool HandleReload(ChatHandler* handler)
    {
        sMpRepo->LoadScaleFactors();
        handler->PSendSysMessage("Mythic+ scale factors updated.");

        return true;
    }

    static bool HandleDebug(ChatHandler* handler)
    {
        Player* player = handler->GetPlayer();
        if (!DebugCooldownOk(player->GetGUID()))
        {
            handler->PSendSysMessage("Mythic+: debug is rate-limited.");
            return true;
        }

        if (Creature* target = handler->getSelectedCreature())
        {
            if (target->IsControlledByPlayer() && target->GetCharmerOrOwnerGUID() != player->GetGUID())
            {
                handler->PSendSysMessage("Mythic+ does not track player pets.");
                return true;
            }

            return HandleDebugCreature(handler, target);
        }

        return HandleDebugInstance(handler, player);
    }

    // At most one debug reply per player every 2 seconds
    static bool DebugCooldownOk(ObjectGuid guid)
    {
        static std::mutex lock;
        static std::unordered_map<ObjectGuid, std::chrono::steady_clock::time_point> last;
        auto now = std::chrono::steady_clock::now();
        std::lock_guard<std::mutex> guard(lock);
        auto [it, inserted] = last.try_emplace(guid, now);
        if (!inserted && now - it->second < std::chrono::seconds(2))
            return false;

        it->second = now;
        return true;
    }

    // The caller's own instance: tier, multipliers, creature counts and deaths of the live group members
    static bool HandleDebugInstance(ChatHandler* handler, Player* player)
    {
        char const* hint = " Select a creature to see its stats.";
        Map* map = player->GetMap();
        if (!map->IsDungeon())
        {
            handler->PSendSysMessage("Mythic+ inactive: not in a dungeon.{}", hint);
            return true;
        }

        if (!sMpConfig->enabled)
        {
            handler->PSendSysMessage("Mythic+ inactive: module disabled.{}", hint);
            return true;
        }

        Group* group = player->GetGroup();
        std::optional<MpGroupData> groupData;
        if (group)
            groupData = sMpState->GetGroupData(group->GetGUID());

        std::optional<MpInstanceData> data = sMpState->GetInstanceData(map->GetId(), map->GetInstanceId());
        if (!data)
        {
            if (groupData && sMpConfig->GetTier(groupData->difficulty))
                handler->PSendSysMessage("Mythic+ inactive: group tier is {} but this instance predates it. "
                    "Leave and reset instances.{}", MpDifficultyName(groupData->difficulty), hint);
            else
                handler->PSendSysMessage("Mythic+ inactive: your group has no tier. Leader: .mp set mythic "
                    "outside, then enter.{}", hint);
            return true;
        }

        MpCreatureCounts counts = sMpState->CountInstanceCreatures(map->GetId(), map->GetInstanceId());
        handler->PSendSysMessage("Mythic+ {} ({} base) \xE2\x80\x94 {}, instance {}",
            MpDifficultyName(data->difficulty), map->IsHeroic() ? "heroic" : "normal", map->GetMapName(),
            map->GetInstanceId());
        handler->PSendSysMessage("Trash x hp{} melee{} spell{} armor{}, lvl {}", data->creature.health,
            data->creature.melee, data->creature.spell, data->creature.armor, uint32(data->creature.avgLevel));
        handler->PSendSysMessage("Boss  x hp{} melee{} spell{} armor{}, lvl {}", data->boss.health,
            data->boss.melee, data->boss.spell, data->boss.armor, uint32(data->boss.avgLevel));
        handler->PSendSysMessage("Creatures: {} scaled, {} pending", counts.scaled, counts.pending);

        std::string deaths;
        uint32 total = 0;
        if (group)
        {
            for (Group::MemberSlot const& slot : group->GetMemberSlots())
            {
                std::optional<MpPlayerData> pd = sMpState->GetPlayerData(slot.guid);
                uint32 n = pd ? pd->GetDeaths(map->GetId(), map->GetInstanceId()) : 0;
                total += n;
                if (n)
                    deaths += Acore::StringFormat("{}{} {}", deaths.empty() ? "" : ", ", slot.name, n);
            }
        }

        handler->PSendSysMessage("Deaths {} (limit {}, not enforced){}{}", total, data->deathLimits,
            deaths.empty() ? "" : ": ", deaths);
        return true;
    }

    static bool HandleDebugCreature(ChatHandler* handler, Creature* target)
    {
        CreatureTemplate const* creatureTemplate = target->GetCreatureTemplate();
        std::optional<MpCreatureData> creatureData = sMpState->GetCreatureData(target);

        handler->PSendSysMessage(LANG_NPCINFO_LEVEL, target->GetLevel());
        handler->PSendSysMessage(LANG_NPCINFO_HEALTH, target->GetCreateHealth(), target->GetMaxHealth(),
            target->GetHealth());
        handler->PSendSysMessage("WeaponDmg Main {} - {}",
            target->GetWeaponDamageRange(BASE_ATTACK, MINDAMAGE),
            target->GetWeaponDamageRange(BASE_ATTACK, MAXDAMAGE)
        );
        handler->PSendSysMessage("WeaponDmg Range {} - {}",
            target->GetWeaponDamageRange(RANGED_ATTACK, MINDAMAGE),
            target->GetWeaponDamageRange(RANGED_ATTACK, MAXDAMAGE)
        );
        handler->PSendSysMessage("WeaponDmg Offhand {} - {}",
            target->GetWeaponDamageRange(OFF_ATTACK, MINDAMAGE),
            target->GetWeaponDamageRange(OFF_ATTACK, MAXDAMAGE)
        );
        handler->PSendSysMessage("Attack Power Main {}",
            target->GetFlatModifierValue(UNIT_MOD_ATTACK_POWER, BASE_VALUE));
        handler->PSendSysMessage("Attack Power Ranged {}",
            target->GetFlatModifierValue(UNIT_MOD_ATTACK_POWER_RANGED, BASE_VALUE));
        handler->PSendSysMessage("Armor {}", target->GetArmor());
        handler->PSendSysMessage("Damage Modifier on template {}",creatureTemplate->DamageModifier);

        if (creatureData)
        {
            handler->PSendSysMessage("CreatureData: {}", creatureData->ToString());
        }

        return true;
    }

    // sets the difficluty for the group
    static bool HandleSetDifficulty(ChatHandler* handler, std::vector<std::string> const& args)
    {
        Player* player = handler->GetSession()->GetPlayer();
        Group* group = player->GetGroup();

        if (!group)
        {
            LogSetFailed(player, "no_group");
            handler->PSendSysMessage("|cFFFF0000 You must be in a group to be able to set a Mythic+ difficulty.");
            return true;
        }

        if (args.empty())
        {
            LogSetFailed(player, "bad_arg");
            handler->PSendSysMessage(
                "|cFFFF0000 You must specify a difficulty level. Expected values are 'mythic', 'legendary', or "
                "'ascendant'.");
            return true;
        }

        std::string difficulty = args[0];

        if (!group->IsLeader(player->GetGUID()))
        {
            LogSetFailed(player, "not_leader");
            handler->PSendSysMessage("|cFFFF0000 You must be the group leader to set a Mythic+ difficulty.");
            return true;
        }

        if (player->GetMap()->IsDungeon())
        {
            LogSetFailed(player, "inside_dungeon");
            player->ResetInstances(player->GetGUID(), INSTANCE_RESET_CHANGE_DIFFICULTY, false);
            player->SendResetInstanceSuccess(player->GetMap()->GetId());
            return true;
        }

        if (difficulty == "mythic")
        {
            sMpState->SetGroupData(group, MpGroupData(group->GetGUID(), MP_DIFFICULTY_MYTHIC));
        }
        else if (difficulty == "legendary")
        {
            sMpState->SetGroupData(group, MpGroupData(group->GetGUID(), MP_DIFFICULTY_LEGENDARY));
        }
        else if (difficulty == "ascendant")
        {
            sMpState->SetGroupData(group, MpGroupData(group->GetGUID(), MP_DIFFICULTY_ASCENDANT));
        }
        else if (difficulty == "heroic")
        {
            sMpState->RemoveGroupData(group->GetGUID());
            sMpRepo->DBRemoveGroupData(group->GetGUID());
            group->SetDungeonDifficulty(DUNGEON_DIFFICULTY_HEROIC);
        }
        else if (difficulty == "normal")
        {
            sMpState->RemoveGroupData(group->GetGUID());
            sMpRepo->DBRemoveGroupData(group->GetGUID());
            group->SetDungeonDifficulty(DUNGEON_DIFFICULTY_NORMAL);
        }
        else
        {
            LogSetFailed(player, "bad_arg");
            handler->PSendSysMessage(
                "|cFFFF0000 Invalid difficulty level. Expected values are 'normal', 'heroic', 'mythic', 'legendary', "
                "or 'ascendant'.");
            return true;
        }

        handler->PSendSysMessage("Mythic+ difficulty set to: " + difficulty);
        return true;
    }

    static bool HandleStatus(ChatHandler* handler)
    {
        Player* player = handler->GetPlayer();

        Map* map = player->GetMap();
        uint32 mapId = player->GetMapId();

        std::string status = Acore::StringFormat(
            "Mythic+ Status:\n Mythic+ Enabled: {}\n Mythic+ Item Rewards: {}\n Mythic+ DeathLimits: {}\n",
            std::string((sMpConfig->enabled) ? "Yes" : "No"),
            std::string((sMpConfig->enableItemRewards) ? "Yes" : "No"),
            std::string((sMpConfig->enableDeathLimits) ? "Yes" : "No")
        );

        if (player->GetGroup())
        {
            ObjectGuid groupGuid = player->GetGroup()->GetGUID();
            std::optional<MpGroupData> groupData = sMpState->GetGroupData(groupGuid);
            if (groupData)
            {
                MpScaleFactor scaleFactors{};

                if (map->IsDungeon())
                {
                    scaleFactors = sMpRepo->GetScaleFactor(mapId, groupData->difficulty);
                }

                status += Acore::StringFormat("  Group Difficulty: {}\n Group Deaths: {}\n  Scale FactorStr {}\n",
                    (groupData->difficulty) ? groupData->difficulty : 0,
                    sMpState->GetGroupDeaths(groupGuid, player->GetMapId(), player->GetInstanceId()),
                    scaleFactors.ToString()
                );
            }
            else
            {
                status += "  Group Difficulty: Not Set\n";
            }
        }

        handler->PSendSysMessage(status);
        return true;
    }

    static bool HandleReScale(ChatHandler* handler)
    {
        Creature* creature = handler->getSelectedCreature();
        if (!creature)
        {
            handler->PSendSysMessage("You must select a creature to rescale.");
            return true;
        }

        if (!sMpState->GetCreatureData(creature))
        {
            handler->PSendSysMessage("Creature is not eligible for rescaling.");
            return true;
        }

        std::optional<MpInstanceData> instanceData = sMpState->GetInstanceData(creature->GetMapId(),
            creature->GetInstanceId());
        if (!instanceData)
        {
            handler->PSendSysMessage("No instance data found for this creature.");
            return true;
        }

        if (creature->IsDungeonBoss() || creature->GetEntry() == MpConstants::HEADLESS_HORSEMAN)
        {
            sMpScaler->ScaleCreature(creature->GetLevel(), creature, &instanceData->boss, instanceData->difficulty);
        }
        else
        {
            sMpScaler->ScaleCreature(creature->GetLevel(), creature, &instanceData->creature, instanceData->difficulty);
        }

        handler->PSendSysMessage("Creature rescaled: {}", creature->GetName());

        return true;
    }

    static bool HandleReScaleAll(ChatHandler* handler)
    {
        Player* player = handler->GetPlayer();
        if (!player)
        {
            handler->PSendSysMessage("You must be a player to rescale all creatures.");
            return true;
        }

        Map* map = player->GetMap(); // never null: GetMap() asserts

        int32 mapId = map->GetId();
        int32 instanceId = map->GetInstanceId();

        std::optional<MpInstanceData> instanceData = sMpState->GetInstanceData(mapId, instanceId);
        if (!instanceData)
        {
            handler->PSendSysMessage("No mythic instance data found for this map.");
            return true;
        }

        sMpScaler->ScaleAll(player, *instanceData);
        handler->PSendSysMessage("All creatures rescaled.");

        return true;
    }

    static bool HandleDisable(ChatHandler* handler)
    {
        MpLog::Debug(MpLog::Area::Instance, "HandleDisable()");
        sMpConfig->enabled = false;
        handler->SendSysMessage("Mythic+ mod has been disabled.");
        return true;
    }

    static bool HandleEnable(ChatHandler* handler)
    {
        MpLog::Debug(MpLog::Area::Instance, "HandleEnable()");
        sMpConfig->enabled = true;
        handler->SendSysMessage("Mythic+ mod has been enabled.");
        return true;
    }

    static bool HandleChangeMelee(ChatHandler* handler, std::vector<std::string> const& args)
    {
        if (args.empty())
        {
            handler->PSendSysMessage("|cFFFF0000 You must specify a value to set the melee scale factor.");
            return true;
        }

        Player* player = handler->GetSession()->GetPlayer();
        if (!player)
        {
            handler->PSendSysMessage("|cFFFF0000 Invalid session or player.");
            return true;
        }

        if (player->GetGroup())
        {
            std::optional<MpGroupData> groupData = sMpState->GetGroupData(player->GetGroup()->GetGUID());

            if (groupData)
            {
                Optional<float> parsed = Acore::StringTo<float>(args[0]);
                if (!parsed || *parsed <= 0.0f)
                {
                    handler->PSendSysMessage("|cFFFF0000 Invalid number: {}", args[0]);
                    return true;
                }

                float value = *parsed;
                sMpRepo->SetMeleeScaleFactor(player->GetMapId(), groupData->difficulty, value);
                handler->PSendSysMessage(Acore::StringFormat("Melee scale factor set to: {}", value));
                return true;
            }
        }

        handler->PSendSysMessage("|cFFFF0000 You must be in a group and mythic+ instance to set a melee scale factor.");
        return true;
    }

    static bool HandleChangeSpell(ChatHandler* handler, std::vector<std::string> const& args)
    {
        if (args.empty())
        {
            handler->PSendSysMessage("|cFFFF0000 You must specify a value to set the spell scale factor.");
            return true;
        }

        Player* player = handler->GetSession()->GetPlayer();
        if (!player)
        {
            handler->PSendSysMessage("|cFFFF0000 Invalid session or player.");
            return true;
        }

        if (player->GetGroup())
        {
            std::optional<MpGroupData> groupData = sMpState->GetGroupData(player->GetGroup()->GetGUID());

            if (groupData)
            {
                Optional<float> parsed = Acore::StringTo<float>(args[0]);
                if (!parsed || *parsed <= 0.0f)
                {
                    handler->PSendSysMessage("|cFFFF0000 Invalid number: {}", args[0]);
                    return true;
                }

                float value = *parsed;
                sMpRepo->SetSpellScaleFactor(player->GetMapId(), groupData->difficulty, value);
                handler->PSendSysMessage(Acore::StringFormat("Spell scale factor set to: {}", value));
                return true;
            }
        }

        handler->PSendSysMessage(
            "|cFFFF0000 You must be in a group and mythic+ instance to set a spell scale factor.");
        return true;
    }

    static bool HandleChangeHealth(ChatHandler* handler, std::vector<std::string> const& args)
    {
        if (args.empty())
        {
            handler->PSendSysMessage("|cFFFF0000 You must specify a value to set the health scale factor.");
            return true;
        }

        Player* player = handler->GetSession()->GetPlayer();
        if (!player)
        {
            handler->PSendSysMessage("|cFFFF0000 Invalid session or player.");
            return true;
        }

        if (player->GetGroup())
        {
            std::optional<MpGroupData> groupData = sMpState->GetGroupData(player->GetGroup()->GetGUID());

            if (groupData)
            {
                Optional<float> parsed = Acore::StringTo<float>(args[0]);
                if (!parsed || *parsed <= 0.0f)
                {
                    handler->PSendSysMessage("|cFFFF0000 Invalid number: {}", args[0]);
                    return true;
                }

                float value = *parsed;
                sMpRepo->SetHealthScaleFactor(player->GetMapId(), groupData->difficulty, value);
                handler->PSendSysMessage(Acore::StringFormat("Health scale factor set to: {}", value));
                return true;
            }
        }

        handler->PSendSysMessage(
            "|cFFFF0000 You must be in a group and mythic+ instance to set a health scale factor.");
        return true;
    }

    static bool HandleAdvancement(ChatHandler* handler)
    {
        Player* player = handler->GetSession()->GetPlayer();
        std::string message = "";

        for (int i =0; i < MpAdvancements::MP_ADV_MAX; i++)
        {
            std::optional<MpPlayerRank> playerRank = sAdvancementMgr->GetPlayerAdvancementRank(player,
                static_cast<MpAdvancements>(i));
            if (!playerRank)
            {
                continue;
            }

            message += Acore::StringFormat("Your Advancement Bonuses: \n {}: {} bonus: {}",
                MpAdvancementsToString(static_cast<MpAdvancements>(i)), playerRank->rank, playerRank->bonus);
        }

        if (message.empty())
        {
            message = "You have no advancements.";
        }

        handler->PSendSysMessage(message);

        return true;
    }
};

void Add_MP_CommandScripts()
{
    MpLog::Debug(MpLog::Area::Instance, "Add_MP_CommandScripts()");
    new MythicPlus_CommandScript();
}
