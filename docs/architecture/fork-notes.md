# Fork Notes (divergence from upstream rAthena)

> Status: **Phase 6**. ✅ = verified in source / git · ⚠️ = inferred · ❓ = unknown.
> Branches: `master` tracks upstream rAthena. `my-server` is the customised server, **24 commits** ahead of `master` (merge-base `7f080871c`). `claude-documentation` = `my-server` + the `docs/architecture/` commits only (no source changes).
> Command used: `git diff --stat master...my-server` → 26 files, +602 / −494.

## 1. Commit timeline (`git log master..my-server`)

| Date | Commit | Label | Summary |
|---|---|---|---|
| 2026-07-19 | `5a5e015a8` | ref | Refactor `src/common/malloc.cpp` for readability |
| 2026-07-19 | `281b029c2` | conf | Drop and exp rates |
| 2026-07-19 | `d42b24b02` | conf | `127.0.0.1` → `localhost` in `inter_athena.conf` (Linux) |
| 2026-07-19 | `c25620ac8` | conf | Disable pincode at character select |
| 2026-07-19 | `584f73b35` | conf | Test items in the cash shop |
| 2026-07-19 | `49609ca51` | conf | `PACKETVER 20220406` to match the client |
| 2026-07-19 | `9050e486b` | conf | Enable utility NPCs, add buffs to healer |
| 2026-07-19 | `90c7628b0` | feat | Endower NPC in Prontera |
| 2026-07-25 | `8ecdb832d` | feat | Failed refine only downgrades |
| 2026-07-25 | `9cd4155ae` | feat | Items drop identified |
| 2026-07-25 | `5af172aae` | feat | Green Apple Ring equippable by all jobs (Star Emperor fix) |
| 2026-07-25 | `cb63a375b` | conf | Jobmaster gives job item; script header on endower |
| 2026-07-30 | `16f575374` | feat | Fix `setlook`/outfit: add `*_2nd` job keys to `job_stats.yml` |
| 2026-08-08 | `749da1359` | conf | Boss drop rate 30× |
| 2026-08-08 | `23c0ef98f` | conf | `@item`, `@points`, `@cash` for group 1 |
| 2026-08-08 | `0f4fce128` | feat | Dragon Eye Ring + combo |
| 2026-08-08 | `16ae6babe` | feat | Dress Changer NPC |
| 2026-08-08 | `d290fe705` | feat | Kafra in Prontera centre |
| 2026-08-08 | `22befddf2` | conf | Remove level penalty (`level_penalty.yml` emptied) |
| 2026-08-08 | `55fd9d01c` | conf | Adjust downgrade threshold (refine) |
| 2026-08-08 | `143906645` | feat | Card remover never fails |
| 2026-08-08 | `ff372470f` | feat | Content rewards ×10 (quests) |
| 2026-08-09 | `7999329c9` | fix | Centre the Prontera Kafra save point |
| 2026-08-09 | `1691e1bd1` | feat | Remove the base-level restriction in academy quests |

## 2. Source code changes (rebuild required)

| File | Change | Effect | Doc |
|---|---|---|---|
| `src/custom/defines_pre.hpp` | `#define PACKETVER 20220406` | Client protocol version (2022-04-06 client). Changes packet layouts and `#if PACKETVER` features across `clif.cpp` | [packet-system.md](packet-system.md) |
| `src/config/core.hpp` | `#define ENABLE_CASHSHOP_PREVIEW_PATCH` | Enables cash-shop item preview packets | [gameplay/item-system.md](gameplay/item-system.md) |
| `src/map/itemdb.cpp` (`itemdb_isidentified`) | `switch` on weapon/armor/pet armor/shadow gear → `return 1` | **Every created item is identified**: drops, `getitem`, mob loot, pet loot. The Identify skill, magnifiers and `OnPCIdentifyEvent` become effectively unused | [gameplay/item-system.md §1](gameplay/item-system.md) |
| `src/common/malloc.cpp` | Readability refactor (70 lines) | Intended to keep the same behaviour. The int16 index type was inherited from upstream, not introduced here | [memory-management.md](memory-management.md) |

⚠️ **`PACKETVER` placement**: in `src/custom/defines_pre.hpp` the `#define` sits **after** the closing `#endif` of the include guard ✅. It's harmless (an identical redefinition), but it should move inside the guard (roadmap item 22).

## 3. Configuration changes

| File | Key(s) | Upstream → fork | Notes |
|---|---|---|---|
| `conf/battle/exp.conf` | `base_exp_rate`, `job_exp_rate`, `mvp_exp_rate`, `quest_exp_rate` | 100 → **7000** (70×) | Applied when the mob DB loads ([monster-system.md](gameplay/monster-system.md)) |
| `conf/battle/exp.conf` | `multi_level_up` | no → yes | Needed at 70× |
| `conf/battle/drops.conf` | `item_rate_{common,heal,use,equip,card}` | 100 → **5000** (50×) normal; **3000** (30×) boss and MVP | Plus `item_rate_mvp`, `item_rate_adddrop`, `item_group_rate`, `item_rate_treasure` → 3000 |
| `conf/battle/drops.conf` | `item_auto_get` | no → yes | Drops go straight to the inventory |
| `conf/char_athena.conf` | `pincode_enabled` | yes → **no** | [login-and-char-servers.md](login-and-char-servers.md) |
| `conf/inter_athena.conf` | `*_server_ip`, `*_db_ip` | `127.0.0.1` → `localhost` | On Linux MySQL clients, `localhost` means the Unix socket ⚠️ |
| `conf/groups.yml` | group **1 "Super Player"** `Commands` | + `cash`, `points`, `item` | Accounts with `group_id = 1` can spawn items and cash. Group 0 (default) is unchanged |

## 4. Database changes

| File | Change |
|---|---|
| `db/re/refine.yml` | 118 `DowngradeAmount: 1` added; `BreakingRate: 10000` (112) and `BreakingRate: 2000` (50) removed; `DowngradeAmount: 3` removed. **Failed refines downgrade by 1 and never break** |
| `db/re/level_penalty.yml` | Body removed (22 lines). **No level-gap penalty** on exp or drops (Renewal) |
| `db/re/item_db_equip.yml` | New **Dragon Eye Ring** (Id 490690, Rune Knight, right accessory). Green Apple Ring job list → `All: true` |
| `db/re/item_combos.yml` | Dragon Eye Ring + `YSF01_Ring` combo (+15% MaxHP, long-range attack and size damage; at armor refine ≥ 14, an `autobonus` with rate 30 = **3%** per weapon hit (1000 = 100%, `doc/script_commands.txt:6471`) grants +15 POW/CON and +150 STR/DEX for 10 s) |
| `db/re/job_stats.yml` | 13 `*_2nd` job keys (`Rune_Knight_2nd`, `Warlock_2nd`, …) added to the 3rd-job entries. Needed because `job_outfits.yml` alternate-outfit keys must exist in `job_stats.yml` (commit message `16f575374`, which notes a TODO to apply it to all classes) |
| `db/item_cash.yml` | +54 lines of test items in the cash shop |

## 5. NPC and script changes

| File | Change |
|---|---|
| `npc/scripts_custom.conf` | Enables `warper`, `jobmaster`, `platinum_skills`, `healer`, `breeder`, `stylist`, `resetnpc`, `card_remover`. Adds `endower.txt`, `change_dress.txt` |
| `npc/custom/change_dress.txt` | **New**: Dress Changer (alternate job outfits) |
| `npc/custom/endower.txt` | **New**: element endow service in Prontera |
| `npc/custom/healer.txt` | Extra buffs |
| `npc/custom/card_remover.txt` | Failure path removed |
| `npc/custom/jobmaster.txt` | Gives the job item |
| `npc/kafras/kafras.txt` | New Kafra `kaf_prontera` at `prontera,159,190` saving at `155,184` |
| `npc/re/jobs/novice/academy.txt` | Removes the `BaseLevel < 15` requirement |
| `npc/re/quests/quests_16_1.txt`, `quests_illusion_dungeons.txt`, `quests_mora.txt` | Reward amounts ×10 (for example `getitem 6919,10` → `100`) |

## 6. Merge hazards (when pulling upstream into `my-server`)

1. **Base files edited instead of `import/`**: `drops.conf`, `exp.conf`, `groups.yml`, `refine.yml`, `level_penalty.yml`, `job_stats.yml`, `item_db_equip.yml`, `item_combos.yml` and `item_cash.yml` are upstream-owned. Upstream changes them often, so expect conflicts.
   - `conf/import/` and `db/import/` are **gitignored** (`.gitignore:67-71`) and generated from `*-tmpl` at build (`CMakeLists.txt:516-518`). That's probably why the fork edits tracked files: so the settings are versioned.
   - Alternative: commit an override set elsewhere and copy it into `import/` at deploy time ⚠️.
2. **`refine.yml` was rewritten** (348 lines changed). Upstream refine changes (new materials, grades) will conflict almost line by line. Consider keeping the downgrade rule as a script or script-side patch, or re-applying it with a transform.
3. **`itemdb_isidentified`** is a 10-line local change in an upstream function. Usually an easy merge, but watch for upstream refactors of item identification.
4. **Encoding damage in `quests_16_1.txt`**: commit `ff372470f` also replaced the original non-UTF-8 bytes in 19 flavour-text lines with U+FFFD (`�`), for example `"Suspect: ����(Prisoner No.: 243AD265)"`. The text is lost in this file. Any upstream edit nearby will conflict. Fix by re-applying only the `getitem` changes on top of the upstream file, using an editor that keeps the original encoding.
5. **`PACKETVER`**: upstream periodically changes default packet versions and adds `#if PACKETVER` blocks. Re-test login, char select and the cash shop after each merge.
6. **Generated files**: `db/import`, `conf/import` and `save/` are local. Back them up before a clean checkout.

## 7. Behavioural consequences worth remembering

| Change | Consequence |
|---|---|
| 70× exp + `multi_level_up` | Quests that gate on level ranges complete quickly. Academy level gates were removed for this reason |
| 50×/30× drops + `item_auto_get` | Inventory weight and slots fill quickly. Vending, storage and economy inflation |
| Empty `level_penalty.yml` | Low-level characters farm high-level mobs at full rate |
| No refine breaking | Refine materials are the only sink. +20 is reachable with enough materials |
| All items identified | Identification items and NPCs are obsolete |
| Group 1 can `@item`/`@cash` | Make sure regular players stay in group 0 |

## 8. Security items found during documentation (not fork-specific)

These come from upstream defaults but matter for this deployment:

| Item | Where | Doc |
|---|---|---|
| Passwords stored in plaintext (`use_MD5_passwords: no`) | `conf/login_athena.conf:118` | [login-and-char-servers.md](login-and-char-servers.md) |
| Web `*_load` routes skip the token check | `src/web/*_controller.cpp` | [web-server.md §7](web-server.md#7-security-and-robustness-notes) |
| Web server has no TLS | `src/web/web.cpp` | [web-server.md](web-server.md) |
| Pincode disabled (fork) | `conf/char_athena.conf` | — |

## 9. Job Record System (branch `job-record-implementation`, not merged yet)

A feature branch on top of `my-server`: one character can keep several builds ("records") and switch between them with a bound item. Design and as-built reference: [../subsystems/map/job-record-system-design.md](../subsystems/map/job-record-system-design.md) §10. Test plan: [../subsystems/map/job-record-test-checklist.md](../subsystems/map/job-record-test-checklist.md). **Untested in game.**

- **Needs:** the upgrade SQL `sql-files/upgrades/upgrade_20261108.sql`, a rebuild of char-server and map-server, and a client item info entry for item 900000.
- **New files:** `src/char/int_jobrecord.*`, `src/map/jobrecord.*`, `src/common/job_record.hpp`, `conf/battle/job_record.conf`, `db/job_record_vars.yml`, `db/job_record_item_db.yml`, `npc/custom/job_record.txt`.
- **Hooks in upstream files** (each marked `// [Job Record]`): `pc.hpp`, `pc.cpp` (`pc_reg_received`), `chrif.cpp` (`chrif_save`, two calls), `intif.cpp` (packet length row 0x38B0 and dispatch), `inter.cpp` (length row 0x30B0 and dispatch), `char.cpp`/`char.hpp` (table names, column check, character delete), `map.cpp`, `script.cpp`, `atcommand.cpp`, `sql-files/main.sql`, `conf/inter_athena.conf`, `conf/battle_athena.conf`, `db/item_db.yml`, the `.vcxproj` files.
- **Merge hazards:** upstream may claim inter-server packets 0x30B0–0x30B5 or 0x38B0–0x38B5 (check `doc/packet_interserv.txt` and the length tables after each merge). `chrif_save` changes upstream would need the two hooks placed again in the same order (before the inventory save, after the status save). `npc/custom/jobmaster.txt` now gives its gifts through `Give_Job_Equip_Once`, so upstream Job Master updates conflict in `Get_Job_Equip`.
- **Behaviour:** Job Master gifts are given once per character (`JM_GEAR_<id>`), and Green Apple Ring 2795, Makeover Brush 6121, Paint Brush 6122 and Hourglass Necklace 490087 become storable.
