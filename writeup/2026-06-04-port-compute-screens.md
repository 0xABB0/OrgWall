# Port compute/sim gallery screens to dual-lane runtime Slang (task #35 batch G1)

## Work done — what changed, and why

Ported the four remaining COMPUTE/sim gallery screens — `reacdiff`, `compute_plasma`,
`particles`, `dispatch_indirect` — from precompiled GLSL→SPIR-V `*_spv.h` bundles to
single-source dual-lane runtime Slang (`#embed` + `mel_gpu_pipeline_*_create_from_slang`),
matching the proven mandelbrot/bloom/boids pattern. Each `.slang` is multi-entry with a
single unified `Root` push-constant struct (the Metal RHI binds the from-slang argument
buffer at fixed `[[buffer(0)]]`, so all entries of a screen must share one Root) and the
`#if defined(MEL_TARGET_METAL)` split:

- **Metal lane:** resources are first-class `DescriptorHandle<T>` fields in `Root`
  (storage image, storage buffer, sampled texture, sampler), lowered to a per-dispatch
  inlined argument buffer.
- **Vulkan/WGSL lane:** the original `uint` slot fields + `[[vk::binding(N,0)]]` unbounded
  heap arrays (binding 0 = sampled textures, 1 = samplers, 2 = storage buffers,
  4 = `[format("rgba8")]` storage images). `NonUniformResourceIndex` kept on graphics-lane
  reads (matching the original `nonuniformEXT`), dropped on compute-lane writes per the
  established convention.

Migrated each screen `.c` to `#embed` the `.slang` and build pipelines through
`from_slang`; removed the now-orphaned `*_spv.h` includes and deleted the ten orphaned
headers (`plasma_spv.h`, `particle_sim_spv.h`, `particle_draw_spv.h`, `reacdiff_init_spv.h`,
`reacdiff_step_spv.h`, `reacdiff_draw_spv.h`, `cull_spv.h`, `buildargs_spv.h`, `shade_spv.h`,
`clear_spv.h`). `instances_spv.h` (still used by `instances.c`/`msaa.c`) and `blit_spv.h`
(still used by `gallery.c`/`layers.c`/`msaa.c`/`postprocess.c`) were left intact.

Per-screen notes:
- **compute_plasma** — one storage buffer (`float4[]`): compute writes a plasma colormap,
  instanced quad draw reads it. `cs_main`/`vs_cells`/`fs_cells`.
- **particles** — 40k-particle storage buffer (`{float4 pos_life; float4 vel}`): compute
  integrates toward a moving attractor, additive-blended instanced quad draw. The same
  blend (`SRC_ALPHA`/`ONE` add) is carried to the from-slang graphics pipeline.
  `cs_sim`/`vs_draw`/`fs_draw`.
- **reacdiff** — Gray-Scott ping-pong: `cs_init` seeds, `cs_step` reads a sampled texture +
  sampler and writes the storage image, `vs_draw`/`fs_draw` colormaps the latest field. The
  old separate `blit.vert` vertex was inlined as `vs_draw`.
- **dispatch_indirect** — GPU-driven cull→buildargs→indirect-shade: `cs_cull` atomically
  appends survivors of a moving cull region (`InterlockedAdd` on an `RWStructuredBuffer<uint>`
  modelling `{count; pad; idx[]}`), `cs_args` turns the count into `{gx,1,1}`, `cs_clear`
  paints a gradient storage image, `cs_shade` (dispatched indirectly) splats survivor disks.

Added the four screens as gpu-scene scenes in `test_scene.c` (one contiguous, comment-fenced
block to ease union-merge with the sibling batch), each diffed against a macOS-Vulkan-oracle
golden in `golden/shared/<screen>.ppm` (minted by the Vulkan run; the eight pre-existing
goldens were untouched — byte-identical on re-mint).

## The dispatch_indirect determinism fix (shade kernel)

As authored from the original GLSL, `cs_shade` was an **unsynchronized additive
read-modify-write image scatter** from concurrently-dispatched survivor threads over
overlapping disk footprints — a data race, compounded by atomic-ordered survivor
compaction. Measured: the SAME Vulkan backend diverged run-to-run by max channel delta ~129
over ~1350/4096 pixels. No stable golden is possible for a race.

Fixed without changing the visual in expectation: `cs_shade` now runs as a single invocation
(`SV_GroupID==0 && SV_GroupThreadID==0` guard) that loops agents **in index order**,
recomputes the cull predicate inline (so it no longer depends on the nondeterministic
survivor-list ordering), and splats survivors serially. The indirect dispatch is still
genuinely exercised — `cs_args` computes `gx` from the GPU-side survivor count and
`cmd_dispatch_indirect` consumes it; extra workgroups are guarded no-ops. The serial
single-thread accumulation is race-free and deterministic; the `cull_r/cull_x/cull_y` were
added to the shade Root (host updated in both the screen and the scene). Verified
deterministic across runs on both Vulkan and Metal (each mints then asserts green against its
own backend's output).

## PROOF — gpu-scene per backend

- `--gpu=vulkan` (oracle, mints the shared goldens): **13/13**, deterministic across two
  independent assert runs. The four new scenes assert against their freshly-minted goldens
  with the strict Vulkan band (delta≤2, frac 0).
- `--gpu=metal`: **12 passed / 1 skipped**. `compute_plasma`, `particles`, `reacdiff` all
  **RENDER on Metal** and diff bit-close to the Vulkan oracle goldens through the RHI (within
  the non-Vulkan band; no skip — compute + bindless from-slang now work for these). The
  three pre-existing screens (mandelbrot/bloom/boids) stay green. `dispatch_indirect` is an
  honest Metal skip — see the RHI gap below.
- `--gpu=webgpu`: **3 passed / 10 skipped**. The four new screens skip honestly (the WGSL
  emit of every device-global-heap pass fails with `E36107: unavailable features in entry
  point`, and the device lacks the bindless heap) — identical degradation to bloom/mandelbrot.

Other suites: **gpu-metal 12/12, gpu-vulkan 48/48, slang-compile 10/10.**
hello-gpu builds+links on macos metal/vulkan/webgpu.

Offline cross-emit (slangc 2026.10.2, the pinned vendored toolchain): every entry of all four
shaders emits clean SPIR-V (Vulkan lane) AND MSL that compiles to AIR via
`xcrun -sdk macosx metal -c` (Metal lane). WGSL emits for the non-bindless entries; the
bindless-heap entries fail WGSL exactly where WebGPU skips.

## RHI gap (FLAGGED — gpu backend src, out of this task's ownership)

`mel_gpu_cmd_dispatch_indirect` in `modules/gpu/src/metal/macos/record.m` does **not** build
the per-dispatch from-slang bindless argument buffer that `mel_gpu_cmd_dispatch` builds
(`record.m:584-589`, `mel_gpu__compute_argbuffer_plan` + `mel_gpu__build_compute_argbuffer`).
So an indirectly-dispatched from-slang **bindless** compute kernel runs on Metal with **no
bound bindless resources** — `cs_shade` reads/writes nothing and the survivor splats never
land (the Metal image came back without them; confirmed by ASCII-diffing the Metal-produced
image against the Vulkan golden — the survivor disk is entirely absent). `cull`/`buildargs`/
`clear` (plain `cmd_dispatch`) all run correctly on Metal; only the indirectly-dispatched
shade is blocked.

The fix is mechanical and matches the existing pattern — lift the same compute-argbuffer
build into `cmd_dispatch_indirect` — but that file is gpu backend src, which this task forbids
touching. So `dispatch_indirect` is an honest Metal `MEL_SKIP` with the precise reason,
rather than a forced/loosened/flaky diff (MEL-ENGINE-VIII). The Vulkan oracle proves the
algorithm end-to-end; once the RHI builds the arg buffer in `cmd_dispatch_indirect`, the
Metal skip can be removed and it will render like the other three.

## Format-enum growth

None. All four screens fit the existing vertex/instance path; no `gpu/format.{h,c}` change was
needed (these screens pull vertices procedurally via `SV_VertexID`/`SV_InstanceID` and read
instance data from storage buffers — no new vertex format).

## Kludges / debt (MEL-ENGINE-VIII: confess all)

- **dispatch_indirect Metal skip.** Not a code shortcut but a real RHI gap (above). The
  scene degrades honestly on Metal; nothing is faked.
- **dispatch_indirect shade serialized to one invocation.** To get a deterministic golden the
  shade pass is single-threaded (group 0, thread 0) over all agents in index order. This is a
  faithful, race-free re-expression of the same splat set (visual identical in expectation),
  but it is slower than a parallel scatter would be (MEL-ENGINE-III: a visible, deliberate
  cost). The original parallel scatter was never correct (a data race); this is the honest
  deterministic form. A truly parallel race-free version would need an integer storage image +
  `InterlockedMax`/`InterlockedAdd` atomics (r32 format) — a larger change, deferred.
- **Per-screen inlined dual-lane block.** Each `.slang` repeats the `#if MEL_TARGET_METAL`
  Root/accessor split (no shared prelude — the wrapper compiles a single source string with no
  include path, as documented in the prior bindless-dual-lane writeup). Inlining is the honest
  form here; a dead unincludable prelude would be slop.
- **clang-format not auto-applied.** The host clang-format (v22) disagrees globally with the
  committed style (it would un-align assignments and churn unrelated code, as the prior
  writeup notes). New lines were hand-matched to the surrounding committed style; no residual
  diff in any added line attributable to formatting.

## CLAUDE.md suggestions

None.

## Suggestions

- **Close the Metal `cmd_dispatch_indirect` bindless gap.** Lift the
  `mel_gpu__compute_argbuffer_plan` + `mel_gpu__build_compute_argbuffer` call from
  `cmd_dispatch` (record.m:584-589) into `cmd_dispatch_indirect` (record.m:594) before the
  `dispatchThreadgroupsWithIndirectBuffer:`. Symmetric to the direct path; would make
  dispatch_indirect render on Metal (drop the scene skip). Small, isolated, gpu-team-owned.
- **A race-free parallel scatter primitive** (integer storage image + image atomics) would let
  dispatch_indirect's shade go parallel again while staying golden-stable — worth it if a
  workload ever needs many survivor splats per frame.

## Open questions for Gabbo

1. Want the Metal `cmd_dispatch_indirect` bindless-argbuffer fix landed (it's the one-liner
   that unblocks dispatch_indirect on Metal), or keep it as a tracked gap for the backend
   owner?
2. The dispatch_indirect shade was serialized to make a deterministic golden possible. Accept
   the serial form as the canonical screen behavior, or should the screen keep the original
   parallel (racy) scatter for visual liveliness and the golden test instead diff at a coarse
   tolerance / be dropped?
