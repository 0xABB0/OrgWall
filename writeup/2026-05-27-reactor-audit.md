# 2026-05-27 — Reactor audit & backend rework

## Work done

- **Audited the reactor module and every integration.** Read the public header,
  core (`reactor.c`), all six backends, the macOS event pump, and all call sites:
  `app` (posix/win32/ios/android), `gpu/render_source`, `gui` (cocoa + android
  quit wiring), and the three apps. Established the reference contract: the
  reactor is the abstraction *over* the platform loop (the platform may own it),
  thread-discipline is owner-only for all source/poll mutation, `post`/`quit`
  are the only cross-thread-safe calls, structural edits during dispatch defer
  to reap.

- **Corrected the core mischaracterisation.** The reactor is not "a main loop";
  it wraps whatever demultiplexer the target has — driving Win32 by hand,
  wrapping NSApp/CFRunLoop, riding the browser/Looper/UIApplication loop — and
  must spend nothing beyond what the live source set demands (zero sources = one
  idle native wait).

- **Reworked the apple backend** (`src/apple/reactor_backend.inl`) to keep
  `CFFileDescriptor` registrations as durable state and *reconcile* them to the
  live poll set each `wait` (add new, drop gone, leave stable), replacing the
  old per-iteration create/add/remove/release churn. THREADED records readiness
  via the persistent fd callback (no extra syscall); ATTACHED pulls it with one
  non-blocking `poll()`. Added `cf_fd_polls[]` to the reactor struct.

- **Reworked the android backend** (`src/android/reactor_backend.inl`) to
  register source fds on the `ALooper` (`ALooper_addFd`, reconciled like apple)
  and **removed the 16 ms busy re-poll** — fd readiness now wakes the next
  iterate through the looper instead of a fixed-rate spin. Added
  `reg_fds[]` / `reg_fd_count` to the reactor struct.

- **Verified the apple path**: `reactor.c` compiles clean for macOS at `-O2
  -Wall -Wextra`; the macOS GUI app runs (confirmed by Gabbo). For the
  currently-built apps (no fd poll sources) behaviour is identical — the
  reconcile is a no-op at `poll_count == 0`.

- **Fixed an unrelated pre-existing build break**: `third-party/webgpu/build.c`
  used `strcmp` without `<string.h>`; added the include (at Gabbo's request).

- **Rewrote `README.org`** to be precise: the abstraction-over-the-loop framing,
  the three goals, the actual public surface (`spawn`/`quit`/`post`/queries +
  source API — no nonexistent `run`/`iterate`/`shutdown`), the two modes and
  their teardown, and the persistent-reconcile fd model.

- **Created `todo.org` and `tests.org`** for the module.

## Kludges

- **None introduced in the reactor.** The per-iterate poll-set reconcile is a
  deliberate desired-state pattern, not a diff-kludge: native register/unregister
  fire only on change; the steady-state cost is a bounded pointer comparison.
  Rationale (and the rejected core-hook alternative) recorded in `todo.org`.
- **Android is unverified.** The looper-registration rework is written against
  the NDK API but has not run on a device/emulator (no NDK build in-session).
  Tracked in `todo.org`; flagged in the README's "What is verified".
- The `build.c` `<string.h>` fix is incidental, unrelated to the reactor.

## CLAUDE.md suggestions (recommendations only)

- Consider pointing the engine docs (or `CLAUDE.md`) at
  `modules/reactor/README.org` as the canonical reactor mental model, so future
  sessions stop conflating the reactor with the main loop. The README now states
  the framing explicitly; a one-line pointer would anchor it.

## Suggestions

- **Fix F1 first (real UAF).** hello-gpu's per-window render source outlives its
  GPU surface because the cocoa frame never fires `on_destroy`/`on_close` (a
  known gap — see `modules/gui/todo.org`). Closing a non-last GPU window renders
  into a freed Metal surface. Wire `on_destroy` from `windowWillClose`; have
  hello-gpu tear down on it. Low effort, high severity.
- **Resolve the source allocator asymmetry** before anyone ships a custom
  `Spawn_Opt.alloc`: sources are heap-allocated but freed via `reactor->alloc`.
- **Build the spy-backend test harness** described in `tests.org` — the
  "reconcile, don't churn" guarantee and the deferred-reap re-entrancy are
  currently unobservable without it, and they are exactly the parts most likely
  to regress.
