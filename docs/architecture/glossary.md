# Glossary

> Status: **Phase 6**. Terms used across the code and these documents. "→" points to the document that explains the term in depth.

## Identifiers

| Term | Meaning |
|---|---|
| **AID** | Account id (`login.account_id`, `sd->status.account_id`). Starts at `START_ACCOUNT_NUM` = 2000000 (`mmo.hpp:176`); char ids start at 150000 (`:178`). Also used as the player's `bl.id` on the map, so a player's game id equals their AID |
| **CID** / **char_id** | Character id (`char.char_id`). Persistent key for everything character-owned |
| **GID** | "Game id": the `block_list::id` of any unit on a map. For players it's the AID. Monsters, NPCs and other units get ids from `map_get_new_object_id` (`map.cpp:1649`). The web API reuses `GID` to mean char id |
| **GDID** | Guild id (web API field) |
| **nameid** | Item definition id (`t_itemid`), the key into the item DB |
| **mob_id / class_** | Monster definition id (key into the mob DB). `class_` is also the player job id or the NPC sprite id, depending on the object |
| **skill_id / skill_lv** | Skill id (`e_skill`, for example `SM_BASH`) and level |
| **mapindex / m** | `mapindex` = global map name index (`map_index.txt`). `m` = this map-server's local `map[]` slot |
| **fd** | Socket descriptor index into `session[]` → [networking.md](networking.md) |
| **login_id1 / login_id2** | Random session keys created by login and checked by char and map when the client hops servers → [login-and-char-servers.md](login-and-char-servers.md) |
| **unique_id** | 64-bit GUID on items flagged `guid`, used for tracking |

## Code abbreviations (variable names)

| Abbrev | Type | Notes |
|---|---|---|
| `bl` | `block_list` | Base of every map object: id, type (`BL_*`), position, block-grid links → [gameplay/map-and-movement.md](gameplay/map-and-movement.md) |
| `sd` / `TBL_PC` | `map_session_data` | A player |
| `md` | `mob_data` (monster) **or** `s_mercenary_data` (`sd->md`, mercenary) | Read the type, not the name |
| `nd` | `npc_data` | NPC |
| `hd` | `homun_data` | Homunculus (`sd->hd`) |
| `pd` | `pet_data` | Pet (`sd->pd`) |
| `ed` | `s_elemental_data` | Elemental (`sd->ed`) |
| `ud` | `unit_data` | Movement, attack and cast state for any unit that can move |
| `vd` | `view_data` | Appearance sent to clients |
| `sc` / `sce` | `status_change` / `status_change_entry` | Active status effects (`SC_*`) → [gameplay/status-system.md](gameplay/status-system.md) |
| `st` | `script_state` | A running script instance → [scripting-engine.md](scripting-engine.md) |
| `tsd`, `tbl` | "target" sd / bl | Convention for the second party |
| `src` / `target` | `block_list*` | Attacker / defender in battle code |
| `wd`, `ad`, `md` (in battle) | `Damage` | Weapon / magic / misc damage records → [gameplay/combat-system.md](gameplay/combat-system.md) |
| `gc` | `guild_castle` | WoE castle data |
| `p`, `g` | `party_data` / `MapGuild` | Party / guild caches |

## Engine concepts

| Term | Meaning |
|---|---|
| **Core / `main_core<T>`** | Shared server lifecycle class each server subclasses → [startup-and-initialization.md](startup-and-initialization.md) |
| **Reactor / main loop** | `do_timer` → `do_sockets` → parse, single-threaded → [threading-and-concurrency.md](threading-and-concurrency.md) |
| **Tick / `gettick()`** | Millisecond monotonic clock, cached per loop iteration |
| **TIMER_FUNC** | Timer callback signature `(tid, tick, id, data)` |
| **FIFO (RFIFO/WFIFO)** | Per-socket read and write buffers → [networking.md](networking.md) |
| **PACKETVER** | Client date the server speaks to (`20220406` in this fork) → [packet-system.md](packet-system.md) |
| **packet_db / clif** | Client packet table and the "client interface" module (`clif.cpp`) |
| **chrif / intif** | Map↔char link: session control (`0x2b**`) and inter traffic (`0x30**`/`0x38**`) → [inter-server.md](inter-server.md) |
| **mapif / chmapif / logchrif / chlogif** | Same links seen from the other side (char→map, login→char, char→login) |
| **inter / int_*** | Char-server modules that own shared data (party, guild, storage…) |
| **auth_node / auth_db** | Pending hand-off token between servers |
| **online_db** | Who is online where (login and char each keep one) |
| **DBMap / `idb_*`, `strdb_*`** | Legacy hash map → [memory-management.md](memory-management.md) |
| **ERS** | "Entry Reusage System": fixed-size object pool allocator |
| **aMalloc / aFree / CREATE** | Memory manager wrappers (`USE_MEMMGR`) |
| **map_freeblock_lock** | Deferred deletion guard while iterating units |
| **bl_list / `map_foreachinrange`** | Area iteration over the 8×8-cell block grid |
| **AREA_SIZE** | Visibility radius (14 cells by default) |
| **Cell / `CELL_*`** | Per-tile flags (walkable, water, no-vending, no-chat…) |
| **Mapflag / `MF_*`** | Per-map rules (pvp, gvg_castle, nosave, noteleport…) |
| **Map cache** | Pre-built map geometry (`db/map_cache.dat`) |
| **Instance map** | Runtime copy of a map for one party, guild or char → [gameplay/content-systems.md](gameplay/content-systems.md) |
| **YAML DB / `TypesafeYamlDatabase`** | Data loader framework (`Header`, `Body`, `Footer.Imports`) → [database-architecture.md](database-architecture.md) |
| **import / import-tmpl** | Local override folders (`conf/import`, `db/import`), generated from templates and gitignored → [customization-guide.md](customization-guide.md) |
| **Renewal / pre-renewal (RE / pre-RE)** | The two official rule sets. This fork builds `RENEWAL` (`db/re/`) |
| **MINICORE** | Reduced common library used by tools (csv2yaml, mapcache) |

## Scripting

| Term | Meaning |
|---|---|
| **buildin / BUILDIN_FUNC** | A script command implemented in C++ |
| **script_code** | Compiled bytecode buffer for one script |
| **RERUNLINE / sleep / menu** | Script suspension states that resume later |
| **Variable prefixes** | `@` temporary char, `$` global (mapreg), `$@` temporary global, `.` NPC, `.@` scope, `'` instance, `#` account, `##` global account. Suffix `$` = string → [scripting-engine.md §5](scripting-engine.md) |
| **mapreg** | Persistent `$` global variables (SQL `mapreg`) |
| **Registry (reg)** | Persistent char and account variables (`char_reg_*`, `acc_reg_*`, `global_acc_reg_*`) |
| **Event label** | `OnSomething:` entry point; called as `NpcName::OnSomething` → [gameplay/npc-system.md](gameplay/npc-system.md) |
| **Duplicate NPC** | NPC sharing another's code (`duplicate(Name)`) |
| **Floating NPC** | NPC with map `-` (no position), used for event-only scripts |

## Characters, status and combat

| Term | Meaning |
|---|---|
| **Base / Job level, exp** | Character level and job level → [gameplay/character-system.md](gameplay/character-system.md) |
| **Class / job / mapid** | Job id (`JOB_*`) vs. the bitmask job hierarchy (`MAPID_*`) |
| **Trans / rebirth, 3rd, 4th, Expanded** | Job tiers. `_2nd` suffix keys exist for alternate outfits (fork) |
| **base_status / battle_status** | Stats from character + gear vs. after all status changes → [gameplay/status-system.md](gameplay/status-system.md) |
| **SC / `SC_*`** | Status change (buff or debuff) |
| **SCB / `SCB_*`** | Bit flags saying which stats an SC changes (partial recalculation) |
| **opt1 / opt2 / option** | Client-side status display bits (stone, poison, cart, riding…) |
| **ATK / MATK / DEF / MDEF / HIT / FLEE / CRI / ASPD** | Standard RO combat stats |
| **POW / STA / WIS / SPL / CON / CRT, P.ATK, S.MATK, RES, MRES, AP** | 4th-job "trait" stats (Renewal) |
| **Race / Element / Size** | Damage modifier axes (`RC_*`, `ELE_*`, `SZ_*`) → [gameplay/combat-system.md](gameplay/combat-system.md) |
| **BF_*** | Battle flags: weapon / magic / misc, short / long, normal / skill |
| **BCT_*** | Target categories for `battle_check_target` (enemy, party, guild, self…) |
| **battle_config** | ~631 tunables from `conf/battle/*.conf` |
| **Skill ratio** | Percentage multiplier of a skill over a normal attack |
| **SkillImpl / SkillFactory** | Per-skill C++ classes, plus the custom override factory → [gameplay/skill-system.md](gameplay/skill-system.md) |
| **Skill unit / ground skill** | Persistent area effect (`skill_unit`, `skill_unit_group`) |
| **Autobonus** | Temporary bonus triggered by chance on attack, skill or being hit (rate 1000 = 100%) |
| **Combo** | Bonus when a set of items is equipped together |
| **Refine / downgrade / break** | Upgrading equipment. This fork downgrades instead of breaking |
| **Random option** | Extra rolled stats on an item instance |

## Monsters and content

| Term | Meaning |
|---|---|
| **Mob / mode (`MD_*`)** | Monster and its behaviour flags (aggressive, looter, boss…) → [gameplay/monster-system.md](gameplay/monster-system.md) |
| **Hard AI / lazy AI** | 100 ms AI for mobs near players vs. 1 s cheap tick |
| **dmglog** | Per-mob list of who did damage, used for exp and drop credit |
| **MVP** | Boss monster with a special reward to the top damage dealer |
| **Spawn / respawn** | `monster` script lines and `mob_spawn`. `mob_data` is reused on respawn |
| **WoE / Agit** | War of Emperium (guild castle siege). `agit_flag`, `OnAgitStart` → [gameplay/social-systems.md](gameplay/social-systems.md) |
| **FE / SE / TE** | WoE First, Second and Training editions |
| **Emperium** | Castle core monster (`MOBID_EMPERIUM`) |
| **BG** | Battleground: queued team PvP |
| **RoDEX** | Modern in-game mail |
| **Vending / autotrade** | Player shop / shop that stays open after the client disconnects → [gameplay/economy-systems.md](gameplay/economy-systems.md) |
| **Buying store / search store** | Reverse shop / catalog search across shops |
| **Kafra** | Storage, save and teleport service NPCs |
| **Homunculus / Mercenary / Elemental / Pet** | Companions → [gameplay/companions.md](gameplay/companions.md) |
| **Intimacy / loyalty / faith** | Companion affection values |

## Data stores and servers

| Term | Meaning |
|---|---|
| **login / char / map / web server** | The four executables → [server-architecture.md](server-architecture.md) |
| **ragnarok DB / log DB** | Main schema (`main.sql`, `web.sql`) and logging schema (`logs.sql`) |
| **autosave** | Periodic character save from map to char → [database-architecture.md §6](database-architecture.md) |
| **save_settings / CHARSAVE_*** | Extra immediate saves after trade, vending, storage and so on |
| **web_auth_token** | Per-login token the client uses against the web-server → [web-server.md](web-server.md) |
| **Group / permissions** | `conf/groups.yml` levels, commands and permissions (group 0 = player) |
