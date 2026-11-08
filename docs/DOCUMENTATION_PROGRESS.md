# Documentation Progress (deep dives)

Overview-level docs are in [architecture/](architecture/README.md). This file tracks the source-level deep dives under [subsystems/](subsystems/).

| Subsystem | Document | Date | Status | Open questions |
|---|---|---|---|---|
| Character progression (skill points, jobs, quest/char variables, equipment) + job-record feasibility | [subsystems/map/character-progression-and-job-records.md](subsystems/map/character-progression-and-job-records.md) | 2026-11-07 | ✅ Done | §9 answered 2026-11-08 (see design doc); 5 technical unknowns (§11) |
| Job Record System (design + as built) | [subsystems/map/job-record-system-design.md](subsystems/map/job-record-system-design.md) | 2026-11-08 | 🔨 Implemented on branch `job-record-implementation` (phases 1–10), builds, **not tested in game**. §10 = as built and differences from the design | ❓ Everything in the test checklist, including the Doram hair client test and the char-server crash tests (§9.2). Not done: no expiry countdown for deletion mails (§2.4), `CostDoram` typo (§9.1) |
| Job Record System: test checklist | [subsystems/map/job-record-test-checklist.md](subsystems/map/job-record-test-checklist.md) | 2026-11-08 | 📋 Not run yet | — |
