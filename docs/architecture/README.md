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

## Index

| Document | Status |
|---|---|
| [project-overview.md](project-overview.md) | ✅ Phase 1 |
| [repository-structure.md](repository-structure.md) | ✅ Phase 1 |
| [server-architecture.md](server-architecture.md) | ✅ Phase 1 (overview level) |
| [subsystems.md](subsystems.md) | ✅ Phase 1 (inventory) |
| [dependency-map.md](dependency-map.md) | ✅ Phase 1 (initial) |
| [roadmap.md](roadmap.md) | ✅ Phase 1 |
| startup-and-initialization.md | ⏳ planned |
| networking.md / packet-system.md | ⏳ planned |
| threading-and-concurrency.md / memory-management.md | ⏳ planned |
| database-architecture.md | ⏳ planned |
| scripting-engine.md | ⏳ planned |
| gameplay/*.md | ⏳ planned |
| customization-guide.md / glossary.md / fork-notes.md | ⏳ planned |

## Key facts at a glance

- There are four server processes: **login**, **char**, **map** and **web**. Each one subclasses `rathena::server_core::Core` (`src/common/core.hpp:59`).
- Login, char and map are **single-threaded reactors**. Each loop iteration runs due timers, then select/epoll on the sockets, then parses packets. Only the web-server uses threads.
- The char-server sits at the **centre of a star**. Map-servers relay all shared or persistent state through it, using `chrif` for control and `intif` for inter-server traffic.
- Static game data is stored in **YAML** and loaded through `TypesafeYamlDatabase` subclasses. Mutable state lives in **MySQL**.
- The client protocol version is fixed **at compile time** (`PACKETVER 20220406` in `src/custom/defines_pre.hpp`). This fork builds in Renewal mode.
- Skills are being moved from large `switch` blocks to **per-skill classes** in `src/map/skills/` (`SkillImpl`).
- The official source customisation points are `src/custom/*.inc`, `src/map/skills/custom/`, `conf/import/` and `db/import/`.

## Other references in the repository

`doc/script_commands.txt`, `doc/packet_client.txt`, `doc/packet_interserv.txt`, `doc/item_bonus.txt`, `doc/status_change.txt`, `doc/mapflags.txt`, `doc/permissions.txt` and `doc/yaml/` are upstream references. This knowledge base links to them rather than repeating them.
