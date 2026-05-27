# hello-gpu 3D examples — a lit cube and a Lorenz attractor, and the three latent defects they flushed out

`apps/hello-gpu` hosted only the static `hello-triangle`. This session added two
animated 3D windows beside it — a directionally-lit spinning cube and a Lorenz
attractor drawn as a 3D line strip — and, in making them work, surfaced and fixed
three pre-existing engine defects that the static triangle had never exercised.
All three backends now run the examples: macOS/Metal, Android/Vulkan (on device),
and web/WebGPU (in-browser, confirmed by Gabbo).

## Work done

### The examples — `apps/hello-gpu/src/`
- `passthrough.{h,c}`: a shared helper exposing `Pt_Vertex` (`float3 pos` +
  `float4 color`), `passthrough_shader`, and `passthrough_pipeline(topology,
  color_format)`. The shader is semantically identical to the triangle's
  pass-through, so the MSL/WGSL are hand-copies and the SPIR-V is reused verbatim
  from `triangle_spv.h` (the arrays are `static const`, so including the header in
  a second translation unit is safe). Both examples share this; they differ only
  in topology and CPU mesh generation, per MEL-ENGINE-IX.
- `cube.{h,c}`: eight corners, six face colours, transformed CPU-side each frame
  (rotation → translate → perspective divide), emitted as a triangle list with
  per-face diffuse shading baked into vertex colours.
- `lorenz.{h,c}`: the classic system (σ=10, ρ=28, β=8/3) integrated to 4000 points
  at init, drawn as a `LINE_LIST`, slowly yawing, with a hue gradient that drifts
  along the curve.
- `graphical_app.h`: added an optional `resize(state, w, h)` callback. The
  swapchain exposes no size getter, and CPU projection needs the aspect ratio;
  the host fires it after `init` and on every resize. The triangle leaves it
  `NULL`, untouched.
- `gpu_host.c`: invokes the new `resize` callback. `main.c`: two more buttons.

The transform is done CPU-side because the `gpu` command surface has no uniforms
and no depth attachment. Occlusion follows from that constraint:
- The cube uses **CPU back-face culling**, not a depth buffer. A convex solid's
  front faces never overlap in screen space, so culling alone is exact and draw
  order is irrelevant. (My first attempt sorted faces by centroid depth — a
  painter's heuristic that is *not* exact and visibly mis-ordered at silhouette
  edges. Culling replaced it.)
- The attractor is lines; it needs no occlusion.

### Defect 1 — no portable buffer update (the dynamic-geometry gap)
`mel_gpu_buffer_map` returns a real pointer only on Metal; Vulkan and WebGPU
return `NULL` by design ("supply `.data` at creation"). The triangle is static so
it never noticed. My per-frame CPU transform needs to rewrite a vertex buffer
every frame, and writing through the `NULL` map pointer is what crashed Android.
Added `mel_gpu_buffer_write(buf, data, size)` to the public `buffer.h`,
implemented per backend: Metal `memcpy` into shared `contents`; Vulkan
`vkMapMemory`/`memcpy`/`vkUnmapMemory` on the host-visible coherent allocation;
WebGPU `wgpuQueueWriteBuffer` (the WebGPU buffer struct gained a `device` pointer;
it already carried `CopyDst`). Both examples triple-buffer their vertex data
(`vbo[3]`, cycled per frame) so the CPU never rewrites a buffer the GPU may still
be reading.

### Defect 2 — Android did not link libm — `modules/build.c`
The library failed to `dlopen` at launch: `cannot locate symbol "tanf"`. The
math module's transcendentals (`mel_sinf`/`mel_cosf`/`mel_tanf`, all lowering to
`__builtin_*` → libm calls at `-O0`) are `static inline`, so until my code called
one, no Android translation unit referenced libm. `modules/build.c` linked `-lm`
for Linux but not for Android; macOS folds libm into libSystem so it never
surfaced there either. Added `-lm` to the `MEL_PLATFORM_ANDROID` link flags,
mirroring Linux. Verified `libm.so` now appears in the `.so`'s `NEEDED` entries
and the app loads and initialises Vulkan on a Pixel-class device.

### Defect 3 — web's stale WebGPU header — `modules/gpu/src/webgpu/device.c`
The web build failed to compile: `'emscripten/html5_webgpu.h' file not found`.
That is emscripten's *old* built-in-WebGPU header. The build uses
`--use-port=emdawnwebgpu`, which does not ship it — `emscripten_webgpu_get_device`
moved into `<webgpu/webgpu.h>`, already included via `webgpu_backend.h`. The
include was pure dead breakage; removing it was the entire fix. Pre-existing,
untouched by my examples — web simply had not been compiled since the port swap.

### Verification
- **macOS/Metal**: builds, links, runs (visual confirmation by Gabbo across the
  session).
- **Android/Vulkan**: `libmelody.so` links with `libm.so` in `NEEDED`; on device
  the loader reports `Load … libmelody.so … : ok` and the Adreno Vulkan driver
  initialises. All three examples render on device, confirmed by Gabbo on a
  Pixel-class device.
- **Android/WebGPU** (`--gpu webgpu`): runs on device with Dawn over the Adreno
  Vulkan driver (WebGPU→Vulkan, distinct from the direct-Vulkan build). All three
  examples render, confirmed by Gabbo.
- **web/WebGPU**: compiles and `WEBLINK`s; Gabbo confirmed it runs in-browser. I
  could not verify in-browser here (no GPU browser on the host).

## Kludges

- **CPU transform with a constant NDC depth.** Every projected vertex is emitted
  with `z = 0.5`, because there is no depth test and occlusion comes from culling
  (cube) or not at all (lines). This is correct for these two examples but does
  not generalise: a non-convex *solid* mesh would render wrong without a real
  depth buffer. The debt is the missing depth-attachment capability in the `gpu`
  module, not the examples per se.
- **Hand-duplicated shader source.** `passthrough.c` re-copies the triangle's MSL
  and WGSL strings rather than sharing them from one location, and reuses the
  triangle's SPIR-V by name. If the pass-through shader ever changes, two copies
  drift. A shared shader-source unit would retire this.
- **Examples carry their own CPU math.** Small `V3` helpers and a hand-rolled
  `hue` live in the example files instead of using the `math` module
  (`Mel_Vec3`, `mel_mat4_perspective`, …). Kept self-contained to avoid pulling
  matrix machinery for a few operations, but it duplicates what the engine
  already offers.
- **Fixed triple-buffer depth.** `vbo[3]` is a guess at frames-in-flight, not
  derived from the swapchain. On Vulkan `frame_begin` already fences the prior
  frame, so even single-buffering would be safe there; on Metal it relies on the
  count exceeding the drawable backlog. It works but the number is unprincipled.

## CLAUDE.md suggestions (recommendations only)

- No change proposed to the root or module CLAUDE.md. The instructions held up.
- `tools/build/platforms.md` still describes WebGPU as the web path only; with
  this and the prior native-webgpu work it is doubly stale (also flagged in
  `2026-05-27-native-webgpu.md`). A combined refresh of that section is overdue.

## Suggestions

- **Add a depth-buffer option to the `gpu` module.** The single largest constraint
  this session. A depth attachment on the swapchain/pass would let the examples
  (and any future 3D content) render arbitrary meshes correctly and drop the
  CPU-side culling/`z=0.5` workaround. Honors MEL-ENGINE-I.
- **Add a uniform/push-constant binding to the command API.** With it, the cube's
  MVP could live on the GPU and the per-frame CPU transform + `mel_gpu_buffer_write`
  churn would vanish. The buffer-usage enum already has `MEL_GPU_BUFFER_UNIFORM`;
  only the bind/draw plumbing is missing.
- **Make the render source display-linked.** `render_source.c` drives a free-running
  60 Hz reactor timer against a vsync'd present, which beats and judders under
  motion (invisible on the static triangle, obvious on the cube). A `CADisplayLink`
  (macOS) / `choreographer` (Android) / `requestAnimationFrame` (web) source would
  remove it for every GPU app. Left untouched this session by scope choice.
- **Audit for other libm-only-on-Linux gaps.** The Android `-lm` omission implies
  the platform link-flag lists are maintained by hand and diverge. A shared
  "common math/runtime libs" set applied across Linux/Android/win32 would prevent
  the next dormant-symbol crash.
- **A depth-buffered `MEL_GPU_BUFFER_INDEX` example.** Once depth and indices land,
  an indexed, depth-tested mesh (a torus, a loaded model) would exercise the parts
  the current minimal API still stubs, and make `hello-gpu` a real capability
  showcase rather than three special cases.
