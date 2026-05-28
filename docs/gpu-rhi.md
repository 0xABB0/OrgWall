# Melody GPU RHI — Architecture Spec

This document specifies the redesign of `modules/gpu`. It replaces the current hello-triangle-tier abstraction with an explicit, capability-rich Render Hardware Interface (RHI) whose ceiling is the current explicit-API frontier — **Vulkan Roadmap 2026 milestone over the 1.4 line / D3D12 with Agility SDK 1.619.3 retail and Shader Model 6.9 plus the 1.720-preview + SM 6.10 LinAlg track named where it lands first / Metal 4 on Apple Silicon (macOS 26 / iOS 26 / visionOS 26+)** — while still supporting earlier useful versions through capability checks and alternate lowerings. The spec is full-scope; the implementation is phased — Layer 0 (the RHI itself) lands first, the render graph (Layer 1) follows in a second milestone.

This document is bound by the Ten Commandments of the Engine. Where a decision turns on one, the commandment is cited by tag (`MEL-ENGINE-N`).

---

## 1. Design principles

The RHI rests on the engine's commandments and two global policies established during design. Both policies govern every unit in this spec; they are stated once here and not repeated in each section.

### P1 — Emulate-to-equivalent

When a backend lacks something the API expresses, the constrained backend **presents the same API shape and behavior, implemented differently**. The user writes one path. Capability-gating is the exception, reserved for what cannot be faithfully emulated — where emulation would *lie* (behavior diverges under load; a depended-on effect silently no-ops). The test is **behavioral fidelity**. Examples of emulation: WebGPU adapter enumeration, timeline semaphores on its single queue, automatic barriers as no-ops, work-graph dispatch lowered to compute/indirect loops. Examples of honest gating: hardware ray tracing, mesh/task shaders, true bindless heaps on WebGPU core, hardware video encode/decode, native ML/tensor encoders. Caps always report the tier so power users *can* branch on emulated-vs-native; they are never *forced* to.

The API is designed to the most-capable backends. WebGPU is never allowed to set the ceiling. (MEL-ENGINE-VII: age forward, not backward.)

**Sync/async refinement.** When the default API form is synchronous and a backend cannot provide it synchronously, the sync form **hard-errors** on that backend; the async form is the portable alternative. Faking sync where it cannot be honored is forbidden. (MEL-ENGINE-VIII.)

### P2 — Full-control escape hatches

Every engine-managed convenience exposes the primitives required for the app to **fully reimplement** it. The escape hatch is a **peer** of the convenience, not a degraded subset, not a simple toggle. The simple path *is* the powerful path further along (MEL-ENGINE-II); the architecture bends, never breaks (MEL-ENGINE-IV). Test, when designing any convenience: "can the app fully reimplement what we provide?" If not, primitives are missing.

P2 applies throughout — pipeline cache, bindless tables, render graph, allocator, sync, native interop, command pools, queries, media/video, acceleration structures, compute/ML numerics, and GPU-driven work scheduling — and every relevant section calls out the escape.

### Hard prohibitions

- **No deferring.** Half-implementations multiply rework. The spec is full-scope. Implementation is phased only when adding a feature later is **purely additive** (does not change existing API). The render graph is the one case that qualifies.
- **No silent failure.** Every creation failure logs the underlying cause at the site (MEL-ENGINE-VIII).
- **No thread-local last-error.** Errors travel with the operation's result, never in ambient state — incompatible with fiber suspension and thread migration in the job system.

---

## 2. Backend targets

The RHI distinguishes **ceiling** from **support floor**. The ceiling is where each backend has a first-class, idiomatic path; the floor is the oldest runtime still worth supporting without deforming the API. Capability checks are mandatory between floor and ceiling. A feature being core in a newer API version only removes extension ceremony on that path; it never licenses pretending the feature exists on an older runtime. This follows MEL-ENGINE-VII: age forward, but degrade gracefully.

- **Vulkan — floor Vulkan 1.2 with required feature probes, preferred profile Roadmap 2024 Milestone, ceiling Roadmap 2026 Milestone over the 1.4 line.** Vulkan 1.2 is the oldest explicit Vulkan runtime still worth carrying; earlier Vulkan versions buy too little compared with a separate OpenGL backend. The floor is **not** "1.2 therefore every 1.2 device works" — device creation probes the required Tier 0 / Tier 1 feature set, then every higher feature gates through U4. Core-in-1.2 features such as `VK_EXT_descriptor_indexing`, `VK_KHR_timeline_semaphore`, and `VK_KHR_buffer_device_address` are still treated as requested-and-granted capabilities; absence means the corresponding binding, timeline, or address feature is absent, not faked. Vulkan 1.2 floor lowerings keep legacy fallbacks where faithful: `VK_KHR_synchronization2` if granted, otherwise legacy barriers; `VK_KHR_dynamic_rendering` if granted, otherwise render passes; descriptor indexing / update-after-bind where granted, otherwise classic descriptor sets; BDA where granted, otherwise the U14 descriptor-table floor. Roadmap 2024 is the **preferred Vulkan profile** for modern-but-not-frontier devices, not the support floor.

  Khronos's Roadmap 2026 Milestone (announced 23 January 2026) is the substantive ceiling target; Vulkan 1.4 is its minor-version vehicle, not the modern surface by itself. The Roadmap 2026 mandated set is the idiom and the architecture has first-class paths for each: `VK_KHR_robustness2`, `VK_KHR_pipeline_binary`, `VK_KHR_fragment_shading_rate`, `VK_KHR_shader_clock`, `VK_KHR_workgroup_memory_explicit_layout`, `VK_KHR_compute_shader_derivatives`, `VK_KHR_maintenance7`/`8`/`9`, `VK_KHR_depth_clamp_zero_one`, `VK_KHR_copy_memory_indirect`, `VK_KHR_shader_untyped_pointers`, `VK_KHR_present_mode_fifo_latest_ready`, `VK_KHR_present_id2`, `VK_KHR_present_wait2`, `VK_KHR_surface_maintenance1`, `VK_KHR_swapchain_maintenance1`, `VK_KHR_cooperative_matrix`, plus mandated `hostImageCopy`. The 1.4 core promotions that compose the high path are `VK_KHR_dynamic_rendering_local_read`, `VK_KHR_global_priority`, `VK_KHR_index_type_uint8`, `VK_KHR_line_rasterization`, `VK_KHR_load_store_op_none`, `VK_KHR_maintenance5`/`6`, `VK_KHR_map_memory2`, `VK_KHR_push_descriptor`, `VK_KHR_shader_expect_assume`, `VK_KHR_shader_float_controls2`, `VK_KHR_shader_subgroup_rotate`, `VK_KHR_vertex_attribute_divisor`, `VK_EXT_host_image_copy`, `VK_EXT_pipeline_protected_access`, `VK_EXT_pipeline_robustness`, plus mandated `scalarBlockLayout`. Ceiling extensions named individually because the architecture depends on each: `VK_EXT_descriptor_heap` (announced 23 January 2026 alongside Vulkan 1.4.340 and the Roadmap 2026 milestone, deep-dived in Tobias Hector's "Simplifying Vulkan One Subsystem at a Time" 5 February 2026; intended replacement for `VK_EXT_descriptor_buffer` and the legacy `VkDescriptorSetLayout` / `VkPipelineLayout` machinery — `descriptor_buffer` framed as the prior attempt being superseded — DX12-style two-heap split, requires `VK_KHR_buffer_device_address` or Vulkan 1.2 plus `VK_KHR_shader_untyped_pointers`) for U14's bindless ceiling, with `VK_EXT_descriptor_buffer` as the transitional path; `VK_EXT_shader_object` for the fully-dynamic-state lane in U15 / §6.5; `VK_KHR_pipeline_binary` as the primary modern pipeline-persistence path; `VK_KHR_unified_image_layouts` (KHR ratified 6 June 2025, SDK 1.4.321.0) to collapse U17 transitions where granted; `VK_EXT_dynamic_rendering_unused_attachments` for U16 pipeline combinatorics; `VK_EXT_attachment_feedback_loop_layout` + `VK_EXT_attachment_feedback_loop_dynamic_state` for feedback loops; `VK_EXT_ray_tracing_invocation_reorder` (multi-vendor EXT, 18 November 2025) for SER; `VK_KHR_cooperative_matrix` + the NV-only `VK_NV_cooperative_vector` / `VK_NV_cooperative_matrix2` + `VK_ARM_tensors` for ML/tensor; `VK_EXT_device_generated_commands` for U15 indirect; `VK_AMDX_shader_enqueue` (provisional, AMD-only, revision 2 mesh nodes July 2024, RDNA3+/RDNA4) for U28 work-graphs native; `VK_KHR_present_wait2` + `VK_KHR_present_id2` + `VK_KHR_swapchain_maintenance1` + `VK_KHR_surface_maintenance1` for U18 / U19; the `VK_KHR_video_*` codec and maintenance set (decode H.264/H.265/AV1/VP9, encode H.264/H.265/AV1, `video_maintenance1`/`2`, `video_encode_quantization_map`) for U27; `VK_EXT_memory_priority` + `VK_EXT_pageable_device_local_memory` for U8 residency. Note: Vulkan Profiles (VPK) is the declarative carrier for requesting Roadmap 2024 / Roadmap 2026 as preferred profiles; failure to grant a profile falls back to explicit feature probes when the app allows the Vulkan 1.2 floor.
- **Metal — floor Metal 2/3 class where still useful, ceiling Metal 4 on Apple Silicon — M1+ Mac, A14+ iPhone/iPad, gated to macOS 26 / iOS 26 / visionOS 26+.** On Mac, Metal 4 is conjunction-gated on macOS 26 *and* Apple Silicon — the four Intel Macs that still boot macOS 26 (Mac Pro 2019, MBP 16″ 2019, MBP 13″ 2020 4TB3, iMac 2020) get Metal 3 only, and macOS 27 will be Apple-Silicon-only. Metal 4 has the first-class lowering: `MTL4CommandBuffer` (created via `MTLDevice.makeCommandBuffer()`, decoupled from queue, paired with explicit `MTL4CommandAllocator` pools); `MTL4ArgumentTable` (explicit per-stage binding tables — the "one buffer binding" bindless shape); the rewritten encoder model (`MTL4ComputeCommandEncoder` unifies compute + blit + acceleration-structure work; `MTL4RenderCommandEncoder` adds a logical→physical attachment map); `MTL4MachineLearningCommandEncoder` for `.mtlpackage` ML networks on the GPU timeline; `MTL4Compiler` as an explicit CPU-side compile object with `makeLibrary(descriptor:)`, pipeline serialization, and unspecialized→specialized reuse of Metal IR; `MTLTensor` as a first-class resource peer to buffer and texture, including MSL 4 cooperative-tensor reductions and `Convolution2d` descriptors; **Shader ML** for inline small networks in fragment/vertex/compute (no encoder round-trip); the Metal 4 low-overhead Barrier API for granular sync (relevant to U17, which the spec otherwise lowers via encoder boundaries on Metal); placement sparse resources; and ray-tracing intersection-function buffers in MSL 4. `MTLResidencySet` shipped earlier (macOS 15 / iOS 18, Metal 3 era) and is *integrated* by Metal 4 via `MTL4CommandQueue.addResidencySet(_:)` / `MTL4CommandBuffer.useResidencySet(_:)`, not introduced by it. Earlier Metal runtimes remain supported behind availability checks and backend shims where they can honor the behavior: classic command buffers/encoders, argument buffers where available, heap/resource-option differences, older capture/debug paths. The API shape does not collapse to Metal 2; the backend chooses the best faithful lowering.
- **D3D12 — floor Feature Level 12_0 / Shader Model 6-era runtime, retail ceiling Agility SDK 1.619.3 / Shader Model 6.9, preview ceiling Agility SDK 1.720-preview / Shader Model 6.10 LinAlg.** The Agility 1.619.3 + SM 6.9 retail path is first-class: enhanced barriers (`ID3D12GraphicsCommandList7::Barrier` + `ID3D12Device10` resource creation with initial layout), Work Graphs 1.0 (compute/broadcast nodes, retailed in Agility 1.613.0 March 2024), GPU upload heaps (`D3D12_HEAP_TYPE_GPU_UPLOAD`, requires ReBAR), mesh/amplification shaders, dynamic resource binding (`ResourceDescriptorHeap` / `SamplerDescriptorHeap`), **DXR 1.2** (Shader Execution Reordering with `HitObject` + Opacity Micromaps — SER is required by SM 6.9), **Long Vectors** (≤1024 elements, the headline SM 6.9 feature), mandatory native 16-bit and 64-bit integer and wave ops, CPU Timeline Query Resolves, revised resource-view creation APIs, Periodic Trim Notifications, increased 1D dispatch limits, and D3D12 Video Decode/Encode/Process with **AV1 encode (Win11 24H2 / WDDM 3.2)** alongside H.264/HEVC and HEVC 4:2:2/4:4:4. **Cooperative-vector and wave-matrix intrinsics did *not* retail in SM 6.9** — the experimental cooperative-vector implementation was deprecated and removed from the compiler, and both were consolidated into the **LinAlg** unified linear-algebra surface in SM 6.10 preview (Agility 1.720-preview, April 2026), which is the preview ceiling alongside Batched Async Commands (removes implicit serialization on `CopyBufferRegion` / `ClearUAV*` / `ResolveSubresource`), Fence Barriers, the Extension Mechanism, `GetGroupWaveIndex()` / `GetGroupWaveCount()` intrinsics, and Work Graphs 1.1 mesh nodes (previewed in Agility 1.715-preview, disabled in 1.716-preview, retail re-enabling pending). Older D3D12 runtimes remain supported through feature queries: resource binding tier, root signature version, wave ops, mesh/RT/VRS/sampler-feedback/work-graph tiers, enhanced-barrier availability, video support, and shader-model availability. D3D11 is not a degraded mode of this RHI.
- **WebGPU — the moving constrained target, split into three cap profiles.** The runtime is **current Dawn on native** and **current browser WebGPU on web**; their capability surfaces have diverged enough — multi-draw-indirect-count, `SharedTextureMemory` / `SharedFence` interop, HLSL pass-through, chromium-experimental subgroup variants, pixel-local-storage are Dawn-native-only — that one bullet no longer suffices. Plus a third lane: **WebGPU Compatibility mode** (Chrome 146, February 2026), the opt-in subset targeting GLES 3.1 for old/mobile hardware and the honest MEL-ENGINE-VI degrade lane below the core profile. WebGPU core still lacks much of what the other targets provide — true bindless (active design at Milestone 3 as of January/February 2026 GPU Web meetings via `GPUResourceTable`, with **sized binding arrays** as the shipping stepping-stone), mesh/task shaders (blocked on bindless), ray tracing (blocked on bindless), persistent CPU mapping, push constants in core, command-buffer indirect (browser; Dawn-native has `multi-draw-indirect`), tile-local read (subpass input), residency control, calibrated timestamps, pipeline cache primitive, hardware video encode/decode commands — so it is consistently the most-emulated backend under P1, and each unit calls out its specific gates. Optional features that *did* ship and the spec consumes: `shader-f16` (Chrome 120), `dual-source-blending` (Chrome 130), `float32-filterable` (Chrome 119), `float32-blendable` (Chrome 132), `subgroups` (Chrome 134) + `subgroup_uniformity` WGSL ext (Chrome 145) — `subgroups-f16` deprecated, combine `shader-f16` + `subgroups` instead, `timestamp-query` (Chrome 121, quantized to 100 µs, isolated-contexts-only), `TRANSIENT_ATTACHMENT` memoryless attachments (Chrome 146), `clip-distances`, `rg11b10ufloat-renderable`, `primitive-index`, `bgra8unorm-storage`, `texture-compression-bc`, and `importExternalTexture` + WebCodecs `VideoFrame` (Chrome 116+, Display-P3 HDR Chrome 121) as a first-class video-import path, not a degraded fallback. **WebGPU is a supported backend with first-class behavior within its constraints; it is never the ceiling of API shape.**

What this scoping buys: the latest API paths are not second-class bolt-ons, but older still-useful explicit runtimes are not discarded. Every backend reports the real feature set it can honor, and the same RHI surface either lowers faithfully, emulates faithfully, or gates loudly (MEL-ENGINE-VIII).

---

## 3. Cross-cutting primitives (Tier 0)

Every signature in the RHI inherits the five primitives in this tier.

### 3.1 Resource identity (U1)

Resources are referenced by **typed handles, value types, each a thin struct wrapping `Mel_SlotMap_Handle`**. One slotmap per resource type, owned by the `Mel_Gpu_Device`. Distinct wrapper types (`Mel_Gpu_Buffer`, `Mel_Gpu_Texture`, `Mel_Gpu_Texture_View`, `Mel_Gpu_Sampler`, `Mel_Gpu_Shader`, `Mel_Gpu_Pipeline`, `Mel_Gpu_Command_List`, `Mel_Gpu_Query_Pool`, `Mel_Gpu_Sync`, `Mel_Gpu_Swapchain`, etc.) give compile-time safety — a texture handle cannot be passed where a buffer is expected. The generation field turns use-after-free into a loud `alive()` failure instead of a dangling-pointer crash (MEL-ENGINE-VIII).

**Bindless slot identity is contractual, and the direct / indirect type split makes the contract visible at the type level.** Engine-created persistent resources of the *direct* family — `Mel_Gpu_Buffer`, `Mel_Gpu_Texture_View`, `Mel_Gpu_Sampler`, `Mel_Gpu_Accel_Struct` — guarantee `bindless_slot == handle.index`: the allocator reserves the heap slot at the slotmap's index at creation time and never relocates it. The fast path is the integer index itself; no per-use lookup is generated, and the engine's per-draw root-record fill consumes `handle.index` directly. **Imported, compacted-heap, and `bindless = capped` (WebGPU floor) resources use *indirect* peer types** — `Mel_Gpu_Buffer_Indirect`, `Mel_Gpu_Texture_View_Indirect`, `Mel_Gpu_Sampler_Indirect`, `Mel_Gpu_Accel_Struct_Indirect` — that carry the slot field separately and require `*_bindless_slot(handle) → u32` to resolve. The two families do not implicitly convert: a function taking a direct handle cannot silently accept an indirect (MEL-ENGINE-VIII). Polymorphic call sites (engine binding-fill code that admits both) take a `u32 slot` rather than a polymorphic handle, so the resolve happens at the call site where the cost is visible. An explicit transition operator `*_make_indirect(direct) → indirect` is provided for the rare engine-driven compaction path that needs to migrate a direct allocation into a compacted heap; it invalidates the original direct handle's generation. Identity never constrains residency, and descriptor pressure never invalidates identity (MEL-ENGINE-IX). Value handles are trivially copyable, threadable, and serializable.

Each slot carries an **`Owned`/`Borrowed` ownership flag** for the inbound-import path in U5, **orthogonal to the direct / indirect split**: engine-created allocations are `Owned` (direct in the common case, indirect when compacted into a tight heap or allocated on a `bindless = capped` backend); imports are `Borrowed` (always indirect). `Borrowed` slots are not freed on destroy and their memory is never aliased by the allocator. Non-bindless use sites — copy / blit / barrier / vertex-input / index-buffer records — accept either family identically (MEL-ENGINE-IX). Re-export eligibility rides the import descriptor's flag from U5, not the ownership flag alone.

**Capture-replay metadata.** Resources, samplers, acceleration structures, and the underlying memory pools admit a `capture_replay` creation flag (cap-gated by `caps.debug.capture_replay`, requested on device-create through U4) that records the opaque capture-replay handle in slot metadata. Tools like RenderDoc / PIX / Aftermath require this so that a replayed capture reconstructs identical descriptor addresses and bindless slot indices — without it, captures of a session using the §6.7 ceiling are unreplayable. Flag and metadata live in U1 because they cross every resource type; lowering and per-resource consumption happen in §6.1 / §6.2 / §6.6 / §6.7.

No uniform `destroy_any_handle`. Each resource type has its own `destroy`. The indirect peer types share the same destroy entry points by overload on the typed pointer; both family destroys resolve to the same slotmap-free path.

### 3.2 Status and result model (U2)

Every fallible action returns a **by-value `{ value, status }` result struct**. The `status` *type* is **fully independent per action** — no shared base type, no shared discriminant enum — so swapchain creation enumerates only swapchain outcomes, pipeline creation only pipeline outcomes, and no shared mega-enum accretes. The status *encoding* is shared: every per-action status enum reserves its low two bits for `Mel_Gpu_Severity ∈ { Ok, Warned, Error }`, with the diagnostic enum value in the upper bits. Per-action `failed(status)` / `warned(status)` helpers are therefore branch-free bit-tests, and generic code (logging, retry layers, the U3 `*_sync` wrapper) dispatches on `Severity` without committing to one action's vocabulary. **Handle validity remains the usability signal** (the status is purely diagnostic).

The status carries warnings as a **per-action bitset** alongside a per-action error enum. A non-empty warning bitset coexists with a valid handle — request a non-blocking swapchain, none available, get a vsynced swapchain plus a warning naming the substitution. This is the success-with-degradation channel.

**Naming convention:** `Mel_Gpu_<Action>_Result` for the result struct, `Mel_Gpu_<Action>_Status` for the per-action status. *Status*, not *Error* — the term is precise when the carrier can report success-with-warning.

**Scope of fallibility.** Creation, submission, present, acquire, and map operations are in the error model. Hot-path recording calls (bind, draw, dispatch, barrier, begin/end rendering) are **contract calls**, not fallible ones — debug-asserted on misuse, validated by U21 in debug, branch-free and silent in release (MEL-ENGINE-III: no per-draw error tax).

Detailed human-readable causes (the `VkResult`, the Slang diagnostic, the Dawn message) always *also* go to `mel_log_error("gpu", ...)` at the failure site so the enum stays small while no detail is lost.

### 3.3 Awaitable / async execution (U3)

All creation operations return a future. The future is **reactor-driven** as the portable core; the job system is an accelerator, not the foundation.

**Why reactor-core.** The web target is single-threaded under default Emscripten and resolves WebGPU callbacks only when control returns to the browser event loop; Dawn on native is the same shape (callbacks fire only when `wgpuInstanceProcessEvents` is pumped). A fiber that parks waiting for a result that arrives only when the event loop runs deadlocks. Meanwhile every app already lives on the reactor; the job system is wired into nothing today.

**Completion pump.** One completion pump per device, attached to an owner reactor at device creation. **Not** a reactor source per action — the reactor is not built to carry hundreds of sources, and the completion-port shape is correct here. Inside the pump, **one reactor source per completion family**: one poll-set FD on Linux (epoll over Vulkan `external_fence_fd` / `external_semaphore_fd`), one IOCP handle on Windows, one kqueue on Darwin, one `ProcessEvents` tick-source on WebGPU. The pump owns an internal registry of in-flight operations and multiplexes them onto that single per-family source; hundreds of in-flight ops cost one reactor source, not hundreds. *Web exception:* Dawn's `wgpuInstanceProcessEvents` is per-`WGPUInstance`, not per-device — on the WebGPU backend a single pump binds to the instance and services every device created under it; per-device pump objects remain in the public API for shape consistency but multiplex onto the one instance tick.

**Pump cadence.** Decoupled from the frame loop. Prefer a **native-waitable poll** registered on the pump (Vulkan fence fds via `VK_KHR_external_fence_fd`, Windows IOCP handles) — lowest latency, no spin. Fall back to an **independent timer faster than the frame**, and only opportunistically also pump per-frame. Resource creation often happens at load and between frames; binding completion to 60 Hz would add up to 16 ms of dead time and stall when the app is not drawing.

**Future shape.** A `Mel_Gpu_<Action>_Future` holds a slot for the per-action result (U2) and a continuation. It carries a **target reactor**; when the op finishes, the pump routes the continuation there — directly if same thread, via `mel_reactor_post` if not. Register on thread A, resume on thread B is a built-in pattern.

**Three completion primitives**, unified behind one future contract — the integrations are inherently plural; forcing them behind one mechanism would be the mistake:

- **Native-waitable poll** — Vulkan fence fds, Windows IOCP handles, registered on the reactor via `mel_reactor_source_add_poll`.
- **Pump on tick** — Dawn `wgpuInstanceProcessEvents`, which exposes no waitable, only "drain me." A pump-source ticks it from the device's reactor.
- **Thread-callback bridged by `mel_reactor_post`** — Metal completion handlers / shared-event notification blocks, and **job-system completions** for CPU-bound work (Slang compilation, `vkCreateGraphicsPipelines` on a compile thread pool). The bridge is the same regardless of source.

**Consumer ergonomics.** Coroutine suspension via `async.coroutine` (already dt-driven on the reactor) for `await`-shaped user code. A plain continuation-callback form for non-coroutine code. A `*_sync` wrapper that pumps the reactor until resolved, for tooling and startup. **`*_sync` called from the reactor's own thread debug-asserts** — re-entrant pumping deadlocks; the wrapper is for off-reactor threads only (MEL-ENGINE-VIII).

**Cancellation.** In-flight futures are **not cancellable** in M1-M2: the reactor-core pump cannot interrupt a Slang compile, a `vkCreateGraphicsPipelines` call on the compile pool, or a WebGPU async resolve without driver cooperation. A future awaiting a device that is destroyed resolves with `Severity = Error` and a `device_lost` code (U6). Explicit `*_cancel` is reserved as a future additive surface if a concrete need emerges; introducing it later does not change existing futures' contract.

**Backpressure.** The pump's cross-thread post path through `mel_reactor_post` is bounded. Each pump carries a high-water mark on in-flight cross-thread continuations (default sized to roughly one frame's worth of pipeline / readback / present completions, configurable at device creation); above the mark, completions for the *same* future coalesce (a second resolve-post for an already-pending future is a no-op, not a queue duplicate), and a U2 warning surfaces on the next device-level event. The pump never silently drops a unique completion ※ coalescing only collapses redundant posts. The hard ceiling is a debug-asserting fail at 4× the high-water mark — the symptom of a real bug (a reactor that has stopped pumping), not a load condition the engine should paper over (MEL-ENGINE-VIII).

**Retirement is future-gated.** Every reclamation that must wait until the GPU has finished consuming a resource — command-pool reset (U15), transient-ring reset (U8/U9), deferred-destroy of resource slots (U1), bindless-slot reclamation (U14) — takes a dependency on the completion future of the last submission consuming it. There is no public `Mel_Gpu_Frame_Index`; the engine tracks retirement internally as the set of pending futures gating each resource. The render loop's frame cadence is incidental to retirement, not the basis for it. This pins the answer to "when is it safe to free this" to one mechanism across every unit (MEL-ENGINE-IX).

### 3.4 Capability / feature model (U4)

`Mel_Gpu_Caps` is immutable, queried from the device. It is a **feature database**, not a flat grab-bag. Top-level domains: `adapter`, `memory`, `queues`, `shader`, `compute`, `raster`, `tessellation`, `ray_tracing`, `work_graphs`, `media`, `presentation`, `interop`, `queries`, `power`, and `debug`. Graduated features are **tiered enums** (`bindless = none | capped | full`; `multi_adapter = none | external_share | linked_device`; `ray_tracing = none | inline | pipeline`; `cluster_accel_struct = none | emulated | vendor | core`; `partitioned_tlas = none | emulated | vendor | core`; `timeline = native | emulated`; `tile_local = none | emulated | native`; `work_graphs = none | emulated | native`; `video_decode = none | import_only | hardware`; `video_encode = none | import_only | hardware`; `video_process = none | shader_emulated | hardware`; `protected_media = none | decode_only | process_present`; `ml_tensor = none | shader_emulated | native`; `matrix_scope = none | thread | wave | group | full` (SM 6.10 LinAlg alignment); `vrs = none | per_draw | per_primitive | per_image`; `foveation = none | static | gaze_driven`; `sampler_feedback = none | tier1 | tier2`; `sparse_buffer = none | partial | full`; `sparse_texture = none | tier1 | tier2`; `asset_io = none | cpu_staged | gpu_decompress`; `latency_marker = none | reflex | anti_lag | xess_fg | platform_native`; `tessellation = none | partial | full`; `pipeline_robustness = none | per_pipeline | per_stage`; `indirect_state_change = none | tier1 | tier2`; `mutable_descriptor_type = none | partial | full`; `capture_replay = none | partial | full`; `performance_query = none | passes | streaming`; `internally_synchronized_queues = none | partial | full`; `adapter_type = discrete | integrated | software | virtual | external`; `power_source = ac | battery | unknown`; `low_power_mode = off | on`). Binary features remain bools only when the feature is truly yes/no.

Limits live inside the domain that owns them: descriptor-table sizes, heap residency, max bindless slots per heap, and per-resource indirect-slot budget under `memory/bindless`; max texture dimensions and sparse page shape under `memory/textures`; max workgroup size, shared memory, subgroup sizes, dispatch dimensions, and indirect-dispatch support under `compute`; wave/subgroup operations, numeric formats (`fp64`, `fp32`, `fp16`, `bf16`, `fp8` E4M3/E5M2, `int64`, `int16`, `int8`, `int4`), integer-dot-product, packed-dot integer-8/16, cooperative matrix/vector/tensor support, barycentrics, derivatives, memory scopes, atomics-by-type (`atomic_int64`, `atomic_float32`, `atomic_float16`, `image_atomic_int64`), and shader clock under `shader`; max tess patch control points, isoline support, point-mode tessellation, and per-patch outputs under `tessellation`; codec/profile/bit-depth/chroma support under `media`; color spaces, HDR metadata, present modes, VRR, shared-presentable-image, and timing under `presentation`; CPU↔GPU calibrated timestamp tier, HW performance-counter passes / streaming under `queries`. The `adapter` domain exposes `adapter_type`, vendor ID, device ID, driver version, `luid` (Windows / D3D12), `uuid` (Vulkan / Metal), and the OpenXR-runtime-published required-adapter LUID matcher when the runtime is present. The `power` domain exposes `power_source` and `low_power_mode`, both event-driven (callbacks installed on `Mel_Gpu_Instance`). Caps also carry per-format support tiers (`sampled`, `storage`, `attachment`, `blend`, `linear_filter`, `ycbcr_sampled`, `video_decode_output`, `video_encode_input`, `external_only`, `sampler_feedback_pair`).

**Enablement is request-and-grant only — no auto default.** Device creation requires an explicit desired feature set; the backend enables only the corresponding extensions; `caps` reports what was actually granted. Users name what they want. The battery-conscious build enables nothing it does not use (MEL-ENGINE-III).

### 3.5 Native interop — escape hatch and import (U5)

Two directions, both first-class.

**Outbound — the escape hatch.** Per-backend **opt-in native integration headers** under `gpu/<backend>/` — `gpu/vulkan/<integration>.h`, `gpu/metal/<integration>.h`, `gpu/webgpu/<integration>.h`, `gpu/d3d12/<integration>.h` — expose typed accessors for every handle type: device, queue, command list, buffer, texture, sampler, pipeline, shader. The umbrella `gpu.h` stays backend-clean; no application that includes the core surface drags in `vulkan.h` or `Metal/Metal.h`. Only a translation unit that deliberately includes a backend-folder header pays the header cost, and the typed signatures give full safety.

The highest-value escape is grabbing the native command buffer **mid-recording** to inject an unwrapped command (an extension the engine has not yet wrapped). This is paired with a **U17 state-resync hook** — `cmd_assume_state(resource, subresource_range, state, queue_owner?, stage_access_override?)` — so the user can hand state back coherently after the raw work. Past the hatch the user owns correctness (an explicit opt-out of validation, MEL-ENGINE-VIII).

**Outbound — export.** Export is a first-class interop operation, not merely "ask for the native pointer." Exportable buffers, textures, swapchain image sets, memory pools, and sync primitives declare their export intent and allowed handle families at creation/import time (`fd`, Win32 `HANDLE`, `IOSurface`, `AHardwareBuffer`, DXGI shared resource, `MTLSharedEvent`, Dawn `SharedTextureMemory` / `SharedFence`, etc.). `buffer_export`, `texture_export`, `memory_pool_export`, and `sync_export` return typed export records carrying ownership, state/layout, queue-family or node ownership, protection restrictions, and close/release obligations. Borrowed resources may be re-exported only if the import descriptor grants that right. Protected media resources refuse export unless the protected path explicitly permits it. Browser WebGPU reports export support as absent; Dawn-native and platform backends report the exact external-memory and external-sync tiers they can honor.

**Inbound — import.** External native resources (`VkImage`, `MTLTexture`, `WGPUTexture`, external buffers, camera/video frames, multiplanar image planes, swapchain image sets) are wrapped into **`Mel_Gpu_*_Indirect` handle types** (§3.1's indirect family) carrying the `Borrowed` flag. At non-bindless use sites — copy / blit / barrier / vertex-input / index-buffer records — they are interchangeable with direct engine-created handles (MEL-ENGINE-IX). At bindless use sites the user resolves the slot via `*_bindless_slot(indirect_handle) → u32`; the engine's binding-fill code paths take a `u32 slot` precisely to admit both families through one entry. The indirect type stores the slot field separately because imports cannot honor the slot-equals-index contract — the resource's heap slot is whatever the engine allocates at import time. The `Borrowed` flag governs destroy (no underlying free), allocator behavior (no memory aliasing), and re-export eligibility.

External memory imports go through the U8 allocator (which wraps borrowed `VkDeviceMemory`/`MTLHeap`). External-swapchain image sets are first-class in U18 for OpenXR and video interop.

**External sync is unified with internal sync.** The U17 binary semaphore, timeline semaphore, and submission completion-future are each constructible **internally OR from an external native handle** (fd, Win32 `HANDLE`, `MTLSharedEvent`). The user waits and signals them at submission identically regardless of origin. Cross-API and cross-process interop — a video decoder, OpenXR, CUDA, OpenCL, GStreamer DMA-BUF chain handing you a frame plus a semaphore — is "import the texture, import the semaphore, wait on it before your pass." Same primitives, one path.

**Named cross-API interop targets.** The U5 import surface explicitly designs for the following sources so the implementation never has to retrofit them: **CUDA** (`cuImportExternalMemory` / `cuImportExternalSemaphore` over `VK_KHR_external_memory_fd|win32` + `VK_KHR_external_semaphore_fd|win32`; D3D12 shared NT handles; Metal `IOSurface` + `MTLSharedEvent` bridged via Apple's MPS / CoreML interop), **OpenCL** (`cl_khr_external_memory` + `cl_khr_external_semaphore` family), **GStreamer / V4L2 / PipeWire** (DMA-BUF fd import with implicit-sync→explicit-sync conversion via Linux sync_file fds), and **WebCodecs / browser composition** (already covered via the WebGPU `importExternalTexture` + `SharedTextureMemory` paths in §6.2 / §7.4). Capability-gated by `external_memory` and `external_sync` tiers and the `interop.cuda_compat` / `interop.opencl_compat` flags; honestly absent on browser WebGPU.

---

## 4. Mobile and power (U22 cross-cutting constraint)

All mobile GPUs (Apple, Mali, Adreno, PowerVR) are **tile-based deferred** (TBDR); battery cost is dominated by **memory bandwidth**, not ALU. The architecture honors this directly (MEL-ENGINE-VI: a phone hath a battery; honor it). Two settled principles:

- **Tile-local read is first-class** in the rendering API, expressed as a **semantic dependency declaration** ("pass B reads attachment A on-tile"), never as a backend-specific primitive. Each backend lowers idiomatically: Metal tile shaders / framebuffer fetch where available; Vulkan `dynamic_rendering_local_read` on 1.4 or the extension, otherwise subpasses/separate passes; D3D12 separate passes + barriers; WebGPU separate passes (P1 emulate). Declare intent, the backend chooses the idiom.
- **Device class (TBDR vs IMR) is a runtime capability, not a compile-time class split.** One Vulkan binary spans Mali (TBDR) and desktop NVIDIA/AMD (IMR); one Metal binary spans Apple Silicon (TBDR) and Intel Mac (IMR). A `Desktop`/`Mobile` translation-unit split would mis-specialize. Cap tier `tile_local = none | emulated | native` reports the runtime fact. Per-platform backend TUs still dead-strip impossible code paths (the web TU contains no tile-local machinery at all), giving the compile-time leanness as a side effect of the per-platform build, not as a user-facing class split.

Specific mobile requirements land in their home units: per-attachment load/store/clear actions and on-tile MSAA resolve in U16; memoryless / transient attachments in U8 and U10; unified-memory-aware allocation that skips staging in U8 and U9; platform-vsync and on-demand frame pacing in U19. The tiler-specific optimization surface — `Mel_Gpu_Tiler_Profile` and the per-vendor mobile providers (Apple, Adreno, Mali / Immortalis, PowerVR) plus the Meta Quest mobile-budget contract — lives in §9.7 (U33).

---

## 5. Device foundation (Tier 1)

### 5.1 Instance / Adapter / Device (U6)

A three-object phased model, **nothing hidden**:

- **`Mel_Gpu_Instance`** — the enumeration root, validation-config owner, and host of the `adapter_removed`, `power_source_changed`, and `low_power_mode_changed` event callbacks (`VkInstance`, `WGPUInstance`; trivial on Metal). Created explicitly.
- **`Mel_Gpu_Adapter`** — a candidate GPU whose caps the user **inspects before committing** (`VkPhysicalDevice`, `WGPUAdapter`, `MTLDevice`-as-adapter). The adapter interface is **uniform across all backends**: enumerate adapters + inspect caps (features/limits/info) + power preference. Each adapter exposes `adapter_type ∈ { discrete | integrated | software | virtual | external }`, vendor / device IDs, driver version, the platform-native identity (`luid` on D3D12 / DXGI, `uuid` on Vulkan, registry ID on Metal), and the OpenXR-runtime-published required-adapter LUID matcher when an OpenXR runtime is present (so XR sessions can deterministically pick the runtime-required adapter without the user reaching through native interop). Software adapters (WARP, lavapipe, SwiftShader) are first-class enumeration entries; apps that exclude them filter on `adapter_type`.
- **`Mel_Gpu_Device`** — created from a chosen adapter with the U4 request-and-grant feature set.

**WebGPU presents a synthetic adapter set**, not true enumeration. The backend requests the meaningful candidates the platform exposes (`Default`, `HighPerformance`, `LowPower`, and `Compatibility` where available), de-duplicates adapters whose privacy-limited identity and caps collapse to the same object, and reports that set through the uniform `Mel_Gpu_Adapter` interface. Caps come from `adapter.features` / `adapter.limits`; identity from `adapter.info` is advisory and privacy-limited. Emulated, not degraded — but not falsely equivalent to native physical-device enumeration.

**Power preference is first-class** (battery, MEL-ENGINE-VI). Adapter request takes `LowPower` / `HighPerformance` / `Default`.

**Headless is first-class.** Device creation never requires a surface. Compute-only, offscreen, and server rendering all create a device with no swapchain; presentation is a queried property when a surface later appears.

**No device singleton.** Multiple devices (multi-GPU, isolation) are allowed.

**Device groups / linked adapters are first-class.** Adapter enumeration may return both physical adapters and device-group candidates. Device creation can request one adapter or a group with explicit node/queue affinity. Resources may carry an optional residency/visibility mask; queues report their node. Vulkan lowers to device groups, D3D12 to linked adapters / node masks, Metal exposes separate devices with shared external-memory paths where possible, WebGPU reports no device-group support. The simple single-adapter path remains the default; multi-adapter is a peer, not a hidden special case.

**Device loss is a first-class lifecycle event.** Vulkan returns `VK_ERROR_DEVICE_LOST` from submit/wait; D3D12 reports device removal; Metal's `MTLDevice` becomes invalid; WebGPU's `device.lost` promise resolves. The RHI carries a `device_lost` callback on `Mel_Gpu_Device`, installed at device creation; on a loss event the engine routes the callback, **invalidates every handle issued by that device** (slotmap generations roll), **resolves every in-flight future with an `ERROR`-severity status** carrying a `device_lost` code, **tears down the U3 completion pump** for that device, and reports the loss reason through U21's crash-diagnostics path (DRED, Aftermath, `VK_EXT_device_fault`, Metal command-buffer error state) where available. Recovery is the app's job: re-enumerate adapters via the phased model above, create a new device, rebuild resources. The engine **never silently substitutes** a fresh device for the lost one — the contract is to fail loudly, then let the user reconstruct (MEL-ENGINE-VIII). The callback slot is part of the device contract from M1 day one, even if the app's recovery flow starts as "log and exit"; adding it later would change every device-destruction contract.

**Adapter loss is a first-class lifecycle event paralleling device loss.** Adapters can be removed at runtime (eGPU disconnect, NVIDIA / AMD hot-unplug, monitor reconfiguration triggering DXGI adapter renumber, `MTLDeviceWasRemovedNotification`, `DXGI_ERROR_NOT_CURRENT`, Vulkan hot-unplug). The `Mel_Gpu_Instance` carries an `adapter_removed` callback installed at instance creation; on removal the engine invalidates the affected `Mel_Gpu_Adapter` handle, cascades device loss on every device created from it (each device's `device_lost` callback fires with reason `adapter_removed`), and detaches the affected outputs (see below). Re-enumeration is the app's responsibility; the engine never silently substitutes. Adapter handles carry generations so use-after-removal is a loud `alive()` failure, not a dangling-pointer crash (MEL-ENGINE-VIII).

**Display / output enumeration is first-class.** Each adapter exposes a list of `Mel_Gpu_Output` candidates — `IDXGIOutput6` on DXGI, `VkDisplayKHR` on `VK_KHR_display` where granted, `NSScreen` on macOS, `UIScreen` on iOS, `wl_output` on Wayland — carrying display name, native resolution, supported refresh modes, **VRR range** (Variable Refresh Rate minimum and maximum Hz), **HDR / wide-color capabilities** (peak / average / minimum luminance from `DXGI_OUTPUT_DESC1` and `EDR` reference values, supported color spaces, mastering-primary support), per-output color profile (ICC where the OS publishes it), and current occlusion / power state. The swapchain (U18) optionally targets a specific output where the platform admits it (Windows exclusive-fullscreen flip, direct-output composition); apps that ignore outputs get the system-default association; apps that care can pick. Output handles invalidate together with their adapter on `adapter_removed`. WebGPU reports a synthetic single-output entry (the rendering canvas's containing screen) with the privacy-limited fields the browser admits.

An optional `mel_gpu_device_create_default(power_preference, features) → Mel_Gpu_Device_Create_Future` is pure composition of the public phased calls — hides nothing — and returns a future so it composes coherently with the rest of U3. The convenience is callable from any thread, including the reactor thread (the U3 `*_sync` deadlock applies only to the *user's* explicit sync wrapper, not to the futures the convenience itself returns).

### 5.2 Queues and submission (U7)

**Availability/acquire model over an explicit device-create queue plan.** `mel_gpu_queue_available(device, role, priority)` reports remaining acquirable queues for that role and priority. `mel_gpu_queue_request(device, role, priority)` hands out an explicit, addressable `Mel_Gpu_Queue` object. Device creation accepts an optional `queue_plan[]` of `{ role, count, priority }` entries; the default plan requests one `Graphics` queue at `Normal` plus the queues required by enabled features. Vulkan commits queue families and global-priority lanes at device creation, so priority cannot be upgraded by a later request; D3D12 creates command queues lazily and Metal exposes logical scheduling lanes, but both still report the same planned availability surface. Roles: `Graphics`, `Compute`, `Transfer`, `AsyncCompute`, `VideoDecode`, `VideoEncode`, `VideoProcess`. Media roles collapse onto compute/graphics or external platform engines where the backend has no addressable GPU queue; caps report `native`, `emulated`, or `import_only`.

Vulkan pre-creates the planned hardware queue set at device creation. A late request allocates from that pool and reports `would_require_device_reopen` when a missing family, count, or priority lane would require a new device.

**Graceful fallback with U2 warning.** Request walks a role-compatibility chain and returns the best available with a warning naming the substitution. The chains, pinned per role:

- `Graphics ⊇ Compute ⊇ Transfer` — a graphics-capable queue can run compute and transfer; a compute-capable queue can run transfer.
- `AsyncCompute → Compute → Graphics` — async-compute is a scheduling hint; the chain widens until a queue is available.
- `VideoDecode → ComputeShaderEmulated → ImportOnly` — no engine-wrapped decode queue; the engine refuses to run a hardware decoder it lacks the queue for and falls to compute-shader emulation only when caps report the codec as `shader_emulated`, otherwise gates to `import_only` so the app supplies platform-decoded frames via U5.
- `VideoEncode → ComputeShaderEmulated → ImportOnly` — symmetric.
- `VideoProcess → ComputeShaderEmulated (color-convert, scale, deinterlace via compute) → ImportOnly`.
- `Transfer → Compute → Graphics` — pure copy/upload work widens upward where the dedicated transfer queue is absent.

Hard-fail only if no role-compatible queue exists at all. Acquire/release lifecycle — `queue_release` returns the queue to availability.

**Global priority hint.** `queue_request` admits an optional `priority ∈ { Low, Normal, High, Realtime }`, lowering to `VK_KHR_global_priority` (Roadmap 2026 mandatory: `VK_QUEUE_GLOBAL_PRIORITY_LOW_KHR` / `_MEDIUM_KHR` / `_HIGH_KHR` / `_REALTIME_KHR`) on Vulkan, `D3D12_COMMAND_QUEUE_PRIORITY_NORMAL` / `_HIGH` / `_GLOBAL_REALTIME` on D3D12, Metal's advisory queue priority where available, and no-op on WebGPU. On Vulkan the requested priority must already exist in the device-create `queue_plan`; if it does not, request demotes within the granted lanes and emits a U2 warning. **Realtime requires platform privilege** (Windows dev-mode or admin for `_GLOBAL_REALTIME`; Linux `CAP_SYS_NICE`; Vulkan driver-policy). Callers that require the exact priority use `queue_request_exact` or a fail-on-demote flag (MEL-ENGINE-VIII reports the demotion honestly).

**Internally synchronized queues** (`VK_KHR_internally_synchronized_queues`, Roadmap 2026; promoted from EXT and shipping in NVIDIA driver 582.29 January 2026). `queue_request` admits an `internally_synchronized: bool` flag (gated by `caps.queues.internally_synchronized`). When set and granted, the per-queue submit lock collapses — the queue is safe to submit from any thread concurrently, and the engine-internal mutex around `queue_submit` (otherwise required by §7.1) becomes a no-op for that queue. Vulkan lowers to `VK_DEVICE_QUEUE_CREATE_INTERNALLY_SYNCHRONIZED_BIT_KHR` at device-create (the flag must already be present in the `queue_plan`; late requests upgrade only when a matching pre-created queue exists). D3D12 / Metal / WebGPU report the cap as absent and the flag downgrades silently to the locked path. The internally-synchronized path is the default for media / asset-IO queues where producers naturally run on many threads.

**Submission is the sync site and returns a completion future.** `queue_submit(queue, { command_lists[], wait[], signal[] }) → future<void>`. The `wait` and `signal` arrays are U17 timeline or binary semaphores (possibly imported, U5). The returned future (U3) resolves when the GPU finishes the batch — "await GPU completion" is the same await as everything else.

**Per-queue thread-safe submit** via an internal lock; the user submits from any thread. Recording (U15) is lock-free per-thread; the per-queue lock applies only at submit handoff.

Async-compute overlap is just "submit to the compute queue signaling a timeline value; have the graphics queue wait it." Serialized (emulated) on WebGPU with `caps.async_compute = false`.

### 5.3 Memory allocator and heaps (U8)

Built on `modules/allocator`, not raw `malloc`/`vkAllocateMemory`.

**Three allocation strategies**, each from an existing primitive:

- **Per-memory-type block suballocator** (the `buddy` allocator) — the VMA-equivalent.
- **Dedicated allocations** for large or render-target resources (`VK_KHR_dedicated_allocation`).
- **Per-frame linear ring** (`modules/allocator/ring`) for transient upload and uniform streaming.

**Roles:** `DEVICE` (GPU-only), `UPLOAD` (host-visible write-combined), `READBACK` (host-visible cached). **`MEMORYLESS`** is a flag on attachment-only resources (`MTLStorageModeMemoryless` / VK `LAZILY_ALLOCATED + TRANSIENT_ATTACHMENT` / WebGPU `GPUTextureUsage.TRANSIENT_ATTACHMENT` since Chrome 146) — depth and MSAA stay tile-resident and never touch RAM (U22). Pre-Chrome-146 WebGPU runtimes fall back to normal allocation; the cap reports the runtime fact.

**UMA is automatic.** Unified-memory cap collapses `DEVICE`/`UPLOAD` onto one heap and makes `buffer_write` a direct write with no staging blit. The API does not change — the roles still exist and resolve to the same heap on Apple Silicon and mobile (U22). D3D12 **GPU upload heaps** (`D3D12_HEAP_TYPE_GPU_UPLOAD`, retailed in Agility SDK 1.613.0 March 2024, requires ReBAR) collapse the same way on supporting hardware — NVIDIA Ampere+, Intel Arc, AMD via current drivers.

**Dual surface** (P2). Implicit path: a resource names a role and the engine allocates from default pools. Explicit path: `Mel_Gpu_Memory_Pool`, **placed resources**, and **memory aliasing** — peer of the implicit path, full power. Memory aliasing is required by U20's render graph for transient resource overlap.

**Full residency mechanism, but the policy lives in the app, not the RHI.** The RHI provides the **mechanism** — budget visibility, explicit residency operations, native residency primitives. It does **not** ship an engine-side eviction policy: a generic policy lacks the semantic knowledge to distinguish a hero asset from a distant LOD, and the canonical failure mode is evicting the player's diffuse map to make room for a particle texture (MEL-ENGINE-V).

- **Budget tracking** via `VK_EXT_memory_budget`, DXGI budget, Metal `currentAllocatedSize` versus `recommendedWorkingSetSize`. Exposed as a query plus a **`budget_pressure` event** — the engine raises it upward to a user-registered callback when allocations approach or exceed budget, with the current/desired figures and the offending pool. The streaming / quality / asset system holds the semantic knowledge of what is worth keeping and decides what to unload.
- **Explicit residency operations** — `make_resident`, `evict`, Metal `MTLResidencySet` (macOS 15 / iOS 18+) integrated into Metal 4 command-queue/buffer residency, D3D12 `MakeResident`/`Evict`. On Vulkan the modern primitives behind these calls are `VK_EXT_memory_priority` (per-allocation priority hint informing driver eviction order) and `VK_EXT_pageable_device_local_memory` (pageable device-local heap that the driver pages in/out under host pressure); the engine surfaces both transparently and caps-gates each. These are the primitives the app uses to act on the pressure signal, or to drive residency manually (P2).
- **No engine-side eviction policy.** Without a `budget_pressure` handler the engine reports OOM honestly (status severity `ERROR`) and refuses the allocation; the app installs whatever policy fits its content model on top of the primitives above.

WebGPU has no residency control — `caps.residency_control = none`; the residency calls are advisory/no-op, the budget-pressure event never fires, and the user is informed honestly.

**External memory import** (U5) wraps borrowed `VkDeviceMemory` / `MTLHeap`; never freed, never aliased.

**Suballocation on WebGPU emulates 1:1** — each allocation is one `WGPUBuffer`/`WGPUTexture`, since opaque `WGPUBuffer` cannot be suballocated. The suballocation *efficiency* degrades; behavior is intact (P1).

---

## 6. Resources (Tier 2)

### 6.1 Buffers (U9)

Operations:

- **`buffer_create({ size, usage, role, pool?, flags?, initial_data? }) → Mel_Gpu_Buffer_Create_Future`.** Resolves to `{ Mel_Gpu_Buffer, status }`. Usage flags: `VERTEX | INDEX | UNIFORM | STORAGE | STORAGE_TEXEL | INDIRECT | INDIRECT_COUNT | COPY_SRC | COPY_DST | SHADER_DEVICE_ADDRESS | AS_BUILD_INPUT | AS_STORAGE | SBT | VIDEO_BITSTREAM | VIDEO_PARAMETER | ML_TENSOR | CLUSTER_AS_BUILD_INPUT | CLUSTER_AS_STORAGE | PARTITIONED_TLAS_INSTANCE | SAMPLER_FEEDBACK_PAIRED`. Storage, indirect, device-address, acceleration-structure, cluster-AS, video-bitstream, and tensor usages are the essentials for GPU-driven rendering, compute, media, and ML-shaped workloads. **Creation flags** (`flags?`) include `CAPTURE_REPLAY` (caps-gated by `caps.debug.capture_replay`; reserves a stable BDA / descriptor address so RenderDoc / PIX / Aftermath captures replay identically — required for any buffer that is referenced through bindless or BDA in a debuggable build) and `SHADER_DEVICE_ADDRESS_CAPTURE_REPLAY` (paired flag for buffers whose addresses appear inside other buffers' contents, e.g. SBT records, work-graph node IO). `initial_data` does the right thing per role — direct write on UMA, persistent-map memcpy on native UPLOAD, staging blit on DEVICE — and the future is the ordering edge for any deferred copy or allocation work.
- **`buffer_write(buf, offset, data, size, ordering) → Mel_Gpu_Buffer_Write_Future`** — the **portable streaming primitive**, no map needed. Lowers to `queueWriteBuffer` on WebGPU, persistent-map memcpy on native UPLOAD, staging blit on DEVICE. The returned future / sync edge is what later submissions wait on; a synchronous helper may wait, but the base call never hides an untracked GPU copy behind a `void` return. Debug-asserts a 4-byte-granular convention on `offset` and `size` — WebGPU's `writeBuffer` / `MAP_WRITE` constraint, distinct from binding-alignment constraints (e.g., the 256-byte uniform-buffer binding offset alignment), which are reported through caps limits and validated at bind time.
- **`buffer_map_async(buf, mode, range) → future<Mel_Gpu_Mapped_Buffer>`** — portable for read-back and write staging. `mode = Read | Write`; read-modify-write is an explicit helper built from readback plus upload, not a pretend direct map on backends that cannot provide one. The mapped object carries `ptr`, `size`, `flush`, `invalidate`, and `unmap`. Native upload/readback heaps resolve eagerly with the persistent pointer; WebGPU direct mapping obeys its `MAP_READ` / `MAP_WRITE` usage restrictions and otherwise lowers through staging/readback helpers resolved by the U3 pump.
- **`buffer_persistent_ptr(buf, range) → { ptr, status }`** (UPLOAD / READBACK only) — native zero-cost live pointer. **WebGPU hard-errors** (status severity = `ERROR`); `caps.persistent_map = none`. P1 sync-impossible: the contract cannot be honored synchronously on WebGPU, so do not fake it.
- **`buffer_alloc_transient(size, alignment) → { view, offset, mapped_ptr }`** — per-frame ring slice from U8. **Lifetime is gated on the completion future of the last submission consuming the slice** (§3.3 retirement is future-gated), not on a wall-clock frame boundary — the ring's reset wedge advances only past resolved futures, so async-compute or video-encode work outlasting the graphics frame still keeps the slice live. The ring is the engine-managed convenience over `Mel_Gpu_Memory_Pool` (U8); U8 placed allocations are the P2 peer for power users who want their own lifetime policy. Replaces the current "cycle N buffers manually" pattern.
- **`buffer_destroy(buf)`** — slot-frees; honors `Borrowed` (U5 import), no underlying free.

The buffer's default bindless slot is stored in the slot metadata from U1. It is normally equal to the handle index, but code must ask `buffer_bindless_slot(buf)` if it needs the actual shader-visible slot.

### 6.2 Textures and views (U10)

**`Mel_Gpu_Texture` and `Mel_Gpu_Texture_View` are separate handle types.** The texture owns dimensions, mips, layers, samples, planes, and external-origin metadata; the view subsets it (mip range, layer range, plane, format reinterpret, component mapping, view dimension override). The **view owns the bindless slot**, not the texture, and the §3.1 direct / indirect contract applies: engine-created views are `Mel_Gpu_Texture_View` with `slot == handle.index`; imported and compacted-heap views are `Mel_Gpu_Texture_View_Indirect`. A convenience `texture_default_view(tex)` yields a full-resource view for the trivial case. **View bindless-slot budget is a hard limit** reported by `caps.memory.bindless.max_texture_view_slots`: a heavily-reused texture with N distinct views (format reinterpret / aspect / mip-range subsets) consumes N slots. The engine never silently compacts; if the budget is exceeded, view creation returns a `BindlessSlotExhausted` status and the app either explicitly migrates to indirect views via `texture_view_create_indirect(...)` or rebuilds heap sizing through U4 device-recreate.

**`texture_create`** accepts `resource_kind` (`1D`/`2D`/`3D`), `extent`, `array_layers`, `format`, `mip_levels`, `sample_count`, `usage` (`SAMPLED | STORAGE | ATTACHMENT | COPY_SRC | COPY_DST | TRANSIENT | VIDEO_DECODE_OUTPUT | VIDEO_ENCODE_INPUT | VIDEO_PROCESS_INPUT | VIDEO_PROCESS_OUTPUT | EXTERNAL_CAMERA | EXTERNAL_VIDEO | SAMPLER_FEEDBACK_MIP_REGION | SAMPLER_FEEDBACK_MIN_MIP | SHADING_RATE_SOURCE | FRAGMENT_DENSITY_MAP`), the `memoryless` flag (U8/U22), `cube_compatible` for 2D arrays, `role`, optional `pool`, optional `initial_data`, and creation `flags?` including `CAPTURE_REPLAY` (gated by `caps.debug.capture_replay`; required for any texture that appears in a bindless heap that needs to be RenderDoc / PIX replayable). Views carry their own `view_dimension` (`1D`, `1DArray`, `2D`, `2DArray`, `3D`, `Cube`, `CubeArray`) plus the subresource range they expose; cube is a view over a compatible 2D array, not a distinct resource kind.

**View component mapping is Vulkan-style `Mel_Gpu_Component_Mapping`** with identity default — each of `r`, `g`, `b`, `a` independently selectable from `{ Identity, Zero, One, R, G, B, A }`. Identity is encoded as `{ Identity, Identity, Identity, Identity }` and is always free / no-op-lowered on every backend. Non-identity mapping lowers to `VkComponentMapping` directly on Vulkan, to D3D12 component-mapping bits in the view descriptor (D3D12 supports the same enum precisely; the engine does not synthesize a sampling shader), to Metal `textureSwizzle:` on view-create, and to WebGPU-equivalent format-reinterpret-plus-shader where the backend lacks native swizzle (caps gate via `caps.memory.textures.component_mapping = identity_only | full`).

**Format enum — concrete named set.** Color UNORM/SNORM/UINT/SINT/FLOAT/SRGB variants across R8, RG8, RGBA8, BGRA8, R16, RG16, RGBA16, R32, RG32, RGBA32, plus B5G6R5_UNORM_PACK16, B5G5R5A1_UNORM_PACK16, A2R10G10B10_UNORM, A2B10G10R10_UNORM, R10G10B10A2_UINT, R11G11B10_UFLOAT, R9G9B9E5_UFLOAT_PACK32, RGBA1010102_XR (Metal). Depth/stencil D16_UNORM, D32_FLOAT, D24_UNORM_S8_UINT, D32_FLOAT_S8X24_UINT, S8_UINT. Compressed — BC1/BC2/BC3 (UNORM + SRGB), BC4/BC5 (UNORM + SNORM), BC6H (UFLOAT + SFLOAT), BC7 (UNORM + SRGB); ETC2_R8G8B8/A1/RGBA8 (UNORM + SRGB), EAC_R11/RG11 (UNORM + SNORM); ASTC LDR + HDR for every block from 4×4 through 12×12, both UNORM and SRGB and HDR profiles; PVRTC1/2 where the platform exposes it. Multiplanar / video — `NV12`, `NV21`, `P010`, `P012`, `P016`, `YUY2`, `UYVY`, `Y210`, `Y216`, `Y410`, `Y416`, `AYUV`, plus opaque platform external formats and the per-plane view formats the backend exposes (`G8_B8R8_2PLANE_420_UNORM` style per-plane views on Vulkan, equivalents on D3D12 / Metal). Caps report per-format support tier (sampled/storage/attachment/blend/linear-filter/YCbCr-sampled/video-decode-output/video-encode-input/external-only/sampler-feedback-pair); the user inspects before relying.

**Sampler feedback** (`caps.sampler_feedback = none | tier1 | tier2`). A sampler-feedback texture is created with `SAMPLER_FEEDBACK_MIP_REGION` or `SAMPLER_FEEDBACK_MIN_MIP` usage, paired with the sampled texture at view creation. Shaders write feedback via `WriteSamplerFeedback(...)` (HLSL) / `feedback.write_min_mip(...)` (MSL) / Slang's vendored equivalent. The CPU reads the feedback texture as an ordinary readback to drive virtual-texture page-in decisions. Lowering: D3D12 `D3D12_FEATURE_SAMPLER_FEEDBACK` Tier 1 / Tier 2; Vulkan via `VK_EXT_image_view_min_lod` + driver-extension for write-feedback (gating to `vendor` tier where the cross-vendor KHR is not yet ratified); Metal sparse texture access counters where the cap lowers; WebGPU honestly absent.

**YCbCr and external texture sampling are first-class.** A texture view may carry a color model (`RGB`, `BT.601`, `BT.709`, `BT.2020`), transfer (`linear`, `sRGB`, `PQ`, `HLG`), range (`full`/`limited`), chroma siting, and plane mapping. Shader sampling lowers to Vulkan sampler-YCbCr conversion, Metal/CoreVideo texture caches or native texture views, D3D12 video-process/sample paths, or WebGPU external-texture/import-plus-shader conversion. Camera and video frames are not demoted to ad hoc app-side blits.

**Commands:** copy region (texture↔texture, texture↔buffer), filtered blit, **mip-generation as a built-in** (every engine eventually writes this; just provide it, MEL-ENGINE-II).

**Texture handle is conceptually virtual** — physical memory decoupled from identity. Today every texture has full physical backing by default; the **sparse / partially-resident** capability — Vulkan **1.0 core feature flags** (`sparseBinding`, `sparseResidencyBuffer`, `sparseResidencyImage2D`, `sparseResidencyImage3D`, `sparseResidency2Samples`/`4`/`8`/`16Samples`, `sparseResidencyAliased`) on `VkPhysicalDeviceFeatures` plus the `VK_QUEUE_SPARSE_BINDING_BIT` queue capability, bound via `vkQueueBindSparse`; D3D12 tiled resources; Metal sparse heaps with Metal 4 **placement sparse resources** for fine-grained layout — is added later as purely-additive operations (`texture_bind_pages(tex, region, memory)`) without rewriting the existing API. The factual correction matters: there is no `VK_KHR_sparse_binding` extension. This is the architecture choice, not a deferred implementation — designing for full scope means the model accommodates sparse without retrofit. WebGPU has no sparse — `caps.sparse = none`.

**Import** (U5) wraps external `VkImage`/`MTLTexture`/`WGPUTexture`, `CVPixelBuffer`, `AHardwareBuffer`, DXGI shared resources, and browser/native WebGPU external images as `Borrowed`; views over imported textures and planes are supported the same way; initial state and color metadata are declared at import.

### 6.3 Samplers (U11)

Descriptor: filter (min/mag/mip), wrap (s/t/r), max anisotropy (bounded by `caps.max_anisotropy`), compare op (shadow sampling), border color, lod range, optional YCbCr conversion reference for backends that bind conversion with the sampler. The sampler's default bindless slot is stored in U1 metadata and queried with `sampler_bindless_slot(sampler)`.

**Auto-deduplication.** `sampler_create(desc)` hashes the descriptor and returns a shared handle for any identical request — one internal sampler per unique descriptor across the device. Each create / retain increments the interned object's reference count; each destroy releases one logical claim, and the bindless slot is recycled only after the backend-safe deferred-destroy interval. Costs a small hash map; makes D3D12 sampler-heap pressure a non-issue; mirrors Metal's implicit dedup.

**Static / immutable samplers land in M2** alongside U11 — D3D12 root-signature static samplers and Vulkan immutable samplers save root-DWORDs and sampler-heap descriptors from the first ship, and shadow / cubemap samplers benefit immediately. The pipeline layout (U13) admits a `static_samplers[]` field carrying descriptor + binding; reflection picks up the engine-canonical Slang attribute (`[melody::static_sampler]`) and emits the binding into the layout. Lowering: D3D12 `D3D12_STATIC_SAMPLER_DESC` in the root signature; Vulkan immutable samplers on the descriptor-set / descriptor-heap binding; Metal samplers compiled into the argument table. WebGPU has no baked static-sampler equivalent, so the engine binds an owned `GPUSampler` through an ordinary sampler binding and reports `static_sampler_model = EngineBound` rather than claiming the sampler is free. No sampler import — samplers are trivial descriptors and importing buys nothing.

### 6.4 Shaders (U12)

Slang single-source. Full stage coverage from day one: `VERTEX`, `TESS_CONTROL` (Hull), `TESS_EVALUATION` (Domain), `GEOMETRY` (cap-gated, mostly legacy — kept for porting), `FRAGMENT`, `COMPUTE`, `MESH`, `TASK`, and the ray-tracing set — `RAYGEN`, `MISS`, `CLOSESTHIT`, `ANYHIT`, `INTERSECTION`, `CALLABLE`. **Work-graph node shaders** (D3D12 Work Graphs 1.0 retail on the Agility/SM 6.9 ceiling path; Slang `[Shader("node")]` / `[NodeLaunch(...)]` / NodeIO records via its HLSL-superset path; mesh nodes preview-tier under Work Graphs 1.1 / `lib_6_9`) and the **LinAlg matrix-scope surface** (SM 6.10 preview LinAlg unifying the previously-separate cooperative-vector and wave-matrix tracks into `MatrixScope::Thread | Wave | Group`; Slang shipped cooperative vectors January 2025 targeting `SPV_NV_cooperative_vector` and `dx::linalg::MatVec`, transitioning to the LinAlg surface) are also expressible. Capability-gated where the backend lacks the stage or intrinsic — tessellation gates through `caps.tessellation = none | partial | full`, gated absent on mobile / WebGPU core / pre-Metal-tessellation hardware. Tessellation pipeline state (patch control points, tess factors, partitioning mode, output topology, point mode) lives in U13 alongside the other pipeline state.

**Raw-bytecode passthrough is a first-class peer to the Slang path** (P2). `shader_create_from_bytecode({ target, blob, entry, stage, reflection? }) → Mel_Gpu_Shader_Create_Future` accepts a precompiled blob for the target backend's native form — SPIR-V on Vulkan, DXIL on D3D12, MSL / Metal IR on Metal, WGSL or compiled WGSL on WebGPU. The user supplies reflection metadata directly (or asks the engine to extract it from the blob via the backend's reflection facility — SPIRV-Reflect, DXC reflection, MTLLibrary introspection, WGSL parse). Bundle generation, hot reload, and the U12 `melody.binding` mixin are Slang conveniences over this primitive; an app that hand-rolls its shader pipeline (third-party material editor, AOT-baked variants, shader obfuscation, custom optimizer chains) ships only raw blobs and bypasses Slang entirely. Capability-gated by `caps.shader.bytecode_passthrough.<target>` so the user can introspect what the backend will accept.

**Multi-entry modules.** A single Slang source compiles to a module exposing N entry points; pipeline creation picks `(entry_name, stage)` pairs from it. Idiomatic Slang.

**Per-backend bundle** — single file per backend (the platform TU never carries another backend's blob). Contains the compiled blob plus **reflection metadata** Slang emits.

**Canonical Slang mixin for binding-model duality.** Melody ships a Slang module — `melody.binding` — providing `RootRecord<T>` and `BindlessResource<T>` mixins so one user-authored resource-set declaration synthesizes **both** the **pointer-bearing form** for U14's ceiling (BDA + heap indices) and the **index-bearing form** for U14's floor (per-type descriptor tables + push-constant indices). The mixin uses Slang `__target_switch` over `binding_model = root_record | descriptor_tables` capability atoms; reflection emits both shapes; bundle generation compiles both. The simple path *is* the powerful path further along (MEL-ENGINE-II): the user writes one declaration; U13 selects the lowering at pipeline-create from the granted `caps.binding_model`. User-authored bindless without the mixin is the P2 peer for those who want their own root-record layout.

**Reflection-driven pipeline layouts** are the default — every binding's set/slot/type, vertex input layout, push-constant size, specialization-constant layout. Pipeline creation in U13 derives the layout from the bundle's reflection automatically; the shader IS the source of truth for its bindings. A manual layout declaration is available as P2 escape (no hiding).

One Slang authoring detail the spec acknowledges (current Slang limitation, tracked as Slang issue #9643 "Overlapping push constant ranges in SPIR-V target," open as of 2026-01-16): `[shader("...")]` entry-point parameters map to push constants starting at offset 0 per stage, but Vulkan shares one push-constant range across all stages in a pipeline, and Slang does not currently expose offset control per entry-point parameter. The supportable pattern is a **global-scope `[[vk::push_constant]]` struct** that all stages reference; reflection picks it up and U13 lays it out as one shared range. Bundle generation in U12 validates that per-entry push-constant uses are reconcilable and emits an honest error otherwise. This is what U14's per-draw root-record carrier rides on.

**Capability-system coordination with Slang.** Slang ships its own capability system — `[require(...)]`, `__target_switch`, `CapabilityAtom`, hierarchical atoms with inheritance (e.g., `_spirv_1_5 : _spirv_1_4`), and aliases (`alias raytracing = glsl + GL_EXT_ray_tracing | spirv + SPV_KHR_ray_tracing | hlsl + _sm_6_4`) — and selects overloads by capability subset at IR link time. This overlaps directly with U4 / U25. The engine **wraps** Slang's capability system rather than duplicating it: U4's request-and-grant set is translated to `-capability` flags on the Slang compile, Slang's link-time capability errors surface as the bundle-generation gate, and Slang reflection records the feature mask required by each entry point so U13 can fail pipeline creation early when the granted device lacks the capability. Caveat: Slang's documentation flags `__target_switch` and friends as "internal compiler features… subject to breaking changes" (see Slang issue #9210); the engine treats the Slang capability surface as a versioned vendored dependency, not a stable public ABI, and pins to a known-good Slang release per Melody version.

**Slang version is part of the bundle key and the `pipeline_binary` cache key.** Each Melody version pins one Slang release (vendored at `tools/build/vendor/slang`); the version string ends up in every emitted bundle's container header and contributes to the cache fingerprint described in §6.5. A Slang upgrade therefore invalidates the persisted pipeline-binary cache deterministically rather than silently producing miscompiled shaders. Major Slang regressions that block a Melody release path are handled by pinning back; the `tools/build/vendor/slang` directory carries a `SLANG_VERSION.lock` file with the upstream commit hash and a `MIGRATION.md` of any source-level deltas the engine consumes from one pin to the next.

**Bundle-load capability check.** Every compiled bundle records the union of `[require(...)]` capability atoms across all entry points alongside per-entry masks. Bundle load validates the container, target metadata, and reflection schema. Per-entry capability checks happen when an entry or pipeline is selected, so one unsupported entry does not block other entries in the same bundle. Tools and strict startup paths can opt into `require_all_entries_supported`; that eager check is one set-difference operation and the failure status carries the missing atoms verbatim (`requires _spirv_1_5, raytracing_pipeline; granted _spirv_1_4`) instead of an opaque pipeline-create failure later.

**Specialization constants first-class.** Slang `[specialization_constant]` lowers to Vulkan specialization constants / Metal function constants / WGSL `override` declarations — uniform API for baking compile-time variants without re-running Slang.

**Async creation (U3).** `shader_create(...) → future<shader>` — eagerly resolved for offline bundles (just bytes), genuinely async for runtime Slang compile (dispatched to the job system, completion bridged to the target reactor).

**Runtime compiler is app-opt-in at build time** — not Melody-dev-only. The app developer chooses what to ship: offline-only (no Slang runtime linked, `caps.runtime_shader_compile = false`), runtime-capable (Slang runtime bundled, caps true), or both (offline default with runtime fallback for generated/mod/AI-authored shaders). MEL-ENGINE-III (don't link what you don't need) + MEL-ENGINE-X (engine serves the user's vision).

**Hot reload** rides the runtime compile path plus the engine's file-watcher.

### 6.4.1 Shader model, compute, and ML/tensor caps (U25)

Compute is not treated as "rendering without color attachments." It has its own caps block and first-class ergonomics because Melody targets applications, tooling, media, simulation, visualization, and ML-shaped workloads as much as games.

**Shader/subgroup surface.** Caps expose subgroup/wave size range, supported stages, quad operations, vote/ballot/shuffle/arithmetic/clustered ops, derivative support in compute (Vulkan `VK_KHR_compute_shader_derivatives`, Roadmap 2026 mandatory), memory scopes, memory semantics, atomics by type, barycentrics, clip/cull distances, dual-source blending, interpolation controls, shader clock (`VK_KHR_shader_clock`), explicit workgroup-memory layout (`VK_KHR_workgroup_memory_explicit_layout`), and untyped pointers (`VK_KHR_shader_untyped_pointers`). Slang reflection records the feature mask required by each entry point; pipeline creation fails early when an entry requires a feature the granted device lacks.

**Atomic operations are enumerated per-type per-storage**, not collapsed into a single bool. Caps expose `atomic_int32_buffer`, `atomic_int64_buffer` (`VK_KHR_shader_atomic_int64`, SM 6.0+), `atomic_int32_image`, `atomic_int64_image` (`VK_EXT_shader_image_atomic_int64`, mandatory carrier for software-rasterization-into-storage and GPU-driven shading techniques), `atomic_int32_shared`, `atomic_int64_shared`, `atomic_float32_*` (`VK_EXT_shader_atomic_float` add / load / store / exchange / min / max per storage class), `atomic_float64_*`, `atomic_float16_*` (`VK_EXT_shader_atomic_float2` — used in HW BVH builds, neural training inner loops). Slang reflection records which atomic-by-type the shader needs; pipeline creation fails when the granted device lacks it. No silent promotion.

**Numeric surface.** Caps expose `fp64`, `fp32`, `fp16`, `bf16`, `fp8` (E4M3, E5M2), signed/unsigned `int64`/`int32`/`int16`/`int8`/`int4`, packed dot products (`int8` and `int16` cross-vendor), saturation modes, denormal/rounding behavior where the backend reports it. **ML acceleration is unified under `caps.matrix_scope ∈ { none | thread | wave | group | full }`** — the SM 6.10 LinAlg axis, which subsumes the previously-separate cooperative-vector and wave-matrix tracks into one orthogonal surface. The matmul-profile descriptor (below) resolves into a matrix scope and an element-type pair. Underlying cross-vendor extensions stay named so power users can branch: `VK_KHR_cooperative_matrix` is KHR (Roadmap 2026 mandatory); `VK_NV_cooperative_vector` and `VK_NV_cooperative_matrix2` are **NV-only**; `VK_ARM_tensors` (1.4.317) supplies tensor aliasing on ARM. D3D12 surfaces them through SM 6.9 (Long Vectors, mandatory 16-/64-bit + wave ops) and SM 6.10 preview **LinAlg** (the unified replacement, Microsoft preview April 2026, retail expected late 2026). Metal 4 surfaces them through `MTLTensor` (first-class resource peer to buffer and texture, with MSL 4 cooperative-tensor reductions and `Convolution2d` / `matmul2d_descriptor` Metal Performance Primitives) and **Shader ML** (inline small networks in fragment/vertex/compute, no encoder round-trip). No precision is silently promoted or demoted. If a shader asks for `fp16` or `int8` math and the backend cannot honor it, creation fails instead of compiling an accidentally slower or less precise program.

**Dispatch surface.** Workgroup size, **subgroup-size control** (`VK_EXT_subgroup_size_control`, core 1.3 — pin a required subgroup size for the compute pipeline so ML kernels and cooperative-matrix shaders get the wave-width they were tuned for, with `caps.subgroup_size_control = { min, max, supported_stages }`), shared-memory size, dispatch dimension limits, indirect dispatch, indirect-count dispatch (U15 `cmd_dispatch_indirect_count`), device-address availability, device-side copy/fill (Vulkan `VK_KHR_copy_memory_indirect`, Roadmap 2026 mandatory), memory decompression, and async-compute overlap are all explicit. `cmd_dispatch` remains the universal primitive; specialized helpers (`cmd_dispatch_tensor`, `cmd_dispatch_cooperative_matrix` if they exist) are conveniences over the same shader/caps contract, not separate compute worlds.

**Matmul profile.** `Mel_Gpu_Matmul_Profile { element_type, accum_type, m, n, k, layout_a, layout_b, scope_hint? }` is a declarative descriptor that resolves at pipeline-create to a `{ matrix_scope, extension }` pair. The scope axis is the SM 6.10 LinAlg taxonomy: `thread` (per-thread MatVec, what the previous-gen `cooperative_vector` proposal called itself), `wave` (wave-uniform matmul, the cooperative-matrix shape, and Metal MPS wave-scope reductions), or `group` (workgroup-scope matmul; D3D12 SM 6.10 group-scope LinAlg + Metal Shader ML group ops + Vulkan `VK_NV_cooperative_matrix2`). The resolution prefers `group` → `wave` → `thread` → `subgroup_arithmetic` (shader-emulated last-resort) by default; `scope_hint` lets the user pin a scope. Returned in the U2 status so the user *can* introspect and branch when the cost differential matters; the default path does not require it. Without this descriptor the engine forces the user to write three lowerings by hand against three vendor extensions ※ which is the duplication MEL-ENGINE-II forbids.

**Lowering.** Vulkan uses subgroups, shader float/int controls (`VK_KHR_shader_float_controls2`, core 1.4), integer-dot-product, `VK_KHR_cooperative_matrix` plus the NV-only `VK_NV_cooperative_vector` / `VK_NV_cooperative_matrix2` and ARM `VK_ARM_tensors` where present. D3D12 uses the highest granted shader-model wave ops; cooperative-vector and wave-matrix are reached through SM 6.10 **LinAlg** (preview), not SM 6.9. Metal uses the highest available MSL 4 features plus `MTLTensor` resources, Shader ML inline networks, and `MTL4MachineLearningCommandEncoder` for full network dispatch. WebGPU exposes only its granted optional features (`shader-f16`, `subgroups`, `subgroup_uniformity`, `dual-source-blending`, `float32-filterable`, `float32-blendable`, `clip-distances`, and friends) and reports the rest as absent or shader-emulated.

### 6.5 Pipelines (U13)

**Per-type create**, distinct state spaces: `pipeline_graphics_create`, `pipeline_compute_create`, `pipeline_mesh_create`, `pipeline_tess_create` (vertex + tess-control + tess-evaluation + fragment; cap-gated by `caps.tessellation`), `pipeline_rt_create`, `pipeline_work_graph_create` (D3D12 work graphs; cap-gated). No unifying tagged descriptor.

**Full state coverage.** Vertex input layout (reflection default, manual override); topology (including patch-list topologies for tessellation pipelines); full rasterization (cull, fill, front-face, depth bias, conservative raster cap-gated); full depth/stencil (per-face stencil ops, depth bounds); **per-attachment blend** (separate color/alpha, full factor/op enumeration, per-attachment color write mask). **Tessellation state** (patch control point count, primitive partition mode ∈ `{ Integer, FractionalEven, FractionalOdd, Pow2 }`, output topology ∈ `{ Point, Line, TriangleCW, TriangleCCW }`, point-mode flag, max tessellation factor) lives on `pipeline_tess_create`. **Pipeline robustness selector** `robustness ∈ { default | disabled | enabled | per_stage }` (cap-gated by `caps.pipeline_robustness`; lowers to `VK_EXT_pipeline_robustness` per-pipeline, D3D12 root-signature robustness, Metal advisory) — apps that need predictable performance opt out of robustness on hot pipelines and keep it on cold / user-content-driven pipelines. No current single-blend-state shortcuts.

**Maximize dynamic state by default** where the backend supports it (`VK_EXT_extended_dynamic_state{1,2,3}` or the promoted Vulkan 1.4 path; Metal encoder state; WebGPU fewer). Viewport, scissor, blend constants, stencil ref, depth bias, primitive topology, cull mode, front face, depth-test/write, depth-compare, stencil-test/op, **VRS shading-rate + combiner ops** (U16), **sample positions** via `VK_EXT_sample_locations` (programmable MSAA sample positions for TAA-stable jitter; cap `sample_locations = none | per_pipeline | per_subpass`), **sample shading enable + min-sample-shading factor**, and **logic-op state** — all dynamic where possible. The practical lever against pipeline-permutation explosion. Caps report what is actually dynamic.

**Niche pipeline features admitted from day one (cap-gated).** Conservative rasterization (`VK_EXT_conservative_rasterization`), `VK_EXT_shader_stencil_export`, programmable interpolation controls, dual-source blending (when granted), depth-bias-representation, depth-clip enable / depth-clamp-zero-one (`VK_KHR_depth_clamp_zero_one`, Roadmap 2026 mandatory). Each appears in U13 descriptors as cap-gated fields; granted backends consume them, ungranted backends refuse with `MissingFeature`. No deferral (MEL-ENGINE-I).

**Pipeline-less lane via `VK_EXT_shader_object`.** The endgame of "maximize dynamic state" on Vulkan is the no-`VkPipeline` path — `VK_EXT_shader_object` lets the engine bind shader stages individually with every relevant state dynamic, eliminating pipeline-permutation explosion altogether for engine-managed renderers. The RHI exposes it as a sibling creation path (`pipeline_graphics_create_shader_objects`, `pipeline_compute_create_shader_object`) gated by `caps.shader_object = none | partial | full`; the API shape is the same `pipeline_*_create` family so user code switches with no rewrite. On runtimes lacking the extension the ceiling collapses to maximized dynamic state on a real `VkPipeline`; on D3D12 the analog is dynamic-state heavy PSOs; on Metal 4 the analog is the rewritten encoder model with dynamic state already prevalent; on WebGPU honestly absent.

**Async creation** (U3). Metal 4 `MTL4Compiler` with `makeLibrary(descriptor:)` and async pipeline-state creation as an explicit CPU-side compile object (the Metal 2-era `newRenderPipelineStateWithDescriptor:completionHandler:` remains the fallback on pre-Metal-4 runtimes); WebGPU `createRenderPipelineAsync`; Vulkan dispatched to a job-system compile pool, with the U3 pump bridging completion regardless of source; D3D12 `CreatePipelineState` / pipeline-library work runs on the same compile pool. **Background compilation is how production avoids frame hitches**, free through the U3 spine.

**Mesh, RT, and work-graph pipeline descriptors exist from day one** (cap-gated). Their backend implementations land with the M3 feature milestone; until then the relevant caps are false and creation fails cleanly without forcing users to port to a new API surface later. RT pipelines take raygen + miss[] + hit groups + callable[] plus recursion depth and SBT layout; the **shader binding table is engine-managed** (`Mel_Gpu_Sbt`, engine handles alignment and stride), with a raw-handle escape (P2) for users hand-rolling the buffer. SBT layout rules are fiddly and getting them wrong is silent miscompile (MEL-ENGINE-VIII). **Work-graph pipelines** (D3D12 baseline at this target) live alongside.

**Specialization constants** supplied at creation (U12).

**Fragmented compile via Graphics Pipeline Libraries.** `VK_EXT_graphics_pipeline_library` (GPL, ratified 2022, broadly granted on NV / AMD / Intel / Mali) composes a pipeline from precompiled fragments — `VERTEX_INPUT_INTERFACE`, `PRE_RASTERIZATION_SHADERS`, `FRAGMENT_SHADER`, `FRAGMENT_OUTPUT_INTERFACE` — with cheap link-time stitching. The RHI exposes it as `pipeline_graphics_create_library(stage_mask, …)` plus `pipeline_graphics_link({ vertex_input, pre_raster, fragment, fragment_output })`, gated by `caps.pipeline_library = none | partial | full`. This is the primary antidote to runtime pipeline-creation hitches on Vulkan today: `pipeline_binary` below only helps when the binary is already cached; GPL lets cold-cache builds compile only the missing fragment and link the rest in microseconds. D3D12's SM 6.6+ DXIL library composition is the analog; Metal 4 `MTL4Compiler` admits unspecialized→specialized reuse along the same axis. Lowering: granted Vulkan uses GPL; D3D12 uses DXIL libraries + `ID3D12RootSignature` shared across PSOs; Metal 4 uses `MTL4Compiler.makeLibrary(descriptor:)` over Metal IR fragments; WebGPU honestly absent (whole-pipeline compile is the only path). On Vulkan without GPL granted the ceiling collapses to whole-pipeline compile through `VK_EXT_shader_object` (when granted) or a real `VkPipeline`.

**Pipeline persistence — modern path is `VK_KHR_pipeline_binary`; `VkPipelineCache` is the fallback.** `VK_KHR_pipeline_binary` (Roadmap 2026 mandatory) is designed precisely to side-step the global-blob quirks of `VkPipelineCache`: per-pipeline binaries keyed by app-managed keys, no monolithic blob, no UUID-versus-driver-fingerprint paranoia, no atomicity hazards. The engine-managed primary path uses `pipeline_binary` where granted: each pipeline serialized to one binary, keyed by **(descriptor hash, Slang bundle version, granted-caps fingerprint, specialization-constant values, pipeline-library link plan when GPL is in use)**, stored in a content-addressed on-disk cache, looked up at pipeline creation, persisted atomically per binary. Specialization-constant values **are** part of the key (different `[specialization_constant]` values produce different binaries); a key omitting them would collide silently across variants. On runtimes lacking `pipeline_binary` the engine falls back to `VkPipelineCache` with the defensive-load discipline below. The same surface also admits backend-native binary archives where they exist (D3D12 pipeline libraries / state streams; Metal binary archives via `MTL4Compiler` serialization). The app override is **not a path/disable toggle** — it is a full set of peer primitives: `pipeline_cache_create`, `pipeline_binary_create`, `pipeline_binary_serialize`, `pipeline_binary_load`, `pipeline_binary_get_key`, `device_set_pipeline_cache`, `pipeline_cache_merge`, `_export_binary`, `_import_binary`. The app can persist anywhere (custom storage, encryption, network), implement its own key strategy, replicate the engine's behavior — 100% of the functionality is exposed. WebGPU has no cache primitive (P1 emulate-as-noop).

**Defensive load is mandatory on the `VkPipelineCache` fallback.** Real Vulkan drivers misbehave on malformed pipeline-cache blobs: some crash on `pInitialData != NULL && initialDataSize == 0`; some load blobs from a 32-bit build into a 64-bit build despite matching `pipelineCacheUUID` (because vendors update the UUID manually and forget); some return `VK_ERROR_INITIALIZATION_FAILED` on any mismatch rather than discarding silently as the spec promises. The fallback engine-managed cache therefore writes its own header — `magic` + `dataSize` + `dataHash` + `vendorID` + `deviceID` + `driverVersion` + `sizeof(void*)` + `pipelineCacheUUID` — persists via temp-file-plus-`rename` for atomicity, validates every header field on load, treats any validation failure or driver error as a cache miss, and falls through to an empty cache rather than propagating the error. The paranoia is the *fallback* discipline; on `pipeline_binary`-supporting runtimes the per-binary keying obviates it. The P2 primitives expose the same defensive load for app-managed caches; the spec calls it out explicitly so reimplementations do not skip it.

### 6.6 Acceleration structures and shader binding tables (U26)

Ray tracing is not complete without acceleration-structure resources. `Mel_Gpu_Accel_Struct` is a typed handle following the §3.1 direct / indirect contract — engine-created is direct; imports use `Mel_Gpu_Accel_Struct_Indirect`. Types: **bottom-level** (BLAS), **top-level** (TLAS), **cluster-level** (CLAS — `Mel_Gpu_Cluster_Accel_Struct`, the new resource at the leaf of the hierarchy that groups triangles by spatial locality, up to the backend-reported per-cluster triangle / vertex maxima), and **partitioned top-level** (`Mel_Gpu_Partitioned_Tlas` — the incremental-update TLAS shape that maintains internal partitions so only affected partitions rebuild when their instances change).

**Build workflow.** `accel_struct_get_build_sizes(desc)` reports result/scratch/update sizes; `accel_struct_create({ type, size, role, pool?, flags })` allocates backing; recording exposes `cmd_build_accel_struct`, `cmd_update_accel_struct`, `cmd_copy_accel_struct`, `cmd_compact_accel_struct`, `cmd_serialize_accel_struct`, `cmd_deserialize_accel_struct`, and `cmd_trace_rays_indirect` / `cmd_trace_rays_indirect2` (the latter with dispatch dims read entirely from GPU memory — Vulkan `VK_KHR_ray_tracing_maintenance1` `vkCmdTraceRaysIndirect2KHR`, D3D12 DXR 1.1 indirect dispatch from compute). Build inputs accept vertex/index/AABB buffers, transform buffers, instance buffers, opacity micromaps, displacement/motion data, procedural intersection records, and **per-vertex position fetch** (`VK_KHR_ray_tracing_position_fetch` / D3D12 `TriangleObjectPositions`, SM 6.9 mandatory) as capability-gated descriptors. **Ray-tracing motion blur** (instance and transform interpolation) is admitted through `VK_NV_ray_tracing_motion_blur` lowering on Vulkan plus the matching D3D12 motion-instance flags, capability-gated.

**Cluster acceleration structures (CLAS) and partitioned TLAS.** A CLAS contains a fixed-upper-limit group of triangles (typically 128 triangles / 128 vertices, exact bound reported in `caps.ray_tracing.cluster_max_triangles` / `_max_vertices`), spatially grouped so axis-aligned bounding boxes minimize overlap. CLAS references compose into a BLAS via `cmd_build_blas_from_clusters(blas, clusters[], flags)`, enabling the Nanite-style cluster-LOD streaming and view-dependent tessellation paths. A partitioned TLAS replaces full TLAS rebuilds with instance-write / instance-update / partition-translation-write operations: `cmd_partitioned_tlas_update(ptlas, ops[])` mutates only the partitions whose instances changed, and the unchanged partitions reuse their prior internal BVH. **Shader access**: cluster ID is reachable via Slang's vendored equivalent of `gl_ClusterIDNV` / HLSL `ClusterID()` (SM 6.9), with reflection requiring `[require(cluster_acceleration_structure)]` on the RT pipeline so U13 fails creation early when the granted device cannot honor it (including the emulated tier below). Lowering: Vulkan via `VK_NV_cluster_acceleration_structure` + `VK_NV_partitioned_acceleration_structure` (NV-vendor at spec time; the engine carries the cap tier `vendor` and a future-additive path to KHR when ratification lands), D3D12 via the SM 6.9 + Agility 1.619.3 cluster intrinsics and the new D3D12 cluster-build APIs (cross-vendor on NVIDIA RTX, AMD RDNA 4, Intel Arc B-series), Metal honestly absent (`cluster_accel_struct = none`), WebGPU absent.

**Cluster-LOD fallback on `cluster_accel_struct = emulated`** (symmetric to U14's `descriptor_heap → descriptor_buffer → descriptor_indexing` ladder). Where hardware cluster acceleration is missing but the user's renderer wants the cluster-LOD pattern — AMD pre-RDNA 4 Vulkan, Intel Arc pre-B-series Vulkan, every non-NV Vulkan device pre-KHR-ratification, Apple Silicon — the cluster-build commands remain functional and lower behavior-faithfully: `cmd_build_cluster_accel_struct` emits a scratch record of the cluster's triangles plus a cluster→primitive-ID-range mapping; `cmd_build_blas_from_clusters` produces a *standard* `Mel_Gpu_Accel_Struct` BLAS where each cluster's triangles occupy a contiguous primitive-ID range (the per-cluster `firstPrimitive` + `clusterPrimitiveStride` are recorded on the BLAS metadata); Slang's `ClusterID()` builtin lowers to `gl_PrimitiveID / clusterPrimitiveStride` instead of the HW intrinsic, gated by the engine-registered Slang capability atom `cluster_accel_struct_emulated`; `cmd_partitioned_tlas_update` falls back to a full TLAS rebuild driven by the partition's instance set. Rays hit the same triangles and the shader sees the same cluster ID (P1 fidelity); incremental BLAS sharing across CLAS rebuilds and the partitioned-update savings collapse to full rebuilds (P1 honesty — `caps.cluster_accel_struct = emulated` reports the cost so power users can branch, e.g., use a coarser cluster size when emulated to recoup some of the rebuild cost). The `none` tier remains the honest gate: backends without RT at all (`ray_tracing = none`), or runtimes where the user's RT pipeline cannot honor the cluster-AS contract even via emulation, refuse cluster creation with `MissingFeature` rather than silently degrading further (MEL-ENGINE-VIII).

**Compaction protocol — new handle, explicit reference update.** `cmd_compact_accel_struct(src, dst)` writes the compacted form into a freshly-allocated `dst` handle (sized from `cmd_write_acceleration_structures_properties` post-build); it does **not** swap the source in place. TLAS instances that point at the source BLAS continue to point at the source until the user issues a TLAS rebuild or update with the new handle's device address — the engine refuses to mutate TLAS instance data behind the user's back (MEL-ENGINE-VIII, no silent substitution). The honest cost: the user runs build → query compacted size → allocate dst → compact → rebuild TLAS pointing at dst → destroy src once the rebuild's completion future resolves (§3.3 future-gated retire). The alternative — engine swaps the slot's backing in place — was rejected because it forces the engine to re-emit TLAS-touched barriers and silently mutate user state.

**State and memory integration.** AS scratch/result/storage buffers use U9 usage flags and U17 states (`AccelStructBuildRead`, `AccelStructBuildWrite`, `RayTracingAccelStruct`). The render graph sees AS access records exactly like resource access records; it can schedule AS builds, insert barriers, and alias scratch memory where lifetimes permit.

**SBT stays paired but separate.** `Mel_Gpu_Sbt` remains the ergonomic shader-binding-table helper from U13, with raw-buffer escape. The helper handles backend alignment, group handles, stride, and table-region layout; power users can hand-build SBT buffers with `SBT | SHADER_DEVICE_ADDRESS` usage.

**SBT lifecycle is bound to the source RT pipeline.** When the source `pipeline_rt` is replaced (hot reload, specialization-constant change, hit-group set change), the engine **regenerates the SBT atomically** as a new `Mel_Gpu_Sbt` handle and resolves a `pipeline_replaced` future; the prior SBT remains live until its consumers' completion futures resolve (§3.3 future-gated retire). Raw-buffer SBT users carry their own invalidation contract — the engine reports the new group handles through the `pipeline_rt_get_group_handles(pipeline, range) → bytes` query and the user rewrites their buffer. Silently rewriting a raw SBT under the user would violate MEL-ENGINE-VIII.

**Lowering.** Vulkan maps to `VK_KHR_acceleration_structure` / `VK_KHR_ray_tracing_pipeline` plus `VK_EXT_opacity_micromap` and `VK_EXT_ray_tracing_invocation_reorder` (the multi-vendor cross-vendor SER extension announced 18 November 2025, superseding `VK_NV_ray_tracing_invocation_reorder` — there is no `VK_KHR_ray_tracing_invocation_reorder`) where granted. D3D12 maps to DXR 1.2 acceleration structures, compaction, serialization, SER/`HitObject` (required by SM 6.9), and opacity micromaps. Metal maps to Metal acceleration structures and intersection-function tables, with MSL 4 **intersection-function buffers** as the modern SBT lowering on Metal 4. WebGPU has no core RT/AS path and reports `ray_tracing = none`.

### 6.7 Resource binding (U14)

The binding model is **two layers, ceiling and floor**, both first-class. Caps report which is active; the API surface is the same; only the lowering differs. The ceiling is the modern pointer-based model (Aaltonen / Gibson Loon shape); the floor is the classical descriptor-table model that WebGPU and constrained runtimes can still honor.

**Ceiling — root record plus texture / sampler / AS heaps.** The shader receives one root record per draw or dispatch. Pointer-capable backends may pass that record as a 64-bit address to a user-defined struct in GPU memory; descriptor-index backends pass a compact record containing heap / table indices. The record holds whatever the draw needs: buffer data references, texture indices, sampler indices, and top-level acceleration-structure indices. On Vulkan / Metal pointer lanes, buffer data can be a real device address. On D3D12, buffers are addressed through CBV / SRV / UAV descriptors in `ResourceDescriptorHeap`; the root record carries descriptor indices, not arbitrary shader-dereferenced GPU virtual addresses. The CPU may write the record through a persistently-mapped upload buffer, allocate from a transient upload ring and copy/stage, or a compute/work-graph pass may write it directly for GPU-driven rendering. The CPU side becomes "produce a record and pass its carrier"; the GPU side gets persistent slots and no descriptor-set copies.

The ceiling requires `caps.bindless = full` and a backend-specific root-record carrier. **The carrier per backend is pinned**: Vulkan delivers a root-record address or index through a reflected push-constant range; D3D12 uses a **root CBV** or root constants at a fixed root parameter index containing descriptor heap indices; Metal 4 uses one `MTL4ArgumentTable` slot carrying the argument table / buffer reference. The size, alignment, and binding location of the carrier are reflected through U12 onto the pipeline layout so the user never picks them by hand. On Vulkan ceiling backends without `VK_EXT_descriptor_heap` granted, the carrier still rides push constants but the heap-index resolution path differs (`descriptor_buffer` offsets vs. `descriptor_indexing` set bindings); the user-visible resource schema is unchanged. `caps.root_record_payload = pointers | descriptor_indices | mixed` reports whether buffer fields are real addresses, descriptor indices, or a combination; `caps.root_record_update = persistent_map | upload_ring | staging_copy | gpu_generated` reports how root records may be produced. The ceiling backends are **Vulkan with `VK_EXT_descriptor_heap`** (23 January 2026 with Vulkan 1.4.340 / Roadmap 2026; intended replacement for `VK_EXT_descriptor_buffer` and the legacy `VkDescriptorSetLayout` / `VkPipelineLayout` machinery, DX12-style two-heap split — one resource heap, one sampler heap — requiring `VK_KHR_buffer_device_address` or Vulkan 1.2 plus `VK_KHR_shader_untyped_pointers`), with `VK_EXT_descriptor_buffer` as the transitional path where `descriptor_heap` is not yet granted, plus `buffer_device_address` (core 1.2, mature in 1.4); D3D12 SM 6.6+ with `ResourceDescriptorHeap` / `SamplerDescriptorHeap` direct indexing plus a root-record carrier; Metal 4 with `MTL4ArgumentTable` carrying pointer-bearing or table-indexed argument-buffer references plus integrated `MTLResidencySet`. The previous-generation Vulkan ceiling — `buffer_device_address` + `VK_EXT_descriptor_indexing` with `UPDATE_AFTER_BIND` — remains a supported lowering for runtimes that haven't reached `descriptor_buffer`/`descriptor_heap`.

**Floor — per-type descriptor tables, with optional mutable descriptors.** Where the ceiling caps are absent (WebGPU, older runtimes lacking BDA, direct heap indexing, or another root-address carrier), the same RHI surface lowers to **per-resource-class descriptor tables** — one for texture views, one for samplers, one for storage buffers, one for uniform buffers, one for acceleration structures, plus optional app-defined tables for classic descriptor-set layouts. Direct handles (§3.1) guarantee table-index = handle-index by allocator contract: the table slot is reserved at the slotmap's index at creation time and never relocates. Indirect handles store their table index separately and resolve via `*_bindless_slot`. Per-draw indices ride **push constants** on Vulkan/Metal/D3D12, or a tiny uniform buffer with dynamic offset on WebGPU. This is the classical bindless-tables model — still first-class on the floor, never a degraded fallback.

**Mutable descriptor types** (`caps.mutable_descriptor_type = none | partial | full`) collapse the per-class tables when granted. `VK_EXT_mutable_descriptor_type` (KHR ratification pending; broadly granted on NV / AMD / Intel today) lets one descriptor slot accept any of N declared resource kinds — texture-view ∪ storage-image ∪ uniform-texel-buffer ∪ storage-buffer ∪ … — making the floor's descriptor pressure 4-5× more compact on hardware that supports it. The engine exposes a `Mel_Gpu_Mutable_Descriptor_Layout` admitting the kind union explicitly; pipeline layouts that take a mutable layout get one descriptor table sized to the union rather than N parallel per-class tables. On `mutable_descriptor_type = none` backends (WebGPU, older runtimes) the same source-level layout falls back to N per-class tables transparently.

**One source for both layers.** Slang authors the per-draw struct in either form — pointer-bearing where the backend can use real device addresses, index-bearing where descriptors are the portable currency; reflection (U12) tells U13 which lowering applies. The pipeline layout reflects either a root-record carrier plus heap declarations, or the descriptor-table layout. The user declares "this draw consumes this resource set" the same way. `caps.binding_model = root_record | descriptor_tables` and `caps.root_record_payload = pointers | descriptor_indices | mixed` are queryable so a hand-tuned variant can be written, but the simple path does not require it. Persistent slots remain: a resource stays at its pointer-or-index until destroyed; pipelines reference slots dynamically; no per-frame rebinding ceremony.

**Heaps as resources, with P2 introspection.** The texture / sampler / AS heaps are themselves resources the user can introspect — query the heap, read or write descriptor entries, swap entries. Full reimplementability per P2. Heap creation admits a `CAPTURE_REPLAY` flag (`caps.debug.capture_replay`) that opts the heap and its memory into the `descriptorBufferCaptureReplay` (Vulkan) / `D3D12_HEAP_FLAG_TOOLS_USE_MANUAL_WRITE_TRACKING` (D3D12) discipline so RenderDoc / PIX / Aftermath can replay the bindless surface deterministically. Resources allocated into a capture-replay heap must themselves carry the resource-level `CAPTURE_REPLAY` flag from §6.1 / §6.2 / §6.6; engine validation enforces the pairing.

**Heap write visibility to in-flight submissions.** Descriptor-heap writes (both engine-managed and P2-direct) follow update-after-bind semantics on Vulkan (`VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT` + `DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT`), `D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE` on D3D12, and Metal 4 `MTL4ArgumentTable` slot writes (always visible). The contract: **writing a heap slot that an in-flight submission is sampling-from is undefined**; the engine never writes a live slot — destroy-then-rewrite is gated on the consumer's completion future (§3.3 retire). P2-direct writers carry the same contract; the engine cannot police user writes past the hatch.

**Pipeline-create distinguishes `MissingBindlessSlot` from `MissingFeature`.** A pipeline whose reflection demands heap slot N when the layout's heap is sized < N+1 fails create with `MissingBindlessSlot{ heap, slot }`, distinct from `MissingFeature{ atoms }` (granted caps lack a required Slang capability atom). The two failure modes have different remedies — grow the heap vs. request more capabilities — and conflating them in one enum value is exactly the diagnostic dilution MEL-ENGINE-VIII forbids.

**Lowering, in detail.** Vulkan ceiling: BDA buffers + `VK_EXT_descriptor_heap` resource/sampler heaps (or `VK_EXT_descriptor_buffer` transitionally; or `VK_EXT_descriptor_indexing` with `UPDATE_AFTER_BIND` on the earlier ceiling) for the heaps. Vulkan floor (no BDA or no descriptor-indexing): classical descriptor-set bind groups. D3D12 ceiling: `ResourceDescriptorHeap` / `SamplerDescriptorHeap` direct indexing + descriptor-index root record; buffer access still goes through CBV / SRV / UAV descriptors. D3D12 floor: classical descriptor heaps with root-signature tables. Metal ceiling: `MTL4ArgumentTable` with pointer-bearing or table-indexed argument buffers + integrated `MTLResidencySet`. Metal floor: classical argument buffers / per-stage buffer/texture slots on earlier runtimes. WebGPU: floor only today; bindless via `GPUResourceTable` is at Milestone 3 (January/February 2026 GPU Web meetings) with `sized binding arrays` as the shipping stepping-stone, so U14 designs the WebGPU floor forward-compatibly with `GPUResourceTable` rather than committing only to sized-array semantics. Capped bind groups with `caps.bindless = capped` and the actual cap reported. Slot allocation past the WebGPU cap **hard-errors** (P1 sync-impossible refinement) rather than faking capacity.

**Engine-managed lifecycle, classic descriptor sets as P2 peer.** Resource creation auto-populates global heaps / tables and per-resource descriptors, then records the resource's persistent slot in handle metadata. Root records are produced by the renderer at draw / dispatch recording time or by explicit GPU-driven setup passes; resource creation cannot implicitly fill every future root record. Destruction frees the slot after the backend-safe deferred-destroy interval. A user who wants explicit classic descriptor sets / bind groups declares a non-bindless pipeline layout, creates descriptor sets / bind groups, binds via `cmd_bind_descriptor_set` / `cmd_bind_bind_group`. The classic path is a peer, not a degraded fallback.

Static / immutable samplers from U11 are part of the M2 pipeline-layout admission, not deferred — `pipeline_layout.static_samplers[]` carries the descriptor + binding from day one (D3D12 root-signature `D3D12_STATIC_SAMPLER_DESC`, Vulkan immutable samplers, Metal arg-table baked, WebGPU engine-bound ordinary sampler binding).

### 6.8 Asset IO and GPU decompression (U29)

GPU work is fed by *bytes from disk* as often as by compute output. The platforms have built explicit asset-IO surfaces — D3D12 **DirectStorage** 1.x (NVMe→VRAM bypass of CPU staging, GPU-side GDeflate decompression) and Apple **`MTLIOCommandQueue`** with `MTLIOCompressionMethod` (parallel asset loading with hardware Lossless block decompression) — so that the load path is a peer of the render path, not an app-side reinvention per platform. Vulkan has no shipped equivalent (proposals are discussion-stage; some vendors expose GDeflate through `VK_NV_memory_decompression` and analogs). The RHI surfaces a unified asset-IO object so Melody apps do not write their streamer per platform (MEL-ENGINE-I: refusal to support this would be cowardice).

**`Mel_Gpu_Io_Queue`** is a sibling queue type to `Mel_Gpu_Queue` (U7), requested through the same availability/acquire model with role `AssetIo`. Submission to the IO queue is independent of the graphics / compute timelines; U17 timeline semaphores synchronize between the IO queue and other queues identically to any other cross-queue handoff. `caps.asset_io = none | cpu_staged | gpu_decompress` reports the tier.

**Operations recorded into `Mel_Gpu_Io_Command_List`**: `cmd_io_load(src, src_offset, dst_buffer | dst_texture_region, decompress_codec)` reads bytes from a file / blob / mapped region into a GPU buffer or texture subregion, optionally GPU-decompressing along the way. `cmd_io_decompress(src_buffer, dst_buffer, codec)` decompresses in place without touching disk. `cmd_io_decode_block_compressed(src_buffer, dst_texture_view, format)` runs GPU-side block-compression decode (BCn / ASTC / ETC2). Codec enum: `none`, `gdeflate` (D3D12 retail / Vulkan vendor), `lz4`, `apple_lzfse`, `apple_lzbitmap`, `zstd` (engine-side fallback). Caps enumerate the supported codec set per backend.

**`Mel_Gpu_Io_Source`** wraps a file handle, memory-mapped region, or platform-specific source — `IDStorageFile` on D3D12, `MTLIOFileHandle` on Metal, raw fd / `HANDLE` on Vulkan-with-CPU-staging, a `fetch()` Response on Web. Sources are imported as `Borrowed` (U5); the engine never closes the underlying handle.

**Lowering.** D3D12 maps to `IDStorageQueue` + `IDStorageFile` + GDeflate (RTX IO on supporting hardware); on hardware lacking GPU decompress the runtime falls to a CPU-staged path through the same `IDStorageQueue` so submission shape is unchanged. Metal maps to `MTLIOCommandQueue` + `MTLIOFileHandle` + `MTLIOCompressionMethod`. Vulkan with no extension lowers `cmd_io_load` to a CPU-staged read into an UPLOAD buffer plus a graphics-queue copy (`asset_io = cpu_staged`), and GPU decompress lowers to a compute shader running the codec. WebGPU exposes `asset_io = none` honestly — there is no efficient asset-IO primitive; apps stream through `fetch()` + `queueWriteBuffer` and pay the staging round-trip.

**P2 escape.** Apps that need a custom streamer (compression-dictionary management, paged virtual-texture residency, network-streamed bitstreams) bypass the queue and use direct `buffer_write` / `cmd_copy_*` over their own thread pool. The U29 primitives are sufficient to reimplement the queue: source wrap, decompress codec dispatch, completion-future bridging.

---

## 7. Recording and rendering (Tier 3)

### 7.1 Command lists and pools (U15)

**Pools are engine-managed, per-thread per-queue, allocated via TLS.** Recording is lock-free per-thread; submission to a queue takes the queue's lock (U7). P2 escape: explicit pool exposure for power users who want manual reset or aliasing strategies.

**Single-use by default** — Metal and WebGPU mandate it; the portable contract is "record, close, submit, discard." **Reusable command lists** (record once, submit many) are a capability tier — Vulkan/D3D12 native; Metal indirect-command-buffer approximates; WebGPU is an honest gate, `caps.reusable_cmd_lists = false`.

**GPU-driven indirect commands first-class** (no deferral) with a capability tier: `cmd_execute_indirect(layout, args_buffer, count?, max_count)` lowers to Metal `MTLIndirectCommandBuffer`, D3D12 `ExecuteIndirect`, Vulkan `VK_EXT_device_generated_commands`. WebGPU has partial support via `drawIndirect` / `dispatchIndirect`; tier reports it. **`Mel_Gpu_Indirect_Layout`** describes the per-command structure — which state changes the indirect command may encode (root-constant write, root-CBV / root-SRV / root-UAV update, vertex-buffer / index-buffer rebind, pipeline switch) followed by the terminating draw / dispatch / dispatch-mesh / dispatch-rays call. This is the D3D12 `ID3D12CommandSignature` analog and the Vulkan `VkIndirectExecutionSetEXT` / `VkIndirectCommandsLayoutEXT` carrier. Caps gate the admissible state changes via `caps.indirect_state_change = none | tier1 | tier2` — `tier1` admits root-constant writes and draw / dispatch calls (WebGPU equivalent); `tier2` admits binding-table rebinds and pipeline switches (D3D12 native, Vulkan EXT). The layout is built once at start-up, hashed, and referenced from `cmd_execute_indirect` calls.

**Full modern recording surface**: `cmd_bind_pipeline` (or `cmd_bind_shader_objects` on the `VK_EXT_shader_object` pipeline-less lane from §6.5), `cmd_push_constants` (rides the U12 global-`[[vk::push_constant]]` struct discipline; the offset/range come from reflection, not from hand-pick), `cmd_push_descriptors` (`VK_KHR_push_descriptor` core 1.4 — cheap-per-draw lane for bindings that change every draw and the bindless heap is overkill), `cmd_bind_index_buffer` (carries an explicit size parameter so the modern `vkCmdBindIndexBuffer2KHR` from `maintenance5` is reachable), `cmd_bind_vertex_buffers`, `cmd_draw` / `_indexed` / `_indirect` / **`_indirect_count`** / **`_indexed_indirect_count`** (count buffer GPU-produced, no CPU round-trip — Vulkan `vkCmdDrawIndirectCount` Roadmap 2024 mandatory; D3D12 via `ExecuteIndirect` with predicate-count; Metal indirect command buffers; Dawn-native `multi-draw-indirect`+count), `cmd_dispatch` / `_indirect` / **`_indirect_count`**, `cmd_trace_rays` / **`_indirect`** / **`_indirect2`** (RT — `VK_KHR_ray_tracing_maintenance1`, DXR 1.1 from compute), `cmd_draw_mesh_tasks` / `_indirect` / `_indirect_count` (mesh), `cmd_execute_indirect`, `cmd_dispatch_graph` (work graphs, cap-gated, U28), `cmd_build_accel_struct` / `cmd_update_accel_struct` / `cmd_copy_accel_struct` / `cmd_compact_accel_struct` / **`cmd_build_cluster_accel_struct`** / **`cmd_build_blas_from_clusters`** / **`cmd_partitioned_tlas_update`** (U26), `cmd_video_decode` / `cmd_video_encode` / `cmd_video_process` (U27), `cmd_io_load` / `cmd_io_decompress` / `cmd_io_decode_block_compressed` (U29), `cmd_copy_*`, `cmd_blit`, `cmd_clear_*`, `cmd_generate_mips`, `cmd_begin_rendering` / `_end` (U16), `cmd_next_subpass` (U16), `cmd_barrier` / **`cmd_aliasing_barrier(prev_resource, next_resource)`** / **`cmd_queue_ownership_release(resource, dst_queue, subresource_range)`** / **`cmd_queue_ownership_acquire(resource, src_queue, subresource_range)`** (U17), `cmd_set_dynamic_state_*` (viewport, scissor, blend constants, stencil ref, depth bias, primitive topology, cull mode, front face, depth-test / write / compare, stencil-test / op, **`cmd_set_shading_rate`** for VRS, **`cmd_set_sample_locations`**, **`cmd_set_patch_control_points`** for tessellation pipelines, **`cmd_set_tessellation_domain_origin`**), **`cmd_begin_conditional_rendering(buffer, offset, flags)` / `_end`** (`VK_EXT_conditional_rendering` / D3D12 `SetPredication` — draws within the scope are no-ops when the predicate is zero, cap-gated `conditional_rendering = none | predicate | inverted`), `cmd_begin_debug_marker` / `_end` (U21), `cmd_bind_descriptor_set` / `cmd_bind_bind_group` (U14 classic escape), `cmd_write_timestamp` / `cmd_begin_query` / `cmd_end_query` (U24), `cmd_latency_mark(point)` (U19 vendor-latency markers). Subgroup size remains pipeline / shader creation state, exposed through U25 caps and validated by U13, not command-list dynamic state.

Recording calls are infallible per U2 — debug-asserted on misuse, validated by U21 layers in debug, branch-free in release.

**Parallel render-pass recording first-class.** `cmd_begin_rendering_parallel(...)` returns N child contexts; each thread records into its own; the engine assembles them at end. Lowers to Vulkan secondary command buffers / Metal `MTLParallelRenderCommandEncoder` / D3D12 bundles; WebGPU serializes internally (P1 emulate). Designed in from day one — retrofitting parallel recording into a serial API is exactly the rework the no-deferral rule prevents.

### 7.1.1 GPU-driven scheduling and work graphs (U28)

Indirect commands are a command-buffer feature; **work graphs** are a scheduling feature. The API names both so D3D12 Work Graphs, Vulkan `VK_AMDX_shader_enqueue` plus `VK_EXT_device_generated_commands`, Metal indirect command buffers, and future backend-native graph dispatch do not get squeezed into one weak abstraction.

`Mel_Gpu_Work_Graph` owns node declarations, backing memory requirements, entry nodes, record layouts, maximum records, and recursion/depth limits. `pipeline_work_graph_create` validates the Slang node shaders and reflection against those declarations. Recording exposes `cmd_dispatch_graph(graph, entry, records_buffer, count_or_indirect)`; queue submission and U17 timeline waits/signals are unchanged.

Caps report `none | emulated | native`. `native` means the GPU can enqueue node work without CPU intervention. `emulated` means the engine lowers to compute/indirect dispatch loops with the same visible result but no promise of the same scheduling cost. If an app depends on native self-scheduling latency, it checks the cap and branches honestly. **Cross-vendor reality:** D3D12 Work Graphs 1.0 (compute/broadcast nodes) is native on conformant hardware across vendors; 1.1 mesh nodes are preview as of Agility 1.715-preview, retail re-enabling pending. On Vulkan, native work graphs are reachable **only** through `VK_AMDX_shader_enqueue` — provisional, AMD-only, RDNA3+/RDNA4 — so `caps.work_graphs = native` on Vulkan will resolve to AMD hardware exclusively for the foreseeable future; every other Vulkan device falls to `emulated`. Metal lacks a native work-graph primitive; `caps.work_graphs = emulated` lowers to compute + indirect-command-buffer chains. WebGPU core reports `none`.

P2 escape: native work-graph/indirect-command handles are exposed through U5, and users may issue backend-specific generated-command calls inside an interop section followed by `cmd_assume_state` for resources touched by the raw work.

### 7.2 Dynamic rendering and sub-passes (U16)

**Declarative topology, up-front.** `cmd_begin_rendering(pass_desc)` where `pass_desc = { attachments[N color + optional depth/stencil], subpasses[{ writes: [att...], reads_on_tile: [att...] }, ...] }`. Recording marks sub-pass boundaries with `cmd_next_subpass`.

The engine sees the full topology *before* recording starts and lowers optimally:

- **Tiler** — one merged encoder / render pass (mobile bandwidth win).
- **Vulkan ceiling (Roadmap 2026 / 1.4)** — one dynamic-rendering pass with `dynamic_rendering_local_read` attachment-input transitions, plus `VK_EXT_dynamic_rendering_unused_attachments` to relax pipeline-vs-render-instance format matching and eliminate pipeline-permutation explosion across attachment-set variants. **Roadmap 2024 preferred profile** uses dynamic rendering without the local-read ceiling when unavailable, falling back to subpasses or separate passes per attachment-input dependency. **Vulkan 1.2 floor** uses `VK_KHR_dynamic_rendering` if granted, otherwise classic render passes / subpasses where they preserve behavior.
- **D3D12** — separate passes with engine-inserted enhanced barriers between sub-passes.
- **WebGPU** — separate passes (P1 emulate); on Chrome 146+ memoryless attachments lower to `GPUTextureUsage.TRANSIENT_ATTACHMENT`.

The trivial case is one sub-pass with one write set — ergonomic. The U20 render graph later auto-generates `pass_desc` from a declared dependency DAG; the same primitive carries both layers.

**Per-attachment load action** (`Clear` / `Load` / `DontCare`) and **store action** (`Store` / `DontCare` / `Resolve`). **On-tile MSAA resolve** via `storeAction = Resolve` + resolve target; never store the multisample surface (U22).

**Memoryless flag** on attachment-only textures (U8/U10) — tile-resident only, never RAM. Lowers to `MTLStorageModeMemoryless`, Vulkan `LAZILY_ALLOCATED + TRANSIENT_ATTACHMENT`, and WebGPU `GPUTextureUsage.TRANSIENT_ATTACHMENT` (Chrome 146+).

**Attachment feedback loops** — sampling from a bound attachment without a barrier — are declared explicitly via `pass_desc.feedback_loops`, lowering to `VK_EXT_attachment_feedback_loop_layout` plus `VK_EXT_attachment_feedback_loop_dynamic_state` on Vulkan (retained as exceptions even under `VK_KHR_unified_image_layouts`), Metal's intrinsic support, D3D12's `D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY`/`END_ONLY` pairs around the loop, and WebGPU's restrictions.

**VRS, foveation, multiview, and Metal tile shaders are admitted from day one (cap-gated).**

- **VRS / fragment shading rate.** Cap `caps.vrs = none | per_draw | per_primitive | per_image` (D3D12 Tier 1 = per_draw; Tier 2 = per_primitive + per_image; Vulkan `VK_KHR_fragment_shading_rate` Roadmap 2026 mandatory). `pass_desc.shading_rate_attachment` binds a screen-space shading-rate texture, `cmd_set_shading_rate(rate, combiner_ops[2])` sets per-draw rate (the combiner pair governs pipeline / primitive / attachment composition), Slang `[shading_rate_per_primitive]` carries per-primitive rate in mesh / vertex outputs.
- **Foveated rendering.** Cap `caps.foveation = none | static | gaze_driven`. `pass_desc.fragment_density_map` binds a fragment-density-map attachment (Vulkan `VK_EXT_fragment_density_map` plus `VK_EXT_fragment_density_map_offset`, Metal `MTLRasterizationRateMap` via `MTLRenderPassDescriptor.rasterizationRateMap`, D3D12 lowers to VRS Tier 2 screen-space image). `gaze_driven` is the same primitive with a per-frame density-map update from the platform eye-tracker (OpenXR `XR_FB_eye_tracking_social` and analogs). visionOS `cp_layer_renderer` foveation is consumed transparently when the swapchain image set is borrowed from Compositor Services (U18).
- **Multiview** (VR / AR / stereo). Cap `caps.multiview = none | view_mask | view_instancing | per_view_viewport`. `pass_desc.view_mask` enables multiview rendering; lowering: Vulkan `VK_KHR_multiview` (core 1.1), D3D12 view instancing (`SV_ViewID`), Metal vertex amplification (`MTLRenderPassDescriptor.maxVertexAmplificationCount`), WebGPU emulated as separate passes per view.
- **Metal tile shaders.** Tile-local compute interleaved with raster, admitted through the U16 sub-pass topology when `caps.tile_local = native` is granted (Apple Silicon).

**Motion-vector and reactive-mask attachment compatibility.** U16 attachment declarations admit user-defined motion-vector and reactive-mask outputs by convention — no special attachment kind beyond `Color`, the format is the user's choice. This is the architectural commitment that DLSS / FSR / XeSS / MetalFX upscaling and frame-generation can be integrated by the app without engine bending: the engine does not foreclose the attachments or per-frame data those SDKs require. `Frame_Info` (U19) carries the current and prior-frame jitter offsets so reprojection stays reproducible. The engine itself ships no upscaler — that is the app's choice from a vendor SDK or a custom solution (MEL-ENGINE-X: serve the user's vision, do not impose your own).

Sample positions per pipeline / per subpass are set via U13's `cmd_set_sample_locations` dynamic state and the `sample_locations` pipeline field (`VK_EXT_sample_locations`), so TAA-stable jitter patterns are reachable without baking N pipelines per pattern.

### 7.3 Barriers and timeline synchronization (U17)

**Sync primitives.** **Timeline semaphore** (64-bit monotonic counter — Vulkan core 1.2 / Metal `MTLSharedEvent` / emulated on WebGPU via submit ordering) is the modern cross-queue workhorse. **Binary semaphore** exists for swapchain interop where required (Vulkan present cannot take a timeline). **No separate fence object** — CPU completion is the future returned by `queue_submit` (U7); the future *is* the fence. One less concept (MEL-ENGINE-IX).

All sync primitives carry **importable backing** (U5) — constructible internally or from external native handle, identical wait/signal at submission.

**State model — D3D12-style resource-state enum** as the primary public API: `Common`, `VertexBuffer`, `IndexBuffer`, `ConstantBuffer`, `ShaderResource`, `UnorderedAccess`, `RenderTarget`, `DepthWrite`, `DepthRead`, `CopySource`, `CopyDest`, `ResolveSource`, `ResolveDest`, `IndirectArgument`, `Predication`, `StreamOutput`, `Present`, `RayTracingAccelStruct`, `AccelStructBuildRead`, `AccelStructBuildWrite`, `ShadingRateSource`, `VideoDecodeRead`, `VideoDecodeWrite`, `VideoEncodeRead`, `VideoEncodeWrite`, `VideoProcessRead`, `VideoProcessWrite`, `MlTensorRead`, `MlTensorWrite`, `ExternalRead`, `ExternalWrite`. Engine maps each transition to the optimal `pipeline_stage_2`/`access_2` pair on Vulkan synchronization2 (core 1.3 or `VK_KHR_synchronization2` on 1.2) and falls back to legacy barriers only when behavior is equivalent; D3D12 lowers to enhanced barriers where available or legacy resource barriers where equivalent.

**`VK_KHR_unified_image_layouts` fast path.** Where granted (KHR ratified 6 June 2025, SDK 1.4.321.0 July 2025), `VK_IMAGE_LAYOUT_GENERAL` is guaranteed as efficient as specialized layouts for the covered image usages on conformant hardware. The engine caps it (`caps.unified_image_layouts = none | graphics_compute | graphics_compute_video`) and on the granted path skips only the layout component that the granted feature actually covers, while still emitting stage/access barriers and ownership transfers. Present/shared-present layouts remain interaction with the OS compositor; external-memory handoff remains queue-family/foreign ownership transfer; Vulkan Video layouts require the separate `unifiedImageLayoutsVideo` feature. Feedback-loop exceptions remain explicit through the loop-layout / dynamic-state declarations above. Backends without the cap follow the original state-enum lowering.

**P2 escape: explicit stage+access barriers** as a peer — `cmd_barrier_fine(resource, src_stage, src_access, dst_stage, dst_access, layout_old, layout_new)` — for power users needing synchronization2 / enhanced-barrier precision. Easy path but no gutting the power users.

**Explicit barriers at Layer 0** via `cmd_barrier(resource, src_state, dst_state, subresource_range)`. The subresource range — buffer offset / size, texture mip / layer / plane / aspect — is mandatory; whole-resource transitions are sugar over a full-range argument. Engine-internal state tracking is **per command list per subresource**, not per whole resource, so a streaming-in mip transition does not stomp the state of unrelated mips on the same texture. Mismatches assert in debug (MEL-ENGINE-VIII). The U20 render graph automates barriers later from a declared DAG; explicit remains the P2 peer.

**Aliasing barriers.** Memory-aliased resources from U8 placed allocations require an explicit handoff: `cmd_aliasing_barrier(prev_resource, next_resource)` invalidates `prev_resource` on the shared backing and `discard-initializes` `next_resource`. Lowering: D3D12 `D3D12_RESOURCE_BARRIER_TYPE_ALIASING`; Vulkan a `VK_IMAGE_LAYOUT_UNDEFINED` transition on `next_resource` with the appropriate stage / access mask; Metal a heap residency-consistency point; WebGPU absent (U8 suballocates 1:1, no aliasing possible). The render graph (U20) emits these automatically for transient resources; explicit issue is the P2 peer for power users running their own pool.

**Cross-queue ownership transfer.** Vulkan `VK_SHARING_MODE_EXCLUSIVE` resources require an explicit acquire / release pair when migrating between queue families; mishandling is silent corruption on Mali / Adreno. `cmd_queue_ownership_release(resource, dst_queue, subresource_range)` is recorded on the source queue; `cmd_queue_ownership_acquire(resource, src_queue, subresource_range)` is recorded on the destination queue; the pair is fenced with a U17 semaphore signaled from the source submit and waited on the destination submit. D3D12 has no explicit ownership concept (state is queue-agnostic with enhanced barriers); the calls lower to a no-op-but-tracked hint. Metal queues are untyped; no-op. WebGPU is single-queue; no-op. The render graph emits these automatically when scheduling a pass on a different queue family from its producer.

**State-resync hook (U5):** `cmd_assume_state(resource, subresource_range, state, queue_owner?, stage_access_override?)` lets a user who reached through the native interop hand state back coherently without collapsing the tracker back to whole-resource state.

### 7.4 Swapchain, surface, present (U18)

**`Mel_Gpu_Surface` wraps a platform window** — `NSView*`, `ANativeWindow*`, `HWND`, canvas selector, EGL surface — decoupled from the swapchain so the surface outlives swapchain rebuilds.

**Extent semantics are pinned in device pixels for the GPU surface and reported in OS-native units separately for the input / UI surface.** `surface.pixel_extent` is the swapchain's natural extent — the unit the GPU rasterizes into, the unit `swapchain_acquire` returns. `surface.point_extent` is the OS-coordinate-system size — `NSView.bounds` in points on macOS, the `CALayer` size before `contentsScale` on iOS, the DPI-adjusted window size on Windows, the `wl_surface` configure size pre-scale on Wayland, the CSS-pixel canvas size on Web. `surface.scale_factor` is the ratio (Retina = 2.0, fractional on Wayland HiDPI, 1.0 elsewhere). The DPI / scale-factor change event fires when *any* of these changes — moving a window between Retina and non-Retina displays, Wayland output scale change, Web `devicePixelRatio` change — and the app is responsible for deciding whether to resize the swapchain or re-flow its UI. The engine never silently re-scales the swapchain on a scale-factor change.

**Surface lifecycle is explicit.** A surface has state independent of the swapchain: `Alive`, `Occluded`, `Minimized`, `Backgrounded`, `Lost`, and `Destroyed`. It emits events for resize, drawable-size change, DPI / scale-factor change, orientation change, display migration (a window moved to a different `Mel_Gpu_Output` from U6), HDR / EDR capability change, occlusion, background / foreground transition, platform-layer replacement, canvas reconfiguration, and surface loss. Android `ANativeWindow` destroy / recreate, iOS `CAMetalLayer` drawable-size changes, macOS window scale / display moves, Windows swapchain resize / occlusion, Wayland configure, and Web canvas resizing all lower to this same event stream.

**Surface-lifecycle event handlers run synchronously on the platform's UI thread.** On Android specifically, `surfaceDestroyed` requires the engine to release the Vulkan swapchain *before the callback returns* — the UI thread will not unblock otherwise, and the next `surfaceCreated` arrives with the prior swapchain still bound to a dead `ANativeWindow`. The RHI honors this: the swapchain release path is callable synchronously from the handler, and in-flight submission completion futures detach (resolved with `surface_lost` status when the submission finishes draining on the GPU) so the handler does not block on GPU work. The reactor-driven completion pump is decoupled from surface event delivery for exactly this reason.

**Swapchain lifecycle follows surface lifecycle.** `swapchain_acquire` and `swapchain_present` return U2 statuses for `suboptimal`, `out_of_date`, `occluded`, `surface_lost`, and `backgrounded`. The engine may provide `swapchain_reconfigure(surface, desc)` as the convenience path, but the app can also destroy/recreate explicitly. No resize, minimize, or background transition is hidden behind a silent recreate; every change that affects latency, size, color, or availability is visible to the app (MEL-ENGINE-III, VIII).

**Swapchain configuration**: format, present mode (`Immediate` / `Mailbox` / `Fifo` / `FifoRelaxed`), image count, color space, alpha mode, usage flags. All negotiated through U2 status — request `Mailbox`, get `Fifo` with a substitution warning; request `HDR10`, get `sRGB` with a warning if unsupported. The user inspects and decides whether to accept.

**HDR / wide-color first-class** (no deferral). Color spaces cover `sRGB`, `Display-P3`, `Rec.2020`, `scRGB-linear`, `HDR10` (PQ + Rec.2020), `HLG`. Caps report supported spaces plus HDR metadata (peak nits, content luminance). Unsupported targets show up as substitution warnings.

**HDR metadata setters are first-class** (`swapchain_set_hdr_metadata`). The descriptor carries mastering-display primaries, white-point, max / min mastering luminance, `MaxCLL` (Content Light Level), `MaxFALL` (Frame-Average Light Level), and SDR-content reference-white nits for SDR-in-HDR composition. Lowering: D3D12 `IDXGISwapChain4::SetHDRMetaData` with `DXGI_HDR_METADATA_HDR10` / `_HDR10PLUS`; Vulkan `VK_EXT_hdr_metadata` and `VK_EXT_hdr_metadata_plus`; macOS `CAMetalLayer.wantsExtendedDynamicRangeContent` + `CAEDRMetadata`; iOS HDR layer attributes; WebGPU `configure({ toneMapping, hdrMetadata })` where the browser exposes it. Without explicit metadata the engine ships the conservative defaults the platform expects; setting metadata is the user's deliberate act.

**Wayland color management** (the Wayland color-management protocol stabilizing 2025-2026) is consumed on the Linux backend transparently — `Mel_Gpu_Surface` reports supported color spaces from the compositor's protocol surface where granted, and HDR metadata setters route through the protocol when the compositor admits it. Outside that protocol the Vulkan + DRM path remains the fallback.

**Tearing / variable-refresh.** Cap `caps.allow_tearing` (D3D12 `DXGI_FEATURE_PRESENT_ALLOW_TEARING`); swapchain creation admits `flags.allow_tearing` so `Immediate` present mode can lower to `DXGI_PRESENT_ALLOW_TEARING` for true VRR on Windows. Vulkan VRR rides the underlying `Fifo` / `FifoRelaxed` present modes plus the display's variable-refresh protocol; the engine does not need a special tearing flag there. WebGPU has no tearing primitive.

**Format negotiation restricted to representable formats** — the prior BGRA8Unorm-vs-sRGB-mismatch bug does not recur because negotiation only ever picks a format `Mel_Gpu_Format` can name.

**Per-image render-finished sync** — array sized to image count, indexed by acquired image (fixes the old hazard of one shared semaphore being signaled before its prior present consumed it). **GC discipline pinned**: each per-image render-finished semaphore is reused only after the prior present's `presentFenceInfo` fence (from `VK_KHR_swapchain_maintenance1`, below) signals — not by frame index. The reactor pump observes the present fence and the binary-semaphore reset is gated on it (§3.3 future-gated retire). Under variable image counts (swapchain rebuild changing count) the array is re-sized and the old semaphores destroyed once their gating fences resolve.

**External / borrowed image sets** (U5): `swapchain_create_borrowed(image_set, ...)` for OpenXR swapchain images, visionOS Compositor Services `cp_layer_renderer` image sets, and video interop; the swapchain wraps but does not own.

**Shared presentable images** (`caps.presentation.shared_presentable_image`, lowered via `VK_KHR_shared_presentable_image` on Vulkan, equivalent direct-mode flips on D3D12 / Metal where exposed) admit a present mode `SharedDemandRefresh` / `SharedContinuousRefresh` for VR / low-latency / direct-scanout paths where the compositor is bypassed and the GPU writes directly into a scanout buffer the display reads concurrently. `swapchain_create_shared(...)` is the carrier; OpenXR direct-mode HMDs and visionOS Compositor Services consume this transparently through borrowed image sets, but the primitive is available for any low-latency direct-scanout use case. WebGPU honestly absent.

**OpenXR integration depth.** Borrowed-image swapchains carry the OpenXR view-configuration (mono / stereo / quad / foveated), the per-frame `XrFrameState` predicted display time, and the per-view projection state. `swapchain_present_xr(image_set, layer_descriptors[])` admits the OpenXR compositor layer types — `XR_COMPOSITION_LAYER_PROJECTION`, `_QUAD`, `_EQUIRECT2`, `_CYLINDER`, `_CUBE`, `_PASSTHROUGH_FB`, `_DEPTH_INFO` — so a Melody-built XR app does not have to reach through the native hatch to submit a multi-layer compositor frame. Foveation per-eye is consumed via U16 (`fragment_density_map`). The U19 pacing source for an XR swapchain ticks off `xrWaitFrame`'s predicted-display-time, not the desktop vsync.

**visionOS** `cp_layer_renderer` layered swapchains are first-class: per-layer foveation rates, per-eye projection state, and the visionOS compositor-driven frame timing flow through the U16 / U18 surfaces transparently.

**WebGPU `importExternalTexture` lifetime is source-specific.** A `GPUExternalTexture` imported from an HTML media element expires through WebGPU's automatic expiry rules; a texture imported from a WebCodecs `VideoFrame` remains tied to that `VideoFrame`'s validity and expires when the source frame closes. The borrowed-image-set path enforces this lifetime explicitly — the imported texture's slot generation rolls when the browser invalidates the import or the source closes, turning use-after-invalidate into a loud `alive()` failure rather than a sample-from-stale-memory race.

**Multi-swapchain per device.** No singleton; multi-window first-class.

**Acquire/present sync ownership.** Engine-auto by default — acquire returns a texture view, present takes it, semaphores handled internally. **P2 escape** returns the binary semaphores for users driving custom queue submission.

**DXGI frame-latency waitable.** On D3D12 the swapchain admits the `DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT` lane: `swapchain.frame_latency_waitable() → WAITABLE_HANDLE` is exposed (cap-gated `caps.frame_latency_waitable`) so the engine integrates the `WaitForSingleObject`-on-IOCP loop as a U3 reactor source. This is the canonical low-latency path on Windows; the Vulkan analog is `VK_KHR_present_wait2`, already named below. Both feed the U19 pacing source identically.

**`VK_KHR_swapchain_maintenance1` + `VK_KHR_surface_maintenance1`** (promoted to KHR on 2025-03-31; Roadmap 2026 mandatory) drive the modern swapchain control surface: per-present mode switching without swapchain recreation, deferred swapchain memory, fence-on-present, image release without present. The RHI consumes them transparently for the present-mode-change-on-the-fly path; the EXT variants remain as aliases on runtimes that haven't promoted.

**Headless is not in this unit.** Offscreen rendering is render-to-texture (U10) + copy to `READBACK` buffer (U9). No virtual swapchain — surface-attached presentation only.

**Present timing first-class.** The swapchain exposes per-presented-frame timestamps and a `next_vsync_estimate()` query. Vulkan: `VK_KHR_present_wait2` + `VK_KHR_present_id2` (both Roadmap 2026 mandatory; supersede the deprecated `VK_KHR_present_wait` / `VK_KHR_present_id` — NVIDIA shipped them in driver 573.38 beta June 2025) for per-surface frame waits, paired with `VK_KHR_present_mode_fifo_latest_ready` for tight pacing under FIFO. `VK_EXT_present_timing` supplies presentation-engine timing feedback and scheduled-present hints where available; `VK_GOOGLE_display_timing` remains the older fallback on platforms that ship it. Metal: `CAMetalDrawable.presentedTime` + `MTLCommandBuffer.GPUStartTime`. D3D12: `IDXGISwapChain1::GetFrameStatistics` plus the DXGI frame-latency waitable object above. WebGPU has limited timing information. U19 consumes these for vsync-aligned, on-demand, and frame-rate-capped pacing (MEL-ENGINE-VI).

### 7.5 Frame pacing (U19)

**Per-swapchain pacing source** replaces the current free-running reactor-timer render source. Wired to native vsync where available:

- Android **`Choreographer`**.
- iOS / macOS **`CADisplayLink`** (or **`CAMetalDisplayLink`** on Apple Silicon).
- Vulkan platforms **`VK_KHR_present_wait2`** + **`VK_KHR_present_id2`** + **`VK_KHR_swapchain_maintenance1`** `FIFO_LATEST_READY`, with `VK_EXT_present_timing` / `VK_GOOGLE_display_timing` where present.
- Windows D3D12 **DXGI frame-latency waitable object** plus `IDXGISwapChain1::GetFrameStatistics`.
- Web **`requestAnimationFrame`**.

Fallback to a high-precision reactor timer where no native vsync exists.

**Four explicit modes**, each a distinct semantic primitive so the engine can apply mode-specific platform optimizations:

- **`Continuous`** — render every vsync. Game default.
- **`OnDemand`** — render only after `invalidate()`. UI default. The engine **suspends the vsync callback entirely** and re-arms on invalidate — real CPU/battery win on static scenes (MEL-ENGINE-VI), which a single-mode-with-knobs would miss.
- **`Capped(target_fps)`** — render at most `target_fps`, skipping vsyncs to hold the cap (e.g., 60 fps on a 120 Hz panel). The mobile battery lever.
- **`Adaptive(target_frame_ms)`** — target a frame-time budget; the engine exposes "budget left" each frame and the app decides how to scale quality.

**Frame budget exposed each frame.** `Frame_Info` (pacing-tick context, not a retirement index — §3.3 retirement is future-gated and never exposed publicly) passed to the render callback carries previous GPU time (from U24 timestamp queries straddling submission), CPU time, predicted next-vsync time (from U18 present-timing), headroom, mode-specific state, **the current frame's subpixel jitter offset and the prior frame's jitter offset** (so reprojection for TAA / upscaling stays reproducible without engine-side bookkeeping), **the current thermal-pressure tier** (`nominal | fair | serious | critical`, from `caps.thermal`), **the current power source and low-power-mode flag** (`power_source ∈ { ac | battery | unknown }`, `low_power_mode ∈ { off | on }`, both updated from the U6 instance-level events), and **the current latency-marker context** (set by `cmd_latency_mark`, below).

**Latency-marker primitive.** `cmd_latency_mark(point)` and `mel_gpu_latency_sleep(target)` integrate vendor latency reduction surfaces uniformly. Points: `SimStart`, `SimEnd`, `RenderSubmitStart`, `RenderSubmitEnd`, `PresentStart`, `PresentEnd`, `InputSample`. Lowering: NVIDIA Reflex (`NvAPI_D3D_SetLatencyMarker` on D3D12, `VK_NV_low_latency2` on Vulkan), AMD Anti-Lag 2 (`AMD_AGS_Lib` / `VK_AMD_anti_lag`), Intel XeSS Frame Generation latency markers, Metal native low-latency hints, no-op on WebGPU. `mel_gpu_latency_sleep` blocks the reactor until the vendor-recommended pre-input deadline. Cap `caps.latency_marker = none | reflex | anti_lag | xess_fg | platform_native` reports the available surface; one app code path serves them all.

**Thermal-pressure event.** Mobile and laptop devices throttle under heat; the engine raises a `thermal_pressure_changed(tier)` event upward to a user-registered callback when iOS `NSProcessInfo.thermalState`, Android thermal API, or Apple Silicon Mac thermal pressure transitions. The streaming / quality / pacing system holds the semantic knowledge of how to respond (drop to `Capped(30)`, lower internal render resolution, suspend non-critical compute); the engine reports the tier and does not impose a policy (MEL-ENGINE-V: respect the user's product, do not impose your own opinion on his content model).

**Power-source and low-power-mode events.** Independent of thermal pressure, the OS reports power-supply state and user-initiated low-power modes. The `Mel_Gpu_Instance` raises `power_source_changed(source)` when AC ↔ battery transitions occur (Windows `GetSystemPowerStatus` / `RegisterPowerSettingNotification`, macOS / iOS `IOPSCopyPowerSourcesInfo`, Linux `UPower` / `org.freedesktop.UPower`, Android `BatteryManager`, Web `navigator.getBattery()`), and `low_power_mode_changed(on/off)` when the OS-level battery-saver / Low Power Mode toggles (Windows Battery Saver `SYSTEM_POWER_STATUS.SystemStatusFlag`, macOS / iOS `NSProcessInfo.isLowPowerModeEnabled`, Android `PowerManager.isPowerSaveMode`). These are independent signals from thermal because a laptop on AC may be in user-initiated Low Power Mode (the user wants quiet, not cool), and a phone at 90% battery may still be in Low Power Mode (the user is conserving for later). The app picks a pacing / quality / streaming response per its content model.

**Multi-swapchain pacing.** Each swapchain has its own pacing source; one reactor pumps all. A window in the background can auto-drop to `OnDemand` (battery on multi-window UIs).

**VRR / adaptive sync** is transparent at this layer — the underlying `Fifo` present mode handles it; pacing sees variable intervals.

**P2 escape**: the user supplies a custom pacing source built on the same underlying hooks the engine's modes use. The canonical example is **OpenXR-driven pacing**: an XR swapchain (U18) is paced by `xrWaitFrame`'s predicted-display-time, not vsync — the app installs a custom `Mel_Gpu_Render_Source` that fires on the OpenXR runtime's frame-loop callback and consumes the predicted display time as `Frame_Info.predicted_next_present_ns`. Other canonical cases: video-encoder output cadence (encoder pull-clock drives render), networked sync (multi-machine setups paced by a network heartbeat).

### 7.6 Media, video, camera, and image processing (U27)

Melody's GPU layer is an application RHI, not only a game renderer. Camera preview, video calls, video editors, streaming encoders, screen capture, remote desktop, scientific visualization, and ML preprocessing all need the same properties as rendering: explicit memory, explicit sync, color correctness, zero-copy import/export, and predictable fallbacks.

**Objects.** `Mel_Gpu_Video_Session` describes codec, profile, level, chroma format, bit depth, dimensions, rate-control class, decode/encode/process mode, and required parameter sets. `Mel_Gpu_Video_Frame` is a typed wrapper around one or more U10 textures/views plus color metadata. Bitstreams and parameter buffers are U9 buffers with `VIDEO_BITSTREAM` / `VIDEO_PARAMETER` usage. Sessions and frames are ordinary handles, nameable, importable where the backend allows it, and visible to leak detection.

**Decode/encode/process commands.** `cmd_video_decode`, `cmd_video_encode`, and `cmd_video_process` are recorded into command lists and submitted through U7 queues. Video processing includes scale, crop, rotate, deinterlace where supported, color-space conversion, tone-map, and format conversion. Backends without hardware video commands may lower process operations to compute shaders; decode/encode hard-gate if no faithful hardware or platform path exists.

**Interop is the primary path, not an afterthought.** Camera frames, OS decoder output, browser video frames, `CVPixelBuffer`, `AHardwareBuffer`, DXGI shared handles, OpenXR swapchain images, and platform capture surfaces import as `Borrowed` textures/frames with external sync. The user can sample them directly, process them, encode them, or export them again without a mandatory RGB staging copy.

**Protected content is an honest tier.** Some media paths expose protected sessions/surfaces that cannot be mapped, captured, exported freely, or sampled by arbitrary shaders. Caps report protected decode / process / present support separately. The RHI never strips protection to make a feature seem available; protected resources carry restrictions in their descriptors, validation enforces them, and capture / debug tools are told to stand down where required. Beyond video, a `protected` flag may be set on render targets, pipelines (`VK_EXT_pipeline_protected_access` plus `VK_KHR_protected_memory`-derived submission, D3D12 protected-resource sessions), and queues; HDCP-bound render targets and DRM-bound framebuffers ride this flag so a protected pass cannot accidentally bind an unprotected resource (the pipeline-create fails). The granularity is per-resource and per-pipeline, not just per-session.

**Color metadata travels with the frame.** Every imported or created video frame carries color primaries, transfer function, matrix, range, chroma siting, mastering metadata where present, and orientation. Rendering, compute, and video-process commands consume that metadata explicitly; the engine never guesses Rec.709 vs Rec.2020 or full vs limited range.

**Lowering.** Vulkan uses `VK_KHR_video_queue` plus the codec set — `VK_KHR_video_decode_h264`, `VK_KHR_video_decode_h265`, `VK_KHR_video_decode_av1` (KHR), `VK_KHR_video_decode_vp9` (KHR, 1.4.317, June 2025), `VK_KHR_video_encode_h264`, `VK_KHR_video_encode_h265`, `VK_KHR_video_encode_av1` (KHR, November 2024 alongside Vulkan 1.3.302) — plus `VK_KHR_video_maintenance1` / `VK_KHR_video_maintenance2` (the latter supports inline session parameters such as `VkVideoDecodeAV1InlineSessionParametersInfoKHR`), `VK_KHR_video_encode_quantization_map`, and sampler-YCbCr conversion where granted. D3D12 uses Video Decode/Encode/Process command lists plus **AV1 encode (Win11 24H2 / WDDM 3.2)** alongside H.264, HEVC, and HEVC 4:2:2/4:4:4, with shared DXGI resources where supported. Metal uses CoreVideo / VideoToolbox / AVFoundation interop plus Metal textures and Metal compute / ML / video-capable encoders where appropriate. WebGPU exposes `importExternalTexture` + WebCodecs `VideoFrame` (Chrome 116+, Display-P3 HDR Chrome 121) — the **primary** WebGPU video path, not a degraded fallback — for sampling and shader/compute processing; it has no core hardware decode/encode command model and reports `caps.video_decode = import_only` / `caps.video_encode = none` accordingly. The `import_only` tier is the honest constraint, not a stigma — the WebCodecs bridge is a first-class primary path on Web.

**P2 escape.** Apps may bypass the engine's session helpers and import platform decoder/encoder outputs directly through U5. The primitives exposed here are sufficient to reimplement the helper: frame import/export, plane views, color metadata, sync import/export, video queues, bitstream buffers, and process commands.

---

## 8. Render graph (Layer 1, U20)

**Spec full, implementation phased.** Layer 0's API does not change when the graph lands — the graph generates inputs to Layer 0 (`pass_desc` for U16, barrier sequences for U17, aliasing patterns for U8, queue/timeline assignment for U7). It is purely additive; users continue to use Layer 0 directly if they want (P2 escape).

### 8.1 Pass types and resources

Pass types — **`Graphics`** (with U16 sub-pass topology fully expressible), **`Compute`**, **`Copy`**, **`RayTracing`**, **`AccelerationBuild`**, **`VideoDecode`**, **`VideoEncode`**, **`VideoProcess`**, and **`WorkGraph`**. Each pass declares **resource accesses**, not just whole-resource read/write sets, and a **record callback** invoked at execution with the command list and resolved physical resources. The extra pass types are not special render-graph magic; they let the compiler choose the correct U7 queue, U17 states, query scopes, and aliasing rules without hiding the underlying Layer 0 primitives.

Three resource categories:

- **Transient** — graph-local; engine aliases memory between non-overlapping lifetimes via U8 placed allocations.
- **Imported** — handles from outside the graph (U9/U10 persistent resources, U18 swapchain image).
- **Exported** — named outputs available to subsequent code or another graph.

Resources are declared with full descriptors (extent / format / usage); the engine decides physical backing during compilation. Each pass access names the resource, subresource range (buffer offset/size; texture mip/layer/plane/aspect; AS region where applicable), access kind (`Read`, `Write`, `ReadWrite`, `DiscardWrite`, `Resolve`, `Present`, `ExternalAcquire`, `ExternalRelease`), required U17 state, participating shader/command stages, queue role, attachment load/store intent where relevant, aliasing eligibility, and optional initial/final state for imported/exported resources. Whole-resource reads/writes are convenience constructors over this exact access record.

### 8.2 Compilation phases

All automatic from declared dependencies:

1. **Dependency DAG construction** from declared access records.
2. **Pass culling** — passes whose outputs are transitively unused are removed.
3. **Resource lifetime analysis** — when each transient is first written, last read.
4. **Transient memory aliasing** — non-overlapping resources pack into shared physical allocations (U8). Each transition between two aliased resources on the same backing emits a `cmd_aliasing_barrier(prev, next)` (U17) automatically; the explicit Layer-0 primitive remains the P2 peer.
5. **Automatic barrier synthesis** — generates U17 state transitions between passes; the user writes zero barriers. The synthesizer emits **split barriers** (signal-event-after-producer / wait-event-before-consumer; lowering to `VkEvents` on Vulkan, fenced barrier batches on D3D12 enhanced barriers, encoder boundaries on Metal) wherever the lowering supports it, so independent work between producer and consumer can fill the gap instead of stalling on a global pipeline drain. A preceding **pass-reordering step** (Granite-style baker) maximizes the distance between producer and consumer wherever reordering is safe, so split-barrier overlap actually pays off. On backends that cannot split a barrier (WebGPU implicit barriers; older D3D12 legacy resource barriers), the synthesizer collapses to a single full barrier and the optimization simply does not apply (P1 emulate).
6. **Sub-pass merging** — contiguous compatible passes collapse into one U16 render pass with tile-local reads (mobile bandwidth win, MEL-ENGINE-VI).
7. **Queue assignment + async-compute / video / work-graph scheduling** — passes that can overlap dispatch to compute, media, or native work-graph queues where caps allow; U17 timeline semaphores inserted automatically. Cross-queue resource handoffs emit `cmd_queue_ownership_release(resource, dst_queue, range)` on the producer and `cmd_queue_ownership_acquire(resource, src_queue, range)` on the consumer (U17) automatically — the Vulkan `SHARING_MODE_EXCLUSIVE` discipline is the graph's responsibility, not the user's.

### 8.3 Execution and caching

The compiled plan is an ordered list of `(queue, pass, barriers, sub-pass-merge group)`. The engine walks it, invoking each pass's record callback with the resolved physical resources. The user records draws/dispatches; no manual barriers, no sub-pass declarations, no aliasing bookkeeping.

**Caching.** Graph hashed by topology + descriptors; identical hashes reuse the compiled plan. Per-frame rebuild is the default (cheap); persistent compiled-once graphs are supported for unchanging pipelines.

**Conditional passes** carry a runtime predicate and are skipped at execution.

**External imports/exports** declare initial/final state (U5 state-resync hook).

**Debug introspection.** The compiled plan is inspectable — pass list, resource lifetimes, aliasing decisions, barriers, sub-pass merge groups, queue assignment timelines — for profilers and visualizers.

### 8.4 Builder API

**Function-pointer two-phase, builder-style.** A per-pass descriptor struct:

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

Passes are added via `mel_gpu_graph_add_pass(g, desc)`. Resources declared symbolically (`Mel_Gpu_Graph_Resource` is a graph-local handle, resolved to a physical resource after compilation). C-idiomatic; no closures required; easy to serialize, inspect, and diff.

**`recordFn` is forbidden from creating pipelines, accel structs, or other compile-time resources.** Pipelines must exist at graph-compile time; reaching for `pipeline_*_create` inside `recordFn` either blocks the record thread on async compile (defeating the cache reuse the graph's hash is built on) or returns a `MissingPipeline` status that recording cannot recover from. The pattern: warm pipelines before the graph is added, capture their handles in `userData`, retrieve them in `recordFn`. Debug-asserts on creation calls issued inside a record callback (MEL-ENGINE-VIII).

**Work-graph dispatch composes inside a render-graph pass.** A `WorkGraph` pass type takes a `Mel_Gpu_Work_Graph` handle and the entry-node record buffer; `recordFn` issues `cmd_dispatch_graph(graph, entry, records, count)` and nothing else. The render graph's resource-access records still drive U17 state transitions for the work-graph's input and output resources; the engine treats the work-graph dispatch as one opaque pass for scheduling purposes — its internal node DAG is the GPU's concern, not the render graph's. This composition is the natural carrier for GPU-driven scenes where the CPU sketches the rendergraph topology (passes, attachments, dependencies) and the GPU produces the actual draw lists inside a work-graph pass.

### 8.5 P2 escape

Skip the graph entirely. Use Layer 0 directly — manual U16 sub-pass topology, manual U17 barriers, manual U8 pools, manual U7 submission. The graph is one path; Layer 0 manual is the peer.

---

## 9. Vendor optimization providers (Layer 2)

Layer 2 sits atop Layer 0 (the RHI) and Layer 1 (the render graph) and exposes vendor SDKs and architecture-specific paths as **optional providers**. The rule for every item in this layer is simple: **vendor paths are optimization surfaces, never correctness surfaces**. If a provider is absent, unavailable on the user's driver, blocked by platform policy, or not worth enabling for the current workload, the engine falls back, gates loudly, or returns a precise missing-capability status (MEL-ENGINE-VIII). No game code should have to spell "NVIDIA", "AMD", "Intel", "Apple", "Qualcomm", "Arm", or "Imagination" to be correct.

### 9.1 Vendor and architecture profile (U30)

`Mel_Gpu_Vendor_Profile` is an immutable device-side record adjacent to U4 caps:

- `vendor`: `Nvidia | Amd | Intel | Apple | Qualcomm | Arm | Imagination | Microsoft | Mesa | Unknown`.
- `architecture_family`: backend-specific string plus a stable enum where known, e.g. `Ada`, `Blackwell`, `RDNA3`, `RDNA4`, `XeHPG`, `Xe2`, `AppleGpuFamily10`, `Adreno7xx`, `MaliImmortalis`, `PowerVR`.
- `driver`: vendor driver name, branch, version, feature-profile ID, and shader compiler fingerprint.
- `render_architecture`: `Immediate | Tiler | HybridTiler | Unknown`.
- `subgroup`: supported wave sizes, preferred wave size by stage, quad-op support, subgroup-size-control tier.
- `memory`: UMA/discrete, ReBAR/GPU-upload availability, cacheline hints where exposed, tile-memory behavior, compression friendliness.
- `rt`: ray-tracing tier, traversal stack limits where exposed, SER/reordering support, micromap/micromesh support.
- `matrix`: tensor/XMX/tensor-core/cooperative-matrix/cooperative-vector tiers.
- `presentation`: low-latency provider, frame-generation provider, variable-refresh behavior, display timing provider.
- `tooling`: capture, counter, crash-dump, and shader-disassembly providers available on this machine.

The profile is for choosing variants, not for changing semantics. Public APIs should ask caps first; vendor profile is a tie-breaker for quality/performance choices.

### 9.2 Provider loading model (U31)

Vendor SDKs are loaded through a single provider registry:

- `mel_gpu_provider_enumerate(device) -> provider[]`
- `mel_gpu_provider_request(device, provider_kind, request_desc) -> { provider_handle, status }`
- `mel_gpu_provider_release(provider_handle)`

Provider kinds:

- `TemporalReconstruction`
- `FrameGeneration`
- `LatencyControl`
- `RayTracingOptimization`
- `GpuProfiling`
- `CrashDiagnostics`
- `ShaderAnalysis`
- `MobilePower`
- `XrRuntimeBridge`

Loading rules:

- Providers are runtime-loaded unless a platform requires static framework linkage.
- Provider ABI version, SDK version, driver version, GPU architecture, and granted caps are part of all pipeline and effect cache keys.
- Provider failure cannot invalidate the base device. It returns a U2 error and the engine falls back.
- Provider-owned resources use normal U1 handles where possible. Native handles are exposed only through U5 interop.
- Provider callbacks enter the U3 completion pump, not ad hoc threads.
- Licensing and redistribution constraints are build-system facts, not RHI facts. A build that does not ship a provider reports it absent.

### 9.3 Temporal reconstruction and frame generation (U32)

`Mel_Gpu_Reconstruction_Context` is a provider-backed object for temporal upscaling, denoising, ray reconstruction, and frame generation.

Provider enum:

- `EngineTaaU`
- `EngineSpatialUpscale`
- `NvidiaDlss`
- `NvidiaNis`
- `AmdFsr`
- `IntelXeSS`
- `AppleMetalFX`
- `QualcommSnapdragonGsr`
- `RuntimeXrReprojection`

Capabilities:

- `super_resolution = none | spatial | temporal | ml_temporal`
- `ray_reconstruction = none | denoise | provider_ml`
- `frame_generation = none | single_interpolated | multi_frame`
- `hud_separation = none | app_composited_after | provider_composited`
- `xr_safe = false | runtime_only | provider_certified`
- `hdr = none | sdr_only | hdr10 | sc_rgb | platform_hdr`

Creation:

`reconstruction_context_create(device, desc) -> future<context>`

`desc` contains provider preference order, internal render size, output size, color space, HDR metadata, motion-vector convention, depth convention, jitter convention, exposure convention, reactive-mask convention, and whether the output enters a normal swapchain or an XR runtime.

Per-frame dispatch:

`reconstruction_dispatch(context, frame_desc) -> future<Mel_Gpu_Texture_View>`

`frame_desc` must carry:

- color input.
- depth input.
- motion vectors, with declared scale and whether they are low-resolution or output-resolution.
- exposure or luminance texture.
- jitter current/prior.
- camera matrices current/prior.
- near/far planes.
- FOV per view.
- reactive / transparency / disocclusion masks where available.
- HUD/UI texture if provider can composite it safely, otherwise UI is composited after reconstruction.
- frame ID, present ID, and U19 pacing metadata.

Provider lowerings:

- NVIDIA: Streamline/DLSS for super resolution, ray reconstruction, frame generation, and Reflex coordination; NGX/NVAPI direct path remains a P2 provider.
- AMD: FidelityFX SDK for FSR super resolution and frame generation, with Anti-Lag coordination.
- Intel: XeSS super resolution, XeSS frame generation, and XeLL pacing.
- Apple: MetalFX spatial scaler, temporal scaler, frame interpolator, and temporal denoised scaler where available.
- Qualcomm: Snapdragon Game Super Resolution / GSR2 as a mobile spatial or temporal provider where available.
- Engine fallback: TAAU, CAS/RCAS-like sharpening, and a simple spatial upscaler.

Frame-generation rules:

- The engine never generates hidden presents. Every real and generated frame has a present ID, pacing record, and latency-marker record.
- UI/HUD and pointer layers are either provider-composited through an explicit provider input or composited after generated frames.
- Generated frames cannot consume game simulation state that does not exist. The game callback runs once per real simulation frame.
- XR frame generation is off by default. It is enabled only through an XR runtime-approved path or a provider whose caps report `xr_safe = provider_certified`; otherwise XR uses runtime reprojection/timewarp only.

### 9.4 Latency control providers (extends U19)

U19's latency markers become provider-backed:

- NVIDIA: Reflex through Streamline/NVAPI and Vulkan `VK_NV_low_latency2`.
- AMD: Anti-Lag through AGS/FidelityFX and Vulkan `VK_AMD_anti_lag`.
- Intel: Xe Low Latency.
- Platform: DXGI frame-latency waitable object, Metal display timing, OpenXR frame pacing, Web `requestAnimationFrame`.

Public API:

- `mel_gpu_latency_provider_request(device, swapchain_or_xr_session, preference[])`
- `mel_gpu_latency_set_mode(provider, mode)`
- `mel_gpu_latency_mark(provider, frame_id, point)`
- `mel_gpu_latency_sleep(provider, frame_id, before_input_sample_deadline)`
- `mel_gpu_latency_get_report(provider, frame_id) -> report`

Marker points:

- `FrameStart`
- `InputSample`
- `SimStart`
- `SimEnd`
- `RenderSubmitStart`
- `RenderSubmitEnd`
- `PresentSubmit`
- `PresentDisplayed`
- `GeneratedPresentSubmit`
- `GeneratedPresentDisplayed`

Rules:

- The sleep point is before input sampling, not after simulation has already started.
- Frame generation must register both real and generated presents.
- Device groups and multi-adapter must be opt-in per provider; if a provider cannot support them, it reports absent.
- Latency providers can suggest pacing. They cannot silently change swapchain image count, present mode, simulation cadence, or XR runtime timing.

### 9.5 Ray and path tracing optimization providers (extends U26)

U26 gets an optional `Mel_Gpu_Rt_Optimization_Profile`.

Caps:

- `shader_execution_reordering = none | shader_hint | hit_object | provider_native`
- `opacity_micromap = none | ext | provider_sdk`
- `displaced_micromesh = none | ext | provider_sdk`
- `ray_denoiser = none | engine | provider`
- `many_light_sampling = none | engine | provider`
- `ray_reconstruction = none | reconstruction_provider`
- `rt_shader_analysis = none | offline | live_capture`

APIs:

- `rt_provider_create(device, provider_kind, desc)`
- `rt_build_optimizer_create(provider, scene_desc)`
- `rt_build_optimizer_encode(cmd, optimizer, blas_or_tlas_desc)`
- `rt_shader_reorder_hint(shader, key_expr, flags)`
- `rt_denoise_dispatch(provider, frame_desc)`

Provider examples:

- NVIDIA: SER where exposed, Opacity Micro-Map SDK, displaced micromesh / RTX micro-mesh paths, RTXDI, NRD, DLSS Ray Reconstruction, Nsight shader and crash paths.
- AMD: SER where exposed through DXR/Vulkan, FidelityFX denoisers and frame-generation swapchain, Radeon Raytracing Analyzer, Radeon GPU Detective.
- Intel: SER where exposed through DXR/Vulkan, XeSS reconstruction where appropriate, Intel GPA and XeSS tooling.
- Apple: Metal ray-tracing lowerings and MetalFX denoised scaler where available.

Rules:

- AS compaction still follows the U26 new-handle protocol. A provider may suggest when to compact; it may not silently swap BLAS storage under a TLAS.
- SER/reorder hints are shader decorations or provider metadata. If unsupported, shaders run with normal ordering.
- Denoisers and ray reconstruction consume declared inputs only. They cannot sample arbitrary hidden renderer state.

### 9.6 GPU-driven and indirect work optimization providers (extends U15/U28)

Provider caps extend U15/U28:

- `device_generated_commands = none | ext | provider_native`
- `work_graphs = none | emulated | native`
- `meshlet_generation = none | compute | provider`
- `shader_table_compaction = none | provider`
- `indirect_count_fast_path = none | native | provider`

Lowerings:

- D3D12: ExecuteIndirect and Work Graphs where granted.
- Vulkan: `VK_EXT_device_generated_commands`; `VK_AMDX_shader_enqueue` where present.
- Metal: indirect command buffers and Metal 4 command model.
- WebGPU: browser-supported indirect only; no hidden command generation.

Rules:

- Provider-generated command memory is a normal U9 buffer with explicit U17 states.
- Any provider that writes commands must declare the exact buffer ranges it writes.
- The render graph sees generated-command producers and consumers as ordinary pass dependencies.

### 9.7 Mobile and tiler-specific optimization (U33, extends §4)

`Mel_Gpu_Tiler_Profile` is the tiler-specific complement to U30's vendor profile, exposed adjacent to U4 caps:

- `tiler = none | tile_based | tile_based_deferred | apple_tile | adreno_gmem | mali_tile | powervr_tile`
- `tile_memory = hidden | explicit | shader_visible`
- `memoryless_attachments = none | transient_attachment | native_memoryless`
- `local_read = none | subpass_input | dynamic_rendering_local_read | framebuffer_fetch | tile_shader`
- `foveation = none | fixed | dynamic | eye_tracked`
- `thermal = none | advisory | enforced_budget`

Graph compiler rules:

- Prefer keeping color, depth, and short-lived G-buffer attachments on tile.
- Prefer `DontCare` load/store where content is not needed.
- Prefer on-tile MSAA resolve over storing MSAA images.
- Avoid splitting a mobile render pass if the split forces a store/load round trip.
- Keep attachment feedback loops explicit.
- Treat a fullscreen pass on mobile as a bandwidth event, not a free operation.
- Prefer clustered/forward+ or tile-local deferred paths over storing large deferred buffers when the tiler profile says memory bandwidth is the bottleneck.

Provider paths:

- Apple: memoryless attachments, tile shaders/imageblocks where available, framebuffer fetch, MetalFX, Metal HUD/counter samples.
- Qualcomm Adreno: Vulkan multiview, fixed/dynamic foveation, `VK_QCOM_tile_shading` where present, Snapdragon GSR, Snapdragon Profiler / Adreno tooling.
- Arm Mali / Immortalis: Vulkan render-pass and memoryless discipline, Arm Performance Studio / Streamline counters, AFBC-friendly render-target use where the platform exposes it.
- Imagination PowerVR: tile-based deferred rendering profile, PVRTune/PVR tooling where available, store/load minimization.

Meta Quest-specific mobile budget:

- Standalone Quest targets Horizon OS on Android and uses OpenXR plus Vulkan as the Melody backend.
- The renderer must model 72 Hz as the minimum comfort target for store submission, with 90 Hz and 120 Hz modes as higher-budget caps when the runtime reports them.
- Frame budgets are expressed in milliseconds in U19, not only FPS: 72 Hz is about 13.9 ms, 90 Hz about 11.1 ms, 120 Hz about 8.3 ms.
- Dynamic foveation and dynamic resolution are coordinated so they do not fight each other. On Quest, platform dynamic foveation wins over generic dynamic resolution when both are requested.

### 9.8 Vendor tooling, counters, and crash diagnostics (extends U21/U24)

U21/U24 admit a `Mel_Gpu_Tooling_Provider` as the carrier for vendor capture, counter, and crash-diagnostics SDKs.

Capabilities:

- `capture = none | manual | programmatic`
- `counters = none | timestamp | hardware_basic | hardware_detailed`
- `crash_dump = none | device_removed | shader_marked | full_provider_dump`
- `shader_disassembly = none | offline | capture_linked`
- `markers = none | debug_labels | provider_labels`

APIs:

- `tooling_provider_request(device, kind, desc)`
- `tooling_capture_begin(provider, label)`
- `tooling_capture_end(provider)`
- `tooling_counter_set_enumerate(provider)`
- `tooling_counter_sample(cmd_or_queue, counter_set, label)`
- `tooling_shader_disassembly_export(pipeline_or_shader, desc)`
- `tooling_crash_metadata_attach(device, key, bytes)`

Provider examples:

- NVIDIA: Nsight Graphics, Nsight Aftermath, Nsight Perf SDK.
- AMD: Radeon GPU Profiler, Radeon Raytracing Analyzer, Radeon GPU Detective, Radeon Memory Visualizer, Radeon GPU Analyzer, GPUPerfAPI.
- Intel: Intel Graphics Performance Analyzers, XeSS Inspector where applicable.
- Apple: Xcode GPU capture, Metal Performance HUD, Metal counter sample buffers.
- Arm: Arm Performance Studio / Streamline.
- Qualcomm: Snapdragon Profiler and Adreno tooling.
- Imagination: PVRTune and PowerVR analysis tools.

Rules:

- Tooling providers never ship in release by accident; build config must opt in.
- Tooling labels must reuse U21 debug marker names so captures line up across tools.
- Crash metadata is bounded and privacy-safe by default. Native provider dumps are opt-in.

### 9.9 Vendor shader variant policy (extends U12/U13/U14)

Shader and pipeline cache keys gain:

- provider ABI versions.
- vendor and architecture profile.
- compiler fingerprint.
- requested subgroup width.
- matrix/tensor tier.
- root-record payload model.
- tiler profile where it changes shader lowering.
- reconstruction provider where shader outputs are provider-specific.

The engine may compile multiple variants:

- generic portable variant.
- vendor architecture variant.
- mobile tiler variant.
- XR multiview/foveated variant.
- reconstruction-provider variant.

Selection rules:

- Generic variant always exists unless the feature itself is vendor-only and explicitly requested.
- Vendor variants are selected only when caps and provider status match.
- Variant mismatch is a cache miss, not undefined behavior.
- Autotuning is permitted only if it writes deterministic cache records keyed by device/driver/compiler.

---

## 10. XR target family (U34)

XR is a first-class target family, not a swapchain mode. The RHI does not present XR images to an OS window. It renders into runtime-provided image sets and submits composition layers to an XR compositor.

### 10.1 Targets and non-goals

Targets:

- `DesktopOpenXR`: PC VR through OpenXR runtimes such as SteamVR, Meta Quest Link, Windows Mixed Reality / OpenXR-compatible runtimes, Varjo, Pimax, Vive, Monado, and similar conformant runtimes.
- `MetaQuestStandalone`: Horizon OS / Android on Quest, OpenXR runtime, Vulkan Melody backend.
- `AppleVisionPro`: visionOS, Compositor Services, Metal Melody backend.

Non-goals:

- OpenVR is not the primary API. It can be a P2 compatibility provider later.
- XR frame generation outside a runtime-approved path is off by default.
- The engine does not fake head tracking, hand tracking, passthrough, or foveation when the runtime does not expose it.

### 10.2 XR object model

New objects:

- `Mel_Xr_Instance`: owns OpenXR instance or platform XR bridge.
- `Mel_Xr_System`: selected headset/runtime/device profile.
- `Mel_Xr_Session`: running interaction session.
- `Mel_Xr_Space`: reference spaces and app-defined spaces.
- `Mel_Xr_View_Set`: per-frame view poses, projection matrices, recommended image size, visibility mask, foveation profile, and blend mode.
- `Mel_Xr_Image_Set`: borrowed runtime swapchain images wrapped as U10 textures/views.
- `Mel_Xr_Frame`: frame state from the runtime, including predicted display time and frame ID.
- `Mel_Xr_Composition_Layer`: projection, quad, cylinder, equirect, cube, passthrough, depth, and platform-specific layers.

Object relationships:

- U6 device selection and XR system selection are coordinated. On desktop OpenXR, the runtime may dictate the graphics adapter. On Quest and Vision Pro, the device is effectively fixed.
- U18 borrowed-image-set swapchains are the bridge between XR images and the RHI.
- U19 pacing source is the XR runtime frame loop, not the desktop display link.
- U20 render graph receives one pass DAG per XR frame, with per-view resource ranges.

### 10.3 XR frame loop

Portable frame loop:

1. Runtime wait begins the frame and returns predicted display time.
2. Runtime locates views for the requested reference space.
3. RHI wraps/acquires runtime swapchain images as borrowed U10 texture views.
4. Render graph records multiview or per-eye passes.
5. Queue submit returns the normal U3 future.
6. Runtime composition layers are submitted with color, depth, and optional motion/foveation metadata.
7. Completion futures gate image reuse; runtime ownership rules gate image release.

Rules:

- `Frame_Info.predicted_next_present_ns` is the XR predicted display time.
- Every per-eye view has its own jitter, projection, culling frustum, TAA history, and foveation center.
- Multiview rendering is preferred when the backend/runtime supports it. Per-eye rendering remains the fallback.
- Depth submission is first-class through OpenXR depth layers or platform equivalents.
- The app supplies stable world scale, near/far planes, and tracking-origin transforms so reprojection remains correct.

### 10.4 Desktop VR through OpenXR

Desktop VR uses OpenXR as the primary API.

Required features:

- OpenXR instance/session creation.
- Graphics binding for D3D12 and/or Vulkan, depending on runtime support.
- Stereo projection layers.
- Runtime-owned swapchain images.
- predicted display time from the OpenXR frame loop.
- action sets for controllers, hands, and haptics.

Important extensions to model:

- `XR_KHR_D3D12_enable`
- `XR_KHR_vulkan_enable2`
- `XR_KHR_vulkan_swapchain_format_list`
- `XR_KHR_composition_layer_depth`
- `XR_EXT_hand_tracking`
- `XR_EXT_eye_gaze_interaction`
- foveated / quad-view extensions where the runtime exposes them.

Desktop provider policy:

- D3D12 is the preferred Melody backend on Windows when the chosen runtime's D3D12 binding is strong.
- Vulkan is the preferred cross-platform backend and the Linux/OpenXR path.
- The runtime chooses final scanout timing. U19 observes and feeds the runtime, not the other way around.
- Runtime reprojection/timewarp is the default comfort path. Vendor frame generation is enabled only if the runtime/provider explicitly reports XR-safe support.
- Desktop mirror-window rendering is a separate ordinary swapchain and never drives XR pacing.

### 10.5 Meta Quest standalone

Quest standalone target:

- Platform: Horizon OS / Android.
- XR API: OpenXR.
- Melody GPU backend: Vulkan only for the RHI target. GLES is not a peer backend for this spec.
- Presentation: OpenXR projection layers, optional passthrough and overlay layers.
- Pacing: OpenXR frame loop with Quest refresh targets exposed to U19.

Quest-specific caps:

- `quest_refresh_rates`: runtime-reported 72/80/90/120 Hz set.
- `quest_foveation = none | fixed | dynamic | eye_tracked`.
- `quest_space_warp = none | app_space_warp`.
- `quest_passthrough = none | compositor_passthrough | camera2_rgb`.
- `quest_scene = none | anchors | scene_mesh | semantic_scene`.
- `quest_hand_tracking = none | hands | simultaneous_hands_and_controllers`.
- `quest_thermal = advisory | enforced_budget`.

Important extension families:

- `XR_FB_foveation`, `XR_FB_foveation_configuration`, `XR_FB_foveation_vulkan`, `XR_FB_swapchain_update_state`.
- `XR_META_foveation_eye_tracked` where hardware/runtime grants eye-tracked foveation.
- `XR_FB_display_refresh_rate`.
- `XR_FB_space_warp` where application space warp is granted.
- `XR_FB_passthrough` and companion geometry/color extensions where granted.
- `XR_EXT_hand_tracking` plus Meta multimodal extensions where granted.

Quest rendering policy:

- Keep passes tile-friendly and bandwidth-light.
- Prefer multiview.
- Prefer memoryless/transient attachments for depth and MSAA.
- Coordinate dynamic foveation, render scale, and reconstruction provider selection through one quality governor.
- Keep camera/passthrough access permission-gated and privacy-labeled. Passthrough camera frames are device-user data; importing them into U10 textures must carry a protected/privacy flag.
- Thermal pressure can force a quality-mode downgrade. This is a U19 event plus app policy, not a hidden engine decision.

### 10.6 Apple Vision Pro

Vision Pro target:

- Platform: visionOS.
- XR API: Compositor Services for fully immersive Metal rendering.
- Melody GPU backend: Metal.
- Presentation: Compositor Services `LayerRenderer` drawables wrapped as borrowed U10 texture views.
- Pacing: layer-renderer timing, not a window display link.

Vision Pro caps:

- `visionos_compositor = compositor_services`.
- `visionos_layered_drawables = stereo | capture_extended`.
- `visionos_foveation = runtime_managed | app_profiled` as exposed by Compositor Services.
- `visionos_passthrough = system_composited`.
- `visionos_input = hands | gaze_indirect | controller_optional | arkit_anchors`, depending on permissions and APIs used.
- `visionos_metalfx = none | spatial | temporal | frame_interpolator | temporal_denoised`.

Rules:

- Vision Pro is not routed through OpenXR in this spec.
- A fully immersive Metal app draws the scene content for both eyes; the system compositor owns final display timing and passthrough environment composition.
- `LayerRenderer` drawables are borrowed image sets. The engine never assumes it can retain them beyond the compositor's lifetime rules.
- Rendering must honor compositor-provided view transforms, projection, texture layout, pixel format, and timing.
- RealityKit/SwiftUI layers can coexist with the RHI path, but the RHI path is Compositor Services plus Metal.
- Eye/gaze and camera-derived data are privacy-gated. The RHI surfaces capability and permission status; it does not promise raw sensor access.
- MetalFX can be a reconstruction provider, but XR frame interpolation is enabled only if the compositor/provider reports it as valid for the immersive layer.

### 10.7 XR render graph requirements

U20 needs XR access records:

- `view_mask`: which views a pass reads/writes.
- `view_local_resources`: per-eye histories and foveation maps.
- `shared_view_resources`: resources shared across eyes.
- `late_latched_uniforms`: pose-dependent constants updated as late as the backend admits.
- `composition_layer_output`: projection/quad/cylinder/passthrough/depth layer records.
- `runtime_ownership`: acquire/wait/release state for runtime images.

Graph rules:

- Per-eye TAA history never aliases the other eye's history.
- Depth for reprojection is not optional when the runtime can consume it and the app renders 3D geometry.
- Motion vectors must be per-view and in the convention declared to the reconstruction/runtime provider.
- Foveation maps are per-view resources and are part of pipeline cache keys when they change shader lowering.
- XR render passes can render a mirror view, but mirror output is a dependent copy, not the primary output.

### 10.8 XR comfort and safety contract

The RHI enforces these as validation rules in debug and status warnings/errors in release:

- The frame loop must be runtime-paced.
- The app must not block the XR frame loop on unrelated file IO, shader compile, or asset streaming.
- The app must declare world scale.
- The app must declare tracking origin and recenter behavior.
- Runtime image ownership must be returned even when rendering fails; failed frames submit a safe fallback layer if the runtime allows it.
- Quality governors must prefer resolution/foveation/LOD reductions over missing frame deadlines.
- Any camera/passthrough/imported-environment data carries privacy/protected flags through U5/U10/U21 capture.

---

## 11. Debug and queries

### 11.1 Validation and debug wiring (U21)

**Per-backend validation routing:**

- **Vulkan** — `VK_LAYER_KHRONOS_validation` + `VK_EXT_debug_utils` messenger when `device.debug = true`; severity routed per VK message severity to `mel_log` levels (verbose→trace, info→info, warning→warn, error→error).
- **WebGPU** — `uncapturedErrorCallbackInfo` + `deviceLostCallbackInfo` installed **always**; errors are always worth surfacing. `debug = true` raises verbosity.
- **Metal** — validation is environment-driven (`MTL_DEBUG_LAYER` and newer Metal diagnostics where available); engine logs an info line at device creation noting it is env-driven. No fabricated programmatic toggle (MEL-ENGINE-VIII).
- **D3D12** — `ID3D12Debug` + `ID3D12InfoQueue` + DRED (Device Removed Extended Data) when debug, severity routed identically.

**Object naming and command-range labels first-class.** Every `Mel_Gpu_*` descriptor accepts a `.name`; backends apply it (`vkSetDebugUtilsObjectNameEXT`, Metal `setLabel:`, WebGPU `label`, D3D12 `SetName`). `cmd_begin_debug_marker(cmd, name, color)` / `cmd_end_debug_marker` for command ranges (Vulkan debug-utils labels, Metal `pushDebugGroup`, WebGPU `pushDebugGroup`, D3D12 PIX events). Free in release where names compile out.

**Failure logging at the site.** Every creation failure logs the underlying cause via `mel_log_error("gpu", ...)` carrying the `VkResult`, Dawn message, or `NSError`. Never silent NULL.

**Debug-only contract assertions** on recording calls (U2): `mel_assert` fires loudly in debug, compiles out in release (MEL-ENGINE-III).

**Leak detection at device destroy.** The slotmap-per-type identity model (U1) makes it trivial — any live slot at destroy is a leak; engine reports type, name (if set), and creation site (debug builds only).

**Performance counters** ride U24 queries; the debug system consumes them for built-in profiling reports.

**Frame capture first-class.** `mel_gpu_capture_frame(device)` and `_capture_for(device, n_frames)` lower to RenderDoc's in-app API, NSight Aftermath, PIX programmable capture, Metal `MTLCaptureManager`, and WebGPU debug captures. Tools that are loaded respond; others no-op. Pluggable — the app adds new tools without engine changes.

**GPU crash diagnostics first-class.** `VK_EXT_device_fault` for page-fault and hung-shader reports, AMD/NVIDIA Aftermath, Metal command-buffer error state on device-lost, D3D12 DRED. Engine consumes these on the device-lost callback and dumps a crash report — active passes, last-issued resource, page-fault address where available — to a log file. The difference between a useful field report and "the driver crashed, good luck." No deferral.

### 11.2 GPU queries (U24)

**Query types.** Timestamp (GPU clock at insertion point); occlusion (binary + counter); pipeline statistics (per-stage invocations, primitives generated, clip-tested); mesh/task statistics where the backend exposes them; acceleration-structure size / compaction / serialization size (RT cap-gated); video encode/decode/process counters where exposed; work-graph/node-dispatch counters where exposed; transform-feedback statistics (cap-gated, mostly legacy); **hardware performance counters** (`Mel_Gpu_Perf_Counter_Pool`) — `VK_KHR_performance_query` on Vulkan, D3D12 `ID3D12QueryHeap` performance heaps + AMD GPA / NVIDIA PerfKit / Intel Graphics Performance Analyzers interop, Metal `MTLCounterSet` / `MTLCommonCounter` set, no carrier on WebGPU. Counter enumeration is descriptor-driven (the user picks counter UUIDs from the device's published set), submission is pass-aware (some counters need multi-pass capture; `caps.performance_query = none | passes | streaming` reports which), and reading is async via the U24 resolve-future path. In-game profiler overlays consume this for ALU / cache / memory-bandwidth instrumentation without reaching through native interop.

**Pools.** `Mel_Gpu_Query_Pool` over `VkQueryPool` / D3D12 query heap / `MTLCounterSampleBuffer` / WebGPU `GPUQuerySet`. Engine-managed default with TLS pools handed to recording threads — same pattern as U15 command pools. **P2 escape**: explicit `query_pool_create(device, type, count)` for power users (custom pool sizes, cross-frame sharing).

**Recording.** `cmd_write_timestamp(cmd, pool, index)` for point queries; `cmd_begin_query` / `cmd_end_query` for range queries. Infallible per U2.

**Result retrieval is async-only.** `query_pool_resolve(pool, indices_range) → future<results>`. Engine emits the resolve copy (`vkCmdCopyQueryPoolResults` / WebGPU `resolveQuerySet`) into a readback buffer; the future resolves when that submission completes via the U3 pump and the readback maps. A `*_sync` wrapper is available for blocking tooling. **No raw `vkGetQueryPoolResults` path** — the resolve-to-buffer pattern is the only portable one (WebGPU has no CPU getResults; D3D12 needs the heap resolved to a buffer). Per P1's sync-impossible refinement, sync getResults would lie on WebGPU; async-only is honest.

**Frame-budget integration (U19).** The engine maintains an internal per-frame timestamp pair straddling submission boundaries; `Frame_Info.gpu_time_ns` reads it. The user does not manage this pool — engine-internal.

**Caps.** `caps.timestamp_query` carries a **quantization tier** — `native` (Vulkan/Metal/D3D12 period ns reported directly), `quantized_100us` (WebGPU `timestamp-query` as shipped in Chrome 121, quantized to 100 µs and exposed only on isolated contexts), or `none` — so consumers can branch honestly when sub-microsecond resolution is required. Plus `caps.timestamp_compute_and_graphics`, `caps.pipeline_statistics`, `caps.occlusion_precise`.

**Calibrated CPU/GPU timestamps are mandatory.** `VK_EXT_calibrated_timestamps`; Metal `GPUStartTime` / `kernelStartTime` / `presentedTime` against `mach_absolute_time`; D3D12 `IDXGISwapChain::GetFrameStatistics` plus calibrated counters. The engine **periodically resamples** a CPU/GPU pair (default every N frames, configurable) and fits a linear — piecewise-linear where drift is non-uniform — model over a sliding window; `timestamp_to_cpu_ns(gpu_ts)` returns the monotonic-clock-aligned timestamp through that model. A one-shot calibration at startup accumulates microseconds of skew per second because GPU and CPU clocks ride distinct crystal oscillators; periodic resampling keeps the alignment honest for hour-long traces. This is what makes GPU traces line up with CPU traces in Tracy / Perfetto / Chrome traces — the difference between "the GPU did something" and "this CPU event triggered this GPU work." WebGPU has no calibration primitive — `caps.timestamp_calibrated = none`, unaligned timestamps, honestly gated.

---

## 12. Implementation milestones

Phased so each milestone is shippable and the seams the next one depends on are proven in real use before being committed to.

**M1 — Skeleton + async spine.** Vulkan backend first, with **Vulkan 1.2 + feature probes** as the support floor, **Roadmap 2024 Milestone** as the preferred profile, and **Roadmap 2026 Milestone** over the 1.4 line as the ceiling path. Vulkan Profiles (VPK) declares preferred profile requests at device creation; if a profile is not granted and the app allows the 1.2 floor, the backend falls back to explicit feature probes. If the model maps cleanly to Vulkan's floor/profile/ceiling split, Metal and WebGPU follow without baking one API version into the RHI. Vulkan runs on macOS through MoltenVK / `vkCreateMetalSurfaceEXT`, so it is runnable on the dev machine. Deliverables:

- U1 typed handles + slotmap-per-type ownership; **direct / indirect handle family split with the slot-equals-index contract on direct handles**; capture-replay slot metadata threaded through every resource type.
- U2 status/result model + log routing.
- U3 reactor-driven future spine, completion pump, three completion primitives, `*_sync` wrapper; **cross-thread `mel_reactor_post` backpressure with coalescing high-water mark + 4× hard ceiling**.
- U4 immutable domain-structured caps + request-and-grant device creation, including false/absent reporting for shader numeric, media, RT/AS, work-graph, and interop domains before their command implementations land; **`adapter_type`, `luid`/`uuid`, `power_source`, `low_power_mode`, `internally_synchronized_queues`, `capture_replay`, `pipeline_robustness`, `mutable_descriptor_type`, `cluster_accel_struct`, `partitioned_tlas`, `tessellation`, `matrix_scope`, atomic-by-type, `sampler_feedback`, `sparse_buffer`, `shared_presentable_image`, `performance_query` all exposed at M1** so apps can branch from day one even when the command implementations land later.
- U5 outbound native-integration headers under `include/gpu/vulkan/`; inbound import scaffolding for buffers/textures (external memory + external semaphore import); **CUDA / OpenCL / GStreamer (DMA-BUF) interop paths named in the import descriptor, even if the corresponding extension wiring lands later**.
- U6 Instance/Adapter/Device three-object phased API on Vulkan; **`mel_gpu_device_create_default` returns a `Mel_Gpu_Device_Create_Future`**; **adapter LUID / UUID + adapter_type exposed**; **`adapter_removed`, `power_source_changed`, `low_power_mode_changed` events on instance**; headless support; no singleton.
- U7 availability/acquire queue model on Vulkan; submission completion futures; per-queue thread-safe submit; **`internally_synchronized` queue flag opt-in from day one (no lock when granted)**.
- U8 allocator on `modules/allocator/buddy` + dedicated allocations + per-frame ring; roles DEVICE/UPLOAD/READBACK; UMA detection; residency mechanism (budget query + explicit `make_resident`/`evict`, no engine-side eviction policy).
- U21 validation wiring + failure logging + object naming + leak detection.

The existing `apps/hello-gpu` triangle and cube run on the new RHI by M1's end. The current `modules/gpu` is retired.

**M2 — Resources, recording, rendering on both ceiling backends in parallel.** Vulkan **AND** D3D12 land in M2. D3D12 is co-primary with Vulkan because the architecture's ceiling is co-defined by Roadmap 2026 over the 1.2-floor / 1.4 line **and** Agility 1.619.3 / SM 6.9 retail (with 1.720-preview / SM 6.10 LinAlg named as the preview track); paper-testing one without the other would let an architectural flaw hide. Vulkan ceiling fast paths: `VK_KHR_unified_image_layouts`, `VK_KHR_pipeline_binary`, `VK_EXT_graphics_pipeline_library` (GPL), `VK_EXT_shader_object`, `VK_EXT_dynamic_rendering_unused_attachments`, `VK_KHR_fragment_shading_rate`, `VK_EXT_fragment_density_map`, `VK_KHR_present_wait2`/`present_id2`, `VK_KHR_swapchain_maintenance1`/`surface_maintenance1`. D3D12 retail fast paths: enhanced barriers (`ID3D12GraphicsCommandList7::Barrier` + `ID3D12Device10`), GPU upload heaps (`D3D12_HEAP_TYPE_GPU_UPLOAD`), dynamic resource binding (`ResourceDescriptorHeap` / `SamplerDescriptorHeap`), DXIL library composition (the GPL analog), CPU Timeline Query Resolves, AV1 encode, VRS Tier 2; preview-track items (SM 6.10 LinAlg, Batched Async Commands, Fence Barriers, Work Graphs 1.1 mesh nodes) are named for Agility 1.720-preview where the user opts in.

- U9 buffers with `buffer_write`, `buffer_map_async`, persistent ptr on native, transient ring slices (lifetime gated on consumer-submission completion futures).
- U10 textures + views with the full format enum, including video / multiplanar / external formats; the virtual texture handle architecture; mip-gen, blit, copy.
- U11 sampler dedup **plus static / immutable samplers** (D3D12 root-signature static samplers, Vulkan immutable samplers) — landed at M2 alongside U11, not deferred.
- U12 Slang offline shaders + bundle format + reflection extraction; per-entry required-feature masks; bundle container validation plus optional strict all-entry capability check; engine-shipped **`melody.binding` mixin module** so one resource-set declaration synthesizes both ceiling and floor root records; **`shader_create_from_bytecode` raw-passthrough peer for SPIR-V / DXIL / MSL / WGSL from day one (P2 peer to the Slang convenience)**; **Slang version pinned via `tools/build/vendor/slang/SLANG_VERSION.lock` and contributing to the bundle / pipeline-binary cache key**; tessellation control / evaluation stages reflected and exposed.
- U13 graphics + compute pipelines async-created on both backends; reflection-driven layouts; engine-managed pipeline persistence via `VK_KHR_pipeline_binary` + DXIL library archives with `VkPipelineCache` defensive-load fallback + P2 primitives; **`VK_EXT_graphics_pipeline_library` (GPL) + DXIL library composition** for fragmented compile; `VK_EXT_shader_object` pipeline-less lane where granted; sample-locations, conditional rendering, stencil export, conservative rasterization, depth-clamp-zero-one admitted cap-gated; **`pipeline_tess_create` with full tessellation state**; **`pipeline_robustness` selector**; **`Mel_Gpu_Indirect_Layout` (command-signature analog) for `cmd_execute_indirect`**.
- U25 shader-model / compute numeric caps + **subgroup-size control** + **matmul-profile descriptor with matrix-scope resolution** + **atomic-by-type / image-atomic-int64 / atomic-float caps**, exposed in caps and validated during pipeline creation.
- U14 per-type bindless tables on both backends; Vulkan advances `VK_EXT_descriptor_indexing` → `VK_EXT_descriptor_buffer` → `VK_EXT_descriptor_heap` as granted; D3D12 uses `ResourceDescriptorHeap` / `SamplerDescriptorHeap` + root-record descriptor indices from day one; per-backend root-record carrier pinned per §6.7; **`MissingBindlessSlot` distinguished from `MissingFeature`** in the pipeline-create status; **`Mel_Gpu_Mutable_Descriptor_Layout` admitted from day one (cap-gated)**; **`CAPTURE_REPLAY` flag wired through resource and heap creation** so RenderDoc / PIX / Aftermath captures of the bindless surface replay deterministically.
- U15 multithreaded command pools (TLS); single-use recording; full recording surface — including `cmd_draw_indirect_count` / `_indexed_indirect_count` / `cmd_dispatch_indirect_count`, `cmd_push_descriptors`, `cmd_bind_index_buffer` with size, `cmd_set_shading_rate`, `cmd_set_sample_locations`, `cmd_set_patch_control_points`, `cmd_begin_conditional_rendering` / `_end`, `cmd_aliasing_barrier`, `cmd_queue_ownership_release` / `_acquire`, `cmd_latency_mark`, `cmd_execute_indirect(layout, ...)` — and parallel render-pass recording.
- U16 declarative sub-pass topology + per-attachment load/store/resolve + memoryless attachments + attachment feedback loops + **VRS Tier 1 (per-draw) and Tier 2 (per-primitive + per-image)** + **foveation `static` tier** + multiview view-mask + motion-vector / reactive-mask attachment compatibility. Vulkan lowers to 1.4 `dynamic_rendering_local_read` + `VK_EXT_dynamic_rendering_unused_attachments` + `VK_EXT_attachment_feedback_loop_layout` / `dynamic_state` + `VK_KHR_fragment_shading_rate` + `VK_EXT_fragment_density_map`; D3D12 uses enhanced-barrier separate passes + VRS Tier 2.
- U17 timeline + binary semaphores; D3D12-style state barriers (lowered to Vulkan synchronization2 / D3D12 enhanced barriers natively); `VK_KHR_unified_image_layouts` fast path where granted; `cmd_aliasing_barrier`, `cmd_queue_ownership_release` / `_acquire`; subresource-range argument on `cmd_barrier`; fine-grained P2 escape; state-resync hook.
- U18 surface lifecycle states/events + **Android UI-thread synchronous release discipline**; **DPI / extent pinned (pixel_extent / point_extent / scale_factor)**; `Mel_Gpu_Output` enumeration on both backends; swapchain with HDR set + **HDR metadata setters** (`IDXGISwapChain4::SetHDRMetaData`, `VK_EXT_hdr_metadata`, macOS EDR); per-image render-finished with `presentFenceInfo` GC; present timing (`VK_KHR_present_wait2` / `present_id2`, `VK_EXT_present_timing` where available, `VK_KHR_swapchain_maintenance1`, **DXGI frame-latency waitable**); **tearing flag** (`DXGI_PRESENT_ALLOW_TEARING`); multi-swapchain; Wayland color-management protocol where granted on the Linux backend.
- U19 four-mode pacing source; native vsync wiring; **latency-marker primitive** (Reflex on D3D12+Vulkan-NV / Anti-Lag on AMD / XeSS-FG / platform-native); **thermal-pressure event** (iOS / Android / Apple Silicon Mac); **`power_source_changed` and `low_power_mode_changed` events** (independent of thermal); OpenXR-driven pacing as the P2 canonical example; `Frame_Info` carries current and prior-frame jitter offsets plus current power-source and low-power-mode.
- U24 timestamp + occlusion queries; calibrated timestamps; frame-budget integration; **hardware performance-counter query pool** (`Mel_Gpu_Perf_Counter_Pool`) with `VK_KHR_performance_query` + D3D12 / Metal counter-set lowering.

**M3 — Bleeding-edge and general-application capabilities.**

- U13 mesh + RT pipelines on both backends; U26 acceleration structures, AS build / update / compact / serialize commands following the **new-handle compaction protocol** per §6.6, engine-managed SBT with the **pipeline-bound regeneration discipline** + raw escape; `cmd_trace_rays_indirect` / `_indirect2` (`VK_KHR_ray_tracing_maintenance1`, DXR 1.1 indirect from compute); `VK_EXT_ray_tracing_invocation_reorder` (multi-vendor SER) + DXR 1.2 `HitObject` (required by SM 6.9) + opacity micromaps + **position fetch** (`VK_KHR_ray_tracing_position_fetch` / D3D12 `TriangleObjectPositions`) + **motion blur** (`VK_NV_ray_tracing_motion_blur` / D3D12 motion-instance) where granted.
- U26 **cluster acceleration structures and partitioned TLAS** — `Mel_Gpu_Cluster_Accel_Struct`, `Mel_Gpu_Partitioned_Tlas`, `cmd_build_cluster_accel_struct`, `cmd_build_blas_from_clusters`, `cmd_partitioned_tlas_update` — Vulkan via `VK_NV_cluster_acceleration_structure` + `VK_NV_partitioned_acceleration_structure` (NV-vendor at M3, KHR-additive when ratified), D3D12 via SM 6.9 cluster intrinsics + the Agility 1.619.3 cluster-build APIs (cross-vendor on NVIDIA RTX, AMD RDNA 4, Intel Arc B-series), Metal / WebGPU absent. **Emulated tier ships at M3** (`cluster_accel_struct = emulated` per §6.6) so AMD pre-RDNA 4, Intel Arc pre-B-series, and pre-KHR-ratification Vulkan devices honor the API surface with standard-BLAS-plus-primitive-ID-stride lowering — Slang capability atom `cluster_accel_struct_emulated` registered with the vendored Slang at M3. The Nanite-style cluster-LOD streaming path is reachable from M3 on the full Vulkan device matrix, not just NV; cost differs per cap tier, behavior does not.
- U10 **sampler feedback Tier 1 + Tier 2** — paired feedback textures and `WriteSamplerFeedback` shader API consumed by the streaming / virtual-texturing layer.
- U14 bindless **full** tier on supporting hardware — `VK_EXT_descriptor_heap` ceiling on Vulkan as ratification matures, `VK_EXT_descriptor_buffer` transitional path remains. **Fallback policy**: if `descriptor_heap` has not ratified by M3 ship, U14 ceiling on Vulkan rests on `descriptor_buffer` + `descriptor_indexing`; M3 ships without `descriptor_heap` and admits it later as purely additive (no API change).
- U15 indirect commands (`VK_EXT_device_generated_commands` + `ExecuteIndirect` already at M2; M3 admits the reusable-command-list tier where supported).
- U16 **foveation `gaze_driven` tier** (eye-tracker-driven density-map updates), Metal tile shaders on Apple Silicon (M4 backend).
- U27 media / video / camera frame import, YCbCr sampling, video-process commands, full Vulkan video codec set (`VK_KHR_video_decode_h264/h265/av1/vp9`, `VK_KHR_video_encode_h264/h265/av1`, `VK_KHR_video_maintenance1/2`, `VK_KHR_video_encode_quantization_map`); D3D12 video set including AV1 encode (24H2 / WDDM 3.2).
- **U29 asset IO and GPU decompression** — `Mel_Gpu_Io_Queue` lowering to D3D12 DirectStorage + GDeflate, Metal `MTLIOCommandQueue` + `MTLIOCompressionMethod`; Vulkan with `cpu_staged` fallback (vendor `gpu_decompress` where granted); WebGPU absent.
- U28 GPU-driven scheduling / work-graph API surface, native where the backend exposes it — `VK_AMDX_shader_enqueue` AMD-only on Vulkan, D3D12 Work Graphs 1.0 across vendors, mesh nodes preview-tier — and honestly gated elsewhere.
- U17 import of external sync primitives wired end-to-end.
- U21 frame capture integration + GPU crash diagnostics (DRED, Aftermath, `VK_EXT_device_fault`, Metal command-buffer error state).
- OpenXR compositor-layer submission surface from U18 wired end-to-end with the visionOS `cp_layer_renderer` integration.
- **U30 vendor and architecture profile** (`Mel_Gpu_Vendor_Profile` adjacent to U4 caps; vendor / architecture_family / driver / render_architecture / subgroup / memory / rt / matrix / presentation / tooling records).
- **U31 provider loading model** — `mel_gpu_provider_enumerate` / `_request` / `_release` registry, all nine provider kinds enumerated (`TemporalReconstruction`, `FrameGeneration`, `LatencyControl`, `RayTracingOptimization`, `GpuProfiling`, `CrashDiagnostics`, `ShaderAnalysis`, `MobilePower`, `XrRuntimeBridge`); provider ABI / SDK / driver / arch / granted-caps included in pipeline and effect cache keys.
- **U32 reconstruction context** — `Mel_Gpu_Reconstruction_Context` with `reconstruction_context_create` / `reconstruction_dispatch`; NVIDIA Streamline / DLSS, AMD FSR, Intel XeSS providers wired on Vulkan + D3D12; engine TAAU / spatial upscaler fallback; frame-generation present-ID and pacing-record discipline enforced.
- **U19 latency providers (§9.4)** — `mel_gpu_latency_provider_request` / `_set_mode` / `_mark` / `_sleep` / `_get_report` wraps the M2 latency-marker primitive; NVIDIA Reflex (Streamline/NVAPI, `VK_NV_low_latency2`), AMD Anti-Lag (AGS/FidelityFX, `VK_AMD_anti_lag`), Intel Xe Low Latency, and platform fallbacks (DXGI frame-latency waitable, Metal display timing, OpenXR frame pacing, `requestAnimationFrame`) selected through one preference list.
- **U26 RT optimization providers (§9.5)** — `Mel_Gpu_Rt_Optimization_Profile`; SER, opacity micromap, displaced micromesh, ray denoiser, many-light sampling, ray reconstruction, RT shader analysis caps; NVIDIA / AMD / Intel / Apple provider lowerings; `rt_provider_create`, `rt_build_optimizer_*`, `rt_shader_reorder_hint`, `rt_denoise_dispatch` APIs.
- **U15/U28 GPU-driven optimization providers (§9.6)** — `device_generated_commands`, `work_graphs`, `meshlet_generation`, `shader_table_compaction`, `indirect_count_fast_path` caps wired to D3D12 / Vulkan / Metal / WebGPU lowerings; provider-generated command memory is normal U9 buffer with declared write ranges.
- **U21/U24 tooling providers (§9.8)** — `Mel_Gpu_Tooling_Provider` for vendor capture, counter sets, crash diagnostics, shader disassembly; Nsight / Radeon GPU Profiler / Intel GPA / Xcode GPU / Arm Performance Studio / Snapdragon Profiler / PVRTune providers wired through `tooling_provider_request` and reusing U21 debug marker names.
- **U12/U13/U14 variant policy (§9.9)** — pipeline and effect cache keys gain provider ABI / vendor / compiler fingerprint / subgroup width / matrix tier / root-record payload / tiler profile / reconstruction-provider entries; variant table (generic / vendor / mobile-tiler / XR multiview-foveated / reconstruction) compiled where caps justify.
- **U34 XR target family — desktop OpenXR (§10.4)** — `Mel_Xr_Instance` / `_System` / `_Session` / `_Space` / `_View_Set` / `_Image_Set` / `_Frame` / `_Composition_Layer` objects; OpenXR graphics binding for D3D12 (`XR_KHR_D3D12_enable`) and Vulkan (`XR_KHR_vulkan_enable2` + `XR_KHR_vulkan_swapchain_format_list`); stereo projection layers, runtime-owned swapchain images, predicted display time, action sets; `XR_KHR_composition_layer_depth`, `XR_EXT_hand_tracking`, `XR_EXT_eye_gaze_interaction` plus foveated/quad-view extensions where granted; XR render-graph access records (`view_mask`, `view_local_resources`, `shared_view_resources`, `late_latched_uniforms`, `composition_layer_output`, `runtime_ownership`) and XR comfort/safety contract enforced.

**M4 — Metal and WebGPU backends to parity.** Metal backend with Metal 4 fast paths (`MTL4CommandBuffer`, `MTL4ArgumentTable`, `MTL4Compiler`, `MTL4MachineLearningCommandEncoder`, integrated `MTLResidencySet`, `MTLTensor`, Shader ML, Metal 4 low-overhead barriers, placement sparse) plus availability-checked earlier Metal lowerings where behavior remains faithful; WebGPU (Dawn native + Emscripten web) with the three cap profiles — Dawn-native, browser-core, Compatibility-mode — split per §2; Chrome 146+ memoryless attachments via `TRANSIENT_ATTACHMENT`; subgroups + dual-source-blending + float32-blendable / filterable + timestamp-query (100 µs quantized) consumed where granted; `importExternalTexture` + WebCodecs `VideoFrame` source-specific lifetime enforced; `GPUResourceTable` bindless landed forward-compatibly as the GPU Web working group's Milestone 3 surface lands. Also at M4:

- **U33 tiler profile** — `Mel_Gpu_Tiler_Profile` (tiler / tile_memory / memoryless_attachments / local_read / foveation / thermal axes) fully populated on Metal (Apple Silicon), Vulkan-Adreno, Vulkan-Mali / Immortalis, and Vulkan-PowerVR backends; render-graph compiler consumes tiler hints (on-tile attachments, `DontCare` load/store, on-tile MSAA resolve, store/load-round-trip avoidance, clustered/forward+/tile-local-deferred preference).
- **U32 reconstruction — Apple MetalFX provider** (spatial / temporal / frame interpolator / temporal denoised scaler where available) and **Qualcomm Snapdragon GSR / GSR2** mobile providers wired alongside the M3 desktop providers.
- **U34 XR target family — Meta Quest standalone (§10.5)** — Horizon OS / Android, OpenXR + Vulkan backend, projection / passthrough / overlay composition layers; Quest-specific caps (`quest_refresh_rates`, `quest_foveation`, `quest_space_warp`, `quest_passthrough`, `quest_scene`, `quest_hand_tracking`, `quest_thermal`) and extension families (`XR_FB_foveation*`, `XR_META_foveation_eye_tracked`, `XR_FB_display_refresh_rate`, `XR_FB_space_warp`, `XR_FB_passthrough`, `XR_EXT_hand_tracking` + Meta multimodal); 72/90/120 Hz frame-budget targets fed to U19; passthrough-camera privacy/protected flag propagated through U10/U21.
- **U34 XR target family — Apple Vision Pro (§10.6)** — visionOS, Compositor Services `LayerRenderer` drawables wrapped as borrowed U10 texture views, layer-renderer pacing; Vision Pro caps (`visionos_compositor`, `visionos_layered_drawables`, `visionos_foveation`, `visionos_passthrough`, `visionos_input`, `visionos_metalfx`); MetalFX as a reconstruction provider with `xr_safe` enforced for frame interpolation; eye/gaze and camera-derived data privacy-gated.

**M5 — Layer 1: render graph (U20).** Full design from this spec, including the XR access records from §10.7 (`view_mask`, `view_local_resources`, `shared_view_resources`, `late_latched_uniforms`, `composition_layer_output`, `runtime_ownership`) and the tiler-aware compilation rules from §9.7.

**M6+ — Additive capabilities.** Sparse texture residency (Vulkan 1.0 sparse feature flags + queue, D3D12 tiled resources, Metal placement sparse). Native ML / tensor backends beyond shader dispatch where future cooperative-tensor extensions land. Advanced capture integrations beyond M3. Higher-level streaming / residency policies outside the RHI layer. Explicit `*_cancel` future surface if a concrete need emerges. Further bleeding-edge features as they become required.

---

## 13. Module structure and dependencies

```
modules/gpu/
  include/
    gpu.h                 -- umbrella include (backend-clean)
    gpu/
      caps.h              -- Mel_Gpu_Caps + tiers (incl. adapter_type, power_source, low_power_mode,
                             internally_synchronized_queues, capture_replay, mutable_descriptor_type,
                             cluster_accel_struct, partitioned_tlas, tessellation, matrix_scope,
                             atomic-by-type, sampler_feedback, sparse_buffer, shared_presentable_image,
                             pipeline_robustness, indirect_state_change, performance_query)
      status.h            -- per-action status types are declared with each action
      result.h            -- helpers
      future.h            -- Mel_Gpu_*_Future, target_reactor, reactor-post backpressure policy
      format.h            -- Mel_Gpu_Format enum (shared by texture/swapchain/video); component mapping
      device.h            -- Mel_Gpu_Instance / Adapter / Device; adapter_removed, power_source_changed,
                             low_power_mode_changed events; adapter_type / luid / uuid / OpenXR LUID matcher
      output.h            -- Mel_Gpu_Output; per-adapter display enumeration, refresh range, HDR caps
      queue.h             -- queue role, request/release, submit, global priority,
                             internally_synchronized flag; Mel_Gpu_Queue
      memory.h            -- pool, placed, aliasing, residency; role enum; sparse buffer + texture
      buffer.h            -- Mel_Gpu_Buffer + Mel_Gpu_Buffer_Indirect + buffer usage + capture_replay flag
      texture.h           -- Mel_Gpu_Texture, Mel_Gpu_Texture_View + Mel_Gpu_Texture_View_Indirect +
                             texture usage enum + component mapping + sampler-feedback usage + capture_replay
      sampler.h           -- Mel_Gpu_Sampler + Mel_Gpu_Sampler_Indirect (incl. static / immutable sampler
                             descriptors)
      shader.h            -- Mel_Gpu_Shader, Slang bundle, reflection types, capability mask,
                             shader_create_from_bytecode raw-passthrough peer, tessellation stages
      accel_struct.h      -- Mel_Gpu_Accel_Struct + Mel_Gpu_Accel_Struct_Indirect;
                             Mel_Gpu_Cluster_Accel_Struct, Mel_Gpu_Partitioned_Tlas; BLAS/TLAS, cluster
                             build, partition-update, position fetch, motion blur, scratch/build/update/
                             compact/serialize
      pipeline.h          -- Mel_Gpu_Pipeline, pipeline_binary, GPL library lane, shader_object lane,
                             pipeline_robustness selector, pipeline_tess_create
      indirect.h          -- Mel_Gpu_Indirect_Layout (D3D12 command-signature analog); execute_indirect
      bindless.h          -- descriptor heaps, root-record layout, push constants, push_descriptor,
                             Mel_Gpu_Mutable_Descriptor_Layout, capture-replay heap flag
      command.h           -- Mel_Gpu_Command_List, record surface (incl. indirect-count, aliasing barrier,
                             queue-ownership transfer, conditional rendering, latency mark,
                             cluster-AS build, partitioned-TLAS update, patch-control-points dynamic)
      rendering.h         -- begin/end_rendering, sub-pass, attachments, feedback loops, VRS, foveation,
                             multiview, sample locations
      sync.h              -- Mel_Gpu_Sync; timeline/binary semaphores, state model, aliasing-barrier,
                             queue-ownership-transfer
      surface.h           -- Mel_Gpu_Surface; lifecycle events, UI-thread synchronous-release discipline,
                             pixel_extent / point_extent / scale_factor pinned
      swapchain.h         -- Mel_Gpu_Swapchain; HDR set + metadata setters, present timing, tearing,
                             external/borrowed image sets, shared-presentable-image,
                             OpenXR + visionOS layer descriptors
      pacing.h            -- Mel_Gpu_Render_Source modes; latency markers, thermal-pressure event,
                             power_source / low_power_mode events; Frame_Info pacing context
      media.h             -- video sessions, frames, color metadata, process/decode/encode, protected flag
      work_graph.h        -- Mel_Gpu_Work_Graph; GPU-side scheduling declarations
      vendor.h            -- Mel_Gpu_Vendor_Profile (vendor / architecture_family / driver /
                             render_architecture / subgroup / memory / rt / matrix / presentation /
                             tooling); Mel_Gpu_Tiler_Profile (tiler / tile_memory /
                             memoryless_attachments / local_read / foveation / thermal)
      provider.h          -- provider registry: enumerate / request / release; provider kinds
                             (TemporalReconstruction, FrameGeneration, LatencyControl,
                             RayTracingOptimization, GpuProfiling, CrashDiagnostics,
                             ShaderAnalysis, MobilePower, XrRuntimeBridge);
                             Mel_Gpu_Tooling_Provider (capture / counters / crash_dump /
                             shader_disassembly / markers)
      reconstruction.h    -- Mel_Gpu_Reconstruction_Context; provider enum (EngineTaaU /
                             EngineSpatialUpscale / NvidiaDlss / NvidiaNis / AmdFsr / IntelXeSS /
                             AppleMetalFX / QualcommSnapdragonGsr / RuntimeXrReprojection);
                             reconstruction_context_create / reconstruction_dispatch; frame_desc
                             (color/depth/motion/exposure/jitter/camera/masks/HUD)
      latency.h           -- mel_gpu_latency_provider_request / _set_mode / _mark / _sleep /
                             _get_report; marker points (FrameStart, InputSample, SimStart,
                             SimEnd, RenderSubmitStart, RenderSubmitEnd, PresentSubmit,
                             PresentDisplayed, GeneratedPresentSubmit, GeneratedPresentDisplayed)
      rt_optimization.h   -- Mel_Gpu_Rt_Optimization_Profile (SER / opacity_micromap /
                             displaced_micromesh / ray_denoiser / many_light_sampling /
                             ray_reconstruction / rt_shader_analysis); rt_provider_create,
                             rt_build_optimizer_create / _encode, rt_shader_reorder_hint,
                             rt_denoise_dispatch
      io.h                -- Mel_Gpu_Io_Queue; asset IO + GPU decompression (U29)
      query.h             -- Mel_Gpu_Query_Pool, Mel_Gpu_Perf_Counter_Pool
      xr.h                -- Mel_Xr_Instance / _System / _Session / _Space / _View_Set /
                             _Image_Set / _Frame / _Composition_Layer; OpenXR + visionOS
                             Compositor Services bridges; Quest and Vision Pro caps surfaces;
                             XR render-graph access records (view_mask, view_local_resources,
                             shared_view_resources, late_latched_uniforms,
                             composition_layer_output, runtime_ownership); XR comfort/safety
                             validation contract
      graph.h             -- (M5) render graph (incl. WorkGraph pass type)
      debug.h             -- capture, crash diagnostics, capture-replay discipline
      -- Opt-in per-backend native integration (NOT pulled in by gpu.h):
      vulkan/             -- include/gpu/vulkan/<integration>.h — typed accessors for VkDevice / VkQueue / VkCommandBuffer / VkImage / VkBuffer / external memory/sync import / raw-command injection / mid-record state-resync hook
      metal/              -- include/gpu/metal/<integration>.h — typed accessors for MTLDevice / MTL4CommandQueue / MTL4CommandBuffer / MTLTexture / MTLBuffer / MTLSharedEvent / CVPixelBuffer
      webgpu/             -- include/gpu/webgpu/<integration>.h — typed accessors for WGPUDevice / WGPUQueue / WGPUCommandEncoder / WGPUTexture / WGPUBuffer / external-texture import
      d3d12/              -- include/gpu/d3d12/<integration>.h — typed accessors for ID3D12Device / ID3D12CommandQueue / ID3D12GraphicsCommandList / ID3D12Resource / DXGI shared handles
  src/
    vulkan/                (M1-M3, Vulkan 1.2 floor / Roadmap 2024 preferred profile / Roadmap 2026 ceiling over 1.4)
      windows/             -- Win32 surface (VK_KHR_win32_surface), IOCP completion-port pump, Win32 external-memory/semaphore HANDLE import
      linux/               -- XCB / Wayland surfaces, Wayland color-management protocol, epoll on external_fence_fd / external_semaphore_fd
      macos/               -- MoltenVK surface via vkCreateMetalSurfaceEXT
      android/             -- ANativeWindow surface (synchronous UI-thread release), Choreographer pacing bridge, thermal API hook, AHardwareBuffer import
    d3d12/                 (M2-M3, FL12_0 floor / Agility 1.619.3 + SM 6.9 retail ceiling, 1.720-preview + SM 6.10 LinAlg preview track)
      windows/             -- HWND surface, IOCP completion-port pump, DXGI swapchain + IDXGISwapChain4 HDR metadata + DXGI frame-latency waitable, DirectStorage IO queue, DRED capture wiring
    metal/                 (M4, Metal availability-checked floor / Metal 4 ceiling)
      macos/               -- NSView / CAMetalLayer, CADisplayLink / CAMetalDisplayLink, NSWindow lifecycle, MTLIOCommandQueue
      ios/                 -- UIView / CAMetalLayer, CADisplayLink, UIScene phase bridge, NSProcessInfo.thermalState hook
      visionos/            -- cp_layer_renderer compositor layers, ARKit / Compositor Services integration
    webgpu/                (M4, Dawn native + Emscripten web)
      windows/             -- Dawn-native on Windows, SharedTextureMemory / SharedFence interop
      linux/               -- Dawn-native on Linux
      macos/               -- Dawn-native on macOS
      web/                 -- Emscripten WebGPU, requestAnimationFrame pacing, importExternalTexture + WebCodecs VideoFrame bridge (source-specific lifetime)
```

The `src/` tree carries the implementation directly — there is no `common/` folder. Each backend folder contains everything backend-specific that can stay common across native integrations (slotmap-typed handles, pump glue, allocator wrappers per-backend); per-OS native integrations live under `src/<backend>/<os>/`. Backend-agnostic primitives the engine consumes — slotmap, ring, coroutine futures, reactor, allocator — come from the listed dependencies, not from a co-located shared subtree.

The `include/` tree splits the public API into one header per type with `gpu.h` at the top level as the backend-clean umbrella. There is no `types.h` accretion file — every type lives next to its operations. Per-backend native integration is opt-in: an application that wants raw Vulkan access includes a header from `gpu/vulkan/`, and only that translation unit pays the `vulkan.h` cost; `gpu.h` never drags it in.

Dependencies (additions): `core`, `allocator`, `collection.slotmap`, `collection.ring`, `async.coroutine`, `async.signal`, `async.job` (M3 onward for compile pool), `reactor`, `log`, `debug` (when asserts are functional), `string`, `thread`. Tools: `tools/build` gains a Slang compile step (M2).

---

## 14. What this spec replaces and what survives

**Retired by M1.** `modules/gpu` in its current form — the one-shot device, single render pass, hardcoded format, single-frame-in-flight, per-action reactor source, NULL-on-failure model. All of it. The existing `mel_old/gpu.*` reference code remains for historical lookup.

**Survives.** The shape of the host-side render driver in `apps/hello-gpu/src/gpu_host.c` — reactor-attached render source, app lifecycle — is conceptually correct and ports forward; the underlying RHI it calls is replaced. The existing apps (`triangle`, `cube`, `lorenz`) are the first regression set for M1/M2.

---

## 15. Notes for the reader

This document is the *spec*. It pins decisions; it does not justify them re-litigably — each unit's resolution captures the trade-off considered and the principle it followed. The trackable per-unit resolutions live in the task list that backed this spec's drafting; cross-reference them when revisiting any unit.

When implementing, three tests catch most drift:

1. **"Can the app fully reimplement this?"** — if not, P2 primitives are missing.
2. **"Does this emulate, or does it lie?"** — if behavior diverges under load, gate honestly (P1).
3. **"Would adding this later force rework of existing API?"** — if yes, it cannot be deferred.

---

## 16. Vendor and XR source references

Recheck these primary sources whenever revisiting Sections 9 (vendor providers) or 10 (XR target family); version drift in these documents is the most common driver of staleness in those sections.

- Khronos OpenXR registry and OpenXR 1.1 spec, especially graphics bindings, depth layers, hand tracking, foveation, passthrough, and runtime conformance.
- Apple Compositor Services and MetalFX documentation for Vision Pro/visionOS.
- Meta Horizon OS Quest performance, foveation, passthrough camera, and OpenXR extension documentation.
- NVIDIA Streamline, DLSS, Reflex, Nsight Aftermath, and OMM/micromesh SDK docs.
- AMD FidelityFX, Anti-Lag, Radeon GPU Profiler, Radeon Raytracing Analyzer, Radeon GPU Detective, and GPUPerfAPI docs.
- Intel XeSS, XeSS-FG, XeLL, and Intel Graphics Performance Analyzers docs.
- Arm Performance Studio and Mali Vulkan best-practices docs.
- Qualcomm Snapdragon Game Super Resolution, Snapdragon Profiler, Adreno, and `VK_QCOM_tile_shading` docs.
- Imagination PowerVR/PVRTune docs.
