# hello-gpu showcase round 2: MSAA, GPU-driven dispatch, particles, render-graph, live HUD

## Work done

Four new self-contained `Graphical_App` screens plus the live caps/FPS HUD the
round-1 appsmith deferred. The ten existing screens are untouched. Every screen is
wired into the host menu and the `HELLO_GPU_AUTO` switch; `build.c` is unchanged
(`mel_sources(app, ALWAYS, "src/*.c")` globs the new sources).

| screen | `HELLO_GPU_AUTO` | technique | new RHI surface exercised |
|---|---|---|---|
| `msaa.c` | `msaa` | a spiky rotating star rendered into a 4× multisample attachment that resolves on-tile into a single-sample target (`Mel_Gpu_Color_Attachment.resolve_view`), shown split-screen against a 1× reference of the same star | MSAA `samples`, on-tile resolve, `mel_gpu_format_properties(...).sample_counts` |
| `dispatch_indirect.c` | `dispatch-indirect` / `indirect` | a cull compute pass atomically appends survivors of a moving cull region; a one-thread pass turns that GPU-side count into `{gx,1,1}`; `cmd_dispatch_indirect` runs the shade pass at exactly that size, splatting survivors into a bindless storage image | `cmd_dispatch_indirect`, `MEL_GPU_BUFFER_INDIRECT`, `INDIRECT_ARGUMENT` state, storage-image bindless (heap binding 4) |
| `particles.c` | `particles` | a compute pass integrates 40 000 particles in place under a Lissajous attractor + drag + respawn; one instanced draw reads the same buffer (additive blend) | compute-integrate → instanced draw, in-place per-invocation RMW |
| `prepass.c` | `prepass` | a depth-only prepass establishes nearest-Z over a heavily overlapping cube stack, then a lit pass reuses that depth with `COMPARE_EQUAL` + depth-write off, so each pixel shades exactly once (overdraw killed) | shared depth attachment across two passes, full `Mel_Gpu_Depth_Stencil` control (EQUAL), colour-attachment-less prepass |

### Caps / FPS HUD (the sanctioned `gpu_host.c` change)

The round-1 blocker was that the per-window render callback held no handle to the
GUI label. Fixed with a minimal, reusable seam:

- `gpu_host.c` captures the label handle into `Gpu_Window.status` at window
  creation, and sets a file-static `g_rendering` to the active window around the
  render call. `window_render` runs on the reactor thread, which on macOS *is* the
  AppKit main thread (`main()` calls `mel_reactor_spawn(THREADED, ...)` in place, so
  the CFRunLoop and the render-source timer share that thread) — so the Cocoa
  `setText:` is in-band, no cross-thread hop.
- New public seam in `gpu_host.h`: `void gpu_host_set_status(str8)` (routes to the
  rendering window's label; no-op outside a render callback). The HUD reads caps off
  the `dev` the host already passes to `init`, so no device accessor was added.
- `hud.{c,h}` is a reusable helper any screen embeds: `hud_init(dev)` snapshots the
  adapter name + bindless tier from `mel_gpu_device_caps`; `hud_frame(dt, suffix)`
  EWMA-smooths the frame time into an FPS reading and pushes
  `"<adapter> · bindless <tier> · <fps> fps · <suffix>"` to the label ~5×/s.

Verified live (transient probe, since reverted): the label reads e.g.
`Apple M3 Pro · bindless full · 60 fps · MSAA 4× | left resolved, right 1×`. All
four new screens carry the HUD; the ten originals are left as-is.

### Shaders

GLSL under `apps/hello-gpu/shaders/`, compiled to embedded `*_spv.h` with
`glslc … -mfmt=c` (the round-1 recipe). New sources: `star.vert`,
`msaa_compose.frag`, `cull.comp`, `buildargs.comp`, `clear.comp`, `shade.comp`,
`particle_sim.comp`, `particle_draw.vert`, `depth_only.frag`. Binding conventions
match the heap signature exactly: set-0 binding 0 = sampled image, 1 = sampler,
2 = storage buffer, 4 = storage image (confirmed against
`modules/gpu/src/vulkan/binding.c`). A texture created `STORAGE | SAMPLED` registers
its view at the *same* slot index in both the binding-0 and binding-4 heaps, so one
slot value drives both the compute write and the present sample.

## Build & validation

- Build: `./nob build hello-gpu macos --gpu=vulkan` — clean (173/173 then
  incremental). All new screens pass `clang-format` (`.clang-format` at repo root).
- Headless render-verify (≈3-4 s each, validation on, Apple M3 Pro / MoltenVK):
  **all 14 screens** (10 original + 4 new) report **zero** `VUID` / validation
  error / device-lost / assertion / `[ERROR]` / `leak:`, and `swapchain ready` each.
  The four new screens additionally report **zero** `[WARN] validation` lines.
- **Sync validation** (temporarily flipped `sync_validation = true` in `gpu_host.c`,
  then reverted): `msaa`, `dispatch-indirect`, `plasma`, `post`, `depth` all report
  **zero** SYNC hazards. This is the load-bearing proof for dispatch-indirect — the
  cull → buildargs → indirect-read chain and the clear → shade → sample
  storage-image chain are correctly synchronized.
- MSAA: 4× granted on MoltenVK (no fallback warning); the `samples` query against
  `format_properties.sample_counts` picks the best of {4,2,1}.
- Prepass: `COMPARE_EQUAL` across the two passes raises no Z-fighting / validation
  complaint — position is bit-identical because both pipelines run the same
  `scene3d.vert` over the same vertex buffer.

## Kludges (MEL-ENGINE-VIII — full confession)

1. **`g_rendering` file-static in `gpu_host.c`.** `gpu_host_set_status` routes to the
   "currently rendering" window via a file-static set around the render call rather
   than threading a context handle into the `render` signature. It is correct because
   the reactor (and thus every render callback) is single-threaded, and the static is
   cleared after each call — but it is a hidden channel, not a parameter. Threading a
   window/HUD handle into `Graphical_App.render` would be the clean form; it would
   touch the `render` signature of all ten existing screens, out of scope here.

2. **MSAA multisample attachment barriered by the app.** `cmd_begin_rendering` sets
   the multisample (`ms`) and resolve (`aa`) images to `COLOR_ATTACHMENT_OPTIMAL` in
   the rendering-info but issues **no** layout transition, so the app must barrier
   *both* into `RENDER_TARGET` first. My first cut barriered only the resolve target
   and tripped `VUID-...-09592` (the ms image still `UNDEFINED`); fixed by adding the
   `ms` barrier with its own COMMON→RENDER_TARGET (fresh) / RENDER_TARGET (warm)
   prior-state. Correct, but it is app code compensating for the same
   persistent-state-tracker detail the round-1 writeup flagged (kludge 1 there). The
   `ms` surface is never sampled, so it simply stays in RENDER_TARGET across frames.

3. **Offscreen targets sized from the `resize` callback, recreated on resize.** Per
   the brief, the new screens do **not** use a fixed `OFF_W/OFF_H`; they allocate
   their offscreen colour/depth/MSAA/storage-image targets in `resize(state, w, h)`
   with the real client size and reallocate when it changes. The cost: a
   `targets_fresh`/`img_fresh` flag so the first frame after a (re)create declares the
   texture's COMMON initial state to the persistent tracker, the warm path declares
   the real prior state. This is the documented round-1 gotcha, paid per-screen. When
   `mel_gpu_swapchain_extent` lands (the `fixer`'s task) the resize-driven sizing
   stays correct; only the round-1 screens that hard-coded 1024×768 need it.

4. **dispatch-indirect survivor/args ring in UPLOAD memory.** The survivor count is
   zeroed each frame through the host mapping (`mel_gpu_buffer_mapped` + a single
   `u32` store) rather than on the GPU, because there is no cross-workgroup reset
   primitive and `mel_gpu_buffer_write` on a DEVICE buffer does a staging copy with a
   `WaitIdle` (a per-frame stall). To keep that host write off an in-flight slot I
   round-robin 3 sets of {survivors, args} buffers — the slot I touch was last used 3
   frames ago, retired under vsync. Correct and stall-free, but it relies on the
   3-deep ring outpacing the in-flight depth; a `HELLO_GPU_FRAMES=N` clean exit (still
   absent) plus an explicit fence would let me assert it rather than argue it.

5. **shade.comp does non-atomic RGBA read-modify-write into the storage image.**
   Overlapping survivor discs additively blend by `imageLoad`/`imageStore` without an
   atomic, so two survivors landing on the same texel in one dispatch can lose a
   contribution (a faint flicker, never a crash or validation hazard — intra-dispatch
   shader writes to a storage image are unordered-but-legal per spec, and sync-val is
   clean). Acceptable for a showcase splat; a real accumulator would use
   `imageAtomicAdd` on an R32_UINT image or a separate additive blend pass.

6. **App uses `malloc`/`calloc`/`free` (sanctioned).** The new screens allocate state
   and per-frame scratch with libc, matching the existing screens and the brief. Trips
   the `no-bare-malloc` *warning*; it is app layer, MEL-CODE-003 binds engine code.

7. **Comments in app sources (Rule #1 tension, inherited).** Global CLAUDE.md says
   "Never write comments"; the local hello-gpu house style (and the round-1 screens)
   comment heavily, and the brief said to match it. I followed the local convention.
   This is the same direct conflict the round-1 writeup confessed (its kludge 5); per
   Rule #1 it still needs Gabbo's explicit say-so. If the global rule wins, strip every
   comment from the new `.c`/`.h` and the GLSL — the code stands without them.

8. **Compile-time geometry in fixed arrays (MEL-CODE-002 tension, inherited).**
   `CORNERS[8]`, `FACES[6][4]`, `FACE_COLOR[6]`, the per-spoke star triangle, the
   GLSL `corners[6]` — bounded-by-construction constants, not `MEL_MAX_*` ceilings, so
   not the failure mode MEL-CODE-002 targets. Count-driven data (agent pool, particle
   pool, survivor list, scene VBO) is `malloc`'d / device-buffer'd from a count.

## Skipped / not landed

Nothing from the brief was skipped. Both required screens (MSAA, dispatch-indirect)
and the HUD landed, plus the two optional depth screens (GPU particle system,
mini render-graph depth-prepass). No screen needed an unlanded RHI feature, so there
is nothing queued for the fixer beyond the extent accessor already on their plate.

## For the fixer / Gabbo

- **`mel_gpu_swapchain_extent(sc)`** (fixer's task): not consumed here — the new
  screens size from `resize` per the brief — but it would simplify a screen that wants
  its target size *inside* `render` without caching the resize values. The round-1
  depth/post screens that hard-code 1024×768 are the real consumers.
- **`cmd_begin_rendering` should transition attachments it names** (or document that
  it does not): kludge 2. Today the caller must barrier both the multisample and the
  resolve image into RENDER_TARGET even though the rendering-info already declares the
  layout. A one-line note in `rendering.h` next to `resolve_view` would have saved the
  first-cut VUID.
- **`imageAtomicAdd` storage-image showcase** would retire kludge 5 and demonstrate
  atomic image ops — a natural round-3 screen if the format/feature is exposed.

## CLAUDE.md suggestions (recommendations only — not applied)

- The hello-gpu orientation already (per round-1's suggestion) should note the shared
  swapchain recorder's persistent state tracker. Round-2 adds a second instance of the
  same trap: `cmd_begin_rendering` does **not** transition the attachments it names —
  both the colour/MSAA attachment and any `resolve_view` must be barriered into
  RENDER_TARGET by the caller. Worth one line in the orientation.

## Suggestions

- A `HELLO_GPU_FRAMES=N` clean-exit path (round-1's suggestion, still open) would let
  the leak detector and `atexit` teardown run under the windowed harness and let
  dispatch-indirect's ring assumption (kludge 4) be asserted with a fence rather than
  argued.
- The HUD seam (`gpu_host_set_status` + `hud.{c,h}`) is reusable by the ten original
  screens for free — a follow-up could add `hud_init`/`hud_frame` to each so every
  screen surfaces live caps + FPS, not just the four new ones.
