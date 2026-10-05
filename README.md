# Mythic Plus Module for AzerothCore
📣 **long overdue update**: This has been a passion project and relaxing output of my time. I was laid off, had to get surgery, then get a real job again at a high demand start-up that has uses up all my programming energy. 

I also took a pause on this to build out some new action based mechanics for encounters, really early days on that and experimental.

I expect to come back to working on this project in a few months when as I am really getting the itch to revisit and **finish this**.

So what does that mean?

**Do the boring stuff**:
1. upgrade everything to latest version of npc bots and patch any problems.
2. start a fresh version not modified version and install as well as script a custom installer for the sql needs.
3. Write thorough docs on how to install this.

**Finish the content:**
1. Finish the remaining NPC trainers for new leveling tradeskills and make them more accessible.
2. Retool the items one final time simplifying the sprawl that happened while I iterated on item creator.
3. Add in the legendary and ascendant paths I always intended to complete

That list seems short but it is not, low effort or small amount of time I assure you. 

Once I have checked those off... I am going to call this done.  

I will fix bugs and put into maintainer mode, so only upgrading from new patches down from acore trickerrer, as I can.

👊 If you want to help me accomplish this to make it happen faster and have some programming skills or cash for AI credits to generate code, please DM me on discord, I am in all the WoW channels. 

Look forward to completing this project and giving it to the rest of my nostalgic gamers out there.

-- Volek

🚨 **This is not mythic like in retail.** 🚨

This module started with the intention of creating a retail-like mythic plus system but has evolved into something unique. Instead, think of it as an **expansion of existing Wrath to new end game conte r** that reuses much of the existing dungeons and creatures, overhauling them to much higher difficulties with new loot mechanics.

## Fork

This is a fork maintained at [csm-kb/mod-mythic-plus](https://github.com/csm-kb/mod-mythic-plus) of
[araxiaonline/mod-mythic-plus](https://github.com/araxiaonline/mod-mythic-plus) (the `upstream` remote). The work
branch `fork/compile-and-tidy` starts from `upstream/araxia-merge-20260707` (the core-port branch), not from `main`.
It restructures the code into bounded units, fixes the defects found while doing so, makes the build and the
documentation agree with the code, and adds structured logging and the `.mp debug` instance inspector. Tier
semantics, scaling formulas, lockouts and loot behavior are unchanged, apart from the defect fixes listed in the git
history and three config defaults that were resolved to the more recently changed side (`Mythic.DungeonArmor` 1.25,
`Mythic.DeathAllowance` 1000, `DiminishingExponent` 0.96).

## What it does

- `.mp set mythic|legendary|ascendant` (group leader, outside a dungeon) stores a tier for the group. When that group
  enters a dungeon, creatures are scaled to a flat tier level with per-tier health, melee, spell and armor
  multipliers (bosses have their own set).
- Per-map tuning lives in the `mp_scale_factors` table (`acore_world`), applied on top of the config values.
- Item drops are re-mapped to a tier item (original entry + a per-tier offset).
- Players spend materials on advancement ranks (`.advancement`, and the client event channel).
- Deaths are counted and shown, but the death limit is **not enforced yet** (see Death System).

Mythic, Legendary and Ascendant share the base dungeon difficulty's lockout. A group's tier is held in memory only.

## Installation (docker)

Back up your databases first: the module's world SQL (items, stat overrides, NPCs) is applied to your real world
database the first time `ac-db-import` runs with the module in the image.
`04_creature_classlevelstats.sql` sets creature stats for levels 84+ only; stock rows for levels 81-83 are untouched.

1. Put the module in `modules/mod-mythic-plus`.
2. `docker compose build ac-worldserver ac-db-import` (the module is compiled into the worldserver image).
3. `docker compose up -d`. `ac-db-import` applies the module SQL (`data/sql/db-world`, `data/sql/db-characters`)
   before the worldserver starts.
4. Optionally copy `conf/mod-mythic-plus.conf.dist` to `env/dist/etc/modules/mod-mythic-plus.conf` and edit it.

dbimport records every applied SQL file by content hash; a file whose bytes change is applied again. Never edit an
applied `base/` file at all: `01_mp_schema.sql` drops all seven `mp_*` tables (`DROP TABLE IF EXISTS mp_group_data`
first, then the player, run, stats and advancement tables), so re-running it wipes player data. New SQL goes in a
new file under `data/sql/db-*/updates/`. `.gitattributes` pins `*.sql` to the stored LF bytes (`-text`), so do not
hand-edit or convert SQL line endings.

For a non-docker build, clone the module into `modules/`, rebuild the core, and let the normal `dbimport` apply the
SQL.

### Bot frameworks

The module builds with mod-playerbots, with NPCBots, with both, or with neither; the choice is made at compile
time from the macros the core defines (`MOD_PLAYERBOTS`, `MOD_PRESENT_NPCBOTS`). The `bots=` field of the
`module_loaded` log line shows what was compiled in (`playerbots`, `npcbots`, `playerbots,npcbots` or `none`).
`-DMP_NO_BOT_PROVIDERS=ON` forces `none`. All of this is confined to `src/Bots/`.

### Optional custom-DBC models

`data/sql/optional/araxia-custom-dbc/11_custom_npc_models.sql` holds the `creature_template_model` and
`creature_model_info` rows for the custom display IDs 9500561-9500568. It is **not** applied by dbimport: those
display IDs exist only in Araxia's client patch (custom DBC). Without them, NPCs 9500562-9500568 are invisible on a
stock 3.3.5a client. Apply the file by hand only if your clients carry that patch. It keeps a hard-coded
`acore_world.` schema prefix on its last statement, so edit that if your world database has another name.

If your database already imported an earlier version of `db-world/base/11_custom_npcs.sql`, it contains
`creature_model_info` rows for DisplayID 9500561-9500568 that a stock client cannot resolve, and the worldserver
segfaults while loading them. Remove them manually (and the matching model rows) before starting:

```sql
DELETE FROM creature_model_info WHERE DisplayID BETWEEN 9500561 AND 9500568;
DELETE FROM creature_template_model WHERE CreatureID BETWEEN 9500562 AND 9500568;
```

## Commands

`.mp` and `.mythicplus` are the same command. `debug` and `reload` are registered as `.mp debug` and `.mp reload`
only (there is no `.mythicplus debug`).

| Command | Security | Console | What it does |
|---|---|---|---|
| `.mp` | player | no | Help text. |
| `.mp status` | player | no | Module flags, and your group's tier, deaths and scale factors. |
| `.mp set <normal\|heroic\|mythic\|legendary\|ascendant>` | player (group leader) | no | Sets the group tier. `normal`/`heroic` clear the tier and set the dungeon difficulty. Inside a dungeon it only resets instances. |
| `.mp debug` | player | no | Instance inspector (nothing selected) or creature stat dump (creature selected). Rate limited to one use per 2 seconds. |
| `.mp reload` | game master | no | Reloads the `mp_scale_factors` table only. |
| `.mp rescale` | game master | no | Rescales the selected creature. |
| `.mp rescale all` | game master | no | Rescales every tracked creature on your map. |
| `.mp enable` / `.mp disable` | administrator | yes | Turns the module on or off at runtime. |
| `.mp change melee\|spell\|health <value>` | administrator | no | Sets a scale factor for your current Mythic+ instance. A non-number is rejected. |
| `.advancement` | player | no | Lists your advancement ranks and bonuses. |

There are no token, material or achievement commands.

`.mp debug` with nothing selected prints at most five lines for the instance you are in: the tier and base
difficulty, trash and boss multipliers and level, how many creatures are scaled, and the group's deaths. If the run
is not scaled it says why (not in a dungeon, module disabled, your group has no tier, or the group tier postdates
the instance). With a creature selected it prints that creature's level, health, weapon damage, attack power and
armor. It never takes a player or instance argument and never changes anything.

## Configuration

Copy `conf/mod-mythic-plus.conf.dist` to `etc/modules/mod-mythic-plus.conf`. Every key can also be set with the
environment variable in the last column (the core maps `MythicPlus.Mythic.DungeonHealth` to
`AC_MYTHIC_PLUS_MYTHIC_DUNGEON_HEALTH`). The defaults below are the code defaults in `src/Core/MpConfig.cpp`; a
configure-time check in `mod-mythic-plus.cmake` warns when a key or default there differs from conf.dist.

A value that is out of range or not a number is logged as `config_invalid` (WARN) and the default is used.

`.reload config` (game master, core command) re-reads this file and logs `config_reloaded`. `.mp reload` is separate:
it reloads only the `mp_scale_factors` database table.

| Key | Default | Rule | Env var |
|---|---|---|---|
| `MythicPlus.Enabled` | 1 | 1 = on, 0 = off | `AC_MYTHIC_PLUS_ENABLED` |
| `MythicPlus.EnableItemRewards` | 1 | 1 = on, 0 = off | `AC_MYTHIC_PLUS_ENABLE_ITEM_REWARDS` |
| `MythicPlus.EnableDeathLimits` | 1 | 1 = on, 0 = off. Shown in .mp status only; not enforced yet | `AC_MYTHIC_PLUS_ENABLE_DEATH_LIMITS` |
| `MythicPlus.Mythic.DungeonHealth` | 1.25 | > 0 | `AC_MYTHIC_PLUS_MYTHIC_DUNGEON_HEALTH` |
| `MythicPlus.Mythic.DungeonMelee` | 1.25 | > 0 | `AC_MYTHIC_PLUS_MYTHIC_DUNGEON_MELEE` |
| `MythicPlus.Mythic.DungeonBaseDamage` | 1.50 | > 0 | `AC_MYTHIC_PLUS_MYTHIC_DUNGEON_BASE_DAMAGE` |
| `MythicPlus.Mythic.DungeonSpell` | 1.15 | > 0 | `AC_MYTHIC_PLUS_MYTHIC_DUNGEON_SPELL` |
| `MythicPlus.Mythic.DungeonArmor` | 1.25 | > 0 | `AC_MYTHIC_PLUS_MYTHIC_DUNGEON_ARMOR` |
| `MythicPlus.Mythic.DungeonAvgLevel` | 83 | 1-255 | `AC_MYTHIC_PLUS_MYTHIC_DUNGEON_AVG_LEVEL` |
| `MythicPlus.Mythic.DungeonBossHealth` | 1.50 | > 0 | `AC_MYTHIC_PLUS_MYTHIC_DUNGEON_BOSS_HEALTH` |
| `MythicPlus.Mythic.DungeonBossMelee` | 1.35 | > 0 | `AC_MYTHIC_PLUS_MYTHIC_DUNGEON_BOSS_MELEE` |
| `MythicPlus.Mythic.DungeonBossBaseDamage` | 2.00 | > 0 | `AC_MYTHIC_PLUS_MYTHIC_DUNGEON_BOSS_BASE_DAMAGE` |
| `MythicPlus.Mythic.DungeonBossSpell` | 1.25 | > 0 | `AC_MYTHIC_PLUS_MYTHIC_DUNGEON_BOSS_SPELL` |
| `MythicPlus.Mythic.DungeonBossArmor` | 1.35 | > 0 | `AC_MYTHIC_PLUS_MYTHIC_DUNGEON_BOSS_ARMOR` |
| `MythicPlus.Mythic.DungeonBossLevel` | 85 | 1-255 | `AC_MYTHIC_PLUS_MYTHIC_DUNGEON_BOSS_LEVEL` |
| `MythicPlus.Legendary.DungeonHealth` | 2.25 | > 0 | `AC_MYTHIC_PLUS_LEGENDARY_DUNGEON_HEALTH` |
| `MythicPlus.Legendary.DungeonMelee` | 2.25 | > 0 | `AC_MYTHIC_PLUS_LEGENDARY_DUNGEON_MELEE` |
| `MythicPlus.Legendary.DungeonBaseDamage` | 2.75 | > 0 | `AC_MYTHIC_PLUS_LEGENDARY_DUNGEON_BASE_DAMAGE` |
| `MythicPlus.Legendary.DungeonSpell` | 2.25 | > 0 | `AC_MYTHIC_PLUS_LEGENDARY_DUNGEON_SPELL` |
| `MythicPlus.Legendary.DungeonArmor` | 2.25 | > 0 | `AC_MYTHIC_PLUS_LEGENDARY_DUNGEON_ARMOR` |
| `MythicPlus.Legendary.DungeonAvgLevel` | 85 | 1-255 | `AC_MYTHIC_PLUS_LEGENDARY_DUNGEON_AVG_LEVEL` |
| `MythicPlus.Legendary.DungeonBossHealth` | 2.25 | > 0 | `AC_MYTHIC_PLUS_LEGENDARY_DUNGEON_BOSS_HEALTH` |
| `MythicPlus.Legendary.DungeonBossMelee` | 2.25 | > 0 | `AC_MYTHIC_PLUS_LEGENDARY_DUNGEON_BOSS_MELEE` |
| `MythicPlus.Legendary.DungeonBossBaseDamage` | 4.00 | > 0 | `AC_MYTHIC_PLUS_LEGENDARY_DUNGEON_BOSS_BASE_DAMAGE` |
| `MythicPlus.Legendary.DungeonBossSpell` | 2.25 | > 0 | `AC_MYTHIC_PLUS_LEGENDARY_DUNGEON_BOSS_SPELL` |
| `MythicPlus.Legendary.DungeonBossArmor` | 3.25 | > 0 | `AC_MYTHIC_PLUS_LEGENDARY_DUNGEON_BOSS_ARMOR` |
| `MythicPlus.Legendary.DungeonBossLevel` | 87 | 1-255 | `AC_MYTHIC_PLUS_LEGENDARY_DUNGEON_BOSS_LEVEL` |
| `MythicPlus.Ascendant.DungeonHealth` | 3.25 | > 0 | `AC_MYTHIC_PLUS_ASCENDANT_DUNGEON_HEALTH` |
| `MythicPlus.Ascendant.DungeonMelee` | 3.25 | > 0 | `AC_MYTHIC_PLUS_ASCENDANT_DUNGEON_MELEE` |
| `MythicPlus.Ascendant.DungeonBaseDamage` | 4.75 | > 0 | `AC_MYTHIC_PLUS_ASCENDANT_DUNGEON_BASE_DAMAGE` |
| `MythicPlus.Ascendant.DungeonSpell` | 3.25 | > 0 | `AC_MYTHIC_PLUS_ASCENDANT_DUNGEON_SPELL` |
| `MythicPlus.Ascendant.DungeonArmor` | 3.25 | > 0 | `AC_MYTHIC_PLUS_ASCENDANT_DUNGEON_ARMOR` |
| `MythicPlus.Ascendant.DungeonAvgLevel` | 87 | 1-255 | `AC_MYTHIC_PLUS_ASCENDANT_DUNGEON_AVG_LEVEL` |
| `MythicPlus.Ascendant.DungeonBossHealth` | 3.25 | > 0 | `AC_MYTHIC_PLUS_ASCENDANT_DUNGEON_BOSS_HEALTH` |
| `MythicPlus.Ascendant.DungeonBossMelee` | 3.25 | > 0 | `AC_MYTHIC_PLUS_ASCENDANT_DUNGEON_BOSS_MELEE` |
| `MythicPlus.Ascendant.DungeonBossBaseDamage` | 6.00 | > 0 | `AC_MYTHIC_PLUS_ASCENDANT_DUNGEON_BOSS_BASE_DAMAGE` |
| `MythicPlus.Ascendant.DungeonBossSpell` | 3.25 | > 0 | `AC_MYTHIC_PLUS_ASCENDANT_DUNGEON_BOSS_SPELL` |
| `MythicPlus.Ascendant.DungeonBossArmor` | 3.25 | > 0 | `AC_MYTHIC_PLUS_ASCENDANT_DUNGEON_BOSS_ARMOR` |
| `MythicPlus.Ascendant.DungeonBossLevel` | 90 | 1-255 | `AC_MYTHIC_PLUS_ASCENDANT_DUNGEON_BOSS_LEVEL` |
| `MythicPlus.Mythic.DeathAllowance` | 1000 | Stored and shown in .mp status only; not enforced yet | `AC_MYTHIC_PLUS_MYTHIC_DEATH_ALLOWANCE` |
| `MythicPlus.Legendary.DeathAllowance` | 30 | Stored and shown in .mp status only; not enforced yet | `AC_MYTHIC_PLUS_LEGENDARY_DEATH_ALLOWANCE` |
| `MythicPlus.Ascendant.DeathAllowance` | 15 | Stored and shown in .mp status only; not enforced yet | `AC_MYTHIC_PLUS_ASCENDANT_DEATH_ALLOWANCE` |
| `MythicPlus.Mythic.ItemOffset` | 20000000 | > 0 | `AC_MYTHIC_PLUS_MYTHIC_ITEM_OFFSET` |
| `MythicPlus.Legendary.ItemOffset` | 21000000 | > 0 | `AC_MYTHIC_PLUS_LEGENDARY_ITEM_OFFSET` |
| `MythicPlus.Ascendant.ItemOffset` | 22000000 | > 0 | `AC_MYTHIC_PLUS_ASCENDANT_ITEM_OFFSET` |
| `MythicPlus.DiminishingExponent` | 0.96 | > 0 | `AC_MYTHIC_PLUS_DIMINISHING_EXPONENT` |
| `MythicPlus.DiminishingThreshold.Mythic` | 10000 | 0 or more | `AC_MYTHIC_PLUS_DIMINISHING_THRESHOLD_MYTHIC` |
| `MythicPlus.DiminishingThreshold.Legendary` | 20000 | 0 or more | `AC_MYTHIC_PLUS_DIMINISHING_THRESHOLD_LEGENDARY` |
| `MythicPlus.DiminishingThreshold.Ascendant` | 40000 | 0 or more | `AC_MYTHIC_PLUS_DIMINISHING_THRESHOLD_ASCENDANT` |
| `MythicPlus.ElementalMeleeReducer` | 0.50 | > 0 | `AC_MYTHIC_PLUS_ELEMENTAL_MELEE_REDUCER` |
| `MythicPlus.NormalEnemyReducer` | 0.50 | > 0 | `AC_MYTHIC_PLUS_NORMAL_ENEMY_REDUCER` |
| `MythicPlus.NonCreatureSpellReducer` | 0.50 | > 0 | `AC_MYTHIC_PLUS_NON_CREATURE_SPELL_REDUCER` |

`EnableDeathLimits` and the three `DeathAllowance` keys are stored and shown in `.mp status`, but nothing enforces
them.

## Death System

Deaths in a tiered instance are counted per group and per player and are shown by `.mp status` and `.mp debug`
(`Deaths 3 (limit 1000, not enforced)`). The limit is **not enforced yet**: reaching it does not kick anyone or fail
the run. Enforcement is deferred to the tier-layering work.

## Logging

Seven loggers, all children of `module.MythicPlus`:
`module.MythicPlus.config`, `.instance`, `.scaling`, `.loot`, `.advancement`, `.events` and `.combat`.
`combat` carries every per-hit and spell-math message at DEBUG. To see one area's DEBUG output add a line like this
to `worldserver.conf` (Logger keys cannot be set with `AC_*` environment variables); DEBUG goes to `Server.log`
because the console appender is capped at INFO:

```ini
Logger.module.MythicPlus.scaling=5,Server
```

Log lines are `event=<name> key=value ...`:

| Question | Event | Level |
|---|---|---|
| Did the module start healthy? | `module_loaded enabled= bots= scale_factors= advancement_ranks= material_types= config_warnings= [invalid_keys=]` | INFO (WARN if warnings) |
| Did a reload change validity? | `config_reloaded config_warnings= [invalid_keys=]` | INFO/WARN |
| Is a config value bad? | `config_invalid key= value= rule= default=` | WARN |
| Did this run get a tier? | `instance_tier_applied map= instance= group= leader= tier=<name> base=normal\|heroic trash=hp:...,melee:...,spell:...,armor:...,lvl:... boss=... death_limit=` | INFO |
| Why didn't this run scale? | `instance_untiered map= instance= group= leader= player= guid= reason=no_group\|no_group_tier\|module_disabled` | INFO for humans, DEBUG for bots |
| Why was a command rejected? | `command_failed cmd= reason= player= guid=` | INFO |
| Why did this creature scale? | `creature_scaled map= instance= entry= level= hp=`, logged whenever a creature is scaled: on add-to-world, on respawn, when an unscaled creature is scaled late, when the instance gets its tier (`ScaleRemaining`), and on `.mp rescale` and `.mp rescale all` | DEBUG |
| Why didn't this creature scale? | `creature_skipped map= instance= entry= reason=` (on add-to-world, tiered instances only) | DEBUG |
| Advancement failure? | `advancement_failed reason=rank_not_found\|save_failed\|history_save_failed player= guid= ...` | ERROR |
| Loot failure? | `loot_failed reason=no_instance_settings map= instance= player= guid=` / `loot_failed reason=offset_item_not_found new_item= original_item= name=` | WARN |

Instance lines carry `map= instance= group=`; players are logged as `player=<name> guid=<guid>`.

## Troubleshooting

**Why didn't my run scale?** Run `.mp debug` with nothing selected inside the dungeon: it names the reason. The log
has the same answer as `event=instance_untiered reason=...` on `module.MythicPlus.instance`. Usual causes: the group
had no tier when it entered (the leader must run `.mp set mythic` **outside** the dungeon, before entering), or the
instance was created before the tier was set (leave, reset instances, re-enter).

**`.mp set` was rejected.** Look for `event=command_failed cmd=set reason=...` (`no_group`, `bad_arg`, `not_leader`,
`inside_dungeon`).

**Is the module loaded, and with which bots?** Look for `event=module_loaded` at startup. `config_warnings` greater
than zero means a config value is invalid; `invalid_keys=` names them.

**Worldserver crashes loading creature models after an upgrade.** See "Optional custom-DBC models" above.

## Development

All builds happen in docker; the scripts are in `tools/` and write logs and reports to `var/mpcheck/out/` in the
AzerothCore root. They use the image tag `mpcheck` and refuse `master`, and they only ever read (mysqldump)
from the real `ac-database`.

| Script | Purpose | Expected time |
|---|---|---|
| `tools/mp-build.sh [default]` | `docker compose build ac-worldserver` (tag `mpcheck`), module warnings compared with the recorded baseline. `--full` (= `--no-cache`) applies to `default` only. | minutes with a warm ccache; about 35 minutes with `--full` |
| `tools/mp-build.sh noproviders` | Flips `MP_NO_BOT_PROVIDERS` to ON in `mod-mythic-plus.cmake` for one build (tag `mpcheck-noprov`), restores the file, removes the image. On a fully cached layer (no source change since the last run) it prints a NOTE and passes without re-verifying. If a killed run left the cmake file modified, the script refuses to start: run `git checkout -- mod-mythic-plus.cmake`. | minutes (module TUs only) |
| `tools/mp-gate.sh` | `docker compose build ac-worldserver ac-db-import` under `mpcheck`. | minutes with a warm cache |
| `tools/mp-db.sh up\|down\|status` | Throwaway `ac-mpcheck-db` MySQL holding copies of your acore databases. | `up`: several minutes |
| `tools/mp-preflight.sh [--dbimport-only]` | db-import, then `worldserver --dry-run`, against `ac-mpcheck-db`. `MP_KEEP_DB=1` keeps the database. | minutes |
| `tools/mp-live.sh` | Runs auth and `worldserver:$DOCKER_IMAGE_TAG` against `ac-mpcheck-db` for hand testing (stops your normal stack, restarts it on exit). | interactive |
| `tools/mp-audit.sh <label>` | Static snapshot of registered scripts, loader calls and hooks, for before/after diffs. | seconds |

Every hook must carry `override` (module sources are built with `-Werror=suggest-override`), and a hook that
overrides nothing is a build error.

## Credits

**Development Team**: Araxia Online Development Team
- **Lead Developer**: ben-of-codecraft
- **Contributors**: james-huston

**Special Thanks**:
- AzerothCore development team for the excellent foundation
- Community members who provided testing and feedback
- Original World of Warcraft developers for creating the content we enhance

James Huston ported the module to the merged AzerothCore core (unit modifier API split, `UNIT_CLASS_*` removal,
`OnPlayer*` hooks, chat observer) and updated the SQL for the current schema.

## License

This project is licensed under the GPL-3.0 License - see the [LICENSE](LICENSE) file for details.
