# Job Record System: test checklist

> For branch `job-record-implementation`. Design and as-built notes: [job-record-system-design.md](job-record-system-design.md) (§10).
> Status on 2026-11-08: **nothing below has been run yet.** Tick each box as it passes and note the date and anything odd.

## 0. Setup

- [ ] `sql-files/upgrades/upgrade_20261108.sql` applied. The char-server starts without a `char_checkdb` error.
- [ ] Char-server and map-server rebuilt. The map-server log shows `db/job_record_vars.yml` loading without warnings.
- [ ] The client shows item 900000 with its name and sprite (needs the client item info entry).
- [ ] A test account with GM access to `@jobrecord`, `@jobrecordinfo`, `@item`, `@jobchange`, `@baselvl`, `@joblvl`, `@kick`.
- [ ] Database access for checks: `char_job_record*` tables, `inventory`, `mail`, `mail_attachments`.

Useful query after each step (replace `<cid>`):

```sql
SELECT * FROM char_job_record_state WHERE char_id = <cid>;
SELECT record_id, name, class, base_level, job_level, equip_count FROM char_job_record WHERE char_id = <cid>;
SELECT record_id, nameid, equip, unique_id FROM char_job_record_item WHERE char_id = <cid>;
SELECT nameid, equip, unique_id FROM inventory WHERE char_id = <cid> AND equip <> 0;
```

## 1. Book and menu

- [ ] A new character gets the Job Record Book at its first login. An existing character gets it at its next login. A second copy is never given.
- [ ] The book weighs 0, can't be dropped, traded, sold, carted, stored or mailed, and isn't used up.
- [ ] With no record yet, the menu shows the current build and says it'll be saved as "Default".
- [ ] `getjobrecords` returns −1 right after login, if you're quick enough: the menu says records are loading.

## 2. Create

- [ ] Knight 99/50 with gear → create Human "Spear Knight". The old build becomes "Default" with Knight 99/50 and its gear. The character is a Novice at base 1 / job 1, stats 1, no skills, nothing equipped, empty hotkey bar.
- [ ] Create Doram from a human. The character is a Summoner with hair 1 and clothes colour 0; the hair colour is kept if it's 0–8. The guild window shows the new hair.
- [ ] Create Human from a Doram: hair 1, clothes colour 0.
- [ ] A transcendent or baby character creates a record: plain Novice, no extra status points.
- [ ] Names: empty or 24+ characters refused; a duplicate name refused; a name with `:` works and shows with a space in the menu.
- [ ] `job_record_max: 20` → the 21st create is refused. With `job_record_max: 0` more than 20 can be created, and the menu pages 10 per page (Next/Previous).

## 3. Switch

- [ ] Switching back to "Default" restores the class, base and job level, exp, stats and points, skills (including 2nd/3rd/4th job skills that depend on the change levels), hotkeys, look, and equipment in the same slots, costumes and shadow gear included.
- [ ] The record left behind keeps everything it had (switch back and forth twice).
- [ ] Refine, cards, random options, enchant grade, bound flag and expiry of the gear survive a round trip.
- [ ] A rental that expired while stashed is gone after the switch, not equipped.
- [ ] Same class on both sides (Novice record → Novice record) still swaps stats, gear and progress.
- [ ] 175 → 99 switch (4th job → 2nd job): the base level is the stored one, not truncated, and no status points are given for levels.
- [ ] Party and guild windows show the new job and level.
- [ ] Switching to the active record is refused.

## 4. Weight and slots

- [ ] Predicted weight ≥ 70% → the menu shows the warning, and the switch is still possible.
- [ ] Predicted weight > 100% → refused ("You would be too heavy…").
- [ ] Too few free inventory slots for the target's gear → refused.
- [ ] `@jobrecord preview <id>` numbers match the weight shown after the switch (it can be slightly higher: item weight bonuses aren't counted).

## 5. Job-quest progress

- [ ] Do part of an official job quest (e.g. Knight: talk to the first NPC). Create a new record: the quest starts from the beginning, and the quest log has no job quest entries.
- [ ] Switch back: the earlier progress and quest-log entries (including kill counts and timers) are back.
- [ ] Non-job progress (e.g. `MISC_QUEST`, cooking, sailing) is the same on every record.
- [ ] Star Gladiator feel/hate maps and Taekwon mission are per record.
- [ ] Job Master rebirth (`lastJob`) works on a record that rebirths.
- [ ] `@jobrecord vars` lists the record's job variables; `@jobrecord reload` reloads the YAML without a restart.

## 6. Guards and cooldown

- [ ] Within 10 s of attacking, casting or being hit: create/switch/delete refused ("during combat").
- [ ] Refused on a WoE castle, a GvG map, a battleground map and inside any instance. Allowed in a town.
- [ ] Refused while dead, vending, trading, in a chat room, with storage open.
- [ ] After a create, switch or delete, a second one is refused for 30 s, also after a relog. Viewing and renaming still work.
- [ ] Two clicks in quick succession don't start two actions ("already in progress").

## 7. Rename

- [ ] Rename the active record and an inactive one; the new name is shown at once and after a relog.

## 8. Delete

- [ ] The active record can't be deleted.
- [ ] Delete a record with 12 pieces of gear → 3 mails from "Job Record", titled "Equipment of <name>", holding 5 + 5 + 2 items with refine, cards and options intact.
- [ ] The mails are still there after `mail_return_days` / `mail_delete_days` have passed (shorten them in `conf/char_athena.conf` to test). Note whether the client shows an expiry countdown (known cosmetic issue, design §10.2).
- [ ] An item of the deleted record that's already in the inventory, cart or a storage (same `unique_id`, edit the database to fake it) is not mailed again.
- [ ] All `char_job_record_*` rows of that record are gone; the other records are untouched.
- [ ] Deleting the character removes all its job-record rows.

## 9. Job Master gifts

- [ ] Change to a job with a gift: it's given once and `JM_GEAR_<id>` is set.
- [ ] New record, same job again → "You already received …", no second copy.
- [ ] Green Apple Ring, Makeover Brush, Paint Brush and Hourglass Necklace can be put in storage.
- [ ] Shared gifts (Book 1550, Survivor's Rod 1617, Main Gauche 1208) are only given to the first job that gets them (expected, design §10.7).

## 10. Char-server failures (design §9.2)

For each case, compare `inventory` and `char_job_record_item` by `unique_id` afterwards: **no item may be missing; a duplicate is acceptable only if it's removed at the next load.**

- [ ] **(a) Before the action:** stop the char-server, try to switch → "character server is unavailable".
- [ ] **(b) During loading:** pause or kill the char-server right after starting a switch → after 10 s "did not answer", state back to idle, nothing changed.
- [ ] **(c) After the switch:** kill the char-server right after the switch finishes, keep playing, restart it → the next autosave (or relog) sends everything; `@jobrecordinfo` shows the queue emptying.
- [ ] **(d) Logout while down:** with the char-server down after a switch, `@kick` the player, then restart the char-server → the final save is resent with the job-record writes.
- [ ] **(e) Interrupted switch at login:** set `pending_to` by hand to an inactive record whose stored data matches the live character → at login the switch is completed. With data that doesn't match → rolled back to the source record, and the target's gear is not in the inventory.
- [ ] **(f) Map-server crash** right after a switch, before any save → the character comes back on the old record with the old gear.
- [ ] **(g) Delete while the char-server dies:** the transaction either deletes the record and sends all mails, or does neither.

## 11. Client checks still open (design §9.1)

- [ ] A Doram with hair 11, 25 and 42 and clothes colour 4 and 7 (set with `@hairstyle` / `@dye`): how does the 2022-04-06 client draw it?
