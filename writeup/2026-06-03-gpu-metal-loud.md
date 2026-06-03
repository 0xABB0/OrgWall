# GPU Metal — close the two silent capability-drops (matrix task #18)

Verify-metal returned SHIP-WITH-FIXES on two silent drops. Under the zero-silent-problems mandate
(MEL-ENGINE-VIII) both are now either honored or loud. The verified-clean lifetime/leak/alloc/assertion
work was not touched.

## Work done

### Finding 1 — multi-vertex-buffer slot collapse → LOUD-REJECTED

`mel_gpu_cmd_bind_vertex_buffer` discarded `slot` (`(void)slot;`) and bound every buffer to the single
Metal `bufferIndex 30` (`MEL_GPU_METAL_VERTEX_BUFFER_INDEX`); `pipeline.m` pins all vertex elements to that
same index. Two streams (slot0=A; slot1=B) silently overwrote — A lost, no diagnostic.

A correct slot→bufferIndex bijection is NOT cheaply implementable in this lane: the per-element vertex
layout (`Mel_Gpu_Vertex_Element { location, format, offset }`, in `gpu/pipeline.h`) carries **no per-element
buffer-slot selector**, and there is a single `vertex_stride`. The pipeline's `MTLVertexDescriptor` therefore
describes exactly one stream. Binding a second buffer to a distinct `bufferIndex` would have no matching
`layouts[idx]`/attributes, so a real bijection requires a core header change (out of lane — shader.h/pipeline.h
core are owned elsewhere). Honest floor: keep slot 0 as the single stream and make slot != 0 a loud failure.

`modules/gpu/src/metal/macos/record.m` `mel_gpu_cmd_bind_vertex_buffer`:

    if (slot != 0)
    {
        mel_log_error("gpu", "cmd_bind_vertex_buffer: slot %u rejected; the Metal backend binds a single
            vertex stream (slot 0) to bufferIndex %u and the vertex layout carries no per-element stream
            selector, so slot>0 would silently collapse onto the slot-0 stream and lose data",
            slot, MEL_GPU_METAL_VERTEX_BUFFER_INDEX);
        mel_assert(slot == 0);
        return;
    }

`mel_log_error` (always) + `mel_assert` (debug-fatal). In release the log fires and the function returns
without the silent collapse (no `setVertexBuffer` at the wrong index). No data is lost silently anymore.

### Finding 2 — depth-state / cull / front-face / fill-mode drop → IMPLEMENTED

`pipeline_create` read only `opt.depth_format` and ignored `opt.depth_stencil`, `opt.cull`, `opt.front_face`,
`opt.fill`. No `MTLDepthStencilState` was ever built (depth LESS+write rendered always-pass/no-write); cull
and winding were never set; `setTriangleFillMode:` was never wired despite caps advertising
`raster.fill_mode_non_solid = true`. All implemented so Metal honors the same pipeline contract as Vulkan
(MEL-ENGINE-IX).

`modules/gpu/src/metal/macos/mtl_backend.h` — extended `Mel_Gpu_Pipeline_Obj` with
`depth_stencil_state`, `cull_mode`, `front_face`, `fill_mode`, and `stencil_test` + `stencil_ref_front/back`
(Metal's stencil reference is dynamic encoder state, not part of `MTLDepthStencilDescriptor`).

`modules/gpu/src/metal/macos/pipeline.m`:
- New mappers: `mel_gpu__mtl_compare`, `mel_gpu__mtl_stencil_op`, `mel_gpu__mtl_stencil_face`,
  `mel_gpu__mtl_cull`, `mel_gpu__mtl_fill`, and `mel_gpu__mtl_depth_stencil_state`. Compare/stencil mappings
  mirror the Vulkan backend bit-for-bit (incl. the `depth_test && compare==NONE → LESS` warn).
- `mel_gpu__mtl_depth_stencil_state`: with no `depth_stencil` but a depth format → default LESS+write (matches
  Vulkan's implicit default); with `depth_stencil` → honors test/write/compare and (when stencil_test) the
  front/back faces. Warns (no silent drop) for `depth_bounds_test` (no Metal equivalent) and for
  `depth_stencil` supplied with no depth_format.
- `mel_gpu__mtl_fill`: SOLID→Fill, WIREFRAME→Lines, POINT→Lines with a MissingFeature-style warn (Metal has no
  point fill; honest degrade per MEL-ENGINE-VII). caps `fill_mode_non_solid` is now genuinely wired, so it stays
  advertised honestly.
- `pipeline_create_opt`: builds the depth-stencil state, loud-fails if `newDepthStencilState` returns nil while
  depth was requested, and stores cull/winding/fill/stencil-ref on the obj. `pipeline_destroy` releases the new
  retained `depth_stencil_state`.

`modules/gpu/src/metal/macos/record.m` `mel_gpu_cmd_bind_pipeline`: applies `setCullMode:`,
`setFrontFacingWinding:`, `setTriangleFillMode:`, `setDepthStencilState:` (when present), and
`setStencilFrontReferenceValue:backReferenceValue:` (when stencil_test). Metal sets depth/cull/fill on the
encoder, not the render-pipeline state, so bind time is the correct site.

caps.m: no change — `fill_mode_non_solid` is now backed (wireframe wired; point degrades loudly), so it remains
honestly true.

### Tests (in lane: `modules/gpu/test/test_metal.c`)

Two new tests, self-contained via inline MSL source (no codegen dependency):
- `metal_state.depth_occlusion`: D32 depth target, depth LESS+write; draws a near-red triangle (z=0.3) then a
  far-green triangle (z=0.7). Asserts center stays RED (green occluded). If depth were ignored (always-pass) the
  later green would overwrite — so a passing assert proves depth state takes effect.
- `metal_state.back_face_cull`: same geometry rendered twice via a helper — `cull=BACK, front_face=CCW` (center
  green ≈ 0, culled) vs `cull=NONE` (center green > 200, visible). The relative difference proves cull/winding
  take effect. (Winding literal was empirically flipped once: under Metal window-space the back-face order is
  `top, bottom-right, bottom-left`.)

## Green-run counts
- `gpu-metal` macos --gpu=metal: **6 passed, 0 failed, 0 skipped, of 6** (4 prior + 2 new), MEL_TEST_NOFORK=1.
- `gpu-foundation` macos --gpu=metal: **13 passed, 0 failed, 0 skipped, of 13**.
- Release build of `gpu-metal` (`--release`): links clean, no warnings.
- iOS: not re-run (no simulator launched this session). The iOS target compiles the same `src/metal/macos/*.m`
  (per `modules/gpu/build.c`), so the changes apply there at build; runtime unverified on iOS.

## Kludges / debt
- **Vertex-buffer slot is loud-rejected, not implemented.** Multi-stream vertex input needs a per-element
  buffer-slot field in the core `Mel_Gpu_Vertex_Element` (and matching `MTLVertexDescriptor.layouts[]` wiring).
  That is a core header change outside this lane. Until then slot>0 is honestly refused. This same single-stream
  limitation exists irrespective of backend; worth a cross-backend decision.
- **Point fill degrades to wireframe** (Metal has no point `MTLTriangleFillMode`); warned, not silent. Honest per
  MEL-ENGINE-VII but not a true point-fill.
- **`depth_bounds_test` ignored** (no Metal equivalent); warned. Vulkan honors it where supported; Metal cannot.
- New tests rely on `newLibraryWithSource` compiling inline MSL at runtime (slight per-test cost). Acceptable for
  a test; not a shipping path.

## CLAUDE.md suggestions (recommendations only)
- The build doc says "Invoke from the repo root only," but agent worktrees ship no compiled `nob`; I had to
  bootstrap it (`clang -std=c23 -g -Imodules/build -o nob nob.c`) inside the worktree before building. A one-line
  note in CLAUDE.md ("in a fresh worktree, bootstrap nob first") would save the next agent the detour.

## Suggestions
- Add a per-element vertex buffer-slot to `Mel_Gpu_Vertex_Element` and a small `vertex_buffer_count`/per-slot
  stride array so multi-stream binding becomes real on every backend; then lift the Metal loud-reject to a true
  slot→bufferIndex bijection and test two interleaved streams.
- Consider a shared cross-backend depth/cull readback test (Vulkan + Metal + D3D12) driven off one geometry +
  one expectation, to keep the pipeline contract honest as backends drift.
