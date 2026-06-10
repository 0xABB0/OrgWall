# vat — integrating the control-inversion probe

Scope: bring the externally probed main-loop substrate (see `probe/main_design/README.md` for the
architecture it validates) into melody as a new leaf module `vat`, reusing existing modules instead
of the probe's hand-rolled stand-ins. Replacement of `app`, `executor`, `reactor` (and migration of
`port`, `io`) is a later wave, gated on this module proving friction-free and benchmark-competitive.

## What the probe validated

A Chromium-lineage decomposition: platform wait primitive behind a vtbl (kqueue / Cocoa / IOCP /
embedder-guest), a work-source merge (cross-thread mailbox ∪ due timers ∪ ready queue), a driver
running one turn at a time with re-entrant nested runs and innermost-quit semantics, a wait/poll
policy fed by pending-work info and pacing summary (frame pacing as policy input, not special case),
and same-thread-cheap / cross-thread-waking post rules. Smoke-tested on macOS (kqueue, Cocoa,
vsync), web (emscripten guest pump), with shard-per-core echo servers.

Probe faults the integration corrects, by design not patching:

- Source construction bypassed the wait-primitive vtbl (free functions blind-casting the pump);
  `main_design`'s source contract (`wakeables`, `deadline`, `drain(budget)`, `cancel`) routes
  everything through the vat, and readiness/completion is the Waiter's concern, not the source's.
- Allocation per posted work item (malloc-node queues); melody's intrusive `Mel_Task` +
  `Mel_Mpsc` make posting zero-alloc, never-fail.
- No allocator seam; every `mel_vat_*` constructor takes `const Mel_Alloc*` (MEL-CODE-003).
- Single anonymous FIFO; the driver vtbl owns ordering/budget among ready sources (fair / unfair
  drivers), and ready work is drained under the same budget discipline.

## Reuse map (probe stand-in → melody module)

| probe | melody |
|---|---|
| ml_sync.h (mutex/cond/thread-id) | thread |
| ml_clock | time (`mel_nanos_since_unspecified_epoch`, ns everywhere) |
| ml_event_loop_proxy (locked list) | collection `Mel_Mpsc` + doorbell |
| ml_work / ml_task_queue | executor `Mel_Task` (intrusive, armed-coalesced) |
| ml_executor | executor `Mel_Executor` waist; `mel_vat_executor(v)` is the
  "reactor executor" the executor readme names as owed |
| ml_wake_up_queue | shipped timer source over collection heap |
| ml_pool / ml_scheduler | job module (fiber-backed pool, `mel_job_executor`) |
| ml_arena (atomic) | allocator `Mel_Arena` (per-vat, single-thread) |
| ml_async senders | not ported: future + channel + fiber/continuation cover composition; the
  stop-token may return later as a cancellation primitive |
| ml_run / entries | later wave: `app` module shims spawn a root vat |

## Module surface (wave 1)

Namespace `<vat/...>`, prefix `mel_vat_`. Vocabulary per `probe/main_design`.

    Mel_Vat*      mel_vat_open(const Mel_Alloc*, Mel_Vat_Desc);   // .waiter, .driver: explicit, no silent default
    void          mel_vat_close(Mel_Vat*);
    void          mel_vat_run(Mel_Vat*);                          // turns until nothing retains the vat or quit
    bool          mel_vat_step(Mel_Vat*);                         // one turn, for hosts owning the wait
    void          mel_vat_quit(Mel_Vat*);                         // innermost-run stop
    void          mel_vat_post(Mel_Vat*, Mel_Task*);              // any thread; cross-thread rings the doorbell
    Mel_Executor* mel_vat_executor(Mel_Vat*);
    bool          mel_vat_is_owner(const Mel_Vat*);

Source: vtbl of exactly four entries; a source contributes a wakeable set (possibly empty) and a
deadline (`0` / `t` / `MEL_VAT_NEVER`), and is drained with a budget, reporting `more`.

    struct Mel_Vat_Wakeable { i64 handle; u32 events; u32 revents; };
    struct Mel_Vat_Source_Vtbl {
        void (*wakeables)(Mel_Vat_Source*, Mel_Vat_Wakeable**, usize*);
        i64  (*deadline)(Mel_Vat_Source*);
        bool (*drain)(Mel_Vat_Source*, u32 budget);
        void (*cancel)(Mel_Vat_Source*);
    };
    Mel_Vat_Source* mel_vat_source_open(Mel_Vat*, const Mel_Vat_Source_Vtbl*, void* state);
    void            mel_vat_source_close(Mel_Vat_Source*);
    void            mel_vat_source_demand_changed(Mel_Vat_Source*);

Waiter (sovereign wait primitive) and driver (turn policy) are vtbls handed to the vat at open —
implementations are objects, never flags (MEL-CODE-001 honored structurally: no mode enums; the
sovereign/subordinate split is which open path runs, fair/unfair is which driver object you pass).

Wave 1 ships: kqueue waiter (darwin host), fair driver, timer source, mailbox/doorbell, executor
adapter. Wave 2: epoll + io_uring waiter, IOCP waiter, guest bridge (wasm/CFRunLoop/ALooper),
unfair driver, vsync source. Wave 3: `port` backend rides the waiter; `app` entry shims spawn the
root vat; `reactor` consumers migrate (shim `Mel_Reactor_Source` over a vat source if a gradual
path is wanted, else coordinated rename).

## Invariants (from main_design, pinned by tests)

- Demand published before doorbell ring; driver reads demands only after draining.
- Drain snapshots the source set; open/close from inside a drain defers to the turn boundary.
- No lost wakeup across drain→block: readiness rechecked before sleeping.
- Doorbell coalesces; mailbox drained to empty each wake; surplus ring costs one empty wake.
- min-deadline reduction: any `0` ⇒ poll; finite min ⇒ timed; all `NEVER` ⇒ block indefinitely.
- Quit stops the innermost run only; nested runs save/restore quitting.

## Benchmarks (acceptance gates, not decoration)

Targets under `modules/vat/bench/`, run via `./nob test`. Compare:

1. turn overhead, idle source — vat vs raw kqueue loop vs incumbent `reactor` (its bench exists;
   same metric: ns/iter, throughput, % of 60/240/1000 Hz frame).
2. cross-thread post throughput + wake latency — vat vs libuv `uv_async_send` (third-party via
   `mel_cmake`) vs incumbent `mel_reactor_post`.
3. timer churn — N pending timers, insert/cancel/fire — vat vs libuv timers.
4. TCP echo, 1 shard and N shards — vat + listener source vs libuv echo vs raw kqueue echo;
   p50/p99 latency and req/s under pipelined load.

Competitive bar: within noise of raw platform primitive on (1); ≥ libuv on (2)–(4). Linux gates
(epoll/io_uring, vs seastar-class echo) attach to wave 2.

## App migration discipline (wave: applications)

Modules between an app and the loop are decoupled from the loop *type*, not retargeted to a new
one: a module that only delivers callbacks takes `Mel_Executor*` (camera); a module that only
needs "stop the app" takes a two-pointer host callback (window's `Mel_Window_Host`). Such
modules then serve reactor-hosted and vat-hosted apps alike during the migration, and end up
depending on neither loop. Apps own the loop explicitly: open waiter + driver + vat, retain
while work is in flight, run, close. The waiter selectors `mel_vat_waiter_ui` / `_io` pick the
platform implementation; the *class* of waiter stays the caller's explicit choice.

Async discipline for apps: a module future is consumed only from a `mel_future_then`
continuation delivered on `mel_vat_executor(vat)`; reading `mel_future_value`-backed helpers on
a possibly-pending future is the bug class this migration eliminates (barcode-reader read
`auth`/`status` inline, which misreads a pending authorization as denial). Stage progression is
the task's `run` pointer — no state enum.

## Suspension bridging (fiber ⇄ stackless), from the second probe

An external probe (`test-suspension`) demonstrates the unifier: one scheduler-facing parkable
shape — resume → done/ready/blocked — under which stackful fibers and stackless coroutines park
on the *same* waitlists and wake each other. Melody already owns the waist (`Mel_Task` +
`Mel_Waker`; channel ops already carry a `Mel_Waker`), so the bridge is thin glue, not new
machinery. Shipped as `modules/await` (`<await/await.h>`, prefix `mel_await_`):

- `mel_await_coro_start`: drives a coro frame as a `Mel_Task` on an explicit executor; each
  resume yields a `Mel_Await_Step` naming what it waits on (future `then`, channel op via the
  channel's future flavor, vat timer, cooperative reschedule) and the adapter registers the
  task with it. A vat-bound coro holds `mel_vat_retain` across its live span (the fs pattern).
- `mel_await_future`: a fiber awaits anything future-completable by parking on `Mel_Signal`
  (already how the channel's fiber flavor works); the resolve path sets the signal via a
  then-task on the inline executor.

Tests (`await-bridge`) recreate the probe's scenarios on melody primitives: the bidirectional
fiber ⇄ coro channel bridge, fiber-signalled token acquisition, and timer-paced vat retention.
Owed (module readme): cancellation propagation, vat-affine coro spawn sugar.

Naming (approved, done): `fiber` kept (the stackful mechanism); `continuation` → `coro` (it
*is* the stackless coroutine; full symbol sweep `mel_cont_` → `mel_coro_`, includes
`<coro/...>`); `coroutine` → `routine` (`mel_routine_*`, includes `<routine/...>` — it is a
fiber-pool scheduler for frame-cadenced routines, wait(ms)/yield(updates), not a coroutine
primitive; `script` stays reserved for the language-embedding module: lua/js/mono/java);
`await` is one verb whose mechanism (fiber park, coro yield, blocking wait) is the callee's
affair, per `main_design`.

## Replacement ledger

Terminal state: `modules/reactor` and `modules/app` are DELETED — zero consumers remained
(audited: no build.c deps, no includes, no symbols). Every application runs on boot + vat.
Owed beyond this point: epoll/io_uring waiter, IOCP waiter (un-stubs win32 port), ALooper/UIKit
bridges, web lifecycle provider, the libuv/seastar benchmark gates, executor-offload for
regular-file io.


- `reactor` — replaced by `vat`; consumers migrating (window ✓ decoupled, camera ✓ decoupled,
  audio ✓ decoupled (dead parameter deleted), port ✓ migrated, gui ✓ migrated (retains its
  vat; xcb flush rides the deadline query), hello-window ✓, barcode-reader ✓,
  hello-world-gui ✓, barcode-gui ✓, display-gui ✓, midi-monitor ✓, hello-audio ✓,
  vibration ✓ + hello-vibration ✓ (tick pause/re-arm), io ✓ rewritten + fs ✓ + storage ✓
  (honest regular-file caps; offload owed), process ✓ migrated (pipes ride port's vat
  sources; exit reap is a deadline-only vat source), gpu ✓ migrated + hello-gpu ✓ (device
  opts carry the vat; futures deliver on `mel_vat_executor`; the completion pump is a
  deadline-paced vat source idle at `NEVER` with no pollers; render pacing is a
  CVDisplayLink vsync vat source on macOS — `vat/vsync.h`, signaled-flag + doorbell ring,
  deadline 0 when signaled — with an hz tick fallback elsewhere), dialog ✓ migrated (init and
  opts carry the vat; owner affinity is `mel_vat_is_owner`; deliver must match
  `mel_vat_executor`), shell ✓ decoupled (dead loop parameter deleted; depends on neither
  loop), clipboard ✓ migrated (watch poll is a `Mel_Vat_Tick`; x11/wayland fd sources ride
  the vat, flush rides the deadline query), hid ✓ (port-fallback read poll is a deadline-0
  vat source on the port's vat), melody-showcase ✓ (boot entry, both modes; smoke is a
  posted-task chain that quits the root vat and sets the exit code; process stdout rides a
  regular-file redirect because the cocoa ui waiter refuses fd wakeables — port sources
  cannot ride the macos root vat until the waiter grows fd bridging); pending:
  camera-scanner (in flight)).
- `app` — replaced by the vat entry module (in flight): framework owns `main` on every
  platform; `mel_app_setup(Mel_Vat* root)` registers and returns; apps NEVER define main.
- `executor` — the waist (`Mel_Task`/`Mel_Executor`/`Mel_Waker`) survives as the substrate
  contract; the module's loop-coupled duties (loop executor, deferred posting) live in vat.
- `port` — migrated to vat sources (`mel_port_create(.vat = v)`); win32 backend temporarily
  `unavailable` pending the IOCP waiter, where completions become native rather than
  event-handle shims.
- `io` — rewritten with the vat abstraction in mind (in flight): port-backed sockets/pipes,
  honest caps for regular files, executor offload owed.
- `coroutine` — renamed to `routine` ✓ (full sweep: `mel_coro_*` → `mel_routine_*`,
  `<routine/...>`; no consumers existed).
- `continuation` — renamed to `coro` ✓ (full sweep including the codegen tool and goldens:
  `mel_cont_*` → `mel_coro_*`, `Mel_Cont_*` → `Mel_Coro_*`, `<coro/...>`, tool `coro-gen`,
  tests `coro-test-*`, fixtures/goldens `*.coro.h`; no symbol-prefix split was needed).
- `await` — new ✓: the suspension waist (`mel_await_coro_start`, `mel_await_future`),
  tests `await-bridge`; cancellation propagation and vat-affine spawn sugar owed.

## Open decisions (gabbo)

- Module name `vat` (recommended: it is `main_design`'s own vocabulary) vs `loop`/`pump`.
- Phase-3 consumer migration: compat shim vs coordinated rename of ~26 modules' call sites.
- Whether the probe's sender/receiver layer ever lands, or future+fiber+continuation is the
  blessed composition surface (recommended: the latter; revisit only with a concrete need).
- Chase–Lev work-stealing executor from the probe vs job module as the one pool (recommended:
  keep job; bench if doubt arises).
