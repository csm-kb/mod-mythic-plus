-- Renumber the Mythic+ vendor spawns from 15_new_npc_vendors.sql below AzerothCore's creature spawn-id cap.
-- Spawn guids 19001025-19001029 exceed 0xFFFFFF (16777215): ObjectMgr::GenerateCreatureSpawnId then logs
-- "Creature spawn id overflow!! ... TCE00007" and shuts the worldserver down on the first runtime spawn.
-- 9101018-9101022 follow the module's own custom NPC spawn range (9101010-9101017).
UPDATE `creature` SET `guid` = 9101018 WHERE `guid` = 19001025 AND `id` = 9500800;
UPDATE `creature` SET `guid` = 9101019 WHERE `guid` = 19001026 AND `id` = 9500801;
UPDATE `creature` SET `guid` = 9101020 WHERE `guid` = 19001027 AND `id` = 9500802;
UPDATE `creature` SET `guid` = 9101021 WHERE `guid` = 19001028 AND `id` = 9500803;
UPDATE `creature` SET `guid` = 9101022 WHERE `guid` = 19001029 AND `id` = 9500804;
