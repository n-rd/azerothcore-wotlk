-- Rocketeer's Charge: heirloom ring (warrior) with the Goblin Rocket Helmet's
-- on-use rocket charge (spell 22641) on a 3-minute item cooldown. Ring
-- conventions and scaling copied from the Dread Pirate Ring (50255); icon from
-- Charged Gear (9461).
-- REPLACE keeps the update idempotent without deleting from item_template (custom id 100116).
REPLACE INTO `item_template` (`entry`, `class`, `subclass`, `name`, `displayid`, `Quality`, `Flags`, `BuyCount`, `InventoryType`, `AllowableClass`, `ItemLevel`, `RequiredLevel`, `stackable`, `ScalingStatDistribution`, `ScalingStatValue`, `armor`, `spellid_1`, `spelltrigger_1`, `spellcooldown_1`, `bonding`, `description`, `Material`) VALUES
(100116, 4, 0, 'Rocketeer''s Charge', 3258, 7, 134742016, 1, 11, 1, 1, 1, 1, 371, 262144, 0, 22641, 0, 180000, 1, 'A second Charge, with style.', 5);
