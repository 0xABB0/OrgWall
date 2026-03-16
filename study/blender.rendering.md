# Blender Rendering Architecture Study

Source: `~/repo/suck/blender/source/blender/draw/` and `source/blender/gpu/`

## The Layer Stack

1. **GPU layer** (`source/blender/gpu/`) -- raw GPU resource abstraction (textures, framebuffers, shaders, batches). Backend-agnostic C++ classes.
2. **GPUViewport** -- thin container owning output textures (`color_render`, `color_overlay`, `depth`) and framebuffers, plus color management. The "screen rectangle."
3. **Draw Manager / DRW** (`source/blender/draw/`) -- orchestrator. Scene traversal, engine lifecycle, resource handles, visibility culling, pass submission.
4. **Engine layer** -- concrete renderers (EEVEE, Workbench, Overlay, GpencilEngine) implementing `DrawEngine` vtable.

## Views and Viewports

### GPUViewport (`gpu/intern/gpu_viewport.cc`, `GPU_viewport.hh`)

Lowest-level "output surface." Owns:
- Two `color_render_tx` + two `color_overlay_tx` (stereo)
- One shared depth texture
- Two framebuffers: `render_fb` (linear/HDR) and `overlay_fb` (display-space)
- `DRWData*` -- per-viewport persistent draw state

NOT aware of scenes, cameras, or objects. Purely GPU resource container.

### DRWContext (`draw/intern/DRW_render.hh`)

The "what are we drawing right now" context. Thread-local singleton via `g_context`. Holds:
- `Depsgraph*` (evaluated scene state)
- `GPUViewport*` (output target, can be null)
- `View3D*`, `ARegion*`, `RegionView3D*` (UI context)
- `Mode` enum: VIEWPORT, VIEWPORT_XR, VIEWPORT_OFFSCREEN, VIEWPORT_RENDER, SELECT_*, DEPTH, RENDER, CUSTOM

Constructor extracts everything from depsgraph immediately -- rest of pipeline never reaches back.

### draw::View (`draw/intern/draw_view.hh`)

Mathematical view -- camera frustum for culling and shading. Holds:
- `UniformArrayBuffer<ViewMatrices, DRW_VIEW_MAX>` -- multiple view matrices (stereo/panoramic)
- `UniformArrayBuffer<ViewCullingData, DRW_VIEW_MAX>` -- frustum planes + bounding sphere per view
- `VisibilityBuf` -- GPU-side frustum culling result (1 bit per resource per view)

## Multiple Viewports

Each `ARegion` owns its own `GPUViewport`. Each `GPUViewport` stores `DRWData*`, which holds `DRWViewData*[2]` (stereo). Each `DRWViewData` contains **one instance of every engine**.

```
ARegion (UI region)
  -> GPUViewport (output textures + framebuffers)
     -> DRWData (persistent per-viewport draw state)
        -> DRWViewData[2] (one per stereo eye)
           -> eevee::Engine
           -> workbench::Engine
           -> overlay::Engine
           -> gpencil::Engine
           -> draw::Manager (per-viewport resource handle table)
```

Multiple viewports are isolated by separate `GPUViewport` + `DRWData`. No shared mutable state.

## The EEVEE Pipeline

Central `Instance` object owns every subsystem as a direct value member (not pointer).

### Key Instance members
- `Camera` -- wraps scene camera, computes matrices
- `Film` -- accumulation buffer, render pass storage, jitter
- `Sampling` -- TAA sample counter, jitter sequence
- `PipelineModule` -- all rendering pipelines
- `MainView` -- contains 6 `ShadingView` (panoramic cube-face)
- `ShadowModule`, `LightModule`, `HiZBuffer`, `RayTraceModule`, `GBuffer`, `RenderBuffers`

### PipelineModule contents
- `BackgroundPipeline` -- world background
- `ForwardPipeline` -- alpha-blended, NPR (prepass -> opaque -> transparent sortable)
- `DeferredPipeline` -- three layers: opaque, refraction, volumetric. Each: Prepass -> gbuffer -> eval_light -> combine
- `ShadowPipeline`, `VolumePipeline`, `CapturePipeline`

Every pipeline contains `PassMain`, `PassSimple`, and `PassSortable` instances.

### DrawEngine vtable

```cpp
struct DrawEngine {
  virtual void init() = 0;
  virtual void begin_sync() = 0;
  virtual void object_sync(ObjectRef &, Manager &) = 0;
  virtual void end_sync() = 0;
  virtual void draw(Manager &) = 0;
};
```

### Frame lifecycle

```
DRWContext::engines_init_and_sync()
  foreach_enabled_engine: instance.init()
  Manager::begin_sync()
  foreach_enabled_engine: instance.begin_sync()
  DRWContext::sync(iter_callback)
    foreach_obref_in_scene -> foreach_enabled_engine: instance.object_sync(ob_ref, manager)
  foreach_enabled_engine: instance.end_sync()
  Manager::end_sync()

DRWContext::engines_draw_scene()
  foreach_enabled_engine: instance.draw(manager)
```

## The Draw Manager (`draw::Manager`)

Per-viewport database of GPU-side per-object data. `StorageArrayBuffer`s indexed by `ResourceHandle`:

- `matrix_buf` -- `ObjectMatrices` (model + model_inverse)
- `bounds_buf` -- `ObjectBounds` (for culling)
- `infos_buf` -- `ObjectInfos` (color, index, flags, attribute offsets)
- `attributes_buf` -- `ObjectAttribute` array

Double-buffered (SwapChain<..., 2>) for temporal access.

Handle allocation: `Manager::resource_handle(ObjectRef &ref)` allocates slot, copies matrix/bounds/info.

### Visibility

`Manager::compute_visibility(View &view)` dispatches GPU compute shader: reads `bounds_buf` + `infos_buf`, tests against view frustum, writes bitfield into `view.visibility_buf_`.

### Command generation

```cpp
manager.compute_visibility(view);
manager.generate_commands(pass, view);
manager.submit(pass, view);
```

`PassMain` uses `DrawMultiBuf` (GPU indirect draw -- full GPU-side batching).
`PassSimple` uses `DrawCommandBuf` (CPU-side commands, deterministic order).
`PassSortable` sorts sub-passes by float key before submission.

## Scene Data -> Rendering: The Depsgraph Bridge

`Depsgraph` = fully-evaluated copy-on-write scene. Draw system only reads evaluated copies.

`ObjectRef` wraps evaluated `Object*` + dupli context. When engines call `manager.resource_handle(ob_ref)`, manager reads `object->object_to_world()` directly.

EEVEE's `object_sync()`: gets `ObjectHandle` (keyed by `ObjectKey`), routes to `sync_mesh()` / `sync_curves()` etc., allocates resource handle, creates GPU batches, registers draw calls.

## Key Design Patterns

- **Engines are per-viewport instances, not singletons**
- **Passes are view-agnostic** -- populated during sync, submitted against a view during draw
- **The Manager is the GPU-side object table** -- handle-indexed storage buffers
- **Visibility is fully GPU-driven** -- compute dispatch produces per-resource-per-view bitfield
- **DRWContext mode enum unifies all render paths** (viewport, offscreen, XR, F12, selection)

## Key files
- `draw/intern/DRW_render.hh` -- DrawEngine vtable, DRWContext
- `draw/intern/draw_manager.hh` -- draw::Manager
- `draw/intern/draw_view.hh` -- draw::View
- `draw/intern/draw_pass.hh` -- PassMain, PassSimple, PassSortable
- `draw/engines/eevee/eevee_instance.hh` -- Instance
- `draw/engines/eevee/eevee_pipeline.hh` -- all pipelines
- `draw/engines/eevee/eevee_view.hh` -- ShadingView, MainView
- `draw/engines/eevee/eevee_sync.hh` -- SyncModule
