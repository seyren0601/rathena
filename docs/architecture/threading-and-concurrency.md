# Threading and Concurrency

> Status: **Phase 2**. ✅ = verified in source · ⚠️ = inferred · ❓ = unknown.

## 1. The model in one sentence

✅ Login, char and map each run **one thread**, a cooperative event loop. All game logic, network I/O, timers and **synchronous SQL** run on it, one after another. Only the web-server uses extra threads.

Evidence: grepping for `std::thread|pthread_|std::mutex|std::atomic` across `src/` finds hits only in `src/web/web.cpp` and `src/web/sqllock.*`. The ERS header also states "The system is not thread-safe at the moment" (`src/common/ers.hpp`).

## 2. The main loop

✅ `Core::start` (`src/common/core.cpp:388-392`):

```cpp
while( this->get_status() == e_core_status::RUNNING ){
    t_tick next = do_timer( gettick_nocache() );
    this->handle_main( next );          // Core default → do_sockets( next )
}
```

Login, char and map do **not** override `handle_main`, so they use the default `do_sockets`. The web-server overrides it to just sleep (`web.cpp:528-530`).

### 2.1 `do_timer` — the scheduler

✅ `src/common/timer.cpp`:

- **Storage.** `timer_data[]` is a growable array of `TimerData { tick, func, type, interval, id, data }`. Free slots are reused through `free_timer_list`. A timer id (`tid`) is an index into that array.
- **Ordering.** A binary min-heap of tids, keyed on `tick` (`BHEAP_VAR(int32, timer_heap)`, `timer.cpp:38`).
- **`add_timer(tick, func, id, data)`** creates a one-shot timer (`TIMER_ONCE_AUTODEL`). **`add_timer_interval(..., interval)`** creates a repeating one (`TIMER_INTERVAL`).
- **`do_timer(now)`** (`timer.cpp:~295`):
  1. While the top of the heap has `tick <= now`: pop it and mark it `TIMER_REMOVE_HEAP`.
  2. Call `func(tid, tick, id, data)`. If the timer is more than 1 s late, `now` is passed instead of the scheduled tick.
  3. If the callback left the timer alone: a one-shot slot is freed, and an interval timer is re-pushed at `tick + interval`. If it is more than 1 s late, it is re-pushed at `now + interval`, so missed runs are **not** caught up.
  4. Returns the time until the next timer, clamped to **[20 ms, 1000 ms]** (`TIMER_MIN_INTERVAL`, `TIMER_MAX_INTERVAL`, `timer.cpp:20-21`). That value becomes the `select`/`epoll` timeout.
- **`delete_timer(tid, func)`** checks that `func` matches, logging `"function mismatch"` if not. It then **nulls `func`** and leaves the heap entry in place to be discarded lazily. So deleting a timer is cheap, and a deleted timer never fires.
- **`settick_timer` / `addtick_timer`** reschedule a timer by doing a linear search of the heap, then pop-at-index and push. This is O(n).
- **`gettick()`.** Without `TICK_CACHE` (no define was found in `src/`), `gettick()` reads the clock on every call. It uses `rdtsc` when that is enabled, otherwise the OS monotonic clock (`timer.cpp:84-160`).

Timer callback contract (`TIMER_FUNC(x)` = `int32 x(int32 tid, t_tick tick, int32 id, intptr_t data)`):

- `id` is usually a `block_list` id. **Always re-resolve it** (`map_id2bl(id)`, `map_id2sd(id)`) and check for `nullptr`, because the object may be gone.
- `data` is an opaque `intptr_t`. Pointers stored in it can dangle; game code prefers to store ids.
- Objects that own a pending timer keep its tid (for example `ud->walktimer`, `ud->attacktimer`, `sce->timer`) and must `delete_timer` it in their teardown path. A timer firing against a freed owner is a classic source of crashes.
- Register every callback with `add_timer_func_list(fn, "name")`, so error messages can name it (`search_timer_func_list`).

### 2.2 `do_sockets` — I/O reactor

See [networking.md](networking.md#3-the-io-cycle-do_sockets) for the full phase breakdown. In short: flush writes, wait (`select`/`epoll`) for up to `next` ms, `recv` into the FIFOs, flush writes again and close EOF sessions, then call `func_parse` on **every** session.

### 2.3 Timeline of one iteration

```mermaid
gantt
    dateFormat X
    axisFormat %L
    section Main thread
    do_timer: expired timers (AI, walk, SC, autosave…) :a, 0, 8
    flush writes                  :b, 8, 9
    select/epoll wait ≤ next ms   :c, 9, 29
    recv → RFIFO                  :d, 29, 31
    flush + close EOF             :e, 31, 32
    func_parse all fds (packet handlers, SQL…) :f, 32, 45
```

Every box runs on the same thread. A slow SQL query or heavy `map_foreach` call inside **any** handler or timer delays everything that comes after it, for every player on that process.

## 3. What concurrency hazards still exist?

With a single thread there are no data races, but there is **re-entrancy**: a function can indirectly cause an object it is still working on to be freed.

| Hazard | Mechanism | Mitigation in code |
|---|---|---|
| A unit dies or is removed in the middle of an area loop | A skill hits N targets inside `map_foreachinrange`, one target's death triggers a script that removes another target | ✅ `map_foreachinrangeV` first copies the matching objects into the global `bl_list[]`, then holds a `FreeBlockLock` and skips entries whose `prev == nullptr` (`map.cpp`, see [memory-management.md](memory-management.md#4-deferred-object-destruction)) |
| Object freed while further up the call stack | `skill_attack → battle → status_damage → mob_dead → unit_free` | `map_freeblock_lock()` / `map_freeblock_unlock()` around the risky section. Freed blocks are queued until the lock count drops to 0 |
| Timer fires for a gone object | Pending timer with a stale `id` | Re-resolve the id inside the callback; delete owned timers during teardown |
| Script pauses (dialog, `sleep`) | `script_state` survives across packets/timers while the player may log out | ❓ to analyse in scripting-engine.md (phase 3) |
| Inter-server async replies | A request is sent to char, the reply arrives in a later iteration; the player may have left | Handlers look the player up again by `account_id`/`char_id` (e.g. `chrif_authok`, `intif_parse_*`) ⚠️ pattern, not individually verified |

## 4. The web-server exception

✅ `src/web/web.cpp`:

- `http_server->listen()` runs on `std::thread svr_thr` (`web.cpp:92`, `:503`). cpp-httplib then serves requests on its own worker pool (⚠️ library default).
- Route handlers (`*_controller.cpp`) therefore run **concurrently** and off the main thread.
- `SQLLock` (`src/web/sqllock.hpp`) wraps a `std::unique_lock<std::mutex>` per DB category (`LOGIN_SQL_LOCK`, `CHAR_SQL_LOCK`, `MAP_SQL_LOCK`, `WEB_SQL_LOCK`). Controllers must take the matching lock before using a `Sql*` handle, because MySQL handles are not thread-safe.
- `Show*` logging from worker threads is ⚠️ not proven thread-safe.

## 5. Scaling implications

- One map-server process can only use **one CPU core** for game logic. To scale horizontally, split the maps across several map-servers (each lists its own maps in `conf/maps_athena.conf`). Char routes each player to the server that holds their map (`chclif_send_map_data`, `char_clif.cpp:902`).
- Monster AI is timer-driven. Monsters with no players nearby use a cheaper "lazy" path (`mob_ai_lazy`, `mob.cpp:2460`) than active ones (`mob_ai_hard`, `mob.cpp:2468`). Details in phase 4.
- Expensive one-off work, such as big SQL reads at login, runs inline. Data that has to come from char is requested asynchronously over `intif` (for example, storage and inventory are requested in `pc_reg_received`).

## 6. Rules for contributors

1. Never block: no `sleep`, no synchronous outbound network calls, no unbounded loops in handlers.
2. Keep SQL queries in game code small. Prefer char-server round trips (intif) for persistent data, which is the established pattern.
3. Use timers for anything delayed or periodic. Store ids, not pointers, in timer `data`.
4. Wrap code that can destroy objects while iterating in `map_freeblock_lock/unlock`, or use the RAII `FreeBlockLock`.
5. Don't add threads to login, char or map. None of the shared structures (`session[]`, `DBMap`, ERS, the timer heap, `bl_list`) are synchronised.
