# Documentation Progress (deep dives)

Overview-level docs are in [architecture/](architecture/README.md). This file tracks the source-level deep dives under [subsystems/](subsystems/).

| Subsystem | Document | Date | Status | Open questions |
|---|---|---|---|---|
| Character progression (skill points, jobs, quest/char variables, equipment) + job-record feasibility | [subsystems/map/character-progression-and-job-records.md](subsystems/map/character-progression-and-job-records.md) | 2026-11-07 | ✅ Done | §9 answered 2026-11-08 (see design doc); 5 technical unknowns (§11) |
| Job Record System (design, not implemented) | [subsystems/map/job-record-system-design.md](subsystems/map/job-record-system-design.md) | 2026-11-08 | 📝 Proposal (all decisions made, branch `feature/job-record`) | None blocking. §9 (2026-11-08): server side of Doram hair and char-server disconnect traced; revised write protocol. ❓ still needs a client test (Doram hair 11–42, clothes colour 4–7) and a crash test during implementation |
