# Project Overview

> Status: **Discovery pass** (phase 1). Statements marked ✅ were verified in source; ⚠️ are inferences; ❓ are unknown/not yet investigated.

## What this is

rAthena is an open-source Ragnarok Online server emulator written in C++. This checkout is a **customised fork** (branch `my-server`, 24 commits ahead of `master`) — see [Fork-specific changes](#fork-specific-changes).

The runtime is split into **four network server processes** plus several **offline tools**, all built on one shared library (`src/common`).

| Executable | Entry point | Core class | Role |
|---|---|---|---|
| `login-server` | `src/login/login.cpp:914` | `LoginServer` (`src/login/login.hpp:21`) | Account authentication, char-server registry, IP bans |
| `char-server` | `src/char/char.cpp:3283` | `CharacterServer` (`src/char/char.hpp:22`) | Character select/create/delete, **all character persistence**, inter-server relay (parties, guilds, mail, storage…) |
| `map-server` | `src/map/map.cpp:5477` | `MapServer` (`src/map/map.hpp:31`) | The game world: maps, movement, combat, skills, NPC scripts, monsters |
| `web-server` | `src/web/web.cpp:533` | `WebServer` (`src/web/web.hpp:19`) | HTTP API used by newer clients (emblems, char/user config, party booking, merchant store) |
| `map-server-generator` | same as map, built with `MAP_GENERATOR` | `MapServer` | Offline generator for navigation data, item move info, reputation (`src/map/map.cpp` `#ifdef MAP_GENERATOR` block) |
| `mapcache` | `src/tool/mapcache.cpp:307` | `MapcacheTool` | Builds `db/map_cache.dat` from GRF data |
| `csv2yaml` | `src/tool/csv2yaml.cpp:5281` | `Csv2YamlTool` | Converts legacy TXT/CSV DBs to YAML |
| `yaml2sql` | `src/tool/yaml2sql.cpp:970` | `Yaml2SqlTool` | Exports YAML DBs to SQL (`sql-files/item_db*.sql`, `mob_db*.sql`) |
| `yamlupgrade` | `src/tool/yamlupgrade.cpp:604` | `YamlUpgradeTool` | Upgrades YAML DB files between schema versions |

✅ Every executable uses the same template entry `main_core<T>()` (`src/common/core.hpp:95`), which instantiates the server class, stores it in `global_core`, and calls `Core::start()`.

## Languages & technologies

| Area | Technology | Evidence |
|---|---|---|
| Server code | C++ (C-style legacy + modern C++17 features: `std::shared_ptr`, templates, `std::function`) | `src/**/*.cpp` |
| Fixed-width integer aliases | `int32`, `uint16`, `t_tick`, … | `src/common/cbasetypes.hpp` |
| Persistent storage | MySQL / MariaDB | `3rdparty/mysql`, `src/common/sql.cpp`, `sql-files/main.sql` |
| Static game data | YAML (parsed with **rapidyaml**/`ryml`) | `src/common/database.hpp:19`, `db/**/*.yml` |
| Legacy config | libconfig-style `.conf` and plain `key: value` `.conf` files | `src/common/conf.cpp`, `conf/*.conf` |
| NPC content | rAthena's own script language (custom compiler + bytecode VM) | `src/map/script.cpp`, `npc/**/*.txt` |
| HTTP | cpp-httplib + nlohmann/json | `3rdparty/httplib`, `3rdparty/json`, `src/web/web.cpp` |
| Regex | PCRE (NPC chat triggers `OnPCRE`, `pcre_match` script cmd) | `src/map/npc_chat.cpp`, `src/map/script.cpp` |
| Compression | zlib (GRF reading, guild emblems) | `src/common/grfio.cpp`, `src/map/clif.cpp` |
| Tooling YAML | yaml-cpp (used only by the conversion tools) | `src/tool/csv2yaml.cpp`, `src/tool/CMakeLists.txt` |
| Scripts in `tools/` | Perl, Python, PHP, Batch, Shell | `tools/` |

## Build systems

Three parallel build systems are maintained (✅ all present at the repo root):

1. **CMake** — `CMakeLists.txt`, `src/CMakeLists.txt`, per-module `src/*/CMakeLists.txt`, `3rdparty/CMakeLists.txt`.
2. **Autotools** — `configure.ac` → `configure`, `Makefile.in`, `src/*/Makefile.in`.
3. **Visual Studio** — `rAthena.sln` with `src/**/*.vcxproj`. `common.vcxproj` and `common-minicore.vcxproj` produce the full and "minicore" variants of the common library.

**MINICORE** (⚠️ inferred from `#ifndef MINICORE` guards in `src/common/core.cpp:362-371` and `:386`): tools link a reduced common library without SQL, timers, sockets or the main loop.

CI: `.github/workflows/` builds with GCC, Clang, CMake (Linux/macOS), MSBuild, across packet versions, VIP mode and pre-renewal/renewal modes, plus CodeQL and an NPC DB validation job. Docker setup lives in `tools/docker/`.

## Compile-time configuration (very important)

A large part of server behaviour is chosen at **compile time**, not runtime:

| File | Controls | Current value in this fork |
|---|---|---|
| `src/config/packets.hpp` | `PACKETVER` (client date), `PACKET_OBFUSCATION` | default `20211103` |
| `src/custom/defines_pre.hpp` | user overrides included *before* config | ✅ `#define PACKETVER 20220406` (note: placed **after** the include guard's `#endif` — works, but is outside the guard) |
| `src/config/renewal.hpp` | `RENEWAL` vs `PRERE` and per-mechanic renewal switches | ✅ `RENEWAL` defined |
| `src/config/core.hpp` | misc core features | ✅ `ENABLE_CASHSHOP_PREVIEW_PATCH` enabled in this fork |
| `src/config/const.hpp`, `classes/general.hpp`, `secure.hpp` | constants and security limits | ❓ not yet inspected |
| `src/custom/*.inc` | inject custom atcommands, script commands, battle-config keys | see [repository-structure.md](repository-structure.md#srccustom) |

Changing any of these requires a rebuild of all servers.

## Runtime modes

- **Renewal vs. pre-renewal**: selected by `RENEWAL`; data loads from `db/re/` or `db/pre-re/` and NPCs from `npc/re/scripts_main.conf` or `npc/pre-re/scripts_main.conf` (✅ `map_reloadnpc`, `src/map/map.cpp:4258-4266`).
- **Imports**: nearly every conf/db file supports an override in `conf/import/` / `db/import/` (templates in `conf/import-tmpl/`, `db/import-tmpl/` — 60 files). Local customisation should go there rather than editing base files.

## Fork-specific changes

`git diff --stat master...my-server` touches 26 files. Source-code changes only:

| File | Change |
|---|---|
| `src/custom/defines_pre.hpp` | `PACKETVER 20220406` |
| `src/config/core.hpp` | `ENABLE_CASHSHOP_PREVIEW_PATCH` |
| `src/map/itemdb.cpp` (`itemdb_isidentified`) | Always returns `1` — all items drop identified |
| `src/common/malloc.cpp` | Readability refactor |

The full commit-by-commit list, config and DB values, and merge hazards are in [fork-notes.md](fork-notes.md).

Everything else is data/config/NPC changes (rates, refine table, cash shop, custom NPCs such as `npc/custom/change_dress.txt`, `endower.txt`). Upstream merges will need to account for these.

## Related documents

- [repository-structure.md](repository-structure.md)
- [server-architecture.md](server-architecture.md)
- [subsystems.md](subsystems.md)
- [dependency-map.md](dependency-map.md)
- [roadmap.md](roadmap.md)
