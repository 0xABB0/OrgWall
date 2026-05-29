# `render.graph` — declarative GPU rendering graph and execution-plan compiler

The render graph consumes GPU primitives (Layer 0 — the RHI) and emits a compiled execution plan. The user declares **what** each pass reads and writes; the graph decides barrier topology, transient memory aliasing, sub-pass merging, queue assignment, and cross-queue ownership handoff. The module sits **above** the GPU, not inside it — the GPU module ships first and is fully usable without the graph; the graph is a purely additive Layer 1 that generates inputs to Layer 0. The module name is `render.graph`, not `frame.graph`, because its scope is rendering passes — graphics, compute, copy, ray-tracing, acceleration-structure build, video decode/encode/process, and work-graph dispatch. It is not a general scheduling abstraction; the engine reactor and `frame.pacing` are the general schedulers, and the render graph composes downstream of them.

This module was U20 / Layer 1 of `docs/gpu-rhi.md` (§8 in its entirety, plus §10.7 for the XR extension). It now lives here. The GPU spec keeps the cross-references in §1, §9.7, §10.7, and the M5 milestone description; nothing about the Layer 0 surface depends on this document.

## Inherited principles

- **P1 — Emulate-to-equivalent.** Split-barrier synthesis is the modern lowering (`VkEvents`, fenced barrier batches on D3D12 enhanced barriers, encoder boundaries on Metal, `VK_EXT_shader_split_barrier` where ratified). On backends that cannot split a barrier — WebGPU implicit barriers, older D3D12 legacy resource barriers — the synthesizer collapses to a single full barrier and the pass-reordering step still runs. The graph **API shape does not change** across this lowering; only the emitted command stream does.
- **P2 — Full-control escape (§8.5).** Skip the graph entirely. Use Layer 0 directly: manual sub-pass topology (`cmd_begin_rendering`), manual barriers (`cmd_barrier`), manual pools, manual queue submission, manual aliasing through `cmd_aliasing_barrier`. The graph is one path; Layer-0-manual is its peer, not a degraded subset.
- **MEL-ENGINE-II — hide complexity, not power.** The user writes draws/dispatches inside `recordFn`; the graph emits split barriers, aliasing transitions, queue ownership transfers, sub-pass merges, and timeline semaphores beneath. Every one of those mechanisms remains expressible through Layer 0 if the user wants it directly.
- **MEL-ENGINE-IV — constrain conventions, never capabilities.** The graph imposes a declarative shape on the access record; it does not refuse work the GPU can express. Video passes, work-graph dispatch, acceleration-structure builds, and ray-tracing passes are first-class pass kinds, not bolt-ons.
- **MEL-ENGINE-VI — respect every device.** Tiler-aware compilation consumes `Mel_Gpu_Tiler_Profile` (§9.7) and rewrites the plan to keep attachments on tile, prefer `DontCare`, prefer on-tile MSAA resolve, avoid store/load round trips. Mobile bandwidth is a first-class cost axis.
- **MEL-ENGINE-VIII — fail with honor.** A conditional first-writer without a false-edge declaration is a **compile error**, not a silent miscompile. Pipeline creation inside `recordFn` is debug-asserted. Missing capture-replay flags on resources used by a captured session log loudly at submission.
- **MEL-ENGINE-IX — parts compose.** The `WorkGraph` pass kind is the natural composition of GPU-driven scenes with the rendergraph's CPU-side topology. The XR access records compose orthogonally onto the standard access record set, without inventing a parallel "XR graph" surface.

## Public objects

- **`Mel_Render_Graph`** — the graph handle. One graph instance owns its symbolic resource table, its pass list, its compiled plan, and its hash; multiple graphs may coexist (e.g. one per swapchain, one per offscreen camera, one per XR view bundle). The handle is a thin wrapper over `Mel_SlotMap_Handle` like every other Layer 0 resource handle (GPU §3.1), so use-after-free is a loud `alive()` failure, never a dangling pointer.
- **`Mel_Render_Graph_Resource`** — symbolic, graph-local resource handle. Declared with a full descriptor (extent / format / usage); resolved to a physical GPU resource (buffer / texture / acceleration structure / swapchain image) after compilation. Identity is graph-scoped — the same symbolic handle in two different graphs refers to two unrelated resources. The split between symbolic graph handles and the GPU's `Mel_Gpu_Buffer` / `Mel_Gpu_Texture` / `Mel_Gpu_Accel_Struct` is deliberate: it makes the resolution boundary visible at the type level so the compiler-vs-record-time distinction never blurs.
- **`GraphPassDesc`**, **`GraphAccess`**, **`GraphResourceRange`** — the per-pass descriptor records (Idris 2 syntax pinned below in §Builder API).
- **Pass kind enum** — `Graphics | Compute | Copy | RayTracing | AccelerationBuild | VideoDecode | VideoEncode | VideoProcess | WorkGraph`.
- **Access kind enum** — `Read | Write | ReadWrite | DiscardWrite | Resolve | Present | ExternalAcquire | ExternalRelease`. `DiscardWrite` is the contractual form of "the prior contents are not needed"; the compiler uses it to drop unnecessary load operations and to widen aliasing eligibility. `ExternalAcquire` / `ExternalRelease` mark the seam where the graph hands a resource to or receives one from code outside the graph (a hand-recorded Layer 0 command list, an XR runtime, a video encoder).
- **Resource category** — `Transient | Imported | Exported`.
- **`Mel_Render_Graph_Compiled_Plan`** — the inspectable post-compile artifact returned by the introspection surface. Not directly executable by the user; the engine walks it. Consumed by profilers and visualizers.
- **`GraphAccessKind`**, **`GraphPassKind`**, **`ResourceState`**, **`PipelineStage`**, **`QueueRole`**, **`AttachmentAccess`** — the enums and structs the descriptor records compose over. `ResourceState`, `PipelineStage`, and `QueueRole` are shared with Layer 0 verbatim (the GPU U17 and U7 vocabularies); the graph does not redefine state for its own purposes.

## Pass types and resource categories (§8.1)

The pass kind tells the compiler which Layer 0 queue family, which state-transition discipline, and which command-list scope to assemble around the recorded work. The extra pass kinds are not render-graph magic — they let the compiler pick the correct queue role, U17 resource states, query scopes, and aliasing rules without hiding the underlying Layer 0 primitives.

- **`Graphics`** — the sub-pass topology of `cmd_begin_rendering` is fully expressible; attachments carry their own load/store/clear intent inside the access record. Multiview and feedback loops ride the same record shape, gated by Layer 0 caps.
- **`Compute`** — dispatches against general-purpose compute queues; eligible for async-compute scheduling under §Compilation phase 7. Indirect dispatch buffers participate as access records, not as opaque arguments.
- **`Copy`** — host-to-device, device-to-device, and device-to-host transfers; the natural carrier for DMA queue dispatch. Includes blits, resolves outside a graphics pass, buffer-image transfers, and the indirect-copy lowering where granted.
- **`RayTracing`** — bound to a ray-tracing pipeline; access record names the TLAS and any per-instance buffers as standard graph resources. Shader-binding-table buffers are imports, not transients (their content lifetime predates the graph).
- **`AccelerationBuild`** — BLAS / TLAS build, update, or compaction; the compiler schedules these on the build-eligible queue and sequences dependent ray-tracing passes after them. Scratch buffers are first-class transients the compiler aliases aggressively.
- **`VideoDecode`**, **`VideoEncode`**, **`VideoProcess`** — bound to the corresponding video queues; bitstream and reference-frame buffers participate as standard graph resources. See `docs/media-video.md` for the codec surface; the graph schedules and emits the inter-queue ownership transfers between video and graphics work.
- **`WorkGraph`** — one opaque dispatch of a `Mel_Gpu_Work_Graph` handle. See §Work-graph composition below for the load-bearing details.

The pass kind is also the discriminant the compiler uses to validate access records — a `VideoDecode` pass declaring a graphics attachment access is rejected at descriptor-add time, not at compile time, with the offending field cited. This is the load-bearing application of MEL-ENGINE-VIII for the access record vocabulary: the failure mode is loud and local.

Each pass declares **resource accesses**, not whole-resource read/write sets. Every access record names the resource, the subresource range (buffer offset/size; texture mip/layer/plane/aspect; AS region where applicable), the access kind, the required Layer 0 state, the participating shader/command stages, the queue role, the attachment load/store intent where relevant, the aliasing-class eligibility, and (for `Imported` / `Exported` resources) the optional initial / final state. Whole-resource read / write convenience constructors are sugar over this exact access record.

Resource categories:

- **`Transient`** — graph-local. The compiler aliases physical memory between non-overlapping lifetimes via the GPU's placed-allocation primitive. Lifetime is the graph instance. The user may opt a transient out of aliasing by tagging it with a unique alias class, which is the load-bearing path for resources that are conceptually transient but feed late-latched debug capture.
- **`Imported`** — handles from outside the graph: persistent textures and buffers, swapchain images, externally-acquired interop images, XR runtime images. The access record carries an initial state (the state in which the import arrived) and a final state (the state in which the graph must leave it for the next consumer). Imports are never aliased — borrowing the GPU's residency contract directly.
- **`Exported`** — named outputs available to subsequent code or another graph. Exported handles are stable across graph rebuilds when the descriptor and name are stable, so downstream consumers can bind to "the depth buffer of the previous frame" without reconstructing the graph. Exports are how cross-graph dependencies are expressed; the next graph imports the previous graph's export and the chain of barriers carries through.

## Compilation phases (§8.2)

All seven phases run automatically from the declared access records; the user writes none of this machinery. The phase order is load-bearing — culling before lifetime analysis avoids aliasing dead resources; reordering before barrier synthesis lets split barriers actually overlap; sub-pass merging after barrier synthesis lets the merger collapse barriers that crossed pass boundaries which no longer exist.

1. **Dependency DAG construction** — every (resource, version) pair becomes a node; every access record becomes an edge whose direction is determined by the access kind. Producer-before-consumer is the dominant edge; `ReadWrite` introduces a self-loop the compiler resolves into a barrier between mips/layers where the range admits it. Cycles are illegal and cited at the offending pass pair; the user must split the pass or introduce a versioned intermediate.
2. **Pass culling** — passes whose outputs are transitively unused (no `Exported` resource downstream, no `Present` access, no external-release edge, no side-effect-marked accesses such as performance-query writes) are removed. Per MEL-ENGINE-III: a pass nobody depends on is a stolen cycle. Culling is non-recoverable from inside `recordFn` — if a debug pipeline wants the pass to run unconditionally, it declares an `Exported` resource (a debug overlay buffer, for instance) or marks the pass as `keep_unconditionally` in the descriptor so culling cannot reach it. The cull report (which passes were removed and why) is part of the debug introspection surface.
3. **Resource lifetime analysis** — for every `Transient`, compute first-write and last-read passes against the final pass order. Lifetimes are intervals; intervals do not yet have physical backing. Sub-resource ranges are tracked separately when access records carry distinct ranges, so a texture written on mip 0 and read on mip 1 is two disjoint sub-lifetimes from the aliasing standpoint.
4. **Transient memory aliasing** — non-overlapping `Transient` lifetimes pack into shared physical allocations through the GPU module's placed-allocation primitive. The packing problem is bin-packing under format / extent / alignment / heap-type constraints; the compiler runs a greedy first-fit pass over the lifetime intervals, ordered by descending allocation size, and exposes the resulting allocation map to debug introspection. Each transition between two aliased resources on the same backing emits `cmd_aliasing_barrier(prev, next)` automatically. The explicit Layer-0 `cmd_aliasing_barrier` primitive remains the P2 peer; users who skip the graph still get the same primitive.
5. **Automatic barrier synthesis with split barriers** — preceded by a **pass-reordering step** (Granite-style baker). Reordering maximizes the distance between producer and consumer wherever the DAG admits it, so the split barrier's gap covers real work instead of a few instructions. Reordering respects the user's pass-name ordering as a tie-breaker so frame-to-frame plans are stable for capture replay. The synthesizer then emits **split barriers** — `signal-event-after-producer` paired with `wait-event-before-consumer` — wherever the lowering supports it: `VkEvents` on Vulkan (or `VK_EXT_shader_split_barrier` where ratified, see GPU §8.2), fenced barrier batches on D3D12 enhanced barriers, encoder boundaries on Metal. On backends that cannot split — WebGPU implicit barriers, older D3D12 legacy resource barriers — the synthesizer collapses to a single full barrier (P1 emulate). The user writes zero barriers in either case. Where a barrier batch coalesces across multiple producer/consumer pairs into a single Layer 0 `cmd_barrier`, the compiler emits one batched call rather than a sequence — this is a measurable mobile win.
6. **Sub-pass merging** — contiguous compatible `Graphics` passes collapse into one rendering pass with tile-local reads. This is the mobile bandwidth win (MEL-ENGINE-VI) and is also a measurable desktop win when the merged passes share attachment formats and store/load intents. Merging consumes the tiler profile (§Tiler-aware compilation below) — on tilers, the merger is aggressive and tries to keep entire G-buffer chains on tile; on desktop, the merger is more conservative. The merger also rewrites load/store ops post-hoc: a transient attachment whose lifetime is entirely inside a merged group becomes `DontCare` on both ends.
7. **Queue assignment + async-compute / video / work-graph scheduling** — passes whose dependencies admit it dispatch to compute, media, or native work-graph queues where caps allow. Timeline semaphores are inserted automatically. Cross-queue resource handoffs emit `cmd_queue_ownership_release(resource, dst_queue, range)` on the producer and `cmd_queue_ownership_acquire(resource, src_queue, range)` on the consumer — the Vulkan `SHARING_MODE_EXCLUSIVE` discipline is the graph's responsibility, never the user's. The scheduler is cap-aware: backends without async-compute queues (some WebGPU profiles) collapse everything to the graphics queue and the semaphore inserts become no-ops.

## Execution and caching (§8.3)

The compiled plan is an ordered list of `(queue, pass, barriers, sub-pass-merge group)`. The engine walks it, invoking each pass's `recordFn` with the resolved physical resources. The user records draws/dispatches inside the callback; no manual barriers, no sub-pass declarations, no aliasing bookkeeping. The engine submits the recorded command lists at the right point — at sub-pass-merge-group boundaries when merging applied, at pass boundaries otherwise — and inserts the timeline-semaphore signals/waits between queues without surfacing them to the callback.

**Caching.** The graph is hashed by topology + descriptors — every pass kind, every access record (resource id, range, kind, state, stages, queue role, load/store, alias class), every resource descriptor (extent / format / usage), every conditional predicate identity, and every XR access record's structural shape participate in the hash. The hash deliberately **excludes** `userData` pointers and `recordFn` function-pointer addresses; those are per-frame mutable. Identical hashes reuse the compiled plan. Per-frame rebuild is the default and is cheap (typical graphs have tens to low hundreds of passes; the compiler is built to run inside the per-frame budget). Persistent compiled-once graphs are supported for unchanging pipelines — the user holds the `Mel_Render_Graph` across frames and only changes pass `userData` and recorded contents per frame. The cache key is exposed to debug introspection so a graph that recompiles every frame due to an inadvertent descriptor delta can be diagnosed without instrumentation.

**External imports/exports** declare initial/final state so the import path resyncs cleanly with whoever produced the resource before this graph, and the export path leaves the resource in the state the next consumer needs (e.g. a `Present` exported swapchain image is left in present-ready state, an exported depth buffer for next-frame reprojection is left in shader-read state). The initial/final state declarations are part of the cache key, so a swapchain reacquire that returns an image in a different state recompiles cleanly rather than silently corrupting the next-frame plan.

**Submission cadence.** A single graph executes into a single submission batch by default — the engine collects every command list the graph records and submits them on the assigned queues at graph-end. Cross-queue dependencies inside the graph use timeline semaphores within the same batch. The user may force a mid-graph flush by tagging a pass `submit_after = true`, which is the load-bearing path for swapchain present coordination on backends where the present submission must close one batch and open another. Per-frame submission count is part of debug introspection.

## Conditional passes (load-bearing per MEL-ENGINE-VIII, IX)

A conditional pass carries a runtime predicate (evaluated at execution time, not at compile time) and an **explicit false-edge resource contract**. The false-edge contract is **mandatory**: for every resource version first written or transitioned by the conditional pass, the pass descriptor declares exactly one of these false-branch dispositions:

- **`PreservePreviousVersion`** — when the predicate is false, downstream consumers read the prior version and prior state of the resource. The graph emits barriers that route through the prior writer.
- **`DiscardAndDefineState(state)`** — when the predicate is false, the resource version is undefined except that consumers may only `load = discard`, `load = clear`, or rebind it from the declared state. The compiler emits the transition into `state` on the false edge but no contents.
- **`UseFallbackWriter(pass_id)`** — a separate pass is nominated as the false-branch producer of this resource version. The graph emits both producers' barrier edges; only one runs.
- **`SkipDependents`** — every downstream consumer that reads this version, transitively within the conditional subgraph, is also skipped on the false branch. The graph computes the skipped-pass closure at compile time.

The compiler treats true and false outcomes as **distinct resource versions** and emits barriers for both, then chooses the live barrier sequence at execution based on the predicate's result. A conditional first-writer **without** a false-edge declaration is a **compile error** with the offending pass and resource cited at the error site. This is the load-bearing case for MEL-ENGINE-VIII: a conditional pass without a false-edge contract is the single largest class of silent-corruption miscompile in hand-rolled render graphs, and the graph is required to refuse to compile it.

The predicate itself is supplied by the user as a value-typed callback that the engine evaluates at the appropriate point in execution — between the producer barrier and the consumer barrier, never inside `recordFn`. Predicates are pure functions over `userData`; they must not record GPU work. Conditional passes nest — a conditional pass may itself be a fallback writer for another conditional pass, and the compiler resolves the closure into the canonical disjoint set of execution traces at compile time. The number of distinct traces is bounded by the conditional-pass count; the compiler reports the trace count through debug introspection so a combinatorial explosion is visible rather than hidden.

## Builder API and `recordFn` restrictions (§8.4)

**Function-pointer two-phase, builder-style.** The full lifecycle:

- `mel_render_graph_create(device, hints) -> Mel_Render_Graph_Create_Result` — creates a graph instance bound to a device. The hints carry the per-frame-rebuild vs persistent-compile-once preference, an upper bound on pass count and resource count (for arena sizing), and the debug-introspection enable flag.
- `mel_render_graph_declare_resource(g, desc) -> Mel_Render_Graph_Resource` — declares a symbolic resource with its full descriptor; tags it `Transient` by default, `Imported` when paired with a GPU handle, `Exported` when paired with a name.
- `mel_render_graph_add_pass(g, desc) -> Mel_Render_Graph_Pass_Id` — appends a pass to the graph; the returned id is the handle used in `UseFallbackWriter` declarations and in cross-pass `submit_after` ordering.
- `mel_render_graph_compile(g) -> Mel_Render_Graph_Compile_Result` — runs phases 1-7 and caches the plan against the topology+descriptors hash; reuses the cached plan on hit.
- `mel_render_graph_execute(g, frame_info) -> Mel_Render_Graph_Execute_Result` — walks the compiled plan, invoking `recordFn` for each live pass. The `Frame_Info` from `frame.pacing` is forwarded through to passes that opted into pacing feedback.
- `mel_render_graph_inspect(g) -> Mel_Render_Graph_Compiled_Plan` — returns the introspection view of the compiled plan (read-only).
- `mel_render_graph_destroy(g)` — releases the graph, including all transient backing allocations.

Passes are added via `mel_render_graph_add_pass(g, desc)`. Resources are declared symbolically; `Mel_Render_Graph_Resource` is a graph-local handle, resolved to a physical resource after compilation. C-idiomatic; no closures required; the descriptor is a plain struct so it is easy to serialize, inspect, and diff. The descriptor record family, pinned in Idris 2 syntax:

```idris
record GraphResourceRange where
  bufferOffset : UInt64
  bufferSize   : UInt64
  mipBase      : UInt32
  mipCount     : UInt32
  layerBase    : UInt32
  layerCount   : UInt32
  planeMask    : UInt32

record GraphAccess where
  resource   : GraphResource
  range      : GraphResourceRange
  kind       : GraphAccessKind
  state      : ResourceState
  stages     : List PipelineStage
  queue      : QueueRole
  loadStore  : Maybe AttachmentAccess
  aliasClass : Maybe String

record GraphPassDesc where
  name       : String
  kind       : GraphPassKind
  accesses   : List GraphAccess
  recordFn   : CommandList -> ResolvedResources -> IO ()
  userData   : Ptr ()
```

**`recordFn` is forbidden from creating pipelines, acceleration structures, or other compile-time resources.** Pipelines must exist at graph-compile time; reaching for a `pipeline_*_create` call inside `recordFn` either blocks the record thread on async compile (defeating the cache reuse the graph's hash is built on) or returns a `MissingPipeline` status that recording cannot recover from. The pattern is to **warm pipelines before the graph is added**, capture their handles in `userData`, and retrieve them in `recordFn`. Creation calls issued inside a record callback are debug-asserted at the call site (MEL-ENGINE-VIII).

`recordFn` is permitted: every command-list recording call that does not allocate a Layer 0 creation — bind, draw, dispatch, copy, push-constants update, query begin/end, debug marker push/pop, indirect dispatch, mesh dispatch, ray-tracing dispatch, video decode/encode/process record calls, and (in the `WorkGraph` case) `cmd_dispatch_graph`. The compiler resolves the symbolic resources to physical handles and presents them to `recordFn` as `ResolvedResources`; the callback never sees the symbolic identity. Begin/end-render and begin/end-render-pass calls are emitted by the engine around the merged sub-pass group, not by `recordFn` — the callback is invoked **inside** an already-open render pass when the pass is graphical, **outside** any render pass for compute/copy/RT/build/video/work-graph passes.

The two-phase shape is the load-bearing reason the graph can ship the conditional-pass false-edge contract: the descriptor is the contract, and the contract is fully formed before any work is recorded. A closure-based API would let the false-edge declaration drift across recording boundaries; the function-pointer-plus-descriptor shape forecloses that drift at the type level (MEL-ENGINE-IX).

## Work-graph composition

A `WorkGraph` pass is the natural carrier for GPU-driven scenes. The pass descriptor takes a `Mel_Gpu_Work_Graph` handle and the entry-node record buffer; `recordFn` issues `cmd_dispatch_graph(graph, entry, records, count)` and **nothing else**. The render graph's resource-access records still drive Layer 0 state transitions for the work-graph's input and output resources; the engine treats the work-graph dispatch as one opaque pass for scheduling purposes — its internal node DAG is the GPU's concern, not the render graph's.

This composition is the load-bearing path for the pattern where the CPU sketches the rendergraph **topology** (passes, attachments, dependencies) and the GPU produces the actual **draw lists** inside the work-graph pass. The render graph treats the work-graph as an opaque producer/consumer of declared resources; the work-graph treats the render graph as the surrounding sequencing and barrier discipline. Neither knows the other's internals (MEL-ENGINE-IX).

On backends without native work-graphs (`work_graphs = emulated`), the GPU module's emulation lowering applies — the `WorkGraph` pass still compiles, still participates in the render graph's barrier and queue assignment, and still calls `cmd_dispatch_graph` at the Layer 0 surface; the lowering inside Layer 0 turns it into compute + indirect loops without the render graph noticing. The pass-reordering and split-barrier phases see the work-graph dispatch as a single producer-consumer node — they cannot reach inside it — so the gap between a work-graph pass and a downstream consumer is the only place split barriers can hide on that critical edge. The compiler favors scheduling the work-graph pass early enough that this gap covers meaningful unrelated work.

A typical use: a `Compute` pass that derives per-cluster visibility writes a small command-stream buffer; a `WorkGraph` pass consumes the visibility output and emits draw commands; a `Graphics` pass downstream consumes the work-graph output as indirect-draw arguments. The render graph schedules all three, transitions the visibility buffer between compute-write and work-graph-read, transitions the draw-stream buffer between work-graph-write and indirect-draw-read, and emits no barriers inside the work-graph dispatch itself.

The `WorkGraph` pass also composes with the conditional-pass machinery — a runtime predicate may gate the work-graph dispatch, with the same false-edge contract obligations on the resources it writes. This is the load-bearing path for "GPU sketches the scene only when the CPU decided the scene is dirty" patterns; the false-edge becomes `PreservePreviousVersion` (the prior frame's draw stream is reused) and the savings compound across frames.

## XR access records (§10.7)

The XR module (`docs/xr.md`) does not introduce its own graph. It **adds access records** on top of the standard ones. The graph hosts them and the compiler honors them. The XR-extending access records are:

- **`view_mask : u32`** — bitmask of XR views the pass renders into. Drives multiview-aware sub-pass merging and view-mask propagation through dependency edges.
- **`view_local_resources : List<Mel_Render_Graph_Resource>`** — resources that are per-view (per-eye TAA history, per-view foveation maps, per-view depth pyramids). The compiler refuses to alias per-view resources across views; per-eye TAA history never aliases the other eye's history.
- **`shared_view_resources : List<Mel_Render_Graph_Resource>`** — resources shared across views (a shared environment cubemap, a shared TLAS for the scene). The compiler emits one barrier set covering all views.
- **`late_latched_uniforms : List<UniformSlot>`** — uniforms updated as late as the backend admits (pose, foveation center, dynamic-foveation parameters) after the XR runtime predicts display time. Binding for these slots is **deferred to `recordFn`** rather than baked at compile time, but the slot identity is part of the compile-time pass descriptor so cache hashing remains stable.
- **`composition_layer_output : Mel_Xr_Composition_Layer`** — when this access record is present, the pass output binds to an XR composition layer (projection / quad / cylinder / passthrough / depth) rather than a swapchain image. The compiler emits the runtime image acquire/wait/release sequence around the pass.
- **`runtime_ownership : { acquire_from_runtime, release_to_runtime }`** — image-set ownership transfer. The runtime owns the image-set membership; the graph negotiates per-frame acquire on the producer side and release on the consumer side.

Graph rules the compiler enforces on XR-tagged passes:

- Per-eye TAA history never aliases the other eye's history (aliasing-class disjointness enforced on `view_local_resources`).
- Depth for reprojection is not optional when the runtime can consume it and the app renders 3D geometry — the graph errors if a 3D pass has a `composition_layer_output` but no depth export.
- Motion vectors are per-view and follow the convention declared to the reconstruction provider (consumed by `render.reconstruction`).
- Foveation maps are per-view resources and participate in pipeline cache keys when they change shader lowering.
- XR render passes can render a mirror view, but mirror output is a **dependent copy**, not the primary output — the graph schedules it after the composition-layer output.

The compiler honors the XR access records during sub-pass merging — multiview-aware merging keeps the entire per-view set on tile where the tiler profile admits it, and the merger refuses to fuse a per-view pass with a shared-view pass even when their access patterns would otherwise admit fusion. View-mask propagation through dependency edges is automatic: a downstream pass that reads a per-view output inherits the view mask of its producer unless it declares its own.

Late-latched uniform binding deferred to record time is the most subtle XR rule. The pipeline layout the user authored at graph-compile time reserves slots for the late-latched uniforms; the compiler emits no binding for those slots; the `recordFn` issues the late-latch binding using the values supplied by the XR runtime at predicted-display-time. The cache hash includes the slot identities but not the values, so a runtime that updates pose every frame does not invalidate the compiled plan.

The XR module owns the runtime-side surfaces (image-set negotiation, predicted display time, view configuration). The graph owns the **scheduling and barrier discipline** for XR-tagged passes. Neither leaks into the other (MEL-ENGINE-IX).

## Tiler-aware compilation (consumes GPU `Mel_Gpu_Tiler_Profile`)

The graph compiler consumes the GPU module's `Mel_Gpu_Tiler_Profile` (axes: tiler, tile_memory, memoryless_attachments, local_read, foveation, thermal; full definition in GPU §9.7 — stays in the GPU module, not duplicated here). The profile drives the following rewrites during phases 4-7:

- Prefer keeping color, depth, and short-lived G-buffer attachments **on tile** — the lifetime-analysis phase reclassifies eligible attachments as memoryless / transient_attachment when the profile reports `memoryless_attachments != none`.
- Prefer **`DontCare`** load/store where the access record admits it (no producer before, no consumer after).
- Prefer **on-tile MSAA resolve** over storing MSAA images — the sub-pass merger fuses the resolve into the producing render pass when `local_read` permits.
- Avoid splitting a mobile render pass if the split would force a store/load round trip — the pass-reordering phase respects this constraint; sub-pass merging undoes a split if reordering caused one.
- Keep attachment **feedback loops explicit** — feedback-loop access records consume the dynamic-state lane (`VK_EXT_attachment_feedback_loop_layout` / `_dynamic_state`) where granted; on backends without the cap, the compiler errors at compile time rather than emitting a wrong barrier.
- Treat a **fullscreen pass on mobile as a bandwidth event**, not a free operation — the scheduling-cost model that drives queue assignment and async-compute overlap accounts for the full-tile-store cost.
- Prefer **clustered/forward+ or tile-local deferred** over storing large deferred buffers when the tiler profile reports memory bandwidth as the bottleneck — this is an advisory the compiler exposes to higher-level renderers through the debug-introspection surface, not an automatic rewrite of the user's shading model.

The tiler profile also feeds the **scheduling cost model** used by phase 7. On `tile_based_deferred` tilers, the cost of inserting a queue boundary mid-render-pass is the tile-store-then-tile-load round trip; the scheduler accounts for that and is reluctant to schedule async-compute work in a way that would split a render pass it could otherwise merge. On desktop discrete-GPU profiles (`tiler = none`), the scheduler is permissive — queue boundaries are cheap, async-compute overlap is profitable, and the merger is correspondingly less aggressive.

## Alias-class semantics

The optional `aliasClass : Maybe String` field on `GraphAccess` is the contractual lever for transient memory aliasing. The default (`Nothing`) leaves the resource fully eligible for the compiler's automatic packing. An explicit alias-class name partitions the transient pool: resources sharing a name may alias each other; resources with different names may not. The use cases are narrow but load-bearing:

- A debug-capture path tags an attachment with a unique class so the compiler cannot fold its physical backing into the next pass's working memory; the captured snapshot then reflects what the application authored, not the post-aliasing form.
- Two resources known to the user (but not the compiler) to alias safely tag the same class to force the merger to share their backing even when the lifetime analysis would not have proven it on its own.
- A persistent-compile-once graph that wants stable physical offsets across runs (for capture-replay or for an external profiler) tags every transient with its semantic name as alias class.

Misuse — two resources sharing a class but with overlapping lifetimes — is a **compile error**, not a wrong-result miscompile. The compiler validates alias-class consistency before emitting the packed allocation map.

## A worked example of the compiled output

A frame with three passes — a `Compute` that fills a visibility buffer, a `Graphics` that consumes visibility and writes color + depth, and a `Graphics` that resolves and presents — compiles to roughly the following ordered plan. The user wrote three pass descriptors and three access lists; the engine emitted everything below.

- Compute queue: `cmd_dispatch` for visibility; `signal-event` on visibility buffer (split-barrier producer half).
- Graphics queue: `wait-event` on visibility buffer (split-barrier consumer half); `cmd_begin_rendering` opening a merged sub-pass group containing both graphics passes with attachments resolved on tile; the user's two graphics `recordFn` callbacks invoked back-to-back; on-tile MSAA resolve inside the sub-pass; `cmd_end_rendering`; barrier to `Present` state; swapchain image export.

If the second graphics pass were conditional with `PreservePreviousVersion` on the color attachment, the compiler would emit both the false-branch barrier set (preserving the prior frame's color export) and the true-branch barrier set (consuming the merged-group output) and select the live set at execution. The user wrote no barriers, no events, no queue-ownership transfers, and no aliasing barriers; the compiler emitted all of them.

## Debug introspection

The compiled plan is inspectable. Profilers, visualizers, and pipeline-tuning tools consume:

- The ordered pass list, including post-cull, post-reorder, post-merge form.
- Resource lifetimes, including which transients alias which physical allocations and at which transition points.
- The synthesized barrier graph — split-barrier signal/wait pairs, full-barrier collapses, aliasing barriers, queue-ownership release/acquire pairs.
- Sub-pass merge groups, including which load/store ops were rewritten by the merger.
- Queue assignment timelines, including timeline-semaphore signal/wait points.
- Conditional-pass disposition — which false-edge contract each conditional first-writer carries, and which branch executed for any captured frame.
- XR access metadata — view masks, view-local vs shared-view classification, late-latch slot list.

This is the surface in-engine profiler overlays and external tools (RenderDoc / PIX / Aftermath / Metal capture) consume; the GPU module's capture-replay metadata flag (GPU §3.1) is the load-bearing prerequisite for cross-tool replay. The introspection surface is read-only — it does not let the user rewrite the plan in place; users who want to author the plan rather than let it be compiled take the P2 escape below and emit Layer 0 calls directly.

The introspection surface also exposes the **cache hit/miss reason** for each graph submission — whether the plan was reused, what descriptor delta forced a recompile, and how long the compile took. This is the diagnostic an in-engine HUD overlays for the user during development; it is the load-bearing instrumentation behind MEL-ENGINE-III (no stolen cycles — a graph that secretly recompiles every frame is stealing).

## P2 escape (§8.5)

**Skip the graph entirely.** Use Layer 0 directly: manual sub-pass topology via `cmd_begin_rendering`, manual barriers via `cmd_barrier`, manual command pools, manual queue submission, manual aliasing via `cmd_aliasing_barrier`, manual cross-queue ownership transfers via `cmd_queue_ownership_release` / `_acquire`. The graph is one path; Layer-0-manual is the **peer**, not a degraded subset (MEL-ENGINE-IV). The user can also mix: use the graph for the bulk of the frame and bracket a hand-recorded command list around it, importing the bracketed work's outputs as `Imported` resources.

The presence of this escape is the reason the graph can be opinionated about the false-edge contract, the `recordFn` restrictions, the work-graph pass shape, and the XR access records — anyone whose use case the graph genuinely cannot serve uses Layer 0 directly and pays no toll. The conveniences in the graph are convenient because the escape is real.

The graph itself is implemented entirely on top of the Layer 0 surface — the compiler does not reach for any GPU primitive that Layer 0 does not expose. This is the architectural test for whether a new GPU feature belongs in Layer 0 or in the graph: if the graph's compiler needs it to emit a faithful execution plan, it goes in Layer 0; if the graph could equivalently be authored by hand against Layer 0 to produce the same effect, it stays in the graph. The line is bright and the conveniences do not bleed into the lower layer.

## Threading model

Graph construction is single-threaded against a single graph handle. The user appends passes from one thread; concurrent appends to the same graph are a contract violation. This keeps the descriptor accretion ordering-stable and avoids a lock on the hot path.

Graph **compilation** runs on whatever thread invoked `mel_render_graph_compile`. The compiler does not internally fan work out across the engine job system; the per-graph compile is fast enough that the per-frame budget tolerates it on the calling thread, and the engine schedules whole-graph compiles in parallel when multiple graphs (one per swapchain, one per offscreen camera) need recompilation.

Graph **execution** — the walk of the compiled plan — invokes `recordFn` on the thread that called `mel_render_graph_execute`. Multi-threaded recording is the GPU module's primary command-list parallelism axis (Layer 0 command pools are per-thread); the graph cooperates by allowing the engine to dispatch `recordFn` callbacks across threads when the user opts in via the create-time hint `multithreaded_record = true`. The barriers and queue-ownership transfers between callbacks are still emitted by the engine on the submission thread; the user's callbacks never coordinate with each other.

## Sibling-module dependencies

Upstream — the graph consumes:

- The GPU module (Layer 0 — `docs/gpu-rhi.md`) — every primitive the compiler emits is a Layer 0 call. The graph adds no new Layer 0 surface.
- `Mel_Gpu_Tiler_Profile` (GPU §9.7) — drives tiler-aware compilation rewrites.
- `frame.pacing` (`docs/frame-pacing.md`) — `Frame_Info.headroom_ns` is available to passes that opt into pacing feedback (quality-scaling passes that want a per-frame budget signal).
- `frame.latency` (`docs/frame-latency.md`) — latency markers carried through the pacing context are recorded inside graph passes via the GPU's `cmd_latency_mark`; the graph does not invent its own marker surface.

Downstream — the graph is consumed by:

- `xr` (`docs/xr.md`) — adds the XR access records described above; the XR module owns image-set negotiation and predicted-display-time, the graph owns scheduling and barriers for XR-tagged passes.
- `render.reconstruction` (`docs/render-reconstruction.md`) — TAA and upscaling passes register as graph passes; jitter offsets ride in from `frame.pacing` through `Frame_Info`. The reconstruction module's history buffers are exported resources, so the graph's cross-frame chain carries them.
- `media.video` (`docs/media-video.md`) — video decode/encode/process pass kinds are graph pass kinds; the codec surface lives in `media.video`, the scheduling lives here. Video decode feeding an `importExternalTexture`-style graphics consumer rides the same `Imported` / `Exported` discipline as any other cross-module resource handoff.
- `io.asset` (`docs/io-asset.md`) — streaming-driven residency decisions consult the graph's per-frame access summary to prioritize residency for resources the next frame's graph will declare.
- `provider` (`docs/provider.md`), `platform.surface` (`docs/platform-surface.md`), `display` (`docs/platform-display.md`), `sensor` (`modules/sensor/spec.md`) — orthogonal; the graph does not depend on them directly but cohabits the same engine reactor. Display HDR metadata and surface format do flow through the swapchain image's import descriptor, but that is a Layer 0 concern; the graph just inherits.
