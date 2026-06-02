# hello-gpu showcase — round 3

Agent **screens3**. Off `origin/main` (comment-free `b0ab013`). Three new technique-combining screens added; all 14 prior screens retained. Zero comments in any added C/GLSL.

## Work done

Three self-contained screens (`<name>.{c,h}` + `<NAME>_APP`, wired into `main.c` menu, `HELLO_GPU_AUTO`, and the `OPEN_BUTTON`/include rows). Each owns its state in `init`, runs one frame's passes in `render`, tears down leak-free.

### `raymarch` (`raymarch-sdf`)
Pure-GPU fragment-pipeline raymarcher. Fullscreen triangle (reuses `BLIT_VERT_SPV`, which is the compiled `fullscreen.vert` emitting `v_uv`) feeds `raymarch.frag`: an SDF field of four `smin`-blended orbiting spheres over a torus and a checkered ground plane, orbiting camera, 96-step sphere trace, soft shadows (48-step), 5-tap AO, Fresnel rim, Blinn specular, tonemap + gamma. No offscreen target — renders straight to the swapchain. Push constants carry `time` + `aspect` (from `resize`). HUD via `hud_frame`.

### `mandelbrot` (`mandelbrot-explorer`)
Compute → storage-image → bindless-present, mirroring the proven `dispatch_indirect` storage-image path. `mandelbrot.comp` (binding=4 `rgba8 image2D[]`, `imageStore`) computes a continuously-iterated Mandelbrot with smooth (continuous) escape-time coloring and a cosine palette that cycles. Offscreen storage image is sized from **`mel_gpu_swapchain_extent(sc)`** at `init` and recreated on `resize` (the round-2 fixer this round was told to exercise). Camera zoom oscillates with a cosine envelope toward the seahorse-valley point `(-0.7436, 0.1318)`, iteration count scales with zoom depth. Blit via the shared `Bindless_Present` helper.

### `boids` (`gpu-boids`)
Compute flocking → instanced triangle draw, larger/prettier than the particles screen. Ping-pong storage buffers (`buf[2]`): `boids_sim.comp` reads `src`, writes `dst`, applying separation / alignment / cohesion (brute-force O(N²) over the view radius) plus a wandering goal target and speed clamping, with reflective box walls. `boids_draw.vert` draws 3 verts/boid as a velocity-oriented arrowhead, heading-hued, reusing `instances.frag`. Additive-free opaque pass. `buf_slot[]` swapped each frame; HUD via `hud_frame`.

## Build & validation

- Build: `./nob build hello-gpu macos --gpu=vulkan` — clean (181 steps, links `raymarch.o`/`mandelbrot.o`/`boids.o`). `build.c` globs `src/*.c`, so no build.c edit was needed.
- Worktree note: the worktree had `nob.c` but no compiled `nob`; bootstrapped it (`clang -std=c23 -g -Imodules/build -o nob nob.c`) so the build operated on the worktree sources rather than the shared checkout.
- Headless (~4 s each, validation on, Apple M3 Pro / Vulkan via MoltenVK): all three report **0** of `VUID-/[ERROR]/[WARN]/device.lost/assert/leak:` and a single `swapchain ready`; none hit a bindless-`unavailable` fallback (each took the real GPU path). Regression-checked `particles`, `prepass`, `dispatch-indirect` — still 0 errors.
- Comment scan: 0 in all added `.c/.h` and GLSL.

## Skipped / deliberate constraints

- **No `double` in mandelbrot.** The device (`gpu_host.c`) does not request `shaderFloat64`, and I may not edit `modules/gpu`. The compute kernel is single-precision `float`; consequently zoom depth is capped (~1e-5 before pixelation), so the auto-zoom oscillates within a float-crisp envelope instead of diving arbitrarily deep. A true deep-zoom explorer wants fp64 (or perturbation/series-approximation in fp32×2). Note for the builder: enabling `shaderFloat64` at device creation — gated on `caps.shader.fp64` — would let this screen, and any future high-precision compute, go far deeper.
- **No GPU timestamp queries.** Per boundaries, the round-3 timing feature is not on this base; HUD fps stays CPU-side EWMA via the existing `hud.c`.

## Kludges (MEL-ENGINE-VIII — confess all)

- **Boids is brute-force O(N²).** No spatial hash / grid binning; every boid scans all others. At the shipped `BOID_COUNT = 4096` (≈16.7M neighbor tests/frame) it runs clean on the M3 Pro, but it is needlessly heavy for weak GPUs (MEL-ENGINE-VI). I lowered it from an initial 8192 (67M tests, label "8k") to 4096 ("4k") specifically to keep dignity on weaker hardware. The honest fix is a uniform-grid binning pass (build/scan/scatter into cells, then scan only the 3×3 neighborhood) — that would make 64k+ boids cheap. Left undone; it is its own screen-sized effort.
- **Mandelbrot float32 ceiling** (above) is a precision kludge forced by the un-editable device feature set, not a design choice.
- **Reused `instances.frag` and `BLIT_VERT_SPV` by symbol, not by matching source filename.** Pre-existing repo convention (the `blit_spv.h` symbol is the compiled `fullscreen.vert`); I followed it rather than introduce a redundant `fullscreen_spv.h`. Slightly surprising to a new reader but consistent with how `compute_plasma`/`particles`/`postprocess` already wire fullscreen + instance shaders.
- **Boids screen-space squish.** The draw vertex shader applies the aspect correction to the boid's full world position (not just its local offset, as `particle_draw.vert` does), mapping the square sim domain into a centered square on-screen. Intentional (keeps the flock circular, not stretched), but it diverges from the particles convention — flagged so it is not mistaken for a bug.

## CLAUDE.md suggestions (recommendations only)

- Document the worktree-vs-shared-checkout `nob` gotcha: from a fresh worktree, `nob` must be bootstrapped (`clang -std=c23 -g -Imodules/build -o nob nob.c`) before `./nob build …`, otherwise the repo-root binary builds the shared checkout and silently reports "no work to do" for worktree edits.
- Note in the hello-gpu area that `BLIT_VERT_SPV` is the canonical fullscreen-triangle vertex shader (emits `v_uv`); reuse it for any fullscreen fragment screen rather than minting a new `*_spv.h`.

## Suggestions

- **Spatial-grid boids** as a follow-up screen (or upgrade): a compute binning pass would showcase the indirect/atomic-counter machinery the RHI already has, and let the flock scale an order of magnitude. Good MEL-ENGINE-IX composition demo (binning ⊕ sim ⊕ instanced draw).
- **fp64 device opt-in.** If `gpu_host` requested `shaderFloat64` when `caps.shader.fp64` is set, the mandelbrot screen could become a genuine deep-zoom explorer (UI-driven center/zoom), and compute screens generally gain a precision path. Cheap, gated, no cost when absent.
- The mandelbrot palette + raymarch SDF field are both easy seams for an interactive variant once pointer/scroll input is plumbed into the GPU view — both already key everything off push constants.
