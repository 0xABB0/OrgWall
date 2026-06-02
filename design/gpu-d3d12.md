# GPU RHI — D3D12 backend (M2 co-primary)

Implements `design/gpu-rhi.md` on D3D12. D3D12 is co-primary with Vulkan (§12 M2): paper-testing one
without the other lets an architectural flaw hide. The public surface (`include/gpu/*.h`) is backend-clean
and unchanged; D3D12 supplies a second concrete `struct Mel_Gpu_Device` under `src/d3d12/`. Only one
backend compiles per `--gpu`, so the per-backend struct definitions never collide.

## Targets

- **Floor** — Feature Level 12_0 / SM 6-era runtime, **in-box** Windows SDK `d3d12.h`/`dxgi1_6.h`, legacy
  `ResourceBarrier` state transitions. No Agility SDK required to bring the backend up.
- **Ceiling** — Agility SDK 1.619.3 / SM 6.9: enhanced barriers (`ID3D12GraphicsCommandList7::Barrier`
  + `ID3D12Device10` create-with-layout), GPU upload heaps (`D3D12_HEAP_TYPE_GPU_UPLOAD`), dynamic resource
  binding (`ResourceDescriptorHeap` / `SamplerDescriptorHeap`), DXIL library composition, CPU timeline query
  resolves. The Agility runtime (D3D12Core.dll + `D3D12SDKVersion`/`D3D12SDKPath` exports) is loaded behind a
  build flag; the ceiling lowerings gate on its presence, mirroring how Vulkan carries legacy barriers + the
  sync2 / unified-image-layouts fast path.

## Backend mechanics

- **COM in C.** `COBJMACROS` + the C struct/vtable path (`d3d12.h` selects it when `__cplusplus` is
  undefined). Calls are `ID3D12Device_CreateCommandQueue(dev, ...)`; releases `IUnknown_Release`. IIDs come
  from `dxguid.lib`. Link `-ld3d12 -ldxgi -ldxguid`.
- **Debug layer** is the validation analog: `ID3D12Debug::EnableDebugLayer` (+ `ID3D12Debug1::
  SetEnableGPUBasedValidation` for the GPU-AV descriptor-indexing analog), gated on `device.debug`.
- **Shared infra is reused verbatim** — the reactor completion pump (U3), thread-safety tracker (U36),
  slotmap-per-type tables (U1), future-gated deferred-free watermark (§3.3) are backend-agnostic; D3D12 only
  swaps the object frees and the submit/fence completion source.

## Binding model (U14) — where D3D12 surfaces flaws Vulkan hides

D3D12's ceiling is **descriptor indices in a root record**, not pointers: `ResourceDescriptorHeap[i]` /
`SamplerDescriptorHeap[i]` (SM 6.6 dynamic resources, ResourceBindingTier 3). Buffers are descriptor indices
on D3D12 even on the ceiling — the mixed-payload (`root_record_payload = mixed`) that Vulkan reports with BDA
collapses to `descriptor_indices` here. This is the contrast the co-primary mandate exists to expose: the
root-record carrier must already admit both `pointers` and `descriptor_indices` payloads (it does, §6.7).
Two heaps (CBV/SRV/UAV + sampler), one set bound for the frame; `slot == handle.index` holds because the
engine reserves the heap slot at the resource's slotmap index, identical to the Vulkan floor.

## State model (U17) — enhanced vs legacy barriers

`Mel_Gpu_Resource_State` is already the D3D12-shaped enum (it was authored against D3D12 state semantics).
Floor lowering: `D3D12_RESOURCE_BARRIER` transition barriers. Ceiling lowering: `ID3D12GraphicsCommandList7::
Barrier` enhanced barriers (sync/access/layout split — the native form Vulkan sync2 mirrors). The per-CL
per-subresource tracker (U17) is shared; only the lowering differs.

## Rendering (U16)

Dynamic rendering is native: `ID3D12GraphicsCommandList4::BeginRenderPass` / `EndRenderPass` with
`D3D12_RENDER_PASS_RENDER_TARGET_DESC` load/store, or the plain `OMSetRenderTargets` + clear floor. No
render-pass-object compile step (the Vulkan `!dynamic_rendering` floor has no D3D12 analog).

## Phases (no-prerequisite-first)

0. **Device foundation** — build wiring (`--gpu=d3d12` on win32), DXGI factory + adapter enumeration, caps
   probe (adapter/memory/shader/queries/bindless-tier/sampler), `D3D12CreateDevice` + direct command queue,
   slotmap tables + pump + tracker + deferred-free watermark, headless create/destroy. Proves the toolchain
   and the U1–U6 device layer against a second API. Tests: `d3d12_device.{instance_adapters_caps,
   create_and_destroy_headless}`.
1. **Queues + allocator + buffers** — U7 direct queue submit→future over an `ID3D12Fence` + event poller;
   U8 allocator over committed + placed resources (heap-type DEFAULT/UPLOAD/READBACK); U9 buffers
   (`buffer_write`, mapped UPLOAD, DEVICE via copy). Tests mirror `vk_alloc`/`vk_queue`.
2. **Textures + barriers + command lists + rendering** — U10 textures/views (DXGI formats, `WriteToSubresource`
   / staged copy), U17 legacy `ResourceBarrier` + the shared per-CL tracker, U15 standalone command lists
   (per-thread `ID3D12CommandAllocator`), U16 `BeginRenderPass`/`OMSetRenderTargets`. Headless clear→readback
   pixel test.
3. **Pipelines + bindless + reflection** — U13 graphics+compute PSOs + root signatures (reflection-derived),
   U14 `ResourceDescriptorHeap` bindless heap + root-record indices, U12 DXIL passthrough reflection
   (`shader_create_from_bytecode`; DXC reflection or a DXIL container reader). Bindless sample + storage-buffer
   compute pixel tests, mirroring the Vulkan binding-model suite.
4. **Swapchain (DXGI)** — U18 `IDXGISwapChain3` flip-model over the HWND, frame-latency waitable, tearing;
   `hello-gpu` cube on D3D12.

## Bar

Each phase: headless pixel/clean tests, zero debug-layer errors, zero leaks (ReportLiveObjects), on the
win-pilot RTX 2060. The Vulkan suite stays green (untouched). Build/test only on win-pilot over SSH.
