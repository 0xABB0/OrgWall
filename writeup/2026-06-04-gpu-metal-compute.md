# GPU Metal — compute path: pipeline, encoder transitions, dispatch, barriers (task #30)

Brought the Metal backend from loud-stubbed compute to a working compute path: a
`MTLComputeCommandEncoder` is opened/closed cleanly against the render encoder, compute pipelines
bind and dispatch, and the compute↔graphics encoder boundary carries the cross-stage ordering on the
single Metal queue. Proven by two new inline-MSL tests with exact readback.

## Work done

### Encoder model (`record.m`, `mtl_backend.h`)
The command list held only `id<MTLRenderCommandEncoder> encoder`. Added a peer
`id<MTLComputeCommandEncoder> compute_encoder`, plus the bound compute state
(`compute_state`, `compute_threadgroup`, `has_compute_pipeline`). Metal allows exactly one open
encoder per command buffer, so all open paths funnel through one helper:

- `mel_gpu__cmd_end_active_encoder(cmd)` — ends whichever of render/compute is open and clears it.
  This is the single place encoders close.
- `mel_gpu__cmd_open_compute_encoder(cmd)` — ends any open encoder, opens a compute encoder.

Wired it into every transition site so no encoder ever leaks and no two are open at once:
`frame_begin` / `frame_end` (reset/close), `command_list_begin` / `_end` / `_destroy`,
`cmd_begin_pass`, `cmd_begin_rendering_opt` (rendering.m), `cmd_copy_texture_to_buffer`,
`cmd_copy_buffer`. The two copy paths and the two begin-render paths previously ended only the render
encoder; they now end whatever is active, so a copy or a render pass after a dispatch is correct.

### Compute pipeline bind + dispatch (`record.m`)
- `cmd_bind_pipeline` on a compute pipeline: opens the compute encoder,
  `setComputePipelineState:`, caches the pipeline's threadgroup. (Compute pipeline *creation* —
  `newComputePipelineStateWithFunction:`, retain-once/release-once — was already implemented in
  `pipeline.m` from the render round; it needed no change. `threadgroup` is the pipeline's
  `threadExecutionWidth × 1 × 1`.)
- `cmd_dispatch(gx,gy,gz)` → `dispatchThreadgroups:threadsPerThreadgroup:` with the cached
  threadgroup. `(gx,gy,gz)` are threadgroup counts (Vulkan local-size semantics: total threads =
  groups × threadsPerThreadgroup).
- `cmd_dispatch_indirect(args,offset)` →
  `dispatchThreadgroupsWithIndirectBuffer:indirectBufferOffset:threadsPerThreadgroup:`.
- Both loud-fail (no silent no-op) when no compute encoder / no bound compute pipeline.

### Compute resource binding (`record.m`)
There is **no public cmd-level storage-bind command** — the engine's generic binding path is bind
groups (`cmd_bind_descriptor_set`), which on Metal is loud-stubbed in `misc.m` (out of this lane), and
the bindless path every hello-gpu compute screen uses is `caps.bindless = none` on Metal. So within
this lane the storage-bind surface is the existing buffer-bind call routed to the compute encoder:
- `cmd_bind_vertex_buffer(slot, buf)` on an active compute encoder → `setBuffer:offset:atIndex:slot`
  (ascending index = the natural MSL `[[buffer(slot)]]`). Slot 0 is reserved for push-constants and is
  loud-rejected on the compute encoder (the kernel reads params at `[[buffer(0)]]`).
- `cmd_push_constants(0, n, data)` on an active compute encoder → `setBytes:length:atIndex:0`.
  Nonzero offset is still loud-rejected (push constants ride a single buffer slot). This keeps the
  index convention identical to the graphics path: params/push-constants at buffer index 0.

### Barriers (`record.m`)
`cmd_texture_barrier` / `cmd_buffer_barrier` were pure no-ops. Now:
- When a compute encoder is open, emit `memoryBarrierWithScope:` (buffers, or buffers|textures) —
  the within-encoder dispatch-to-dispatch hazard (e.g. fill-args then indirect-dispatch).
- The compute→graphics (and any cross-encoder) hazard is carried by the **encoder boundary**: ending
  the compute encoder before the render encoder opens makes prior writes visible to the next encoder
  on the single queue. This is exactly what the RHI spec (§7.3) says Metal lowers sync to. The barrier
  call is therefore correct, not a silent drop: the ordering is real, supplied by the boundary the
  begin-render path now always inserts.

The state-enum args (`src`/`dst`) are accepted but not consulted — Metal's barrier scope is
resource-class, not the D3D12 state pair; the boundary + scope cover the visible hazard.

## Tests (`test/test_metal.c`, inline MSL — mirrors metal-loud)

- `metal_compute.storage_buffer_write`: a compute kernel writes `out[i] = i*3 + 7` to a STORAGE +
  READBACK buffer (host-shared on UMA, so readback is a direct map). Binds the buffer at slot 1,
  pushes `n` at index 0, dispatches `(N+31)/32` groups (the `/32` is the caps SIMD width
  `subgroup_size_min == subgroup_size_max == 32`). Asserts **all 256** elements exactly. Since the
  pattern is nonzero for every i and the buffer is fresh, a no-op dispatch would fail `out[0] == 7`.
- `metal_compute.compute_to_graphics`: a compute kernel generates a triangle's vertices
  (`Tri_Vertex`-laid-out via `packed_float3 pos; packed_float4 color`, 28-byte stride matching the C
  struct) into a DEVICE (private) buffer; a `cmd_buffer_barrier` UNORDERED_ACCESS→VERTEX_BUFFER; then a
  render pass binds that same buffer as the vertex stream (slot 0 → index 30) and draws. Readback:
  center = green `(0,255,0)`, corner = clear black. This proves the compute→render encoder transition
  and the cross-encoder ordering — if the boundary didn't order the compute write before the draw, the
  triangle would be garbage/empty.

## Green-run counts
- `gpu-metal` macos --gpu=metal: **8 passed, 0 failed, 0 skipped, of 8** (6 prior + 2 new),
  MEL_TEST_NOFORK=1. Same 8/8 under `--release`.
- `gpu-foundation` macos --gpu=metal: **13 passed, 0 failed, of 13**.
- `gpu-resources` macos --gpu=metal: **4 passed, 0 failed, of 4**.
- Clean rebuild: no warnings on `record.m` / `pipeline.m` / `rendering.m` / `mtl_backend.h`.
- `clang-format --dry-run -Werror` clean on all four edited files.

## Allocation / lifetime notes
- No new retained objects in the encoder path. The compute encoder is autoreleased by the command
  buffer (same as the render/blit encoders); `compute_state` / `compute_encoder` are unretained `id`
  caches on the command list, cleared on every `_end` / `_destroy`. No per-dispatch allocation.
- The `MTLComputePipelineState` lifetime is unchanged: retained once at `pipeline_compute_create`
  (`__bridge_retained`), released once in `pipeline_destroy` (`__bridge_transfer`) — the existing
  graphics-pipeline discipline.

## Kludges / debt (confessed — bar zero, MEL-ENGINE-VIII)
1. **Storage binding rides `cmd_bind_vertex_buffer` on the compute encoder.** The name is graphics-y,
   but it is the only public buffer-bind surface and the `setBuffer:atIndex:` call is encoder-agnostic.
   The proper engine path is bind groups (`cmd_bind_descriptor_set`), loud-stubbed on Metal in
   `misc.m` (out of this lane), and bindless (`caps.bindless = none` on Metal). When either lands on
   Metal, compute binding should move there and this overload should retract to graphics-only. The
   index convention (params@0, buffers@slot) is documented and coherent with the graphics path, so the
   move is mechanical.
2. **No public texture-bind for compute.** A storage-image compute test (write a `MTLTexture` via
   `setTexture:atIndex:`) needs a texture-bind command that does not exist in the public surface
   (only via bind groups / bindless, both unavailable). So the compute→graphics proof uses a
   compute-written **vertex buffer** consumed by the draw, not a compute-written texture sampled by
   the draw. The encoder transition + barrier it exercises are identical; only the resource class
   differs. A texture-output compute test is blocked on the same bind-group/bindless lane as #1.
3. **Threadgroup is `threadExecutionWidth × 1 × 1`, 1-D only.** Metal supplies threads-per-threadgroup
   at dispatch, not from the MSL kernel (MSL has no `[numthreads]`); the public
   `Mel_Gpu_Pipeline_Compute_Opt` carries no threadgroup, and Metal can't reflect a Vulkan/Slang
   `local_size`. So the backend picks a 1-D `threadExecutionWidth` group and the kernel must use
   `[[thread_position_in_grid]]` with a bounds check. A 2-D/3-D local size (tile compute, image
   kernels) would need the threadgroup shape to come from shader reflection — a Slang-lane concern, or
   a future field on the compute opt. The two tests are 1-D and bounds-checked, so this is correct for
   them; flagged so 2-D dispatch is not mistaken for done.
4. **Barrier ignores the `src`/`dst` state pair.** Metal's `memoryBarrierWithScope:` is resource-class,
   not a D3D12 state transition; the visible hazard is covered by scope + encoder boundary, but the
   richer state intent (e.g. UNORDERED_ACCESS vs SHADER_RESOURCE read-vs-write) is not used to narrow
   the scope. Acceptable for correctness; a finer lowering could pick `MTLBarrierScope` from the
   states.
5. **Tests compile MSL at load (`newLibraryWithSource:`).** Same as the render tests — a per-test
   AIR compile, fine for a test, not a shipping path.

## What's still loud-stubbed (unchanged, honestly gated)
- Bind groups / descriptor sets on Metal (`misc.m`) — loud error, not in this lane.
- Bindless (`caps.bindless = none`) — pipeline create loud-rejects bindless; the hello-gpu compute
  screens (bloom/boids/mandelbrot/reacdiff/plasma) bind storage **only** via bindless, so they remain
  unrunnable on Metal until bindless or a Slang argument-buffer path lands. That is the binding lane,
  orthogonal to this encoder mechanism.
- `pipeline_compute_create` spec-constants — warned-ignored (no function-constant lowering), unchanged.

## Open questions for Gabbo
- **Compute resource binding surface.** Is the intended Metal path (a) bind groups lowered to argument
  buffers, (b) emulated bindless (a global argument-buffer heap), or (c) a new explicit
  `cmd_bind_storage_buffer` / `cmd_bind_storage_texture` cmd? My overload of `cmd_bind_vertex_buffer`
  is a stopgap; the real answer decides whether the hello-gpu compute screens run on Metal.
- **Compute threadgroup / local-size.** Should the compute pipeline carry an explicit threadgroup
  (3-D) on `Mel_Gpu_Pipeline_Compute_Opt`, populated by Slang reflection of `[numthreads]` /
  `local_size`? Metal cannot reflect it, so without that field 2-D/3-D dispatch can't pick a correct
  group shape.

## CLAUDE.md suggestions (recommendations only — not applied)
None this round.

## Suggestions
- A compute threadgroup field driven off Slang reflection would make N-D dispatch correct on every
  backend and let Metal pick the matching group shape instead of defaulting to 1-D.
- Once bind groups land on Metal (argument buffers), retract the compute storage-bind overload and add
  a storage-image compute test (write a texture, sample it in graphics) to close kludge #2.
