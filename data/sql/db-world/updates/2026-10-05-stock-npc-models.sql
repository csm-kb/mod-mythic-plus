-- Stock 3.3.5a display models for the custom Mythic+ NPCs whose original models need Araxia's client patch
-- (see data/sql/optional/araxia-custom-dbc/). Each display id is borrowed from an existing stock creature.
-- INSERT IGNORE keeps any model a server already set, including the optional Araxia rows.
INSERT IGNORE INTO `creature_template_model`
    (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`, `VerifiedBuild`) VALUES
(9500562, 0, 3097, 1, 1, 0),  -- Thorin Firehand: Bengus Deepforge
(9500563, 0, 26378, 1, 1, 0), -- Elowyn Threadbinder: Lalla Brightweave
(9500564, 0, 21462, 1, 1, 0), -- Shivey: Krixel Pinchwhistle
(9500565, 0, 26838, 1, 1, 0), -- Steve: Argent Champion
(9500566, 0, 25998, 1, 1, 0), -- Vaeric Bloodbane: Knight of the Ebon Blade
(9500567, 0, 25947, 1, 1, 0), -- Agatha Veil: Archmage Celindra
(9500568, 0, 25771, 1, 1, 0); -- Sylvia Steelheart: Valkyrion Aspirant
