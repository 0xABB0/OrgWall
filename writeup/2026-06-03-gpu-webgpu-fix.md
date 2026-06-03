# WebGPU backend red-team fixes (matrix task #19)

Verdict was SHIP-WITH-FIXES. Five findings addressed; one (LOW) deliberately deferred to the shared-goldens lane because `build.c` is shared this round.

## Work done

### 1. HIGH — `queue_submit` host-blocked to GPU completion on every submit
`modules/gpu/src/webgpu/queue.c`. The old path called `wgpuQueueOnSubmittedWorkDone` then `mel_gpu__drain_until` inline, so every submit spun the instance until the GPU finished — no CPU/GPU pipelining (MEL-ENGINE-III/VI), and no re-entrancy guard, so submitting from the device reactor thread would re-enter the pump and deadlock.

Reworked to mirror the Vulkan backend's pump/no-pump split:
- **Reactor present (`dev->pump != NULL`):** heap a `Mel_Gpu_Work_Done` context `{dev, future, serial, buffers, buffer_count, heap_buffers}`, register `wgpuQueueOnSubmittedWorkDone` with the async callback, and return immediately. The standing per-instance poller registered at `device.c:151` (`mel_gpu__instance_pump_tick` → `wgpuInstanceProcessEvents`) fires the callback on the next reactor tick; the callback releases the command buffers, marks the serial complete, resolves the future, and frees the context. This is the spec §3.3 "pump on tick" completion source. No in-submit host-block.
- **No reactor (`dev->pump == NULL`, e.g. headless tooling / startup):** the `*_sync` path — register the sync callback against a stack context, then `mel_gpu__drain_sync` (see fix 3) which debug-asserts it is **not** on the reactor thread before pumping, then retire inline.
- **Zero-buffer submit:** resolves the future immediately (matches the Metal backend), no callback registered.

Command buffers must outlive the call on the async path; when the count is ≤8 they were on a stack array, so the async branch copies them into a small heap array owned by the context (freed in the retire helper). `mel_gpu__submit_retire` is shared by both paths.

### 2. MED — `buffer_mapped` sticky one-shot map
`modules/gpu/src/webgpu/resources.c`. The old code mapped once, set `mapped=true`, and never unmapped: a second call returned the frozen first snapshot, and a second `copyTextureToBuffer` into the still-mapped buffer failed Dawn validation ("used in submit while mapped"). Metal/Vulkan return a coherent persistent pointer; WebGPU silently diverged (MEL-ENGINE-VIII/IX).

**Contract decision — shadow copy.** `mel_gpu_buffer_mapped` now maps → `mel_gpu__drain_sync` → `memcpy` the mapped range into a per-buffer device-`alloc` shadow → **unmaps** → returns the shadow pointer. This makes WebGPU behave like the persistent-pointer backends: each call returns a stable pointer reflecting the buffer's current contents, and the GPU buffer is **never left mapped**, so any subsequent GPU copy into it validates. The shadow is allocated lazily on first map and freed in `buffer_destroy` (visible, traceable cost — MEL-ENGINE-III). The `mapped` bool field was removed from `Mel_Gpu_Buffer_Obj` and replaced by `void* shadow`.

This was iterated: the first attempt (unmap-then-remap on the next `mapped()` call) still left the buffer mapped between a read and the next GPU submit, which the new regression test caught immediately. The shadow approach is the correct cross-backend-coherent form.

### 3. MED — silent timeout give-up
`modules/gpu/src/webgpu/common.c`. Added `mel_gpu__drain_sync(dev, done, what)`:
- Debug-asserts `!mel_reactor_is_owner(dev->reactor)` with a loud `mel_log_error` before asserting (re-entrant pump deadlock; spec §3.3 `*_sync` is off-reactor only).
- On timeout (the ~10s spin in `mel_gpu__drain_until` exhausting without the callback) it logs loudly, naming the operation and distinguishing timeout from a fired-but-failed callback (the callback itself logs failure). Both `queue_submit` (sync path) and `buffer_mapped` route through it.

`mel_gpu__drain_until` keeps its raw signature for the adapter/device bootstrap drains in `instance.c`/`device.c`, which run before any pump exists and are inherently off-reactor.

### 4. LOW — raw `calloc`/`free` in pipeline vertex attrs
`modules/gpu/src/webgpu/pipeline.c`. Replaced `calloc`/`free` with `mel_alloc_array(dev->alloc, …)` / `mel_dealloc(dev->alloc, …)` (MEL-CODE-003). Each attribute is now initialized with a full compound literal so `nextInChain` is NULL (the old `calloc` zeroed it; `mel_alloc_array` does not). Dropped the now-unused `<stdlib.h>` include.

### Test added
`modules/gpu/test/test_webgpu.c` — `webgpu_resources.mapped_remaps_per_call`: clears a 64×64 RT red, copies to a readback buffer, reads it, then clears the same RT green, copies into the **same** readback buffer, and reads again. Asserts the first read is red and the second is green. Under the old sticky-map this fails (frozen snapshot + "used in submit while mapped" validation error). 64×64 chosen because WebGPU requires `copyTextureToBuffer` bytesPerRow be a multiple of 256 (RGBA8 → 256-byte row).

## Green runs
- `gpu-webgpu macos --gpu=webgpu`: **4/4** (was 3/3 + the new test).
- `gpu-foundation macos --gpu=webgpu`: **13/13**.
- `./nob build hello-gpu wasm --gpu=webgpu`: **links** (148/148). The async/sync WorkDone callbacks carry the `#ifdef __EMSCRIPTEN__` signature variants; the wasm drain branch stays `emscripten_sleep`/ASYNCIFY via `mel_gpu__drain_until`.

## Header additions flagged
None to shared/public headers. The only header touched is the backend-private `modules/gpu/src/webgpu/wgpu_backend.h`:
- `Mel_Gpu_Buffer_Obj`: `bool mapped` → `void* shadow`.
- Declared `bool mel_gpu__drain_sync(Mel_Gpu_Device*, const bool*, const char*)`.

No public `mel_gpu_buffer_unmap` was needed — the shadow-copy contract keeps map/unmap internal, so the shared `gpu/buffer.h` was not touched.

## Kludges / debt (MEL-ENGINE-VIII, confess all)
- **In-flight async submits leak if the device is destroyed before the pump ticks them out.** On the reactor path the heap `Mel_Gpu_Work_Done` context (and its copied command-buffer array) is freed only by the WorkDone callback. If `mel_gpu_device_destroy` runs with submits still in flight, those contexts and the futures leak and never resolve. This is the same shape as the Vulkan backend (which pushes to `dev->pending` and does not force-drain at destroy); spec §3.3 makes retirement future-gated, i.e. the app is expected to quiesce before teardown. I did **not** add a destroy-time drain — it needs reactor-affinity care and is out of this task's scope. Flagged for a follow-up that drains pending submits across all backends uniformly.
- **`buffer_mapped` shadow doubles readback memory** (one device-side WGPU buffer + one host shadow per readback buffer). This is the price of a coherent persistent pointer on a backend whose map is a transient CPU-owned snapshot. Cost is visible and freed on destroy; acceptable, but noted.
- **No test exercises the reactor-driven async submit path or the off-reactor assert.** The headless test harness creates devices with no reactor (`test_make_device` passes no `.reactor`), so only the `*_sync` branch is covered by tests; the async pump-tick branch and the `mel_gpu__drain_sync` reactor-owner assert are proven only by the wasm link + code review, not a unit test. Constructing the async path deterministically needs a device on a driven reactor (threaded reactor + cross-thread affinity), which is heavy and flaky for a unit test; deferred.

## Deferred (LOW #5) — `img_golden.c` dead-linked into gpu-webgpu
`build.c:196` links `test/img_golden.c` into `gpu-webgpu`, but `test_webgpu.c` uses only the `Mel_Golden_Tolerance` *type* (from the header) and never calls `mel_golden_check`/`MEL_GOLDEN`, so the object is dead-linked (harmless — no unresolved symbols). The task says `build.c` is shared this round; **I left it and flag it** rather than touch the shared file. Removal is a one-line drop of `mel_sources(wgputest, ALWAYS, "test/img_golden.c");` — leave for the shared-goldens agent, or wire a real golden assert.

## CLAUDE.md suggestions
None.

## Suggestions
- A uniform cross-backend `mel_gpu__device_drain_pending` called from `device_destroy` would close the in-flight-submit leak window for both Vulkan and WebGPU in one place (MEL-ENGINE-IX), and let a future `*_sync` device shutdown be honest about outstanding work.
- The webgpu test harness would benefit from a small helper that spins up a driven reactor so the async completion path gets real coverage; worth it once the destroy-drain lands.

## Open questions for Gabbo
- Is the shadow-copy contract for `buffer_mapped` the one you want as the canonical WebGPU readback semantics, or would you prefer the buffer stay mapped and expose a public `mel_gpu_buffer_unmap` across all backends (which would change the shared `gpu/buffer.h` surface)? I chose shadow to keep the public API unchanged and match Metal/Vulkan's persistent-pointer behavior.
- The in-flight-submit leak at device destroy is pre-existing in shape (Vulkan has it too). Do you want a destroy-time pending-drain added now (cross-backend), or is "quiesce before destroy" the accepted contract?
