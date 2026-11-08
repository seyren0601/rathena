-- Job Record System (custom): per-character job records
-- See docs/subsystems/map/job-record-system-design.md

--
-- Table structure for table `char_job_record`
--

CREATE TABLE IF NOT EXISTS `char_job_record` (
  `char_id` int(11) unsigned NOT NULL default '0',
  `record_id` int(11) unsigned NOT NULL default '0',
  `name` varchar(24) NOT NULL default '',
  `class` smallint(6) unsigned NOT NULL default '0',
  `base_level` smallint(6) unsigned NOT NULL default '1',
  `base_exp` bigint(20) unsigned NOT NULL default '0',
  `job_level` smallint(6) unsigned NOT NULL default '1',
  `job_exp` bigint(20) unsigned NOT NULL default '0',
  `status_point` int(11) unsigned NOT NULL default '0',
  `skill_point` int(11) unsigned NOT NULL default '0',
  `trait_point` int(11) unsigned NOT NULL default '0',
  `str` smallint(4) unsigned NOT NULL default '1',
  `agi` smallint(4) unsigned NOT NULL default '1',
  `vit` smallint(4) unsigned NOT NULL default '1',
  `int` smallint(4) unsigned NOT NULL default '1',
  `dex` smallint(4) unsigned NOT NULL default '1',
  `luk` smallint(4) unsigned NOT NULL default '1',
  `pow` smallint(4) unsigned NOT NULL default '0',
  `sta` smallint(4) unsigned NOT NULL default '0',
  `wis` smallint(4) unsigned NOT NULL default '0',
  `spl` smallint(4) unsigned NOT NULL default '0',
  `con` smallint(4) unsigned NOT NULL default '0',
  `crt` smallint(4) unsigned NOT NULL default '0',
  `chg_2nd` tinyint(3) unsigned NOT NULL default '0',
  `chg_3rd` tinyint(3) unsigned NOT NULL default '0',
  `chg_4th` tinyint(3) unsigned NOT NULL default '0',
  `hair` tinyint(4) unsigned NOT NULL default '0',
  `hair_color` smallint(5) unsigned NOT NULL default '0',
  `clothes_color` smallint(5) unsigned NOT NULL default '0',
  `equip_weight` int(11) unsigned NOT NULL default '0',
  `equip_count` smallint(5) unsigned NOT NULL default '0',
  `created` int(11) unsigned NOT NULL default '0',
  `last_used` int(11) unsigned NOT NULL default '0',
  PRIMARY KEY (`char_id`,`record_id`)
) ENGINE=InnoDB;

--
-- Table structure for table `char_job_record_hotkey`
--

CREATE TABLE IF NOT EXISTS `char_job_record_hotkey` (
  `char_id` int(11) unsigned NOT NULL default '0',
  `record_id` int(11) unsigned NOT NULL default '0',
  `hotkey` tinyint(2) unsigned NOT NULL,
  `type` tinyint(1) unsigned NOT NULL default '0',
  `itemskill_id` int(11) unsigned NOT NULL default '0',
  `skill_lvl` tinyint(4) unsigned NOT NULL default '0',
  PRIMARY KEY (`char_id`,`record_id`,`hotkey`)
) ENGINE=InnoDB;

--
-- Table structure for table `char_job_record_item`
--

CREATE TABLE IF NOT EXISTS `char_job_record_item` (
  `id` int(11) unsigned NOT NULL auto_increment,
  `char_id` int(11) unsigned NOT NULL default '0',
  `record_id` int(11) unsigned NOT NULL default '0',
  `nameid` int(10) unsigned NOT NULL default '0',
  `amount` int(11) unsigned NOT NULL default '0',
  `equip` int(11) unsigned NOT NULL default '0',
  `identify` smallint(6) NOT NULL default '0',
  `refine` tinyint(3) unsigned NOT NULL default '0',
  `attribute` tinyint(4) unsigned NOT NULL default '0',
  `card0` int(10) unsigned NOT NULL default '0',
  `card1` int(10) unsigned NOT NULL default '0',
  `card2` int(10) unsigned NOT NULL default '0',
  `card3` int(10) unsigned NOT NULL default '0',
  `option_id0` smallint(5) NOT NULL default '0',
  `option_val0` smallint(5) NOT NULL default '0',
  `option_parm0` tinyint(3) NOT NULL default '0',
  `option_id1` smallint(5) NOT NULL default '0',
  `option_val1` smallint(5) NOT NULL default '0',
  `option_parm1` tinyint(3) NOT NULL default '0',
  `option_id2` smallint(5) NOT NULL default '0',
  `option_val2` smallint(5) NOT NULL default '0',
  `option_parm2` tinyint(3) NOT NULL default '0',
  `option_id3` smallint(5) NOT NULL default '0',
  `option_val3` smallint(5) NOT NULL default '0',
  `option_parm3` tinyint(3) NOT NULL default '0',
  `option_id4` smallint(5) NOT NULL default '0',
  `option_val4` smallint(5) NOT NULL default '0',
  `option_parm4` tinyint(3) NOT NULL default '0',
  `expire_time` int(11) unsigned NOT NULL default '0',
  `favorite` tinyint(3) unsigned NOT NULL default '0',
  `bound` tinyint(3) unsigned NOT NULL default '0',
  `unique_id` bigint(20) unsigned NOT NULL default '0',
  `enchantgrade` tinyint unsigned NOT NULL default '0',
  PRIMARY KEY (`id`),
  KEY `char_record` (`char_id`,`record_id`),
  KEY `unique_id` (`unique_id`)
) ENGINE=InnoDB;

--
-- Table structure for table `char_job_record_quest`
--

CREATE TABLE IF NOT EXISTS `char_job_record_quest` (
  `char_id` int(11) unsigned NOT NULL default '0',
  `record_id` int(11) unsigned NOT NULL default '0',
  `quest_id` int(10) unsigned NOT NULL,
  `state` enum('0','1','2') NOT NULL default '0',
  `time` int(11) unsigned NOT NULL default '0',
  `count1` mediumint(8) unsigned NOT NULL default '0',
  `count2` mediumint(8) unsigned NOT NULL default '0',
  `count3` mediumint(8) unsigned NOT NULL default '0',
  PRIMARY KEY (`char_id`,`record_id`,`quest_id`)
) ENGINE=InnoDB;

--
-- Table structure for table `char_job_record_reg`
--

CREATE TABLE IF NOT EXISTS `char_job_record_reg` (
  `char_id` int(11) unsigned NOT NULL default '0',
  `record_id` int(11) unsigned NOT NULL default '0',
  `key` varchar(32) binary NOT NULL default '',
  `index` int(11) unsigned NOT NULL default '0',
  `is_str` tinyint(1) unsigned NOT NULL default '0',
  `value_num` bigint(20) NOT NULL default '0',
  `value_str` varchar(254) NOT NULL default '',
  PRIMARY KEY (`char_id`,`record_id`,`key`,`index`)
) ENGINE=InnoDB;

--
-- Table structure for table `char_job_record_skill`
--

CREATE TABLE IF NOT EXISTS `char_job_record_skill` (
  `char_id` int(11) unsigned NOT NULL default '0',
  `record_id` int(11) unsigned NOT NULL default '0',
  `id` smallint(11) unsigned NOT NULL default '0',
  `lv` tinyint(4) unsigned NOT NULL default '0',
  `flag` tinyint(1) unsigned NOT NULL default '0',
  PRIMARY KEY (`char_id`,`record_id`,`id`)
) ENGINE=InnoDB;

--
-- Table structure for table `char_job_record_state`
--

CREATE TABLE IF NOT EXISTS `char_job_record_state` (
  `char_id` int(11) unsigned NOT NULL default '0',
  `active_record` int(11) unsigned NOT NULL default '0',
  `pending_to` int(11) unsigned NOT NULL default '0',
  `next_record_id` int(11) unsigned NOT NULL default '1',
  PRIMARY KEY (`char_id`)
) ENGINE=InnoDB;
