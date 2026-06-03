# 2026-06-04 — hello-gpu WebGPU triangle renders in the browser (wasm ABI trap fixed)

Task #27. After the log-web fix (`2026-06-04-log-web-sink.md`) the web app ran to **device
created**, then a wasm ABI trap flooded every render tick and killed surface bring-up:

    [pageerror] function signature mismatch   (×312 per tick)
    [pageerror] memory access out of bounds

No `<canvas>` was created; clear-present never ran. Root-caused to **one** function-pointer
signature mismatch in the WebGPU completion pump, and a **second** downstream gap (an emdawnwebgpu
present abort) exposed once the first was fixed. Both are inside `modules/gpu/src/webgpu/**`.

## Root cause #1 — the `function signature mismatch` flood (the ABI trap)

`Mel_Gpu_Poll_Fn` is `bool (*)(void* user)` (`modules/gpu/include/gpu/future.h:11`). The WebGPU
device registered its per-tick `wgpuInstanceProcessEvents` poller like this
(`device.c`, original):

    mel_gpu_pump_add_poller(dev->pump, (Mel_Gpu_Poll_Fn)mel_gpu__instance_pump_tick, dev);

but `mel_gpu__instance_pump_tick` was declared **`void` mel_gpu__instance_pump_tick(void*)** — a
`void(void*)` function **cast** to a `bool(void*)` slot. The pump invokes the poller every tick
through the `Mel_Gpu_Poll_Fn` type (`future.c:126`, `snapshot[i].fn(snapshot[i].user)`).

On native (Metal/Vulkan/D3D12) this cast is UB-but-works: the extra return register is garbage,
ignored. Under wasm the function-pointer table is **typed by signature**: a `void(void*)` function
lands in the `vi` table; invoking it through a `bool(void*)` (`ii`) slot is exactly Emscripten's
`function signature mismatch` abort. The pump timer fires at 2 ms (~500 Hz) and the reactor drives
it each iterate, so the trap floods "×per render tick" the moment the device's pump is registered —
i.e. immediately after `webgpu device created`, before any GPU window/canvas opens. The repeated
abort then corrupts the stack into `memory access out of bounds`.

The smell was visible in the tree: Vulkan and D3D12 register their poller (`mel_gpu__submit_poller`,
`static bool …(void* user)`) **without any cast**; only WebGPU cast. The cast was papering over a
return-type mismatch.

### Fix #1 — match the poller type exactly (no cast)

`mel_gpu__instance_pump_tick` now returns `bool` (`return true;` — semantics: "keep polling," the
same contract as `mel_gpu__pump_timer_cb` and the Vulkan/D3D12 pollers), and both call sites drop
the `(Mel_Gpu_Poll_Fn)` cast. Touched: `common.c` (definition), `wgpu_backend.h` (declaration),
`device.c` (add + remove poller). Native is behavior-identical — the function already did nothing
with a return value; now it has the correct type so the wasm table slot matches.

## Root cause #2 — `wgpuSurfacePresent` aborts under emdawnwebgpu (the next gap, also fixed)

With #1 fixed the trap vanished and bring-up advanced all the way to **`webgpu swapchain ready:
616×424`** — the canvas (`#mel-gpu-26`) was created and the swapchain configured. The very next
render tick then aborted:

    Aborted(wgpuSurfacePresent is unsupported (use requestAnimationFrame via html5.h instead))

This is **deliberate** in emdawnwebgpu: `wgpuSurfacePresent` is declared in its `webgpu.h`
(present in the native Dawn API the backend targets) but its JS implementation is an unconditional
`abort()` (`emdawnwebgpu_pkg/webgpu/src/library_webgpu.js:2615`). On the browser the canvas is
composited **implicitly** when control returns to the JS event loop at the end of the RAF/event-loop
turn — there is no explicit present call. The native Dawn backend requires the present;
emdawnwebgpu forbids it.

### Fix #2 — skip the explicit present on Emscripten

`record.c` `mel_gpu_frame_end` now gates the present:

    #ifndef __EMSCRIPTEN__
        wgpuSurfacePresent(sc->surface->wgpu);
    #endif
        wgpuInstanceProcessEvents(dev->wgpu_instance);

The `wgpuInstanceProcessEvents` that follows still flushes the queue each frame; the browser
presents the canvas automatically. Native is byte-identical (the gate excludes only Emscripten).

## Result — the triangle renders in headless Chrome

`./nob build hello-gpu wasm --gpu=webgpu` → GREEN (159/159).

Browser: Google Chrome for Testing 148 (playwright cache
`~/Library/Caches/ms-playwright/chromium-1223`), headless `--headless=new --enable-unsafe-webgpu
--use-angle=metal --use-gpu-in-tests --ignore-gpu-blocklist`, driven via `playwright-core` over CDP.
Serve: `python3 modules/build/web/serve.py apps/hello-gpu/build/wasm-debug 8741`. Driver clicks the
`hello-triangle` launcher button to open the GPU view.

Console after both fixes (clean — no abort, no signature mismatch):

    [WARN] [log] writer thread unavailable; draining synchronously on the calling thread
    [INFO] [gpu] webgpu instance created: 1 adapter ('WebGPU Adapter')
    [WARN] [gpu] device_create: feature 'descriptor_indexing …' not available; caps report the honest tier
    [INFO] [gpu] webgpu device created on 'WebGPU Adapter'
    [INFO] [gpu] webgpu swapchain ready: 616x424, format 27, vsync=1

`function signature mismatch count: 0`. No `wgpuSurfacePresent` abort. The canvas `#mel-gpu-26`
(616×424) shows the **3-color interpolated triangle** (red apex, blue bottom-left, green
bottom-right) on the dark clear background — the hello-triangle scene.

Screenshots: `/tmp/melody-wasm-render-fix/canvas2.png` (the rendered triangle, 616×424; copied to
`/tmp/melody-webgpu-proof/triangle-rendered.png`). The first run's `canvas.png` (pre-present-fix)
captured the swapchain-ready-but-present-aborting state for the record. Run logs:
`/tmp/melody-wasm-render-fix/run{1,2}.txt`. Driver (throwaway, outside the repo tree):
`/tmp/melody-wasm-render-fix/drive.mjs`.

The triangle drawing (not clear-only) means the app's shader target now branches to WGSL on the
WebGPU caps path — the SPIR-V-refusal "clear-only" gap the prior writeups flagged was resolved
separately on the app side; on this build the WGSL triangle pipeline compiles and draws.

## Native WebGPU regression — intact

After both edits, on macOS:

    DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1 ./nob test gpu-webgpu macos --gpu=webgpu
      → 4 passed / 0 failed / 0 skipped (of 4)
    DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1 ./nob test gpu-scene macos --gpu=webgpu
      → 2 passed / 0 failed / 1 skipped (of 3)

The 1 skip in gpu-scene is pre-existing (`quad` needs push constants, MissingFeature on WebGPU
core) — not a regression. Both fixes are native-byte-equivalent: #1 corrects a return type the
native path already ignored; #2 is `#ifndef __EMSCRIPTEN__`, so the native present is unchanged.

## Cross-file edits

**None outside `modules/gpu/src/webgpu/`.** Four files, all in lane:
`common.c`, `device.c`, `record.c`, `wgpu_backend.h` (7 insertions, 4 deletions total). The reactor
web tick callbacks (`reactor_web_raf_cb`/`reactor_web_timeout_cb`) were audited and are ABI-correct
(`double,void*→EM_BOOL` for RAF; `void*→void` for setTimeout); they are **not** the culprit and
were not touched.

## Kludges (confess all, MEL-ENGINE-VIII)

1. **`#ifndef __EMSCRIPTEN__` around the present, not a backend-split function.** It is a one-line
   gate inside `mel_gpu_frame_end` rather than a `surface_present()` abstraction with native/wasm
   bodies. Honest and minimal for a single call, but it is a platform `#ifdef` in the hot frame
   path; a future pass could route present through a backend hook if more of `record.c` diverges.
2. **The browser proof depends on the ms-playwright cache + an npx-resolved `playwright-core`**, and
   the driver lives in `/tmp` (throwaway, not committed). If those caches are pruned the recipe
   needs `npx playwright install chromium` first. Inherited from the log-web proof setup; not new
   debt I introduced, but it gates reproducibility.
3. **No automated test asserts the wasm render.** The triangle is proven by a manual headless-Chrome
   screenshot, not a CI check. A pixel-readback or a `function signature mismatch == 0` console
   assertion in a browser harness would lock this in; out of this task's scope (and there is no
   browser test lane in the repo yet).

## CLAUDE.md suggestions (recommendations only — not applied)

- Add a note to `modules/gpu`'s readme/spec: **poller/callback function pointers handed to the pump
  must match `Mel_Gpu_Poll_Fn` exactly — never cast a `void(void*)` into the slot.** The cast
  silently works on native and traps under wasm; this is the second time the wasm-typed function
  table caught a latent UB cast. A `static_assert`-style compile guard is not possible across TU
  boundaries, but the convention should be documented.
- Document in `modules/build/platforms.md` (or the gpu webgpu spec) that **emdawnwebgpu aborts on
  `wgpuSurfacePresent`** — present is implicit on the browser. Future readers porting more of the
  frame loop will hit this.

## Open questions for Gabbo

1. **Present abstraction.** Keep the `#ifndef __EMSCRIPTEN__` one-liner (kludge #1), or introduce a
   small `mel_gpu__surface_present(dev, surface)` backend hook now? Only one call diverges today.
2. **Browser render in CI.** Worth standing up a committed headless-WebGPU harness (driver +
   pixel-readback assertion) so the triangle render is a real test, not a manual screenshot? It
   needs the playwright Chrome provisioned on the runner.
3. **The favicon 404** in the console (`Failed to load resource: 404`) is cosmetic — the shell
   requests `/favicon.ico`. Add one to `shell.html`, or ignore? (Out of lane; flagging only.)
