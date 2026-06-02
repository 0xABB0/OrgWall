# GPU RHI — agent-team round 3 + integration

Scope (Gabbo's call): **follow-ups only** (the 5-set bindless restructure deferred to its own
coordinated pass), and **comments stripped** to honor the global "never write comments" rule.
Two-phase: a comment-strip pass first, then a feature team on the comment-free tree.

## Phase 0 — comment strip (`b0ab013`)
A lexer-based pass removed every hand-authored code comment from `modules/gpu` + `apps/hello-gpu`:
**123 files, 1767 pure-comment + 319 trailing lines removed**, proven comments-only by
`tokenize(before) == tokenize(after)` over all 123 files (0 mismatch). `_spv.h` payloads byte-identical
(leading banners only). All six suites stayed green. Generated-artifact provenance comments inside the
two test `_spv.h` (the GLSL regeneration source) were **kept** (orchestrator call — provenance, not logic
comments; also mirrored in writeups). Every Phase-1 agent then wrote zero comments.

## Phase 1 — feature team (all off the stripped tree, zero comments)
- **builder3** (`src/vulkan`): **GPU timestamp queries** (`include/gpu/query.h` + `query.c`: query pools,
  `cmd_write_timestamp`, `cmd_reset_query_pool`, `query_pool_resolve` → ns via `timestamp_period_ns`;
  MoltenVK 1.4.1 grants them, 1.0 ns/tick, measured ~117 µs for a 4 MiB copy). `caps.raster.fill_mode_non_solid`
  surfaced. `cmd_begin_rendering` now **auto-transitions** its color/resolve/depth attachments (fixes the
  VUID the round-2 screens hit; manual barrier stays valid as the P2 escape). Also fixed a real silent
  default: `caps.queries.timestamp_compute_and_graphics` was documented-probed but never written
  (MEL-CODE-007) — now reads the limit. `gpu-vulkan` 37→40.
- **d3d12-3** (`src/d3d12`, win-pilot): storage-image (UAV-texture) bindless class; the **classic
  descriptor-set path** (`set_layouts`/bind groups — no longer `MissingFeature`); the first `reflect.c`
  input-signature test. `gpu-d3d12` **16/0/1-skip of 17** on RTX 2060 SUPER, break-on-error armed (the 1
  skip is the unchanged environmental swapchain-present skip).
- **redteam3** (`test_concurrency.c`/`test_stress.c`): proved BUG-2's tracker end-to-end (24/24 cross-thread
  violations reported, process survives, **zero false positives**); hardened BUG-1 with three tougher
  storms (mixed-type churn, interleaved submit+destroy, sampler-dedup RMW); perf-sanity ~1.05× at 8 threads
  (no new cliff). **No new bugs.** `gpu-concurrency` 7→10, `gpu-stress` 16→20.
- **screens3** (`apps/hello-gpu`): three technique-combining demos — **raymarch-sdf** (SDF raymarcher,
  soft shadows + AO + Fresnel), **mandelbrot-explorer** (compute → storage-image → bindless present, sized
  via `mel_gpu_swapchain_extent`), **gpu-boids** (ping-pong compute flock → instanced draw). 17 screens total.

## Verification (integrated `main`, Apple M3 Pro / MoltenVK, validation on)
gpu-foundation 8/8 · gpu-vulkan **40/40** · gpu-stress **20/20** · gpu-concurrency **10/10** ·
gpu-visual 11/11 · gpu-bench 12/12 = **101 tests**; 17/17 hello-gpu screens clean; tree-wide comment
grep clean; zero VUID/leak/unexpected-validation. D3D12 to the extent SSH allows on win-pilot.

## Deferred / needs Gabbo (unchanged or new)
- **5-set bindless restructure** (the only path to grow-on-demand) — still its own coordinated pass.
- **Async query resolve & async transfer upload** — both are the spec's async path (resolve-to-buffer
  future / transfer-queue upload); the round-3 timestamp resolve is a Vulkan-only synchronous helper.
- **D3D12 present** needs an interactive win-pilot session; **classic D3D12 heaps** have no slot reclaim.
- `shaderFloat64` at device-create would turn mandelbrot into a real deep-zoom explorer.
- Retire/rename the now-false-premise test `conc_tracker.device_accepts_flag_but_tracker_is_unwired`.
- `…_VK_FAILED → …_BACKEND_FAILED` cross-backend rename (still a single mechanical pass, unfiled).
