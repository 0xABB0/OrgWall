# GPU RHI M2 — agent-team round 2 + integration

A second, larger agent team (8 agents incl. a mandatory **fixer**) advanced `design/gpu-rhi.md`
from round-1 `main` (`1274225`). All branches integrated into `main` with a green gate after each
merge. Per-track detail is in the sibling round-2 writeups; this is the integrated picture + the
decision ledger that needs Gabbo.

## Tracks (merge order)
- **fixer** — kludge ledger. Landed 4/5: frame-recorder U17 reset (kills the round-1 `first_frame`
  workaround), `mel_gpu_swapchain_extent`, per-upload fence (no more queue-wide `WaitIdle`), dynamic
  `queue_submit` array. **#1 grow-on-demand bindless: genuinely blocked on MoltenVK 1.2** (proved:
  VUID-08600 on layout-count growth; `VARIABLE_DESCRIPTOR_COUNT` is last-binding-only so can't cover
  the sampler class; front-loading the ceiling ≈29 MB violates MEL-ENGINE-III). Left the round-1
  loud-status stop-gap intact. **Needs Gabbo (below).**
- **d3d12** — Phase 4 DXGI flip-model swapchain (`src/d3d12/`), built on win-pilot: 13/13 prior green
  + 1 new swapchain test that **skips** because the SSH service window-station can't create any DXGI
  swapchain (`DXGI_ERROR_INVALID_CALL`). Inert on macOS (gated `--gpu=d3d12`).
- **redteam** — multi-threaded concurrency suite (`gpu-concurrency` 7/7) + stress. Measured the
  slotmap at **0.81× on 8 threads** (single `obj_lock`, not the spec's lock-free MPMC). Found two
  new bugs and left a deterministic red guard for BUG-1.
- **visualizer** — `gpu-visual` 4→11: storage-image bindless, MSAA-resolve edge (reads 128 at a
  half-covered edge — the AA signature), dispatch-indirect, depth, MRT, wireframe, sync2.
- **screens** — 4 new hello-gpu screens (msaa / dispatch-indirect / particles / prepass) + the
  caps-FPS HUD seam (`gpu_host_set_status`). 14 screens total, all validation-clean.
- **scribe** — the long-absent `modules/gpu/readme.md` (+ binding-model contract). Caught stale
  facts in the round-1 writeups (indirect-import family mostly unimplemented; `HELLO_GPU_AUTO=triangle`
  is the else-branch).
- **profiler** — `gpu-bench` 12/12 throughput/latency/allocator harness. Independently measured the
  buddy suballocator at **1.62×** (corroborates the allocator-rounding work). Notes: real GPU-time
  needs timestamp-query pools (unimplemented).
- **patcher** (the 8th, spawned after red-team) — **fixed BUG-1 + BUG-2**:
  - **BUG-1 (HIGH, host-memory corruption)**: `mel_gpu__table_get` returned an interior pointer into
    the packed slotmap; ~43 sites dereferenced it after `obj_lock` released → concurrent insert/remove
    relocates the record → wrong buddy range freed → two live buffers alias VRAM (§3.7 says
    create/destroy is `Concurrent`). Fix: `mel_gpu__table_get_copy` (memcpy under the lock); ~43 sites
    converted. Guard `threaded_overlap_storm` went 8/8 red → **PASS over 19 consecutive runs**.
  - **BUG-2 (MEDIUM)**: the §3.7/U21 thread-safety tracker was allocated but never invoked. Wired
    enter/exit into the load-bearing families (destroy, mutate, recording, submit), changed
    assert-abort → loud `log_error` (survives the NOFORK runner), added a negative probe.

## Verification (integrated `main`, Apple M3 Pro / MoltenVK 1.2.334, validation on)
gpu-foundation 8/8 · gpu-vulkan **37/37** · gpu-stress **16/16** · gpu-concurrency 7/7 ·
gpu-visual 11/11 · gpu-bench 12/12 = **91 tests**; hello-gpu 14/14 screens clean; zero
VUID/leak/unexpected-validation. D3D12 verified to the extent SSH allows on win-pilot.

## Needs Gabbo (decisions, not blockers)
- **Grow-on-demand bindless = a 5-separate-sets variable-count heap restructure** — cross-cutting:
  every bindless shader (`set=0,binding=N` → `set=N,binding=0`), `reflect.c`, and the hello-gpu app
  shaders. Schedule as its own coordinated pass with the shader authors. Probe evidence in
  `writeup/2026-06-02-gpu-rhi-m2-fixer.md`.
- **D3D12 present-path verification** needs an **interactive** win-pilot session (console / scheduled
  task on `WinSta0`); it cannot be machine-checked over the SSH service window station.
- **Comments vs Rule-#1** — every agent matched the module's dense house comments and flagged the
  global "never write comments" rule; awaiting one ruling to keep `modules/gpu` consistent.
- **Slotmap is a per-device mutex, not lock-free MPMC** (§3.7/U1 deviation, 0.81× at 8 threads) —
  correct but unscalable; left as a documented deviation, not fixed.

## Open follow-ups (catalogued, unfixed)
- `cmd_begin_rendering` should transition the attachments it names (incl. `resolve_view`) or document
  that it doesn't (screens hit a VUID and barriered manually).
- Surface `caps.raster.fill_mode_non_solid` (recorded internally, no public flag).
- GPU-time profiling via timestamp-query pools; async transfer-queue upload (the WaitIdle-free real
  fix); per-individual-`cmd_*` foreign-thread tracker detection; `mel_gpu_swapchain_acquire_texture`
  for D3D12 to drop the test backdoor; `…_VK_FAILED → …_BACKEND_FAILED` rename (cross-backend).
