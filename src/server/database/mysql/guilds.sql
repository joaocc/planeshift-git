# MySQL-Front Dump 1.16 beta
#
# Host: localhost Database: planeshift
#--------------------------------------------------------
# Server version 3.23.52-max-nt
#
# Table structure for table 'guilds'
#

CREATE TABLE guilds (
  id int(11) NOT NULL auto_increment,
  name varchar(25) NOT NULL DEFAULT '' ,
  char_id_founder int(10) NOT NULL DEFAULT '0' ,
  web_page varchar(255) ,
  date_created datetime NOT NULL DEFAULT '0000-00-00 00:00:00' ,
  karma_points int(10) NOT NULL DEFAULT '0' ,
  secret_ind char(1) ,
  motd char(200) ,
  alliance int(11),
  bank_money_circles int(10) unsigned NOT NULL default '0',
  bank_money_octas int(10) unsigned NOT NULL default '0',
  bank_money_hexas int(10) unsigned NOT NULL default '0',
  bank_money_trias int(10) unsigned NOT NULL default '0',
  PRIMARY KEY (id),
  KEY id_2 (id),
  UNIQUE name (name),
  UNIQUE id (id)
);


#
# Dumping data for table 'guilds'
#

INSERT INTO guilds VALUES("1","Insomniac Developers","2","www.yahoo.com","2002-05-08 00:07:37","0","N","Guild MOTD", 1, 1, 1, 1, 1);
INSERT INTO guilds VALUES("11","Karmic Fanboys","1","www.linux.org","2003-07-25 00:07:37","0","N","", 1, 0, 0, 0, 0);
