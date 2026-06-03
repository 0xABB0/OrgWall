# GPU RHI M2 — D3D12 round 4: classic descriptor-heap slot reclaim + coverage

Closes the round-3 deferred item "classic D3D12 heaps have no slot reclaim". Off the comment-free tree.
Only `src/d3d12/**` + `test_d3d12.c` edited; no `include/**` change. Zero code comments (verified by grep
over every touched file). NOT verified on win-pilot this round (the box was unreachable over SSH from the
build environment — connection timed out); static review only. The orchestrator must build+run `gpu-d3d12`
on win-pilot.

## Work done

### 1. Classic descriptor-heap slot reclaim (future-gated, the mandate)

The classic CBV/SRV/UAV and sampler heaps were bump-only: `bind_group_destroy` freed the obj but never
returned its heap slots, so a create/destroy churn leaked heap space until device-destroy. Now reclaimed.

- **Free-list per heap.** A dynamic array of `Mel_Gpu_Classic_Block {base, count}` per heap
  (`classic_res_free` / `classic_smp_free`, growable, MEL-CODE-002 honored). `mel_gpu__classic_alloc` is
  first-fit over the free-list (splitting a larger block), falling back to the bump cursor; allocation
  failure (genuine exhaustion) is reported loudly with the in-use count (MEL-ENGINE-VIII, no silent
  overrun). `mel_gpu__classic_free_block` coalesces the returned block with adjacent free blocks, and if the
  coalesced block abuts the bump cursor it **retracts the cursor and drains** any further cursor-adjacent
  free blocks — so a fully-drained heap returns the cursor to 0 (no fragmentation residue on the common
  alloc/free-in-LIFO pattern). All under the existing `classic_lock`.

- **Future-gated retirement (§3.3, design L102).** `bind_group_destroy` no longer frees the heap block
  inline; it captures the obj's `{res_base,res_count}` / `{smp_base,smp_count}` and rides the **existing
  deferred-free watermark** via `mel_gpu__defer_free` (`Mel_Gpu_Deferred_Free` extended with
  `has_classic_res`/`classic_res` + the sampler pair). The block is stamped with `dev->submit_serial` (the
  latest issued serial) and returned to the free-list only when `mel_gpu__submit_complete` advances the
  watermark past it — i.e. when the last submission that could have bound the group has completed. A slot a
  live submission is sampling-from is therefore never reused (design L150/L423). This reuses the one
  retirement mechanism every other D3D12 resource destroy already uses (buffer/texture/pipeline), so there
  is one rule, not two (MEL-ENGINE-IX). If the group was never submitted, the stamp is `<= submit_completed`
  and the block frees immediately (the fast path in `defer_free`).

- **Lock order.** `mel_gpu__free_deferred_entry` (which now calls `mel_gpu__classic_*_free` → `classic_lock`)
  runs under `submit_lock` from `submit_complete`, and lock-free from `defer_free`'s immediate path. No path
  takes `classic_lock` then `submit_lock`, so the only order is submit_lock → classic_lock — no inversion.

### 2. Coverage (test_d3d12.c)

Five new tests. Reclaim is observable via two new internal hooks `mel_gpu__classic_{res,smp}_in_use`
(declared `extern` in the test, same pattern as the swapchain/reflect hooks; they take the opaque
`Mel_Gpu_Device*`, no struct exposed).

- `d3d12_bind_group.classic_slot_reclaim` — create→destroy→recreate; asserts in-use returns to baseline on
  destroy and the recreate reuses the freed block (the mandate's primary proof; no submission, so it proves
  the free-list/cursor reuse directly).
- `d3d12_bind_group.classic_churn_under_submission` — 8192 cycles of create→write→render(bind set)→
  copy-to-readback→submit→destroy, past the 4096-slot res cap. Without reclaim, `bind_group_create` fails by
  cycle ~4097; with reclaim each cycle returns to baseline (asserted in-loop) and the sampled pixel stays
  correct. This proves the future-gated path end-to-end on a real GPU (the destroy stamps the just-completed
  submission's serial; the block frees because the synchronous submit already advanced the watermark).
- `d3d12_bind_group.classic_fragmentation_coalesce` — exercises split (alloc a 4-wide block past a 2-wide
  hole), coalesce (merge a freed block with both neighbours), and the cursor-retract drain to 0.
- `d3d12_bind_group.classic_uniform_buffer` — the uncovered classic CBV binding case: a set with
  `SAMPLED_IMAGE` + `UNIFORM_BUFFER` + `SAMPLER`, tint multiply, pixel-verified. Exercises
  `bind_group_write_buffer`'s CBV path (round-3 only covered the SRV+sampler path).
- `d3d12_reflect.input_signature_indexed_semantic` — a 2-input VS with an **indexed** semantic
  (`TEXCOORD3`) and `float2`/`float4`; asserts the base semantic "TEXCOORD" + index 3 are split correctly and
  offsets/stride pack tight (0/8, stride 24). Complements round-3's single 3-input reflect test.

### 3. Bug-audit H2 — `caps.memory.persistent_map` silent capability lie (coordinator-flagged)

`caps.c:35` hardcoded `persistent_map = true` at the **adapter** stage — no device, no heap query, a silent
claim (MEL-CODE-007, MEL-ENGINE-VIII) that would lie on a device with no CPU-mappable UPLOAD heap. Fixed:
adapter caps now report `false` (honestly unknown without a device); `caps_refine_device` (which has the
`ID3D12Device*`) sets it from a **real probe** — create a 256-byte UPLOAD committed resource and `Map` it,
`true` only if the map succeeds. The transient is `Unmap`-ed and released immediately (MEL-ENGINE-III: tiny,
requested for a genuine verification, freed at once). On a real D3D12 device the UPLOAD heap is mandated, so
the probe returns `true` (same value as before — but now verified, not asserted). No test asserts this cap,
so nothing breaks. Chose the create+map probe over `GetCustomHeapProperties` to avoid the C-COBJMACRO
struct-return ABI fragility across in-box SDK versions.

### 4. Present (best-effort, static review only)

Per the mandate present needs an interactive win-pilot session, which I do not have. I performed a static
review of `swapchain.c` rather than make speculative changes I cannot verify:
- The flip-model present path (`frame_begin`/`frame_commands`/`frame_end` with COMMON→RENDER_TARGET→PRESENT
  barriers, per-frame allocator/list, `frame_serial`-gated allocator reset, vsync/tearing flags) is
  structurally complete and correct. I found **no present-path bug to repair**; no change made.
- One latent test-only risk (not production, not mine to rewrite): `mel_gpu__swapchain_readback_back` copies
  from `sc->buffers[sc->back_index]` after `frame_end` already `Present()`-ed that buffer; under
  `FLIP_DISCARD` a presented buffer's contents are undefined. The present test is environmentally **skipped**
  over SSH anyway (no DWM/desktop in the service window station), so this never executes there. Flagged for a
  future interactive-session pass; a clean fix is to read back the buffer *before* the final present, or to
  render+copy without presenting.

## Verification

**Not run on win-pilot** — the box was unreachable (`ssh win-pilot` timed out from the build environment).
I pushed the branch, confirmed I could not reach the box, and **deleted the remote branch** so no stray
branch remains on origin. The orchestrator must build+run on win-pilot:

    ssh win-pilot "cd /d D:\repo\OrgWall && git pull --ff-only && C:\Users\Gabbo\dev.cmd nob build gpu-d3d12"
    ssh win-pilot "cd /d D:\repo\OrgWall && C:\Users\Gabbo\dev.cmd nob test gpu-d3d12 win32 --gpu=d3d12"

Static review covered: type/signature correctness (`mel_realloc(alloc,ptr,size)` matches; compound literals
C99-valid; `Mel_Gpu_Device*` opaque-pointer externs in the test compile against the public headers), lock
ordering (no inversion), free-list arithmetic (hand-traced `classic_fragmentation_coalesce` step by step,
matches the asserts), reclaim watermark semantics (matches the existing buffer/texture destroy path), and
zero comments. Expected outcome: the 17 prior round-3 tests stay green; the 5 new tests pass; debug layer
clean; no `leak:` lines (the new free-list arrays are deallocated in `mel_gpu__classic_destroy`).

## Kludges and debt (confessed, MEL-ENGINE-VIII)

- **Reclaim is gated on the *latest issued* serial, not the precise last-consuming submission.** `defer_free`
  stamps `dev->submit_serial` (the conservative high-water mark), identical to every other D3D12 destroy.
  This can hold a block slightly longer than strictly necessary (until a later, unrelated submission also
  completes) but never frees too early. Tracking the exact consuming submission per bind-group would need
  per-CL bind-group usage tracking (the same "no per-CL classic state tracking" gap round-3 confessed);
  deferred. Correct and safe as-is.
- **First-fit, not best-fit.** Fragmentation is possible under adversarial variable-size churn (alloc a
  large block while only small holes exist → bump grows even though total free ≥ need). The cursor-retract
  drain mitigates the common LIFO case to zero residue; best-fit / a buddy scheme is the principled form if a
  real app stresses it. The heap still **errors loudly** on true exhaustion (no silent overrun).
- **The free-lists are still bounded by the fixed 4096/512 heap caps.** Reclaim removes the *leak*, but the
  heaps themselves are not grow-on-demand (a `ResizeDescriptorHeaps`-style re-create + re-register, or a
  chained heap, is the round-3-flagged next step). MEL-CODE-002 brush remains on the cap constants
  themselves; the free-list arrays are growable.
- **`bind_groups` slotmap entry is removed immediately, not deferred.** Only the *heap block* is
  future-gated. This is correct (the obj carries no D3D12 object, just integer bases captured by value before
  removal) but differs from buffer/texture which defer the slotmap reclaim too. A new bind group can take the
  reused slotmap index immediately; it allocates its own heap block. No hazard.
- **A bind group still alive at device-destroy leaks its heap block** (the leak-report path only reports,
  never frees). A leak is already an error the tests never trip; matches the existing posture.
- **clang-format not run** (absent on the dev mac, MEL-CODE-004); house style matched by hand.
- **The churn test does 8192 synchronous GPU submits** — a few seconds of wall time. It is a stress proof;
  acceptable for a once-per-merge run, but if CI time matters the count can drop to ~5000 (still > the 4096
  cap needed to prove reclaim).

### Rule carve-outs

- **Enums:** no new enum; `Mel_Gpu_Classic_Block` is a plain `{u32,u32}` struct. Existing protocol-mapping
  enums (`Mel_Gpu_Descriptor_Kind` → D3D12 range types) unchanged (MEL-CODE-001 protocol carve-out).
- **Comments:** none written.

## CLAUDE.md suggestions (recommendations only)

- Record the D3D12 classic-heap allocator shape in `modules/gpu/readme.md`: free-list of `{base,count}`
  blocks per heap, first-fit + bump fallback, coalesce + cursor-retract, slot return future-gated on the
  deferred-free watermark. (readme is still absent of a D3D12-classic-allocator note; round-3 flagged this.)

## Suggestions

- The `…_VK_FAILED → …_BACKEND_FAILED` rename round-3 flagged is still unfiled and the D3D12 backend keeps
  emitting `MEL_GPU_DEVICE_CREATE_VK_FAILED` / `MEL_GPU_BUFFER_CREATE_VK_FAILED` from a non-Vulkan backend —
  a single mechanical cross-backend pass.
- Grow-on-demand for both the classic heaps and the bindless heap is the one remaining structural gap for a
  long-running app that drives many bind groups or many bindless resources; it is the natural successor to
  this reclaim work (reclaim alone bounds steady-state, growth handles the high-water mark).
- An interactive win-pilot session would let the present path (and its readback-before-present fix) finally
  be verified; it is the last unproven D3D12 surface.
