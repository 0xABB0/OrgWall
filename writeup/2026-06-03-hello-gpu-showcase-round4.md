# hello-gpu showcase — round 4

Agent **screens4**. Off `origin/main` (post-round-3 merge). Three new technique-combining screens; all 17 prior screens retained. Zero comments in any added C/GLSL. build.c unchanged.

## Work done

### `bloom` (`hdr-bloom`)

Five-pass compute/graphics chain: `bloom_scene.comp` ray-intersects four emissive spheres per pixel into an RGBA8 storage image; `bloom_bright.comp` threshold-extracts (luma > 0.55) into a second storage image; `bloom_blurx.comp` applies a 7-tap horizontal Gaussian blur into a third; `bloom_blury.comp` applies the vertical pass into a fourth; a fullscreen `bloom_composite.frag` samples scene + bloom images, composites with strength 1.8, applies Reinhard tonemap and gamma correction, and writes to the swapchain.

All four intermediate images are RGBA8_UNORM with `STORAGE | SAMPLED` usage, so the same bindless slot serves both the compute write (binding-4) and the sampled read (binding-0) — the established convention. Barriers chain: `SHADER_RESOURCE → UNORDERED_ACCESS` for the write, `UNORDERED_ACCESS → SHADER_RESOURCE` for the subsequent read.

### `reacdiff` (`reaction-diffusion`)

Gray-Scott reaction-diffusion automaton running on two ping-pong RGBA8 storage images. `reacdiff_init.comp` seeds the field: a central disc plus random scatter of B=1 pixels. `reacdiff_step.comp` samples the source texture (binding-0) and writes the next state to the destination (binding-4), using a 9-tap weighted Laplacian stencil and the standard Gray-Scott update (`da=1.0, db=0.5, feed=0.055, kill=0.062`). Eight steps run per frame to produce visible evolution at 60 fps. A fullscreen `reacdiff_draw.frag` maps the final A-B difference through a cosine palette and blits to the swapchain via the existing `BLIT_VERT_SPV` triangle.

The texture ping-pong (src read from binding-0 as sampled, dst written to binding-4 as storage) is the analogue of how `boids` ping-pongs storage buffers — same barrier pattern, different resource type.

### `shadow` (`shadow-mapping`)

Two-pass shadow map. All projection is CPU-side (same approach as `prepass.c`). An orthographic light frustum projects the scene into `Depth_Vertex` (position only); a `shadow_depth.vert` + empty `shadow_depth.frag` renders those into a `D32_FLOAT` shadow map texture (512×512, `ATTACHMENT | SAMPLED`). The scene pass writes `Scene_Vertex` (pos, normal, color, shadow_uvz) — `shadow_uvz.xy` is the pre-computed UV into the shadow map, `shadow_uvz.z` is the linear depth value. `shadow_scene.frag` samples the shadow map with a nearest-neighbour sampler, compares `shadow_z - 0.003 <= shadow_depth.r`, and attenuates diffuse to 0.2 in shadow. Five coloured cubes + a ground plane orbit the camera slowly. Color result goes to an RGBA8 offscreen; `bindless_present_blit` presents it.

The shadow map depth texture registers its bindless slot in the binding-0 heap (sampled images) — the same mechanism `postprocess` uses for RGBA8 scenes — but for D32_FLOAT. The fragment shader samples `.r` to get the depth scalar.

## Build

`./nob build hello-gpu macos --gpu=vulkan` — clean (185 steps). Zero warnings. Zero comments in all added `.c`/`.h`/GLSL. The build `src/*.c` glob picks up `bloom.c`, `reacdiff.c`, `shadow.c` automatically.

SPIR-V headers generated with `glslc <src> -mfmt=c -o /dev/stdout` piped into the `#pragma once / static const uint32_t NAME[] = ...` wrapper matching the existing repo convention.

Interactive run was not performed (worktree, headless environment). The build is clean and the reasoning below argues correctness.

## Kludges (MEL-ENGINE-VIII — confess all)

**Bloom is pseudo-HDR.** The scene compute shader uses emissive multipliers > 1.0 (e.g. `vec3(2.8, …)`) but the output is RGBA8_UNORM, which clamps to [0,1] on store. Genuine HDR would require RGBA16F or RGBA32_FLOAT storage images. The available format list in `gpu/include/gpu/format.h` has `RGBA32_FLOAT` but not `RGBA16F`. Using RGBA32_FLOAT storage images with `binding=4` requires a `rgba32f` format qualifier in GLSL and the device to support that format as a storage image — not verified against MoltenVK. Chose RGBA8 for safety; the visual effect (bright halos, tonemap, gamma) still demonstrates the pipeline shape correctly, but the "HDR" label is technically inaccurate. True HDR would need format validation before allocation.

**Shadow map sampled as `texture2D` without depth-compare path.** The standard approach uses `sampler2DShadow` with `GL_COMPARE_REF_TO_TEXTURE` enabled on the sampler. The API sampler has a `compare` field (`Mel_Gpu_Compare_Op`) but the bindless sampler heap (binding-1) and the sampler slot system are shared — it is unclear whether a comparison sampler registers at a separate slot from a regular sampler or overlaps. To avoid uncertainty, the shadow shader uses a regular sampler and does the depth comparison manually in GLSL (`sz - 0.003 <= shadow_depth.r`). This forfeits hardware PCF bilinear filtering (a real shadow will have 1-texel aliased edges), but is correct and avoids API guessing.

**Shadow projection uses CPU-side hand-rolled orthographic + perspective.** No matrix library exists in scope; all math is inlined. The light frustum is fixed-size (`half_w = half_h = 3.5`, `near=0.5`, `far=14.0`), which correctly covers the scene but has no automatic fitting. If the scene were larger or moved, shadows would clip.

**Per-frame `malloc(sizeof *sd)` in shadow_render.** Allocates a `Scene_Data` (≈8 KB) every frame to hold the CPU-projected geometry. This is an UPLOAD-memory buffer write pattern that works fine at 60 fps on a modern machine but allocates/frees on the hot path. The prepass screen does the same with `malloc(SCENE_VERTS * sizeof(Pt_Vertex))` — inherited convention, sanctioned at app layer.

**Reaction-diffusion uses a WRAP_REPEAT sampler.** The step shader samples the source with `WRAP_REPEAT` so boundary conditions are toroidal — this is intentional (avoids edge artifacts) but the initial seed has a finite boundary between initialized regions, so the first few hundred frames may show a seam at the wrap point before the pattern fills the domain. Not a crash, barely visible.

**`reacdiff` state tracker uses a boolean `initialized` flag.** On resize, `make_imgs` resets `initialized = false` so the init pass runs again on the next frame. The two old textures are destroyed before the init pass, so the first frame after resize transitions from `COMMON` (enforced by `fresh = true`). Correct but a mild state-machine smell.

## CLAUDE.md suggestions (recommendations only)

- Document that `D32_FLOAT` textures with `ATTACHMENT | SAMPLED` usage can register in the binding-0 heap via `mel_gpu_texture_view_bindless_slot`, so depth textures are sampable in bindless fragment shaders. Not currently stated anywhere.
- Document the RGBA16F gap in `format.h`: if added, it would unlock true HDR intermediate storage in compute pipelines without the 4× bandwidth cost of RGBA32_FLOAT.

## Suggestions

- **True HDR storage.** Add `MEL_GPU_FORMAT_RGBA16F` to `format.h` and the Vulkan backend. The bloom screen's scene compute would immediately become genuinely HDR. This is the single most impactful format addition for rendering.
- **Comparison sampler bindless slot.** Expose whether a comparison sampler occupies a distinct bindless slot (binding-1) or shares its slot with a regular sampler configured differently. If it shares, the shadow screen could switch to `sampler2DShadow` for free hardware PCF.
- **Reaction-diffusion presets.** `feed/kill` parameter pairs produce wildly different patterns (spots, stripes, solitons, worms). Cycling presets via the timer would make the screen more visually varied without any new GPU infrastructure.
