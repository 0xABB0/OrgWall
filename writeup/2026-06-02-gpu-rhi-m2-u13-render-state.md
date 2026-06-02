# GPU RHI M2 — U13 graphics pipeline render-state (blend / depth / stencil / MRT / MSAA / raster)

Continues the GPU RHI rewrite (`design/gpu-rhi.md`). The binding-model sessions left the graphics pipeline with a
bare state space — topology, cull, a single color format, and depth-on-when-a-depth-format-is-present (compare
hardcoded LESS). This session lands the **U13 §6.5 static render-state** the spec calls "full state coverage":
per-attachment blend, MRT, full depth/stencil (per-face stencil, depth bounds), rasterization (fill mode, front
face, depth bias), and MSAA sample count. Vulkan/macOS over MoltenVK, headlessly pixel-verified.

**gpu-vulkan 28/28, gpu-foundation 8/8, collection-slotmap 3/3, zero validation errors, zero leaks, zero VUIDs.**
The cube regression app builds and runs clean through the extended `pipeline_create`.

## Scope
The slice Gabbo selected: U13 *render-state* only. The other U13 sub-items (GPL, `pipeline_binary` cache,
`shader_object`, cache-control flags, `pipeline_robustness`, tessellation, hot-reload `pipeline_replaced`,
indirect-layout) are **out of scope** and untouched, as are the host-blocked D3D12 backend and the
halt-and-query Slang front end.

## Work done

### API surface (`include/gpu/pipeline.h`)
The `Mel_Gpu_Pipeline_Opt` is extended **additively** — every prior call site (`.color_format = X`, `.cull`, …)
compiles and behaves identically:
- **Blend** — `Mel_Gpu_Blend_Factor` / `Mel_Gpu_Blend_Op` enums, `Mel_Gpu_Blend` per-attachment struct,
  `Mel_Gpu_Color_Write_Mask` (bits chosen to coincide with `VK_COLOR_COMPONENT_*_BIT`), and the
  `MEL_GPU_BLEND_OPAQUE` / `MEL_GPU_BLEND_ALPHA` convenience initializers.
- **MRT** — `Mel_Gpu_Color_Target { format, blend }` + `opt.color_targets[]` / `color_target_count`. The scalar
  `opt.color_format` is retained as the single-opaque-target shortcut; when `color_target_count > 0` it is ignored.
  `opt.blend_constants[4]` feeds the `CONSTANT_*` factors.
- **Depth/stencil** — `Mel_Gpu_Depth_Stencil` (depth test/write/compare/bounds + per-face `Mel_Gpu_Stencil_Face`)
  as an **opt-in pointer** `opt.depth_stencil`; NULL reproduces the documented default (depth test+write+LESS when a
  `depth_format` is present). `Mel_Gpu_Compare_Op` is **reused** from `sampler.h` (MEL-ENGINE-IX), not duplicated.
- **Rasterization** — `opt.front_face` (zero = CCW), `opt.fill` (zero = solid), `opt.depth_bias` + constant/clamp/slope.
- **MSAA** — `opt.samples`, `opt.alpha_to_coverage`, `opt.sample_shading`, `opt.min_sample_shading`.

### Lowering (`src/vulkan/pipeline.c`)
- Blend-factor / blend-op / stencil-op / stencil-face lowerings; `mel_gpu__sample_bits` for the sample count.
- Color targets resolved (explicit array → else single `color_format` opaque → else zero color attachments for a
  depth-only pipeline); `VkFormat[]` + `VkPipelineColorBlendAttachmentState[]` built dynamically (device allocator,
  no fixed arrays — MEL-CODE-002), `pri.colorAttachmentCount` / `pColorAttachmentFormats` driven from them.
- Full `VkPipelineDepthStencilStateCreateInfo` from `opt.depth_stencil`, else the prior default; `stencilAttachmentFormat`
  set for `D24_UNORM_S8_UINT`.
- Rasterization: polygon mode, front face, depth bias; multisample: validated `rasterizationSamples`, sample shading,
  alpha-to-coverage.
- Render-pass **floor** kept faithful: a new `mel_gpu__make_pipeline_compat_render_pass` builds an MRT/MSAA/depth-
  compatible pass for the `!dynamic_rendering` path (the swapchain's present pass in `swapchain.c` is untouched).

### Device features (`src/vulkan/device.c`, `vk_backend.h`)
`fillModeNonSolid`, `depthBounds`, `depthBiasClamp`, `sampleRateShading` are enabled at device-create **when the
physical device supports them**; the enabled flags and `framebuffer{Color,Depth}SampleCounts` limits are cached on the
device. A pipeline that requests an unenabled one **degrades with a warning** (MEL-CODE-007), never a silent miscompile.

### Shared compare lowering
Promoted the sampler's static `mel_gpu__compare` to `mel_gpu__vk_compare_op` in `vk_common.c` (declared in
`vk_backend.h`); `sampler.c` now calls it. One lowering serves U11 sampler compare and U13 depth/stencil compare.

### Tests (`test/test_vulkan.c`, shaders in `test/bindless_spv.h`)
Four new pixel/clean tests (new shaders `MRT_FRAG`, `SOLID_PC_FRAG`, `DEPTHPC_{VERT,FRAG}`, compiled with `glslc`):
- `vk_pipeline.alpha_blend` — clear (0.2,0.4,0.6), draw src (1,0,0,0.5) through `MEL_GPU_BLEND_ALPHA`; src-over
  result (0.6,0.2,0.3,1) pixel-verified.
- `vk_pipeline.mrt_two_targets` — one pipeline, two color targets; loc0 and loc1 written to two attachments, both read back.
- `vk_pipeline.depth_compare` — explicit depth_stencil (LESS); near-red over far-green; nearer wins, pixel-verified.
- `vk_pipeline.msaa_renders_clean` — 4-sample pipeline into a 4-sample attachment; clean submit (no VUID) is the proof.

## Kludges and debt (confessed, MEL-ENGINE-VIII)

- **MSAA is wired but not pixel-verified.** Reading an MSAA target back requires a resolve, and `cmd_resolve` is the
  separate U10/U16 slice. The test proves pipeline sample-count plumbing + attachment compatibility via a clean submit
  (the validation layer fires on any mismatch), not a resolved pixel.
- **Two render-states are unverifiable on this host.** MoltenVK does not expose `fillModeNonSolid` or `depthBounds`,
  so wireframe/point fill and the depth-bounds test **degrade with a warning** here and have no positive test. The
  lowering + gating are implemented; they will exercise on a device that grants the features (a desktop Vulkan box).
  No blind default was baked (MEL-CODE-007).
- **Stencil is lowered but only structurally exercised.** Per-face stencil state builds and the D24S8 stencil
  attachment format is wired, but there is no stencil **pixel** test (write-then-compare). The depth_compare test
  covers the depth half of the depth/stencil block; a stencil-mask test is the obvious follow-on.
- **Color-write-mask zero means no writes** (the Vulkan/D3D12 convention). For a hand-authored `color_targets[]`
  entry a forgotten `write_mask` paints nothing. Mitigated by the `MEL_GPU_BLEND_OPAQUE/_ALPHA` initializers and the
  scalar `color_format` shortcut (both set ALL); documented in the header. Not reinterpreted to ALL (that would be a
  silent default, MEL-CODE-007).
- **The compute pipeline still duplicates** the layout-build logic (pre-existing debt); the graphics/compute
  `mel_gpu__build_pipeline_layout` refactor is still deferred. This slice did not touch the compute path.
- **The render-pass floor's MRT/MSAA path (`make_pipeline_compat_render_pass`) is compiled but unexercised** — every
  Melody target here grants dynamic rendering, so the `!dynamic_rendering` branch never runs on this host. Written
  correct-by-construction to keep the §7.2 floor faithful (MEL-ENGINE-VII), not validated at runtime.
- **`Mel_Gpu_Blend_Factor` / `_Blend_Op` / `_Stencil_Op` / `_Fill` / `_Front_Face` and the color-write-mask
  constants are enums** (MEL-CODE-001) — protocol maps onto the Vulkan/D3D12 enums, the same carve-out and Rule-#1
  flag as the existing format/topology/cull/compare enums. Flagged per Rule #1.
- **clang-format not applied.** Homebrew-LLVM `clang-format` reports the module's committed files as already
  non-compliant at HEAD (e.g. `test_vulkan.c` differs on 249 lines before my edits), so reformatting would churn
  unrelated lines. I matched the file's actual committed style instead. (MEL-CODE-004 flag: the repo's `.clang-format`
  and the committed code disagree.)
- **Dynamic-state maximization (§6.5) not pursued.** Blend/depth/stencil state is static in the PSO; the
  `VK_EXT_extended_dynamic_state*` "maximize dynamic state" lever is a distinct U13 sub-slice, out of scope here.
- **Process:** uncommitted on worktree branch `gpu-m2-vk-u13` (commit only when asked). Built from local HEAD
  `5b7157e` (origin/main was an ambiguous base; the worktree was branched from local HEAD explicitly).

## Rule-#1 flag — comments
The global `~/CLAUDE.md` says "Never write comments," but this entire module is densely commented (spec-section and
MEL-ENGINE tags throughout), which the project CLAUDE.md actively encourages ("cite it by tag"). I matched the
pervasive house style rather than leave my additions as the lone uncommented outlier. **Halt-and-query:** if the
global rule governs here, say so and I will strip comments from the additions (and, if wanted, the module).

## CLAUDE.md / repo-convention suggestions (recommendations only)
- The `.clang-format` config and the committed code disagree across the gpu module; either reformat the module once
  or relax/align the config, so MEL-CODE-004 is checkable rather than aspirational.
- `modules/gpu/readme.md` is still absent (flagged in three prior writeups). The render-state conventions belong
  there: scalar `color_format` = single opaque target, `color_targets[]` = MRT, `depth_stencil` NULL = default-LESS,
  write-mask 0 = no writes.

## Suggestions
- Next U13 render-state follow-ons: a stencil-mask pixel test; `cmd_resolve` (U10/U16) to make the MSAA test pixel-
  exact; then the dynamic-state lever and the cache-control/`pipeline_binary`/GPL slices.
- The render-state on a desktop Vulkan device would close the wireframe / depth-bounds verification gap that MoltenVK
  leaves open.

## Shader sources (for `test/bindless_spv.h` regeneration)
Regenerate with `glslc -fshader-stage={vert,frag} -mfmt=c <src>` then wrap as `static const uint32_t NAME_SPV[] = { … };`.

```glsl
// mrt.frag — two color outputs (MRT)
#version 460
layout(location = 0) out vec4 o0;
layout(location = 1) out vec4 o1;
void main() { o0 = vec4(0.25, 0.5, 0.75, 1.0); o1 = vec4(1.0, 0.0, 0.5, 1.0); }
```
```glsl
// solidpc.frag — push-constant colour (blend / stencil / MSAA draws)
#version 460
layout(push_constant) uniform Root { vec4 color; } root;
layout(location = 0) out vec4 o_color;
void main() { o_color = root.color; }
```
```glsl
// depthpc.vert — fullscreen triangle at a push-constant depth
#version 460
layout(push_constant) uniform Root { vec4 color; float depth; } root;
void main() {
    vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
    gl_Position = vec4(p * 2.0 - 1.0, root.depth, 1.0);
}
```
```glsl
// depthpc.frag — push-constant colour (depth-compare test)
#version 460
layout(push_constant) uniform Root { vec4 color; float depth; } root;
layout(location = 0) out vec4 o_color;
void main() { o_color = root.color; }
```
