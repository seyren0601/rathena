# Subsystem Catalogue

> Status: **Discovery pass**. This is an inventory, not deep documentation. Each row names the files and the anchor symbols found in source; "Depth" shows how far it has been investigated (see [roadmap.md](roadmap.md)).
>
> Depth legend: **D0** = file located · **D1** = entry points/structures identified · **D2** = execution flow traced · **D3** = fully documented with extension points.

## A. Core / infrastructure (`src/common`)

| # | Subsystem | Files | Anchor symbols | Depth |
|---|---|---|---|---|
| A1 | Process lifecycle | `core.cpp` | `Core::start`, `e_core_status`, `main_core<T>` | D3 |
| A2 | Timer scheduler | `timer.cpp` | `add_timer`, `add_timer_interval`, `do_timer`, `TIMER_FUNC` | D3 |
| A3 | Sockets / sessions | `socket.cpp` | `session[]`, `socket_data`, `do_sockets`, `RFIFO*/WFIFO*` | D3 |
| A4 | Typed packets + generic dispatcher | `packets.hpp` | `PacketDatabase<T>` | D3 |
| A5 | SQL access | `sql.cpp` | `Sql_Malloc`, `Sql_Query`, `SqlStmt` | D2 |
| A6 | YAML DB framework | `database.cpp` | `YamlDatabase`, `TypesafeYamlDatabase`, `TypesafeCachedYamlDatabase`, `parseBodyNode`, `loadingFinished` | D3 |
| A7 | Legacy containers | `db.cpp` | `DBMap`, `idb_alloc`, `strdb_alloc`… | D2 |
| A8 | Memory | `malloc.cpp`, `ers.cpp` | `aMalloc`/`CREATE`, `ers_new` | D3 |
| A9 | Logging / console | `showmsg.cpp`, `cli.cpp` | `Show*`, `cli_get_options`, `parse_console` | D0 |
| A10 | Map index / GRF / map cache | `mapindex.cpp`, `grfio.cpp`, `map.cpp: map_readallmaps` | — | D0 |
| A11 | Messages / i18n | `msg_conf.cpp`, `map.cpp: map_msg_*` | `msg_txt(sd, n)` | D0 |

## B. Login-server (`src/login`)

| # | Subsystem | Anchor | Depth |
|---|---|---|---|
| B1 | Client auth (raw/MD5/SSO/OTP) | `logclif_parse_reqauth_raw/md5/sso`, `logclif_parse_otp_login` (`loginclif.cpp:266-475`) | D2 |
| B2 | Account engine | `account_db_sql` (`account.cpp`) — vtable-style C struct (`accounts->init(...)`) | D0 |
| B3 | Char-server registry & auth nodes | `logclif_parse_reqcharconnec`, `logchrif_parse_reqauth` | D1 |
| B4 | IP ban / login log | `ipban.cpp`, `loginlog.cpp` | D0 |

## C. Char-server (`src/char`)

| # | Subsystem | Anchor | Depth |
|---|---|---|---|
| C1 | Character select/create/delete/rename/pincode | `CharPacketDatabase`, `chclif_parse_*` | D1 |
| C2 | Character persistence | `char_mmo_char_tosql`, `char_mmo_char_fromsql`, `char_memitemdata_to_sql` | D2 |
| C3 | Map-server registry & map routing | `chmapif_parse`, `chclif_send_map_data` | D1 |
| C4 | Inter relay | `inter_parse_frommap` + `int_party`, `int_guild`, `int_storage`, `int_mail`, `int_auction`, `int_pet`, `int_homun`, `int_mercenary`, `int_elemental`, `int_quest`, `int_achievement`, `int_clan` | D1 |
| C5 | Account/char registry variables (`#`, `##` vars) | `mapif_parse_Registry` (`inter.cpp`) | D0 |
| C6 | Fame lists, online tracking, timers | `char_read_fame_list`, `char_online_data_cleanup` | D0 |

## D. Map-server: world and engine (`src/map`)

| # | Subsystem | Files | Anchor symbols | Depth |
|---|---|---|---|---|
| D1 | Map data & block grid | `map.cpp/.hpp` | `map_data` (`map.hpp:819`), `block_list`, `map_addblock`, `map_foreachin*`, `id_db`, mapflags (`map_data::flags`) | D3 |
| D2 | Unit state machine (move/attack/cast) | `unit.cpp` | `unit_data` (`unit.hpp:22`), `unit_walktoxy`, `unit_skilluse_id2` (`:2077`), `unit_attack_timer` | D3 |
| D3 | Pathfinding / navigation | `path.cpp`, `navi.cpp` | `path_search` (`path.cpp:269`), `navi_create_lists` | D3 |
| D4 | Client protocol | `clif.cpp`, `clif_packetdb.hpp`, `packets*.hpp` | `clif_parse`, `packet_db[]`, `clif_send` | D3 (protocol layer; individual handlers per gameplay doc) |
| D5 | Char-server link | `chrif.cpp` | `chrif_parse`, `chrif_authreq`, `chrif_save` (`:277`) | D1 |
| D6 | Inter relay link | `intif.cpp` | `intif_parse`, `intif_saveregistry` | D1 |
| D7 | SQL logging | `log.cpp` | `log_config_read`, `log_pick_pc` (`log.cpp:261`), `log_zeny` (`:277`) | D0 |
| D8 | GM commands | `atcommand.cpp`, `pc_groups.cpp`, `conf/groups.yml`, `conf/atcommands.yml` | `ACMD_DEF` (318 commands), `AtcommandAliasDatabase`, `PlayerGroupDatabase` | D0 |

## E. Map-server: gameplay

| # | Subsystem | Files | Anchor symbols | Data/config | Depth |
|---|---|---|---|---|---|
| E1 | Player character | `pc.cpp` | `map_session_data` (`pc.hpp:381`), `pc_authok`, `pc_setpos` (`:6930`), `pc_makesavestatus`, `pc_autosave` | `job_stats.yml`, `job_exp.yml`, `job_basepoints.yml`, `statpoint.yml`, `skill_tree.yml`, `battle/player.conf` | D3 |
| E2 | Jobs & classes | `pc.cpp` (`JobDatabase` `pc.hpp:1105`), `SkillTreeDatabase` | — | `job_*.yml`, `doc/ea_job_system.txt` | D3 |
| E3 | Status / stats calc | `status.cpp` | `status_data` (`status.hpp:3372`), `status_calc_pc_` (`:4997`), `status_calc_bl_` (`:6525`), `status_damage` (`:1492`) | `battle/status.conf`, `size_fix.yml`, `attr_fix.yml` | D3 |
| E4 | Status changes (buffs/debuffs) | `status.cpp` | `status_change` (`status.hpp:3479`), `status_change_start` (`:10152`), `status_change_end` (`:13434`), `status_change_timer`, `StatusDatabase` | `db/status.yml`, `status_disabled.txt`, `doc/status_change.txt` | D3 |
| E5 | Skills | `skill.cpp`, `skills/**` | `SkillDatabase`, `s_skill_db::impl`, `skill_castend_id`, `skill_castend_damage_id` (`:4179`), `skill_castend_nodamage_id` (`:4366`), `skill_castend_pos2` (`:5404`), `skill_attack` (`:2701`), `SkillImpl` | `skill_db.yml`, `skill_tree.yml`, `battle/skill.conf`, `skill_damage_db.txt`, `skill_nocast_db.txt`, `produce_db.txt`, `create_arrow_db.yml`, `abra_db.yml`, `spellbook_db.yml`, `magicmushroom_db.yml` | D3 |
| E6 | Combat / damage | `battle.cpp` | `battle_calc_attack` (`:6753`), `battle_calc_weapon_attack` (`:5387`), `battle_calc_magic_attack` (`:5801`), `battle_weapon_attack` (`:7133`), `battle_config` | `conf/battle/*.conf`, `attr_fix.yml`, `size_fix.yml` | D3 |
| E7 | Items DB | `itemdb.cpp` | `item_data` (`itemdb.hpp:3460`), `ItemDatabase`, `ItemGroupDatabase`, `ComboDatabase`, `RandomOption*`, `ItemPackageDatabase` | `item_db*.yml`, `item_group_db.yml`, `item_combos.yml`, `item_randomopt_*.yml`, `item_packages.yml` | D3 |
| E8 | Inventory & equipment | `pc.cpp` | `pc_additem` (`:5991`), `pc_equipitem` (`:12015`), `pc_delitem`, `item` struct | `doc/item_bonus.txt`, `battle/items.conf` | D3 |
| E9 | Item bonuses / item scripts | `pc.cpp: pc_bonus*`, `script.cpp` | `pc_bonus` (`pc.cpp:3725`), `pc_bonus2` (`:4498`), … | item `Script:` fields | D3 |
| E10 | Refine / enchant / reform / Laphine | `status.cpp` (`RefineDatabase` `status.hpp:121`, `EnchantgradeDatabase`), `itemdb.cpp` (`ItemReformDatabase`, `ItemEnchantDatabase`, `Laphine*Database`) | — | `refine.yml` (**modified in this fork**), `enchantgrade.yml`, `item_enchant.yml`, `item_reform.yml`, `laphine_*.yml` | D2 |
| E11 | Monsters & AI | `mob.cpp` | `mob_data` (`mob.hpp:337`), `MobDatabase`, `mob_spawn` (`:1117`), `mob_ai_sub_hard` (`:1841`), `mob_ai_hard`, `mob_dead` (`:2934`), `mob_once_spawn` (`:679`) | `mob_db.yml`, `mob_skill_db.txt`, `mob_summon.yml`, `mob_chat_db.yml`, `mob_item_ratio.yml`, `map_drops.yml`, `battle/monster.conf`, `battle/drops.conf` | D3 |
| E12 | NPCs | `npc.cpp`, `npc_chat.cpp` | `npc_data`, `npc_parsesrcfile` (`:5646`), `npc_parse_script/warp/shop/mob/mapflag/function`, `npc_click` (`:2212`), `npc_event` (`:1794`), `npc_event_do_oninit` (`:1420`), `npc_reload` (`:6058`) | `npc/**`, `npc/barters.yml` (`npc.cpp:395`), `db/<mode>/stylist.yml` (`npc.cpp:127`) | D3 |
| E13 | Script engine | `script.cpp`, `script_constants.hpp`, `mapreg.cpp` | `parse_script_` (`:2475`), `script_code` (`script.hpp:295`), `script_state` (`:316`), `run_script` (`:4186`), `run_script_main` (`:4376`), `buildin_func[]` (`:27928`, 676 `BUILDIN_DEF`s), `ConstantDatabase` | `db/const.yml`, `conf/script_athena.conf`, `doc/script_commands.txt` | D3 |
| E14 | Party | `party.cpp`, char `int_party.cpp` | `party_create` (`:144`) | `battle/party.conf` | D0 |
| E15 | Guild & WoE | `guild.cpp`, char `int_guild.cpp` | `guild_create` (`:689`), `CastleDatabase`, `GuildSkillTreeDatabase` | `castle_db.yml`, `guild_skill_tree.yml`, `exp_guild.yml`, `battle/guild.conf`, `npc/guild*/` | D0 |
| E16 | Clan | `clan.cpp`, char `int_clan.cpp` | — | — | D0 |
| E17 | Chat rooms / channels / duel | `chat.cpp`, `channel.cpp`, `duel.cpp` | — | `conf/channels.conf` | D0 |
| E18 | Quests | `quest.cpp`, char `int_quest.cpp` | `QuestDatabase`, `quest_add` (`:590`) | `quest_db.yml` | D0 |
| E19 | Achievements | `achievement.cpp`, char `int_achievement.cpp` | `AchievementDatabase`, `achievement_update_objective` (`:1090`) | `achievement_db.yml`, `achievement_level_db.yml` | D0 |
| E20 | Trade | `trade.cpp` | `trade_traderequest` (`:31`) | — | D0 |
| E21 | Storage (personal/guild/premium) | `storage.cpp`, char `int_storage.cpp` | `storage_storageopen` (`:133`), `InterServerDatabase` | `conf/inter_server.yml` | D0 |
| E22 | Vending / buying store / search store | `vending.cpp`, `buyingstore.cpp`, `searchstore.cpp` | — | `battle/feature.conf` ⚠️ | D0 |
| E23 | Cash shop / item mall | `cashshop.cpp`, `npc.cpp` cashshop NPC subtype | `CashShopDatabase` | `item_cash.yml` (**modified in fork**) | D0 |
| E24 | Mail / auction / RoDEX | `mail.cpp`, char `int_mail.cpp`, `int_auction.cpp` | — | — | D0 |
| E25 | Pets | `pet.cpp`, char `int_pet.cpp` | `PetDatabase` | `pet_db.yml`, `battle/pet.conf` | D0 |
| E26 | Homunculus | `homunculus.cpp`, char `int_homun.cpp` | `HomunculusDatabase`, `hom_call` (`:1114`) | `homunculus_db.yml`, `exp_homun.yml`, `battle/homunc.conf` | D0 |
| E27 | Mercenary | `mercenary.cpp`, char `int_mercenary.cpp` | `MercenaryDatabase` | `mercenary_db.yml` | D0 |
| E28 | Elemental | `elemental.cpp`, char `int_elemental.cpp` | `ElementalDatabase` | `elemental_db.yml` | D0 |
| E29 | Instances | `instance.cpp` | `InstanceDatabase`, `instance_create` (`:608`) | `instance_db.yml`, `battle/instance.conf`, `npc/instances/` | D0 |
| E30 | Battlegrounds | `battleground.cpp` | `BattlegroundDatabase`, `bg_team_join` (`:523`) | `battleground_db.yml`, `battle/battleground.conf`, `npc/battleground/` | D0 |
| E31 | Experience, penalties, rates | `pc.cpp`, `mob.cpp` | `PenaltyDatabase` | `level_penalty.yml` (**emptied in fork**), `battle/exp.conf`, `battle/drops.conf` | D2 |
| E32 | Attendance / reputation / captcha / stylist / barter | `pc.cpp`, `npc.cpp` | `AttendanceDatabase`, `ReputationDatabase`, `CaptchaDatabase`, `StylistDatabase`, `BarterDatabase` | `attendance.yml`, `reputation*.yml`, `captcha_db.yml`, `npc/barters.yml` | D0 |
| E33 | Global map variables (`$var`) | `mapreg.cpp` | `mapreg` table, `mapreg_dirty` flag | `sql: mapreg` | D2 |
| E34 | Gameplay events (OnInit, OnPC*, OnClock, WoE timers) | `npc.cpp`, `script.cpp` | `npc_event_doall` (`npc.cpp:1322`), `npc_event_do_clock` timer (`npc.cpp:1368`) | `doc/woe_time_explanation.txt` | D2 |
| E35 | Navigation generator | `navi.cpp`, `MAP_GENERATOR` | `navi_create_lists` | `tools/navi.py` | D0 |

## F. Web-server (`src/web`)

| # | Subsystem | Anchor | Depth |
|---|---|---|---|
| F1 | HTTP routing / threading | `WebServer::initialize`, httplib `Post(...)` routes | D1 |
| F2 | Auth | `auth.cpp` | D0 |
| F3 | Controllers | `charconfig`, `userconfig`, `emblem`, `merchantstore`, `partybooking` | D0 |

## YAML database classes

✅ Every `TypesafeYamlDatabase` / `TypesafeCachedYamlDatabase` / `YamlDatabase` subclass found by grep:

| Server | Class (file:line) |
|---|---|
| char | `InterServerDatabase` (`inter.hpp:17`), `GuildExpDatabase` (`int_guild.hpp:37`) |
| map / items | `ItemDatabase`★ (`itemdb.hpp:3554`), `ItemGroupDatabase`★ (`:3596`), `ComboDatabase`, `RandomOptionDatabase`, `RandomOptionGroupDatabase`, `LaphineSynthesisDatabase`, `LaphineUpgradeDatabase`, `ItemReformDatabase`, `ItemEnchantDatabase`, `ItemPackageDatabase`, `CashShopDatabase` |
| map / player | `JobDatabase`★ (`pc.hpp:1105`), `PlayerStatPointDatabase`★, `SkillTreeDatabase`, `PenaltyDatabase`, `CaptchaDatabase`, `AttendanceDatabase`, `ReputationDatabase`, `ReputationGroupDatabase`, `PlayerGroupDatabase` (`pc_groups.hpp:111`) |
| map / skills & status | `SkillDatabase`★ (`skill.hpp:325`), `SkillArrowDatabase`, `AbraDatabase`, `ReadingSpellbookDatabase`, `MagicMushroomDatabase`, `StatusDatabase`★ (`status.hpp:3297`), `RefineDatabase`, `SizeFixDatabase`, `AttributeDatabase`, `EnchantgradeDatabase` |
| map / monsters | `MobDatabase`★ (`mob.hpp:287`), `MobChatDatabase`, `MobItemRatioDatabase`, `MapDropDatabase`, `MobAvailDatabase`, `MobSummonDatabase` |
| map / other | `QuestDatabase`, `AchievementDatabase`, `AchievementLevelDatabase`, `InstanceDatabase`, `BattlegroundDatabase`, `CastleDatabase`, `GuildSkillTreeDatabase`, `PetDatabase`, `HomunculusDatabase`, `HomExpDatabase`, `MercenaryDatabase`, `ElementalDatabase`, `StylistDatabase`, `BarterDatabase`, `AtcommandAliasDatabase`, `ConstantDatabase` (`script.hpp:2282`) |

★ = cached variant (adds an index-based fast lookup cache; ⚠️ mechanism in `database.hpp:150-200` not yet analysed).

## Additional systems discovered beyond the requested list

Clans (E16), chat channels (E17), RoDEX mail & auctions (E24), Laphine synthesis/upgrade and item reform (E10), random options (E7), attendance, reputation, captcha (E32), stylist (E32), barter/market shops (E32), vending/buying/search store (E22), mapreg (E33), navigation generation (E35), the HTTP web-server (F), and the **per-skill class framework** in `src/map/skills/` (E5).
