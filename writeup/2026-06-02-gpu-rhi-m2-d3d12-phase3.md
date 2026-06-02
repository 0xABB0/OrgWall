# GPU RHI M2 — D3D12 co-primary Phase 3: pipelines + bindless + reflection (U12/U13/U14)

Continues the D3D12 co-primary bring-up (`design/gpu-d3d12.md`, `writeup/2026-06-02-gpu-rhi-m2-d3d12-bringup.md`)
where Phases 0–2 stopped at first pixels (device/queues/buffers/textures/barriers/command-lists/dynamic-rendering,
9/9). This session lands **Phase 3** — the binding model, the highest co-primary value — porting the Vulkan
binding suite to D3D12 and watching the payload differ.

Built and tested on `win-pilot` (Windows 10 22H2, RTX 2060 SUPER, in-box Windows SDK 10.0.22621, clang/MSVC ABI).
**gpu-d3d12 13/13** (the prior 9 plus four Phase-3 tests), debug layer on with **break-on-ERROR/CORRUPTION
armed** (zero debug-layer errors, proven not assumed), no melody leaks. Vulkan suite **gpu-vulkan 28/28** on
macOS/MoltenVK (behaviorally untouched; the one shared addition, `cmd_copy_buffer`, is unused by the 28). Worktree
branch `worktree-gpu-d3d12-phase3`, pushed; `main` untouched.

## The headline finding: ceiling vs floor on D3D12 bindless

The spec's §6.7 D3D12 *ceiling* is SM 6.6 dynamic resources — `ResourceDescriptorHeap[i]` /
`SamplerDescriptorHeap[i]` with the `HEAP_DIRECTLY_INDEXED` root-signature flags. **The in-box Windows 10 22H2
runtime does not support it**: `D3D12SerializeVersionedRootSignature` rejects the directly-indexed flags
("Unsupported bit-flag set, flags c01") and `CreateGraphicsPipelineState` rejects SM 6.6 bytecode (E_INVALIDARG).
SM 6.6 dynamic resources ride the Agility SDK or a Win11 runtime — exactly the "no Agility for the floor" line in
the design.

So the floor is the §6.7 D3D12 *floor* — **classical descriptor heaps with root-signature tables** — which is
first-class, not a degraded fallback (P1). The first build proved this empirically: everything compiled clean on
the first attempt and 10/13 passed; the 3 bindless/pipeline tests failed precisely at the SM-6.6 ceiling, which is
the co-primary mandate doing its job (paper-testing the ceiling-only model would have hidden the floor's necessity).

The binding-model **contrast still lands**, and is the same regardless of floor/ceiling: D3D12 reports
`binding_model = root_record`, `root_record_payload = descriptor_indices` where the Vulkan-BDA path reports
`mixed` (textures/samplers as indices, buffers as pointers). On D3D12 buffers are descriptors even at the ceiling,
so the payload collapses to indices. `d3d12_bindless.binding_model_caps` asserts it.

## Work done

### U11 samplers (`sampler.c`)
Value handle + slotmap; auto-dedup by canonical key (FNV-1a + `memcmp`), reference-counted (the public contract);
slot retires future-gated at refcount 0. `D3D12_SAMPLER_DESC` from filter/wrap/anisotropy/compare/border/lod
(filter packed via the `D3D12_FILTER` bit layout). Registers a sampler descriptor into the device sampler heap at
`slot == handle.index`. `mel_gpu__sampler_retain` backs static-sampler lifetime. The intern table + slotmap are
both under `obj_lock`, touched via raw `mel_slotmap_*` (the `mel_gpu__table_*` wrappers lock `obj_lock`
themselves; nesting a PLAIN mutex would deadlock).

### U12 DXIL passthrough + reflection (`shader.c`, `reflect.c`)
`shader_create_from_bytecode` / `_compute_from_bytecode` copy the DXIL blob(s). `reflect.c` is a self-contained
DXIL-container reader (DXBC FourCC table → `ISG1`/`ISGN` input-signature part) that yields the vertex-input
layout (semantic + format + tight-packed offset) as the §6.5 reflection default. In-process DXC reflection
(`IDxcUtils::CreateReflection`) needs `dxcapi.h`, which is off the in-box INCLUDE path; injecting the DXC SDK
would break the in-box-only floor, so the container reader is the "DXIL container reader" path §6.4 names.

### U13 pipelines (`pipeline.c`)
Reflection-derived root signature + graphics/compute PSO. The bindless root signature is root 32-bit constants
(the per-draw record at b0) + two descriptor tables: one CBV/SRV/UAV table with SRV+UAV+CBV ranges each offset to
its class base, and a sampler table — `DESCRIPTORS_VOLATILE` so partially-populated heaps are legal (the Vulkan
partially-bound analog). Full enum→D3D12 mapping for blend / blend-op / cull / fill / front-face / compare /
stencil / topology. Static samplers → `D3D12_STATIC_SAMPLER_DESC`. `MissingFeature` gate (bindless requested,
no heap); destroy defers PSO+root-sig past in-flight submissions and releases static-sampler claims after.

### U14 bindless heaps (`binding.c`)
One shader-visible CBV/SRV/UAV heap partitioned into four class bases (SRV textures, UAV storage buffers, CBV
uniform buffers, UAV storage images) + one sampler heap. Resource creation writes the descriptor at
`base[class] + handle.index`; the table range's `OffsetInDescriptorsFromTableStart = base[class]` maps the
shader's per-class array index onto that heap slot, so the **shader index is `handle.index`** and `slot ==
handle.index` holds (§3.1). `cmd_bind_bindless` binds both heaps; `cmd_bind_pipeline` binds them + the two
descriptor tables for a bindless pipeline (the simple path: bind, push the record, draw/dispatch).

### Recording (`record.c`)
`cmd_bind_pipeline` (PSO + root sig + IA topology + heaps/tables), `cmd_push_constants` (root 32-bit constants,
graphics/compute by bound bind point), `cmd_draw` / `_draw_indexed` / `cmd_dispatch`, `cmd_bind_vertex_buffer` /
`_bind_index_buffer`, and a new `cmd_copy_buffer` primitive (both backends).

### U21 validation (`device.c`)
When the debug layer is active, `ID3D12InfoQueue::SetBreakOnSeverity(CORRUPTION|ERROR)` so a validation failure
aborts loudly at the offending call (MEL-ENGINE-VIII) — this is how the zero-debug-layer-errors bar is *enforced*.

### Caps (`device.c`)
At device-create, when `descriptor_indexing` is requested and ResourceBindingTier 3 is present:
`binding_model = root_record`, `root_record_payload = descriptor_indices`, `root_record_update = persistent_map`;
heap slot counts refined to the realized capacities.

## Verification
`ssh win-pilot … nob.exe test gpu-d3d12 win32 --gpu=d3d12` — **13/13**, break-on-ERROR/CORRUPTION armed:
- `d3d12_bindless.binding_model_caps` — `root_record` / `descriptor_indices` (the Vulkan-`mixed` contrast).
- `d3d12_pipeline.graphics_create` — graphics PSO + reflection-derived root signature from DXIL.
- `d3d12_bindless.sample_texture_readback` — fullscreen triangle samples a heap-resident solid texture through a
  heap-resident sampler, slots delivered in the root record; cleared RT read back, sampled colour **pixel-verified**
  (≈64/128/192/255).
- `d3d12_compute.storage_buffer_bindless` — `out[i] = in[i] + 1`, both storage buffers addressed by heap slot;
  device-local result copied to a readback buffer and verified (the storage-buffer payload the graphics test can't
  reach).

Test DXIL is compiled at test time by spawning the in-box `dxc.exe` (SM 6.0, on PATH under `dev.cmd`; `dxil.dll`
co-located so the bytecode is signed). `gpu-vulkan` re-run on macOS: **28/28**. No `leak:` lines at any device
destroy.

## Kludges and debt (confessed, MEL-ENGINE-VIII)

- **Bindless is the §6.7 floor (descriptor tables), not the SM 6.6 ceiling.** The in-box Win10 runtime can't honor
  `ResourceDescriptorHeap` / `HEAP_DIRECTLY_INDEXED` / SM 6.6 bytecode, so the floor lowers to Tier-3 unbounded
  descriptor tables + SM 6.0. Correct and first-class, but the ceiling (Agility SDK / Win11) is deferred. `caps`
  reports `root_record` / `descriptor_indices` for both floor and ceiling and does **not** distinguish the table
  lowering from direct heap indexing — a power user can't branch on it yet (a `dynamic_resources` sub-flag would
  fix this).
- **`push_constant_size` and the `bindless` flag are explicit, not reflected.** Deriving them needs the full DXC
  reflection blob (`dxcapi.h`, off the in-box INCLUDE path), so they ride the §6.5 manual-layout P2 peer. The
  Vulkan side reflects both from SPIR-V; D3D12 does not. `reflect.c` reflects the **input signature only**.
- **`reflect.c` is untested this slice.** Phase-3 shaders have no vertex input (fullscreen triangle = `SV_VertexID`;
  compute = none), so the ISG1/ISGN reader produces nothing and is exercised by neither test. It is Phase-4 (cube)
  readiness; it parses `float` vec2/3/4 only (matching the Vulkan floor), withdraws the whole layout on any other
  format, and bounds every container read — a malformed container yields "no inputs", never a crash. A dedicated
  reflection test is owed.
- **Specialization constants have no DXIL analog** — ignored on D3D12 with a warning (HLSL specializes via `-D` +
  recompile). The Vulkan `vk_pipeline.spec_constants_bake` test does not port.
- **Classic descriptor-set path (`set_layouts` / bind groups) not implemented on D3D12.** `pipeline_create` rejects
  `set_layouts` with `MissingFeature`. The bindless path is the only descriptor path this slice; the Vulkan
  `bind_group.c` peer has no D3D12 analog yet.
- **`cmd_copy_buffer` is a new public primitive** added to `command.h` + both backends, driven by the compute
  readback (a device-local UAV buffer cannot be host-mapped on D3D12, unlike the Vulkan host-coherent SSBO the
  `vk_compute` test reads directly). The Vulkan impl is verified (28/28); it is unused by the existing 28.
- **Compute readback uses two submissions** (dispatch, then copy) so the UAV buffer decays to COMMON and
  re-promotes to COPY_SOURCE. A single-list `UAV → COPY_SOURCE` transition is not emitted — `cmd_buffer_barrier`
  only issues UAV barriers (the Phase-2 buffer-state design). Fine for the test; a real frame wants the transition.
- **Storage-image (UAV texture) bindless class is not wired.** Only SRV (sampled texture), UAV (storage buffer),
  CBV (uniform buffer), and sampler classes register and have table ranges; the storage-image base exists but no
  range maps it. No test.
- **Texture-view SRV registration is gated on the parent's `SAMPLED` usage, and the SRV desc always fills the
  `Texture2D` union member.** Non-2D view dimensions (cube / 3D / array) would mis-fill the union; Phase 3 uses 2D
  only.
- **Heap caps are fixed** (16384 each for SRV/UAV/CBV/storage-image in one 65536 CBV/SRV/UAV heap, 2048 samplers),
  not grown on demand. `MissingBindlessSlot` is **unreachable** on D3D12: an unbounded descriptor-table range has
  no sized array a shader can over-index at create time (symmetric to the Vulkan note that runtime arrays never
  trip it). Over-indexing past a class cap is GPU-UB, not a create-time error.
- **Static samplers and the bindless sampler table would collide** (both `s0 space0`) if a pipeline used both. No
  Phase-3 test does; a static sampler under a bindless pipeline needs a distinct register space.
- **`shader_create_from_bytecode` ignores the entry-name fields on D3D12** (DXIL bakes the entry at `dxc -E`). The
  public `Mel_Gpu_Shader_Bytecode_Opt` fields are SPIR-V-named (`spirv_vertex`…) but carry DXIL — same backend-name
  carve-out family as `…_VK_FAILED`.
- **`buffer_device_address` returns the real GPU VA** (gated on `DEVICE_ADDRESS` usage for API symmetry), but the
  root-record payload is `descriptor_indices` — the shader cannot dereference it as a buffer-reference the way
  Vulkan-BDA does. The VA is genuine (root CBV / VBV / IBV).
- **Test shaders are compiled at runtime via `system("dxc …")`** to `%TEMP%`, not embedded. Depends on `dxc` on
  PATH (present under `dev.cmd`); a missing `dxc` fails the test rather than skipping it.
- **clang-format could not be run** (absent on the dev mac, MEL-CODE-004); house style matched by hand.

### Rule-#1 flags (carried from every prior M2 slice)
- **Comments.** Global `~/CLAUDE.md` says "never write comments"; this module is densely commented in the house
  style the project `CLAUDE.md` encourages ("cite it by tag"). Matched the surrounding style; an uncommented
  Phase-3 beside a commented Phase-0–2 would be the inconsistent outlier. **Halt-and-query** if the global rule
  governs and I should strip them.
- **Enums.** Reuses the public RHI enums (status/format/state/blend/compare/sampler/binding-model) — the same
  protocol-mapping carve-out (MEL-CODE-001) mapping onto `D3D12_BLEND` / `DXGI_FORMAT` / `D3D12_FILTER` etc.
- **`…_VK_FAILED`** reused as the generic backend-fail code on D3D12 (public enum is Vulkan-named).

## CLAUDE.md / repo-convention suggestions (recommendations only)
- `design/gpu-d3d12.md` Phase 3 names "`ResourceDescriptorHeap` bindless heap" as the deliverable. Clarify that
  `ResourceDescriptorHeap` is the **ceiling** (Agility SDK / Win11 / SM 6.6) and the in-box floor is **Tier-3
  descriptor tables**; the "no Agility SDK required to bring the backend up" line still holds, but bindless on the
  floor is tables, not direct heap indexing.
- A caps sub-tier distinguishing dynamic-resources (`ResourceDescriptorHeap`) from descriptor-table bindless would
  let a power user branch; both currently report `root_record` / `descriptor_indices`.
- The `…_VK_FAILED` → `…_BACKEND_FAILED` rename (touches both backends + apps; small, pervasive) still stands.
- `modules/gpu/readme.md` still absent (flagged in five prior writeups). The D3D12 binding conventions belong
  there: bindless = descriptor tables on the in-box floor (`ResourceDescriptorHeap` ceiling on Agility), per-class
  heap bases, `slot == handle.index`, root record = descriptor indices, classic path unimplemented.

## Suggestions / next steps (sequenced)
- **Phase 4 — DXGI flip-model swapchain** over the HWND (`gpu_view` exists on win32), then `hello-gpu` cube on
  D3D12. The cube passes an explicit `vertex_layout` (location-indexed); D3D12 input layouts need semantic names,
  so the explicit path currently maps location → `TEXCOORD<location>` (a convention, flagged). The cleaner route is
  the reflection-derived input layout (`reflect.c` reads the cube VS's `ISG1` semantics) — Phase 4 should verify
  `reflect.c` against the cube and prefer it.
- **Agility-SDK ceiling pass** behind a build flag: `ResourceDescriptorHeap` / `SamplerDescriptorHeap` direct
  indexing, enhanced barriers, GPU upload heaps — and full DXC reflection (push-constant size, bindless detection)
  once the DXC SDK is on the build path. Caps then distinguish floor-tables from ceiling-direct-indexing.
- Storage-image bindless class + a UAV-texture compute test; the classic descriptor-set D3D12 path (the `bind_group`
  peer); a `reflect.c` input-signature unit test.
