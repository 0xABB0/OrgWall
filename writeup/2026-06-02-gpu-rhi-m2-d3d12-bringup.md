# GPU RHI M2 — D3D12 co-primary backend bring-up (Phases 0–2)

Continues the GPU RHI rewrite (`design/gpu-rhi.md`). M2's Vulkan half is ~complete and green on macOS/MoltenVK
and Windows/NVIDIA; D3D12 — co-primary per §12, and the backend the spec makes co-primary precisely to surface
design flaws Vulkan-alone hides — was entirely absent. With the `win-pilot` box available it is now buildable.
This session stands up the D3D12 backend from nothing through **first pixels**, phased no-prerequisite-first
(`design/gpu-d3d12.md`). Gabbo selected the D3D12 track over the Vulkan tail.

All work built and tested on `win-pilot` (Windows 10 22H2, RTX 2060 SUPER, in-box Windows SDK d3d12.h, clang/MSVC
ABI). **gpu-d3d12 9/9, debug layer on, zero debug-layer errors.** The Vulkan suite is untouched and stays
**gpu-vulkan 28/28** on macOS. Worktree branch `worktree-gpu-d3d12-bringup`; pushed (win-pilot fetches+checks it
out, `main` untouched).

## What the co-primary port confirms (and the contrasts it surfaced)

The public surface (`include/gpu/*.h`) is **backend-clean and unchanged** — D3D12 supplies a second concrete
`struct Mel_Gpu_Device` under `src/d3d12/`; only one backend compiles per `--gpu`, so the per-backend struct
definitions never collide. The value-handle identity (U1), per-action result/severity (U2), reactor future
spine (U3), domain caps (U4), instance/adapter/device phased create (U6), availability queue model (U7), and the
future-gated deferred-free watermark (§3.3) all ported **with no surface change** — the abstractions are not
Vulkan-shaped. Where D3D12 genuinely differs, the difference stayed *inside* the backend:

- **Views.** A `VkImageView` is a concrete object; a D3D12 view is a descriptor-heap materialization. The
  `Mel_Gpu_Texture_View` holds intent (parent + format + dimension + range) and the RTV/SRV is created on demand
  (RTV at `begin_rendering` into a round-robin device heap). The handle abstraction held.
- **State model.** Buffers ride D3D12 common-state promotion/decay, so a buffer transition barrier would mismatch
  `StateBefore`; the faithful lowering is a UAV barrier only. Textures use legacy `ResourceBarrier` transitions,
  skipping the emit when `before == after` (D3D12 rejects it; Vulkan does not). The shared per-CL per-subresource
  tracker (U17) is identical across backends.
- **Binding payload (foreshadowed).** D3D12 caps already report `bindless.tier = full` from ResourceBindingTier 3,
  and the root-record carrier must report `descriptor_indices` where Vulkan-BDA reports `mixed` — the contrast the
  co-primary mandate exists to expose. Lands in Phase 3.
- **Copy footprints.** Texture↔buffer copies are 256-row-aligned (`D3D12_TEXTURE_DATA_PITCH_ALIGNMENT`) via
  `GetCopyableFootprints` + per-row staging; Vulkan copies tightly. Hidden behind `texture_write` /
  `cmd_copy_texture_to_buffer`.
- **No render-pass object.** D3D12 dynamic rendering is `OMSetRenderTargets` + clears; there is no compile step,
  so the Vulkan `!dynamic_rendering` floor has no D3D12 analog.

## Work done

### Toolchain + build (build.c, modules/build/driver.c)
- `--gpu=d3d12` added as a valid win32 backend (`gpu_valid`). `src/d3d12/*.c` gated on `.gpu="d3d12"`,
  `MEL_GPU_D3D12=1` define, link `-ld3d12 -ldxgi -ldxguid`. **No Agility SDK and no SDK-path injection** — the
  in-box Windows SDK headers/libs are already on vcvars' INCLUDE/LIB (the same path the Vulkan win32 surface
  uses for `windows.h`). COM consumed through the C struct/vtable path: `d3d12.h` selects it when `__cplusplus`
  is undefined, `COBJMACROS` gives the `ID3D12X_Method(obj, …)` macros, IIDs come from `dxguid.lib`.
- `gpu-d3d12` test target added (mirrors `gpu-vulkan`); the body is `#if MEL_GPU_D3D12`-guarded so it links to an
  empty 0-test runner on any non-d3d12 build (verified on macOS).

### Phase 0 — device foundation (instance.c, caps.c, device.c)
DXGI factory + `EnumAdapterByGpuPreference` (discrete first); adapter-domain caps from the DXGI descriptor at
instance-create, device-level caps refined via `CheckFeatureSupport` (bindless tier from ResourceBindingTier,
wave/int64/int16 from OPTIONS1/4, UMA/ReBAR from ARCHITECTURE1, timestamp period from the queue frequency);
`D3D12CreateDevice` (FL 12_0 floor) + DIRECT command queue; the D3D12 debug layer as the validation analog
(`EnableDebugLayer` + DXGI debug factory, retry-without on absence, mirroring the Vulkan validation-not-installed
path); the shared reactor pump (U3) + thread-safety tracker (U36); headless create/destroy.

### Phase 1 — queues, buffers, timeline fence (queue.c, buffer.c, memory.c)
One device `ID3D12Fence` timeline whose signal value **is** the submission serial, driving both submit→future
(reactor poller, else synchronous `SetEventOnCompletion` wait) and the §3.3 deferred-free watermark. Slotmap
table helpers + watermark ported. Buffers as committed resources (UPLOAD/READBACK persistent map; DEVICE via a
transient staging copy + `CopyBufferRegion` on a one-shot DIRECT list, buffers riding common-state promotion so
no barriers). Budget via `IDXGIAdapter3::QueryVideoMemoryInfo`.

### Phase 2 — textures, barriers, command lists, rendering (texture.c, record.c)
U10 textures (committed DEFAULT resources, RT/DS/UAV flags from usage), views as descriptor intent,
`texture_write` (256-aligned staging upload). U15 standalone command lists (DIRECT allocator+list, single-use:
created-closed, begin resets, end closes). U17 legacy `ResourceBarrier` + the shared per-CL tracker;
`cmd_buffer_barrier` emits UAV barriers only. U16 dynamic rendering (`OMSetRenderTargets` + `ClearRenderTargetView`
/ `ClearDepthStencilView`, RTV/DSV round-robin CPU heaps, viewport/scissor). `cmd_copy_texture_to_buffer` via
placed footprint. The offscreen clear→readback pixel test is the machine-checked first-pixels proof.

## Verification
`ssh win-pilot … nob.exe test gpu-d3d12 win32 --gpu=d3d12` — **9/9**: device instance/adapters/caps;
headless create/destroy; buffer upload+device; queue request/info/submit→future; residency budget+caps;
texture create/view/alive; **offscreen clear→readback (pixel-verified 0.25/0.5/0.75/1.0 → ~64/128/191/255)**;
texture write→readback round-trip; buffer-barrier clean submit. Debug layer on, no debug-layer errors, no
melody-level leaks. Each phase was a separate commit+push and a separate win-pilot build; all three compiled
clean on the first build (blind D3D12-in-C against the in-box SDK). `gpu-vulkan` re-run on macOS: **28/28**.

## Kludges and debt (confessed, MEL-ENGINE-VIII)

- **Committed resources only (U8 dedicated-allocation floor).** Every buffer/texture is its own implicit heap;
  no placed-resource suballocator yet (the Vulkan backend has a buddy suballocator). Correct, but more
  allocations than ideal. Placed-resource + heap suballocation is the next U8 tier.
- **No Agility SDK / enhanced barriers.** Floor lowering only: legacy `ResourceBarrier`, in-box d3d12.h. The
  ceiling (`ID3D12GraphicsCommandList7::Barrier` enhanced barriers, `ID3D12Device10`, GPU upload heaps,
  `ResourceDescriptorHeap`) rides the Agility runtime behind a build flag, deferred. Mirrors how Vulkan carries
  legacy barriers + the sync2/unified-image-layouts fast path.
- **READBACK uses persistent map + fence-wait coherency**, not Map-on-read. Valid on x86 (PCIe DMA writes
  snoop-invalidate CPU caches), and the pixel tests confirm it on the RTX 2060; a UMA/ARM target would want an
  explicit Map(read-range) to invalidate. Flagged.
- **Render targets created without an optimized clear value** (unknown at create time — the clear color arrives
  at `begin_rendering`). The debug layer emits a benign perf note, not an error. Plumbing the clear through
  `texture_create` would remove it.
- **Command lists are one allocator+list each, released at destroy** (no per-thread TLS pool, no recycling). The
  single-use contract holds; destroy-after-completion is the caller's contract (the tests wait the future first).
  The U15 TLS pool is the optimization tier.
- **queue_submit assumes destroy-after-completion on the reactor path too.** Phase 2 tests use the synchronous
  (no-reactor) path; the reactor path resolves futures via the fence poller but command-list destroy is not yet
  future-gated. Fine for the tests; a real reactor app wants deferred CL destroy.
- **Single DIRECT queue.** All queue roles lower to it with a warning (no async-compute/copy queue yet); the
  retirement watermark is exact only because completion is in-order on one queue.
- **caps are a Phase-0/1 subset.** bindless tier + slots, shader wave/int, timestamp, UMA/ReBAR, budget are real;
  RT/mesh/video/work-graph/VRS/sampler-feedback report `none`/absent honestly (their command paths are later
  phases). `bindless.binding_model`/`root_record_payload` are not yet set on D3D12 (Phase 3, with U14).
- **`Mel_Gpu_Device_Create_VK_FAILED`** is reused as the generic backend-create-failed code on D3D12 (the public
  enum is Vulkan-named). Not changed in this slice; the public status enum is shared. Flagged.
- **The texture-write / buffer-upload immediate submits block on the fence** (`CopyBufferRegion`/`CopyTextureRegion`
  + signal + wait), like the Vulkan staging path. Async transient-ring upload is a later U9 tier.
- **Phases 3 (pipelines + bindless + reflection) and 4 (DXGI swapchain) are not done.** D3D12 has no PSO/root
  signature, no `ResourceDescriptorHeap` bindless, no DXIL reflection, no swapchain yet — so no `hello-gpu` on
  D3D12. This is the next sequence, not watered-down: the foundation through rendering is complete and green.

### Rule-#1 flags
- **Comments.** Global `~/CLAUDE.md` says "Never write comments"; this module is densely commented with spec/
  MEL-ENGINE tags, which the project `CLAUDE.md` actively encourages ("cite it by tag"). I matched the pervasive
  house style — an uncommented D3D12 backend beside a commented Vulkan one would be the inconsistent outlier
  (MEL-CODE-005). Every prior M2 writeup flagged this and proceeded the same way. **Halt-and-query:** if the
  global rule governs here, say so and I strip the comments.
- **Enums.** The backend reuses the public RHI enums (status/role/format/state/usage). Same protocol-mapping
  carve-out (MEL-CODE-001) and Rule-#1 flag as every prior slice — these map onto `DXGI_FORMAT` /
  `D3D12_RESOURCE_STATES` / `D3D12_COMMAND_LIST_TYPE` etc.

## CLAUDE.md / repo-convention suggestions (recommendations only)
- `modules/build/platforms.md`: win32 now has **two** GPU backends (vulkan + d3d12); `d3d12` is a valid
  `--gpu` on win32. The doc still says "DX12 is unimplemented." Update.
- The `Mel_Gpu_Device_Create_Status` enum's `…_VK_FAILED` member is backend-specific in a backend-clean header;
  consider renaming to `…_BACKEND_FAILED` (touches both backends + apps; small, pervasive — fits the repo's WIP
  posture).
- `modules/gpu/readme.md` still absent (flagged in four prior writeups). The D3D12 conventions belong there:
  views are descriptor intent, buffers ride common-state promotion, copies are 256-aligned, the timeline fence
  serial is the submission serial.

## Suggestions / next steps (sequenced)
- **Phase 3 — pipelines + bindless + reflection (the highest co-primary value).** PSO + root signature
  (reflection-derived), `ResourceDescriptorHeap`/`SamplerDescriptorHeap` bindless with root-record descriptor
  indices, DXIL passthrough reflection (`shader_create_from_bytecode`; a DXIL container reader or DXC reflection).
  This is where the binding-model contrast (`root_record_payload = descriptor_indices` vs Vulkan `mixed`) becomes
  concrete — port the Vulkan bindless suite and watch the payload differ.
- **Phase 4 — DXGI flip-model swapchain** over the HWND (`gpu_view` already exists on win32), then `hello-gpu`
  cube on D3D12.
- Then the Agility-SDK ceiling pass (enhanced barriers, GPU upload heaps, dynamic resource heaps) behind a build
  flag, and the placed-resource U8 suballocator.
