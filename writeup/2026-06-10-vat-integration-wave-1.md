# vat integration — wave 1

## Work done

- Wrote `design/vat-integration.md`: the plan for landing the externally probed main-loop
  substrate (the `probe/main_design` architecture, rehearsed outside the repo under Chromium-
  flavored names) as a candidate replacement for `reactor` and the loop half of `app`. Contains
  the probe→melody reuse map, the wave plan, the benchmark acceptance gates, and the open
  decisions reserved for Gabbo.
- Created `modules/vat` as a leaf module — nothing depends on it; `app`/`executor`/`reactor`
  are untouched. Surface: `Mel_Vat` (one thread as control domain), the four-entry source
  contract (`wakeables`/`deadline`/`drain(budget)`/`cancel`), Waiter and Driver as vtbl objects
  (kqueue waiter, fair driver), a min-heap timers source, and `mel_vat_executor()` — the
  loop-bound executor the executor module's readme names as owed. Work items are the intrusive
  `Mel_Task` waist; cross-thread posting is `Mel_Mpsc` + a coalescing kqueue `EVFILT_USER`
  doorbell guarded by a seq-cst parked flag (no syscall while the vat is awake).
- Tests (`vat-core`, 9 passing): FIFO + armed coalescing, deadline-ordered timers, parked
  cross-thread wake, nested run with innermost-quit, executor adapter, fd readiness drain, and
  two spy-waiter tests pinning the min-deadline reduction through the Waiter seam (`-1` block
  with no deadlines; timed wait ≤ nearest deadline).
- Bench (`vat-bench`, release): turn overhead over a raw zero-timeout `kevent` loop is
  ~4–16 ns/turn (~0.1%, occasionally within noise of zero); cross-thread post ~4 M tasks/s
  after the parked-flag change (~2.2 M/s before it).

## Kludges

- `demand_changed` re-arms but never diffs: a source's wakeable-set membership must be stable
  for its life. Set-diffing is deferred to the epoll/IOCP wave. Documented in the module readme.
- Source/vat teardown ordering is contractual, not enforced: closing the vat before a
  state-owning source (e.g. timers) leaks that source's state; a teardown hook is owed.
- Timer entries have no cancellation handle; an added timer fires or dies with its source.
- The kqueue waiter's `KQ_BATCH 256` is a per-wait dequeue batch, not a capacity — overflow
  is re-reported next wait — but it is still a literal that should become a waiter option.
- The bench's "raw kqueue" baseline measures a zero-timeout `kevent` at ~12 µs under default
  QoS; the absolute number is QoS-bound and only the vat-minus-raw delta is meaningful. No
  affinity/QoS pinning yet (the incumbent reactor bench raises QoS; vat-bench should too).
- `vat-bench` is declared `mel_add_executable` and gets packaged as a `.app` like any target;
  harmless, but a bench-flavored target kind may be worth having.
- libuv/incumbent-reactor comparative benches from the design spec are not yet wired; wave-1
  numbers cover only raw-primitive overhead and cross-post throughput.

## Work done — wave 2 (same session): first apps on the vat

- vat additions: `mel_vat_retain/release` (explicit retention per `main_design`), a parked-flag
  seq-cst doorbell elision (cross-post 2.2 → ~4 M tasks/s), `mel_vat_waiter_cocoa` (AppKit
  `nextEvent` pump ported from the probe, `src/macos/`), and `mel_vat_waiter_ui`/`_io`
  selectors.
- `window` module decoupled from every loop: `mel_window_init(Mel_Window_Host)` (a quit
  callback + user), dead `Mel_Window_Icc_Opt.reactor` field removed, `reactor` dependency
  dropped; gui's cocoa backend passes a reactor-quit shim, vat apps pass `mel_vat_quit`.
  `window-state` test updated and passing.
- `camera` module decoupled: `mel_camera_init(const Mel_Alloc*, Mel_Executor* deliver)`;
  `reactor` dependency dropped; camera tests passing; camera-scanner call site updated
  (passes `mel_reactor_executor(reactor)`, still reactor-hosted).
- `hello-window` rewritten on the vat (cocoa waiter): builds, opens, pumps, survives, dies
  clean on signal.
- `barcode-reader` rewritten on the vat with genuine asynchrony: the old code read
  `mel_camera_future_auth/status` on possibly-pending futures inline in `app_init` (works only
  when the platform resolves synchronously; misreads a pending TCC authorization as denial).
  Now every camera future is consumed in a `mel_future_then` continuation on
  `mel_vat_executor`; stage = the task's `run` pointer. Still-image path verified; streaming
  path builds (not exercised headless — TCC prompt).

## Kludges — wave 2

- The cocoa waiter ignores wakeables (`arm` returns false): fd sources on a UI vat need the
  CFFileDescriptor bridge or a kqueue-fed secondary; owed with the subordinate-vat wave.
- The cocoa waiter's blocking wait dispatches at most one NSEvent per turn (poll mode drains
  all); event-storm behavior untested.
- barcode-reader's streaming path was not run end-to-end (camera permission prompt in a
  headless session); the still path and build are verified.
- `mel_window_keepalive_dec` on a vat app quits via host callback — `mel_vat_quit` from the
  window-close callback runs on the loop thread, fine; but no test pins last-window-quit.

## Work done — wave 3 (same session): port on the vat

- modules/port migrated from reactor to vat: `Mel_Port_Opt.vat`, `mel_port_vat()`, ownership
  asserts via `mel_vat_is_owner`, op records carry a `Mel_Vat_Wakeable` and ride a four-entry
  vat source (deadline NULL, drain = the readiness step); posix and apple backends converted;
  `reactor` dependency dropped. All 17 `port-loop` tests pass on the vat, including
  continuation-on-loop-thread-next-turn, cancel races, destroy-with-pending, peer-close
  SIGPIPE survival, and the no-busy-spin timing gate (validates vat timed waits actually
  sleep).
- The four port test harnesses (test_port + three tsan drivers) converted from
  reactor-spawn/idle-source scaffolding to vat run-helpers (deadline-0 source wrapping the old
  idle body). The tsan mains previously destroyed the port after `mel_reactor_spawn` had
  already torn the reactor down — the owner assert dereferenced freed memory; the vat
  conversion keeps the loop alive until after `mel_port_destroy`.

## Kludges — wave 3

- win32 port backend replaced with the `unavailable` stub: the old backend polled OVERLAPPED
  event handles through the reactor's wait set, and the vat has no win32 waiter yet to poll
  them with. Restored natively by the IOCP waiter wave. Until then win32 port ops resolve
  `ERROR | UNAVAILABLE` (loud, not silent), and modules/io's win32 path was already sync-only.
- modules/io, fs, storage are broken against the new port surface pending the io rewrite
  (delegated, in flight this session).
- tsan_* drivers are not declared in port's build.c (pre-existing); converted blind, compile
  not verified by nob.

## Work done — wave 4 (same session): boot, the framework-owned entry

- New leaf module `modules/boot`: the framework owns the platform `main`; the application owns
  `void mel_app_setup(Mel_Vat* root)` — registration only, returns, never blocks, never runs
  the loop. Surface: `mel_app_argc/argv` (stored before setup), `mel_app_set_exit_code`, and
  `mel_app_on_exit(fn, user)` — a dynamic array of teardown hooks fired LIFO after the run
  ends, before the vat closes. `modules/app` untouched (gui apps still ride it).
- macos entry (sovereign): opens the root vat over `mel_vat_waiter_ui` +
  `mel_vat_driver_fair(alloc, 64)` on the heap allocator, setup, `mel_vat_run`, exit hooks,
  teardown, returns the stored exit code. A setup that registers nothing yields a run that
  returns immediately — retention-based exit is the CLI story, verified by barcode-reader's
  still path.
- web entry (subordinate), ported from the external probe's `ml_pump_guest`/`ml_entry_web`:
  `mel_vat_waiter_guest(alloc, Mel_Vat_Embedder*)` added to vat — `wait` never blocks, it
  forwards the reduced timeout to the embedder (`schedule_work` at `0`,
  `schedule_delayed_work` otherwise, negative = wake-only-on-ring); `ring` is
  `schedule_work`. The boot web TU arms emscripten callbacks (`emscripten_async_call`,
  `emscripten_set_timeout`, pthread-proxied to the main runtime thread when needed), drives
  one `mel_vat_step` per host callback, redrives immediately when a turn never reached the
  waiter, and returns from `main` via `emscripten_exit_with_live_runtime`; quit fires the
  exit hooks and `emscripten_force_exit`s with the stored code.
- Subordinate hosting forced a vat invariant fix: `parked` was true only inside `wait`, so a
  post landing while the host owned the CPU (guest vat between steps) never rang and stalled.
  Now the vat opens parked, the fair driver clears `parked` at turn entry and at every turn
  exit re-parks (seq-cst), re-drains the mailbox, and rings if ready work surfaced; owner-side
  posts and demand changes ring when the vat is at rest. To keep the sovereign cost of the
  extra rings at one syscall per wait cycle, the kqueue and cocoa rings now latch behind an
  atomic flag cleared at wait entry. All 9 vat-core tests still pass.
- `hello-window` and `barcode-reader` no longer define `main`: both implement `mel_app_setup`
  doing registration only and depend on boot. hello-window: window-host quit shim
  (`mel_vat_quit(root)`), create, retain, `mel_window_shutdown` as an exit hook.
  barcode-reader: still mode reads `mel_app_argc/argv` in setup, runs the decode, sets the
  exit code, returns without retaining; streaming mode retains, registers camera teardown as
  an exit hook, and keeps the `mel_future_then` stage-progression state machine verbatim.
- Verified: `./nob test vat-core` (9/9), macos builds of both apps, hello-window alive after
  3 s and killed clean, `barcode-reader assets/test.png` → "no barcode found", exit 2,
  `./nob build hello-window wasm` links (boot web entry + guest waiter compile into the
  wasm app). All touched files clang-formatted.

## Kludges — wave 4

- The wasm artifact was only linked, never executed in a browser; the web entry's drive
  protocol (step-per-callback, redrive-when-the-waiter-was-never-reached) is reasoned, not
  observed. Owed: run it headless.
- The guest waiter refuses `arm`, so any wakeable-bearing source on a guest vat fires the
  demand-changed assert — loud, but it means fd sources are unusable on web until the bridge
  wave.
- The boot web entry never cancels stale `emscripten_set_timeout` arms; a superseded deadline
  costs one empty drive when the old timer fires.
- The root vat on macos is always the cocoa/ui waiter, including for CLI apps: barcode-reader's
  streaming mode previously rode `mel_vat_waiter_io` (kqueue); now its camera completions ring
  the doorbell as a cross-thread `postEvent:` into NSApplication. Works, but a terminal app
  instantiating NSApplication is heavier than it needs to be; a waiter-selection seam in boot
  is owed. Streaming mode is still not exercised end-to-end (headless TCC prompt).
- Still mode now constructs the vat (and thus NSApplication) before setup decides it needs no
  loop; the old `main` short-circuited before opening anything.
- `app_fail` in barcode-reader now sets exit code 1 (the old vat `main` returned 0 on every
  streaming failure); a behavior change, deliberate but unrequested.
- The turn-end ring fires whenever ready work remains at relinquish, even under a sovereign
  `mel_vat_run` where the next turn would run it anyway; the waiter-side latch caps the cost
  at one syscall per wait cycle, but the unlatched guest embedder gets one `schedule_work`
  per backlogged turn (1:1 with drives, no flood).
- ios is skipped entirely this wave; owed in boot's readme, with `app`'s `ios_entry.m` as the
  porting shape. linux/win32/android entries equally absent — boot gates macos and wasm only.
- `boot` declares no test target; its only coverage is transitively through the two apps.

## CLAUDE.md suggestions

- None.

## Suggestions

- The executor module's relaxed `next` stores are documented as inline-only; vat's ready FIFO
  re-uses `Mel_Task.link` single-threaded, and the mailbox path goes through `Mel_Mpsc` proper,
  so the contract holds — worth keeping in mind for any future queued executor backend.
- `modules/reactor/bench/*.c` are not declared in reactor's `build.c`; wiring them would let
  the vat-vs-incumbent comparison run from `./nob` directly.

## Work done — wave 5 (same session): gui on the vat, five more apps

- `vat/tick.h` — `Mel_Vat_Tick`: a periodic source (deadline-paced, catch-up on overshoot,
  `set_interval`, fn-returns-false stops) replacing the reactor's timer sugar; without it every
  app port re-derived the same boilerplate.
- `gui` module migrated: `mel_gui_init(Mel_Vat*)`, internals on `mel_gui__vat`, cocoa quit shim
  → `mel_vat_quit`, xcb backend's prepare/check/dispatch source became a vat source — the
  pre-block X-connection flush moved into the `deadline()` query (the only pre-wait hook the
  vat contract offers; works because reduce queries deadlines immediately before blocking);
  androidnative quit converted; `reactor` dependency dropped. xcb's HIGH source priority was
  dropped — the vat has no source priorities (single round-robin drain).
- Retention semantics for GUI apps: under the reactor the loop ran until quit; under
  retention-based exit nothing held the vat and apps exited instantly. `mel_gui_init` now
  retains its vat; the existing last-frame-close → quit path provides the exit. Releases in
  `mel_gui_shutdown` when on the owner thread.
- `audio` module: the stored `Mel_Reactor*` was written once and never read — parameter
  deleted (`mel_audio_create(alloc, opt)`); test/bench call sites updated; `reactor` dep
  dropped.
- Apps ported to `mel_app_setup(Mel_Vat* root)` + boot: hello-world-gui, barcode-gui,
  display-gui, midi-monitor (all four smoke-run: alive, parked, killable), hello-audio
  (build-verified; not run — it emits sound immediately and the session is unattended).

## Kludges — wave 5

- gui retention is module-global (one retain per init), not per-frame; a gui app that never
  presents a frame parks forever unless something quits.
- xcb/androidnative conversions are textual: no Linux/Android host available this session.
  The xcb deadline-as-flush carries a side effect in a query hook — legal under the current
  contract (reduce runs once per turn, pre-wait) but worth a named pre-wait hook if more
  sources need it.
- The vat has no source priorities; the xcb source's former HIGH priority (input before
  timers) is unenforced. If input-vs-tick ordering bites, the fair driver needs an ordering
  notion or the design needs per-source weight.
- `Mel_Vat_Tick` fn-returns-false leaves the source open (stopped, deadline NEVER) rather than
  closing it — still retains the vat; explicit `mel_vat_tick_close` required.

## Not ported, with reasons (wave 5 survey)

- hello-vibration: the vibration module drives reactor timers with pause/resume semantics
  (`set_ready_time(NEVER)`, `timer_set_interval(remaining)`) — needs a tick pause/resume
  extension or a timers-source rework; small but not mechanical.
- hello-gpu: the gpu module carries the reactor in its public device opts, delivers gpu
  futures through it (`src/future.c`, 11 sites), and ships `mel_gpu_render_source_new` — a
  frame-pacing reactor source that properly belongs to the vat's owed vsync/pacing design
  (`ml_pump_cocoa`'s CVDisplayLink source in the probe is the reference). Deserves its own
  wave, not a textual swap.
- melody-showcase: window mode needs `mel_app_lifecycle_subscribe` (old app module; boot has
  no lifecycle yet) and `mel_process_run(.reactor = …)` (process module unmigrated); smoke
  mode needs io/fs/storage (rewrite in flight). Blocked on three fronts.
- camera-scanner, hello-window, barcode-reader: owned by the other agents this session.

## Work done — wave 4 (same session): io rewritten on the vat; fs + storage migrated

- modules/io **rewritten**, not patched, around the doctrine's source disciplines:
  - Surface: `Mel_Stream_Opt.vat`, `Mel_IO_File_Open_Opt.vat` replace `.reactor`;
    `mel_stream_vat()` replaces `mel_stream_reactor()`; a stream's delivery executor defaults to
    `mel_vat_executor(vat)`; vat-bound submission asserts `mel_vat_is_owner`.
  - Discipline split by what the opened handle *is*: `Mel_IO_File_Native.async_capable` renamed
    to `readiness` and set honestly from `fstat` — true only for fifo/socket/chardev, where the
    port proactor (readiness synthesized to completion on the vat waiter) is physical;
    **regular files report `caps.async = false`** and run the synchronous step inline, because
    kqueue readiness on a regular file is theater (always "ready", `read()` can still block on a
    cold page). Previously posix claimed `async_capable = true` unconditionally and rode regular
    files through the port — exactly the pretense the doctrine forbids.
  - Whole-file `load`/`save` redesigned: the turn-hopping Load_Drive/Save_Drive machinery (which
    only re-sliced a loop-thread-blocking pread across turns) is deleted; they resolve
    synchronously through the same future surface. The `.deliver` field and its
    `deliver_ok` validation are dropped (a do-nothing parameter is theater); `.vat` threads
    through for the offload wave.
  - The prescribed executor-offload path for regular files is **owed**, with the design recorded
    in the module readme and todo (worker syscall + `mel_vat_post` completion +
    retain/release bracketing, the same shape fs already ships).
- modules/fs migrated: `Mel_Fs_Opt.vat`, `mel_fs_vat()`; the worker-pool completion hop is now an
  intrusive `Mel_Task` embedded in the op record posted via `mel_vat_post` (zero-alloc, doorbell
  semantics) instead of `mel_reactor_post`'s closure; affinity asserts via `mel_vat_is_owner`.
  New behavior the reactor version lacked: in-flight ops `mel_vat_retain`/`mel_vat_release` the
  vat, so a vat whose only outstanding work is an fs op does not fall out of `mel_vat_run`
  before the completion lands (retention per `main_design`).
- modules/storage migrated: `Mel_Storage_Opt.vat` / `Mel_Storage_Fs_Opt.vat`,
  `mel_storage_vat()`, `mel_storage_job_vat()`; storage itself posts nothing cross-thread (it
  chains on fs futures over the vat executor), so it needed no retention of its own. The android
  asset backend (`title_android.c`) converted to the same embedded-task + retain/release shape.
- Tests converted from `mel_reactor_spawn` scaffolding to the proven port pattern: a deadline-0
  vat source wrapping the old idle body plus a run helper (io waiter + fair driver + vat,
  run, close). Net effect: the loop runs on the test thread itself, no spawned loop thread.
- build.c deps `reactor` → `vat` in io, fs, storage (lib + test targets).
- Gates: `./nob build io|fs|storage` clean; `io-stream` 11/11, `fs-core` 9/9,
  `storage-core` 7/7; `port-loop` 17/17 and `vat-core` 9/9 still green. clang-format run on
  every touched file.

## Kludges — wave 4

- Regular-file ops on a vat block the loop turn for the syscall's duration (honest
  `caps.async = false`, owed offload). This is no slower than before — the old port path also
  did the pread on the loop thread — it just stops claiming otherwise.
- The io test `load_on_vat_delivers_on_loop_executor` proves delivery lands through the vat
  executor on the loop thread and never inline, but since the loop runs on the test thread the
  thread-identity check is weaker than the old spawned-reactor version.
- `title_android.c` converted blind: android is not buildable in this session. It also keeps a
  pre-existing hazard untouched: completions posted before `asset_destroy` frees the backend
  dereference the freed backend if the vat drains them after destroy (same shape existed with
  `mel_reactor_post`).
- Quitting a vat with fs/storage completion tasks still armed leaks those op records
  (`mel_vat_close` does not run leftover ready tasks). Same leak existed with a quit reactor;
  retention now makes the clean path (don't quit early) actually reachable.
- `Mel_Stream_Opt.executor` silently defaults to `mel_vat_executor(vat)` when a vat is given;
  sanctioned as a derived default (the alternative is requiring both, which invites mismatch),
  documented in the readme.
- The fifo/chardev port path of file streams (readiness=true branch) has no dedicated test;
  it is exercised structurally by port-loop only. A fifo-stream test is worth adding.
- Out-of-gate consumers left broken on the old surface, as sanctioned for this wave:
  `modules/process` (pipe streams pass `.reactor` to `mel_stream_create`/`mel_port_create` —
  already broken by the port migration) and `apps/melody-showcase` (`smoke.c` creates
  fs/storage with `.reactor`, runs on `mel_reactor_spawn`). Both need the vat treatment;
  process's pipe streams are the natural first user of io's readiness branch.
- `modules/port/readme.md` still documents the reactor-era surface (`.reactor`,
  `mel_reactor_is_owner`); port was off-limits this wave, so the stale doc stands — flagged
  here instead of edited.

## CLAUDE.md suggestions — wave 4

- None.

## Suggestions — wave 4

- fs's worker pool and the owed io file-offload are the same machine; when io's offload lands,
  consider one shared blocking-syscall pool (or the job module) so the two modules stop carrying
  parallel worker plumbing.
- `mel_vat_close` could assert (or run-to-drop) armed-but-undrained ready tasks to make the
  quit-with-pending-completions leak loud (MEL-ENGINE-VIII).

## Work done — wave 7 (same session): vibration + hello-vibration; io rewrite consolidated

- `Mel_Vat_Tick` grew the two verbs vibration's completion choreography needs:
  `mel_vat_tick_pause` (deadline → NEVER) and `mel_vat_tick_set_interval` redefined as full
  re-arm (next = now + interval, unpauses) — its only prior caller assumed nothing finer.
- `vibration` migrated: `mel_vib_init(alloc, Mel_Vat*)`, `Mel_Vib_Play_Opt.vat`, per-playback
  one-shot completion timers are vat ticks (open/close/pause/re-arm at pause/resume/abort/
  device-lost); `reactor` dependency dropped. hello-vibration ported (setup-only, boot-hosted),
  builds, smoke-runs alive.
- io/fs/storage rewrite (delegated) landed in parallel: `.vat` surfaces, honest
  readiness-vs-regular-file discipline (regular files no longer claim `async_capable`),
  fs in-flight ops retain the vat, whole-file turn-hopping deleted; `io-stream` 11/11,
  `fs-core` 9/9, `storage-core` 7/7.

## Kludges — wave 7

- `mel_vat_tick_set_interval`'s re-arm semantics differ from the reactor's
  `timer_set_interval` (which adjusted a live cadence); acceptable for the two extant callers,
  but a future periodic consumer wanting drift-free cadence change will need a distinct verb.
- hello-vibration smoke-run proves loop+UI life, not actual haptics (no device assertion in a
  headless run).

## Work done — wave 8 (same session): lifecycle on boot

- `boot/lifecycle.h` + `src/lifecycle.c`: the app module's lifecycle machinery reborn under the
  framework-owned entry — no refcounted subsystem, no user-visible init/quit: boot brings the
  channel up before `mel_app_setup` and tears it down after the run. Same surface
  (`mel_app_lifecycle_poll/subscribe/unsubscribe`, `mel_app_active/foreground`,
  `mel_app__emit`), phases as #defines (the old header used an anonymous enum), delivery
  defaults to the root vat's executor, emit asserts `mel_vat_is_owner`.
- macOS provider ported (`src/macos/lifecycle.m`, NSNotificationCenter observers); web gets an
  honest empty platform hook pair pending the visibilitychange port. Verified: hello-world-gui
  rebuilds and runs with observers registered.

## Kludges — wave 8

- The provider registry (multi-provider array, generations) was dropped: boot has exactly one
  platform hook pair per platform TU. If tests need fake providers (the old app test did),
  the seam must return.
- Web lifecycle (visibilitychange/beforeunload) and the linux SIGTERM self-pipe provider are
  not ported; the WILL_TERMINATE ordering race documented in modules/app/todo.md transfers
  unsolved to whichever wave ports them.

## Work done — wave 9 (same session): process on the vat

- `modules/process` migrated off the reactor. Opts carry `Mel_Vat* vat` instead of
  `Mel_Reactor* reactor` (`Mel_Process_Spawn_Opt`, `Mel_Process_Run_Opt`); the forward decl in
  `process.h` is now `Mel_Vat`.
- Exit reap: the 2 ms reactor timer (`mel_reactor_timer_new` + `source_attach`) became a
  deadline-only vat source on `Mel_Process` itself (`REAP_VT`: `deadline` returns `reap_next`,
  `drain` runs `waitpid(WNOHANG)` and reschedules, mirroring `vat/tick.c`'s self-rescheduling
  shape but closing itself on reap — a paused tick would have retained the vat forever).
  The source is opened only while an async wait is pending and holds a
  `mel_vat_retain`/`release` pair (fs pattern); open/close is funneled through
  `reap_source_open/close` so cancel, `wait_sync`, and `destroy` all balance the retain.
- Pipes: `mel_process__pipe_stream` takes the vat; the backing `mel_port_create(.vat = v)`
  rides port's per-op four-entry vat sources, and the `Mel_Stream` is created with
  `.vat`/`mel_vat_executor`. Affinity asserts are `mel_vat_is_owner` throughout.
- `mel_process_run`: requires `.vat`, asserts ownership, retains the vat for the life of the
  run ctx (released in `run_finalize` after the future resolves), and every internal
  `mel_future_then`/`.deliver` defaults to `mel_vat_executor(vat)`.
- `build.c`: `reactor` dep dropped from lib and test; `vat` added to both, `time` added to the
  lib (the reap deadline reads `mel_nanos_since_unspecified_epoch`, the vat's own clock).
- Tests converted to the port-test run-helper: io waiter + fair driver + vat opened on the
  test thread, a deadline-0 source (`IDLE_VT` + `Idle_Body`) wrapping the old idle bodies,
  `mel_vat_quit` replacing `mel_reactor_quit`. Backends (posix/win32/wasm) untouched — they
  never saw the loop.
- Docs (`readme.md`, `spec.md`, `todo.md`) rewritten to the vat vocabulary; ledger entry in
  `design/vat-integration.md` flipped to `process ✓ migrated`.
- Verified: `./nob build process` clean; `process-spawn` 11/11; regressions `port-loop` 17/17,
  `io-stream` 11/11.

## Kludges — wave 9

- Exit detection is still a 2 ms `waitpid(WNOHANG)` poll, now as a vat deadline source — not
  kqueue `EVFILT_PROC`/`pidfd`. The old backend never used `EVFILT_PROC`, and the vat API gap
  is real: `Mel_Vat_Wakeable` is `{handle, events}` with events limited to IN/OUT/ERR/HUP, so
  a pid filter cannot be armed through the waiter today. Reported as an API gap (todo.md
  updated), not patched around.
- `Mel_Process_Wait_Opt.deliver` is still accepted and ignored (`(void)opt.deliver`),
  pre-existing; resolution delivery comes from the consumer's `mel_future_then` executor.
  port and fs store the same vestigial field; a coordinated decision (honor it or drop it)
  belongs to one sweep across the three modules.
- The reap source double-retains in spirit: an open vat source already retains the vat
  (`mel_vat__retained` walks sources), and the explicit `mel_vat_retain` adds a counter on
  top. Kept because the task pinned the fs pattern and it keeps the run-ctx (which has
  windows with no open source between continuations) and the wait path uniform.
- `Drain_Buf.chunk[4096]` in `process_run.c` (fixed staging array) and the win32 backend
  remain as they were — win32 is compile-gated on win-pilot and was not host-verified here;
  port's win32 backend is `unavailable` pending the IOCP waiter, so piped stdio on win32 is
  dead until that wave regardless.
- melody-showcase (`window_mode.c`, `smoke.c`) still passes `.reactor = g.reactor` and no
  longer compiles against this surface; out of scope by instruction. Its call sites must
  become `mel_process_run(..., .vat = g.vat, .deliver = g.exec, ...)` once the app owns a vat.

# camera-scanner GUI rewrite (separate agent, same day)

## Work done

apps/camera-scanner reinvented the GUI on its lone canvas instead of using the gui
module's components. Rewritten to the component idioms of hello-world-gui /
barcode-gui, preserving the camera/barcode pipeline and async structure unchanged
(mutex-protected triple-buffer frame swap, `mel_reactor_post` to hop to the GUI
thread, `mel_future_then` chaining for camera authorization, reactor-hosted
`mel_app_setup(Mel_Reactor*)` via modules/app — not migrated to vat; that is the
later wave).

What was hand-rolled, and what it became:

- Painted result card (`draw_result_card`: rounded rect, symbology badge, payload
  text, "Tap to copy" hint, all painter calls with hardcoded layout math) → a
  `mel_panel_create` with a column layout under the canvas, holding two
  `mel_label_create` (symbology / decoded payload) and a `mel_button_create`
  ("Copy to clipboard").
- Hand-rolled hit testing (`card_rect` capture during paint + point-in-rect check
  in `on_pointer_down`) → the button's `pointer.on_click`.
- Hand-rolled text eliding (`draw_elided` + `measure_chars` with a guessed
  0.55·font-size glyph width and manual UTF-8 boundary backoff) → deleted; native
  labels handle overflow.
- Painted status screens (`draw_state_message`, centered via strlen·9px width
  guesses) → `mel_gui_set_text` on the two labels; the canvas no longer renders
  text at all.
- "Copied" toast with its own `TOAST_DURATION_NS` repaint-driven timer → the copy
  button's text flips to "Copied" and resets on the next decode; no timer.
- Fixed-size `Decode_Payload` buffers (`char text[DECODE_TEXT_LIMIT]`,
  `char symbology[SYMBOLOGY_LIMIT]`, silent truncation — MEL-CODE-002 violation)
  → heap-owned `char*` payloads (`str8_dup_alloc` on the camera thread, ownership
  handed across the mutex, freed by the consumer). Symbology rides as the decoder's
  string literal. No truncation remains.
- App teardown moved from the canvas's `lifecycle.on_destroy` to the screen
  registration's `.on_destroy` (the navigation-aware hook).

The canvas keeps only what is honestly canvas work: letterboxed preview blit,
reticle + corner ticks + dim overlay, decode flash. Pointer callbacks on the
canvas are gone entirely.

Mid-rewrite, the vat wave landed `mel_gui_init(Mel_Vat*)` (was `Mel_Reactor*`);
camera-scanner, still reactor-hosted by design, now passes NULL — which gui.c
explicitly supports (`if (vat) mel_vat_retain(vat)`); the androidnative backend
already NULL-guards its `mel_vat_quit`. The cocoa backend did not
(`mel_gui__window_quit` dereferenced unconditionally); added the same guard there.

## Verification

- macos: `./nob build camera-scanner` clean.
- android: `./nob build camera-scanner android` clean; ran on the
  Medium_Phone_API_36.1 emulator (`./nob run camera-scanner android` +
  `pm grant android.permission.CAMERA`). Logs show mode selection and
  `streaming 720x480`; screenshot confirms preview + reticle on the canvas and
  native labels/button below. The decode→copy path was not exercised end-to-end
  (the emulator's virtual-scene QR was not reachable from the front camera the
  mode picker selects); decode/copy logic is structurally unchanged apart from
  payload ownership.
- The first `nob run` found a SIGTRAP in `mel_vat_retain` from passing the
  reactor where the freshly-migrated `mel_gui_init` expects a vat — that is what
  prompted the NULL hand-off above. Note the same latent trap exists in the other
  not-yet-migrated reactor apps (hello-gpu, melody-showcase).

## Kludges (MEL-ENGINE-VIII)

- `mel_gui_init(NULL)`: until camera-scanner's vat migration lands, the gui holds
  no vat, so last-frame-close cannot quit any loop. Irrelevant on android (the
  Activity owns teardown) but on macos closing the window leaves the reactor loop
  running. Transitional debt owed to the vat wave.
- The decode flash animation still relies on camera-frame arrival to drive
  repaints (no per-frame invalidation source of its own); at very low camera FPS
  the flash decay would stutter. Pre-existing behavior, knowingly kept.
- `PREVIEW_BUFFERS 3` remains a fixed array; it is the exact size of the
  produce/spare/consume rotation, not a MAX cap, so judged outside MEL-CODE-002.
- The result panel's `preferred_h = 132` and the labels' preferred heights are
  eyeballed logical sizes, same as every exemplar app; the gui module offers no
  intrinsic measurement yet.
- Emulator verification required cold-booting the AVD by hand
  (`-no-snapshot-load`); `nob run`'s own boot attempt timed out against a stale
  snapshot that never came online.

## CLAUDE.md suggestions

- None.

## Suggestions

- A row layout (only `column.h` exists) would let the symbology label and copy
  button share a line, matching the old card's badge arrangement.
- nob's android runner could surface `adb` lookup failure earlier (it currently
  prints `sh: adb: command not found` and then waits the full emulator timeout).

## Work done — gpu + hello-gpu on the vat (same session)

- `modules/gpu` is off the reactor. `Mel_Gpu_Device_Opt.reactor` / `Mel_Gpu_Device_Default_Opt.reactor`
  → `.vat` (`Mel_Vat*`), `mel_gpu_device_reactor` → `mel_gpu_device_vat`, every backend device
  struct (vk/mtl/d3d12/wgpu) carries `Mel_Vat* vat`. All ~11 `mel_gpu_future_create(dev->pump,
  dev->reactor)` sites now pass the vat; future continuations deliver on `mel_vat_executor(vat)`
  (inline executor when vat-less), the same waist `modules/port` settled on.
- The completion pump's 2 ms reactor timer became a first-class deadline vat source
  (`src/future.c`): `deadline()` reports `MEL_VAT_NEVER` while the poller list is empty and
  `next` (re-paced to `now + interval` per drain) otherwise — an idle device no longer costs
  500 wakeups/s (MEL-ENGINE-III); `add/remove_poller` flip an atomic count under the existing
  mutex and nudge the vat via `mel_vat_source_demand_changed`, which is owner/cross-thread safe.
- `mel_gpu_render_source_new(Mel_Vat*, sc, hz, fn, user)`: on macOS it is a true vsync source —
  the probe's CVDisplayLink machinery ported as `modules/vat/src/macos/vsync.c` + `vat/vsync.h`
  (CV thread: CAS an atomic `signaled` flag, ring the waiter doorbell; vat side: deadline `0`
  while signaled else `NEVER`, drain clears and fires the frame callback) — verified live by
  sampling hello-gpu: `mel_vat_run → mel_vat__vsync_drain → window_render`. Elsewhere it falls
  back to an hz-paced `Mel_Vat_Tick`. `vat/build.c` gained `-framework CoreVideo`.
- webgpu's `mel_gpu__drain_sync` re-entrancy guard now uses `mel_vat_is_owner` (NULL-guarded —
  `mel_vat_is_owner` dereferences, unlike the old reactor call).
- `apps/hello-gpu` is boot-hosted: `mel_app_setup(Mel_Vat* root)`, `mel_gui_init(root)`,
  `gpu_host_init(root)`; build deps `app` → `boot` + `vat`. `gpu_host`'s `atexit` shutdown
  became `mel_app_on_exit` — under boot, `atexit` would run after `mel_vat_close(root)` and
  the pump's `mel_vat_source_close` would touch a freed vat.
- `modules/gpu/build.c`: every `reactor` dep (lib + 11 test targets) → `vat`; tests
  `.reactor = NULL` → `.vat = NULL` (test_scene.c, test_metal.c); gpu readme refreshed.
- Gates: `./nob build gpu` clean for metal, vulkan and webgpu on macos; hello-gpu builds,
  runs (bare + `HELLO_GPU_AUTO=cube`/`triangle`), alive at 3–4 s, killed clean. Tests run and
  green: gpu-foundation 13/13, gpu-resources 4/4, gpu-metal 12/12, gpu-scene 16/16 (2 skipped),
  gpu-vulkan 48/48, gpu-concurrency 13/13, gpu-stress 20/20, gpu-visual 13/13 (vulkan ones via
  `DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1`), vat-core 9/9. Not run: gpu-d3d12
  (win32-only; the d3d12 edits are uncompiled here), gpu-webgpu (dawn runtime not exercised),
  gpu-bench (bench, not a gate).

## Kludges — gpu wave (MEL-ENGINE-VIII)

- `mel_vat_vsync_close` does `CVDisplayLinkStop` + `Release` then frees; Apple does not
  guarantee Stop joins an in-flight output callback, so a sub-millisecond window exists where
  the callback could touch the freed struct. Inherited verbatim from the probe's
  `vsync_shutdown`; noted in the vat readme as owed.
- The vsync source ignores the requested `hz` and paces at display refresh (the design
  intent), so a 60 hz request renders at 120 on ProMotion. The hz-tick fallback honors hz.
  No per-source rate divider yet.
- CVDisplayLink is deprecated since macOS 15 (Apple wants per-NSView/NSScreen
  CADisplayLink); it still works and matches the probe. The replacement rides the same
  source shape when taken up.
- `vsync.c` resolves the doorbell via `mel_vat_waiter(vat)->vt->ring` from the CV thread —
  the documented coalescing-doorbell contract, but it bypasses any future per-source wake
  accounting a waiter might grow.
- The pump and gpu futures still allocate from `mel_alloc_heap()` instead of a caller seam
  (pre-existing; MEL-CODE-003 debt left untouched to keep the wave mechanical).
  `mel_gpu_render_source_new` now borrows `mel_vat_alloc(vat)` rather than heap.
- `mel_gpu_future_wait` still spin-sleeps at 100 µs (pre-existing shape, reactor assert
  swapped for the vat one).
- The d3d12 backend edits (`d3d_backend.h`, `device.c`, `queue.c`) are sed-mechanical and
  compile-unverified on this host; a win-pilot build is owed before trusting them.
- ios gpu builds get the hz-tick fallback (no CADisplayLink vat source yet); web gets the
  tick too — `requestAnimationFrame` belongs to the guest-bridge wave.

## CLAUDE.md suggestions — gpu wave

- None.

## Suggestions — gpu wave

- A rate-dividing wrapper over the vsync source (render every Nth vblank) would let the
  `hz` parameter mean something again on high-refresh displays without losing vsync phase.
- `mel_vat_vsync_interval` exposes the measured refresh period; the gpu render source could
  forward it so apps can pace simulation to the true display rate instead of trusting `hz`.

# dialog / shell / clipboard + melody-showcase wave

## Work done

- `dialog` — reactor → vat. The loop coupling was twofold: a thread-id affinity assert
  captured at init, and `deliver_ok` validating the caller's executor against
  `mel_reactor_executor`. Now `mel_dialog_init(const Mel_Alloc*, Mel_Vat*)`, opts carry
  `.vat`, affinity asserts are `mel_vat_is_owner(g.vat)` (thread-id tracking and the
  `thread` dep deleted), expected deliver is `mel_vat_executor`. The two reactor-spawn
  tests now open their own `mel_vat_waiter_io` + fair-driver vat with a deadline-0 idle
  source (the `test_port.c` run-helper shape); 12/12 green.
- `shell` — the reactor parameter was stored and never read (the audio precedent): deleted
  outright. `mel_shell_init(const Mel_Alloc*)`; the module depends on neither loop now.
  18/18 green.
- `clipboard` — three couplings: the default deliver executor, the 250 ms watch poll timer,
  and the linux x11/wayland fd sources behind `mel_clip__reactor()`. Now
  `mel_clip_init(const Mel_Alloc*, Mel_Vat*)`, `g.exec = mel_vat_executor(vat)`, the watch
  timer is a `Mel_Vat_Tick` (closed on unwatch), `mel_clip__vat()` replaces the backend
  accessor, and both linux backends are single-wakeable vat sources modeled on
  `gui/src/xcb/backend.c` (flush rides the deadline query, dispatch rides drain). 34/34
  green.
- `hid` (not in the assigned trio, but broken in the showcase's dependency path by the port
  wave: it called the deleted `mel_port_reactor`) — the non-pollable-fd fallback is now a
  deadline-0 vat source on `mel_port_vat(port)`, drain polls `mel_hid__read_now` and closes
  itself on completion. 8/8 green.
- `melody-showcase` — ported to boot, both modes. Apps define no `main`: `setup.c`'s
  `mel_app_setup(Mel_Vat* root)` scans `mel_app_argc/argv` for `--smoke` and registers
  either mode on the same root vat. Smoke: the reactor idle source became a single
  `mel_vat_post`ed kick task that runs the sync probes and starts the existing future
  chain on `mel_vat_executor(root)`; finish destroys fs/storage, shuts down
  dialog/shell/clip, `mel_app_set_exit_code(0)`, `mel_vat_quit`. Window mode: `g.vat` +
  `g.exec`, gui/vib/dialog/clip inits take root, shell takes none, the 5 Hz timer is a
  `Mel_Vat_Tick`, Q is `mel_vat_quit`, lifecycle subscription moved from `app/lifecycle.h`
  to `boot/lifecycle.h` (`mel_app_init`/`mel_app_quit` deleted — boot owns lifecycle), and
  a `mel_app_on_exit` hook closes the tick, unsubscribes, destroys a live tray, and shuts
  the three modules down. build.c: app → boot, reactor → vat.
- `camera-scanner` (other agent's rewrite in flight): mechanical init fix only —
  `mel_clip_init(g_app.alloc, NULL)`; the file still passes a `Mel_Reactor*` everywhere
  else and does not compile, as before this wave.
- Gates: dialog, shell, clipboard, melody-showcase build clean; `--smoke` runs to
  completion and exits 0; window mode alive at 3 s with the boot lifecycle event delivered
  (phase 0x8); vat-core 9/9, port-loop 17/17, process-spawn 11/11, hid-core 8/8.

## Vat-core gap found (reported, not fixed)

The macos `mel_vat_waiter_ui` (cocoa) `arm` refuses every wakeable, so any port-backed fd
source on the root vat asserts `vat: waiter refused wakeable` (vat.c:315). Concretely:
`mel_process_run` (piped stdio) crashes on every boot-entry macos app. The vat readme's
"wakeable bridging" owed item covers the guest waiter; the sovereign cocoa waiter needs the
same bridge (kqueue or CFFileDescriptor behind the NSApp pump).

## Kludges (MEL-ENGINE-VIII)

- Because of that gap, smoke's process step abandons `mel_process_run` pipes: it spawns
  `/bin/echo` with stdout redirected to a regular temp file (`mel_io_file_open`, no vat),
  awaits the deadline-only reap source, and reads the file back synchronously. Honest
  capture, wrong mechanism; revert to `mel_process_run(.vat, .deliver)` when the ui waiter
  arms fds.
- Window mode's P key still calls `mel_process_run(.vat = g.vat, ...)` as specified — on
  macos today that key asserts in vat core. Left as the intended end-state shape; it is the
  waiter's bug, not the call site's.
- Smoke sets exit code 0 even when a mid-chain step fails (fs/storage/clip failure paths
  still finish the sequence) — parity with the old reactor-spawn return code, but a smoke
  that "passes" on a failed write is a lie worth fixing.
- The hid fallback drain busy-polls at deadline 0 exactly like the old reactor idle source
  busy-polled — parity, but it burns a core while a non-pollable read is pending.
- clip_x11.c / clip_wayland.c are compile-unverified here (no linux toolchain on this
  host); they are line-for-line shaped on the xcb gui backend.
- On x11/wayland dispatch failure the drain closes the source and nulls the cached pointer;
  the X/wayland connection itself stays up until shutdown — same as the reactor version,
  which also only detached the source.
- `mel_clip_watch` with a NULL vat still returns an event that never fires (pre-existing
  honesty hole, kept).
- camera-scanner's mechanical `NULL` vat means its clipboard ops fall back to the inline
  executor until the rewrite passes the root vat.

## CLAUDE.md suggestions

- None.

## Suggestions

- Bridge fd wakeables into the cocoa waiter (kqueue drained behind an NSEvent-posting
  monitor thread, or CFFileDescriptor sources) — it unblocks process pipes, port, and io
  readiness streams for every macos gui app; today the sovereign ui vat is timer-and-post
  only.
- `./nob build` accepting multiple targets in one invocation would make multi-module gates
  cheaper (`./nob build dialog shell` parses `shell` as a platform today).

## Work done — wave 11 (same session): cocoa waiter fd bridge

- The sovereign cocoa waiter now arms fd wakeables via the darwin Bridge from `main_design`:
  each armed wakeable gets a `CFFileDescriptor` + runloop source on common modes, whose
  callback fires inside `nextEventMatchingMask`'s runloop spin, sets `revents`, and posts the
  doorbell event so the wait returns and the driver drains. CFFileDescriptor callbacks are
  one-shot: re-enabled for every bridge at wait entry, giving level-triggered semantics
  (still-ready fds refire on the next spin). Disarm invalidates and releases; close disarms
  all. Pinned by `vat.cocoa_waiter_bridges_fd_readiness` (vat-core 10/10).
- This closes the gap the showcase wave reported: port-backed fd sources (process pipes,
  io readiness streams) no longer assert on macOS boot-entry apps. Showcase smoke's
  file-redirect workaround for the process step can be reverted to `mel_process_run` pipes in
  a follow-up.

## Kludges — wave 11

- The bridge re-enables every armed wakeable's callbacks at each wait entry — O(bridges) per
  turn; fine at GUI fd counts, wrong for a server (which belongs on the kqueue waiter anyway).
- `CFRunLoopGetCurrent()` is captured at arm time implicitly (arm runs on the owner thread by
  the source-open assert); a waiter used before any run on its opening thread is consistent,
  but the constraint is contractual, not asserted.

## Work done — wave 12 (same session): suspension naming + the await waist

- Renamed `modules/coroutine` → `modules/routine` (approved): it is a fiber-pool scheduler
  for frame-cadenced routines, not a coroutine primitive. Full sweep — `mel_coro_*` →
  `mel_routine_*`, `mel__coro*` → `mel__routine*`, `Mel_Coro_*` → `Mel_Routine_*`,
  `MEL_CORO_*` → `MEL_ROUTINE_*`, includes `<routine/routine.h>`, library name `routine`.
  No consumers existed (grep over modules/apps/tools/packages); the only doc references
  (vibration spec §6.4/§7) updated. Module gained a readme (it had none).
- Renamed `modules/continuation` → `modules/coro` (approved; `script` stays reserved for
  language embedding). The full prefix sweep was taken, codegen tool included: `mel_cont*` →
  `mel_coro*`, `Mel_Cont_*` → `Mel_Coro_*`, `MEL_CONT_*` → `MEL_CORO_*`, annotation
  `mel:continuation` → `mel:coro`, managed-region markers, `-DMEL_CORO_CODEGEN`, includes
  `<coro/coro.h>` + `<coro/abi.h>`, tool `continuation-gen` → `coro-gen`
  (`codegen/coro_gen.c`), tests `cont-test-*` → `coro-test-*`, example `coro-example`,
  fixture/golden extension `.cont.h` → `.coro.h`. No prefix split was needed — the sweep was
  mechanical and self-consistent because the layout hash feeds only on field type/name
  strings; the one knock-on (relay's child-frame field type `Mel_Coro_Frame_child_seq`
  changing relay's hash) was healed by the in-place codegen run and the golden refreshed from
  it. All five `coro-test-*` differential tests and `coro-example` green. The readme's stale
  "outside the nob framework" claim was corrected while updating it (the module builds via
  nob targets today).
- New `modules/await` — the suspension waist from the `test-suspension` probe, thin glue over
  the existing pieces, no new scheduler. Surface (`<await/await.h>`, `mel_await_*`):
  - `mel_await_coro_start(c, desc)` drives a stackless frame as a `Mel_Task` on `desc.exec`;
    the resume callback (`bool (*)(void* frame, Mel_Await_Step* out)`, the shape of a
    generated `<name>__resume`) yields a step naming its dependency: `.future` →
    `mel_future_then(f, &task, exec)`; `.channel`/`.slot`/`.is_send` → the channel's future
    flavor (`mel_channel_send_future`/`recv_future` into an adapter-owned `op_future`) then
    `then`; `.after_ns` → `mel_vat_timers_add`; `.reschedule` → executor resubmit;
    `.status_out` → the awaited future's status, written before the next resume. Completion
    resolves an optional `desc.done` future (with the frame pointer), fires `desc.on_done`,
    and releases the vat. A `desc.vat` binding takes `mel_vat_retain` at start and releases
    at completion — the fs op pattern — with asserts pinning `exec == mel_vat_executor(vat)`.
  - `mel_await_future(f)` parks the calling job-worker fiber on a `Mel_Counter` until the
    future resolves (then-task on the inline executor decrements), returns
    `mel_future_value`.
- Tests (`await-bridge`, 6 passing): fiber parks on a pending future resolved off-thread; the
  probe's scenario_bridge — a fiber sends 2/3/7 over an unbuffered channel, the stackless
  side squares and replies over a second channel, sentinel ends it — both sides on melody
  primitives over the job pool; the semaphore-ish scenario (fiber produces three tokens into
  a buffered channel with `mel_job_yield` between, coro acquires them); coro awaits a future
  and reads `MEL_FUTURE_WARNED` through `.status_out` on the inline executor; pure
  `.reschedule` turns; and a vat-bound coro sleeping on three 2 ms timers, with a second
  `mel_vat_run` after `timers_close` proving the retain/release pair balanced (a leaked
  retain would hang the test into its timeout).
- Gates: `await-bridge`, `coro-test-{sum_to,countdown,classify,relay,repeat_sum}`, and
  regressions `vat-core`, `executor-core`, `port-loop`, `future-core`, `channel-core`,
  `channel-fiber`, `job-executor` all green; `routine` builds (it has no tests).
- Ledger and the suspension-bridging section of `design/vat-integration.md` updated to
  as-built.

## Kludges — wave 12

- The coro module's golden/snapshot/rejection suites are still not wired to nob targets (the
  original self-contained driver owned them); only the five differential tests gate. The
  goldens were refreshed by hand against fresh tool output (verified byte-identical after
  repo-style formatting, the goldens' established convention) — but nothing automated will
  catch the next drift. Confessed in the coro readme.
- `await`'s adapter has no cancellation propagation: cancelling `desc.done` does not retract
  a pending channel op or timer; a cancelled awaited future merely surfaces through
  `.status_out`. Owed in the readme.
- `Mel_Await_Step` discriminates by which field is set, asserting `reschedule` when none is —
  a misuse where two fields are set silently takes the first branch (future > channel >
  timer). A debug assert on exclusivity would be better.
- The vat-bound timer test pins release-balance behaviorally (second `mel_vat_run` returns)
  because `Mel_Vat` exposes no retain count; honest but indirect — failure mode is a 30 s
  harness timeout, not an assert.
- `mel_await_future` is fiber-only by construction (`mel_counter_wait` needs the job pool's
  park runtime); calling it off a worker fiber is undefined by the signal module's rules, not
  guarded here.
- `mel_await_coro_start` writes into the channel-op `op_future` embedded in the adapter; a
  coro that yields a channel step while a previous channel op is still unsettled would reuse
  it — impossible through the adapter's own resume discipline (one pending wait at a time),
  but unasserted.
- The await tests' bridge/token scenarios spin on atomics with `mel_thread_yield` bounded by
  a large iteration cap (the channel-fiber test's existing pattern), so a deadlock shows as a
  test failure rather than a hang — still a busy-wait in tests.
- Writeup naming: this session's work is appended here as wave 12 per the established
  one-file-per-session convention, though the renames are not strictly vat work.

## CLAUDE.md suggestions — wave 12

- None.

## Suggestions — wave 12

- Wire the coro golden diff, snapshot round-trip, and rejection fixtures into `mel_add_test`
  targets so the compiler-grade suites gate again.
- `mel_await_coro_spawn(vat, ...)` sugar (alloc from the vat allocator, own the adapter
  lifetime) once a second consumer appears; same moment is right for cancellation
  propagation.
- A `Mel_Vat` debug accessor for the retain count would let retention tests assert directly.

## Work done — wave 13 (same session): the demolition

- camera-scanner, the reactor's last living consumer, ported to boot+vat: setup-only entry,
  `mel_reactor_post` for GUI updates became an embedded armed-coalesced `Mel_Task` via
  `mel_vat_post` (redundant frame-update posts now coalesce for free), auth future on
  `mel_vat_executor`, all module inits on the root vat. Builds; smoke-run alive.
- Audit: zero build.c deps, zero source references to `reactor/*` or `app/*` outside the two
  modules themselves. `modules/reactor` and `modules/app` DELETED (uncommitted; git holds the
  history).
- Post-deletion full `./nob test` sweep: 76 targets. Seven initial failures, none caused by
  the deletion: four audio test TUs still calling the removed three-arg `mel_audio_create`
  (missed in wave 5's sweep; fixed, suites green) and three gpu suites crashing under the
  runner's fork-per-test against Objective-C runtime initialization — pass under
  `MEL_TEST_NOFORK=1`, the same environment the gpu wave used.

## Kludges — wave 13

- The gpu fork-vs-ObjC interaction deserves a durable home (the proposed
  docs/verification.md); today it is tribal knowledge in two writeup waves.
- camera-scanner's android run was not re-verified after the vat port (the gui retain trap its
  rewrite hit is gone — gui_init now receives a real vat — but the emulator pass is owed).
- Deletion is uncommitted by design: Gabbo commits.
