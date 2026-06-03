# gpu

The Melody Render Hardware Interface: one explicit, capability-rich GPU surface that lowers
faithfully across backends. It is designed to the most-capable APIs — ceiling Vulkan Roadmap 2026 /
D3D12 SM 6.9 / Metal 4 — and degrades, never deforms, toward each backend's support floor; caps
report the tier, the API shape is the same, only the lowering differs (`design/gpu-rhi.md` §1 P1/P2,
§2). Resources are value-typed handles over per-device slotmaps (§3.1); creation is reactor-driven
futures (§3.3); features are request-and-grant with no silent default (§3.4, MEL-CODE-007); failure
is loud (MEL-ENGINE-VIII).

## Backends

The spec defines four backend targets, each with a ceiling and a floor (`design/gpu-rhi.md` §2). What
the tree implements today:

- **Vulkan** — the M1/M2 surface, runnable. Floor Vulkan 1.2 with feature probes; ceiling Roadmap
  2026. Built and tested on macOS via the Homebrew MoltenVK loader (`src/vulkan/macos/*.m`, Metal
  surface) and natively on win32 against the Vulkan SDK loader (`src/vulkan/windows/*.c`). Selected
  with `--gpu=vulkan`; gated by `MEL_GPU_VULKAN`.
- **D3D12** — co-primary, brought up on `win-pilot` (Windows 10 22H2, in-box Windows SDK) through the
  Phase-3 binding model. The in-box Win10 floor cannot honor SM 6.6 `ResourceDescriptorHeap` direct
  indexing, so bindless lowers to the §6.7 D3D12 *floor* — classical descriptor heaps with
  root-signature tables, first-class, not degraded. `ResourceDescriptorHeap` direct indexing and the
  rest of the SM 6.9 ceiling ride the Agility SDK / Win11 and are deferred. Selected with
  `--gpu=d3d12` on win32; gated by `MEL_GPU_D3D12`.
- **Metal / WebGPU** — spec targets (§2), not yet built. Future (M4).

## Dependencies

`core`, `allocator`, `collection`, `reactor`, `executor`, `future`, `log`, `debug`, `string`,
`thread`, `time`, `thermal`, `power` (see `build.c`). The completion future is a thin wrapper over
the shared `future` substrate; the pump retains only the reactor timer and fence-poller list.
Headers are consumed as `<gpu/...>`.

## Binding model

The contract a consumer must know (`design/gpu-rhi.md` §6.7; headers `binding.h`, `bind_group.h`,
`handle.h`). Two peers, both first-class: the **device bindless heap** and the **classic
descriptor-set path**.

### Bindless heap (the simple-is-powerful path)

- One **device-global bindless heap** lives at **set 0**: a persistent integer-indexed descriptor
  array per resource class. The binding index within set 0 selects the **heap class** (texture views,
  samplers, storage buffers, uniform buffers, storage images). Available only when the device was
  created with `Mel_Gpu_Feature_Request.descriptor_indexing` (`mel_gpu_bindless_available`).
- **`slot == handle.index` for the direct families.** Engine-created `Mel_Gpu_Buffer`,
  `Mel_Gpu_Texture_View`, `Mel_Gpu_Sampler` auto-register at their slotmap index at creation and keep
  that slot until destroy — no per-frame rebinding. Query the shader-visible slot rather than assume
  it: `mel_gpu_{texture_view,buffer,sampler}_bindless_slot`. Over-capacity registration fails loudly
  with `..._CREATE_BINDLESS_SLOT_EXHAUSTED` (heap-class caps are fixed today; grow-on-demand is owed).
- **Indirect family.** `handle.h` provides `MEL_GPU_HANDLE_INDIRECT` (slot carried separately,
  resolved via `*_bindless_slot`). The realized public surface uses it for **one type only** —
  `Mel_Gpu_Sampler_Indirect` — and it is **engine-owned** (the compacted-heap / capped-bindless form);
  `sampler.h` is explicit that no foreign-sampler import exists. The spec §3.1 describes the broader
  indirect-for-imports family (buffer / texture-view / accel-struct, import → `Borrowed`); those peer
  types are not yet in the public headers.
- **Per-draw root record.** The shader receives one record per draw/dispatch carrying the heap
  indices (and, on the ceiling, buffer device addresses). On the Vulkan floor the carrier is a
  **push-constant block** the user fills via `mel_gpu_cmd_push_constants`; its offset/range come from
  reflection, not by hand. `caps.memory.bindless.{binding_model, root_record_payload}` report the
  active shape.
- **BDA duality.** `mel_gpu_buffer_device_address` (buffer with `MEL_GPU_BUFFER_DEVICE_ADDRESS` usage,
  device with `buffer_device_address`) returns a stable GPU address for a **pointer-bearing** root
  record (the ceiling; `root_record_payload = pointers | mixed`). The **floor** carries a
  descriptor-index payload instead (`root_record_payload = descriptor_indices`). On D3D12 the call
  returns a real VA, but the payload is always `descriptor_indices` — buffers are addressed through
  CBV/SRV/UAV descriptors, so the shader cannot dereference it as a buffer reference the way Vulkan-BDA
  does.
- Bind the heap with `mel_gpu_cmd_bind_bindless`; `mel_gpu_cmd_bind_pipeline` does it automatically for
  a pipeline created with `bindless = true`.

### Classic path (the P2 peer)

App-owned descriptor sets, a peer of the heap, not a fallback (`bind_group.h`). Declare
`Mel_Gpu_Bind_Group_Layout` entries, build a non-bindless pipeline layout from them
(`Mel_Gpu_Pipeline_Opt.set_layouts`, mutually exclusive with `bindless`), allocate `Mel_Gpu_Bind_Group`s,
write resources (`mel_gpu_bind_group_write_*`), bind via `mel_gpu_cmd_bind_descriptor_set`. Reflection
distinguishes a runtime-array set 0 (the heap signature) from a sized/app-owned set 0, so a classic
set-0 shader is not force-marked bindless. **Vulkan only** today; the D3D12 backend rejects `set_layouts`
with `MISSING_FEATURE`.

### Reflection-derived vertex input

When `Mel_Gpu_Pipeline_Opt.vertex_layout` is omitted, `pipeline_create` derives the vertex-input
layout from shader reflection (Vulkan: SPIR-V `Input` locations → tight-packed stride; D3D12:
`reflect.c` reads the DXIL `ISG1`/`ISGN` input signature). An explicit `vertex_layout` overrides it.
The reader handles `float` vec2/3/4; any other format withdraws the whole derived layout with a
warning rather than miscompile. On D3D12 `push_constant_size` and the `bindless` flag are **not**
reflected (the in-box build lacks the DXC reflection SDK) — pass them explicitly.

### Pipeline-create diagnostics

`MISSING_BINDLESS_SLOT{heap, slot}` (layout demands heap slot N, heap sized < N+1 — grow the heap) is
kept distinct from `MISSING_FEATURE{atoms}` (granted caps lack a required capability — request more);
conflating them is the diagnostic dilution MEL-ENGINE-VIII forbids.

## Build / test / run

Targets (`build.c`): the library `gpu`, plus tests `gpu-foundation`, `gpu-vulkan`, `gpu-stress`,
`gpu-concurrency`, `gpu-visual`, `gpu-bench` (all `--gpu=vulkan`, bodies `#if MEL_GPU_VULKAN`-guarded) and `gpu-d3d12`
(`--gpu=d3d12` on win32, `#if MEL_GPU_D3D12`-guarded; an empty 0-test runner otherwise).

macOS Vulkan tests need the Homebrew validation/loader dylib on the path and the no-fork mode
(MoltenVK cannot reinitialize across `fork`):

    DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1 ./nob test gpu-vulkan macos --gpu=vulkan
    DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1 ./nob test gpu-stress  macos --gpu=vulkan
    DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1 ./nob test gpu-visual  macos --gpu=vulkan
    DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1 ./nob test gpu-concurrency macos --gpu=vulkan
    DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1 ./nob test gpu-bench   macos --gpu=vulkan

D3D12 runs on `win-pilot` (CLAUDE.md "Windows builds (remote)"):

    ssh win-pilot "cd /d D:\repo\OrgWall && C:\Users\Gabbo\dev.cmd nob test gpu-d3d12 win32 --gpu=d3d12"

The `hello-gpu` app honors the `HELLO_GPU_AUTO` env hook (`apps/hello-gpu/src/main.c`) — set it to a
screen name (`cube`, `lorenz`, `texquad`, `plasma`, `depth`, `layers`, `post`, `instances`, `gallery`;
any unrecognized value falls through to the triangle) to auto-open that screen headless for a few
seconds, the harness the showcase screens use for clean-exit verification:

    DYLD_LIBRARY_PATH=/opt/homebrew/lib HELLO_GPU_AUTO=cube ./nob run hello-gpu macos --gpu=vulkan

## Layout

- `include/gpu/` — the public C API (per-resource headers: `device`, `buffer`, `texture`, `sampler`,
  `pipeline`, `command`, `binding`, `bind_group`, `caps`, …).
- `src/` — backend-agnostic glue (`format.c`, `future.c`, `render_source.c`, `threading.c`).
- `src/vulkan/` — the Vulkan backend; `src/vulkan/macos/` (Metal surface, Objective-C),
  `src/vulkan/windows/` (Win32 surface).
- `src/d3d12/` — the D3D12 backend (win32).
- `test/` — `test_foundation.c`, `test_vulkan.c`, `test_stress.c`, `test_visual.c`, `test_d3d12.c`,
  with embedded SPIR-V/DXIL fixtures.
