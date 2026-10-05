-- OPTIONAL: requires Araxia's custom client patch / DBC (custom display IDs 9500561-9500568).
-- NOT applied by dbimport. Apply manually only if clients carry that DBC; on a stock 3.3.5a
-- CreatureDisplayInfo.dbc these rows make ObjectMgr::LoadCreatureModelInfo dereference null and crash.
-- Statements moved verbatim from db-world/base/11_custom_npcs.sql.

-- creature_template_model rows pointing at custom displays
DELETE FROM `creature_template_model` WHERE (`CreatureID` = 9500562);
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`) VALUES
(9500562, 0, 9500562, 1, 1, 12340);

DELETE FROM `creature_template_model` WHERE (`CreatureID` = 9500563);
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`) VALUES
(9500563, 0, 9500563, 1, 1, 12340);

DELETE FROM `creature_template_model` WHERE (`CreatureID` = 9500564);
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`) VALUES
(9500564, 0, 9500564, 1, 1, 0);

DELETE FROM `creature_template_model` WHERE (`CreatureID` = 9500565);
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`) VALUES
(9500565, 0, 9500565, 1.25, 1, 0);

DELETE FROM `creature_template_model` WHERE (`CreatureID` = 9500566);
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`) VALUES
(9500566, 0, 9500566, 1.25, 1, 0);

DELETE FROM `creature_template_model` WHERE (`CreatureID` = 9500567);
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`) VALUES
(9500567, 0, 9500567, 1.25, 1, 0);

DELETE FROM `creature_template_model` WHERE (`CreatureID` = 9500568);
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`) VALUES
(9500568, 0, 9500568, 1.25, 1, 0);

--  Model Info Updates
REPLACE INTO creature_model_info (DisplayID, BoundingRadius, CombatReach, Gender, DisplayID_Other_Gender)
VALUES (9500561, 0.35, 1.25, 0, 0);

REPLACE INTO creature_model_info (DisplayID, BoundingRadius, CombatReach, Gender, DisplayID_Other_Gender)
SELECT 9500562, BoundingRadius, CombatReach, Gender, DisplayID_Other_Gender
FROM creature_model_info
WHERE DisplayID = 9500561;

REPLACE INTO creature_model_info (DisplayID, BoundingRadius, CombatReach, Gender, DisplayID_Other_Gender)
SELECT 9500563, BoundingRadius, CombatReach, Gender, DisplayID_Other_Gender
FROM creature_model_info
WHERE DisplayID = 9500561;

REPLACE INTO creature_model_info (DisplayID, BoundingRadius, CombatReach, Gender, DisplayID_Other_Gender)
SELECT 9500564, BoundingRadius, CombatReach, Gender, DisplayID_Other_Gender
FROM creature_model_info
WHERE DisplayID = 9500561;

REPLACE INTO creature_model_info (DisplayID, BoundingRadius, CombatReach, Gender, DisplayID_Other_Gender)
VALUES (9500565, 0.35, 1.25, 0, 0);

REPLACE INTO creature_model_info (DisplayID, BoundingRadius, CombatReach, Gender, DisplayID_Other_Gender)
VALUES (9500566, 0.35, 1.25, 0, 0);

REPLACE INTO creature_model_info (DisplayID, BoundingRadius, CombatReach, Gender, DisplayID_Other_Gender)
VALUES (9500567, 0.35, 1.25, 1, 0);

REPLACE INTO acore_world.creature_model_info (DisplayID, BoundingRadius, CombatReach, Gender, DisplayID_Other_Gender)
VALUES (9500568, 0.35, 1.25, 1, 0);
