--
DELETE FROM `command` WHERE `name` IN ('rate', 'rate set', 'rate reset');
INSERT INTO `command` (`name`, `security`, `help`) VALUES
('rate', 3, 'Syntax: .rate\r\n\r\nShows the current value of the runtime-adjustable server rates: experience (xp.kill, xp.quest, xp.explore), money drops (gold), item drop chances by quality (itemdrop.*) and gathering yield (gathering).'),
('rate set', 3, 'Syntax: .rate set $name $value\r\n\r\nSets a server rate multiplier at runtime, e.g. ".rate set xp.kill 2" or ".rate set gathering 3". The group names xp and itemdrop set every rate they contain. The change applies immediately to the whole realm but is not written to the config file; ".reload config" or a server restart restores the configured values.'),
('rate reset', 3, 'Syntax: .rate reset $name\r\n\r\nRestores a server rate (or a group: xp, itemdrop; or everything: all) to the value configured in worldserver.conf.');
