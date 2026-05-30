# 2026-05-28 — GPU RHI spec update (post-review)

Follow-up to `2026-05-28-gpu-rhi-review.md`. This session applied the recommendations from that review to `design/gpu-rhi.md`. The spec grew from 818 to 868 lines.

## Work done

Four forks were resolved with Gabbo first; everything else was applied directly.

### Forks resolved

- **Tessellation control / evaluation stages** — included as first-class cap-gated. `TESS_CONTROL` (Hull), `TESS_EVALUATION` (Domain), and `GEOMETRY` (cap-gated, legacy) added to the U12 stage set. `caps.tessellation = none | partial | full` plus per-pipeline tess state on a new `pipeline_tess_create`. New dynamic states `cmd_set_patch_control_points` / `cmd_set_tessellation_domain_origin`.
- **Cluster Acceleration Structures + Partitioned TLAS** — full carrier in spec, M3 ships it. New typed handles `Mel_Gpu_Cluster_Accel_Struct` and `Mel_Gpu_Partitioned_Tlas`; new commands `cmd_build_cluster_accel_struct`, `cmd_build_blas_from_clusters`, `cmd_partitioned_tlas_update`. Caps `cluster_accel_struct` / `partitioned_tlas` with tiers `none | vendor | core`. Lowering pinned to `VK_NV_cluster_acceleration_structure` + `VK_NV_partitioned_acceleration_structure` on Vulkan today, with a future-additive KHR path; D3D12 via SM 6.9 cluster intrinsics + Agility 1.619.3 cluster-build APIs.
- **Bindless slot identity** — contract, with direct / indirect handle family split. Engine-created persistent resources of the *direct* family (`Mel_Gpu_Buffer`, `Mel_Gpu_Texture_View`, `Mel_Gpu_Sampler`, `Mel_Gpu_Accel_Struct`) guarantee `bindless_slot == handle.index` by allocator contract. Imports, compacted-heap allocations, and `bindless = capped` (WebGPU floor) use *indirect* peer types (`Mel_Gpu_Buffer_Indirect`, `Mel_Gpu_Texture_View_Indirect`, …) that require the `*_bindless_slot(handle)` lookup. The two families do not implicitly convert. Polymorphic call sites take `u32 slot` rather than a polymorphic handle. Explicit `*_make_indirect(direct) → indirect` for engine-driven compaction.
- **`mel_gpu_device_create_default` from the reactor thread** — convenience now returns `Mel_Gpu_Device_Create_Future` so it composes coherently with the rest of U3. The `*_sync` deadlock applies only to the user's explicit sync wrapper.

### Applied without forks

Capture-replay discipline threaded through every resource type (§3.1 slot metadata, §6.1 buffer flag, §6.2 texture flag, §6.6 AS flag, §6.7 heap flag). `caps.debug.capture_replay = none | partial | full`. This is the blocker for RenderDoc / PIX / Aftermath captures of bindless sessions; it now ships at M1.

Reactor backpressure policy: cross-thread `mel_reactor_post` is bounded with a per-pump high-water mark, redundant-post coalescing, and a 4× hard ceiling that debug-asserts (§3.3).

`VK_KHR_internally_synchronized_queues` (Roadmap 2026 mandatory, NVIDIA driver 582.29 Jan 2026) — opt-in flag on `queue_request`, collapses the per-queue submit lock when granted (§5.2). Default for media / asset-IO queues.

Caps domain expanded with concrete tiers for `adapter_type`, `luid`/`uuid`, `power_source`, `low_power_mode`, `internally_synchronized_queues`, `capture_replay`, `mutable_descriptor_type`, `cluster_accel_struct`, `partitioned_tlas`, `tessellation`, `matrix_scope`, `sampler_feedback`, `sparse_buffer`, `shared_presentable_image`, `pipeline_robustness`, `indirect_state_change`, `performance_query`. Atomic operations enumerated per-type per-storage (`atomic_int64_buffer`, `atomic_int64_image`, `atomic_float32_*`, `atomic_float16_*`).

`Mel_Gpu_Adapter` now exposes `adapter_type` (discrete / integrated / software / virtual / external), `luid` / `uuid`, and the OpenXR-runtime-published required-adapter LUID matcher. Software adapters (WARP / lavapipe / SwiftShader) are first-class enumeration entries; apps that exclude them filter on `adapter_type`. `Mel_Gpu_Instance` carries `power_source_changed` and `low_power_mode_changed` events parallel to thermal pressure.

ML acceleration unified under `caps.matrix_scope ∈ { none | thread | wave | group | full }` matching the SM 6.10 LinAlg axis. `Mel_Gpu_Matmul_Profile` now resolves to a `{ matrix_scope, extension }` pair with optional user `scope_hint`.

§6.4 raw-bytecode passthrough — `shader_create_from_bytecode({ target, blob, entry, stage, reflection? })` admits precompiled SPIR-V / DXIL / MSL / WGSL as a P2 peer to the Slang convenience. Slang version pinned via `tools/build/vendor/slang/SLANG_VERSION.lock` and contributing to the bundle / pipeline-binary cache key.

§6.2 textures — view component mapping pinned to Vulkan-style `Mel_Gpu_Component_Mapping` with identity default. Full named format set enumerated (R9G9B9E5_UFLOAT_PACK32, D32_FLOAT_S8X24_UINT, A2B10G10R10_UNORM, the full ASTC LDR + HDR matrix, multiplanar `Y210` / `Y216` / `Y410` / `Y416` / `AYUV`, etc.). Sampler-feedback streaming textures admitted as cap-gated usage (`SAMPLER_FEEDBACK_MIP_REGION` / `SAMPLER_FEEDBACK_MIN_MIP`). View bindless-slot budget reported explicitly to make the per-view slot pressure visible.

§6.5 — `pipeline_robustness ∈ { default | disabled | enabled | per_stage }` selector (`VK_EXT_pipeline_robustness` lowering).

§6.6 — RT position fetch (`VK_KHR_ray_tracing_position_fetch` / D3D12 `TriangleObjectPositions`, SM 6.9 mandatory) and motion blur (`VK_NV_ray_tracing_motion_blur` / D3D12 motion-instance) admitted as cap-gated descriptors.

§6.7 — mutable descriptor types (`VK_EXT_mutable_descriptor_type`) collapsing the per-class tables on the floor when granted (`caps.mutable_descriptor_type`).

§7.1 — `Mel_Gpu_Indirect_Layout` (D3D12 `ID3D12CommandSignature` / Vulkan `VkIndirectCommandsLayoutEXT` analog) for `cmd_execute_indirect`. Caps `indirect_state_change = none | tier1 | tier2` gate which state changes the indirect command may encode.

§7.4 — extent semantics pinned: `surface.pixel_extent` (GPU unit, what `swapchain_acquire` returns), `surface.point_extent` (OS unit), `surface.scale_factor`. The DPI / scale-factor change event fires on any of them. The engine never silently re-scales the swapchain. Shared presentable images added (`VK_KHR_shared_presentable_image` and D3D12 / Metal direct-mode equivalents) for VR / low-latency / direct-scanout paths.

§7.5 — `Frame_Info` carries the current power source and low-power-mode flag alongside thermal pressure; doc-clarified as pacing-tick context, not retirement index.

§8.4 — `WorkGraph` pass type composes `cmd_dispatch_graph` inside the render graph; the work-graph dispatch is one opaque pass for the graph's scheduling purposes.

§9.2 — hardware performance counters (`Mel_Gpu_Perf_Counter_Pool`) — `VK_KHR_performance_query` on Vulkan, D3D12 performance heap + AMD GPA / NVIDIA PerfKit / Intel GPA interop, Metal `MTLCounterSet`. Caps `performance_query = none | passes | streaming`.

§3.5 — CUDA / OpenCL / GStreamer (DMA-BUF) interop paths named explicitly in the import descriptor, capability-gated by `interop.cuda_compat` / `interop.opencl_compat`.

§10 — milestone list rebalanced. M1 gains capture-replay scaffolding, direct/indirect handle split, default-create-future, adapter type / LUID / UUID, internally synchronized queues, full caps surface for unimplemented domains. M2 gains tessellation, raw-bytecode passthrough, Slang version pin, mutable descriptor layout, indirect-layout command-signature, pipeline robustness selector, perf-counter pool, power-source / low-power-mode events, extent-semantics pin. M3 gains cluster AS, partitioned TLAS, position fetch, motion blur, sampler feedback Tier 2.

§11 — module-structure header tree updated with `indirect.h`, expanded enumeration of caps fields, indirect handle types listed per resource header, and references to the new commands.

## Kludges

None in the spec itself — every recommendation that survived the four-fork resolution was applied as a peer surface, never as a half-implementation or deferral.

The one piece of vendored debt the spec now openly carries is **Slang as a pinned dependency** (§6.4). The pin is the right call (Slang's capability surface is documented-internal-only per Slang issue #9210, so treating it as a stable ABI would be dishonest) but it means a future Slang regression that hits a critical Melody path is a non-trivial unblock. `tools/build/vendor/slang/MIGRATION.md` is the place that debt should live.

## CLAUDE.md suggestions

None. The project CLAUDE.md as it stands cleanly accommodates the spec direction; the global CLAUDE.md's emphasis on precise diction and explicit-over-implicit was load-bearing during the review and update.

## Suggestions

### Feature direction

The spec is now committed to two carriers that don't yet have full cross-vendor backing on Vulkan: **`VK_EXT_descriptor_heap`** (announced 23 Jan 2026, NV driver 582.29 ships it, AMD / Intel / Mali still in flight) and **`VK_NV_cluster_acceleration_structure` + `VK_NV_partitioned_acceleration_structure`** (NV-only at spec time). Both carriers now have symmetric fallback policies pinned in the spec:

- **Descriptor heap** rests on `descriptor_buffer` + `descriptor_indexing` if `descriptor_heap` has not ratified by M3 ship (§10 M3, already there before this session).
- **Cluster AS** added in this session — `caps.cluster_accel_struct = none | emulated | vendor | core` with the `emulated` tier shipping at M3. On `emulated`, `cmd_build_blas_from_clusters` builds a standard BLAS with contiguous per-cluster primitive-ID ranges, and Slang's `ClusterID()` lowers to `gl_PrimitiveID / clusterPrimitiveStride` under the new capability atom `cluster_accel_struct_emulated`. The Nanite-style cluster-LOD streaming path is reachable on the full Vulkan device matrix at M3, not just NV; cost differs per cap tier, behavior does not (§6.6).

### Repo hygiene

The `design/gpu-rhi.md` file is now 868 lines. It is still the *spec* per its closing notes — not a primer — and the per-unit resolution trail lives in the task list that backed the original drafting. Two suggestions:

1. Split the milestone section (§10) out into `docs/gpu-rhi-milestones.md` once M1 lands. The spec and the schedule churn at different cadences, and combining them puts pressure on every spec re-read to also be a schedule re-read.

2. Add a `docs/gpu-rhi-glossary.md` that captures the typed-handle naming conventions (direct vs. indirect family, slotmap-per-type, status / result naming) and the cap-domain layout. The conventions are the highest-recurrence reference and they currently live inside §3 prose where they are easy to miss.

### Code

Not applicable this session — the work was spec, not implementation.
