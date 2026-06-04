# GPU RHI — Metal backend: real rendering from Slang MSL bundles

## Work done

Brought the Metal backend (`modules/gpu/src/metal/macos/`) from clear-present to **actual
rendering** by consuming the Slang-emitted MSL bundles. The headline gap closed: shader → pipeline →
bind → draw now work; the triangle, gradient, and quad MSL-bundled shaders render to a texture and
read back pixel-exact.

### Shader path (`pipeline.m`)
- `shader_create_from_bytecode(MSL)`: builds one `MTLLibrary` per stage via
  `newLibraryWithSource:` (the bundle blobs are MSL **source**, not precompiled `.metallib`); pulls
  the named `MTLFunction`s by entry name (`vs_main`/`fs_main`). Loud-rejects any non-MSL target
  (`MEL_GPU_SHADER_CREATE_TARGET_UNSUPPORTED`) and missing/empty blobs (`NO_CODE`); a missing entry
  point is a loud `VK_FAILED` with the cause logged. The bundle blobs are NOT NUL-terminated, so the
  `NSString` is built with explicit length (`initWithBytes:length:encoding:`).
- Compute analog (`shader_create_compute_from_bytecode(MSL)`) implemented symmetrically.
- ARC ownership per the round-4 fix: library/functions stored as `void*` via `__bridge_retained`,
  released via `__bridge_transfer` in `shader_destroy`. Entry-name copies via the device allocator
  (`mel_dealloc` on destroy), never `mel_malloc`.

### Pipeline path (`pipeline.m`)
- `pipeline_create` → `MTLRenderPipelineDescriptor`: vertex+fragment functions, per-target color
  pixel format + full blend state (factor/op/write-mask mapped from `Mel_Gpu_Blend`), depth/stencil
  attachment format, `rasterSampleCount`, and a `MTLVertexDescriptor` built from the caller's
  `vertex_layout` (attribute format/offset, single interleaved buffer at a reserved high index).
- `pipeline_compute_create` → `newComputePipelineStateWithFunction:`; threadExecutionWidth recorded.
- Bindless / spec-constants / static-samplers are loud (error for bindless = `MissingFeature`; warn
  for the ignored ones) — never silent. Unmapped color/depth/vertex formats loud-fail.
- Pipeline state stored as `void*` (`__bridge_retained`), freed in `pipeline_destroy`.

### Recording (`record.m`)
- `cmd_bind_pipeline` → `setRenderPipelineState:` and caches the primitive type (Metal takes the
  primitive at draw time, not at pipeline build); compute pipelines on the render-encoder list are a
  loud `MissingFeature`.
- `cmd_bind_vertex_buffer` → `setVertexBuffer:offset:atIndex:` at the reserved vertex-buffer index
  (`MEL_GPU_METAL_VERTEX_BUFFER_INDEX = 30`).
- `cmd_push_constants` → `setVertexBytes:`/`setFragmentBytes:` at buffer index 0 (Slang lowers
  `[[vk::push_constant]]` to `buffer(0)`); nonzero offset is a loud error.
- `cmd_bind_index_buffer` caches buffer+type; `cmd_draw` → `drawPrimitives:`, `cmd_draw_indexed` →
  `drawIndexedPrimitives:`.
- `cmd_copy_texture_to_buffer` / `cmd_copy_buffer` now real (blit encoder; the open render encoder
  is ended first since Metal permits one encoder per command buffer) — needed for headless readback.
- `cmd_dispatch` / `cmd_dispatch_indirect` are loud `MissingFeature` (no compute encoder wired into
  the render-encoder command-list model; no MSL compute bundle exercises it).
- The former once-per-list "loud no-op" warn helper is gone; every unimplemented call now errors with
  a precise cause (MEL-ENGINE-VIII).

### Caps / device / header
- `caps.m`: `shader.bytecode_passthrough.msl = true`.
- `mtl_backend.h`: `Mel_Gpu_Shader_Obj` / `Mel_Gpu_Pipeline_Obj`; two new resource tables
  (`shaders`, `pipelines`); command-list draw state (primitive, has_pipeline, index buffer/type);
  the reserved buffer-index constants; `pipeline_get` + `topology_to_primitive` decls.
- `device.m`: init / leak-report / free the two new tables.

### Buffer-index convention (the load-bearing decision)
Slang's MSL puts the push-constant block at `[[buffer(0)]]` and reads vertex attributes via
`[[stage_in]]` (fed by a vertex buffer whose index we choose in the vertex descriptor). To keep the
two from colliding, push-constants/uniforms ride buffer index 0 and vertex-attribute buffers ride
index 30 (Metal's max is 31). This is why `cmd_push_constants` and `cmd_bind_vertex_buffer` target
those exact indices and the vertex descriptor's `bufferIndex`/`layouts[]` match index 30.

### hello-gpu app wiring (outside `modules/gpu`)
`apps/hello-gpu/src/triangle.c` now selects the MSL bundle when
`caps.shader.bytecode_passthrough.msl` is set, else SPIR-V — an explicit branch, no silent default
(MEL-CODE-007). This is the one screen whose shader set is fully MSL-bundled; see Kludges for why
gradient/quad screens in hello-gpu cannot follow yet.

### Test (`test/test_metal.c` + additive `gpu-metal` target in `build.c`)
Render-to-texture + readback, mirroring `test_visual.c`'s pattern (offscreen RGBA8 attachment, blit
copy to a `READBACK` buffer, `mel_gpu_buffer_mapped`). Four tests: MSL-cap assertion, and
triangle/gradient/quad each rendered from their MSL bundle and asserted pixel-analytically, with a
PPM dump per screen. Uses `mel_gpu_future_wait` (not `_status`) because Metal's submit resolves via
the async completion handler — `_status` does not block, `_wait` busy-waits to resolution.

## Verification

- `./nob build hello-gpu macos --gpu=metal` → builds + packages.
- `DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1 ./nob test gpu-metal macos --gpu=metal`
  → **4 passed, 0 failed**.
- `gpu-foundation` under metal: 13 passed. `gpu-resources` under metal: 4 passed. `gpu-visual`
  under metal: skips (vulkan-gated). `gpu-foundation` under vulkan: 13 passed (build.c change
  doesn't disturb the vulkan path).

### Produced pixels (32×32 readback, dumped to `modules/gpu/build/macos-debug/metal_*.ppm`)
- triangle: corner (0,0)=clear black; apex region (16,10)=(201,34,21) strong red (red vertex up-top);
  centroid (16,16)=(121,74,60) the RGB-vertex blend; alpha 255.
- gradient: vertical lerp top-to-bottom — row 0 (16,0)=(52,29,62) ≈ shader "bottom" colour, row 31
  (16,31)=(20,21,47) ≈ shader "top" colour (framebuffer Y-down vs NDC Y-up; identical convention to
  Vulkan readback). Monotone in R and B.
- quad: centre (16,16)=(51,204,102) = exactly the push-constant colour (0.2,0.8,0.4)·255 rounded;
  corner clear.

## Golden-diff: why no `MEL_GOLDEN` cross-backend diff (deliverable #5 caveat)

The committed goldens (`modules/gpu/test/golden/*.ppm`) are the Vulkan **bindless / fullscreen**
synthetic-fragment tests (`ubo_bindless`, `sampled_checker`, `alpha_blend`, `mrt_*`,
`wireframe_*`, …) driven by `visual_spv.h`. Those shaders are **SPIR-V only** — they have no MSL
bundle — so the Metal backend cannot run them (`shader_create` loud-rejects non-MSL). There is **no**
committed golden for the triangle / gradient / quad screens (the only MSL-bundled set). Therefore the
literal "diff the produced image against the committed macOS-Vulkan goldens via `MEL_GOLDEN`" cannot
apply: the goldens that exist belong to a disjoint shader set Metal can't execute, and the screens
Metal *can* render have no golden. Per deliverable #5's escape hatch ("if a golden legitimately can't
match, document precisely why"), the proof here is **analytic pixel assertions** against each
shader's deterministic math (e.g. the quad's exact unorm push-constant colour, the gradient's exact
vertical lerp), plus per-screen PPM dumps for inspection. No golden was modified;
`MEL_GPU_GOLDEN_UPDATE` stays off. The chosen analytic tolerance is ±2 unorm LSB on the
deterministic-colour channels (quad/gradient) and structural for the triangle (clear corners,
nonzero blended interior, colored-pixel fraction bounded). Cross-backend deltas vs Vulkan are not
measurable absent a shared golden; closing that gap needs MSL variants of the `visual_spv.h` shaders
(a separate lane — those shaders are not in my file-ownership scope).

## Allocation / ownership notes
- `MTLLibrary` / `MTLFunction` / `MTLRenderPipelineState` / `MTLComputePipelineState` are retained
  once at create (`__bridge_retained void*`) and released once at destroy (`__bridge_transfer`).
  Entry-name strings use the device allocator. No per-draw allocations: bind/draw/push are pure
  encoder calls; push-constants ride `setVertexBytes:`/`setFragmentBytes:` (no buffer alloc).
- Device-destroy now leak-reports + frees the `shaders` and `pipelines` tables alongside the others.

## Kludges / debt (confessed — bar zero, MEL-ENGINE-VIII)
1. **Compute on the render-encoder command list is a loud `MissingFeature`.** The command list holds
   a `MTLRenderCommandEncoder`; a `MTLComputeCommandEncoder` is a different encoder with its own
   lifecycle. No MSL compute bundle exists (clear/blit are SPIR-V-only), so nothing exercises it.
   Wiring a compute-encoder mode into the command-list model is the next step for compute parity.
2. **Vertex-buffer slot is collapsed to one reserved index (30).** `cmd_bind_vertex_buffer(slot, …)`
   ignores `slot` and binds at index 30, matching the single interleaved binding the pipeline's
   vertex descriptor declares. Multiple distinct vertex bindings (per-instance streams, multi-buffer
   layouts) are not modelled yet; the demos use one interleaved buffer. Honest for the current
   `Mel_Gpu_Vertex_Element` surface (it carries no `binding` field), but it will need a real
   slot→bufferIndex map when multi-stream layouts land.
3. **hello-gpu gradient/quad screens still can't render on Metal.** gallery.c / layers.c /
   postprocess.c pair the **gradient/quad fragment** with the **blit vertex** (`BLIT_VERT_SPV`), and
   the blit bundle is SPIR-V-only (no MSL — explicitly out of my scope). They also use other
   SPIR-V-only shaders (`POST_FRAG`, …). So only the standalone `triangle` screen is MSL-complete and
   wired. The gradient/quad **bundles** are proven end-to-end by `test_metal.c` (their own vs+fs), but
   the hello-gpu screens that would surface them are blocked on the blit MSL bundle, which belongs to
   the Slang lane, not this one.
4. **No depth/stencil state object.** `pipeline_create` records depth/stencil **formats** for the
   pipeline descriptor, but a `MTLDepthStencilState` (depth test/write/compare, stencil ops) is not
   created or bound — depth-tested draws would not honour the compare op yet. None of the three
   MSL-bundled screens use depth; the `Mel_Gpu_Depth_Stencil` opt would need a
   `setDepthStencilState:` path. Flagged so it is not mistaken for complete depth support.
5. **MSL compiled from source at load (`newLibraryWithSource:`), not from precompiled `.metallib`.**
   Correct and matches the bundle shape (the bundles carry MSL text), but it pays a runtime MSL→AIR
   compile on every `shader_create`. A `.metallib` precompile (bundle-side) would remove that; the
   loader already takes a neutral blob, so `newLibraryWithData:` is a drop-in when bundles ship AIR.
6. **`test_metal.c` proves via analytic pixels, not the golden harness** (see Golden-diff above). The
   harness (`img_golden.h`/`.c`) is unmodified and available; it is simply not wired here because no
   matching golden exists. Not a shortcut in the rendering — a faithful consequence of the golden set.

## Shared-header touches
**None.** `shader.h`, `caps.h`, `pipeline.h`, `command.h`, `rendering.h` untouched. The only edits
outside `modules/gpu/src/metal/macos/**` + `modules/gpu/build.c` + the test are: `device.m` and
`caps.m` (both inside `metal/macos`, both required), and `apps/hello-gpu/src/triangle.c` (one app
file, additive cap-driven branch — outside the strict `modules/gpu` scope but load-bearing for the
"hello-gpu renders under metal" deliverable; flagged for the orchestrator).

## CLAUDE.md suggestions (recommendations only — not applied)
None this round.

## Suggestions
- A shared cross-backend golden requires MSL variants of the `visual_spv.h` shaders (or running the
  triangle/gradient/quad through both backends to mint a common golden). Worth a small lane so the
  `MEL_GOLDEN("metal", …)` path the harness already supports actually has references to diff.
- The `Mel_Gpu_Vertex_Element` surface has no per-binding index; adding one would let the Metal
  vertex-buffer slot map be exact rather than collapsed (kludge #2).
- A `MTLDepthStencilState` path in `pipeline_create`/bind would unblock depth-tested Metal screens
  (kludge #4) — the depth attachment format is already wired.
