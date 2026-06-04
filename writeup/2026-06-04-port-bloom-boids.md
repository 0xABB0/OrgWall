# Port bloom (5-pass) + boids (compute-sim → instanced draw) to dual-lane runtime Slang (task #35 batch 2)

## Work done — what changed, and why

Ported the two screens Gabbo named that still did not render on Metal — `bloom` and `boids` —
to single-source dual-lane runtime Slang, so both render on Metal AND Vulkan (and skip honestly
on WebGPU). Both RENDER on Metal as cross-backend gpu-scene proofs, bit-identical to the
macOS-Vulkan oracle on this host.

### Shaders authored (single-source, multi-entry, dual-lane bindless)

- `apps/hello-gpu/shaders/slang/bloom.slang` — five entries: `cs_scene` (compute, sphere
  raymarch → storage image), `cs_bright` (compute, sampled-tex luminance threshold → storage
  image), `cs_blurx`/`cs_blury` (compute, separable 7-tap gaussian, sampled-tex → storage
  image), `vs_composite`/`fs_composite` (fullscreen-triangle graphics, two sampled textures +
  sampler → Reinhard tonemap + gamma). Translated from `shaders/bloom_{scene,bright,blurx,
  blury,composite}.{comp,frag}` + `shaders/blit.frag`.
- `apps/hello-gpu/shaders/slang/boids.slang` — three entries: `cs_sim` (compute flock:
  sep/align/cohere + goal, reads a `StructuredBuffer<Boid>`, writes an `RWStructuredBuffer<Boid>`),
  `vs_draw`/`fs_draw` (instanced triangle draw vertex-pulling a `StructuredBuffer<Boid>` by
  `SV_InstanceID`, oriented by velocity, hue by heading). Translated from
  `shaders/boids_{sim.comp,draw.vert}` + `shaders/instances.frag`.

Each non-WGSL entry emits to SPIRV + MSL and the MSL compiles to AIR (`xcrun metal -c`,
verified offline for all 9 entries). The dual-lane split matches the established pattern
(`mandelbrot`/`bindless_present`): `#if defined(MEL_TARGET_METAL)` uses `DescriptorHandle<T>`
fields in the push-constant `Root` (storage image, sampled texture, sampler, structured
buffers); the Vulkan/WGSL lane keeps the hand-placed `[[vk::binding(N,0)]]` heap arrays
(0=textures, 1=samplers, 2=storage buffers, 4=storage images) + `uint` slots. Slang inlines
the `mel_*()` accessor wrappers, so the SPIR-V lane is the original binding model.

### The decisive new find — multi-entry shaders need ONE shared push-constant Root on Metal

The Metal from_slang path binds the inlined argument buffer at a FIXED buffer index
(`MEL_GPU_METAL_PUSH_CONSTANT_INDEX = 0`) and builds it via
`newArgumentEncoderWithBufferIndex:0`. That holds only when the source has a SINGLE
`[[vk::push_constant]]` struct (mandelbrot, bindless_present). My first cut gave each pass its
own distinct Root struct; Slang's global program layout then assigned each push constant a
DISTINCT Metal buffer index in declaration order — `cs_scene` Root at `[[buffer(0)]]`, but
`cs_bright` at `[[buffer(1)]]`, `cs_blurx` at `[[buffer(2)]]`, `fs_composite` at `[[buffer(3)]]`
(boids: `cs_sim` at 0, `vs_draw` at 3). The RHI's `newArgumentEncoderWithBufferIndex:0` then
fired a Metal assertion ("bufferIndex 0 does not identify an argument buffer") at pipeline
create for every pass past the first.

Probed (libslang 2026.10.2): a SINGLE shared `Root` push constant lands at `[[buffer(0)]]` for
EVERY entry, and the full struct (all resource fields) survives in each entry's argument buffer
even when that entry uses only a subset. So both shaders were re-authored onto ONE unified
`Root` per screen:
- bloom: `{ tex0, tex1, smp, img, w, h, param0, param1 }` (two sampled textures, a sampler, a
  storage image; `param0` carries time/threshold/strength per pass).
- boids: `{ src, dst, total, pad, dt, time, goal_x, goal_y, aspect, pad1 }` (a read
  StructuredBuffer + a write RWStructuredBuffer).

Each pass fills its subset; unused resource handles point at a live, registered slot (the Metal
argument-buffer encoder resolves every resource field to a live `id<MTLResource>` and loud-fails
on an unregistered slot, so every field must be valid). This keeps the deliverable's "one
`.slang` per screen, multi-entry" and needs NO change to the from_slang core, the slang
wrapper, or the RHI's buffer-index convention.

### Screen migrations (drop the *_spv.h)

- `apps/hello-gpu/src/bloom.c` — `#embed`s `bloom.slang`; 4 compute pipelines via
  `mel_gpu_pipeline_compute_create_from_slang` + 1 graphics via
  `mel_gpu_pipeline_create_from_slang` (all `.bindless = true`); unified `Bloom_Root`; render
  now calls `mel_gpu_cmd_bind_bindless` after every `bind_pipeline` (the original used precompiled
  bundles on the manual heap and never bound bindless on Metal). Dropped 6 `*_spv.h` includes.
- `apps/hello-gpu/src/boids.c` — `#embed`s `boids.slang`; compute-sim via the compute
  from_slang variant, instanced draw via the graphics from_slang variant; unified `Boids_Root`;
  `cmd_bind_bindless` added. Dropped 3 `*_spv.h` includes.
- Removed the 7 now-orphaned headers (`bloom_{scene,bright,blurx,blury,composite}_spv.h`,
  `boids_{sim,draw}_spv.h`); `blit_spv.h` / `instances_spv.h` stay (other screens use them).
- `apps/hello-gpu/build.c` — unchanged; `--embed-dir=apps/hello-gpu` already covers the new
  `.slang`.

### Format enum (deliverable #3) — NOT grown, and why

Boids' instanced draw needs NO vertex/instance format beyond F32. The triangle is
VERTEX-PULLED from the storage heap by `SV_VertexID` (3 verts) + `SV_InstanceID` (one boid per
instance); there is no vertex buffer and no instance buffer, so no `Mel_Gpu_Format` is consumed.
`modules/gpu/include/gpu/format.{h,c}` is untouched. FLAGGED as evaluated-and-not-needed.

### PROOF — gpu-scene bloom + boids, rendered on Metal, vs the Vulkan oracle

`modules/gpu/test/test_scene.c` gained `scene_shared.bloom` and `scene_shared.boids` (64×64,
`scene_make_device_bindless`, fixed `time=0` for determinism). bloom runs the full 4-compute +
1-graphics chain (storage-image writes, sampled-tex reads through the heap, separable blur,
composite into the readback RT). boids seeds two storage buffers with the deterministic
golden-angle spiral, runs one sim step (`dt=0.016`, goal at origin), and instanced-draws 4096
boids. Goldens `golden/shared/{bloom,boids}.ppm` minted from the macOS-Vulkan oracle.

Measured Metal-vs-Vulkan-oracle delta on this host (M3 Pro), per channel over 64×64×3:
- **bloom: max channel delta 0, 0/12288 channels off** — bit-identical.
- **boids: max channel delta 0, 0/12288 channels off** — bit-identical.
(Metal-native and Vulkan-via-MoltenVK funnel into the same Metal rasterizer/compute, matching
the file's prior zero-delta observation. The committed goldens are the Vulkan-oracle ones.)

### Verification matrix (all green)

- gpu-scene macos --gpu=metal: **9/9** (bloom + boids RENDER, no skip; delta 0).
- gpu-scene macos --gpu=vulkan: **9/9** (oracle).
- gpu-scene macos --gpu=webgpu: 3 passed + 6 skipped — bloom + boids skip honestly
  (`mel_gpu_bindless_available` false; WebGPU core has no device-global bindless heap, same gate
  as mandelbrot/bindless_present). The WGSL emit of the bindless entries is refused by Slang
  (`NonUniformResourceIndex` unavailable in the WGSL fragment/vertex/compute stages) — the same
  documented limitation the committed `bindless_present.slang` carries; WebGPU never reaches it
  at runtime because the bindless gate trips first.
- gpu-metal: **12/12** (manual 5-heap path + the new device-local buffer staging blit, no
  regression).
- gpu-vulkan: **48/48** (unchanged; SPIR-V bindless lane is the original binding model).
- slang-compile: **10/10** (self-contained; wrapper untouched).
- hello-gpu macos vulkan/metal/webgpu: all link.

## Kludges / out-of-fence touches (MEL-ENGINE-VIII — full account)

1. **Implemented device-local buffer upload on Metal — `modules/gpu/src/metal/macos/resources.m`,
   OUTSIDE my file fence.** `mel_gpu_buffer_create` on the Metal backend SILENTLY DROPPED the
   initial `data` for `MEL_GPU_MEMORY_DEVICE` buffers (it only logged "initial data ignored for
   device-local buffer ... no staging-copy path this round" and left the buffer zeroed). boids
   seeds its storage buffers device-local; with the data dropped, the sim read zeroed buffers and
   the draw produced the clear color (the first failing run: 987/4096 px off, max delta 204 — a
   black frame). I implemented the upload honestly, mirroring the `mel_gpu_texture_write` staging
   blit landed by task #38 in the same file: a transient `MTLStorageModeShared` staging buffer +
   `MTLBlitCommandEncoder copyFromBuffer:toBuffer:` + `waitUntilCompleted`. This is a real,
   pre-existing Metal-backend gap that blocked the boids deliverable; the fix is the GENERAL one
   (fixes the boids SCREEN, the scene, and every future device-local buffer with initial data) and
   composes (MEL-ENGINE-IX). gpu-metal stays 12/12. The blit waits synchronously per call (a
   one-shot upload at create, off the hot path), as the texture upload does; a batched path onto
   the frame command buffer is the real-engine shape, deferred. **FLAGGED** — same fence and same
   precedent as #38's texture_write; an in-fence alternative (seed via host-visible
   `MEL_GPU_MEMORY_UPLOAD`) would have worked for the scene but would NOT have fixed the device-local
   screen, so the general fix is the honest one.

2. **Unified push-constant Root per screen instead of per-pass structs.** The passes' natural
   constants differ; collapsing them onto one shared `Root` (with `param0`/`param1` and unused
   resource handles aimed at a live slot) was forced by the Metal "arg buffer at buffer(0) only
   when a single push constant exists" limitation (see the find above). It is correct and clean,
   but a future reader could mistake `param0` (time | threshold | strength) for a single concept.
   The alternative — teaching the RHI to read the reflected per-entry buffer index and bind the
   argument buffer there — is the more general fix but a real from_slang-core + RHI change,
   outside this task's fence. The unified-Root form needs zero core change and was chosen.

3. **`NonUniformResourceIndex` dropped from the compute lanes, kept in the graphics lanes.** The
   bloom bright/blur and boids sim heap indices ride a uniform push constant, so the nonuniform
   qualifier is semantically wrong AND Slang refuses to lower it for the Metal/WGSL compute stage
   (batch-1's finding). Dropped there. The graphics lanes (composite fragment, boids draw vertex)
   KEEP it to match the committed `bindless_present.slang` and the GLSL originals
   (`bloom_composite.frag`/`boids_draw.vert` used `nonuniformEXT`); the cost is the documented WGSL
   refusal, accepted (WebGPU skips the bindless scene anyway). Asymmetric by design; noted so a
   future reader does not "fix" it into a WGSL break or a Metal-compute refusal.

4. **Fixed-parameter golden frames.** bloom pins `time=0`; boids runs ONE sim step with `dt=0.016`,
   `goal=(0,0)`, `time=0`. The app screens still animate; the golden is a representative
   deterministic frame, not the animation. boids' single step barely moves the seeded spiral, so
   the golden is a rich, structured image (the seeded flock evolved one tick), not a flat field.

5. **clang-format not auto-applied.** Same situation the prior waves recorded: the host
   clang-format (22) disagrees globally with the committed style. New lines were hand-matched to
   the surrounding committed code; no residual diff was introduced in any added line.

## Wrapper change (FLAGGED)

None. `third-party/slang` was NOT touched. The #37/#38 wrapper (per-target macro injection +
Metal argument-buffer reflection, which already classifies `StructuredBuffer`/`RWStructuredBuffer`
as storage buffers and walks mixed sampled-tex + sampler + storage-image fields) exposes
everything bloom and boids need.

## CLAUDE.md suggestions

None.

## Suggestions

- **Teach the Metal from_slang RHI the reflected per-entry argument-buffer index.** The hard
  constraint that forced the unified-Root is that the RHI binds the inlined argument buffer at a
  fixed `[[buffer(0)]]`. Slang already reports each entry's push-constant struct; the Metal
  reflection (`MTLComputePipelineReflection` / the existing `MTLRenderPipelineReflection` gate)
  can report the actual buffer index, so a multi-Root multi-entry shader would work without the
  unified-Root discipline. Small, isolated; it would let screen authors write per-pass constants
  naturally. This is the right follow-up to lift the "one shared Root per multi-entry source"
  rule.
- **Batched buffer/texture upload.** Both the new buffer staging blit and #38's texture_write blit
  wait synchronously per call. A staging path onto the active frame command buffer (no per-call
  wait) is the real-engine shape; one small follow-up covers both.
- **A cross-emit gate** (compile every in-tree `.slang` × every host target, assert blob + empty
  diagnostics, allow-listing the documented WGSL bindless refusal) would have caught the
  multi-Root buffer-index issue and the WGSL refusals at gate time. The #32/batch-1 writeups
  suggested the same.

## Open questions for Gabbo

1. **The device-local buffer-upload fix in resources.m (kludge 1)** lives outside the stated file
   fence, exactly as #38's texture_write did. Keep it (it is the general fix and unblocks the boids
   screen + scene + every future device-local seeded buffer), or split it to a separate
   Metal-backend task and seed boids via host-visible memory in the meantime?
2. **The unified-Root discipline (kludge 2).** Accept it as the convention for multi-entry
   from_slang screens on Metal, or prioritize the RHI per-entry-buffer-index follow-up so per-pass
   constants are allowed?
3. **The `NonUniformResourceIndex` asymmetry (kludge 3)** — compute lanes drop it, graphics lanes
   keep it to match `bindless_present`. Confirm that is the convention you want, or drop it from the
   graphics lanes too (the slot is a uniform push constant; dropping it would make the WGSL emit
   succeed, though WebGPU still skips the scene for lack of the bindless heap).
