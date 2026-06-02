# GPU RHI M2 — visual/golden tests + uniform-buffer bindless proof

Adds a **visual test target `gpu-visual`** to `modules/gpu`: every technique renders offscreen into an RGBA8
attachment, copies it to a READBACK buffer, asserts specific pixels on the CPU (machine-checkable), AND dumps
a binary PPM golden image to the build dir with its absolute path logged (`mel_log_info`), so a human can
eyeball the result. This closes the **uniform-buffer bindless** heap class that the binding-finish writeup
(`writeup/2026-06-02-gpu-rhi-m2-binding-finish.md`) flagged as registered-but-unproven, and lays down a
reusable golden-dump harness alongside the existing headless `gpu-vulkan` pixel suite.

Runnable on Vulkan/macOS over MoltenVK (Apple M3 Pro, Vulkan 1.2.334). **gpu-visual 4/4, gpu-vulkan still
28/28, zero validation errors, zero VUIDs, zero leaks (every device retires with 0 MB still allocated).**

## Work done

### Target + harness (`modules/gpu/build.c`, `modules/gpu/test/test_visual.c`)
- `mel_add_test(b, "gpu-visual")` mirrors the `gpu-vulkan` block exactly — same deps (`test`, `gpu`, `core`,
  `allocator`, `collection`, `reactor`), the macOS AppKit link, `runner.c`, and the
  `WHEN(.gpu = "vulkan")` `MEL_GPU_VULKAN=1` define. The body is `#if MEL_GPU_VULKAN`-guarded and emits one
  `MEL_SKIP` test otherwise.
- The shared scaffold: `Visual_Target` (one color attachment + readback buffer), `test_target_create/destroy`,
  and `test_dump_ppm(name, rgba, w, h)`. The readback is a **tight packed RGBA8 image**
  (`cmd_copy_texture_to_buffer` issues `bufferRowLength = 0`, so rows are `W*4` with no padding), which
  `test_dump_ppm` writes as a P6 PPM (alpha dropped) and logs via `realpath`. A failed `fopen` is a warning,
  never a test failure — the dump is an aid, the pixel asserts are the proof (MEL-ENGINE-VIII honest degrade).
- Dumps land in `modules/gpu/build/macos-debug/` (relative to the test cwd = repo/worktree root). On this run:
  `ubo_bindless.ppm`, `sampled_checker.ppm`, `alpha_blend.ppm`, `two_targets_0.ppm`, `two_targets_1.ppm`.

### Techniques (each = offscreen render + readback + exact pixel assertion + PPM dump)
- **`visual_bindless.uniform_buffer_readback` — the deliverable.** A UBO created with `MEL_GPU_BUFFER_UNIFORM`
  auto-registers into the set-0 **uniform-buffer heap class** (binding 3, `slot == handle.index`; see
  `binding.c` / `vk_backend.h` `MEL_GPU_BINDLESS_BINDING_UNIFORM_BUFFER = 3`). The fragment reads
  `u_ubos[root.ubo].color` (a runtime UBO array at set 0 binding 3, addressed by a 4-byte push-constant root
  record) and writes it to every fragment. Readback equals the UBO colour (0.30,0.55,0.80) → (77,140,204):
  the value reached the output through the uniform-buffer heap, proving the class end-to-end. Reflection
  derives both the bindless signature and the push-constant size; no explicit `.bindless` / `.push_constant_size`.
- **`visual_bindless.sampled_checker_readback`.** A 4x4 red/blue checker filled via `texture_write`, sampled
  through the heap (NEAREST + CLAMP_EDGE) at the fragment UV across an 8x8 target → a clean 2x-magnified
  checker. Asserts two saturated taps and a hard parity flip across the block boundary.
- **`visual_state.alpha_blend_readback`.** A 1x1 translucent-red (255,0,0,128) source sampled everywhere and
  drawn through `MEL_GPU_BLEND_ALPHA` over a (0.2,0.4,0.6) clear. src-over = 0.5·src + 0.5·dst = (0.6,0.2,0.3)
  → (153,51,76), pixel-verified.
- **`visual_state.two_targets_readback`.** Two passes in one command list, each painting a distinct UBO colour
  into its own attachment; both read back and verified ((0.25,0.5,0.75) and (0.90,0.10,0.40)). Exercises the
  UBO heap class with two live slots and multi-pass recording. (The single-pipeline MRT-with-two-outputs
  variant is already covered by `test_vulkan`'s `vk_pipeline.mrt_two_targets`; this is its multi-pass cousin.)

### SPIR-V (`modules/gpu/test/visual_spv.h`, new — self-contained, no symbol clash with `bindless_spv.h`)
Three embedded shaders: a fullscreen-triangle vertex shader emitting a [0,1]² UV, a bindless sampled-texture
fragment (set 0 binding 0 texture + binding 1 sampler), and a uniform-buffer bindless fragment (set 0
binding 3 UBO). Generated with the shaderc/`glslc` toolchain present on the host (`which glslc` →
`/usr/local/bin/glslc`, shaderc v2025.5).

## Verification
- `DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1 ./nob test gpu-visual macos --gpu=vulkan` → **4/4**.
- `… ./nob test gpu-vulkan macos --gpu=vulkan` → **28/28** (no regression; the build.c change is additive).
- Both runs grep-clean of `VUID` / `validation error` / `leak`; every `Destroyed VkPhysicalDevice` reports
  **0 MB of GPU memory still allocated**.
- Golden dumps decoded and confirmed: `ubo_bindless`=(77,140,204), `alpha_blend`=(153,51,76),
  `two_targets_0`=(64,128,191), `two_targets_1`=(229,26,102), `sampled_checker`= clean 2x checker.

## Coverage map (what is pixel-verified here vs. elsewhere vs. documented-xfail)
- **Pixel-verified in `gpu-visual` (with golden dumps):** uniform-buffer bindless (the new class), bindless
  sampled texture (procedural checker), alpha blending, two-target multi-pass readback.
- **Pixel-verified already in `test_vulkan.c` (no dump):** classic descriptor-set path
  (`vk_bind_group.classic_descriptor_set`), depth compare (`vk_pipeline.depth_compare`), single-pipeline MRT
  (`vk_pipeline.mrt_two_targets`), spec constants (`vk_pipeline.spec_constants_bake`), reflection-derived
  vertex input (`vk_pipeline.reflection_vertex_input`), storage-buffer bindless
  (`vk_compute.storage_buffer_bindless`), BDA pointer root record (`vk_bindless.bda_pointer_root_record`).
  These were not duplicated as golden dumps — they are 1-colour or structural and already machine-checked.
- **Not done here (boundaries):** **storage-image bindless** is the builder's deliverable (skipped, no
  duplication). **MSAA resolve-and-readback golden** is *not* added — see below.

## Kludges and debt (confessed, MEL-ENGINE-VIII)
- **House-style comments vs. the global no-comment rule.** `modules/gpu` is densely commented in house style;
  per the task brief I matched it (every technique block carries a §-cited rationale). This is in tension with
  the global CLAUDE.md "Never write comments" directive (Rule #1). **Flagged for Gabbo; no exception taken on
  my own authority** — the brief sanctioned matching module style, but the conflict is recorded here.
- **MSAA resolve-and-readback golden not added.** The resolve path is not yet exposed as a public RHI call:
  `test_vulkan`'s `vk_pipeline.msaa_renders_clean` proves the 4-sample pipeline submits validation-clean, but
  there is no `cmd_resolve` in the public command surface to resolve a multisample attachment into a
  single-sample texture for readback (the binding-finish writeup calls the resolve path "the `cmd_resolve`
  slice (U10/U16)" — i.e. not landed). Without it, an honest golden cannot be produced: a `copy_texture_to_
  buffer` on a 4-sample image is not a defined readback. **Handed to the builder** — once `cmd_resolve` (or an
  attachment `.resolve_view`) lands, this is a 20-line golden. Not faked.
- **`VISUAL_DUMP_DIR` is hardcoded to `modules/gpu/build/macos-debug`** (a string literal, MEL-CODE-007 silent-
  default tension). The test cwd is the repo/worktree root, so the path resolves for the standard
  `./nob test … macos --gpu=vulkan` invocation. A run from another cwd, or a release/other-platform build dir,
  finds no dir → `fopen` fails → a logged warning, the pixel asserts still run. The build dir is not queryable
  from the test runner today; a `MEL_TEST_OUTPUT_DIR` env (or a runner-provided artifact path) would make the
  dump location explicit rather than conventional. Tracked, not blocking.
- **Two-target test is multi-pass, not true single-pipeline MRT.** Deliberate — the single-pipeline MRT path
  is already covered in `test_vulkan` and reusing the UBO fragment kept `visual_spv.h` to three shaders
  (no extra MRT fragment to embed). Named honestly in the test comment; not presented as MRT.
- **Spec constants / reflection-vertex-input not folded into the visual suite.** Both are already pixel-tested
  in `test_vulkan.c`; adding golden dumps would duplicate coverage for a flat-colour output with no visual
  payoff. Skipped deliberately per the "fold in if cheap" latitude — the cost (two more embedded shaders) did
  not buy a meaningfully different golden.

## Needs the builder / Gabbo
- **Builder:** `cmd_resolve` (or attachment resolve-view) to unblock the MSAA resolve golden; storage-image
  bindless pixel test (their deliverable) for the last unproven heap class.
- **Gabbo:** the house-comment vs. Rule-#1 tension above. If the global no-comment rule is to bind even in
  densely-commented modules, the test file's per-block rationale comments should be stripped.

## CLAUDE.md / repo-convention suggestions (recommendations only)
- A runner-provided **artifact output directory** (env or runner field) would let golden-dumping tests write
  next to the binary without hardcoding a per-platform build path. The current convention works but is fragile
  across configs/platforms.
- `modules/gpu/readme.md` is still absent (flagged in two prior writeups). The heap-class binding convention
  this test depends on — set 0 binding {0 sampled image, 1 sampler, 2 storage buffer, 3 uniform buffer,
  4 storage image}, `slot == handle.index` — belongs there.

## Shader sources (regeneration: `glslc -fshader-stage={vert,frag} -mfmt=c <src>`, then wrap as `static const uint32_t NAME_SPV[] = { … };`)

```glsl
// fullscreen.vert -> VISUAL_FULLSCREEN_VERT_SPV
#version 460
layout(location = 0) out vec2 v_uv;
void main() {
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    v_uv = uv;
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}
```
```glsl
// sampled.frag -> VISUAL_SAMPLED_FRAG_SPV  (bindless sampled texture, heap binding 0 + sampler binding 1)
#version 460
#extension GL_EXT_nonuniform_qualifier : require
layout(set = 0, binding = 0) uniform texture2D u_textures[];
layout(set = 0, binding = 1) uniform sampler   u_samplers[];
layout(push_constant) uniform Root { uint tex; uint smp; } root;
layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;
void main() {
    o_color = texture(sampler2D(u_textures[nonuniformEXT(root.tex)], u_samplers[nonuniformEXT(root.smp)]), v_uv);
}
```
```glsl
// ubo_bindless.frag -> VISUAL_UBO_FRAG_SPV  (uniform-buffer bindless, heap binding 3 — the proven-here class)
#version 460
#extension GL_EXT_nonuniform_qualifier : require
layout(set = 0, binding = 3) uniform Ubo { vec4 color; } u_ubos[];
layout(push_constant) uniform Root { uint ubo; } root;
layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;
void main() { o_color = u_ubos[nonuniformEXT(root.ubo)].color; }
```
