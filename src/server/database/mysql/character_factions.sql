
CREATE TABLE IF NOT EXISTS `character_factions` (
  `character_id` int(10) unsigned NOT NULL DEFAULT '0' COMMENT 'The PID of the character this faction is assigned to',
  `faction_id` int(10) unsigned NOT NULL DEFAULT '0' COMMENT 'The id of the faction the points are for',
  `value` int(10) NOT NULL DEFAULT '0' COMMENT 'The amount of points in this faction',
  PRIMARY KEY (`character_id`,`faction_id`)
) ENGINE=MyISAM DEFAULT CHARSET=latin1 ROW_FORMAT=FIXED COMMENT='Stores the faction of the characters';

INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(1, 1, -50);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(1, 2, 50);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(2, 1, -40);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(2, 2, 50);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(3, 1, -30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(3, 2, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(4, 1, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(4, 2, -30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(6, 1, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(6, 2, -30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(7, 1, 35);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(7, 2, -35);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(8, 1, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(8, 3, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(8, 4, -50);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(9, 1, -50);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(9, 2, 50);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(12, 1, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(12, 3, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(12, 4, -50);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(13, 1, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(13, 3, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(13, 4, -50);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(14, 1, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(14, 3, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(14, 4, -50);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(15, 1, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(15, 3, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(15, 4, -50);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(20, 1, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(20, 3, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(20, 4, -50);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(21, 1, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(21, 3, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(21, 4, -50);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(22, 1, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(22, 3, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(22, 4, -50);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(23, 1, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(23, 3, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(23, 4, -50);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(24, 1, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(24, 3, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(24, 4, -50);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(25, 1, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(25, 2, -30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(26, 1, 35);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(26, 2, -35);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(27, 1, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(27, 3, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(27, 4, -50);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(28, 1, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(28, 3, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(28, 4, -50);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(29, 1, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(29, 3, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(29, 4, -50);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(30, 1, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(30, 3, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(30, 4, -50);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(40, 1, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(40, 3, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(40, 4, -50);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(50, 1, -50);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(50, 2, 50);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(51, 1, -50);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(51, 2, 50);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(52, 1, -50);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(52, 2, 50);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(53, 1, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(53, 3, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(53, 4, -50);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(54, 1, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(54, 2, -30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(55, 1, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(55, 2, -30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(56, 1, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(56, 2, -30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(57, 1, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(57, 2, -30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(58, 1, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(58, 2, -30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(59, 1, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(59, 2, -30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(60, 1, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(60, 2, -30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(61, 1, 35);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(61, 2, -35);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(62, 1, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(62, 3, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(62, 4, -50);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(63, 1, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(63, 2, -30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(64, 1, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(64, 2, -30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(65, 1, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(65, 2, -30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(66, 1, 35);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(66, 2, -35);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(67, 1, 35);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(67, 2, -35);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(68, 1, 35);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(68, 2, -35);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(69, 1, 35);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(69, 2, -35);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(70, 1, 35);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(70, 2, -35);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(71, 1, 35);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(71, 2, -35);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(72, 1, 35);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(72, 2, -35);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(73, 1, 35);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(73, 2, -35);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(74, 1, 35);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(74, 2, -35);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(75, 1, 35);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(75, 2, -35);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(76, 1, 35);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(76, 2, -35);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(77, 1, 35);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(77, 2, -35);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(78, 1, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(78, 3, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(78, 4, -50);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(79, 1, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(79, 3, 30);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(79, 4, -50);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(80, 1, 35);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(80, 2, -35);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(81, 1, 35);
INSERT INTO `character_factions` (`character_id`, `faction_id`, `value`) VALUES(81, 2, -35);
