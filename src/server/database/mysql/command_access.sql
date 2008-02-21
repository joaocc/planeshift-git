
###############################################################################
#   Table for the various command groups people have. 
#       id           - The group ID
#       group_name   - The name of the group
###############################################################################

CREATE TABLE command_groups (
  id int(8) unsigned NOT NULL,
  group_name varchar(40) NOT NULL DEFAULT '' ,
  PRIMARY KEY (id)
);


INSERT INTO command_groups VALUES(0, "Player");

INSERT INTO command_groups VALUES(10, "Tester");

INSERT INTO command_groups VALUES(21, "Initiate");
INSERT INTO command_groups VALUES(22, "Intermediate");
INSERT INTO command_groups VALUES(23, "Advanced");
INSERT INTO command_groups VALUES(24, "Senior");
INSERT INTO command_groups VALUES(25, "Expert");
INSERT INTO command_groups VALUES(26, "Elder");

INSERT INTO command_groups VALUES(30, "Developers");



###############################################################################
#   Table for the command to group assignments
###############################################################################
CREATE TABLE command_group_assignment (
  command_name varchar(40) NOT NULL DEFAULT '' ,
  group_member int(8) NOT NULL,
  UNIQUE KEY `UNIQUE1` (`command_name`,`group_member`)
 );

# commands must be listed with the leading slash
# special properties are listed without a slash

# developers only
INSERT INTO command_group_assignment VALUES( "/action", 30 );
INSERT INTO command_group_assignment VALUES( "/badtext", 30 );
INSERT INTO command_group_assignment VALUES( "change NPC names", 30 );
INSERT INTO command_group_assignment VALUES( "/death", 30 );
INSERT INTO command_group_assignment VALUES( "/deletechar", 30 );
INSERT INTO command_group_assignment VALUES( "/location", 30 );
INSERT INTO command_group_assignment VALUES( "/killnpc", 30 );
INSERT INTO command_group_assignment VALUES( "/npc", 30 );
INSERT INTO command_group_assignment VALUES( "/path", 30 );
INSERT INTO command_group_assignment VALUES( "/reload", 30 );
INSERT INTO command_group_assignment VALUES( "/runscript", 30 );

# GM5 and above
INSERT INTO command_group_assignment VALUES( "/crystal", 30 );
INSERT INTO command_group_assignment VALUES( "/crystal", 26 );
INSERT INTO command_group_assignment VALUES( "/crystal", 25 );
INSERT INTO command_group_assignment VALUES( "/item", 30 );
INSERT INTO command_group_assignment VALUES( "/item", 26 );
INSERT INTO command_group_assignment VALUES( "/item", 25 );
INSERT INTO command_group_assignment VALUES( "/weather", 30 );
INSERT INTO command_group_assignment VALUES( "/weather", 26 );
INSERT INTO command_group_assignment VALUES( "/weather", 25 );
INSERT INTO command_group_assignment VALUES( "/rain", 30 );
INSERT INTO command_group_assignment VALUES( "/rain", 26 );
INSERT INTO command_group_assignment VALUES( "/rain", 25 );
INSERT INTO command_group_assignment VALUES( "/snow", 30 );
INSERT INTO command_group_assignment VALUES( "/snow", 26 );
INSERT INTO command_group_assignment VALUES( "/snow", 25 );
INSERT INTO command_group_assignment VALUES( "/thunder", 30 );
INSERT INTO command_group_assignment VALUES( "/thunder", 26 );
INSERT INTO command_group_assignment VALUES( "/thunder", 25 );
INSERT INTO command_group_assignment VALUES( "/fog", 30 );
INSERT INTO command_group_assignment VALUES( "/fog", 26 );
INSERT INTO command_group_assignment VALUES( "/fog", 25 );
INSERT INTO command_group_assignment VALUES( "/modify", 30 );
INSERT INTO command_group_assignment VALUES( "/modify", 26 );
INSERT INTO command_group_assignment VALUES( "/modify", 25 );
INSERT INTO command_group_assignment VALUES( "/key", 30 );
INSERT INTO command_group_assignment VALUES( "/key", 26 );
INSERT INTO command_group_assignment VALUES( "/key", 25 );
INSERT INTO command_group_assignment VALUES( "/takeitem", 30 );
INSERT INTO command_group_assignment VALUES( "/takeitem", 26 );
INSERT INTO command_group_assignment VALUES( "/takeitem", 25 );
INSERT INTO command_group_assignment VALUES( "move unpickupables/spawns", 30 );
INSERT INTO command_group_assignment VALUES( "move unpickupables/spawns", 26 );
INSERT INTO command_group_assignment VALUES( "move unpickupables/spawns", 25 );
INSERT INTO command_group_assignment VALUES( "command area", 30 );
INSERT INTO command_group_assignment VALUES( "command area", 26 );
INSERT INTO command_group_assignment VALUES( "command area", 25 );
INSERT INTO command_group_assignment VALUES( "setskill others", 30 );
INSERT INTO command_group_assignment VALUES( "setskill others", 26 );
INSERT INTO command_group_assignment VALUES( "setskill others", 25 );

# GM4 and above
INSERT INTO command_group_assignment VALUES( "/event", 30 );
INSERT INTO command_group_assignment VALUES( "/event", 26 );
INSERT INTO command_group_assignment VALUES( "/event", 25 );
INSERT INTO command_group_assignment VALUES( "/event", 24 );
INSERT INTO command_group_assignment VALUES( "/awardexp", 30 );
INSERT INTO command_group_assignment VALUES( "/awardexp", 26 );
INSERT INTO command_group_assignment VALUES( "/awardexp", 25 );
INSERT INTO command_group_assignment VALUES( "/awardexp", 24 );
INSERT INTO command_group_assignment VALUES( "/deputize", 30 );
INSERT INTO command_group_assignment VALUES( "/deputize", 26 );
INSERT INTO command_group_assignment VALUES( "/deputize", 25 );
INSERT INTO command_group_assignment VALUES( "/deputize", 24 );
INSERT INTO command_group_assignment VALUES( "/morph", 30 );
INSERT INTO command_group_assignment VALUES( "/morph", 26 );
INSERT INTO command_group_assignment VALUES( "/morph", 25 );
INSERT INTO command_group_assignment VALUES( "/morph", 24 );
INSERT INTO command_group_assignment VALUES( "/impersonate", 30 );
INSERT INTO command_group_assignment VALUES( "/impersonate", 26 );
INSERT INTO command_group_assignment VALUES( "/impersonate", 25 );
INSERT INTO command_group_assignment VALUES( "/impersonate", 24 );
INSERT INTO command_group_assignment VALUES( "quest others", 30 );
INSERT INTO command_group_assignment VALUES( "quest others", 26 );
INSERT INTO command_group_assignment VALUES( "quest others", 25 );
INSERT INTO command_group_assignment VALUES( "quest others", 24 );
INSERT INTO command_group_assignment VALUES( "/setitemname", 30 );
INSERT INTO command_group_assignment VALUES( "/setitemname", 26 );
INSERT INTO command_group_assignment VALUES( "/setitemname", 25 );
INSERT INTO command_group_assignment VALUES( "/setitemname", 24 );

# GM3 and above
INSERT INTO command_group_assignment VALUES( "/ban", 30 );
INSERT INTO command_group_assignment VALUES( "/ban", 26 );
INSERT INTO command_group_assignment VALUES( "/ban", 25 );
INSERT INTO command_group_assignment VALUES( "/ban", 24 );
INSERT INTO command_group_assignment VALUES( "/ban", 23 );
INSERT INTO command_group_assignment VALUES( "/unban", 30 );
INSERT INTO command_group_assignment VALUES( "/unban", 26 );
INSERT INTO command_group_assignment VALUES( "/unban", 25 );
INSERT INTO command_group_assignment VALUES( "/unban", 24 );
INSERT INTO command_group_assignment VALUES( "/unban", 23 );
INSERT INTO command_group_assignment VALUES( "/giveitem", 30 );
INSERT INTO command_group_assignment VALUES( "/giveitem", 26 );
INSERT INTO command_group_assignment VALUES( "/giveitem", 25 );
INSERT INTO command_group_assignment VALUES( "/giveitem", 24 );
INSERT INTO command_group_assignment VALUES( "/giveitem", 23 );
INSERT INTO command_group_assignment VALUES( "/divorce", 30 );
INSERT INTO command_group_assignment VALUES( "/divorce", 26 );
INSERT INTO command_group_assignment VALUES( "/divorce", 25 );
INSERT INTO command_group_assignment VALUES( "/divorce", 24 );
INSERT INTO command_group_assignment VALUES( "/divorce", 23 );
INSERT INTO command_group_assignment VALUES( "/marriageinfo", 30 );
INSERT INTO command_group_assignment VALUES( "/marriageinfo", 26 );
INSERT INTO command_group_assignment VALUES( "/marriageinfo", 25 );
INSERT INTO command_group_assignment VALUES( "/marriageinfo", 24 );
INSERT INTO command_group_assignment VALUES( "/marriageinfo", 23 );
INSERT INTO command_group_assignment VALUES( "cast all spells", 30 );
INSERT INTO command_group_assignment VALUES( "cast all spells", 26 );
INSERT INTO command_group_assignment VALUES( "cast all spells", 25 );
INSERT INTO command_group_assignment VALUES( "cast all spells", 24 );
INSERT INTO command_group_assignment VALUES( "cast all spells", 23 );

# GM2 and above
INSERT INTO command_group_assignment VALUES( "/updaterespawn", 30 );
INSERT INTO command_group_assignment VALUES( "/updaterespawn", 26 );
INSERT INTO command_group_assignment VALUES( "/updaterespawn", 25 );
INSERT INTO command_group_assignment VALUES( "/updaterespawn", 24 );
INSERT INTO command_group_assignment VALUES( "/updaterespawn", 23 );
INSERT INTO command_group_assignment VALUES( "/updaterespawn", 22 );
INSERT INTO command_group_assignment VALUES( "/setskill", 30 );
INSERT INTO command_group_assignment VALUES( "/setskill", 26 );
INSERT INTO command_group_assignment VALUES( "/setskill", 25 );
INSERT INTO command_group_assignment VALUES( "/setskill", 24 );
INSERT INTO command_group_assignment VALUES( "/setskill", 23 );
INSERT INTO command_group_assignment VALUES( "/setskill", 22 );
INSERT INTO command_group_assignment VALUES( "/changename", 30 );
INSERT INTO command_group_assignment VALUES( "/changename", 26 );
INSERT INTO command_group_assignment VALUES( "/changename", 25 );
INSERT INTO command_group_assignment VALUES( "/changename", 24 );
INSERT INTO command_group_assignment VALUES( "/changename", 23 );
INSERT INTO command_group_assignment VALUES( "/changename", 22 );
INSERT INTO command_group_assignment VALUES( "/changeguildname", 30 );
INSERT INTO command_group_assignment VALUES( "/changeguildname", 26 );
INSERT INTO command_group_assignment VALUES( "/changeguildname", 25 );
INSERT INTO command_group_assignment VALUES( "/changeguildname", 24 );
INSERT INTO command_group_assignment VALUES( "/changeguildname", 23 );
INSERT INTO command_group_assignment VALUES( "/changeguildname", 22 );
INSERT INTO command_group_assignment VALUES( "/banname", 30 );
INSERT INTO command_group_assignment VALUES( "/banname", 26 );
INSERT INTO command_group_assignment VALUES( "/banname", 25 );
INSERT INTO command_group_assignment VALUES( "/banname", 24 );
INSERT INTO command_group_assignment VALUES( "/banname", 23 );
INSERT INTO command_group_assignment VALUES( "/banname", 22 );
INSERT INTO command_group_assignment VALUES( "/unbanname", 30 );
INSERT INTO command_group_assignment VALUES( "/unbanname", 26 );
INSERT INTO command_group_assignment VALUES( "/unbanname", 25 );
INSERT INTO command_group_assignment VALUES( "/unbanname", 24 );
INSERT INTO command_group_assignment VALUES( "/unbanname", 23 );
INSERT INTO command_group_assignment VALUES( "/unbanname", 22 );
INSERT INTO command_group_assignment VALUES( "/freeze", 30 );
INSERT INTO command_group_assignment VALUES( "/freeze", 26 );
INSERT INTO command_group_assignment VALUES( "/freeze", 25 );
INSERT INTO command_group_assignment VALUES( "/freeze", 24 );
INSERT INTO command_group_assignment VALUES( "/freeze", 23 );
INSERT INTO command_group_assignment VALUES( "/freeze", 22 );
INSERT INTO command_group_assignment VALUES( "/thaw", 30 );
INSERT INTO command_group_assignment VALUES( "/thaw", 26 );
INSERT INTO command_group_assignment VALUES( "/thaw", 25 );
INSERT INTO command_group_assignment VALUES( "/thaw", 24 );
INSERT INTO command_group_assignment VALUES( "/thaw", 23 );
INSERT INTO command_group_assignment VALUES( "/thaw", 22 );
INSERT INTO command_group_assignment VALUES( "/kick", 30 );
INSERT INTO command_group_assignment VALUES( "/kick", 26 );
INSERT INTO command_group_assignment VALUES( "/kick", 25 );
INSERT INTO command_group_assignment VALUES( "/kick", 24 );
INSERT INTO command_group_assignment VALUES( "/kick", 23 );
INSERT INTO command_group_assignment VALUES( "/kick", 22 );
INSERT INTO command_group_assignment VALUES( "move others", 30 );
INSERT INTO command_group_assignment VALUES( "move others", 26 );
INSERT INTO command_group_assignment VALUES( "move others", 25 );
INSERT INTO command_group_assignment VALUES( "move others", 24 );
INSERT INTO command_group_assignment VALUES( "move others", 23 );
INSERT INTO command_group_assignment VALUES( "move others", 22 );
INSERT INTO command_group_assignment VALUES( "always login", 30 );
INSERT INTO command_group_assignment VALUES( "always login", 26 );
INSERT INTO command_group_assignment VALUES( "always login", 25 );
INSERT INTO command_group_assignment VALUES( "always login", 24 );
INSERT INTO command_group_assignment VALUES( "always login", 23 );
INSERT INTO command_group_assignment VALUES( "always login", 22 );
INSERT INTO command_group_assignment VALUES( "/setlabelcolor", 30 );
INSERT INTO command_group_assignment VALUES( "/setlabelcolor", 26 );
INSERT INTO command_group_assignment VALUES( "/setlabelcolor", 25 );
INSERT INTO command_group_assignment VALUES( "/setlabelcolor", 24 );
INSERT INTO command_group_assignment VALUES( "/setlabelcolor", 23 );
INSERT INTO command_group_assignment VALUES( "/setlabelcolor", 22 );
INSERT INTO command_group_assignment VALUES( "/inspect", 30 );
INSERT INTO command_group_assignment VALUES( "/inspect", 26 );
INSERT INTO command_group_assignment VALUES( "/inspect", 25 );
INSERT INTO command_group_assignment VALUES( "/inspect", 24 );
INSERT INTO command_group_assignment VALUES( "/inspect", 23 );
INSERT INTO command_group_assignment VALUES( "/inspect", 22 );
INSERT INTO command_group_assignment VALUES( "view stats", 30 );
INSERT INTO command_group_assignment VALUES( "view stats", 26 );
INSERT INTO command_group_assignment VALUES( "view stats", 25 );
INSERT INTO command_group_assignment VALUES( "view stats", 24 );
INSERT INTO command_group_assignment VALUES( "view stats", 23 );
INSERT INTO command_group_assignment VALUES( "view stats", 22 );
INSERT INTO command_group_assignment VALUES( "/setquality", 30 );
INSERT INTO command_group_assignment VALUES( "/setquality", 26 );
INSERT INTO command_group_assignment VALUES( "/setquality", 25 );
INSERT INTO command_group_assignment VALUES( "/setquality", 24 );
INSERT INTO command_group_assignment VALUES( "/setquality", 23 );
INSERT INTO command_group_assignment VALUES( "/setquality", 22 );
INSERT INTO command_group_assignment VALUES( "/settrait", 30 );
INSERT INTO command_group_assignment VALUES( "/settrait", 26 );
INSERT INTO command_group_assignment VALUES( "/settrait", 25 );
INSERT INTO command_group_assignment VALUES( "/settrait", 24 );
INSERT INTO command_group_assignment VALUES( "/settrait", 23 );
INSERT INTO command_group_assignment VALUES( "/settrait", 22 );

# GM1 and above
INSERT INTO command_group_assignment VALUES( "/teleport", 30 );
INSERT INTO command_group_assignment VALUES( "/teleport", 26 );
INSERT INTO command_group_assignment VALUES( "/teleport", 25 );
INSERT INTO command_group_assignment VALUES( "/teleport", 24 );
INSERT INTO command_group_assignment VALUES( "/teleport", 23 );
INSERT INTO command_group_assignment VALUES( "/teleport", 22 );
INSERT INTO command_group_assignment VALUES( "/teleport", 21 );
INSERT INTO command_group_assignment VALUES( "/slide", 30 );
INSERT INTO command_group_assignment VALUES( "/slide", 26 );
INSERT INTO command_group_assignment VALUES( "/slide", 25 );
INSERT INTO command_group_assignment VALUES( "/slide", 24 );
INSERT INTO command_group_assignment VALUES( "/slide", 23 );
INSERT INTO command_group_assignment VALUES( "/slide", 22 );
INSERT INTO command_group_assignment VALUES( "/slide", 21 );
INSERT INTO command_group_assignment VALUES( "/mute", 30 );
INSERT INTO command_group_assignment VALUES( "/mute", 26 );
INSERT INTO command_group_assignment VALUES( "/mute", 25 );
INSERT INTO command_group_assignment VALUES( "/mute", 24 );
INSERT INTO command_group_assignment VALUES( "/mute", 23 );
INSERT INTO command_group_assignment VALUES( "/mute", 22 );
INSERT INTO command_group_assignment VALUES( "/mute", 21 );
INSERT INTO command_group_assignment VALUES( "/charlist", 30 );
INSERT INTO command_group_assignment VALUES( "/charlist", 26 );
INSERT INTO command_group_assignment VALUES( "/charlist", 25 );
INSERT INTO command_group_assignment VALUES( "/charlist", 24 );
INSERT INTO command_group_assignment VALUES( "/charlist", 23 );
INSERT INTO command_group_assignment VALUES( "/charlist", 22 );
INSERT INTO command_group_assignment VALUES( "/charlist", 21 );
INSERT INTO command_group_assignment VALUES( "/unmute", 30 );
INSERT INTO command_group_assignment VALUES( "/unmute", 26 );
INSERT INTO command_group_assignment VALUES( "/unmute", 25 );
INSERT INTO command_group_assignment VALUES( "/unmute", 24 );
INSERT INTO command_group_assignment VALUES( "/unmute", 23 );
INSERT INTO command_group_assignment VALUES( "/unmute", 22 );
INSERT INTO command_group_assignment VALUES( "/unmute", 21 );
INSERT INTO command_group_assignment VALUES( "/show_gm", 30 );
INSERT INTO command_group_assignment VALUES( "/show_gm", 26 );
INSERT INTO command_group_assignment VALUES( "/show_gm", 25 );
INSERT INTO command_group_assignment VALUES( "/show_gm", 24 );
INSERT INTO command_group_assignment VALUES( "/show_gm", 23 );
INSERT INTO command_group_assignment VALUES( "/show_gm", 22 );
INSERT INTO command_group_assignment VALUES( "/show_gm", 21 );
INSERT INTO command_group_assignment VALUES( "/warn", 30 );
INSERT INTO command_group_assignment VALUES( "/warn", 26 );
INSERT INTO command_group_assignment VALUES( "/warn", 25 );
INSERT INTO command_group_assignment VALUES( "/warn", 24 );
INSERT INTO command_group_assignment VALUES( "/warn", 23 );
INSERT INTO command_group_assignment VALUES( "/warn", 22 );
INSERT INTO command_group_assignment VALUES( "/warn", 21 );
INSERT INTO command_group_assignment VALUES( "/info", 30 );
INSERT INTO command_group_assignment VALUES( "/info", 26 );
INSERT INTO command_group_assignment VALUES( "/info", 25 );
INSERT INTO command_group_assignment VALUES( "/info", 24 );
INSERT INTO command_group_assignment VALUES( "/info", 23 );
INSERT INTO command_group_assignment VALUES( "/info", 22 );
INSERT INTO command_group_assignment VALUES( "/info", 21 );
INSERT INTO command_group_assignment VALUES( "quest notify", 30 );
INSERT INTO command_group_assignment VALUES( "quest notify", 26 );
INSERT INTO command_group_assignment VALUES( "quest notify", 25 );
INSERT INTO command_group_assignment VALUES( "quest notify", 24 );
INSERT INTO command_group_assignment VALUES( "quest notify", 23 );
INSERT INTO command_group_assignment VALUES( "quest notify", 22 );
INSERT INTO command_group_assignment VALUES( "quest notify", 21 );
INSERT INTO command_group_assignment VALUES( "pos extras", 30 );
INSERT INTO command_group_assignment VALUES( "pos extras", 26 );
INSERT INTO command_group_assignment VALUES( "pos extras", 25 );
INSERT INTO command_group_assignment VALUES( "pos extras", 24 );
INSERT INTO command_group_assignment VALUES( "pos extras", 23 );
INSERT INTO command_group_assignment VALUES( "pos extras", 22 );
INSERT INTO command_group_assignment VALUES( "pos extras", 21 );
INSERT INTO command_group_assignment VALUES( "default invincible", 30 );
INSERT INTO command_group_assignment VALUES( "default invincible", 26 );
INSERT INTO command_group_assignment VALUES( "default invincible", 25 );
INSERT INTO command_group_assignment VALUES( "default invincible", 24 );
INSERT INTO command_group_assignment VALUES( "default invincible", 23 );
INSERT INTO command_group_assignment VALUES( "default invincible", 22 );
INSERT INTO command_group_assignment VALUES( "default invincible", 21 );
INSERT INTO command_group_assignment VALUES( "default invisible", 30 );
INSERT INTO command_group_assignment VALUES( "default invisible", 26 );
INSERT INTO command_group_assignment VALUES( "default invisible", 25 );
INSERT INTO command_group_assignment VALUES( "default invisible", 24 );
INSERT INTO command_group_assignment VALUES( "default invisible", 23 );
INSERT INTO command_group_assignment VALUES( "default invisible", 22 );
INSERT INTO command_group_assignment VALUES( "default invisible", 21 );
INSERT INTO command_group_assignment VALUES( "default infinite inventory", 30 );
INSERT INTO command_group_assignment VALUES( "default infinite inventory", 26 );
INSERT INTO command_group_assignment VALUES( "default infinite inventory", 25 );
INSERT INTO command_group_assignment VALUES( "default infinite inventory", 24 );
INSERT INTO command_group_assignment VALUES( "default infinite inventory", 23 );
INSERT INTO command_group_assignment VALUES( "default infinite inventory", 22 );
INSERT INTO command_group_assignment VALUES( "default infinite inventory", 21 );
INSERT INTO command_group_assignment VALUES( "/set", 30 );
INSERT INTO command_group_assignment VALUES( "/set", 26 );
INSERT INTO command_group_assignment VALUES( "/set", 25 );
INSERT INTO command_group_assignment VALUES( "/set", 24 );
INSERT INTO command_group_assignment VALUES( "/set", 23 );
INSERT INTO command_group_assignment VALUES( "/set", 22 );
INSERT INTO command_group_assignment VALUES( "/set", 21 );
INSERT INTO command_group_assignment VALUES( "/quest", 30 );
INSERT INTO command_group_assignment VALUES( "/quest", 26 );
INSERT INTO command_group_assignment VALUES( "/quest", 25 );
INSERT INTO command_group_assignment VALUES( "/quest", 24 );
INSERT INTO command_group_assignment VALUES( "/quest", 23 );
INSERT INTO command_group_assignment VALUES( "/quest", 22 );
INSERT INTO command_group_assignment VALUES( "/quest", 21 );
INSERT INTO command_group_assignment VALUES( "/listwarnings", 30 );
INSERT INTO command_group_assignment VALUES( "/listwarnings", 26 );
INSERT INTO command_group_assignment VALUES( "/listwarnings", 25 );
INSERT INTO command_group_assignment VALUES( "/listwarnings", 24 );
INSERT INTO command_group_assignment VALUES( "/listwarnings", 23 );
INSERT INTO command_group_assignment VALUES( "/listwarnings", 22 );
INSERT INTO command_group_assignment VALUES( "/listwarnings", 21 );

# testers group
INSERT INTO command_group_assignment VALUES( "/slide", 10 );
INSERT INTO command_group_assignment VALUES( "/teleport", 10 );
INSERT INTO command_group_assignment VALUES( "/info", 10 );
INSERT INTO command_group_assignment VALUES( "/set", 10 );
INSERT INTO command_group_assignment VALUES( "quest notify", 10 );
INSERT INTO command_group_assignment VALUES( "pos extras", 10 );

# everyone
INSERT INTO command_group_assignment VALUES( "/petition", 30 );
INSERT INTO command_group_assignment VALUES( "/petition", 26 );
INSERT INTO command_group_assignment VALUES( "/petition", 25 );
INSERT INTO command_group_assignment VALUES( "/petition", 24 );
INSERT INTO command_group_assignment VALUES( "/petition", 23 );
INSERT INTO command_group_assignment VALUES( "/petition", 22 );
INSERT INTO command_group_assignment VALUES( "/petition", 21 );
INSERT INTO command_group_assignment VALUES( "/petition", 10 );
INSERT INTO command_group_assignment VALUES( "/petition", 0 );
