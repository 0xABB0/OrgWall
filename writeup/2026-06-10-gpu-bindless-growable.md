# GPU bindless — growable 5-set heap (B1 + B2)

## Work done

Implemented the two no-prerequisite roots of `design/gpu-bindless-growable.md` §6 on the Vulkan
backend, in worktree `gpu-bindless-growable`.

**B1 — 5-set split.** The bindless heap moved from one `VkDescriptorSet` with five array bindings to
one set per resource class (set 0 sampled images … set 4 storage images, each one runtime array at
binding 0). Shader-visible addressing changed from `(set 0, binding class)` to
`(set class, binding 0)`:

- `src/vulkan/binding.c` re-partitioned; `MEL_GPU_BINDLESS_BINDING_*` renamed to
  `MEL_GPU_BINDLESS_CLASS_*` (it is a set index now); pipeline layouts of bindless pipelines carry
  five class layouts (static-sampler set moved from set 1 to set 5 — no in-tree user yet);
  `cmd_bind_bindless` and the auto-bind bind all five sets.
- `reflect.c` generalized from set-0-only tracking to (set, binding) pairs; the bindless-shader
  discriminator is now "runtime array at binding 0 of a set < class count".
- All bindless `.slang` shaders migrated (`[[vk::binding(C, 0)]]` → `[[vk::binding(0, C)]]`), plus
  the raw GLSL sources in `apps/hello-gpu/shaders/`.
- Embedded SPIR-V fixtures (9 arrays in `modules/gpu/test/{bindless,visual}_spv.h`, 4 in
  `apps/hello-gpu/src/*_spv.h`) were decoration-patched at the word level (DescriptorSet/Binding
  swap), each `spirv-val`-verified. Classic-path fixtures untouched.
- Heap-class blast radius elsewhere is zero by construction: WebGPU refuses bindless pipelines,
  D3D12 register-space mapping is independent of `[[vk::binding]]`, Metal uses its own
  `MEL_TARGET_METAL` shader branch and argument-buffer heap.

**B2 — grow-on-demand.** Each class seeds small (1024 images/buffers, 256 samplers) and grows
geometrically to the device wall:

- Layout immutability across grows comes from `VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT`:
  one immutable per-class layout at the wall, sets allocated at current capacity. This refines the
  spec's §2.2 "re-point the binding" mechanism — pipeline layouts survive every grow by construction.
- A grow allocates a new pool + set, re-publishes live descriptors index-stable from a per-class
  side table, swaps the current epoch, and retires the old pool **epoch-gated**: command lists take
  an epoch reference at bind-record time, references transfer to the submission at `queue_submit`,
  and release through the deferred-free drain keyed on that submission's serial. This is stronger
  than the existing destroy watermark, which cannot cover a list recorded concurrently with a grow.
- Destroy paths (`buffer/texture_view/sampler`) clear the side-table `live` bit so a grow never
  re-publishes a dying resource. Slot-index reclaim was already future-gated via `has_reclaim`.
- The wall is `min(per-type update-after-bind limit, maxPerStageUpdateAfterBindResources / 4)`
  (samplers outside the aggregate); without the clamp `vkCreatePipelineLayout` would exceed the
  per-stage aggregate limit.
- **MoltenVK finding** (probed empirically): buffer-class pools are charged at the *layout* count,
  not the variable count (`VK_ERROR_OUT_OF_POOL_MEMORY` on a seed pool under a wall-sized layout);
  image/sampler classes honor variable counts. Init therefore probes each class: seed pool first,
  wall-sized allocation (class never grows, cost logged) on pool-charge failure, halving ladder as
  the last resort. Probe tool preserved at the job tmp dir; findings recorded in the spec.
- `caps.memory.bindless` gained `growable` + `seed_*_slots`; `max_*_slots` now reports the wall.
- `mel_gpu__defer_free_marked` added (deferred entry with an explicit serial marker).

**Tests.** All suites green: gpu-vulkan 49/49 (new `vk_bindless.heap_grows_past_seed` — creates past
the seed, verifies slot==index stability, samples through a post-grow slot, reads back),
gpu-visual 13, gpu-stress 20, gpu-concurrency 13, gpu-foundation 13, gpu-bench 12. Nine hello-gpu
screens verified clean under `HELLO_GPU_AUTO` (post/texquad/plasma/gallery/cube/instances/lorenz/
depth/layers). Two test adaptations: the oversize fixture's sized array was patched 20000 → 2^30
(MissingBindlessSlot is reserved for demands beyond the device wall now, per spec §8), and
`missing_bindless_slot`'s cap assertion follows.

**Docs.** Realization-status sections updated in `design/gpu-bindless-growable.md` and
`gpu-rhi.md` §3.1; `modules/gpu/readme.md` binding-model section rewritten for the split + grow, and
its stale "Metal / WebGPU not yet built" backend claim fixed (both are built and tested).

## Kludges

- **Equal-share wall partition.** The per-stage aggregate budget is split as a fixed quarter per
  buffer/image class. Deterministic and reported in caps, but it is an engine policy, not a
  user choice — the B5 `bindless_heap_create` seed/partition surface is the sanctioned fix.
- **`sampler_over_cap_fails_loudly` now walks to the device wall** (500k samplers on MoltenVK,
  growing the heap along the way). It passes but is the slowest test in the suite; it should
  probably cap the walk or use a pre-sized small heap once B5 lands.
- **Buffer destroy clears both buffer classes; view destroy clears both image classes** without
  consulting usage flags. Harmless (clearing a never-registered slot is a no-op) but it is two
  mutex acquisitions where one would do.
- **`vkGetPhysicalDeviceProperties` re-queried in `bindless_init`** for `maxBoundDescriptorSets`
  instead of riding the caps probe; one redundant query at device create.
- The swapchain recorder's epoch release on a device-lost `frame_end` path releases refs that were
  never transferred — correct, but the device-lost interaction with epoch-gated pools is untested.

## CLAUDE.md suggestions

- None.

## Suggestions

- **B3/B4 (D3D12 classic-heap reclaim + grow) are the next no-prerequisite chunk** — pure addition
  on win-pilot, fixes the confessed "no slot reclaim" debt.
- **Metal needs a §4 rung in gpu-bindless-growable.md** (the backend landed after the spec): the
  tier-2 argument-buffer heap has the same fixed-capacity smell, plus the confessed O(N)
  per-registration residency commits and no slot reclaim.
- The `mel_gpu__bindless_cl_bind` epoch bookkeeping adds a brief per-class mutex acquisition per
  pipeline bind. If profiling ever shows it, the cur-epoch pointer can become an atomic with
  refcounts on the epoch object.
- B5 (`bindless_heap_create` seeds/growth_factor/partition) is now purely additive surface.
