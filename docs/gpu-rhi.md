# Melody GPU RHI — Architecture Spec

This document specifies the redesign of `modules/gpu`. It replaces the current hello-triangle-tier abstraction with an explicit, capability-rich Render Hardware Interface (RHI) whose ceiling is the latest explicit graphics APIs (Vulkan 1.4 / D3D12 with Agility SDK 1.619 and Shader Model 6.9 / Metal 4), while still supporting earlier useful versions through capability checks and alternate lowerings. The spec is full-scope; the implementation is phased — Layer 0 (the RHI itself) lands first, the render graph (Layer 1) follows in a second milestone.

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

- **Vulkan — floor 1.2, ceiling 1.4.** Vulkan 1.4 is the preferred path: dynamic rendering, dynamic-rendering-local-read, synchronization2, descriptor-indexing, timeline semaphores, push descriptors, host image copy, line rasterization, and maintenance5/6 are treated as the modern idiom. Vulkan 1.2 and 1.3 remain supported through feature/extension checks: synchronization2 uses `VK_KHR_synchronization2` or falls back to older barriers; dynamic rendering uses `VK_KHR_dynamic_rendering` or falls back to render passes; dynamic-rendering-local-read uses its extension or falls back to subpasses/separate passes; host image copy, maintenance features, push descriptors, device-generated commands, calibrated timestamps, sparse residency, external memory/sync, and video are all gated. Vulkan below 1.2 is out of scope; the compatibility bought there is not worth the abstraction tax, and a separate OpenGL backend would be the more honest legacy path.
- **Metal — floor Metal 2/3 class where still useful, ceiling Metal 4.** Metal 4 has the first-class lowering: `MTL4CommandBuffer`, `MTL4ArgumentTable`, residency sets, the rewritten encoder model, and machine-learning encoder. Earlier Metal runtimes remain supported behind availability checks and backend shims where they can honor the behavior: classic command buffers/encoders, argument buffers where available, heap/resource-option differences, and older capture/debug paths. Features that require Metal 4 or Apple Silicon report absent on older OS/device pairs. The API shape does not collapse to Metal 2; the backend chooses the best faithful lowering.
- **D3D12 — floor Feature Level 12_0 / Shader Model 6-era runtime, ceiling Agility SDK 1.619 / Shader Model 6.9.** The Agility 1.619 + SM 6.9 path is first-class: enhanced barriers, work graphs, GPU upload heaps, mesh shaders, cooperative-vector intrinsics, dynamic resource binding (`ResourceDescriptorHeap`), wave-matrix intrinsics, and modern video paths. Older D3D12 runtimes remain supported through feature queries: resource binding tier, root signature version, wave ops, mesh/RT/VRS/sampler-feedback/work-graph tiers, enhanced-barrier availability, video support, and shader-model availability. D3D11 is not a degraded mode of this RHI.
- **WebGPU — the moving constrained target.** The runtime is current Dawn on native and current browser WebGPU on web; the spec is what it is at any given moment. WebGPU lacks much of what the other targets provide — true bindless, mesh/task shaders, ray tracing, persistent CPU mapping, push constants in core, command-buffer indirect, tile-local read, memoryless attachments, residency control, calibrated timestamps, pipeline cache primitive, hardware video encode/decode commands — so it is consistently the most-emulated backend under P1, and each unit calls out its specific gates. **WebGPU is a supported backend with first-class behavior within its constraints; it is never the ceiling of API shape.**

What this scoping buys: the latest API paths are not second-class bolt-ons, but older still-useful explicit runtimes are not discarded. Every backend reports the real feature set it can honor, and the same RHI surface either lowers faithfully, emulates faithfully, or gates loudly (MEL-ENGINE-VIII).

---

## 3. Cross-cutting primitives (Tier 0)

Every signature in the RHI inherits the five primitives in this tier.

### 3.1 Resource identity (U1)

Resources are referenced by **typed handles, value types, each a thin struct wrapping `Mel_SlotMap_Handle`**. One slotmap per resource type, owned by the `Mel_Gpu_Device`. Distinct wrapper types (`Mel_Gpu_Buffer`, `Mel_Gpu_Texture`, `Mel_Gpu_Texture_View`, `Mel_Gpu_Sampler`, `Mel_Gpu_Shader`, `Mel_Gpu_Pipeline`, `Mel_Gpu_Command_List`, `Mel_Gpu_Query_Pool`, `Mel_Gpu_Sync`, `Mel_Gpu_Swapchain`, etc.) give compile-time safety — a texture handle cannot be passed where a buffer is expected. The generation field turns use-after-free into a loud `alive()` failure instead of a dangling-pointer crash (MEL-ENGINE-VIII).

Every live slot carries a **stable bindless slot field** for U14. The default allocator makes that slot equal to the handle's integer index wherever the backend can support the direct mapping, so the fast path is one integer and no lookup. The equality is an optimization, not a semantic law: constrained backends, compact descriptor heaps, deferred-destroy windows, imported resources, and explicit user descriptor tables may use a separate table index while the handle identity remains stable. The rule is: identity never constrains residency, and descriptor pressure never invalidates identity (MEL-ENGINE-IX). Value handles are trivially copyable, threadable, and serializable.

Each slot carries an **`Owned`/`Borrowed` ownership flag** for the inbound-import path in U5. `Borrowed` slots are not freed on destroy, and their memory is never aliased by the allocator. Ownership is internal — imported and engine-created resources are indistinguishable at use sites (MEL-ENGINE-IX).

No uniform `destroy_any_handle`. Each resource type has its own `destroy`.

### 3.2 Status and result model (U2)

Every fallible action returns a **by-value `{ value, status }` result struct**. The `status` is **fully independent per action** — no shared base type, no shared discriminant field — so swapchain creation enumerates only swapchain outcomes, pipeline creation only pipeline outcomes, and no shared mega-enum accretes. Per-action `failed(status)` / `warned(status)` helpers exist; generic code lives without a shared severity field, since **handle validity is the usability signal** (the status is purely diagnostic).

The status carries warnings as a **per-action bitset** alongside a per-action error enum. A non-empty warning bitset coexists with a valid handle — request a non-blocking swapchain, none available, get a vsynced swapchain plus a warning naming the substitution. This is the success-with-degradation channel.

**Naming convention:** `Mel_Gpu_<Action>_Result` for the result struct, `Mel_Gpu_<Action>_Status` for the per-action status. *Status*, not *Error* — the term is precise when the carrier can report success-with-warning.

**Scope of fallibility.** Creation, submission, present, acquire, and map operations are in the error model. Hot-path recording calls (bind, draw, dispatch, barrier, begin/end rendering) are **contract calls**, not fallible ones — debug-asserted on misuse, validated by U21 in debug, branch-free and silent in release (MEL-ENGINE-III: no per-draw error tax).

Detailed human-readable causes (the `VkResult`, the Slang diagnostic, the Dawn message) always *also* go to `mel_log_error("gpu", ...)` at the failure site so the enum stays small while no detail is lost.

### 3.3 Awaitable / async execution (U3)

All creation operations return a future. The future is **reactor-driven** as the portable core; the job system is an accelerator, not the foundation.

**Why reactor-core.** The web target is single-threaded under default Emscripten and resolves WebGPU callbacks only when control returns to the browser event loop; Dawn on native is the same shape (callbacks fire only when `wgpuInstanceProcessEvents` is pumped). A fiber that parks waiting for a result that arrives only when the event loop runs deadlocks. Meanwhile every app already lives on the reactor; the job system is wired into nothing today.

**Completion pump.** One completion pump per device, attached to an owner reactor at device creation. **Not** a reactor source per action — the reactor is not built to carry hundreds of sources, and the completion-port shape is correct here. The pump owns an internal registry of in-flight operations.

**Pump cadence.** Decoupled from the frame loop. Prefer a **native-waitable poll** registered on the pump (Vulkan fence fds via `VK_KHR_external_fence_fd`, Windows IOCP handles) — lowest latency, no spin. Fall back to an **independent timer faster than the frame**, and only opportunistically also pump per-frame. Resource creation often happens at load and between frames; binding completion to 60 Hz would add up to 16 ms of dead time and stall when the app is not drawing.

**Future shape.** A `Mel_Gpu_<Action>_Future` holds a slot for the per-action result (U2) and a continuation. It carries a **target reactor**; when the op finishes, the pump routes the continuation there — directly if same thread, via `mel_reactor_post` if not. Register on thread A, resume on thread B is a built-in pattern.

**Three completion primitives**, unified behind one future contract — the integrations are inherently plural; forcing them behind one mechanism would be the mistake:

- **Native-waitable poll** — Vulkan fence fds, Windows IOCP handles, registered on the reactor via `mel_reactor_source_add_poll`.
- **Pump on tick** — Dawn `wgpuInstanceProcessEvents`, which exposes no waitable, only "drain me." A pump-source ticks it from the device's reactor.
- **Thread-callback bridged by `mel_reactor_post`** — Metal completion handlers / shared-event notification blocks, and **job-system completions** for CPU-bound work (Slang compilation, `vkCreateGraphicsPipelines` on a compile thread pool). The bridge is the same regardless of source.

**Consumer ergonomics.** Coroutine suspension via `async.coroutine` (already dt-driven on the reactor) for `await`-shaped user code. A plain continuation-callback form for non-coroutine code. A `*_sync` wrapper that pumps the reactor until resolved, for tooling and startup.

### 3.4 Capability / feature model (U4)

`Mel_Gpu_Caps` is immutable, queried from the device. It is a **feature database**, not a flat grab-bag. Top-level domains: `memory`, `queues`, `shader`, `compute`, `raster`, `ray_tracing`, `work_graphs`, `media`, `presentation`, `interop`, `queries`, and `debug`. Graduated features are **tiered enums** (`bindless = none | capped | full`; `multi_adapter = none | external_share | linked_device`; `ray_tracing = none | inline | pipeline`; `timeline = native | emulated`; `tile_local = none | emulated | native`; `work_graphs = none | emulated | native`; `video_decode = none | import_only | hardware`; `video_encode = none | import_only | hardware`; `video_process = none | shader_emulated | hardware`; `protected_media = none | decode_only | process_present`; `ml_tensor = none | shader_emulated | native`). Binary features remain bools only when the feature is truly yes/no.

Limits live inside the domain that owns them: descriptor-table sizes and heap residency under `memory/bindless`; max texture dimensions and sparse page shape under `memory/textures`; max workgroup size, shared memory, subgroup sizes, dispatch dimensions, and indirect-dispatch support under `compute`; wave/subgroup operations, numeric formats (`fp64`, `fp32`, `fp16`, `bf16`, `fp8`, `int64`, `int16`, `int8`), integer-dot-product, cooperative matrix/vector/tensor support, barycentrics, derivatives, memory scopes, and atomics under `shader`; codec/profile/bit-depth/chroma support under `media`; color spaces, HDR metadata, present modes, VRR, and timing under `presentation`. Caps also carry per-format support tiers (`sampled`, `storage`, `attachment`, `blend`, `linear_filter`, `ycbcr_sampled`, `video_decode_output`, `video_encode_input`, `external_only`).

**Enablement is request-and-grant only — no auto default.** Device creation requires an explicit desired feature set; the backend enables only the corresponding extensions; `caps` reports what was actually granted. Users name what they want. The battery-conscious build enables nothing it does not use (MEL-ENGINE-III).

### 3.5 Native interop — escape hatch and import (U5)

Two directions, both first-class.

**Outbound — the escape hatch.** Per-backend **opt-in interop headers** — `gpu/interop_vulkan.h`, `gpu/interop_metal.h`, `gpu/interop_webgpu.h`, `gpu/interop_d3d12.h` — expose typed accessors for every handle type: device, queue, command list, buffer, texture, sampler, pipeline, shader. The core `gpu/gpu.h` stays backend-clean; no application that includes the core surface drags in `vulkan.h` or `Metal/Metal.h`. Only a translation unit that deliberately includes the interop header pays the header cost, and the typed signatures give full safety.

The highest-value escape is grabbing the native command buffer **mid-recording** to inject an unwrapped command (an extension the engine has not yet wrapped). This is paired with a **U17 state-resync hook** — `cmd_assume_state(resource, state)` — so the user can hand state back coherently after the raw work. Past the hatch the user owns correctness (an explicit opt-out of validation, MEL-ENGINE-VIII).

**Inbound — import.** External native resources (`VkImage`, `MTLTexture`, `WGPUTexture`, external buffers, camera/video frames, multiplanar image planes, swapchain image sets) are wrapped into **uniform `Mel_Gpu_*` handles** with the internal `Owned`/`Borrowed` flag from U1. At use sites they are byte-for-byte indistinguishable from engine-created resources (MEL-ENGINE-IX). The flag governs only destroy (no underlying free), the allocator (no memory aliasing), and whether the resource is eligible for a global bindless slot.

External memory imports go through the U8 allocator (which wraps borrowed `VkDeviceMemory`/`MTLHeap`). External-swapchain image sets are first-class in U18 for OpenXR and video interop.

**External sync is unified with internal sync.** The U17 binary semaphore, timeline semaphore, and submission completion-future are each constructible **internally OR from an external native handle** (fd, Win32 `HANDLE`, `MTLSharedEvent`). The user waits and signals them at submission identically regardless of origin. Cross-API and cross-process interop — a video decoder, OpenXR, CUDA handing you a frame plus a semaphore — is "import the texture, import the semaphore, wait on it before your pass." Same primitives, one path. Capability-gated by `external_memory` and `external_sync` tiers, honestly absent on WebGPU.

---

## 4. Mobile and power (U22 cross-cutting constraint)

All mobile GPUs (Apple, Mali, Adreno, PowerVR) are **tile-based deferred** (TBDR); battery cost is dominated by **memory bandwidth**, not ALU. The architecture honors this directly (MEL-ENGINE-VI: a phone hath a battery; honor it). Two settled principles:

- **Tile-local read is first-class** in the rendering API, expressed as a **semantic dependency declaration** ("pass B reads attachment A on-tile"), never as a backend-specific primitive. Each backend lowers idiomatically: Metal tile shaders / framebuffer fetch where available; Vulkan `dynamic_rendering_local_read` on 1.4 or the extension, otherwise subpasses/separate passes; D3D12 separate passes + barriers; WebGPU separate passes (P1 emulate). Declare intent, the backend chooses the idiom.
- **Device class (TBDR vs IMR) is a runtime capability, not a compile-time class split.** One Vulkan binary spans Mali (TBDR) and desktop NVIDIA/AMD (IMR); one Metal binary spans Apple Silicon (TBDR) and Intel Mac (IMR). A `Desktop`/`Mobile` translation-unit split would mis-specialize. Cap tier `tile_local = none | emulated | native` reports the runtime fact. Per-platform backend TUs still dead-strip impossible code paths (the web TU contains no tile-local machinery at all), giving the compile-time leanness as a side effect of the per-platform build, not as a user-facing class split.

Specific mobile requirements land in their home units: per-attachment load/store/clear actions and on-tile MSAA resolve in U16; memoryless / transient attachments in U8 and U10; unified-memory-aware allocation that skips staging in U8 and U9; platform-vsync and on-demand frame pacing in U19.

---

## 5. Device foundation (Tier 1)

### 5.1 Instance / Adapter / Device (U6)

A three-object phased model, **nothing hidden**:

- **`Mel_Gpu_Instance`** — the enumeration root and validation-config owner (`VkInstance`, `WGPUInstance`; trivial on Metal). Created explicitly.
- **`Mel_Gpu_Adapter`** — a candidate GPU whose caps the user **inspects before committing** (`VkPhysicalDevice`, `WGPUAdapter`, `MTLDevice`-as-adapter). The adapter interface is **uniform across all backends**: enumerate adapters + inspect caps (features/limits/info) + power preference.
- **`Mel_Gpu_Device`** — created from a chosen adapter with the U4 request-and-grant feature set.

**WebGPU emulates the adapter interface equivalently** within its privacy constraints — requests the high-performance and low-power adapters and presents them as the enumerable set; caps from `adapter.features` / `adapter.limits`; identity from `adapter.info`. Emulated, not degraded.

**Power preference is first-class** (battery, MEL-ENGINE-VI). Adapter request takes `LowPower` / `HighPerformance` / `Default`.

**Headless is first-class.** Device creation never requires a surface. Compute-only, offscreen, and server rendering all create a device with no swapchain; presentation is a queried property when a surface later appears.

**No device singleton.** Multiple devices (multi-GPU, isolation) are allowed.

**Device groups / linked adapters are first-class.** Adapter enumeration may return both physical adapters and device-group candidates. Device creation can request one adapter or a group with explicit node/queue affinity. Resources may carry an optional residency/visibility mask; queues report their node. Vulkan lowers to device groups, D3D12 to linked adapters / node masks, Metal exposes separate devices with shared external-memory paths where possible, WebGPU reports no device-group support. The simple single-adapter path remains the default; multi-adapter is a peer, not a hidden special case.

**Device loss is a first-class lifecycle event.** Vulkan returns `VK_ERROR_DEVICE_LOST` from submit/wait; D3D12 reports device removal; Metal's `MTLDevice` becomes invalid; WebGPU's `device.lost` promise resolves. The RHI carries a `device_lost` callback on `Mel_Gpu_Device`, installed at device creation; on a loss event the engine routes the callback, **invalidates every handle issued by that device** (slotmap generations roll), **resolves every in-flight future with an `ERROR`-severity status** carrying a `device_lost` code, **tears down the U3 completion pump** for that device, and reports the loss reason through U21's crash-diagnostics path (DRED, Aftermath, `VK_EXT_device_fault`, Metal command-buffer error state) where available. Recovery is the app's job: re-enumerate adapters via the phased model above, create a new device, rebuild resources. The engine **never silently substitutes** a fresh device for the lost one — the contract is to fail loudly, then let the user reconstruct (MEL-ENGINE-VIII). The callback slot is part of the device contract from M1 day one, even if the app's recovery flow starts as "log and exit"; adding it later would change every device-destruction contract.

An optional `mel_gpu_device_create_default(power_preference, features)` is pure composition of the public phased calls — hides nothing.

### 5.2 Queues and submission (U7)

**Availability/acquire model**, not declare-up-front. `mel_gpu_queue_available(device, role)` reports remaining acquirable queues of `role` — on Vulkan the unrequested queues in role-capable families; on Metal a shared role-agnostic budget minus those in use (queues are untyped); on WebGPU virtualized over the single queue; on D3D12 the per-type budget. `mel_gpu_queue_request(device, role)` hands out an explicit, addressable `Mel_Gpu_Queue` object. Roles: `Graphics`, `Compute`, `Transfer`, `AsyncCompute`, `VideoDecode`, `VideoEncode`, `VideoProcess`. Media roles collapse onto compute/graphics or external platform engines where the backend has no addressable GPU queue; caps report `native`, `emulated`, or `import_only`.

Vulkan pre-creates the full hardware queue set at device creation; request allocates from that pool.

**Graceful fallback with U2 warning.** Request walks a role-compatibility chain (`graphics ⊇ compute ⊇ transfer`, media roles to native media queue → compute/graphics process path → import-only path) and returns the best available with a warning naming the substitution. Hard-fail only if no role-compatible queue exists at all. Acquire/release lifecycle — `queue_release` returns the queue to availability.

**Submission is the sync site and returns a completion future.** `queue_submit(queue, { command_lists[], wait[], signal[] }) → future<void>`. The `wait` and `signal` arrays are U17 timeline or binary semaphores (possibly imported, U5). The returned future (U3) resolves when the GPU finishes the batch — "await GPU completion" is the same await as everything else.

**Per-queue thread-safe submit** via an internal lock; the user submits from any thread. Recording (U15) is lock-free per-thread; the per-queue lock applies only at submit handoff.

Async-compute overlap is just "submit to the compute queue signaling a timeline value; have the graphics queue wait it." Serialized (emulated) on WebGPU with `caps.async_compute = false`.

### 5.3 Memory allocator and heaps (U8)

Built on `modules/allocator`, not raw `malloc`/`vkAllocateMemory`.

**Three allocation strategies**, each from an existing primitive:

- **Per-memory-type block suballocator** (the `buddy` allocator) — the VMA-equivalent.
- **Dedicated allocations** for large or render-target resources (`VK_KHR_dedicated_allocation`).
- **Per-frame linear ring** (`modules/allocator/ring`) for transient upload and uniform streaming.

**Roles:** `DEVICE` (GPU-only), `UPLOAD` (host-visible write-combined), `READBACK` (host-visible cached). **`MEMORYLESS`** is a flag on attachment-only resources (`MTLStorageModeMemoryless` / VK `LAZILY_ALLOCATED + TRANSIENT_ATTACHMENT`) — depth and MSAA stay tile-resident and never touch RAM (U22). WebGPU lacks memoryless; it falls back to normal allocation.

**UMA is automatic.** Unified-memory cap collapses `DEVICE`/`UPLOAD` onto one heap and makes `buffer_write` a direct write with no staging blit. The API does not change — the roles still exist and resolve to the same heap on Apple Silicon and mobile (U22). D3D12 **GPU upload heaps** (Agility 1.611+) collapse the same way on supporting hardware.

**Dual surface** (P2). Implicit path: a resource names a role and the engine allocates from default pools. Explicit path: `Mel_Gpu_Memory_Pool`, **placed resources**, and **memory aliasing** — peer of the implicit path, full power. Memory aliasing is required by U20's render graph for transient resource overlap.

**Full residency mechanism, but the policy lives in the app, not the RHI.** The RHI provides the **mechanism** — budget visibility, explicit residency operations, native residency primitives. It does **not** ship an LRU evictor at the RHI layer: a generic LRU lacks the semantic knowledge to distinguish a hero asset from a distant LOD, and the canonical failure mode is evicting the player's diffuse map to make room for a particle texture (MEL-ENGINE-V).

- **Budget tracking** via `VK_EXT_memory_budget`, DXGI budget, Metal `currentAllocatedSize` versus `recommendedWorkingSetSize`. Exposed as a query plus a **`budget_pressure` event** — the engine raises it upward to a user-registered callback when allocations approach or exceed budget, with the current/desired figures and the offending pool. The streaming / quality / asset system holds the semantic knowledge of what is worth keeping and decides what to unload.
- **Explicit residency operations** — `make_resident`, `evict`, Metal residency sets where available, D3D12 `MakeResident`. These are the primitives the app uses to act on the pressure signal, or to drive residency manually (P2).
- **No engine-side eviction policy.** Without a `budget_pressure` handler the engine reports OOM honestly (status severity `ERROR`) and refuses the allocation; the app installs whatever policy fits its content model on top of the primitives above.

WebGPU has no residency control — `caps.residency_control = none`; the residency calls are advisory/no-op, the budget-pressure event never fires, and the user is informed honestly.

**External memory import** (U5) wraps borrowed `VkDeviceMemory` / `MTLHeap`; never freed, never aliased.

**Suballocation on WebGPU emulates 1:1** — each allocation is one `WGPUBuffer`/`WGPUTexture`, since opaque `WGPUBuffer` cannot be suballocated. The suballocation *efficiency* degrades; behavior is intact (P1).

---

## 6. Resources (Tier 2)

### 6.1 Buffers (U9)

Operations:

- **`buffer_create({ size, usage, role, pool?, initial_data? }) → { Mel_Gpu_Buffer, status }`.** Usage flags: `VERTEX | INDEX | UNIFORM | STORAGE | STORAGE_TEXEL | INDIRECT | INDIRECT_COUNT | COPY_SRC | COPY_DST | SHADER_DEVICE_ADDRESS | AS_BUILD_INPUT | AS_STORAGE | SBT | VIDEO_BITSTREAM | VIDEO_PARAMETER | ML_TENSOR`. Storage, indirect, device-address, acceleration-structure, video-bitstream, and tensor usages are the essentials for GPU-driven rendering, compute, media, and ML-shaped workloads. `initial_data` does the right thing per role — direct write on UMA, persistent-map memcpy on native UPLOAD, staging blit on DEVICE.
- **`buffer_write(buf, data, size)`** — the **portable streaming primitive**, no map needed. Lowers to `queueWriteBuffer` on WebGPU, persistent-map memcpy on native UPLOAD, staging blit on DEVICE. Debug-asserts a 4-byte-granular convention on `offset` and `size` — WebGPU's `writeBuffer` / `MAP_WRITE` constraint, distinct from binding-alignment constraints (e.g., the 256-byte uniform-buffer binding offset alignment), which are reported through caps limits and validated at bind time.
- **`buffer_map_async(buf) → future<void*>`** — portable for read-modify-write and read-back; native resolves eagerly with the persistent pointer, WebGPU lowers to `mapAsync` resolved by the U3 pump.
- **`buffer_persistent_ptr(buf) → void*`** (UPLOAD only) — native zero-cost live pointer. **WebGPU hard-errors** (status severity = `ERROR`); `caps.persistent_map = none`. P1 sync-impossible: the contract cannot be honored synchronously on WebGPU, so do not fake it.
- **`buffer_alloc_transient(size, alignment) → { view, offset, mapped_ptr }`** — per-frame ring slice from U8. Lifetime is the frame; the ring resets at the boundary. Replaces the current "cycle N buffers manually" pattern.
- **`buffer_destroy(buf)`** — slot-frees; honors `Borrowed` (U5 import), no underlying free.

The buffer's default bindless slot is stored in the slot metadata from U1. It is normally equal to the handle index, but code must ask `buffer_bindless_slot(buf)` if it needs the actual shader-visible slot.

### 6.2 Textures and views (U10)

**`Mel_Gpu_Texture` and `Mel_Gpu_Texture_View` are separate handle types.** The texture owns dimensions, mips, layers, samples, planes, and external-origin metadata; the view subsets it (mip range, layer range, plane, format reinterpret, swizzle, view dimension override). The **view owns the bindless slot**, not the texture. That slot is usually the view-handle index but remains queried through `texture_view_bindless_slot(view)` for the same reason as buffers. A convenience `texture_default_view(tex)` yields a full-resource view for the trivial case.

**`texture_create`** accepts `dimension` (`1D`/`2D`/`3D`/`Cube`/`Array`), `extent`, `format`, `mip_levels`, `sample_count`, `usage` (`SAMPLED | STORAGE | ATTACHMENT | COPY_SRC | COPY_DST | TRANSIENT | VIDEO_DECODE_OUTPUT | VIDEO_ENCODE_INPUT | VIDEO_PROCESS_INPUT | VIDEO_PROCESS_OUTPUT | EXTERNAL_CAMERA | EXTERNAL_VIDEO`), the `memoryless` flag (U8/U22), `role`, optional `pool`, optional `initial_data`.

**Comprehensive format enum.** BGRA8 / RGBA8 in UNORM and SRGB; R / RG / RGBA in 8/16/32 float and int/uint; packed HDR `RGBA16F`, `R11G11B10F`, `RGB10A2`; depth/stencil `D32_FLOAT`, `D24_UNORM_S8_UINT`, `D16_UNORM`; compressed — BC1–7 (desktop), ETC2/EAC (Android baseline), **ASTC** (modern mobile, MEL-ENGINE-VI); multiplanar/video formats — `NV12`, `P010`, `P016`, `YUY2`, `UYVY`, opaque platform external formats, and per-plane view formats where the backend exposes them. Caps report per-format support tier (sampled/storage/attachment/blend/linear-filter/YCbCr-sampled/video-decode-output/video-encode-input/external-only); the user inspects before relying.

**YCbCr and external texture sampling are first-class.** A texture view may carry a color model (`RGB`, `BT.601`, `BT.709`, `BT.2020`), transfer (`linear`, `sRGB`, `PQ`, `HLG`), range (`full`/`limited`), chroma siting, and plane mapping. Shader sampling lowers to Vulkan sampler-YCbCr conversion, Metal/CoreVideo texture caches or native texture views, D3D12 video-process/sample paths, or WebGPU external-texture/import-plus-shader conversion. Camera and video frames are not demoted to ad hoc app-side blits.

**Commands:** copy region (texture↔texture, texture↔buffer), filtered blit, **mip-generation as a built-in** (every engine eventually writes this; just provide it, MEL-ENGINE-II).

**Texture handle is conceptually virtual** — physical memory decoupled from identity. Today every texture has full physical backing by default; the **sparse / partially-resident** capability (`VK_KHR_sparse_binding`/`sparse_residency`, D3D12 tiled, Metal sparse) is added later as purely-additive operations (`texture_bind_pages(tex, region, memory)`) without rewriting the existing API. This is the architecture choice, not a deferred implementation — designing for full scope means the model accommodates sparse without retrofit. WebGPU has no sparse — `caps.sparse = none`.

**Import** (U5) wraps external `VkImage`/`MTLTexture`/`WGPUTexture`, `CVPixelBuffer`, `AHardwareBuffer`, DXGI shared resources, and browser/native WebGPU external images as `Borrowed`; views over imported textures and planes are supported the same way; initial state and color metadata are declared at import.

### 6.3 Samplers (U11)

Descriptor: filter (min/mag/mip), wrap (s/t/r), max anisotropy (bounded by `caps.max_anisotropy`), compare op (shadow sampling), border color, lod range, optional YCbCr conversion reference for backends that bind conversion with the sampler. The sampler's default bindless slot is stored in U1 metadata and queried with `sampler_bindless_slot(sampler)`.

**Auto-deduplication.** `sampler_create(desc)` hashes the descriptor and returns a shared handle for any identical request — one internal sampler per unique descriptor across the device. Costs a small hash map; makes D3D12 sampler-heap pressure a non-issue; mirrors Metal's implicit dedup.

**Static / immutable samplers** are admitted into the pipeline layout (U13) so the optimization can be added later (D3D12 root-signature static samplers, VK immutable samplers) without breaking pipelines. No sampler import — samplers are trivial descriptors and importing buys nothing.

### 6.4 Shaders (U12)

Slang single-source. Full stage coverage from day one: `VERTEX`, `FRAGMENT`, `COMPUTE`, `MESH`, `TASK`, and the ray-tracing set — `RAYGEN`, `MISS`, `CLOSESTHIT`, `ANYHIT`, `INTERSECTION`, `CALLABLE`. **Work-graph node shaders** (D3D12 work graphs on the Agility/SM ceiling path) and **cooperative-vector intrinsics** (SM 6.9) are also expressible. Capability-gated where the backend lacks the stage or intrinsic.

**Multi-entry modules.** A single Slang source compiles to a module exposing N entry points; pipeline creation picks `(entry_name, stage)` pairs from it. Idiomatic Slang.

**Per-backend bundle** — single file per backend (the platform TU never carries another backend's blob). Contains the compiled blob plus **reflection metadata** Slang emits.

**Reflection-driven pipeline layouts** are the default — every binding's set/slot/type, vertex input layout, push-constant size, specialization-constant layout. Pipeline creation in U13 derives the layout from the bundle's reflection automatically; the shader IS the source of truth for its bindings. A manual layout declaration is available as P2 escape (no hiding).

One Slang authoring detail the spec acknowledges (current Slang limitation): `[shader("...")]` entry-point parameters map to push constants starting at offset 0 per stage, but Vulkan shares one push-constant range across all stages in a pipeline, and Slang does not currently expose offset control per entry-point parameter. The supportable pattern is a **global-scope `[[vk::push_constant]]` struct** that all stages reference; reflection picks it up and U13 lays it out as one shared range. Bundle generation in U12 validates that per-entry push-constant uses are reconcilable and emits an honest error otherwise. This is what U14's per-draw root pointer / index carrier rides on.

**Specialization constants first-class.** Slang `[specialization_constant]` lowers to Vulkan specialization constants / Metal function constants / WGSL `override` declarations — uniform API for baking compile-time variants without re-running Slang.

**Async creation (U3).** `shader_create(...) → future<shader>` — eagerly resolved for offline bundles (just bytes), genuinely async for runtime Slang compile (dispatched to the job system, completion bridged to the target reactor).

**Runtime compiler is app-opt-in at build time** — not Melody-dev-only. The app developer chooses what to ship: offline-only (no Slang runtime linked, `caps.runtime_shader_compile = false`), runtime-capable (Slang runtime bundled, caps true), or both (offline default with runtime fallback for generated/mod/AI-authored shaders). MEL-ENGINE-III (don't link what you don't need) + MEL-ENGINE-X (engine serves the user's vision).

**Hot reload** rides the runtime compile path plus the engine's file-watcher.

### 6.4.1 Shader model, compute, and ML/tensor caps (U25)

Compute is not treated as "rendering without color attachments." It has its own caps block and first-class ergonomics because Melody targets applications, tooling, media, simulation, visualization, and ML-shaped workloads as much as games.

**Shader/subgroup surface.** Caps expose subgroup/wave size range, supported stages, quad operations, vote/ballot/shuffle/arithmetic/clustered ops, derivative support in compute, memory scopes, memory semantics, atomics by type, barycentrics, clip/cull distances, dual-source blending, and interpolation controls. Slang reflection records the feature mask required by each entry point; pipeline creation fails early when an entry requires a feature the granted device lacks.

**Numeric surface.** Caps expose `fp64`, `fp32`, `fp16`, `bf16`, `fp8`, signed/unsigned `int64`/`int32`/`int16`/`int8`, packed dot products, saturation modes, denormal/rounding behavior where the backend reports it, cooperative matrix, cooperative vector, and tensor/ML encoder tiers. No precision is silently promoted or demoted. If a shader asks for `fp16` or `int8` math and the backend cannot honor it, creation fails instead of compiling an accidentally slower or less precise program.

**Dispatch surface.** Workgroup size, shared-memory size, dispatch dimension limits, indirect dispatch, device-address availability, device-side copy/fill, memory decompression, and async-compute overlap are all explicit. `cmd_dispatch` remains the universal primitive; specialized helpers (`cmd_dispatch_tensor`, `cmd_dispatch_cooperative_matrix` if they exist) are conveniences over the same shader/caps contract, not separate compute worlds.

**Lowering.** Vulkan uses subgroups, shader float/int controls, integer-dot-product, cooperative matrix/vector/tensor extensions where present. D3D12 uses the highest granted shader-model wave, cooperative-vector, and wave-matrix features. Metal uses the highest available shading-language features plus the Metal 4 machine-learning encoder where the operation is expressible natively. WebGPU exposes only its granted optional features (`shader-f16`, `subgroups`, and friends) and reports the rest as absent or shader-emulated.

### 6.5 Pipelines (U13)

**Per-type create**, distinct state spaces: `pipeline_graphics_create`, `pipeline_compute_create`, `pipeline_mesh_create`, `pipeline_rt_create`, `pipeline_work_graph_create` (D3D12 work graphs; cap-gated). No unifying tagged descriptor.

**Full state coverage.** Vertex input layout (reflection default, manual override); topology; full rasterization (cull, fill, front-face, depth bias, conservative raster cap-gated); full depth/stencil (per-face stencil ops, depth bounds); **per-attachment blend** (separate color/alpha, full factor/op enumeration, per-attachment color write mask). No current single-blend-state shortcuts.

**Maximize dynamic state by default** where the backend supports it (`VK_EXT_extended_dynamic_state{1,2,3}` or the promoted Vulkan 1.4 path; Metal encoder state; WebGPU fewer). Viewport, scissor, blend constants, stencil ref, depth bias, primitive topology, cull mode, front face, depth-test/write, depth-compare, stencil-test/op — all dynamic where possible. The practical lever against pipeline-permutation explosion. Caps report what is actually dynamic.

**Async creation** (U3). Metal `newRenderPipelineStateWithDescriptor:completionHandler:`, WebGPU `createRenderPipelineAsync`, Vulkan dispatched to a job-system compile pool, D3D12 pipeline-state-stream async via the Agility SDK. **Background compilation is how production avoids frame hitches**, free through the U3 spine.

**Mesh and RT pipelines from day one** (cap-gated). RT pipelines take raygen + miss[] + hit groups + callable[] plus recursion depth and SBT layout; the **shader binding table is engine-managed** (`Mel_Gpu_Sbt`, engine handles alignment and stride), with a raw-handle escape (P2) for users hand-rolling the buffer. SBT layout rules are fiddly and getting them wrong is silent miscompile (MEL-ENGINE-VIII). **Work-graph pipelines** (D3D12 baseline at this target) live alongside.

**Specialization constants** supplied at creation (U12).

**Pipeline cache / library / binary, engine-managed default, full app-override (P2).** Persistent on-disk cache keyed by descriptor hash + driver fingerprint + Slang bundle version, auto-loaded at device creation, auto-saved at device destroy. The same surface admits pipeline libraries and pipeline binaries where the backend exposes them (Vulkan pipeline binary / graphics pipeline library, D3D12 pipeline libraries and state streams, Metal binary archives). The app override is **not a path/disable toggle** — it is a full set of peer primitives: `pipeline_cache_create`, `_serialize`, `_load`, `device_set_pipeline_cache`, `_merge`, `_export_binary`, `_import_binary`. The app can persist anywhere (custom storage, encryption, network), implement its own key strategy, replicate the engine's behavior — 100% of the functionality is exposed. WebGPU has no cache primitive (P1 emulate-as-noop).

**Defensive load is mandatory.** Real Vulkan drivers misbehave on malformed pipeline-cache blobs: some crash on `pInitialData != NULL && initialDataSize == 0`; some load blobs from a 32-bit build into a 64-bit build despite matching `pipelineCacheUUID` (because vendors update the UUID manually and forget); some return `VK_ERROR_INITIALIZATION_FAILED` on any mismatch rather than discarding silently as the spec promises. The engine-managed cache therefore writes its own header — `magic` + `dataSize` + `dataHash` + `vendorID` + `deviceID` + `driverVersion` + `sizeof(void*)` + `pipelineCacheUUID` — persists via temp-file-plus-`rename` for atomicity, validates every header field on load, treats any validation failure or driver error as a cache miss, and falls through to an empty cache rather than propagating the error. The P2 primitives expose the same paranoia for app-managed caches; the spec calls it out explicitly so reimplementations do not skip it.

### 6.6 Acceleration structures and shader binding tables (U26)

Ray tracing is not complete without acceleration-structure resources. `Mel_Gpu_Accel_Struct` is a typed handle with the same `Owned`/`Borrowed` import rules as buffers and textures. Types: bottom-level, top-level, and backend-specific cluster/partitioned forms when caps report them.

**Build workflow.** `accel_struct_get_build_sizes(desc)` reports result/scratch/update sizes; `accel_struct_create({ type, size, role, pool?, flags })` allocates backing; recording exposes `cmd_build_accel_struct`, `cmd_update_accel_struct`, `cmd_copy_accel_struct`, `cmd_compact_accel_struct`, `cmd_serialize_accel_struct`, and `cmd_deserialize_accel_struct`. Build inputs accept vertex/index/AABB buffers, transform buffers, instance buffers, opacity micromaps, displacement/motion data, and procedural intersection records as capability-gated descriptors.

**State and memory integration.** AS scratch/result/storage buffers use U9 usage flags and U17 states (`AccelStructBuildRead`, `AccelStructBuildWrite`, `RayTracingAccelStruct`). The render graph sees AS reads/writes exactly like resources; it can schedule AS builds, inserts barriers, and aliases scratch memory where lifetimes permit.

**SBT stays paired but separate.** `Mel_Gpu_Sbt` remains the ergonomic shader-binding-table helper from U13, with raw-buffer escape. The helper handles backend alignment, group handles, stride, and table-region layout; power users can hand-build SBT buffers with `SBT | SHADER_DEVICE_ADDRESS` usage.

**Lowering.** Vulkan maps to `VK_KHR_acceleration_structure` / `VK_KHR_ray_tracing_pipeline` plus micromap/invocation-reorder extensions where granted. D3D12 maps to DXR acceleration structures, compaction, serialization, SER/HitObject/opacity-micromap tiers where available. Metal maps to Metal acceleration structures and intersection-function tables. WebGPU has no core RT/AS path and reports `ray_tracing = none`.

### 6.7 Resource binding (U14)

The binding model is **two layers, ceiling and floor**, both first-class. Caps report which is active; the API surface is the same; only the lowering differs. The ceiling is the modern pointer-based model (Aaltonen / Gibson Loon shape); the floor is the classical descriptor-table model that WebGPU and constrained runtimes can still honor.

**Ceiling — root pointer plus texture / sampler / AS heaps.** The shader receives **one 64-bit root pointer per draw or dispatch**, addressing a user-defined struct in GPU memory. That struct holds whatever the draw needs: **GPU pointers (buffer device addresses)** for buffer data, plus **32-bit indices** into a small set of device-wide descriptor heaps for textures, samplers, and top-level acceleration structures. The CPU writes the struct into a persistently-mapped UPLOAD buffer (U9, with `SHADER_DEVICE_ADDRESS` usage) and passes one pointer to the draw call; or a compute pass writes it, making GPU-driven rendering trivial without any API intervention. No per-draw push-constant index ceremony, no per-type buffer descriptor table, no per-frame rebinding. Buffers are addressed by pointer; textures, samplers, and AS by index into the heaps. The CPU side becomes "write a C struct and pass a pointer"; the GPU side gets fewer indirections and no descriptor-set copies.

The ceiling requires `caps.shader_device_address = true`, `caps.bindless = full`, and `caps.persistent_map = full`. The ceiling backends are Vulkan 1.2+ with `buffer_device_address` (core 1.2, mature in 1.4) plus `descriptor_indexing`; D3D12 SM 6.6+ with `ResourceDescriptorHeap` plus root-CBV/root-descriptor for the root struct; Metal 4 with `MTL4ArgumentTable` carrying `device T*` pointer-bearing argument-buffer references plus residency sets.

**Floor — per-type descriptor tables.** Where the ceiling caps are absent (WebGPU, older runtimes lacking BDA or persistent map), the same RHI surface lowers to **per-resource-class descriptor tables** — one for texture views, one for samplers, one for storage buffers, one for uniform buffers, one for acceleration structures, plus optional app-defined tables for classic descriptor-set layouts. Each resource slot stores its shader-visible table index; the default policy assigns table index = handle index where possible, preserving the zero-bookkeeping fast path. The API does not promise equality because descriptor residency, heap compaction, WebGPU caps, and borrowed resources may require a separate index. Per-draw indices ride **push constants** on Vulkan/Metal/D3D12, or a tiny uniform buffer with dynamic offset on WebGPU. This is the classical bindless-tables model — still first-class on the floor, never a degraded fallback.

**One source for both layers.** Slang authors the per-draw struct in either form — pointer-bearing for ceiling, index-bearing for floor; reflection (U12) tells U13 which lowering applies; the pipeline layout reflects either a single root-pointer push-constant range plus the heap declarations, or the descriptor-table layout. The user declares "this draw consumes this resource set" the same way. `caps.binding_model = root_pointer | descriptor_tables` is queryable so a hand-tuned variant can be written, but the simple path does not require it. Persistent slots remain: a resource stays at its pointer-or-index until destroyed; pipelines reference slots dynamically; no per-frame rebinding ceremony.

**Heaps as resources, with P2 introspection.** The texture / sampler / AS heaps are themselves resources the user can introspect — query the heap, read or write descriptor entries, swap entries. Full reimplementability per P2.

**Lowering, in detail.** Vulkan ceiling: BDA buffers + `descriptor_indexing` with `UPDATE_AFTER_BIND` for the heaps. Vulkan floor (no BDA or no descriptor-indexing): classical descriptor-set bind groups. D3D12 ceiling: `ResourceDescriptorHeap` direct indexing + GPU pointers + root-CBV for the root struct. D3D12 floor: classical descriptor heaps with root-signature tables. Metal ceiling: `MTL4ArgumentTable` with pointer-bearing argument buffers + residency sets. Metal floor: classical argument buffers / per-stage buffer/texture slots on earlier runtimes. WebGPU: floor only; capped bind groups with `caps.bindless = capped` and the actual cap reported. Slot allocation past the WebGPU cap **hard-errors** (P1 sync-impossible refinement) rather than faking capacity.

**Engine-managed lifecycle, classic descriptor sets as P2 peer.** Resource creation auto-populates the ceiling root-pointer struct fields, or the floor descriptor table, when caps allow it; destruction frees the slot after the backend-safe deferred-destroy interval. A user who wants explicit classic descriptor sets / bind groups declares a non-bindless pipeline layout, creates descriptor sets / bind groups, binds via `cmd_bind_descriptor_set` / `cmd_bind_bind_group`. The classic path is a peer, not a degraded fallback.

Static / immutable samplers from U11 admit later as an additive pipeline-layout feature.

---

## 7. Recording and rendering (Tier 3)

### 7.1 Command lists and pools (U15)

**Pools are engine-managed, per-thread per-queue, allocated via TLS.** Recording is lock-free per-thread; submission to a queue takes the queue's lock (U7). P2 escape: explicit pool exposure for power users who want manual reset or aliasing strategies.

**Single-use by default** — Metal and WebGPU mandate it; the portable contract is "record, close, submit, discard." **Reusable command lists** (record once, submit many) are a capability tier — Vulkan/D3D12 native; Metal indirect-command-buffer approximates; WebGPU is an honest gate, `caps.reusable_cmd_lists = false`.

**GPU-driven indirect commands first-class** (no deferral) with a capability tier: `cmd_execute_indirect` lowers to Metal `MTLIndirectCommandBuffer`, D3D12 `ExecuteIndirect`, Vulkan `VK_EXT_device_generated_commands`. WebGPU has partial support via `drawIndirect` / `dispatchIndirect`; tier reports it.

**Full modern recording surface**: `cmd_bind_pipeline`, `cmd_push_constants`, `cmd_draw`/`_indexed`/`_indirect`, `cmd_dispatch`/`_indirect`, `cmd_trace_rays` (RT), `cmd_draw_mesh_tasks` (mesh), `cmd_execute_indirect`, `cmd_dispatch_graph` (work graphs, cap-gated), `cmd_build_accel_struct` / `cmd_update_accel_struct` / `cmd_copy_accel_struct` (U26), `cmd_video_decode` / `cmd_video_encode` / `cmd_video_process` (U27), `cmd_copy_*`, `cmd_blit`, `cmd_clear_*`, `cmd_generate_mips`, `cmd_begin_rendering`/`_end` (U16), `cmd_barrier` (U17), `cmd_set_dynamic_state_*`, `cmd_begin_debug_marker`/`_end` (U21), `cmd_bind_descriptor_set`/`cmd_bind_bind_group` (U14 classic escape), `cmd_write_timestamp` / `cmd_begin_query` / `cmd_end_query` (U24).

Recording calls are infallible per U2 — debug-asserted on misuse, validated by U21 layers in debug, branch-free in release.

**Parallel render-pass recording first-class.** `cmd_begin_rendering_parallel(...)` returns N child contexts; each thread records into its own; the engine assembles them at end. Lowers to Vulkan secondary command buffers / Metal `MTLParallelRenderCommandEncoder` / D3D12 bundles; WebGPU serializes internally (P1 emulate). Designed in from day one — retrofitting parallel recording into a serial API is exactly the rework the no-deferral rule prevents.

### 7.1.1 GPU-driven scheduling and work graphs (U28)

Indirect commands are a command-buffer feature; **work graphs** are a scheduling feature. The API names both so D3D12 Work Graphs, Vulkan shader/device-generated-command paths, Metal indirect command buffers, and future backend-native graph dispatch do not get squeezed into one weak abstraction.

`Mel_Gpu_Work_Graph` owns node declarations, backing memory requirements, entry nodes, record layouts, maximum records, and recursion/depth limits. `pipeline_work_graph_create` validates the Slang node shaders and reflection against those declarations. Recording exposes `cmd_dispatch_graph(graph, entry, records_buffer, count_or_indirect)`; queue submission and U17 timeline waits/signals are unchanged.

Caps report `none | emulated | native`. `native` means the GPU can enqueue node work without CPU intervention. `emulated` means the engine lowers to compute/indirect dispatch loops with the same visible result but no promise of the same scheduling cost. If an app depends on native self-scheduling latency, it checks the cap and branches honestly. WebGPU core reports `none`.

P2 escape: native work-graph/indirect-command handles are exposed through U5, and users may issue backend-specific generated-command calls inside an interop section followed by `cmd_assume_state` for resources touched by the raw work.

### 7.2 Dynamic rendering and sub-passes (U16)

**Declarative topology, up-front.** `cmd_begin_rendering(pass_desc)` where `pass_desc = { attachments[N color + optional depth/stencil], subpasses[{ writes: [att...], reads_on_tile: [att...] }, ...] }`. Recording marks sub-pass boundaries with `cmd_next_subpass`.

The engine sees the full topology *before* recording starts and lowers optimally:

- **Tiler** — one merged encoder / render pass (mobile bandwidth win).
- **Vulkan 1.4 / extension path** — one dynamic-rendering pass with `dynamic_rendering_local_read` attachment-input transitions; Vulkan 1.2 fallback uses subpasses or separate passes.
- **D3D12** — separate passes with engine-inserted enhanced barriers between sub-passes.
- **WebGPU** — separate passes (P1 emulate).

The trivial case is one sub-pass with one write set — ergonomic. The U20 render graph later auto-generates `pass_desc` from a declared dependency DAG; the same primitive carries both layers.

**Per-attachment load action** (`Clear` / `Load` / `DontCare`) and **store action** (`Store` / `DontCare` / `Resolve`). **On-tile MSAA resolve** via `storeAction = Resolve` + resolve target; never store the multisample surface (U22).

**Memoryless flag** on attachment-only textures (U8/U10) — tile-resident only, never RAM.

**Multiview** (VR/AR), **VRS** (variable-rate shading), and **Metal tile shaders** are cap-gated bleeding-edge additions; the API admits them from day one.

### 7.3 Barriers and timeline synchronization (U17)

**Sync primitives.** **Timeline semaphore** (64-bit monotonic counter — Vulkan core 1.2 / Metal `MTLSharedEvent` / emulated on WebGPU via submit ordering) is the modern cross-queue workhorse. **Binary semaphore** exists for swapchain interop where required (Vulkan present cannot take a timeline). **No separate fence object** — CPU completion is the future returned by `queue_submit` (U7); the future *is* the fence. One less concept (MEL-ENGINE-IX).

All sync primitives carry **importable backing** (U5) — constructible internally or from external native handle, identical wait/signal at submission.

**State model — D3D12-style resource-state enum** as the primary public API: `Common`, `VertexBuffer`, `IndexBuffer`, `ConstantBuffer`, `ShaderResource`, `UnorderedAccess`, `RenderTarget`, `DepthWrite`, `DepthRead`, `CopySource`, `CopyDest`, `IndirectArgument`, `Present`, `RayTracingAccelStruct`, `AccelStructBuildRead`, `AccelStructBuildWrite`, `ShadingRateSource`, `VideoDecodeRead`, `VideoDecodeWrite`, `VideoEncodeRead`, `VideoEncodeWrite`, `VideoProcessRead`, `VideoProcessWrite`, `MlTensorRead`, `MlTensorWrite`. Engine maps each transition to the optimal `pipeline_stage_2`/`access_2` pair on Vulkan synchronization2 (core 1.3 or `VK_KHR_synchronization2` on 1.2) and falls back to legacy barriers only when behavior is equivalent; D3D12 lowers to enhanced barriers where available or legacy resource barriers where equivalent.

**P2 escape: explicit stage+access barriers** as a peer — `cmd_barrier_fine(resource, src_stage, src_access, dst_stage, dst_access, layout_old, layout_new)` — for power users needing synchronization2 / enhanced-barrier precision. Easy path but no gutting the power users.

**Explicit barriers at Layer 0** via `cmd_barrier`. Engine-internal state tracking per command list; mismatches assert in debug (MEL-ENGINE-VIII). The U20 render graph automates barriers later from a declared DAG; explicit remains the P2 peer.

**State-resync hook (U5):** `cmd_assume_state(resource, state)` lets a user who reached through the native interop hand state back coherently.

### 7.4 Swapchain, surface, present (U18)

**`Mel_Gpu_Surface` wraps a platform window** — `NSView*`, `ANativeWindow*`, `HWND`, canvas selector, EGL surface — decoupled from the swapchain so the surface outlives swapchain rebuilds.

**Swapchain configuration**: format, present mode (`Immediate` / `Mailbox` / `Fifo` / `FifoRelaxed`), image count, color space, alpha mode, usage flags. All negotiated through U2 status — request `Mailbox`, get `Fifo` with a substitution warning; request `HDR10`, get `sRGB` with a warning if unsupported. The user inspects and decides whether to accept.

**HDR / wide-color first-class** (no deferral). Color spaces cover `sRGB`, `Display-P3`, `Rec.2020`, `scRGB-linear`, `HDR10` (PQ + Rec.2020), `HLG`. Caps report supported spaces plus HDR metadata (peak nits, content luminance). Unsupported targets show up as substitution warnings.

**Format negotiation restricted to representable formats** — the prior BGRA8Unorm-vs-sRGB-mismatch bug does not recur because negotiation only ever picks a format `Mel_Gpu_Format` can name.

**Per-image render-finished sync** — array sized to image count, indexed by acquired image (fixes the old hazard of one shared semaphore being signaled before its prior present consumed it).

**External / borrowed image sets** (U5): `swapchain_create_borrowed(image_set, ...)` for OpenXR swapchain images and video interop; the swapchain wraps but does not own.

**Multi-swapchain per device.** No singleton; multi-window first-class.

**Acquire/present sync ownership.** Engine-auto by default — acquire returns a texture view, present takes it, semaphores handled internally. **P2 escape** returns the binary semaphores for users driving custom queue submission.

**Headless is not in this unit.** Offscreen rendering is render-to-texture (U10) + copy to `READBACK` buffer (U9). No virtual swapchain — surface-attached presentation only.

**Present timing first-class.** The swapchain exposes per-presented-frame timestamps and a `next_vsync_estimate()` query (`VK_KHR_present_timing`, Metal `CAMetalDrawable.presentedTime` + `MTLCommandBuffer.GPUStartTime`, D3D12 `IDXGISwapChain1::GetFrameStatistics`, WebGPU limited). U19 consumes these for vsync-aligned, on-demand, and frame-rate-capped pacing (MEL-ENGINE-VI).

### 7.5 Frame pacing (U19)

**Per-swapchain pacing source** replaces the current free-running reactor-timer render source. Wired to native vsync where available:

- Android **`Choreographer`**.
- iOS / macOS **`CADisplayLink`** (or **`CAMetalDisplayLink`** on Apple Silicon).
- Linux / Windows **`VK_KHR_present_wait`** / `present_timing`.
- Web **`requestAnimationFrame`**.

Fallback to a high-precision reactor timer where no native vsync exists.

**Four explicit modes**, each a distinct semantic primitive so the engine can apply mode-specific platform optimizations:

- **`Continuous`** — render every vsync. Game default.
- **`OnDemand`** — render only after `invalidate()`. UI default. The engine **suspends the vsync callback entirely** and re-arms on invalidate — real CPU/battery win on static scenes (MEL-ENGINE-VI), which a single-mode-with-knobs would miss.
- **`Capped(target_fps)`** — render at most `target_fps`, skipping vsyncs to hold the cap (e.g., 60 fps on a 120 Hz panel). The mobile battery lever.
- **`Adaptive(target_frame_ms)`** — target a frame-time budget; the engine exposes "budget left" each frame and the app decides how to scale quality.

**Frame budget exposed each frame.** `Frame_Info` passed to the render callback carries previous GPU time (from U24 timestamp queries straddling submission), CPU time, predicted next-vsync time (from U18 present-timing), headroom, and mode-specific state.

**Multi-swapchain pacing.** Each swapchain has its own pacing source; one reactor pumps all. A window in the background can auto-drop to `OnDemand` (battery on multi-window UIs).

**VRR / adaptive sync** is transparent at this layer — the underlying `Fifo` present mode handles it; pacing sees variable intervals.

**P2 escape**: the user supplies a custom pacing source built on the same underlying hooks the engine's modes use.

### 7.6 Media, video, camera, and image processing (U27)

Melody's GPU layer is an application RHI, not only a game renderer. Camera preview, video calls, video editors, streaming encoders, screen capture, remote desktop, scientific visualization, and ML preprocessing all need the same properties as rendering: explicit memory, explicit sync, color correctness, zero-copy import/export, and predictable fallbacks.

**Objects.** `Mel_Gpu_Video_Session` describes codec, profile, level, chroma format, bit depth, dimensions, rate-control class, decode/encode/process mode, and required parameter sets. `Mel_Gpu_Video_Frame` is a typed wrapper around one or more U10 textures/views plus color metadata. Bitstreams and parameter buffers are U9 buffers with `VIDEO_BITSTREAM` / `VIDEO_PARAMETER` usage. Sessions and frames are ordinary handles, nameable, importable where the backend allows it, and visible to leak detection.

**Decode/encode/process commands.** `cmd_video_decode`, `cmd_video_encode`, and `cmd_video_process` are recorded into command lists and submitted through U7 queues. Video processing includes scale, crop, rotate, deinterlace where supported, color-space conversion, tone-map, and format conversion. Backends without hardware video commands may lower process operations to compute shaders; decode/encode hard-gate if no faithful hardware or platform path exists.

**Interop is the primary path, not an afterthought.** Camera frames, OS decoder output, browser video frames, `CVPixelBuffer`, `AHardwareBuffer`, DXGI shared handles, OpenXR swapchain images, and platform capture surfaces import as `Borrowed` textures/frames with external sync. The user can sample them directly, process them, encode them, or export them again without a mandatory RGB staging copy.

**Protected content is an honest tier.** Some media paths expose protected sessions/surfaces that cannot be mapped, captured, exported freely, or sampled by arbitrary shaders. Caps report protected decode/process/present support separately. The RHI never strips protection to make a feature seem available; protected resources carry restrictions in their descriptors, validation enforces them, and capture/debug tools are told to stand down where required.

**Color metadata travels with the frame.** Every imported or created video frame carries color primaries, transfer function, matrix, range, chroma siting, mastering metadata where present, and orientation. Rendering, compute, and video-process commands consume that metadata explicitly; the engine never guesses Rec.709 vs Rec.2020 or full vs limited range.

**Lowering.** Vulkan uses `VK_KHR_video_queue` plus decode/encode codec extensions and sampler-YCbCr conversion where granted. D3D12 uses Video Decode/Encode/Process command lists and shared DXGI resources where supported. Metal uses CoreVideo/VideoToolbox/AVFoundation interop plus Metal textures and Metal compute/ML/video-capable encoders where appropriate. WebGPU supports external texture/video-frame import and shader/compute processing where the runtime exposes it, but has no core hardware decode/encode command model; caps report `import_only` or `none`.

**P2 escape.** Apps may bypass the engine's session helpers and import platform decoder/encoder outputs directly through U5. The primitives exposed here are sufficient to reimplement the helper: frame import/export, plane views, color metadata, sync import/export, video queues, bitstream buffers, and process commands.

---

## 8. Render graph (Layer 1, U20)

**Spec full, implementation phased.** Layer 0's API does not change when the graph lands — the graph generates inputs to Layer 0 (`pass_desc` for U16, barrier sequences for U17, aliasing patterns for U8, queue/timeline assignment for U7). It is purely additive; users continue to use Layer 0 directly if they want (P2 escape).

### 8.1 Pass types and resources

Pass types — **`Graphics`** (with U16 sub-pass topology fully expressible), **`Compute`**, **`Copy`**, **`RayTracing`**, **`AccelerationBuild`**, **`VideoDecode`**, **`VideoEncode`**, **`VideoProcess`**, and **`WorkGraph`**. Each pass declares input resources (read), output resources (write), and a **record callback** invoked at execution with the command list and resolved physical resources. The extra pass types are not special render-graph magic; they let the compiler choose the correct U7 queue, U17 states, query scopes, and aliasing rules without hiding the underlying Layer 0 primitives.

Three resource categories:

- **Transient** — graph-local; engine aliases memory between non-overlapping lifetimes via U8 placed allocations.
- **Imported** — handles from outside the graph (U9/U10 persistent resources, U18 swapchain image).
- **Exported** — named outputs available to subsequent code or another graph.

Resources are declared with full descriptors (extent / format / usage); the engine decides physical backing during compilation.

### 8.2 Compilation phases

All automatic from declared dependencies:

1. **Dependency DAG construction** from declared reads/writes.
2. **Pass culling** — passes whose outputs are transitively unused are removed.
3. **Resource lifetime analysis** — when each transient is first written, last read.
4. **Transient memory aliasing** — non-overlapping resources pack into shared physical allocations (U8).
5. **Automatic barrier synthesis** — generates U17 state transitions between passes; the user writes zero barriers. The synthesizer emits **split barriers** (signal-event-after-producer / wait-event-before-consumer; lowering to `VkEvents` on Vulkan, fenced barrier batches on D3D12 enhanced barriers, encoder boundaries on Metal) wherever the lowering supports it, so independent work between producer and consumer can fill the gap instead of stalling on a global pipeline drain. A preceding **pass-reordering step** (Granite-style baker) maximizes the distance between producer and consumer wherever reordering is safe, so split-barrier overlap actually pays off. On backends that cannot split a barrier (WebGPU implicit barriers; older D3D12 legacy resource barriers), the synthesizer collapses to a single full barrier and the optimization simply does not apply (P1 emulate).
6. **Sub-pass merging** — contiguous compatible passes collapse into one U16 render pass with tile-local reads (mobile bandwidth win, MEL-ENGINE-VI).
7. **Queue assignment + async-compute / video / work-graph scheduling** — passes that can overlap dispatch to compute, media, or native work-graph queues where caps allow; U17 timeline semaphores inserted automatically.

### 8.3 Execution and caching

The compiled plan is an ordered list of `(queue, pass, barriers, sub-pass-merge group)`. The engine walks it, invoking each pass's record callback with the resolved physical resources. The user records draws/dispatches; no manual barriers, no sub-pass declarations, no aliasing bookkeeping.

**Caching.** Graph hashed by topology + descriptors; identical hashes reuse the compiled plan. Per-frame rebuild is the default (cheap); persistent compiled-once graphs are supported for unchanging pipelines.

**Conditional passes** carry a runtime predicate and are skipped at execution.

**External imports/exports** declare initial/final state (U5 state-resync hook).

**Debug introspection.** The compiled plan is inspectable — pass list, resource lifetimes, aliasing decisions, barriers, sub-pass merge groups, queue assignment timelines — for profilers and visualizers.

### 8.4 Builder API

**Function-pointer two-phase, builder-style.** A per-pass descriptor struct:

```idris
record GraphPassDesc where
  name       : String
  reads      : List GraphResource
  writes     : List GraphResource
  recordFn   : CommandList -> ResolvedResources -> IO ()
  userData   : Ptr ()
```

Passes are added via `mel_gpu_graph_add_pass(g, desc)`. Resources declared symbolically (`Mel_Gpu_Graph_Resource` is a graph-local handle, resolved to a physical resource after compilation). C-idiomatic; no closures required; easy to serialize, inspect, and diff.

### 8.5 P2 escape

Skip the graph entirely. Use Layer 0 directly — manual U16 sub-pass topology, manual U17 barriers, manual U8 pools, manual U7 submission. The graph is one path; Layer 0 manual is the peer.

---

## 9. Debug and queries

### 9.1 Validation and debug wiring (U21)

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

### 9.2 GPU queries (U24)

**Query types.** Timestamp (GPU clock at insertion point); occlusion (binary + counter); pipeline statistics (per-stage invocations, primitives generated, clip-tested); mesh/task statistics where the backend exposes them; acceleration-structure size / compaction / serialization size (RT cap-gated); video encode/decode/process counters where exposed; work-graph/node-dispatch counters where exposed; transform-feedback statistics (cap-gated, mostly legacy).

**Pools.** `Mel_Gpu_Query_Pool` over `VkQueryPool` / D3D12 query heap / `MTLCounterSampleBuffer` / WebGPU `GPUQuerySet`. Engine-managed default with TLS pools handed to recording threads — same pattern as U15 command pools. **P2 escape**: explicit `query_pool_create(device, type, count)` for power users (custom pool sizes, cross-frame sharing).

**Recording.** `cmd_write_timestamp(cmd, pool, index)` for point queries; `cmd_begin_query` / `cmd_end_query` for range queries. Infallible per U2.

**Result retrieval is async-only.** `query_pool_resolve(pool, indices_range) → future<results>`. Engine emits the resolve copy (`vkCmdCopyQueryPoolResults` / WebGPU `resolveQuerySet`) into a readback buffer; the future resolves when that submission completes via the U3 pump and the readback maps. A `*_sync` wrapper is available for blocking tooling. **No raw `vkGetQueryPoolResults` path** — the resolve-to-buffer pattern is the only portable one (WebGPU has no CPU getResults; D3D12 needs the heap resolved to a buffer). Per P1's sync-impossible refinement, sync getResults would lie on WebGPU; async-only is honest.

**Frame-budget integration (U19).** The engine maintains an internal per-frame timestamp pair straddling submission boundaries; `Frame_Info.gpu_time_ns` reads it. The user does not manage this pool — engine-internal.

**Caps.** `caps.timestamp_query` (period ns), `caps.timestamp_compute_and_graphics`, `caps.pipeline_statistics`, `caps.occlusion_precise`. WebGPU timestamp-query is an opt-in feature reflected in caps.

**Calibrated CPU/GPU timestamps are mandatory.** `VK_EXT_calibrated_timestamps`; Metal `GPUStartTime` / `kernelStartTime` / `presentedTime` against `mach_absolute_time`; D3D12 `IDXGISwapChain::GetFrameStatistics` plus calibrated counters. The engine **periodically resamples** a CPU/GPU pair (default every N frames, configurable) and fits a linear — piecewise-linear where drift is non-uniform — model over a sliding window; `timestamp_to_cpu_ns(gpu_ts)` returns the monotonic-clock-aligned timestamp through that model. A one-shot calibration at startup accumulates microseconds of skew per second because GPU and CPU clocks ride distinct crystal oscillators; periodic resampling keeps the alignment honest for hour-long traces. This is what makes GPU traces line up with CPU traces in Tracy / Perfetto / Chrome traces — the difference between "the GPU did something" and "this CPU event triggered this GPU work." WebGPU has no calibration primitive — `caps.timestamp_calibrated = none`, unaligned timestamps, honestly gated.

---

## 10. Implementation milestones

Phased so each milestone is shippable and the seams the next one depends on are proven in real use before being committed to.

**M1 — Skeleton + async spine.** Vulkan backend first, with Vulkan 1.2 as the support floor and Vulkan 1.4 as the preferred path. If the model maps cleanly to Vulkan's floor/ceiling split, Metal and WebGPU follow without baking one API version into the RHI. Vulkan runs on macOS through MoltenVK / `vkCreateMetalSurfaceEXT`, so it is runnable on the dev machine. Deliverables:

- U1 typed handles + slotmap-per-type ownership.
- U2 status/result model + log routing.
- U3 reactor-driven future spine, completion pump, three completion primitives, `*_sync` wrapper.
- U4 immutable domain-structured caps + request-and-grant device creation, including false/absent reporting for shader numeric, media, RT/AS, work-graph, and interop domains before their command implementations land.
- U5 outbound interop headers (`interop_vulkan.h`); inbound import scaffolding for buffers/textures (external memory + external semaphore import).
- U6 Instance/Adapter/Device three-object phased API on Vulkan; headless support; no singleton.
- U7 availability/acquire queue model on Vulkan; submission completion futures; per-queue thread-safe submit.
- U8 allocator on `modules/allocator/buddy` + dedicated allocations + per-frame ring; roles DEVICE/UPLOAD/READBACK; UMA detection; residency mechanism (budget query + explicit `make_resident`/`evict`, simple LRU policy).
- U21 validation wiring + failure logging + object naming + leak detection.

The existing `apps/hello-gpu` triangle and cube run on the new RHI by M1's end. The current `modules/gpu` is retired.

**M2 — Resources, recording, rendering.** Full Tier 2 + Tier 3 on Vulkan 1.2+ with feature checks and Vulkan 1.4 fast paths:

- U9 buffers with `buffer_write`, `buffer_map_async`, persistent ptr on native, transient ring slices.
- U10 textures + views with the full format enum, including video/multiplanar/external formats; the virtual texture handle architecture; mip-gen, blit, copy.
- U11 sampler dedup.
- U12 Slang offline shaders + bundle format + reflection extraction, including per-entry required-feature masks.
- U13 graphics + compute pipelines async-created; reflection-driven layouts; engine-managed pipeline cache + P2 primitives.
- U25 shader model / compute numeric caps, exposed in caps and validated during pipeline creation.
- U14 per-type bindless tables; push constants.
- U15 multithreaded command pools (TLS); single-use recording; full surface; parallel render-pass recording.
- U16 declarative sub-pass topology + per-attachment load/store/resolve + memoryless attachments. Lowers to Vulkan 1.4 `dynamic_rendering_local_read` where present, with render-pass/subpass/separate-pass fallbacks on Vulkan 1.2/1.3.
- U17 timeline + binary semaphores; D3D12-style state barriers (lowered to synchronization2 or equivalent legacy barriers); fine-grained P2 escape; state-resync hook.
- U18 swapchain with HDR set, per-image render-finished, present-timing, multi-swapchain.
- U19 four-mode pacing source; native vsync wiring.
- U24 timestamp + occlusion queries; calibrated timestamps; frame-budget integration.

**M3 — Bleeding-edge and general-application capabilities.**

- U13 mesh + RT pipelines; U26 acceleration structures, AS build/update/compact/serialize commands, engine-managed SBT + raw escape.
- U14 bindless full tier on supporting hardware.
- U15 indirect commands (`VK_EXT_device_generated_commands`); reusable command-list tier where supported.
- U27 media/video/camera frame import, YCbCr sampling, video-process commands, and Vulkan video decode/encode where available.
- U28 GPU-driven scheduling/work-graph API surface, native where the backend exposes it and honestly gated elsewhere.
- U17 import of external sync primitives wired end-to-end.
- U21 frame capture integration + GPU crash diagnostics.

**M4 — Metal and WebGPU backends to parity.** Metal backend with Metal 4 fast paths (`MTL4CommandBuffer`, `MTL4ArgumentTable`, residency sets) plus availability-checked earlier Metal lowerings where behavior remains faithful; WebGPU (Dawn native + Emscripten web).

**M5 — Layer 1: render graph (U20).** Full design from this spec.

**M6+ — Additive capabilities and the D3D12 backend.** D3D12 backend with Feature Level 12_0 / Shader Model 6-era support floor and Agility 1.619 + SM 6.9 fast paths (enhanced barriers, native work graphs, `ResourceDescriptorHeap`, GPU upload heaps, D3D12 video); sparse texture residency; native ML/tensor backends beyond shader dispatch; advanced capture integrations; richer eviction policies; static/immutable samplers; further bleeding-edge features as they become required.

---

## 11. Module structure and dependencies

```
modules/gpu/
  include/
    gpu/
      gpu.h               -- umbrella include (backend-clean)
      types.h             -- handles, format/role/usage enums
      caps.h              -- Mel_Gpu_Caps + tiers
      status.h            -- per-action status types are declared with each action
      result.h            -- helpers
      future.h            -- Mel_Gpu_*_Future, target_reactor
      device.h            -- Mel_Gpu_Instance / Adapter / Device
      queue.h             -- queue role, request/release, submit
      memory.h            -- pool, placed, aliasing, residency
      buffer.h
      texture.h
      sampler.h
      shader.h
      accel_struct.h      -- BLAS/TLAS, scratch/build/update/compact/serialize
      pipeline.h
      bindless.h          -- tables, push constants
      command.h           -- record surface
      rendering.h         -- begin/end_rendering, sub-pass, attachments
      sync.h              -- semaphores, barriers, state model
      surface.h
      swapchain.h
      pacing.h            -- Mel_Gpu_Render_Source modes
      media.h             -- video sessions, frames, color metadata, process/decode/encode
      work_graph.h        -- GPU-side scheduling / work graph declarations
      query.h
      graph.h             -- (M5)
      debug.h             -- capture, crash diagnostics
      -- Opt-in per-backend interop (NOT in gpu.h):
      interop_vulkan.h
      interop_metal.h
      interop_webgpu.h
      interop_d3d12.h
  src/
    vulkan/                (M1-M3, Vulkan 1.2 floor / 1.4 ceiling)
    metal/                 (M4, Metal availability-checked floor / Metal 4 ceiling)
    webgpu/                (M4, current Dawn / browser WebGPU)
    d3d12/                 (M6+, FL12_0 floor / Agility 1.619 + SM 6.9 ceiling)
    common/                -- backend-agnostic: slotmaps, futures, completion pump, allocator wrappers
```

Dependencies (additions): `core`, `allocator`, `collection.slotmap`, `collection.ring`, `async.coroutine`, `async.signal`, `async.job` (M3 onward for compile pool), `reactor`, `log`, `debug` (when asserts are functional), `string`, `thread`. Tools: `tools/build` gains a Slang compile step (M2).

---

## 12. What this spec replaces and what survives

**Retired by M1.** `modules/gpu` in its current form — the one-shot device, single render pass, hardcoded format, single-frame-in-flight, per-action reactor source, NULL-on-failure model. All of it. The existing `mel_old/gpu.*` reference code remains for historical lookup.

**Survives.** The shape of the host-side render driver in `apps/hello-gpu/src/gpu_host.c` — reactor-attached render source, app lifecycle — is conceptually correct and ports forward; the underlying RHI it calls is replaced. The existing apps (`triangle`, `cube`, `lorenz`) are the first regression set for M1/M2.

---

## 13. Notes for the reader

This document is the *spec*. It pins decisions; it does not justify them re-litigably — each unit's resolution captures the trade-off considered and the principle it followed. The trackable per-unit resolutions live in the task list that backed this spec's drafting; cross-reference them when revisiting any unit.

When implementing, three tests catch most drift:

1. **"Can the app fully reimplement this?"** — if not, P2 primitives are missing.
2. **"Does this emulate, or does it lie?"** — if behavior diverges under load, gate honestly (P1).
3. **"Would adding this later force rework of existing API?"** — if yes, it cannot be deferred.
