# Port graphics gallery screens to dual-lane Slang (batch G2)

Task #35 batch G2: port `depth3d`, `msaa`, `shadow`, `prepass`, `passthrough` from
bytecode (`*_spv.h`) to single-source, multi-entry, dual-lane runtime Slang
(`pipeline_create_from_slang` / `shader_create_from_slang`), and add a `gpu-scene`
golden per screen diffed against the macOS-Vulkan oracle.

## Work done

### Shaders (new, `apps/hello-gpu/shaders/slang/`)
- `passthrough.slang` — pos+color pass-through (`vs_main`/`fs_main`). Reflection-driven
  vertex layout (`float3 POSITION`, `float4 COLOR0`) matches the shared `Pt_Vertex`.
  Replaces the triangle SPV that `passthrough_shader()` loaded; `cube.c`/`lorenz.c` (not
  owned) inherit it transparently through the unchanged `passthrough.h` API.
- `depth3d.slang` — scene pass (`vs_scene`/`fs_scene`), pos+color, depth-tested via C state.
- `prepass.slang` — `vs_scene` + two fragments: `fs_depth` (void, depth prepass) and
  `fs_lit`. Drives the LESS prepass → EQUAL lit pass (overdraw kill).
- `msaa.slang` — star pass (`vs_star`/`fs_star`, push-constant rotation) + compose pass
  (`vs_compose`/`fs_compose`, dual-lane bindless: samples the resolved + reference targets).
  Unified `Root` (Metal `DescriptorHandle` fields; Vulkan/WGSL `[[vk::binding]]` heap +
  uint slots).
- `shadow.slang` — depth-from-light pass (`vs_depth`/`fs_depth`) + lit pass
  (`vs_scene`/`fs_scene`, dual-lane bindless: samples the shadow map, depth-compare). Unified
  `Root` carries `depth_bias` + `float4 light_dir` (see Metal arg-buffer note below).

### C migrations (`apps/hello-gpu/src/`)
- `passthrough.c` — `#embed` + `shader_create_from_slang`; kept the explicit `Pt_Vertex`
  layout in `passthrough_pipeline` (the shared helper splits shader/pipeline; cube uses
  triangles, lorenz uses lines, so one shader feeds two topologies).
- `depth3d.c`, `prepass.c`, `shadow.c`, `msaa.c` — `#embed` + `pipeline_create_from_slang`
  (reflection-driven vertex layout, `depth_format`/`depth_stencil`/`samples`/`cull`/
  `front_face` carried through). Dropped all `*_spv.h` includes.
- Orphaned headers removed (0 remaining consumers): `scene3d_spv.h`, `depth_only_spv.h`,
  `star_spv.h`, `msaa_compose_spv.h`, `shadow_{depth,scene}_{vert,frag}_spv.h`,
  `triangle_spv.h` (its sole consumer was `passthrough.c`; `triangle_bundle.h` independently
  provides `TRIANGLE_*_SPV` to the tests and is untouched).

### gpu-scene goldens (`modules/gpu/test/`)
- Added 5 scenes in a clearly-delimited contiguous append block (a sibling batch also
  appends here). Each is deterministic and exercises its feature:
  - `passthrough` — single triangle.
  - `depth3d` — near-blue (z=0.25) vs far-red (z=0.75) overlap, far-first; LESS depth-test
    proves the near quad occludes (golden center is blue, red-only region is red).
  - `prepass` — same overlap, depth-only prepass + EQUAL lit pass.
  - `msaa` — star into a multisample target, resolved to the readback RT.
  - `shadow` — depth-from-light occluder band + lit ground sampling the shadow map
    (golden: lit ground 178,178,191 with a darker shadow band 57,57,61).
- Goldens minted on the macOS-Vulkan oracle (`MEL_GPU_GOLDEN_UPDATE=1`), all non-degenerate.

## Verification (all green)
- `gpu-scene macos --gpu=vulkan` — 14 passed, 0 failed (oracle self-diff, delta ≤2).
- `gpu-scene macos --gpu=metal` — 13 passed, 1 skipped (msaa), 0 failed. passthrough/
  depth3d/prepass/shadow **RENDER on Metal with delta 0** against the Vulkan-oracle goldens
  (Metal converges bit-for-bit). depth-state, cull, depth-compare/shadow-sample all verified.
- `gpu-scene macos --gpu=webgpu` — 6 passed, 8 skipped, 0 failed. passthrough/depth3d/
  prepass RENDER; msaa/shadow honest skips (no push constants / no device-global bindless).
- `gpu-metal` 12/12, `gpu-vulkan` 48/48, `slang-compile` 10/10.
- `hello-gpu` builds on macos metal / vulkan / webgpu.

## Format enum
`modules/gpu/include/gpu/format.{h,c}` **NOT touched** — no growth needed. The enum already
carries `RG32_FLOAT`/`RGB32_FLOAT`/`RGBA32_FLOAT`, which cover every vertex attribute these
screens use (depth3d/prepass `Pt_Vertex` = float3+float4; msaa star = float2+float4; shadow
depth = float3, scene = 4×float3). No packed-normal/uint attribute appears. The #32 agent's
flag does not bite this batch; if a sibling needs an additive format it stays uncoordinated
here.

## Kludges / debt (confession, MEL-ENGINE-VIII)

1. **Metal MSAA-resolve RHI gap (real, blocking; reported, not worked around).**
   The Metal RHI's `mel_gpu_cmd_begin_rendering` (`modules/gpu/src/metal/macos/rendering.m`)
   ignores `Mel_Gpu_Color_Attachment.resolve_view` entirely — it never sets
   `resolveTexture` / `MTLStoreActionMultisampleResolve`. A multisampled pass therefore never
   resolves on Metal (the readback target stays unwritten → all-magenta). This is pre-existing
   (the only MSAA coverage, `gpu-visual msaa_resolve_edge`, builds Vulkan-only) and outside my
   file ownership (`modules/gpu/src/metal/*` is off-limits). The `msaa` scene **skips honestly
   on Metal** with the precise reason; Vulkan proves the scene and the Slang→MSL star compiles.
   Metal coverage returns the moment `begin_rendering` honors `resolve_view`. This also means
   the `msaa.c` APP's resolve is a silent no-op on Metal today (it renders the multisample pass
   and presents it unresolved); my migration neither introduced nor fixed that — it is the same
   RHI gap. **OPEN for Gabbo: wire resolve_view into the Metal begin_rendering.**

2. **Unified-Root forces every shared pass to carry the heap on Metal (wrapper limitation).**
   The Slang wrapper reflects the file-global `[[vk::push_constant]] Root` (with
   `DescriptorHandle` fields) into *every* entry's program layout — `getParameterCount()` is
   global, not per-entry (`third-party/slang/src/compile.cpp`). On Metal, `pipeline.m` then
   demands that each pipeline built from such a source expose a `[[buffer(0)]]` argument
   buffer, and `record.m` demands every `DescriptorHandle` slot name a live resource at draw.
   Consequences for the non-sampling passes:
   - `shadow` depth pass (`vs_depth`/`fs_depth`) had to become `.bindless = true` and reference
     the Root so Slang emits the arg buffer. I gave it a *genuine* `depth_bias` it applies to
     the output z (a real shadow-mapping feature, not a dead read), and the depth draw now
     binds bindless + pushes the Root with the live shadow-map/sampler slots.
   - `msaa` star pass shares the compose Root; its draw must supply live `resolved`/`reference`/
     `smp` slots even though the star never samples them. The msaa **test** registers a throwaway
     sampled texture + sampler purely to populate those handles (documented inline). The msaa.c
     APP already supplies the real `aa`/`ref`/sampler slots, so it is unaffected.
   This is the cost of "ONE unified Root per multi-entry screen" when a pass doesn't sample; it
   is honest and loud (every slot is a live resource), but it couples passes that are logically
   independent. Not a hidden shortcut — flagged here as the dual-lane pattern's edge.

3. **`light_dir` widened to `float4` in the shadow Root.** A `float3` push-constant member
   lands at a 16-byte-padded slot in the Metal argument buffer (Slang pads float3→float4),
   making the host struct 36 bytes vs the 32 the naive C struct supplied (loud
   `cmd_push_constants` size-mismatch error). I changed the Root's `light_dir` to `float4`
   (xyz used) in both lanes and both C structs (`shadow.c` `Scene_Root`, the test
   `Scene_Shadow_Root`) so the host layout is unambiguous. Not debt so much as the correct
   alignment discipline for float3-in-arg-buffer; noting it because it's a non-obvious trap for
   the next screen with a vec3 push-constant.

4. **`depth3d` now sets an explicit `.depth_stencil`.** The original SPV `depth3d.c` passed
   `.depth_format` alone and relied on an implicit depth-test-on default that Vulkan/Metal
   honored but WebGPU rejected (pipeline had no depthStencilFormat → attachment-state mismatch,
   blank frame). I added an explicit `{depth_test, depth_write, LESS}` state to both the APP and
   the test (MEL-CODE-007: no silent default). Output is bit-identical on Vulkan/Metal (the
   implicit default was the same), and WebGPU now renders.

## CLAUDE.md suggestions (recommendations only)
- Document the wrapper's whole-module push-constant reflection: a multi-entry `.slang` whose
  global Root has `DescriptorHandle` fields makes **every** pipeline built from it bindless on
  Metal. Authors of mixed sampling/non-sampling screens must either (a) make every pass touch
  the heap, or (b) accept the non-sampling pass carrying the Root + live slots. Worth a line in
  the dual-lane authoring notes.
- Note the float3-in-arg-buffer 16-byte padding rule for push-constant Roots (use float4).

## Suggestions
- Land Metal MSAA resolve in `begin_rendering` (resolveTexture + StoreActionMultisampleResolve);
  it unblocks the `msaa` Metal golden and fixes the silent unresolved present in the msaa APP.
- Consider a wrapper/RHI affordance to mark specific entries of a Slang module as
  non-bindless so a depth-only pass need not carry the heap Root on Metal — would remove
  kludge #2's coupling.
