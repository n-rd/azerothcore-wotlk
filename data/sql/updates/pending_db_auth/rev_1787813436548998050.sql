--
DELETE FROM `rbac_permissions` WHERE `id` IN (946, 947, 948);
INSERT INTO `rbac_permissions` (`id`, `name`) VALUES
(946, 'Command: rate'),
(947, 'Command: rate set'),
(948, 'Command: rate reset');

DELETE FROM `rbac_linked_permissions` WHERE `id` = 196 AND `linkedId` IN (946, 947, 948);
INSERT INTO `rbac_linked_permissions` (`id`, `linkedId`) VALUES
(196, 946),
(196, 947),
(196, 948);
