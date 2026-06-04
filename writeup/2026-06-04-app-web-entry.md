# 2026-06-04 — modules/app WEB/wasm runtime entry (browser lane)

Gives `modules/app` a wasm `main` so hello-gpu's GUI/GPU host is no longer dead-stripped on
`wasm --gpu=webgpu`. Task #20, the gating piece the WebGPU-wasm writeup
(`2026-06-03-gpu-webgpu-wasm.md`) flagged as missing: "modules/app has no web/wasm runtime entry …
nothing calls `mel_app_setup` → the GUI/GPU host is dead-stripped".

## Entry design — how the reactor is driven on the browser loop

`modules/app/src/web/app.c`: a `main` that calls
`mel_reactor_spawn(MEL_REACTOR_ATTACHED, app_init, NULL)`, where `app_init` calls
`mel_app_setup(reactor)` — the exact shape of every other entry (posix/ios/win32/android).

No bespoke `emscripten_set_main_loop` is written here: the **reactor already owns the browser loop**.
`modules/reactor/src/web/reactor_backend.inl` implements `reactor_backend_attached_run`, which runs
`init` on the main thread, drives the first `reactor_iterate`, then calls
`emscripten_exit_with_live_runtime()` and **returns to the browser**. Subsequent iterations come from
`emscripten_request_animation_frame` (timeout==0 / idle-fast sources) and `emscripten_set_timeout`
(finite timeouts, e.g. the 60 Hz render timer) — scheduled per-iterate by `reactor_web_schedule`. So
the GPU render source (a reactor timer at 60 Hz, `modules/gpu/src/render_source.c`) becomes a
`setTimeout`-paced render tick on the browser, and the GUI's canvas resize fires the surface/swapchain
bring-up. `MEL_REACTOR_ATTACHED` is the precise mode (single-threaded, live-runtime, main-thread);
note `reactor.c` already redirects `MEL_REACTOR_THREADED` to the same attached path under
`MEL_PLATFORM_EMSCRIPTEN`, so either mode is non-blocking on web — ATTACHED states the intent.

The canvas the dom gpu_view needs (`#mel-gpu-<N>`) is created by the dom GUI backend itself when a GPU
window opens (`modules/gui/src/dom/gpu_view.c` → `mel_web__el_create("canvas")` appended under
`#mel-root`); the entry does not need to author DOM. The shell (`modules/build/web/shell.html`,
shared, unchanged) already provides `#mel-root`.

Bring-up failure is loud (`emscripten_console_error`) — no silent default (MEL-CODE-007,
MEL-ENGINE-VIII). No `log` dependency was added to `app` (would have coupled a new module
everywhere); `emscripten/console.h` is in the default runtime.

## What actually renders

`./nob build hello-gpu wasm --gpu=webgpu` is **GREEN**; the host is retained (wasm grew 256 KB →
1.0 MB vs the host-stripped prior build; the `.js` glue carries the full emdawnwebgpu surface —
`wgpuInstanceRequestAdapter`, `wgpuAdapterRequestDevice`, `wgpuBufferMapAsync`, …). The entry
provably executes through `mel_app_setup` (see run-proof). **Clear-present was not reached at
runtime** — blocked by a `log`-module thread gap, not the entry (see below). The triangle geometry
would not draw even past that block: `apps/hello-gpu/src/triangle.c` falls to the SPIR-V branch on
WebGPU (no WGSL branch), Tint's SPIR-V reader is off → `shader_create` refuses loudly → clear-only.
That is the **cap-driven-WGSL sibling task** (branch the app shader target on
`caps.shader.bytecode_passthrough.wgsl`); identical to native macOS webgpu. Clear-present is the bar;
the triangle is the sibling's.

## Run-proof — honest, conclusive partial

Browser: **Google Chrome for Testing 148** (playwright cache:
`~/Library/Caches/ms-playwright/chromium-1223/chrome-mac-arm64/Google Chrome for Testing.app`),
headless `--headless=new --enable-unsafe-webgpu --use-angle=metal`.
Serve: `python3 modules/build/web/serve.py apps/hello-gpu/build/wasm-debug 8731` (html/js/wasm all
HTTP 200).

Deterministic console output:

    Aborted(Assertion failed: ok, at: modules/log/src/log.c,464,mel__ensure_writer_thread)
    Uncaught (in promise) RuntimeError: Aborted(...mel__ensure_writer_thread)

This **proves the entry is correct**: `main` ran → `mel_reactor_spawn(ATTACHED)` → `app_init` →
`mel_app_setup` → `mel_gui_init`/`gpu_host_init` → first **log-sink registration** →
`mel__ensure_writer_thread` → `mel_thread_spawn` (`pthread_create`) **fails on the single-threaded
browser** → `assert(ok)` aborts before first paint. The abort fires *inside* `mel_app_setup`, so the
entry demonstrably reaches it. (No screenshot: the runtime aborts before the first frame; faking a
render was refused per task.)

### The remaining gate is `log`, not the app entry

The web lane is single-threaded by design (ASYNCIFY drain + ATTACHED reactor; the reactor spawns **no**
worker thread, the webgpu backend spawns none). The **only** thread-spawn on the whole web lane is
`log`'s async writer thread (`mel__writer_thread_fn`, spawned lazily on the first `mel_log_sink_add`).
On Emscripten without `-pthread` there is no worker, `pthread_create` fails, and `log` hard-asserts.
`log.c` has no synchronous/threadless path. **Fix (log-module owner, outside this lane):** a
synchronous (inline-drain) sink mode on single-threaded targets, or guard
`mel__ensure_writer_thread` to degrade to inline drain when spawn fails instead of asserting. Building
the whole tree with `-pthread` is the wrong remedy — it adopts the Web-Worker/SharedArrayBuffer model
the single-threaded lane deliberately rejects.

## Cross-module enablement discovered (the task's "single gating piece" premise was incomplete)

Bringing the host alive exposed that the prior writeup's "once that entry exists, the backend path is
ready" was only true up to the dead-strip boundary. Live host code pulls `thread`/`time` symbols that
have **no wasm backend**. Minimal, flagged gates (no new behavior on other platforms):

- **`modules/time/build.c`** (SHARED): `src/nano.unix.c` gate += `MEL_ON(WASM)`. It is pure POSIX
  `clock_gettime(CLOCK_MONOTONIC/REALTIME)` (already `#ifndef _WIN32`), which Emscripten supports.
  Closes `mel_nanos_since_unspecified_epoch`.
- **`modules/build/toolchain.c`** (SHARED, build framework): wasm `base_cflags = "-D_GNU_SOURCE"` —
  parity with the linux toolchain (line 83). Without it Emscripten's `<time.h>` hides
  `clock_gettime`/`CLOCK_MONOTONIC` (strict conformance), and `nano.unix.c` fails to compile.
- **`modules/thread/build.c`** (SHARED): gate `src/posix/*.c` + `src/wasm/*.c` for `MEL_ON(WASM)`.
  The posix pthread backend compiles on Emscripten and Emscripten's libc provides the full pthread
  surface (mutex/rwlock/cond/create/join/nanosleep) — single-threaded but link-complete. Closes
  `mel_mutex_*`, `mel_rwlock_*`, `mel_cond_*`, `mel_thread_*`.
- **`modules/thread/src/wasm/setname.c`** (NEW): a no-op `pthread_setname_np` — the **only** pthread
  symbol Emscripten declares but does not define. Thread naming is cosmetic; a single-threaded target
  has nothing to name. Not a silent default — it is the honest implementation for the target.
- **`modules/build/emit.c`** (SHARED, build framework): on wasm, emit static libraries in **reverse
  topo order**. The topo closure is post-order (dependencies-first); `wasm-ld` resolves archives in
  one left-to-right pass and **ignores `--start-group`** (tried — Emscripten strips it), so a
  dependent archive (e.g. `liblog.a`) must precede its dependency (`libthread.a`). This was a latent
  build-framework bug masked until now because nothing pulled `log.o` on wasm (host dead-stripped).
  Apple ld is order-free; the change is gated `v->platform == MEL_PLATFORM_WASM`, native unaffected.

## Build / serve / verify

- `./nob build hello-gpu macos --gpu=metal` → **packaged** (posix entry untouched, native green).
- `DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1 ./nob test gpu-webgpu macos --gpu=webgpu`
  → **3 passed / 0 failed** (native webgpu regression intact).
- `./nob build hello-gpu wasm --gpu=webgpu` → **GREEN**, clean (26/26). Artifacts:
  `apps/hello-gpu/build/wasm-debug/hello-gpu.{html 526B, js 254KB, wasm 1.0MB}`.
- `./nob build gpu wasm --gpu=webgpu` → archive builds (reversal does not break the other wasm lane).
- Serve: `python3 modules/build/web/serve.py apps/hello-gpu/build/wasm-debug 8731` → 200 for all.

## Shared-file edits (FLAGGED)

| file | edit | blast radius |
|---|---|---|
| `modules/app/build.c` | + `src/web/*.c` sources, `-lhtml5` link (WASM) | wasm only |
| `modules/build/emit.c` | wasm: reverse static-lib link order | wasm executables only |
| `modules/build/toolchain.c` | wasm: `base_cflags = -D_GNU_SOURCE` | wasm compile only |
| `modules/thread/build.c` | + posix backend + `src/wasm/*.c` (WASM) | wasm only |
| `modules/time/build.c` | + `MEL_ON(WASM)` on `nano.unix.c` | wasm only |

New files: `modules/app/src/web/app.c`, `modules/thread/src/wasm/setname.c`.

## Kludges (confess all, MEL-ENGINE-VIII)

1. **`emit.c` reverse-order is a band-aid over a single-pass linker, not a true group.** `wasm-ld`
   has no `--start-group`; reverse topo is correct only for an acyclic lib graph (true here). A
   genuine mutual-recursion across two archives would still fail. Debt: detect cycles or repeat
   archives. Low risk today.
2. **`-D_GNU_SOURCE` is broad.** It exposes all GNU/POSIX extensions on wasm, not just
   `clock_gettime`. Matches the linux toolchain, but it is a blunt instrument; a tighter
   `_POSIX_C_SOURCE=199309L` would suffice for the nanos symbol alone. Flagged.
3. **Full posix thread backend gated on wasm, but only mutex/time are actually exercised pre-`log`.**
   The rwlock/cond/thread-spawn members link (Emscripten provides the symbols) but spawn fails at
   runtime — surfaced as the `log` abort. I gated the whole posix backend (not just mutex) so the
   link is complete and the *real* blocker (`log`'s thread policy) is the thing that shows, rather
   than masking it behind a partial gate. Honest, but it ships pthread members that cannot spawn.
4. **`pthread_setname_np` no-op shim defines a libc-reserved name.** Safe today (Emscripten leaves it
   undefined), but a future Emscripten that defines it would collide. Debt: `#ifdef`-guard or use
   `emscripten_set_thread_name`. Flagged.
5. **No automated browser screenshot.** The runtime aborts in `log` before first paint, so there is
   no non-blank canvas to capture yet. The console abort is the evidence. Once `log` gains a
   single-threaded sink, re-run the Chrome-for-Testing recipe above for the clear-present capture.

## Open questions for Gabbo

1. **`log` single-threaded sink.** The web lane needs `log` to drain synchronously (or degrade on
   spawn failure) instead of mandating a writer thread. Whose lane — log-module owner? This is now the
   single gate between "builds + loads + runs `mel_app_setup`" and "clear-presents in a browser".
2. **Relocate the cross-module gates?** The thread/time/toolchain/emit edits are outside the app-web
   lane I was given. Keep them here (they are the genuine prerequisites the task's premise missed), or
   hand the thread/time/build-framework pieces to those owners?
3. **Triangle WGSL on web** (sibling task, unchanged): branch `triangle.c` shader target on
   `caps.shader.bytecode_passthrough.wgsl`; today it clears only.
4. **`emit.c` lib-order fix** — should reverse-topo apply to linux/android too (same GNU-ld
   single-pass constraint)? I gated it wasm-only to avoid perturbing those lanes; they may carry the
   same latent bug, currently masked.
