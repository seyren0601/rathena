# Repository Structure

> Status: **Discovery pass**. Directory responsibilities verified by listing and spot-reading files. Line counts from `wc -l` on this checkout.

```
rathena/
├── src/            C++ source for all servers and tools          (runtime code)
├── 3rdparty/       Vendored libraries                            (runtime deps)
├── conf/           Server runtime configuration                  (config)
├── db/             Static game data (YAML/TXT), map cache        (data)
├── npc/            NPC scripts, loaded by the map-server         (content)
├── sql-files/      MySQL schema + upgrade scripts                (schema)
├── doc/            Upstream reference docs (script cmds, packets…) (docs)
├── docs/           This architecture knowledge base              (docs, new)
├── tools/          Helper scripts, CI, Docker, converters        (dev tools)
├── CMakeLists.txt / configure.ac / Makefile.in / rAthena.sln       (build)
└── athena-start, *.bat (in tools/), install.sh                    (launch scripts)
```

---

## `src/` — source code

### `src/common/` — shared core library (linked into every executable)

| File | Responsibility | Key symbols |
|---|---|---|
| `core.cpp/.hpp` | Process lifecycle, signal handling, main loop | `rathena::server_core::Core`, `Core::start` (`core.cpp:333`), `e_core_status`, `main_core<T>`, `global_core` |
| `socket.cpp/.hpp` (1762 lines) | Non-blocking TCP, per-fd read/write FIFOs, select/epoll dispatch, DDoS/IP access control | `socket_data`, `session[]`, `do_sockets` (`socket.cpp:892`), `make_listen_bind`, `make_connection`, `RFIFO*/WFIFO*` macros, `socket_send<P>` |
| `timer.cpp/.hpp` | Heap-based timer scheduler — *the* mechanism for all deferred work | `add_timer`, `add_timer_interval`, `delete_timer`, `do_timer`, `TIMER_FUNC(x)` macro |
| `packets.hpp` | Typed packet structs shared by servers + generic `PacketDatabase<session>` dispatcher | `PacketDatabase` (`packets.hpp:653`) |
| `mmo.hpp` | **Shared persistent data model** passed between servers | `mmo_charstatus` (`mmo.hpp:562`), `item` (`mmo.hpp:313`), party/guild/storage structs, `MAX_*` limits |
| `database.cpp/.hpp` | YAML database framework | `YamlDatabase`, `TypesafeYamlDatabase<K,V>`, `TypesafeCachedYamlDatabase<K,V>` |
| `db.cpp/.hpp` (3031 lines) | Legacy generic hash/tree map (C-style, `DBMap`) with `idb_*`, `uidb_*`, `strdb_*`, `i64db_*` helpers | `DBMap`, `DBKey`, `DBData`, `db_alloc` |
| `ers.cpp/.hpp` | Entry Reusage System — fixed-size object pools | `ers_new`, `ers_alloc`, `ers_free` |
| `malloc.cpp/.hpp` | Allocation wrappers & optional memory manager/leak tracking | `aMalloc`, `aCalloc`, `aRealloc`, `aFree`, `CREATE`, `RECREATE` |
| `sql.cpp/.hpp` | MySQL wrapper (handles + prepared statements) | `Sql_Malloc`, `Sql_Query` (`sql.cpp:246`), `class SqlStmt` (`sql.hpp:243`) |
| `conf.cpp/.hpp` | libconfig wrapper | — |
| `showmsg.cpp/.hpp` | Coloured console & file logging | `ShowStatus`, `ShowError`, `ShowDebug`… |
| `strlib`, `utils`, `utilities` | String helpers, misc utilities, `Singleton`, map helpers | `safestrncpy`, `rathena::util::*` |
| `grfio.cpp` | Reads client GRF archives (for map data when `enable_grf`) | `grfio_init` |
| `mapindex.cpp` | Map name ↔ index table (`db/map_index.txt`) | `mapindex_init` |
| `msg_conf.cpp` | Multi-language message tables | — |
| `cli.cpp` | Command-line options and console command reading | `cli_get_options` |
| `md5calc`, `des`, `random`, `nullpo`, `winapi` | Hashing, DES (GRF), RNG, null-pointer guard macros, Windows shims | — |

### `src/login/` — login-server

| File | Responsibility |
|---|---|
| `login.cpp` | `LoginServer`, config reading, online-account tracking, auth node bookkeeping |
| `loginclif.cpp` | Client protocol (`logclif_parse`, `LoginPacketDatabase` at `loginclif.cpp:483`) |
| `loginchrif.cpp` | Protocol with char-servers (`logchrif_parse`, `loginchrif.cpp:776`) |
| `account.cpp` | SQL account engine (`account_db_sql`) |
| `ipban.cpp`, `loginlog.cpp` | IP banning, login logging |
| `logincnslif.cpp` | Console commands |

### `src/char/` — char-server

| File | Responsibility |
|---|---|
| `char.cpp` | `CharacterServer`, char load/save to SQL (`char_mmo_char_tosql` `:257`, `char_mmo_char_fromsql` `:1023`), online char tracking, fame lists |
| `char_clif.cpp` | Client protocol (`chclif_parse` `:1624`, `CharPacketDatabase` `:1595`): char list, select, create, delete, pincode, rename |
| `char_logif.cpp` | Link to login-server (`chlogif_parse` `:750`) |
| `char_mapif.cpp` | Link to map-servers (`chmapif_parse` `:1398`) |
| `inter.cpp` | Inter-server relay dispatcher (`inter_parse_frommap` `:1408`), registry vars, whisper routing, `InterServerDatabase` (`conf/inter_server.yml`) |
| `int_*.cpp` | One module per shared feature persisted by char: party, guild, storage, mail, auction, pet, homunculus, mercenary, elemental, quest, achievement, clan |
| `char_cnslif.cpp` | Console commands |

### `src/map/` — map-server (the bulk of the game: ~200k lines)

Largest files: `script.cpp` 28.6k, `clif.cpp` 25.9k, `status.cpp` 16.5k, `skill.cpp` 16.3k, `pc.cpp` 16.2k, `atcommand.cpp` 12.2k, `battle.cpp` 9.2k, `mob.cpp` 7.4k, `npc.cpp` 6.4k, `map.cpp` 5.5k, `itemdb.cpp` 5.0k, `unit.cpp` 4.3k, `intif.cpp` 3.9k.

Grouped by role (detailed in [subsystems.md](subsystems.md)):

| Group | Files |
|---|---|
| World & objects | `map.cpp` (maps, block grid, object registry), `unit.cpp` (movement/attack/cast state machine), `path.cpp` (pathfinding), `navi.cpp` (navigation data generation) |
| Client protocol | `clif.cpp`, `clif_packetdb.hpp`, `clif_shuffle.hpp`, `clif_obfuscation.hpp`, `packets.hpp`, `packets_struct.hpp` |
| Inter-server | `chrif.cpp` (char-server control link), `intif.cpp` (inter relay link) |
| Player | `pc.cpp`, `pc_groups.cpp`, `storage.cpp`, `trade.cpp`, `vending.cpp`, `buyingstore.cpp`, `searchstore.cpp`, `cashshop.cpp`, `mail.cpp` |
| Combat | `battle.cpp`, `skill.cpp`, `skills/` (per-skill classes), `status.cpp` |
| Data DBs | `itemdb.cpp`, `mob.cpp` (also AI), `quest.cpp`, `achievement.cpp`, … |
| Content/scripting | `script.cpp`, `script_constants.hpp`, `npc.cpp`, `npc_chat.cpp`, `mapreg.cpp`, `atcommand.cpp` |
| Social | `party.cpp`, `guild.cpp`, `clan.cpp`, `chat.cpp`, `channel.cpp`, `duel.cpp` |
| Companions | `pet.cpp`, `homunculus.cpp`, `mercenary.cpp`, `elemental.cpp` |
| Instanced content | `instance.cpp`, `battleground.cpp` |
| Misc | `log.cpp` (SQL audit logs), `date.cpp` |

#### `src/map/skills/` — per-skill implementation classes (modern refactor)

✅ 1,244 `.cpp` files organised by job family (`acolyte/` 102, `archer/` 127, `mage/` 145, `npc/` 155, `merchant/` 106, `taekwon/` 95, `thief/` 91, `swordman/` 82, …, `custom/` 1).

- `skill_impl.hpp` defines `class SkillImpl` with virtual hooks: `castendNoDamageId`, `castendDamageId`, `castendPos2`, `calculateSkillRatio`, `modifyHitRate`, `applyAdditionalEffects`, `applyCounterAdditionalEffects`, `modifyDamageData`, `modifyElement`. Comments state they *replace* the old `switch` blocks in `skill.cpp`/`battle.cpp`.
- `skill_factory.cpp` is a **unity build**: it `#include`s every family's `skill_factory_<family>.cpp`. `SkillFactoryImpl::create` asks factories in order, **custom first** (so custom implementations override official ones).
- Bound at load time in `SkillDatabase::loadingFinished` (`skill.cpp:~15760`) into `s_skill_db::impl` (`skill.hpp:322`).
- Used from `skill.cpp:4238`, `:4590`, `:5445`, `:1338`, `:1738` and `battle.cpp:3294`, `:3495`, `:3522`.
- ❓ The migration is partial: the legacy `switch` blocks still exist for skills without an `impl`.

### `src/web/` — web-server

`web.cpp` sets up cpp-httplib routes (`web.cpp:485-498`) served by `*_controller.cpp`; `auth.cpp` validates requests; `sqllock.cpp` provides mutex-guarded SQL handles because HTTP handlers run on httplib's worker threads.

### `src/tool/` — offline tools

`mapcache.cpp`, `csv2yaml.cpp`, `yaml2sql.cpp`, `yamlupgrade.cpp`; built against MINICORE. See `src/tool/readme.md`.

### `src/config/` — compile-time switches

`core.hpp` (includes `custom/defines_pre.hpp` at line 12 and `custom/defines_post.hpp` at line 126), `packets.hpp`, `renewal.hpp`, `const.hpp`, `secure.hpp`, `classes/general.hpp`.

### `src/custom/`

Official injection points for source customisations (✅ verified include sites):

| File | Included at | Purpose |
|---|---|---|
| `defines_pre.hpp` / `defines_post.hpp` | `config/core.hpp:12` / `:126` | Override compile-time defines |
| `atcommand.inc` / `atcommand_def.inc` | `atcommand.cpp:11467` / `:11483` | Add @commands (body / registration) |
| `script.inc` / `script_def.inc` | `script.cpp:27878` / `:28657` | Add script commands (body / `BUILDIN_DEF`) |
| `battle_config_struct.inc` / `battle_config_init.inc` | `battle.hpp:785` / `battle.cpp:8912` | Add `battle_config` fields |
| `skills/custom/skill_factory_custom.cpp` | `skills/skill_factory.cpp:18` | Add/override per-skill classes |

---

## `3rdparty/`

`httplib`, `json` (nlohmann), `libconfig`, `mysql` (headers/libs for Windows), `pcre`, `rapidyaml`, `yaml-cpp`, `zlib`, and `cmake/` helpers.

## `conf/` — runtime configuration

| File | Read by |
|---|---|
| `login_athena.conf` | login |
| `char_athena.conf` | char |
| `map_athena.conf` (imports `maps_athena.conf`, list of maps to load) | map |
| `inter_athena.conf` | char, map, web (SQL connection settings & table names) |
| `inter_server.yml` | char (`InterServerDatabase`: storage tables) |
| `battle_athena.conf` → `battle/*.conf` (17 files: exp, drops, skill, status, player, monster…) | map (`battle_config_read`, `battle.cpp:9158`) |
| `script_athena.conf`, `log_athena.conf`, `channels.conf`, `groups.yml` (GM groups/permissions), `atcommands.yml` (aliases) | map |
| `packet_athena.conf` | all (socket/DDoS settings, `socket_config_read`) |
| `subnet_athena.conf` | login/char (LAN routing) |
| `web_athena.conf` | web |
| `msg_conf/` | per-server message tables, multi-language for map |
| `import-tmpl/` | templates for local overrides in `conf/import/` |

## `db/` — static game data

- Root: the **entry files** the servers open (e.g. `db/skill_db.yml`, `db/item_db.yml`). ✅ Many are thin shims containing only a `Header` and a `Footer.Imports` list with `Mode: Prerenewal|Renewal` filters, chaining to `db/pre-re/…`, `db/re/…`, then `db/import/…` (see `db/skill_db.yml:140-151`). Also `map_index.txt`, `map_cache.dat`, `GeoIP.dat`, `const.yml`.
- `db/re/` (55 entries) and `db/pre-re/` (38): mode-specific data — `item_db_*.yml`, `mob_db.yml`, `skill_db.yml`, `job_stats.yml`, `refine.yml`, `status.yml`, etc.
- `db/import-tmpl/`: templates for `db/import/` overrides.
- A few legacy TXT DBs remain (`skill_changematerial_db.txt`, `skill_damage_db.txt`, `status_disabled.txt`, `re/mob_skill_db.txt`, `re/produce_db.txt`, `re/skill_nocast_db.txt`, `re/item_noequip.txt`, `re/job_noenter_map.txt`).
- Each YAML file is loaded by a `TypesafeYamlDatabase` subclass — see the [list in subsystems.md](subsystems.md#yaml-database-classes).

## `npc/` — scripts

Entry: `npc/re/scripts_main.conf` (or `pre-re`) → imports `npc/scripts_*.conf` → `npc:` lines naming script files. Organised by `cities/`, `quests/`, `jobs/`, `instances/`, `mobs/` (spawn definitions), `warps/`, `mapflag/`, `guild*/`, `custom/` (this fork adds `change_dress.txt`, `endower.txt`, modified `healer.txt`, `card_remover.txt`…), `test/`.

## `sql-files/`

`main.sql` (56 tables: `login`, `char`, `inventory`, `cart_inventory`, `storage`, `guild*`, `party`, `mail*`, `skill`, `sc_data`, `*_reg_*`, `mapreg`, …), `logs.sql` (10), `web.sql` (4), `upgrades/` (dated migrations), `compatibility/`, generated `item_db*.sql`/`mob_db*.sql` (from `yaml2sql`, ⚠️ for external tools/control panels).

## `doc/` — upstream reference

Authoritative references worth reading alongside this KB: `script_commands.txt`, `packet_client.txt`, `packet_interserv.txt`, `item_bonus.txt`, `status_change.txt`, `mapflags.txt`, `permissions.txt`, `atcommands.txt`, `source_doc.txt`, `yaml/` (YAML DB format docs).

## `tools/`

Launch scripts (`*serv.bat`, `runserver.bat`; Unix uses root `athena-start`), converters (`convert_sql.pl`, `mapreg-converter.php`, `navi.py`), `ci/` (NPC/SQL validation), `docker/`, `stackdump`.
