-- Heirloom set completion: head, legs and cloak pieces for the six WotLK heirloom sets,
-- mirroring the fuller retail heirloom catalogue. Scaling values are copied from each
-- set's existing chest (full-budget slots: head/legs) or shoulder (cloaks), so stats
-- track the set exactly. Visuals reuse the classic dungeon-set appearances.
-- REPLACE keeps the update idempotent without deleting from item_template (custom id range 100100-100115).
REPLACE INTO `item_template` (`entry`, `class`, `subclass`, `name`, `displayid`, `Quality`, `Flags`, `BuyCount`, `InventoryType`, `ItemLevel`, `RequiredLevel`, `stackable`, `ScalingStatDistribution`, `ScalingStatValue`, `armor`, `spellid_1`, `spelltrigger_1`, `bonding`, `Material`, `description`) VALUES
-- Heads (scaling copied from the set chest; +10% XP passive like retail heirloom helms)
(100100, 4, 1, 'Tattered Dreadmist Mask',        31263, 7, 134221824, 1,  1, 1, 1, 1, 336, 1048584, 19, 57353, 1, 1, 7, ''),
(100101, 4, 2, 'Stained Shadowcraft Cap',        28180, 7, 134221824, 1,  1, 1, 1, 1, 335, 2097160, 19, 57353, 1, 1, 8, ''),
(100102, 4, 2, 'Preened Ironfeather Bonnet',     31228, 7, 134221824, 1,  1, 1, 1, 1, 334, 2097160, 19, 57353, 1, 1, 8, ''),
(100103, 4, 3, 'Mystical Coif of Elements',      45174, 7, 134221824, 1,  1, 1, 1, 1, 332, 4194312, 19, 57353, 1, 1, 5, ''),
(100104, 4, 3, 'Prized Beastmaster''s Cap',      31410, 7, 134221824, 1,  1, 1, 1, 1, 331, 4194312, 19, 57353, 1, 1, 5, ''),
(100105, 4, 4, 'Polished Helm of Valor',         42241, 7, 134221824, 1,  1, 1, 1, 1, 333, 8388616, 19, 57353, 1, 1, 6, ''),
-- Legs (scaling copied from the set chest; +10% XP passive)
(100106, 4, 1, 'Tattered Dreadmist Leggings',    29797, 7, 134221824, 1,  7, 1, 1, 1, 336, 1048584, 19, 57353, 1, 1, 7, ''),
(100107, 4, 2, 'Stained Shadowcraft Pants',      28161, 7, 134221824, 1,  7, 1, 1, 1, 335, 2097160, 19, 57353, 1, 1, 8, ''),
(100108, 4, 2, 'Preened Ironfeather Britches',   29975, 7, 134221824, 1,  7, 1, 1, 1, 334, 2097160, 19, 57353, 1, 1, 8, ''),
(100109, 4, 3, 'Mystical Kilt of Elements',      31415, 7, 134221824, 1,  7, 1, 1, 1, 332, 4194312, 19, 57353, 1, 1, 5, ''),
(100110, 4, 3, 'Prized Beastmaster''s Pants',    31403, 7, 134221824, 1,  7, 1, 1, 1, 331, 4194312, 19, 57353, 1, 1, 5, ''),
(100111, 4, 4, 'Polished Legplates of Valor',    29963, 7, 134221824, 1,  7, 1, 1, 1, 333, 8388616, 19, 57353, 1, 1, 6, ''),
-- Cloaks (shoulder-budget scaling; no XP passive, matching retail heirloom cloaks)
(100112, 4, 1, 'Inherited Cape of the Black Baron', 24013, 7, 134221824, 1, 16, 1, 1, 1,  10, 33, 12, 0, 0, 1, 7, ''),
(100113, 4, 1, 'Ancient Bloodmoon Cloak',           29827, 7, 134221824, 1, 16, 1, 1, 1,   7, 33, 12, 0, 0, 1, 7, ''),
(100114, 4, 1, 'Ancient Spellweave Cloak',          24073, 7, 134221824, 1, 16, 1, 1, 1,  16, 33, 12, 0, 0, 1, 7, ''),
(100115, 4, 1, 'Worn Stoneskin Gargoyle Cape',      24108, 7, 134221824, 1, 16, 1, 1, 1,   7, 33, 12, 0, 0, 1, 7, '');
