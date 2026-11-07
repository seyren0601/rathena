# rAthena Architecture Knowledge Base

Technical documentation of this rAthena checkout (branch `my-server`), written by reading the source. It's meant to help a developer who is new to the codebase find their way around, trace execution paths and change things safely.

## Conventions

- **✅ Verified**: confirmed in source, with a `file:line` reference.
- **⚠️ Inferred**: a reasonable deduction that hasn't been traced yet.
- **❓ Unknown**: not yet investigated.
- Paths are relative to the repository root. Line numbers are for this checkout and will drift as the code changes.
- Diagrams use Mermaid.

## Start here

1. [project-overview.md](project-overview.md): what the executables are, the tech stack, build systems, compile-time switches and how this fork differs from upstream.
2. [repository-structure.md](repository-structure.md): what each directory and important file does.
3. [server-architecture.md](server-architecture.md): process topology, the `Core` lifecycle, the event loop, packet routing, the client lifecycle and the map-server object model.
4. [subsystems.md](subsystems.md): a catalogue of every subsystem found, with anchor symbols, data files and how deeply each has been investigated.
5. [dependency-map.md](dependency-map.md): how the servers and modules depend on each other, and which server owns which data.
6. [roadmap.md](roadmap.md): the planned documents, a progress checklist and open questions.

Task-oriented entry points:
- **Changing or extending the server?** Start with [customization-guide.md](customization-guide.md). It picks the right layer (conf → db → script → `src/custom` → core) and has checklists.
- **Merging upstream or auditing this fork?** Read [fork-notes.md](fork-notes.md) (every fork change, merge hazards, security items).
- **Unknown abbreviation (`sd`, `SCB`, `BCT`, `GID`…)?** See [glossary.md](glossary.md).

The phase 2 core documents, best read in this order: [startup-and-initialization](startup-and-initialization.md) → [threading-and-concurrency](threading-and-concurrency.md) → [memory-management](memory-management.md) → [networking](networking.md) → [packet-system](packet-system.md) → [database-architecture](database-architecture.md).

## Index

| Document | Status |
|---|---|
| [project-overview.md](project-overview.md) | ✅ Phase 1 |
| [repository-structure.md](repository-structure.md) | ✅ Phase 1 |
| [server-architecture.md](server-architecture.md) | ✅ Phase 1 (overview level) |
| [subsystems.md](subsystems.md) | ✅ Phase 1 (inventory) |
| [dependency-map.md](dependency-map.md) | ✅ Phase 1 (initial) |
| [roadmap.md](roadmap.md) | ✅ Phase 1 |
| [startup-and-initialization.md](startup-and-initialization.md) | ✅ Phase 2 |
| [threading-and-concurrency.md](threading-and-concurrency.md) | ✅ Phase 2 |
| [memory-management.md](memory-management.md) | ✅ Phase 2 |
| [networking.md](networking.md) | ✅ Phase 2 |
| [packet-system.md](packet-system.md) | ✅ Phase 2 |
| [database-architecture.md](database-architecture.md) | ✅ Phase 2 |
| [scripting-engine.md](scripting-engine.md) | ✅ Phase 3 |
| [gameplay/npc-system.md](gameplay/npc-system.md) | ✅ Phase 3 |
| [gameplay/map-and-movement.md](gameplay/map-and-movement.md) | ✅ Phase 3 |
| [gameplay/character-system.md](gameplay/character-system.md) | ✅ Phase 4 |
| [gameplay/status-system.md](gameplay/status-system.md) | ✅ Phase 4 |
| [gameplay/skill-system.md](gameplay/skill-system.md) | ✅ Phase 4 |
| [gameplay/combat-system.md](gameplay/combat-system.md) | ✅ Phase 4 |
| [gameplay/item-system.md](gameplay/item-system.md) | ✅ Phase 4 |
| [gameplay/monster-system.md](gameplay/monster-system.md) | ✅ Phase 4 |
| [gameplay/social-systems.md](gameplay/social-systems.md) | ✅ Phase 5 |
| [gameplay/economy-systems.md](gameplay/economy-systems.md) | ✅ Phase 5 |
| [gameplay/companions.md](gameplay/companions.md) | ✅ Phase 5 |
| [gameplay/content-systems.md](gameplay/content-systems.md) | ✅ Phase 5 |
| [inter-server.md](inter-server.md) | ✅ Phase 5 |
| [login-and-char-servers.md](login-and-char-servers.md) | ✅ Phase 5 |
| [web-server.md](web-server.md) | ✅ Phase 5 |
| [customization-guide.md](customization-guide.md) | ✅ Phase 6 |
| [glossary.md](glossary.md) | ✅ Phase 6 |
| [fork-notes.md](fork-notes.md) | ✅ Phase 6 |
| [../subsystems/map/character-progression-and-job-records.md](../subsystems/map/character-progression-and-job-records.md) | ✅ Deep dive (see [../DOCUMENTATION_PROGRESS.md](../DOCUMENTATION_PROGRESS.md)) |
| [../subsystems/map/job-record-system-design.md](../subsystems/map/job-record-system-design.md) | 📝 Design proposal |

## Key facts at a glance

- There are four server processes: **login**, **char**, **map** and **web**. Each one subclasses `rathena::server_core::Core` (`src/common/core.hpp:59`).
- Login, char and map are **single-threaded reactors**. Each loop iteration runs due timers, then select/epoll on the sockets, then parses packets. Only the web-server uses threads.
- The char-server sits at the **centre of a star**. Map-servers relay all shared or persistent state through it, using `chrif` for control and `intif` for inter-server traffic.
- Static game data is stored in **YAML** and loaded through `TypesafeYamlDatabase` subclasses. Mutable state lives in **MySQL**.
- The client protocol version is fixed **at compile time** (`PACKETVER 20220406` in `src/custom/defines_pre.hpp`). This fork builds in Renewal mode.
- NPC scripts compile to bytecode for a stack VM in `script.cpp`. Dialogs pause the VM in `sd->st` and resume it when the client replies. Builtin commands live in `buildin_func[]`, and custom ones go in `src/custom/script*.inc`.
- Skills are being moved from large `switch` blocks to **per-skill classes** in `src/map/skills/` (`SkillImpl`).
- The official source customisation points are `src/custom/*.inc`, `src/map/skills/custom/`, `conf/import/` and `db/import/`.

## Other references in the repository

`doc/script_commands.txt`, `doc/packet_client.txt`, `doc/packet_interserv.txt`, `doc/item_bonus.txt`, `doc/status_change.txt`, `doc/mapflags.txt`, `doc/permissions.txt` and `doc/yaml/` are upstream references. This knowledge base links to them rather than repeating them.
