# Character System (players, jobs, stats, experience)

> Status: **Phase 4**. ✅ = verified in source · ⚠️ = inferred · ❓ = unknown.
> Main file: `src/map/pc.cpp` (16.2k lines), `src/map/pc.hpp`; persistent model in `src/common/mmo.hpp`. Login, load and save flows are covered in [../networking.md §6](../networking.md#6-full-client-connection-lifecycle) and [../database-architecture.md §6](../database-architecture.md#6-character-persistence).

## 1. Purpose

`pc.cpp` owns everything specific to **player characters** on the map-server:

- the runtime player object
- job/class logic
- base/job levels and experience
- stat and skill points, the skill tree
- player parameters (`SP_*`) readable and writable from scripts
- death and respawn
- permissions and GM groups
- item/equipment handling (see [item-system.md](item-system.md))
- many player-scoped features: attendance, reputation, captcha, bank, cooldowns, autotrade, bonus scripts…

Account and character **records** are owned by login and char (see [../dependency-map.md §5](../dependency-map.md#5-cross-server-data-ownership)).

## 2. Data structures

### 2.1 Persistent: `mmo_charstatus`

✅ `src/common/mmo.hpp:562`. This is a POD struct, copied **whole** between char and map (`0x2afd` on login, `0x2b01` on save). Fields:

| Group | Fields |
|---|---|
| Identity | `char_id`, `account_id`, `name`, `slot`, `sex` |
| Family | `partner_id`, `father`, `mother`, `child` |
| Progress | `class_` (job id), `base_level`, `job_level`, `base_exp`, `job_exp`, `zeny`, `status_point`, `skill_point`, `trait_point` |
| Stats | `str..luk`, `pow, sta, wis, spl, con, crt` |
| Pools | `hp, max_hp, sp, max_sp, ap, max_ap` |
| Appearance | `option`, `hair`, `hair_color`, `clothes_color`, `body`, `weapon`, `shield`, `head_top/mid/bottom`, `robe`, `body_direction` |
| Links | `party_id`, `guild_id`, `pet_id`, `hom_id`, `mer_id`, `ele_id`, `clan_id`, `fame`, mercenary faith/calls |
| Places | `last_point`, `save_point`, `memo_point[]` |
| Content | `skill[MAX_SKILL]` (`s_skill {id, lv, flag}`), `friends[]`, `hotkeys[]` |
| Misc | `karma`, `manner` (chat mute), `rename`, `delete_date`, `unban_time`, `character_moves`, `font`, `title_id`, `show_equip`, privacy toggles, `uniqueitem_counter` |

**Every server must agree on its layout**, which depends on compile-time `MAX_*` limits. `chrif_authok` refuses the connection if the sizes differ.

### 2.2 Runtime: `map_session_data`

✅ `class map_session_data : public block_list` (`src/map/pc.hpp:381`), the "`sd`" everywhere. Members, grouped:

| Group | Members |
|---|---|
| Engine | `ud` (unit), `vd` (view), `base_status` / `battle_status`, `sc`, `regen` / `sregen` / `ssregen` |
| Persistent copy | `status` (`mmo_charstatus`) |
| Items | `inventory`, `cart`, `storage`, `premiumStorage` (`s_storage`), `inventory_data[MAX_INVENTORY]` (raw `item_data*`), `equip_index[EQI_MAX]`, `cart_weight/num/max`, `combos` |
| Session | `fd`, `login_id1/2`, `mapindex`, `group_id`, `group` (`s_player_group`), `permissions` (bitset, 65 `PC_PERM_*`), `class_` (**internal MAPID**, §3) |
| Script/NPC | `st`, `npc_id`, `npc_shopid`, `npc_menu`, `npc_amount`, `npc_str`, `eventqueue[]`, `npc_timer_id`, `npc_ontouch_`, `regs` (registry vars), `vars_dirty` |
| Flags | `state` (≈30 bitflags: `active`, `pc_loaded`, `autotrade`, `trading`, `vending`, `storage_flag`, `warping`, `changemap`, `menu_or_input`, `permanent_speed`…) |
| Bonuses | `special_state`, `bonus`, `indexed_bonus`, `right_weapon` / `left_weapon`, autobonus lists… (filled by item scripts, see item-system) |
| Companions | `pd` (pet), `hd` (homunculus), `md` (mercenary), `ed` (elemental) |
| Timers/cooldowns | `followtimer`, `invincible_timer`, `expiration_tid`, `autotrade_tid`, `canlog_tick`, item and skill cooldowns, `skill_id_old`, dance state… |

Lookup: `map_id2sd(account_id)` (`pc_db`), `map_charid2sd(char_id)`, `map_nick2sd(name)`. Iterate with `mapit_getallusers()` / `map_foreachpc(func)`.

## 3. Jobs and classes

Two id spaces (✅ `src/map/map.hpp:80-200`, `src/map/pc.cpp:7519-7760`):

| Id | Where | Shape |
|---|---|---|
| **Job id** (`JOB_*`, e.g. `JOB_KNIGHT = 7`) | `sd->status.class_`, SQL `char.class`, client packets, `db/job_*.yml` | Flat official numbering |
| **Map id** (`MAPID_*`) | `sd->class_` (uint64) | **Bitfield**: the low byte is the base class (`MAPID_SWORDMAN`…), plus `JOBL_2_1` (0x100), `JOBL_2_2` (0x200), `JOBL_THIRD` (0x1000), `JOBL_FOURTH` (0x10000), `JOBL_UPPER` (0x100000, transcendent), `JOBL_BABY` (0x200000). Masks: `MAPID_FIRSTMASK`/`SECONDMASK`/`THIRDMASK`/`FOURTHMASK`/`UPPERMASK`/`BASEMASK` |

Conversions: `pc_jobid2mapid(job)` (`pc.cpp:7519`), `pc_mapid2jobid(mapid, sex)` (`:7701`). Use the map id for **family checks**: `(sd->class_ & MAPID_UPPERMASK) == MAPID_KNIGHT` matches Knight, Lord Knight, Baby Knight and so on. `doc/ea_job_system.txt` documents the layout. Script constants `EAJ_*`/`EAJL_*` expose it (`eaclass()`).

### Job database

✅ `JobDatabase` (`TypesafeCachedYamlDatabase<uint16, s_job_info>`, `pc.hpp:1105`), assembled from several files in `db/re/`:

| File | Feeds |
|---|---|
| `job_stats.yml` (**modified in this fork**) | `max_weight_base`, `hp/sp/ap_factor`/`increase`, per-level `job_bonus` stats, `max_param` |
| `job_basepoints.yml` | `base_hp/sp/ap` tables per level |
| `job_exp.yml` | `base_exp[]` / `job_exp[]` curves, `max_base_level` / `max_job_level` |
| `job_aspd.yml` | `aspd_base[]` per weapon type |
| `job_noenter_map.txt` | `noenter_map` (zones a job cannot enter) |
| `job_outfits.yml` | `alternate_outfits` |

`skill_tree.yml` → `SkillTreeDatabase` (`pc.hpp:1630`): per job, the learnable skills (`s_skill_tree_entry {skill_id, max_lv, baselv, joblv, need{skill→lv}, exclude_inherit}`) and `inherit_job` (which earlier classes' trees are inherited). `statpoint.yml` → `PlayerStatPointDatabase`: stat and trait points gained per level.

### Changing job

✅ `pc_jobchange(sd, job, upper)` (`pc.cpp:10846`), used by job-change NPCs (`jobchange` script command) and `@job`. It validates the job, sets `status.class_` and `sd->class_`, resets the job level to 1 (and caps the base level at the new job's `MaxBaseLevel`, which also resets stats). It does **not** reset or refund skill points: skills outside the new tree lose their `id` and are not saved (see [deep dive §2.3](../../subsystems/map/character-progression-and-job-records.md#23-what-pc_jobchange-does-to-skills-and-what-it-doesnt)). It then recalculates the skill tree (`pc_calc_skilltree`, `:2601`), sends the look and status updates, refreshes party, guild and clan member info, then `status_calc_pc`. The job master NPC in this fork (`npc/custom/jobmaster.txt`) uses this path.

## 4. Levels, experience and points

### 4.1 Gaining experience

✅ `pc_gainexp(sd, src, base_exp, job_exp, exp_flag)` (`pc.cpp:8452`). Callers include monster kill distribution (`mob_dead`), quests, scripts (`getexp`) and party sharing.

1. Unless `exp_flag & 2` (raw): no exp on PvP maps unless `pvp_exp`. Then `pc_calcexp` (`:8366`) applies modifiers: race/class exp bonuses from items (`expaddrace`, `expaddclass`), VIP bonuses, exp-boosting SCs, PK mode.
2. Cap at `max_exp_gain_rate` (per-kill cap as a fraction of the level requirement).
3. Add to `status.base_exp` / `job_exp`, then `pc_checkbaselevelup` / `pc_checkjoblevelup`. Send `clif_updatestatus` and the "+N exp" displays (`clif_displayexp`, `pc_gainexp_disp`).

Global rates (`base_exp_rate`, `job_exp_rate`, `quest_exp_rate`…) live in `conf/battle/exp.conf` (**modified in this fork**) and are applied where exp is **produced**. ✅ For monsters, `base_exp_rate` is baked into each mob's `base_exp` when `mob_db` loads (`mob.cpp:5077`). That's why `@reloadbattleconf` re-runs `mob_reload` when rates change. The level penalty table is `db/re/level_penalty.yml` (`PenaltyDatabase`), which **this fork emptied**, so it doesn't apply.

### 4.2 Level-up

✅ `pc_checkbaselevelup` (`pc.cpp:8244`):

- Loop while `base_exp >= next` and below `pc_maxbaselv`. `multi_level_up` / `multi_level_up_base` control whether several levels can be gained from one exp grant.
- Each level adds `statpoint_db.pc_gets_status_point(level)` and `pc_gets_trait_point`.
- Then: update the pet level, client updates, `status_calc_pc(SCO_FORCE)`, full heal (`status_percent_heal(sd,100,100)`), the level-up effect, the Novice/Taekwon blessing special cases, party/guild/achievement updates, and `OnPCBaseLvUpEvent` (`:8293`).

`pc_checkjoblevelup` (`:8317`) works the same way for job levels, with skill points and `OnPCJobLvUpEvent` (`:8351`).

### 4.3 Spending points

| Action | Function |
|---|---|
| Raise a stat (client `+` button) | `pc_statusup(sd, type, increase)` (`:8872`), cost from `pc_need_status_point` (`:8809`). `pc_statusup2` (`:8934`) is the free GM/script version |
| Raise a skill | `pc_skillup(sd, skill_id)` (`:9129`): checks the skill tree requirements, then `status.skill[idx].lv++`, then a recalculation |
| Reset | `pc_resetstate` (`:9347`), `pc_resetskill(sd, flag)` (`:9428`) |
| Skill level lookup | `pc_checkskill(sd, skill_id)` (`:7325`) → learned level (including temporary or plagiarised skills, per `flag`) |

## 5. Parameters (`SP_*`)

✅ `pc_readparam(sd, type)` / `pc_setparam(sd, type, value)` (`pc.cpp:10411`). About 500 `case SP_*` labels in `pc.cpp`. These back the script "parameters" (`Zeny`, `BaseLevel`, `Hp`, `MaxHp`, `StatusPoint`, `Class`, `Weight`, …) that are registered with `export_parameter` (see [../scripting-engine.md §3.1](../scripting-engine.md#31-the-global-symbol-table)). The **same `SP_*` enum** is used for:

- `clif_updatestatus(sd, SP_X)`, which tells the client a value changed
- `bonus SP_X, value` in item scripts (`pc_bonus`)
- these read/write calls

So adding a new readable value means touching three places.

## 6. Death and respawn

✅ `pc_dead(sd, src)` (`pc.cpp:9771`), called from `status_damage`:

- **Super Novice steel body**: at 99%+ exp, revive and grant Steel Body instead of dying.
- `pc_setdead`, end SCs, stop casting, close dialogs, companion effects.
- PvP/PK handling: `OnPCKillEvent` for the killer (`:9949`), PK-mode rules (`pk_mode&2`), manner, bone drops (`bone_drop`).
- **Death penalty**: `death_penalty_type`, `death_penalty_base`, `death_penalty_job`, `zeny_penalty`, with VIP variants. Skipped on `nopenalty` maps and some SC/job cases.
- `OnPCDieEvent` (fired from `status_damage`, `status.cpp:1786`).

`pc_respawn(sd, clrtype)` (`:9653`) revives the player and warps them to the save point (`pc_setpos(sd, save_point…)`) when they choose Return to Save Point. `pc_setrestartvalue` sets the HP/SP they come back with.

## 7. Permissions and GM groups

✅ `conf/groups.yml` → `PlayerGroupDatabase` (`pc_groups.hpp:111`): `Id`, `Name`, `Level`, `Inherit`, `Commands` (`@cmd: true` self / `#cmd` on others), `CharCommands`, `Permissions` (`PC_PERM_*`: `can_trade`, `use_all_equipment`, `skill_unconditional`, `join_chat`, `hide_session`, …), `LogCommands`.

- On login, `pc_group_pc_load(sd)` copies the group into `sd->group` and `sd->permissions`.
- Code checks `pc_has_permission(sd, PC_PERM_X)` (`pc.cpp:13170`). `@addperm` can change permissions at runtime.
- **Fork note:** `conf/groups.yml` is modified in this fork (commit "Add admin atcommand for normal player (item, points, cash)"), which gives ordinary players some `@` commands.

## 8. Other player features in `pc.cpp`

Discovered while surveying (D0–D1 depth):

- **Bank** (`bank_vault`, `CHARSAVE_BANK`)
- **Attendance** (`AttendanceDatabase`, `attendance.yml`)
- **Reputation** (`ReputationDatabase`)
- **Captcha** (`CaptchaDatabase`, macro detection)
- **Bonus scripts** (`pc_bonus_script`, `:15008`, persisted in SQL `bonus_script`)
- **Item and skill cooldowns** (persisted in SQL `skillcooldown`)
- **Expiration timers** (rental items, `pc_expiration_timer`)
- **Autotrade** (vending/buying store sessions without a client; ✅ restored from SQL once char first connects: `do_init_buyingstore_autotrade` / `do_init_vending_autotrade`, `chrif.cpp:559-563`)
- **Quest info bubbles** (`pc_show_questinfo`, `:15369`)
- **Day/night** (`pc_daynight_timer_sub`)
- **Autosave** (`pc_autosave`, see the database doc)
- **MOTD** (`pc_read_motd`)
- **Hate/feel** (Star Gladiator)
- **Marriage/adoption** (`partner_id`, `father/mother/child`)
- **Ranking/fame** lists (char-side)

## 9. Dependencies

- **Uses**: status (recalculation, HP/SP), itemdb, script (events, params, item scripts), clif (status and level packets), chrif/intif (saving, party/guild sync), skill (tree, `pc_checkskill`), unit/map (position), party/guild/clan/achievement/quest (level-up hooks), log (zeny logging via `log_zeny` in `pc_payzeny`/`pc_getzeny`, `pc.cpp:5812`/`:5846`).
- **Used by**: nearly everything. `pc.hpp` is included by 38 of the 43 map `.cpp` files (see [../dependency-map.md](../dependency-map.md)).

## 10. Customisation guide

| Goal | Best approach |
|---|---|
| Rates (exp, job exp, death penalty) | `conf/battle/exp.conf` (via `conf/import/battle_conf.txt`) |
| Max level, exp curve, HP/SP per level, job stat bonuses | `db/import/job_exp.yml`, `job_basepoints.yml`, `job_stats.yml` overrides |
| Stat points per level | `db/import/statpoint.yml` |
| Skill trees | `db/import/skill_tree.yml` |
| Who can use which @commands | `conf/groups.yml` |
| React to level-ups, deaths, logins | NPC `OnPC*Event` labels (see [npc-system.md §5.2](npc-system.md#52-global-player-events)) |
| A new persistent per-character value | Usually a **permanent character variable** (`var`, stored in `char_reg_num`). Only add an `mmo_charstatus` field plus a `char` column if it must be in the core struct, because that touches `mmo.hpp`, char load/save SQL in `char.cpp`, the `sql-files/` upgrade, and requires all servers to be rebuilt together |
| A new script-readable player value | Add an `SP_*` enum value, cases in `pc_readparam`/`pc_setparam`, `export_parameter` in `script_constants.hpp`, and `clif_updatestatus` if the client shows it |
| A new job | Job id enum and `MAPID_*` mapping in `map.hpp` / `pc_jobid2mapid` / `pc_mapid2jobid`, job DB YAML entries, skill tree, client data. High effort, touches many switches (`grep MAPID_`) |

Watch out for:

- **`sd->class_` vs `sd->status.class_`**: comparing a `MAPID` with a `JOB_` id is a classic bug.
- `status_calc_pc` is expensive (it re-runs every item script). Don't call it in tight loops; use `status_calc_bl` with specific SCB flags where possible.
- Changing `status.*` fields directly skips the client update. Use `pc_setparam`, or call `clif_updatestatus` yourself.
