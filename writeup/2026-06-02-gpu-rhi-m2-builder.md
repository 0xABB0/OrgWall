# GPU RHI M2 — builder slices + stress-audit defect fixes (Vulkan)

Continues the GPU RHI rewrite (`design/gpu-rhi.md`) on the deferred-M2 backlog
(`writeup/2026-06-02-gpu-rhi-m2-phase-a.md`, `…-binding-finish.md`). Runnable half is
Vulkan/macOS over MoltenVK (Apple M3 Pro, Vulkan 1.2.334), headlessly verified. The session
landed five additive backlog slices, then absorbed a mid-session re-prioritisation from the
stress audit (`writeup/2026-06-02-gpu-rhi-stress-audit.md`) and fixed all five of its confirmed
defects (CRITICAL-1, CRITICAL-2, MAJOR-3, MAJOR-4, MAJOR-5).

**gpu-vulkan 28 → 32 (4 new tests), gpu-foundation 8/8, zero validation errors, zero leaks,
zero VUIDs.** The only logged `[ERROR]`s are the three intentional negative-test diagnostics
(MissingFeature, MissingBindlessSlot, BindlessSlotExhausted, MEL-ENGINE-VIII). Both lowerings are
active on host: `render lowering: dynamic rendering`, `barrier lowering: synchronization2`.

## Work done

### Slice 1 — shared `mel_gpu__build_pipeline_layout` helper (`pipeline.c`, MEL-ENGINE-IX)
Pure refactor. The duplicated binding-model gate, spec-info build, and set-layout composition in
`pipeline_create` / `pipeline_compute_create` are factored into three shared statics:
`mel_gpu__binding_gate` (MissingFeature / MissingBindlessSlot), `mel_gpu__build_spec_info` (one
`VkSpecializationInfo`; graphics warns on >4-byte spec constants, compute does not, parameterised),
and `mel_gpu__build_pipeline_layout` (bindless set 0 / classic set layouts / optional graphics
static-sampler set + `vkCreatePipelineLayout`). Behaviour unchanged; the 28 stayed green.

### Slice 2 — storage-image bindless end-to-end (`bindless_spv.h`, `test_vulkan.c`)
`imgwrite.comp` writes a gradient into one heap-resident storage image addressed purely by its
bindless slot in a push-constant root record (binding 4 = storage-image class); the image is
barriered UnorderedAccess→CopySource and copied to a READBACK buffer, gradient pixel-verified.
Closes the storage-image heap class the binding-finish writeup flagged unproven (storage-buffer
was the only prior proof). No backend code change — the auto-registration path already existed.

### Slice 3 — `cmd_dispatch_indirect` + INDIRECT buffer usage (`command.{h,c}`, `buffer.{h,c}`)
`mel_gpu_cmd_dispatch_indirect(cmd, args, offset)` → `vkCmdDispatchIndirect`, plus the additive
`MEL_GPU_BUFFER_INDIRECT` usage flag (`VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT`). `MEL_GPU_STATE_
INDIRECT_ARGUMENT` was already lowered. Proof (`vk_compute.dispatch_indirect`): a GPU-driven
`fillargs` pass writes the `{group_x,1,1}` triple into a STORAGE+INDIRECT buffer via bindless slot,
a buffer barrier transitions it UnorderedAccess→IndirectArgument, and the `add` kernel is dispatched
from those GPU-produced args — no CPU round-trip; `out[i]==in[i]+1` verified.

### Slice 4 — synchronization2 barriers where granted (`device.c`, `record.c`, `vk_backend.h`, U17 §7.3)
`VK_KHR_synchronization2` is probe-and-gated at device-create (extension **and** feature bit queried
via `vkGetPhysicalDeviceFeatures2`, request-and-grant per U4); `vkCmdPipelineBarrier2KHR` loaded via
`vkGetDeviceProcAddr`. `cmd_texture_barrier` / `cmd_buffer_barrier` lower onto `VkImageMemoryBarrier2`
/ `VkBufferMemoryBarrier2` + `VkDependencyInfo` with the `pipeline_stage_2`/`access_2` enums when
granted; the legacy `vkCmdPipelineBarrier` path is retained as the §7.3 floor. State tracking is
path-independent. On host sync2 is granted, so **every** existing offscreen-render / barrier test now
exercises the sync2 path; the layouts are identical across paths, so behaviour matches.

### Slice 5 — MSAA resolve in dynamic rendering (`rendering.h`, `record.c`, `test_vulkan.c`, U16 §7.2)
Additive `Mel_Gpu_Color_Attachment.resolve_view`. When set and the attachment is multisample,
`cmd_begin_rendering` resolves it to single-sample with `VK_RESOLVE_MODE_AVERAGE` (the §7.2
screen-space color default) via `VkRenderingAttachmentInfo.resolveMode/resolveImageView` in the same
pass — the multisample surface need never be stored (U22). A non-live resolve view warns and skips
(MEL-CODE-007). Proof (`vk_render.msaa_resolve_readback`): a 4-sample white-triangle render with
DONT_CARE store resolves into a single-sample target, copied to READBACK, resolved pixel verified
white. The full `mode_per_aspect` / depth-stencil resolve surface is a later slice.

### Stress-audit defects

**CRITICAL-1 — bindless over-cap silent drop** (`binding.c`, `texture.c`, `buffer.c`, `sampler.c`).
The `slot == handle.index` contract (§3.1) collides with the fixed per-class heap cap: an over-cap
slotmap index made `mel_gpu__bindless_check` `mel_assert`-crash in debug and silently drop the
descriptor in release while reporting the resource OK — a shader then sampled an unbound slot (silent
corruption, MEL-ENGINE-VIII). Fix: the register functions now return `bool` and no longer assert as
control flow; `texture_view_create` / `buffer_create` / `sampler_create` pre-flight **every** heap
class the resource registers into (`mel_gpu__bindless_slot_fits`) **before** writing any descriptor —
an over-cap slot fails the create loudly with a new `…_BINDLESS_SLOT_EXHAUSTED` status and rolls back
(no partial heap write, no unbound slot reported OK). The duplicate heap-cap lookup is folded into a
shared `mel_gpu__heap_cap_for_class` (MEL-ENGINE-IX). Proof
(`vk_bindless.sampler_over_cap_fails_loudly`): fills the sampler class to its cap, then asserts the
(cap+1)-th create returns `BindlessSlotExhausted` — no crash, no silent OK.

**CRITICAL-2 — `cmd_begin_rendering` color truncation** (`record.c`). The color array was a fixed
`[8]` stack array that silently truncated attachments past 8 (MEL-CODE-002/007). Now allocated from
`dev->alloc` sized to `opt.color_count`, matching the pipeline path; freed after the record call.

**MAJOR-3 — allocator live-bytes under-report** (`memory.c`). The buddy reserves `next_pow2(size)`
(clamped to `MEL_GPU_MIN_BLOCK`) but accounting recorded the unrounded request — under-reporting VRAM
by the rounding slack (up to ~2×), skewing `memory_budget`'s self-reported path and delaying
`budget_pressure` (MEL-ENGINE-III/VI). Both buddy alloc sites now store and add the rounded consumed
size; `mem_free` subtracts the same, net-balanced. Dedicated allocations are exact and unchanged.

**MAJOR-4 — off-band uploads bypass the watermark** (`buffer.c`, `texture.c`). Both staging helpers
`vkQueueSubmit` + `vkQueueWaitIdle`'d without reserving a serial or advancing the retirement watermark
(§3.3) — invisible to the engine's single retirement clock, a latent UAF the moment uploads go async.
Each now reserves a serial, routes the staging buffer + memory through the deferred-free queue (gated
at that serial), and advances the watermark via `submit_complete` after the synchronous WaitIdle drains
the submit — making the upload a first-class watermark participant.

**MAJOR-5 — `queue_submit` null-deref** (`queue.c`). A positive `command_list_count` paired with a
NULL array (or a NULL entry) null-dereferenced. Now guards loudly (`mel_assert` + log) and degrades to
an empty fence-only submit in release; a NULL queue is asserted; `count==0` empty submit stays
legitimate.

## Public-header additions (additive only — coordinate with app/tester)
- `MEL_GPU_BUFFER_INDIRECT` (`buffer.h`) — indirect-args buffer usage.
- `mel_gpu_cmd_dispatch_indirect` (`command.h`).
- `MEL_GPU_{TEXTURE_VIEW,BUFFER,SAMPLER}_CREATE_BINDLESS_SLOT_EXHAUSTED` (`texture.h`/`buffer.h`/
  `sampler.h`) — the loud over-cap status; **callers of these create functions on a bindless device
  should branch on it** (it is a new failure mode that was previously a crash / silent corruption).
- `Mel_Gpu_Color_Attachment.resolve_view` (`rendering.h`) — optional MSAA resolve target.

Existing call sites all keep compiling (no field/signature removed or reordered).

## Kludges and debt (confessed, MEL-ENGINE-VIII)

- **CRITICAL-1 is fix (a), not (b).** The over-cap path now fails loudly with a status, but the heap
  class caps remain fixed ceilings — a `[MEL_MAX_*]`-shaped constraint MEL-CODE-002 ultimately
  forbids. Growing the per-class `UPDATE_AFTER_BIND` heap on demand (recreate set layout + pool + set,
  re-register every live descriptor) is the truly-correct fix and is deferred: it is substantial and
  risky to implement blind, and (a) already converts silent corruption into a branchable failure. The
  audit itself called (a) the minimum honest fix. **Tracked for follow-up.**
- **sync2 lowers the same state subset as the legacy path.** The §7.3 ceiling (`VK_KHR_unified_image_
  layouts` GENERAL fast path, timeline-driven cross-queue, aliasing/ownership barriers) is not in this
  slice — only the existing load-bearing states gained a sync2 lowering. Unimplemented states fall to
  GENERAL/all-access with a warning on both paths.
- **MSAA resolve is color-average only.** `resolve_view` lowers `VK_RESOLVE_MODE_AVERAGE` for color;
  the §7.2 `mode_per_aspect` surface (depth SampleZero/Min/Max/Average, stencil SampleZero, the
  `caps.raster.depth_stencil_resolve_modes` gate + refuse-on-ungranted-mode) is a later slice. No
  depth-stencil resolve, no per-attachment mode selection yet.
- **`cmd_dispatch_indirect` is the single-dispatch lowering.** `_indirect_count` (count buffer,
  `vkCmdDispatchIndirectCount`) and the whole draw-indirect / execute-indirect family (§7.1) are
  deferred. The indirect-args buffer carries no `Mel_Gpu_Indirect_Layout`.
- **MAJOR-4 staging frees still ride a synchronous WaitIdle.** The watermark-routing makes the upload
  visible to the retirement clock and frees the staging buffer correctly, but the upload is still
  fully synchronous (`vkQueueWaitIdle` per upload). The async transfer-queue / host-image-copy path
  (§6.2) is the real fix for the per-upload stall; this slice only closed the latent-UAF window.
- **MAJOR-5 release path is a no-op degrade.** A malformed submit (null array, positive count) asserts
  in debug and becomes an empty fence-only submit in release rather than aborting — the future still
  resolves OK. This is the least-surprising non-crashing behaviour, but a release build silently
  produces an empty submit; the debug assert is the real signal.
- **The `[8]` stackbuf in `queue_submit` remains** (falls back to a dynamic alloc past 8, so it is a
  stride not a ceiling — the audit did not flag it; left as-is to keep the MAJOR-5 fix minimal).
- **MoltenVK reports "1 MB of GPU memory still allocated" at `VkPhysicalDevice` destroy.** This is a
  driver-level teardown artifact present across the whole suite (confirmed in pre-change logs on tests
  this session did not touch), NOT an engine leak — the engine's slotmap leak detector reports zero
  live resources at every `device_destroy`. Flagged here for honesty; below the engine, not actioned.
- **Enums.** The new `…_BINDLESS_SLOT_EXHAUSTED` statuses and `MEL_GPU_BUFFER_INDIRECT` extend
  existing enum families (status codes / usage bitflags) — the same protocol-mapping carve-out and
  Rule-#1 flag as every prior status/usage enum in the module.

## Forbidden / needs-Gabbo
- The **Slang / codegen path** (the `melody.binding` mixin, A3) was not touched — CLAUDE.md marks it
  undocumented (halt-and-query). The index/BDA duality remains demonstrated by hand-authored shaders,
  not one declaration. **Still needs Gabbo's go-ahead on the codegen pass.**
- **MoltenVK-absent features** (mutable descriptor types, sampler-YCbCr, descriptor-buffer
  capture-replay) were left untouched — implementing them blind would be an unverified default.

## CLAUDE.md / repo-convention suggestions (recommendations only)
- The `nob` binary is **not** committed per-worktree; a fresh worktree must bootstrap it
  (`clang -std=c23 -g -Imodules/build -o nob nob.c`) before `./nob` works. Worth a session-start note.
- `modules/gpu/readme.md` is still absent (flagged in three prior writeups). The binding-model
  conventions, the slot==index contract **and now its fixed-cap caveat**, the heap class→binding map,
  and the two barrier lowerings belong there.

## Suggestions
- **Grow-on-demand bindless heap** is the right next binding-model step — it retires the CRITICAL-1
  fixed ceiling (MEL-CODE-002) and is the natural home for the indirect family past the cap.
- The sync2 lowering is the seam for the §7.3 ceiling (`unified_image_layouts`, aliasing/ownership
  barriers, timeline cross-queue); extend `mel_gpu__state_to_barrier2` there.
- The MSAA `resolve_view` is the seam for the `mode_per_aspect` depth-stencil resolve surface.

## Shader sources (for `modules/gpu/test/bindless_spv.h` regeneration)
Appended this session (regenerate: `glslc -fshader-stage=comp -mfmt=c <src>`, wrap as
`static const uint32_t NAME_SPV[] = { … };`):

```glsl
// imgwrite.comp — storage-image bindless: gradient write to a heap-resident storage image by slot
#version 460
#extension GL_EXT_nonuniform_qualifier : require
layout(set = 0, binding = 4, rgba8) uniform writeonly image2D u_images[];
layout(push_constant) uniform Root { uint img_slot; uint width; uint height; } root;
layout(local_size_x = 8, local_size_y = 8) in;
void main() {
    uvec2 p = gl_GlobalInvocationID.xy;
    if (p.x >= root.width || p.y >= root.height) return;
    vec4 c = vec4(float(p.x) / float(root.width), float(p.y) / float(root.height), 0.5, 1.0);
    imageStore(u_images[nonuniformEXT(root.img_slot)], ivec2(p), c);
}
```
```glsl
// fillargs.comp — GPU-driven dispatch-indirect args: write {group_x,1,1} to a heap storage buffer by slot
#version 460
#extension GL_EXT_nonuniform_qualifier : require
layout(set = 0, binding = 2) buffer Buf { uint v[]; } u_buffers[];
layout(push_constant) uniform Root { uint args_buf; uint groups_x; } root;
layout(local_size_x = 1) in;
void main() {
    u_buffers[nonuniformEXT(root.args_buf)].v[0] = root.groups_x;
    u_buffers[nonuniformEXT(root.args_buf)].v[1] = 1u;
    u_buffers[nonuniformEXT(root.args_buf)].v[2] = 1u;
}
```

## Verification
- `DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1 ./nob test gpu-vulkan macos --gpu=vulkan` —
  **32/32** (the prior 28 plus `vk_compute.storage_image_bindless`, `vk_compute.dispatch_indirect`,
  `vk_bindless.sampler_over_cap_fails_loudly`, `vk_render.msaa_resolve_readback`).
- `./nob test gpu-foundation` — **8/8**.
- Grep-clean of `leak` / `VUID` / `validation error` / unexpected `[ERROR]`; the only `[ERROR]`s are
  the three intentional negative-test diagnostics. `render lowering: dynamic rendering`,
  `barrier lowering: synchronization2` active.
