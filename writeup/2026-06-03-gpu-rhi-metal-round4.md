# GPU RHI — Metal backend bring-up (round 4)

## Work done

Greenfield Metal backend for `modules/gpu`, gated on `--gpu=metal` for macOS. Metal is the
default macOS GPU per CLAUDE.md and a first-class spec target (design/gpu-rhi.md §2), but no
backend existed; `src/` held only `vulkan/` and `d3d12/`. This round lands a **runnable
clear-and-present skeleton** on Apple Silicon (verified on an M3 Pro).

### Build wiring (`modules/gpu/build.c`)
Added a Metal gate parallel to the vulkan/d3d12 gates, without disturbing them:

    mel_sources(lib, WHEN(.gpu = "metal", .platforms = MEL_ON(MACOS)), "src/metal/macos/*.m");
    mel_defines(lib, MEL_PRIVATE, WHEN(.gpu = "metal"), "MEL_GPU_METAL=1");
    mel_link(lib, MEL_PUBLIC, WHEN(.gpu = "metal", .platforms = MEL_ON(MACOS)),
             "-framework", "Metal", "-framework", "QuartzCore", "-framework", "Foundation", "-framework", "AppKit");

No `modules/build` change was needed: nob already validates `metal` for macos/ios
(`driver.c:gpu_valid`) and already defaults macos to `metal` (`resolve.c`). No codegen
registration was needed (the undocumented pass was not touched).

### Backend translation units (`modules/gpu/src/metal/macos/`)
- `mtl_backend.h` — internal structs (instance/adapter/device/surface/swapchain/command-list/
  queue, resource objects), resource-table + submit-serial + tracker helper decls. ObjC object
  fields stored as `id<...>` (files are `.m`, ARC on).
- `instance.m` — `MTLCopyAllDevices` adapter enumeration, caps probe per adapter.
- `caps.m` — honest Metal cap tiers (see "what runs" below).
- `device.m` — `MTLDevice` device + `MTLCommandQueue`, slotmap resource tables, submit-serial /
  retire bookkeeping, tracker passthrough, memory budget from `recommendedMaxWorkingSetSize` /
  `currentAllocatedSize`, `device_create_default`.
- `queue.m` — queue request/release/info; `queue_submit` commits the command list's
  `MTLCommandBuffer` and bridges its `addCompletedHandler` to the U3 future via
  `mel_gpu_future_resolve` (the shared future code posts cross-thread via `mel_reactor_post`,
  exactly the §3.3 "thread-callback bridged by mel_reactor_post" path).
- `swapchain.m` / `surface.m` — `CAMetalLayer` over the cocoa `NSView` native handle (reuses the
  platform-surface path the vulkan macos TU also uses); `displaySyncEnabled` from vsync.
- `record.m` — frame clear+present spine: `frame_begin` (nextDrawable + command buffer),
  `cmd_begin_pass` (render encoder, `MTLLoadActionClear`), `cmd_end_pass`, `frame_end` (present +
  commit, completion handler retires the serial). Standalone command lists. Draw/bind/dispatch/
  copy `cmd_*` are loud once-per-command-list no-ops.
- `rendering.m` — `cmd_begin_rendering`/`end_rendering` real against attachment texture views
  (clears honored), so offscreen passes set up correctly even though draws inside are no-ops.
- `resources.m` — real `MTLBuffer` (shared/private), `MTLTexture`, texture views
  (`newTextureViewWithPixelFormat:...` for subranges), `MTLSamplerState`. Host buffer write/map
  for shared storage.
- `pipeline.m` — shader/pipeline create return loud unsupported (no SPIR-V→MSL this round).
- `misc.m` — bindless / device-address / sync / query / bind-group / format-properties.
  `format_properties` returns a real Metal-derived bitset; the rest are honest absent/unsupported.
- `format_map.m` — `Mel_Gpu_Format` ↔ `MTLPixelFormat`.

## What runs vs what is stubbed

**Runs (real Metal):**
- Instance + adapter enumeration (`Apple M3 Pro` reported), device + command queue.
- Honest caps: UMA → `host_visible_device_local = full_uma`, `persistent_map = true`,
  `timeline = native`, fp16/int16/int8/wave true, `tile_local = native`, anisotropy, memory budget.
- CAMetalLayer swapchain (BGRA8, vsync), resize.
- **Clear-and-present**: every frame acquires a drawable, clears it to the requested colour, and
  presents. Verified with `HELLO_GPU_AUTO=triangle` — window opens, clears, presents per tick.
- Buffer/texture/view/sampler create+destroy; host buffer write/map for shared storage.
- `format_properties` (Metal-derived), memory budget.
- U3 completion bridge: `MTLCommandBuffer` completion handler → future → reactor.

**Stubbed (loud, never silent — MEL-ENGINE-VIII):**
- Shaders / pipelines: SPIR-V is not translated to MSL → `*_create` return error status
  (`NO_CODE` / `NO_SHADER`) with the cause logged. Consequently the triangle and every
  pipeline-driven hello-gpu demo open + clear but draw nothing; `cmd_bind_pipeline`/`cmd_draw`
  warn once per command list.
- Bindless: reported `tier = none`; `*_bindless_slot` log an error and return 0.
- Sync primitives, query pools, bind groups: return unsupported status / null handles, logged.
- Device-local buffer/texture upload: no staging-copy path → logged error (host-visible writes
  work).
- External import/export, budget-pressure callback: logged unsupported.

## Kludges (confessed in full — MEL-ENGINE-VIII)

1. **Barriers are no-ops** (`cmd_texture_barrier`/`cmd_buffer_barrier`). Within one Metal command
   buffer the driver does automatic hazard tracking on the single queue, so an explicit barrier is
   genuinely unnecessary here — this is faithful P1 emulation, not a lie. It will need real work
   (`MTLFence` / Metal 4 barrier API / encoder boundaries) once async-compute or cross-queue lands.
   Left silent (not even a warn) deliberately: warning every barrier call would be noise, and the
   behaviour is correct. Flagged here so it is not mistaken for completeness.
2. **No SPIR-V→MSL.** The whole shader/pipeline lane is stubbed. This is the single largest gap to
   M1 parity. The apps embed SPIR-V (`*_spv.h`); Metal needs either SPIRV-Cross / a Metal IR path
   or MSL source shaders. Out of scope for a runnable-skeleton round.
3. **Swapchain image count / frames-in-flight not modelled.** The Metal path relies on
   `CAMetalLayer`'s internal drawable pool (default 3) and the `maximumDrawableCount` default; I do
   not expose or pin it. `nextDrawable` can block; acceptable for the skeleton.
4. **`adapter.uuid`/`luid` not populated** (the `@available` block is a placeholder). Metal exposes
   no LUID; a registryID-derived UUID could be synthesised later.
5. **`device_local_bytes`/`host_visible_bytes`** both report `recommendedMaxWorkingSetSize` on UMA;
   there is no separate VRAM pool to report, which is honest for Apple Silicon but coarse.
6. **No U5 native-interop header** (`include/gpu/metal/<integration>.h`) this round — §13 lists it
   but no consumer needs it yet; deferred (purely additive).

## What M1 parity on Metal still needs
- SPIR-V→MSL (or MSL shader source) → real `shader_create` / `pipeline_create` → triangle draws.
- Vertex/index buffer binding, push constants (Metal `setVertexBytes`/argument buffers), draws.
- Real barriers for multi-queue / async-compute (Metal 4 barrier API or `MTLFence`).
- Bindless via `MTLArgumentEncoder` / Metal 4 `MTL4ArgumentTable` (spec §2 ceiling).
- Device-local upload via a blit-encoder staging path; texture upload.
- Query pools (`MTLCounterSampleBuffer`), sync primitives (`MTLSharedEvent`, already the §3.5
  external-sync target).

## CLAUDE.md suggestions (recommendations only — not applied)
- None this round. The build-axis gating story in CLAUDE.md / platforms.md was sufficient to wire a
  new backend without guidance gaps.

## Suggestions
- The hello-gpu apps hard-embed SPIR-V; to let Metal reach triangle parity without a translator,
  consider an MSL variant of the trivial shaders behind a `WHEN(.gpu="metal")` source gate, or
  adopt a build-time SPIRV-Cross codegen pass (would need the codegen-registration story
  documented first — currently halt-and-query).
- The per-backend test targets (`gpu-vulkan`, `gpu-stress`, …) are all `MEL_GPU_VULKAN`-gated and
  silently skip under metal. A small `gpu-metal` smoke test (instance→device→swapchain→one
  clear-present→teardown, headless via an offscreen `MTLTexture`) would catch backend regressions
  in CI without a window.
