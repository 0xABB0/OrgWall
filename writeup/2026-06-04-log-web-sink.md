# 2026-06-04 — log degrades to a synchronous inline drain when the writer thread cannot spawn (web/single-threaded lane)

Task #26. `modules/log`'s async-writer thread is the **sole** thread-spawn on the single-threaded
Emscripten web lane. `pthread_create` fails there; `mel__ensure_writer_thread` asserted fatally
(`assert(ok)` at `log.c:464`), killing hello-gpu bring-up before the first frame. The remedy
(per the app-web agent's analysis, `2026-06-04-app-web-entry.md`): degrade to a synchronous
inline drain instead of mandating a writer thread; `-pthread` is the wrong fix — it adopts the
Web-Worker/SharedArrayBuffer model the single-threaded lane deliberately rejects.

## Degrade design — how it detects and falls back

The log pipeline is unchanged in shape: `mel__log`/`mel__log_signal` format the record and push it
into the lock-free ring; a drainer pops records and fans them to the sinks. Previously **only** the
writer thread drained. The change adds a synchronous drainer on the calling thread, selected at
runtime by a single atomic flag set exactly once.

Two new file-scope atomics (`modules/log/src/log.c`):

- `sync_mode` — true ⇔ the writer thread could not be spawned; the ring is drained inline.
- `writer_thread_active` — true ⇔ a real writer thread exists and must be joined at shutdown.
  Previously `writer_running` conflated "logging is live" with "a thread exists"; in sync mode the
  former is true while the latter is false, so the two concerns are split.

Detection — `mel__ensure_writer_thread` (called lazily on the first `mel_log_sink_add`, i.e. when the
console sink registers at constructor priority 101):

- `mel_thread_spawn` already returns `false` cleanly on `pthread_create` failure (no abort inside
  `modules/thread`), so no thread is leaked. The old `assert(ok)` is replaced:
  - spawn succeeds → `writer_thread_active = true` (async path, **identical to before**).
  - spawn fails → `sync_mode = true`, then **one** loud WARN note is written **directly** to the
    sinks (`mel__sync_note`): `"writer thread unavailable; draining synchronously on the calling
    thread"`. Not silent (MEL-CODE-007); not fatal on a recoverable condition (MEL-ENGINE-VIII /
    VII — the best the platform can offer, not a broken shadow).

Fallback — `mel__drain_inline_if_sync()` is appended to the end of `mel__log` (after `tls.in_log`
is cleared, so the recursion guard is honest if a sink itself logs) and to the end of
`mel__log_signal`. It reads `sync_mode` with `memory_order_relaxed` and, only when set, calls the
existing `mel__drain_all()` on the calling thread. The drainer, the ring, the sink fan-out, and the
dropped-entry accounting are reused verbatim — no second code path, no special-cased sink (MEL-ENGINE-IX).

Flush — `mel_log_sink_flush_all` in sync mode drains + flushes inline and returns, instead of
signalling the (nonexistent) writer thread and blocking on its condvar forever.

Shutdown — `mel__log_shutdown` joins **only** when `writer_thread_active`; in sync mode it drains +
flushes inline (so no record is lost) and never attempts to join a thread that was never created.

## Native path is untouched

On every native target `sync_mode` stays false:

- `mel__ensure_writer_thread` spawns the thread and sets `writer_thread_active` — the async writer
  loop, the flush condvar handshake, and the join-at-shutdown are byte-for-byte the prior behavior.
- The only addition on the native hot path is one `atomic_load_explicit(&sync_mode,
  memory_order_relaxed)` per `mel__log`/`mel__log_signal` call (a relaxed load of a
  monotonically-set bool, branch-predicted not-taken). It is the cost of a single non-synchronizing
  load; it does not touch the ring, the sinks, or any lock. No measurable regression.

Proven green:
- `./nob build log macos` → liblog.a clean.
- `DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1 ./nob test gpu-vulkan macos --gpu=vulkan`
  → **48 passed / 0 failed**. The async writer drained all the INFO/WARN bring-up lines.
- `... ./nob test gpu-metal macos --gpu=metal` → **6 passed / 0 failed**.
- `modules/log` has no discrete `mel_add_test` target; it is exercised transitively by the gpu
  suites above (which log heavily through the async path).

## Web bring-up — what the browser now shows

`./nob build hello-gpu wasm --gpu=webgpu` → GREEN. Artifacts:
`apps/hello-gpu/build/wasm-debug/hello-gpu.{html 526B, js 254KB, wasm 1.0MB}`.

Serve: `python3 modules/build/web/serve.py apps/hello-gpu/build/wasm-debug 8731` (html/js/wasm all
HTTP 200). No bespoke shell/serve helper was needed — the shared `serve.py` and the shared
`shell.html` (`#mel-root`) sufficed, so **only `modules/log/src/log.c` changed**.

Browser: Google Chrome for Testing 148 (playwright cache
`~/Library/Caches/ms-playwright/chromium-1223/chrome-mac-arm64/Google Chrome for Testing.app`),
driven via `playwright-core` 1.60 over CDP, headless `--headless=new --enable-unsafe-webgpu
--use-angle=metal --use-webgpu-adapter=default --use-gpu-in-tests --ignore-gpu-blocklist`. Driver:
`/tmp/melody-webgpu-proof/drive.mjs` (throwaway, outside the repo tree). Screenshot:
`/tmp/melody-webgpu-proof/canvas.png` (1280×720).

Deterministic console (ANSI stripped), in order:

    [WARN] [log] writer thread unavailable; draining synchronously on the calling thread
    [INFO] [gpu] webgpu instance created: 1 adapter ('WebGPU Adapter')
    [WARN] [gpu] device_create: feature 'descriptor_indexing (bindless stays tier=capped)' requested but not available on the WebGPU backend; caps report the honest tier
    [INFO] [gpu] webgpu device created on 'WebGPU Adapter'
    [pageerror] function signature mismatch        (×312)
    [pageerror] memory access out of bounds

`grep -iE 'log\.c|Aborted|Assertion|mel__ensure_writer'` over the full console → **empty**. The log
abort is gone. Every line after the sync-note reached the console **through the synchronous inline
drain** — these records previously never appeared because the runtime aborted at writer-thread spawn.

The screenshot proves the **dom GUI host rendered** the hello-gpu launcher panel (the descriptive
text + every button: "hello-triangle", "spinning-cube (CPU sort)", "lorenz-attractor", …). So
bring-up went well past `mel_app_setup`: GUI init, GPU host init, **WebGPU device creation**, and
the launcher paint all succeeded.

## The remaining gate is NOT log — it is a wasm ABI trap in the gpu/gui webgpu-wasm render path

After device creation the runtime traps: `function signature mismatch` floods every render tick,
then `memory access out of bounds`. No `<canvas>` element is ever created
(`document.querySelector('canvas')` → null), so the GPU-view swapchain/surface bring-up
(`#mel-gpu-N`, created lazily by `modules/gui/src/dom/gpu_view.c` on a GPU window open) is where the
trap lands. `function signature mismatch` is the canonical Emscripten symptom of a C callback whose
signature does not match the JS-side indirect call (a function-pointer-table cast mismatch) — almost
certainly in the webgpu-wasm surface/render callback path
(`modules/gpu/src/webgpu/wasm/surface.c`, `modules/gpu/src/render_source.c`, or a
`requestAnimationFrame`/`emscripten_set_timeout` callback signature). This is **outside this task's
file ownership** (gpu/gui/app wasm backends) and is a genuinely new gap, downstream of log.

Honest deferral: the triangle did **not** render and there is **no** clear-presented canvas yet,
because the surface never came up. What is conclusively delivered: bring-up gets past `mel_app_setup`
and past log (no abort), the GUI shell paints, and the WebGPU device is created — exactly the bar the
log fix was responsible for.

### Manual-browser reproduction (for the gpu/gui-wasm owner)

1. `./nob build hello-gpu wasm --gpu=webgpu`
2. `python3 modules/build/web/serve.py apps/hello-gpu/build/wasm-debug 8731`
3. Open `http://localhost:8731/hello-gpu.html` in Chrome ≥ 113 with WebGPU (or Chrome for Testing
   148 headless: `--headless=new --enable-unsafe-webgpu --use-angle=metal --use-gpu-in-tests`).
4. Observe: launcher panel paints; console shows `webgpu device created`; then
   `function signature mismatch` on the render tick. Click a launcher button to force a GPU-view /
   `#mel-gpu-N` canvas open and watch where the indirect-call signature diverges.

## Kludges (confess all, MEL-ENGINE-VIII)

1. **One added relaxed atomic load per log call on every platform.** To keep the path uniform
   (MEL-ENGINE-IX, no platform `#ifdef`), the sync-mode check compiles everywhere, costing native a
   single relaxed load of a never-true bool per `mel__log`. Sub-nanosecond, branch-predicted, no
   lock or ring touch — but it is non-zero. A compile-time `#if MEL_PLATFORM_EMSCRIPTEN` gate would
   erase it at the cost of a platform special-case; I judged uniformity the better trade and flag it
   for Gabbo to overrule.
2. **Sync-mode drain runs unbounded on the calling thread.** In sync mode `mel__log` drains the
   *entire* ring inline, so a thread that logs in a tight loop pays the full sink-write cost (e.g.
   `fprintf`) synchronously — exactly the latency the async ring was built to hide. This is the
   honest cost of a single-threaded target (there is no other thread to offload to), not a defect,
   but it does mean log calls on web are as slow as the slowest sink. Acceptable on the web lane;
   would be a regression if `sync_mode` were ever wrongly set on a multithreaded target (it is set
   only on real spawn failure).
3. **The browser proof depends on `playwright-core` resolved by absolute path** from the npx cache
   (`~/.npm/_npx/.../playwright-core`) and Chrome for Testing from the ms-playwright cache. The
   driver (`/tmp/melody-webgpu-proof/drive.mjs`) is throwaway and outside the repo; it is not a
   committed artifact. If those caches are pruned the recipe must `npx playwright install chromium`
   first.

## CLAUDE.md suggestions (recommendations only — not applied)

- Document the web lane's threading contract in `modules/log/readme.md` (or a `log` spec): "the
  writer thread is an optimization, not a requirement; on single-threaded targets log drains inline
  on the calling thread." Future readers should not re-discover this from the abort.
- A short note in `modules/build/platforms.md` that the wasm lane is single-threaded by design and
  that any module spawning a thread on web must degrade, not assert (log is now the reference).

## Suggestions / open questions for Gabbo

1. **The `function signature mismatch` trap is the new single gate** between "device created" and
   "clear-presents / triangle". It is in the gpu/gui webgpu-wasm surface/render path, not log —
   needs the gpu-wasm owner. Whose lane?
2. **Should the sync-mode check be compile-time gated** to `MEL_PLATFORM_EMSCRIPTEN` (kludge #1)? My
   default keeps it runtime-uniform.
3. **Generalize the degrade beyond web?** Any future native target where `pthread_create` can fail
   under resource pressure now logs synchronously instead of crashing — a strict improvement, but it
   means a silent perf cliff on an overloaded native host (mitigated by the one-time loud WARN).
   Confirm this broader behavior is desired, or restrict `sync_mode` to web only.
