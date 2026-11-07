# Documentation Roadmap & Open Questions

## 1. Planned documents

Phases are ordered so that each builds on the previous one. "Depends on" lists the documents a reader (and the author) should have first.

| Phase | Document | Scope | Depends on |
|---|---|---|---|
| **1 – Discovery** ✅ | `project-overview.md`, `repository-structure.md`, `server-architecture.md`, `subsystems.md`, `dependency-map.md`, `roadmap.md` | This pass | — |
| **2 – Core** ✅ | `startup-and-initialization.md` | Full init/finalize for all 4 servers, every `do_init_*`/`do_final_*`, reload paths (`@reload*`) | phase 1 |
| 2 | `threading-and-concurrency.md` | Timer heap internals, loop timing, `gettick` caching, web threading, reentrancy hazards (`map_freeblock_lock`) | phase 1 |
| 2 | `memory-management.md` | `aMalloc`/memmgr, `ers`, `DBMap` ownership, `shared_ptr` in YAML DBs, `map_freeblock` deferred deletion | phase 1 |
| 2 | `networking.md` | Socket lifecycle, FIFOs, epoll/select, DDoS, full client lifecycle with exact packet IDs, disconnect and autotrade | phase 1 |
| 2 | `packet-system.md` | `packet_db`/`clif_packetdb.hpp` versioning, `packets_struct.hpp`, obfuscation & shuffle, `PacketDatabase`, inter-server tables (chrif/intif/inter/logchrif), how to add a packet | networking |
| 2 | `database-architecture.md` | `Sql`/`SqlStmt`, handle usage per server, schema relationships (ER diagram), YAML framework internals (imports, cache, `loadingFinished`), runtime ↔ SQL mapping, saving cadence | phase 1 |
| 3 – Engine | `scripting-engine.md` | Lexer/parser, bytecode (`c_op`), stack VM, variables & scopes, `buildin_func` registration, sleep/`RERUNLINE`, script ↔ C++ bridge, adding a command; trace one NPC end-to-end | database |
| 3 | `gameplay/map-and-movement.md` | `map_data`, cells, block grid, `unit_walktoxy` → path → timers → `clif_move`, warping (`pc_setpos`, cross-server map change), mapflags | networking |
| 3 | `gameplay/npc-system.md` | NPC file parsing, NPC types, events (`OnInit`, `OnPC*`, `OnClock`, `OnTouch`), dialog flow, duplicates, reload | scripting |
| 4 – Gameplay | `gameplay/character-system.md` | `map_session_data`, auth/load/save, jobs, stat points, exp, registry vars | database |
| 4 | `gameplay/status-system.md` | `status_calc_*` pipeline, `status_change` DB & lifecycle, SC flags, icons | character |
| 4 | `gameplay/skill-system.md` | `skill_db.yml` → `s_skill_db`, cast flow (`unit_skilluse_id2` → `skill_castend_*` → `skill_attack`), requirements, units/ground skills, `SkillImpl` migration | status |
| 4 | `gameplay/combat-system.md` | `battle_calc_attack` pipeline, weapon/magic/misc, renewal vs pre-re formulas, element/size/race modifiers, damage display | skill |
| 4 | `gameplay/item-system.md` | `item_data`, inventory/cart/storage, equip flow, bonuses, combos, random options, refine/enchant/reform, cash shop | status |
| 4 | `gameplay/monster-system.md` | `mob_db`, spawns (`npc_parse_mob`), AI state machine, mob skills, drops, MVP, summons | combat |
| 5 – Social/economy | `gameplay/social-systems.md` | party, guild/WoE, clan, chat, channels, friends, duel | inter-server |
| 5 | `gameplay/economy-systems.md` | trade, storage, vending, buying/search store, mail/RoDEX, auction, barter/market | item |
| 5 | `gameplay/companions.md` | pet, homunculus, mercenary, elemental | status, unit |
| 5 | `gameplay/content-systems.md` | quests, achievements, instances, battlegrounds, attendance, reputation | npc |
| 5 | `inter-server.md` | char-server `inter`/`int_*` modules, relay patterns, ownership | packet-system |
| 5 | `login-and-char-servers.md` | Internals of login and char beyond the network handshake | networking |
| 5 | `web-server.md` | Routes, auth, threading, schema | database |
| 6 – Guidance | `customization-guide.md` | Extension points per subsystem, `src/custom/*.inc`, `SkillFactoryCustom`, import dirs, script vs C++ decision guide, worked examples | all |
| 6 | `glossary.md` | Terms: AID/CID/GID, SC, bl, sd/md/nd, ud, ers, mapreg, WoE, MVP, renewal, PACKETVER… | all |
| 6 | `fork-notes.md` | Divergence from upstream (`my-server`), merge hazards | phase 1 |

## 2. Progress checklist

A subsystem is only ticked **complete** when its data structures, main execution paths, dependencies and extension points are documented and cross-checked (depth D3 in [subsystems.md](subsystems.md)).

- [x] Repository discovery & structure
- [x] Architecture overview (topology, lifecycle, loop, packet routing)
- [x] Subsystem inventory
- [x] Initial dependency map
- [x] Startup & initialization (detailed)
- [x] Threading & concurrency
- [x] Memory management
- [x] Networking
- [x] Packet system
- [x] Database architecture
- [ ] Scripting engine
- [ ] Map & movement
- [ ] NPC system
- [ ] Character system
- [ ] Status system
- [ ] Skill system
- [ ] Combat system
- [ ] Item system
- [ ] Monster system
- [ ] Social systems
- [ ] Economy systems
- [ ] Companions
- [ ] Content systems
- [ ] Inter-server
- [ ] Login & char server internals
- [ ] Web server
- [ ] Customization guide
- [ ] Glossary
- [ ] Fork notes

## 3. Areas requiring deeper investigation

These are open questions found during discovery — each will be resolved (or explicitly marked unknown) in the phase noted.

### Core / infrastructure
1. ✅ *Resolved (threading-and-concurrency.md §2.1)*: **Timer internals** — heap structure, `TIMER_REMOVE_HEAP`, what happens when a timer func deletes its own owner; how `gettick()` caching interacts with long frames. *(phase 2)*
2. ✅ *Resolved (memory-management.md §4)*: **Deferred deletion** — `map_freeblock_lock/unlock` and how `block_list` objects are safely freed during `map_foreach*` iteration. Critical for anyone writing new combat code. *(phase 2)*
3. ✅ *Resolved (database-architecture.md §4, startup §7)*: **`TypesafeCachedYamlDatabase`** cache mechanics and reload safety (`database.hpp:150-200`): are pointers held across `@reloaditemdb` invalidated? *(phase 2)*
4. **MINICORE** exact boundary — which common modules tools get. *(phase 2)*
5. ✅ *Resolved (networking.md §1)*: **`SEND_SHORTLIST` / `SOCKET_EPOLL`** — which are enabled by default in each build system. *(phase 2)*

### Networking & packets
6. ✅ *Resolved (networking.md §6)*: Full **client handshake** with exact packet IDs per `PACKETVER 20220406`, including pincode (disabled in this fork via `char_athena.conf`) and the char→map redirect. *(phase 2)*
7. ✅ *Resolved (packet-system.md §2-4)*: **Packet versioning** — how `clif_packetdb.hpp` (#if chains), `packets_struct.hpp`, `packets.hpp` (typed structs) and `clif_shuffle.hpp` combine; which coding style new packets should follow. *(phase 2)*
8. ✅ *Resolved (packet-system.md §5: keys are 0 → inactive)*: **Obfuscation keys** for this client date (`clif_obfuscation.hpp`) and whether obfuscation is active. *(phase 2)*
9. **Map-server change** across servers (`chrif_changemapserver`) and multi-map-server deployments. *(phase 5)*

### Data & persistence
10. ✅ *Resolved (database-architecture.md §6)*: **Save cadence & consistency** — autosave interval, `save_settings` flags, what is saved on which event, crash-loss window. *(phase 2)*
11. ✅ *Resolved (database-architecture.md §3: one FK, mostly MyISAM)*: **Schema relationships** across the 56 `main.sql` tables (no foreign keys are expected ⚠️). *(phase 2)*
12. Which tables map writes **directly** (mapreg, vending, buyingstore, market, barter, logs) and multi-map safety. *(phase 2/5)*

### Scripting
13. Script **bytecode format** and parser stages; how `RERUNLINE`/sleep/`menu` suspend and resume `script_state`. *(phase 3)*
14. Variable scopes (`@`, `$`, `$@`, `.`, `.@`, `'`, `#`, `##`) → storage location (memory, mapreg, char/acc registry). *(phase 3)*
15. `script_constants.hpp` export mechanism and `db/const.yml`. *(phase 3)*

### Gameplay
16. **SkillImpl migration status** — which skills still use legacy switches; how `calculateSkillRatio` interacts with `battle_calc_skillratio`. *(phase 4)*
17. `status_calc_pc_` full pipeline order (base → job → equipment → item scripts → cards → SC), and `status_calc_bl_` dirty-flag (`SCB_*`) mechanism. *(phase 4)*
18. Renewal-specific code paths: ✅ there are 334 `#ifdef RENEWAL` occurrences across `src/map/*.cpp`, not counting `#ifndef`/`#if defined` variants. Which mechanics they switch still needs to be catalogued. *(phase 4)*
19. Mob AI: lazy vs hard AI split, mob skill conditions (`mob_skill_db.txt` still TXT). *(phase 4)*
20. Remaining **legacy TXT databases** (`skill_damage_db`, `produce_db`, `skill_nocast_db`, `item_noequip`, `job_noenter_map`, `mob_skill_db`, `status_disabled`, `skill_changematerial_db`) — loader locations. *(phase 4)*

### New questions raised in phase 2
24. Whether char's final-save ack (`0x2b21`) always arrives before map's `do_final_chrif` closes the link at shutdown. *(phase 5)*
25. MySQL reconnect behaviour, given that `Sql_P_KeepaliveTimer` is disabled (`sql.cpp:217`). *(phase 5)*
26. A TCP-fragmented client packet causes a disconnect (`clif_parse` and `PacketDatabase::handle` call `set_eof` on short reads). Is this a real-world issue? *(phase 5)*
27. `TypesafeCachedYamlDatabase::find` checks `key < cache.size()` but indexes with `calculateCacheKey(key)`. Audit the DBs that override the key transform. *(phase 4)*
28. Scripts paused across logout (`script_state` lifetime). *(phase 3)*

### Fork-specific
21. Impact of `itemdb_isidentified` always returning `1` on other code paths (e.g. appraisal items, identify skill, NPC checks). *(fork notes)*
22. `PACKETVER` define placed outside the include guard in `src/custom/defines_pre.hpp` — harmless today but worth normalising. *(fork notes)*
23. ✅ *Partly resolved (memory-management.md §1: the int16 index type is inherited from upstream)*: The changes in `src/common/malloc.cpp` (refactor) — confirm behaviour parity with upstream. *(fork notes)*
