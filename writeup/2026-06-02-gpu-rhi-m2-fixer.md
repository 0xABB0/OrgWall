# GPU RHI M2 — fixer (round 2): debt repayment on the Vulkan backend

Round 2 takes the consolidated kludge ledger from the round-1 team integration
(`writeup/2026-06-02-gpu-rhi-m2-team-integration.md`) and repays it with real corrections,
each with a test, on the Vulkan/MoltenVK backend. Isolated worktree off round-1 `main`.

**gpu-vulkan 32 → 36 (4 new tests), gpu-foundation 8/8. Zero VUID / leak / validation error;
the only logged `[ERROR]`s remain the three intentional negative-test diagnostics
(MissingFeature / MissingBindlessSlot / BindlessSlotExhausted, MEL-ENGINE-VIII).** Both lowerings
still active on host: `render lowering: dynamic rendering`, `barrier lowering: synchronization2`.
Host: Apple M3 Pro / MoltenVK 1.2.334, validation on.

## Fixes landed (4 of 5; #1 is blocked — see below)

### #2 — frame recorder's U17 state tracker reset at `frame_begin` (`command.c`)
The swapchain's embedded frame recorder (`sc->recorder`, a `Mel_Gpu_Command_List`) reused its
per-subresource state tracker across frames: `frame_begin` reset `cb` / `cur_layout` but **not**
`state_count`. So re-declaring `Common→RenderTarget` every frame tripped the U17 state-mismatch
assert (the tracker still held the prior frame's `Present`). Standalone CLs already reset in
`command_list_begin`; this brings the frame recorder to parity — one line, `sc->recorder.state_count = 0`.
Closes the round-1 appsmith `first_frame` bookkeeping workaround (gpu-rhi.md §7.3).

### #3 — `mel_gpu_swapchain_extent(sc) → { u32 width, height }` (`swapchain.h`/`swapchain.c`)
Public accessor for the surface-clamped backing-image extent the driver granted (`sc->extent`, set in
`images_build`, stays current across resize). A renderer sizes offscreen targets and viewport from this
instead of guessing; round-1 appsmith stretch-blitted offscreen to a fixed 1024×768 for lack of it.
NULL → `{0,0}` (no deref). New public type `Mel_Gpu_Swapchain_Extent`. **Additive** — no existing call
site changes.

### #4 — staging upload waits on its own fence, not a full-queue `vkQueueWaitIdle` (`buffer.c`, `texture.c`)
The synchronous `buffer_write`/`texture_write` staging path did `vkQueueSubmit` + `vkQueueWaitIdle`,
which idles the **whole** single graphics queue — serializing every unrelated submission against one
upload (MEL-ENGINE-III/VI: cycles the caller never asked for). Each upload now submits with its own
`VkFence` and `vkWaitForFences` on it — the exact drain edge for its work alone. Synchronous contract
unchanged; the retirement watermark still advances at the fence signal (`submit_complete`), and the
staging buffer still routes through the deferred-free queue (so the pattern stays correct when uploads
go async). `memory.c` is staging-free and was already on the watermark — untouched.

### #5 — `queue_submit` command-buffer array from `dev->alloc`, not fixed `[8]` (`queue.c`)
The submit collected command buffers into a `VkCommandBuffer stackbuf[8]`, allocating dynamically only
past 8 — a stride masquerading as a ceiling (MEL-CODE-002). Now always sized to `command_list_count`
from `dev->alloc`, matching the round-1 attachment-array fix, freed after the submit records it.

### Tests added (`test_vulkan.c`, all headless)
- `vk_queue.submit_many_command_lists` — 16 CLs in one submit, each clears its own target a distinct
  shade and round-trips it; every readback carries its list's shade → none truncated past the old `[8]`.
- `vk_render.command_list_state_reset_on_rerecord` — a standalone CL reused across two recordings, each
  first-touch `Common→RenderTarget`, proving the per-recording U17 reset (the semantics #2 extends to the
  frame recorder; the recorder itself needs a native surface, unavailable headlessly).
- `vk_alloc.repeated_uploads_round_trip` — 24 device-local staging uploads round-tripped, exercising the
  per-upload fence path under churn, leak-free.
- `vk_swapchain.extent_accessor_null_contract` — the NULL → `{0,0}` accessor contract.

## #1 grow-on-demand bindless heap — ATTEMPTED, REVERTED, BLOCKED ON MoltenVK 1.2 (needs Gabbo)

This is the headline fix and I did not paper it. I implemented a full grow-on-demand
(set-layout + pool + set recreated larger on class-fill, every live descriptor replayed, old heap
retired future-gated, a dedicated bindless-only `bind_layout` to decouple the heap-set bind from
per-pipeline layouts) — then **reverted it** when end-to-end validation proved it cannot be made
correct on MoltenVK 1.2 within this track's scope. The blocker is concrete and probe-verified, not
a guess:

1. **MoltenVK reserves descriptors at set-allocation, not lazily on write.** A set-layout binding with
   `descriptorCount = ceiling` allocated from a pool smaller than the ceiling fails with
   `VK_ERROR_OUT_OF_POOL_MEMORY`. So the pool size is forced to equal the layout count — the
   "immutable ceiling layout + small growable pool" trick is impossible (probe confirmed).
2. **Front-loading the device ceiling costs ~29 MB** of argument-buffer memory at device-create
   (500k samplers + 1M each of the four others), vs ~0.4 MB for the round-1 caps — a secret claim that
   violates MEL-ENGINE-III (measured via `VK_EXT_memory_budget` heap usage).
3. **Growing the set-layout's `descriptorCount` breaks pipeline-layout compatibility.** A draw whose
   bound heap set has a larger count than the pipeline's baked set-layout trips
   **VUID-vkCmdDraw-None-08600** (and binds trip **VUID-vkCmdBindDescriptorSets-pDescriptorSets-00358**).
   My `bind_layout` decoupling makes the *bind* clean, but the *draw* still validates the bound set
   against the **pipeline's baked** layout — and that has the pre-grow count. So every pipeline created
   before the grow becomes incompatible. (A no-descriptor probe passed only because 08600 checks
   statically-used sets; real bindless shaders use set 0, so it always fires.)
4. **The one escape — `VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT`** — makes the count
   irrelevant to compatibility AND makes the pool reserve only the allocated (not max) count (both
   probe-verified clean on MoltenVK). But the Vulkan spec restricts it to the binding with the **largest
   binding number** in the set. The heap is one set with five bindings; only binding 4 (storage image)
   could be variable. The realistic over-cap trip — and the round-1 test — is the **sampler** class
   (binding 1), which therefore cannot be made variable without reordering binding indices, which breaks
   every bindless shader's `set=0, binding=N` declarations.

**The correct architecture is five separate descriptor sets (set 0..4), one variable-count binding each
— the canonical D3D12-style per-type heap.** That is faithfully growable on MoltenVK. It is **out of this
track's scope**: it changes every bindless shader from `set=0, binding=N` to `set=N, binding=0` — the
test shaders (`bindless_spv.h`, `visual_spv.h`), `reflect.c`'s set-0 model, and the hello-gpu showcase
shaders (`apps/**`, which I am forbidden to touch). Doing it half-way (one growable class) would not fix
the sampler over-cap the task names, so I left the round-1 loud-status stop-gap (fixed per-class caps +
`BINDLESS_SLOT_EXHAUSTED`) **intact and untouched** — it is honest (branchable failure, no silent
corruption), just not growable.

**Decision for Gabbo:** the 5-set bindless-heap restructure is a deliberate, cross-cutting binding-model
change (shaders + reflect + apps). It wants its own coordinated pass, not a smuggled-in partial. The
MEL-CODE-002 fixed-cap tension on the single-set heap is real and remains until that pass lands.
`vk_bindless.sampler_over_cap_fails_loudly` is **unchanged** (still asserts loud failure at the fixed cap),
because growth is not yet real.

## Public-header additions (coordinate with screens / tester / d3d12)
- `Mel_Gpu_Swapchain_Extent { u32 width; u32 height; }` and
  `mel_gpu_swapchain_extent(const Mel_Gpu_Swapchain*)` (`swapchain.h`). Additive; the only public-surface
  change this round. screens/host code can now query the granted extent; d3d12/webgpu should mirror the
  accessor for parity when their swapchains land.

No other public surface changed. No status enums added (the round-1 `BINDLESS_SLOT_EXHAUSTED` statuses
remain as-is).

## New kludges confessed (MEL-ENGINE-VIII) — bar is zero
- **None introduced.** The four landed fixes each remove debt without adding any. The one residual
  coupling is pre-existing and unchanged: the staging upload is still **synchronous** (waits on its own
  fence rather than returning a future); the async transfer-queue / host-image-copy path (§6.2) is the
  real fix and was explicitly out of scope this round. #4 only removed the queue-wide stall, which was
  the actionable part.
- **Retirement-gate caveat carried, not introduced:** had grow landed, the old heap's deferred-free
  marker would have used the current `submit_serial` — the same gate every deferred free in the engine
  uses (§3.3). A resource-create-triggered grow during an in-flight recording-not-yet-submitted could
  in principle free a set a pending CL still references; this is the identical hazard the round-1
  future-gated slot reclamation already lives with, not a new one. Moot now that grow is reverted, noted
  for the eventual 5-set pass.

## MoltenVK "1 MB still allocated" at VkPhysicalDevice destroy
Unchanged pre-existing driver teardown artifact (confirmed against untouched baseline); the engine's
slotmap leak detector reports zero live resources at every `device_destroy`. Not actioned (below the
engine).

## Comments vs Rule-#1 (carried flag)
`modules/gpu` is densely commented house-style; the global `~/CLAUDE.md` says "Never write comments".
Every prior track flagged this; I matched the module style (dense intent comments) on every edit and
flag the same tension. Will strip on Gabbo's word.

## Verification
- `DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1 ./nob test gpu-vulkan macos --gpu=vulkan` —
  **36/36** (32 prior + the 4 new). Grep-clean of `VUID` / `Validation Error` / `leak`; only the three
  intentional negative diagnostics remain.
- `./nob test gpu-foundation` — **8/8**.
- `render lowering: dynamic rendering` and `barrier lowering: synchronization2` active on every device.

## Suggestions
- **5-set variable-count bindless heap** is the right grow-on-demand vehicle and the natural home for the
  indirect family past the cap. It is a binding-model change (shaders + reflect + apps) — schedule it as
  a coordinated pass with the shader authors, not as a backend-local fix. The MoltenVK probe evidence in
  this writeup is the design's starting constraint set (per-set variable count, lazy pool reservation,
  count-independent compatibility — all verified).
- Async staging upload (transfer queue + `host_image_copy`, §6.2) is the next upload-path step now that
  the queue-wide idle is gone; it turns `buffer_write`/`texture_write` into a future-returning async form
  with a synchronous wrapper.
- `modules/gpu/readme.md` is still absent (flagged in four prior writeups). The binding-model conventions,
  the slot==index contract and its current fixed-cap caveat, the heap-class→binding map, and the two
  barrier lowerings belong there.

## CLAUDE.md / repo-convention suggestions (recommendations only)
- A fresh worktree's `nob` must be bootstrapped (`clang -std=c23 -g -Imodules/build -o nob nob.c`) and
  **all** build/test commands must run from the worktree root — a stray `cd` to the shared checkout silently
  builds the wrong tree. Worth a session-start note (cost me a full rebuild cycle this session).
