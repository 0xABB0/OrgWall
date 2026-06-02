# GPU RHI — visual/golden tests round 2 (M2-builder features)

Extends the `gpu-visual` golden suite (`modules/gpu/test/test_visual.c` + `visual_spv.h`) with pixel-verified
goldens for the features the round-1 M2 builder landed (`writeup/2026-06-02-gpu-rhi-m2-builder.md`): storage-image
bindless, MSAA resolve (the AA-edge signature the round-1 tester deferred for lack of a resolve path),
`cmd_dispatch_indirect`, depth test, single-pipeline MRT, plus the latitude wireframe and sync2 barrier goldens.
Each new test renders/dispatches offscreen → copies to a READBACK buffer → asserts exact pixels on the CPU →
dumps a P6 PPM golden and logs its absolute path. Runnable on Vulkan/macOS over MoltenVK (Apple M3 Pro,
Vulkan 1.2.334, MoltenVK 1.4.1).

**`gpu-visual` 4 → 11 (7 new tests), 11/11 pass, zero VUIDs, zero validation errors, zero engine leaks.**

## Work done

Seven new `MEL_TEST`s in `test_visual.c`, six new embedded shaders in `visual_spv.h`. Every one is a true
pixel-verified golden — no xfail, no fake pass.

### `visual_bindless.storage_image_readback` — storage-image bindless (§6.7)
A compute shader (`imgchecker.comp`) writes a 2×2-cell red/blue checker into one heap-resident storage image
addressed purely by its bindless slot (heap binding 4); the image is barriered UnorderedAccess→CopySource,
copied to READBACK, and the checker pixel-verified (parity flip across the cell boundary). The legible cousin of
`test_vulkan`'s `vk_compute.storage_image_bindless` (which proves the same heap class with a gradient, no dump).
Golden `storage_image_checker.ppm` decoded: a clean 2×2 checker.

### `visual_state.msaa_resolve_edge_readback` — MSAA resolve, the AA signature (§7.2)
The deliverable the round-1 tester explicitly deferred. A 4-sample color attachment renders a triangle covering
the framebuffer's lower-left half (`halftri.vert`, NDC (-1,-1),(1,-1),(-1,1)); the attachment RESOLVES
(VK_RESOLVE_MODE_AVERAGE) into a single-sample `resolve_view` **in the same dynamic-rendering pass** (the
4-sample surface is never stored, DONT_CARE). The resolve target is read back and pixel-verified: edge pixels on
the main diagonal hold an **intermediate** resolved value (exactly **128** on this host — 2 of 4 samples covered,
the averaged anti-aliased value), interior pixels 255, exterior 0. A hard (non-averaged) edge could never produce
the intermediate; its presence is the defining MSAA-resolve signature. This is strictly stronger than
`test_vulkan`'s `vk_render.msaa_resolve_readback`, which uses a fully-covering triangle and only verifies flat
white (no edge, no averaging proof). Golden `msaa_resolve_edge.ppm`: a clean 128-valued anti-aliased diagonal.

### `visual_state.dispatch_indirect_readback` — host-filled indirect dispatch (§7.1)
The indirect-args buffer is **host-written** with `{G,1,1}` (`MEL_GPU_BUFFER_INDIRECT` + UPLOAD memory), then
`cmd_dispatch_indirect` reads it and dispatches G groups of `idxwrite.comp` (local_size_x=1), each invocation
writing `(1000 + gl_NumWorkGroups.x)` at its global index into a heap storage buffer. The readback proves exactly
indices [0,G) are written and every one holds `1000+G` — the indirect group count, not a hardcoded grid, drove
the dispatch. The host-fill complement to `test_vulkan`'s `vk_compute.dispatch_indirect` (which fills the args via
a prior GPU pass). Buffer-only proof — no PPM (no visual payoff for a scalar buffer).

### `visual_state.depth_boundary_readback` — depth test with a visible occlusion boundary (§6.5)
Two covering triangles into color+depth (`depthtri.vert`): draw 0 fills the frame at flat depth 0.5; draw 1 fills
it again with depth ramping 0.2 (left) → 0.8 (right), drawn **second**. Under LESS, draw 1 wins the left half
(depth < 0.5) and loses the right (depth > 0.5) — so the boundary is decided by the per-fragment depth compare,
not draw order (draw order alone would flat-fill with draw 1). Golden `depth_boundary.ppm` decoded: a crisp
vertical split, green left 4 columns / red right 4 columns. Stronger than `test_vulkan`'s `vk_pipeline.depth_compare`
(flat single-winner, no in-image boundary).

### `visual_state.mrt_single_pipeline_readback` — single-pipeline MRT (§6.5)
**One** pipeline, two color targets; `mrt2.frag` writes distinct constant colours to location 0 and location 1 in
a single draw, both attachments read back and pixel-verified, both dumped (`mrt_target_0/1.ppm`). The true
single-pipeline MRT path (one fragment, two outputs), distinct from the pre-existing
`visual_state.two_targets_readback` (two passes reusing the UBO fragment).

### `visual_state.wireframe_vs_solid_readback` — wireframe vs solid (§6.5, latitude)
The same lower-left-half triangle rendered once SOLID and once `MEL_GPU_FILL_WIREFRAME` into two 16×16 targets,
both dumped. Asserts the wireframe set-pixel count is strictly **less** than the solid fill. On this host
`fillModeNonSolid` is granted, so the strict edge assert ran and passed (`wireframe_solid.ppm` = 120 filled px,
`wireframe_wire.ppm` = 31 edge-only px — the three triangle edges). The test self-detects a solid-fallback
degrade (counts equal) and logs an honest skip-of-the-strict-assert rather than a false failure, since there is
**no public caps flag for fill-mode-non-solid** to gate on up front (see kludges).

### `visual_state.sync2_barrier_smoke_readback` — sync2 barrier-heavy path (§7.3, latitude)
A deliberately barrier-dense offscreen render: clear, draw a UBO colour, then ping-pong the target through
RENDER_TARGET→COPY_SOURCE→COPY_DEST→RENDER_TARGET→COPY_SOURCE before readback — several texture-state transitions
in one list. On this host `barrier lowering: synchronization2` is active, so every barrier lowers through
`vkCmdPipelineBarrier2`. The colour survives the churn intact ((0.40,0.70,0.20)→(102,178,51), pixel-verified):
the sync2 path stays correct under a transition-heavy list. Golden `sync2_barrier.ppm`.

## Coverage map
- **Pixel-verified golden (with PPM dump):** storage-image bindless, MSAA resolve AA-edge, depth occlusion
  boundary, single-pipeline MRT (two dumps), wireframe-vs-solid (two dumps), sync2 barrier path.
- **Pixel-verified, buffer-only (no PPM):** dispatch-indirect (host-filled args).
- **xfail / documented-failure:** none. Every targeted feature is exercised and proven on this host.

## Where the PPMs land
`modules/gpu/build/macos-debug/` (relative to the test cwd = repo/worktree root). New this round:
`storage_image_checker.ppm`, `msaa_resolve_edge.ppm`, `depth_boundary.ppm`, `mrt_target_0.ppm`,
`mrt_target_1.ppm`, `wireframe_solid.ppm`, `wireframe_wire.ppm`, `sync2_barrier.ppm`. The absolute path of each is
`mel_log_info`'d at dump time. (dispatch-indirect dumps nothing — scalar buffer.)

## Verification
- `DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1 ./nob test gpu-visual macos --gpu=vulkan` → **11/11**.
- The pre-existing 4 (`uniform_buffer_readback`, `sampled_checker_readback`, `alpha_blend_readback`,
  `two_targets_readback`) stay green — additive.
- Grep-clean of `VUID` / `validation error`; no engine-level resource-leak report; the engine slotmap leak
  detector is silent. `render lowering: dynamic rendering`, `barrier lowering: synchronization2` active.
- **MoltenVK reports "1 MB of GPU memory still allocated" at `VkPhysicalDevice` destroy** for the six
  heavier-allocating tests (the first five report 0 MB). This is the **driver-level teardown artifact** the
  round-1 builder writeup documented (present across the whole suite, scaling with device-memory churn), NOT an
  engine leak — the engine's own leak detector reports zero live resources at every `device_destroy`. Flagged for
  honesty (MEL-ENGINE-VIII); below the engine, not actioned.
- Goldens decoded and confirmed: storage checker (clean 2×2), MSAA edge (128-valued diagonal), depth (green/red
  vertical split at centre), MRT (0=(51,153,229), 1=(242,89,26)), wireframe (120 vs 31 set px), sync2 ((102,178,51)).

## Kludges and debt (confessed, MEL-ENGINE-VIII)
- **House-style comments vs. the global no-comment rule (Rule #1).** `modules/gpu` is densely commented in house
  style and the round-1 visual writeup matched it; I matched it too (every new technique block carries a §-cited
  rationale, consistent with the four pre-existing visual tests). This is in tension with the global CLAUDE.md
  "Never write comments" directive. **Flagged for Gabbo; no exception taken on my own authority** — the existing
  file and the brief ("match house comments") both point to commenting, but the global-rule conflict is recorded.
- **`VISUAL_DUMP_DIR` hardcoded to `modules/gpu/build/macos-debug`** (inherited from round 1, a string literal,
  MEL-CODE-007 silent-default tension). The test cwd is the repo/worktree root, so the path resolves for the
  standard `./nob test … macos --gpu=vulkan` invocation; a run from another cwd or a release/other-platform build
  dir finds no dir → `fopen` warns and skips the dump, the pixel asserts still run. Unchanged from round 1;
  tracked there, not re-actioned here.
- **Wireframe degrade detected from the rendered result, not a caps flag.** There is no public
  `caps` field exposing `fillModeNonSolid`, so the wireframe golden cannot gate up front; it detects a
  solid-fallback degrade post-hoc (wire set-px ≥ solid set-px) and logs an honest skip-of-the-strict-assert
  instead of a false failure. On this host the feature is granted, so the strict assert ran. A
  `caps.raster.fill_mode_non_solid` bool would let the test gate cleanly (suggestion below).
- **Shader sources live only in this writeup, not the tree.** Per MEL-SPEC-002 (no spec clutter) and matching the
  round-1 convention, the six GLSL sources are recorded verbatim below for regeneration; they are not committed as
  files. The embedded SPIR-V in `visual_spv.h` is the build input.

## Needs the fixer / Gabbo
- **Fixer:** nothing blocking. All seven goldens pass on this host; no feature was found broken or missing. The
  `1 MB` MoltenVK teardown line is a known driver artifact (documented, not an engine leak) — only flag it if a
  future run shows the *engine* leak detector reporting live resources (it does not here).
- **Gabbo:** the house-comment vs. Rule-#1 tension above. If the global no-comment rule binds even in
  densely-commented modules, the per-block rationale comments in `test_visual.c` (mine and the four pre-existing)
  should be stripped wholesale.

## CLAUDE.md / repo-convention suggestions (recommendations only)
- **Expose `fillModeNonSolid` (and the other optional raster features) in `Mel_Gpu_Caps`.** The device already
  records `dev->feat_fill_non_solid`; surfacing it as `caps.raster.fill_mode_non_solid` would let a wireframe
  golden gate up front (`MEL_SKIP` when ungranted) instead of detecting the degrade from rendered pixels. Same for
  `feat_depth_bounds` / `feat_sample_rate_shading`.
- **A runner-provided artifact output directory** (env or runner field) would retire the hardcoded
  `VISUAL_DUMP_DIR`, letting golden-dumping tests write next to the binary across configs/platforms. (Repeated
  from round 1; still open.)
- **`modules/gpu/readme.md` is still absent** (flagged in four prior writeups). The heap-class→binding map
  (0 sampled image, 1 sampler, 2 storage buffer, 3 uniform buffer, 4 storage image), the `slot == handle.index`
  contract, and the two barrier lowerings belong there — every visual test depends on that convention.

## Shader sources (regeneration)
Six shaders, appended to `modules/gpu/test/visual_spv.h`. Regenerate each:
`glslc -fshader-stage={comp,vert,frag} -mfmt=c <src>`, then wrap as `static const uint32_t NAME[] = { … };`.
Toolchain on host: `glslc` (shaderc v2025.5) at `/usr/local/bin/glslc`.

```glsl
// imgchecker.comp -> VISUAL_IMGCHECKER_COMP_SPV  (storage-image bindless, heap binding 4)
#version 460
#extension GL_EXT_nonuniform_qualifier : require
layout(set = 0, binding = 4, rgba8) uniform writeonly image2D u_images[];
layout(push_constant) uniform Root { uint img_slot; uint width; uint height; uint cell; } root;
layout(local_size_x = 8, local_size_y = 8) in;
void main() {
    uvec2 p = gl_GlobalInvocationID.xy;
    if (p.x >= root.width || p.y >= root.height) return;
    bool a = (((p.x / root.cell) ^ (p.y / root.cell)) & 1u) == 0u;
    vec4 c = a ? vec4(0.90, 0.10, 0.20, 1.0) : vec4(0.10, 0.20, 0.90, 1.0);
    imageStore(u_images[nonuniformEXT(root.img_slot)], ivec2(p), c);
}
```
```glsl
// halftri.vert -> VISUAL_HALFTRI_VERT_SPV  (lower-left-half triangle; MSAA/wireframe geometry)
#version 460
void main() {
    vec2 p = vec2(0.0);
    if (gl_VertexIndex == 0) p = vec2(-1.0, -1.0);
    else if (gl_VertexIndex == 1) p = vec2(1.0, -1.0);
    else p = vec2(-1.0, 1.0);
    gl_Position = vec4(p, 0.0, 1.0);
}
```
```glsl
// solidpc.frag -> VISUAL_SOLID_PC_FRAG_SPV  (flat colour from a 16-byte push-constant)
#version 460
layout(push_constant) uniform Root { vec4 color; } root;
layout(location = 0) out vec4 o_color;
void main() { o_color = root.color; }
```
```glsl
// depthtri.vert -> VISUAL_DEPTHTRI_VERT_SPV  (covering triangle, flat-0.5 or ramped 0.2->0.8 depth)
#version 460
layout(push_constant) uniform Root { vec4 color; uint ramp; } root;
void main() {
    vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    vec2 ndc = p * 2.0 - 1.0;
    float z = 0.5;
    if (root.ramp != 0u) z = 0.2 + 0.6 * p.x;
    gl_Position = vec4(ndc, z, 1.0);
}
```
```glsl
// mrt2.frag -> VISUAL_MRT2_FRAG_SPV  (single-pipeline MRT: two distinct constant outputs)
#version 460
layout(location = 0) out vec4 o0;
layout(location = 1) out vec4 o1;
void main() {
    o0 = vec4(0.20, 0.60, 0.90, 1.0);
    o1 = vec4(0.95, 0.35, 0.10, 1.0);
}
```
```glsl
// idxwrite.comp -> VISUAL_IDXWRITE_COMP_SPV  (dispatch-indirect proof: write 1000+gl_NumWorkGroups.x per index)
#version 460
#extension GL_EXT_nonuniform_qualifier : require
layout(set = 0, binding = 2) buffer Buf { uint v[]; } u_buffers[];
layout(push_constant) uniform Root { uint out_buf; uint n; } root;
layout(local_size_x = 1) in;
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= root.n) return;
    u_buffers[nonuniformEXT(root.out_buf)].v[i] = 1000u + gl_NumWorkGroups.x;
}
```
