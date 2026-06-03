# GPU bindless — growable 5-set heap

Granular spec for the deferred bindless restructure. Replaces the fixed-capacity heap (over-capacity creation fails with `..._CREATE_BINDLESS_SLOT_EXHAUSTED`) with a grow-on-demand partition that preserves the §3.1 `slot == handle.index` contract. Bound by `design/gpu-rhi.md` §3.1 (handle identity, direct/indirect), §3.3 (future-gated retire), §3.4 (caps), §6.7 (binding model), §13 (module structure). Cites `MEL-ENGINE-N` where a decision turns on a commandment.

This document targets **target state**. The realized state is one Vulkan descriptor set at set 0 carrying five fixed-capacity bindings; see "Realization status" at the end of each unit and §6.7's realization note in `gpu-rhi.md`.

---

## 0. Problem

The realized heap is a single `VkDescriptorSet` with five `descriptorCount`-fixed array bindings (sampled-image, sampler, storage-buffer, uniform-buffer, storage-image), sized once at device-create to `min(default, caps-max)` per class. Direct-family creation registers at `handle.index`; `index >= cap` refuses with `BindlessSlotExhausted` (`MEL_GPU_{BUFFER,SAMPLER,TEXTURE_VIEW}_CREATE_BINDLESS_SLOT_EXHAUSTED`). The cap is a hard ceiling: a renderer that creates more than `cap` views of one class cannot continue. This is a fixed `[MEL_MAX_*]` (MEL-CODE-002) wearing a runtime number, and a creation-time wall the app cannot cross (MEL-ENGINE-I: do not declare "we shall never support this"; MEL-ENGINE-IV: capabilities, not limits).

`slot == handle.index` is the constraint that makes growth hard. The slotmap recycles a freed index; the heap slot is *that* index; therefore a grow must add capacity to a class **without renumbering any live slot** — no relocation, because relocation would invalidate every `handle.index`-derived shader-visible slot the app has already captured (MEL-ENGINE-VIII: descriptor pressure never invalidates identity, §3.1).

---

## 1. The 5-set partition

The heap is partitioned into **one descriptor set per resource class**, each independently sized and independently grown:

- **set 0** — sampled images (texture views, `SAMPLED_IMAGE`)
- **set 1** — samplers (`SAMPLER`)
- **set 2** — storage buffers (`STORAGE_BUFFER`)
- **set 3** — uniform buffers (`UNIFORM_BUFFER`)
- **set 4** — storage images (`STORAGE_IMAGE`)

Why per-class sets rather than the realized one-set-five-bindings: a single set cannot grow one binding's `descriptorCount` after allocation — the `VkDescriptorSetLayout` is immutable and the set's backing pool is sized at create. Growth of *any* class would force re-allocating the whole set and **renumbering every binding's array**, which relocates live slots across all five classes. Splitting each class into its own set localizes a grow to one class's pool and leaves the other four sets' descriptors — and therefore every captured `handle.index` slot in those classes — byte-identical and bound (MEL-ENGINE-IX: parts compose; growing samplers must not touch textures).

The set index per class is **fixed and part of the pipeline-layout contract**: a pipeline created `bindless = true` declares all five sets in its layout; the shader addresses class C's slot as `set = C, binding = 0, array index = slot`. The class→set map is not a closed protocol that will never change — it is a stable convention (MEL-ENGINE-IV); adding a sixth class (acceleration structures, §6.6) appends set 5 and is purely additive (no existing set renumbers). This is **not** an enum-as-closed-set (MEL-CODE-001): the partition is a contiguous index space whose width the device reports, not a fixed taxonomy the app branches on.

The "5" is the floor, not a ceiling. The AS class (`Mel_Gpu_Accel_Struct` bindless, §6.6, M3) is set 5 when `ray_tracing` is granted; mutable-descriptor collapse (§6.7, `mutable_descriptor_type`) folds classes into one set sized to the kind union, which is the *fewer-sets* direction and is compatible — a mutable layout is one grown set carrying the union, the per-class layout is N grown sets, both lower from the same source declaration (§6.4 `melody.binding` mixin).

**WebGPU floor exception.** WebGPU's `maxBindGroups = 4` cannot host five sets. The §6.7 4-bind-group packing (group 0 root record, group 1 textures, group 2 samplers, group 3 SSBO+UBO dual-entry) stands unchanged; growth on WebGPU is recreation of the affected bind group (§4.5). The 5-set partition is the native/desktop shape; WebGPU keeps its 4-group packing and grows by bind-group reconstruction.

### Realization status
Realized: one set, five bindings. Target: five sets, one binding each (six+ with AS / fewer with mutable collapse). The split is the prerequisite for every grow path below.

---

## 2. Grow-on-demand allocation (no fixed `[MEL_MAX_*]`)

### 2.1 Per-class capacity is a growable run, not a fixed array

Each class owns a **capacity** `cap_C` (current allocated descriptor count) and a **high-water** `used_C` (highest live slot + 1). Initial `cap_C` is a small seed (e.g. 1024), **not** `min(default, caps-max)` — the seed is a starting allocation, not a ceiling. `cap_C` grows geometrically (`cap_C *= growth_factor`, default 2, configurable at `bindless_heap_create`) when a creation would place a slot at `index >= cap_C`, bounded above by `caps.memory.bindless.max_<class>_slots` (the device-reported hardware/driver limit, the genuine ceiling). Exhausting the *hardware* limit still fails loud with `BindlessSlotExhausted` (MEL-ENGINE-VIII) — but that is the device's wall, not the engine's, and the diagnostic names the granted hardware max so the remedy is unambiguous (request a device whose limit is higher, or migrate the class to indirect/compacted heaps, §4.4).

The growable run uses `modules/collection.slotmap` semantics for index assignment (the slot index *is* the slotmap index per §3.1) and a `modules/allocator`-backed descriptor-pool list for backing (MEL-CODE-003: the heap takes the device's allocator, never `mel_malloc`). No `[MEL_MAX_*]` anywhere (MEL-CODE-002): `cap_C` is a field, the pool list is a dynamic array.

### 2.2 The grow operation (Vulkan, `descriptor_indexing` floor)

A `VkDescriptorSetLayout` binding's `descriptorCount` is immutable; a `VkDescriptorSet`'s capacity is fixed at allocate. Therefore grow does **not** resize the live set — it allocates a **new, larger set** for the class from a **new pool**, then re-points the binding for that class. The mechanism that preserves identity:

1. Compute `new_cap = min(cap_C * growth_factor, hw_max_C)`.
2. Allocate `new_pool` sized `new_cap` (`UPDATE_AFTER_BIND` pool, same flags as realized) and `new_set` from it.
3. **Re-publish every live descriptor of class C into `new_set` at its same array index.** The slot numbers are unchanged — slot `k` in `old_set` becomes slot `k` in `new_set`. `slot == handle.index` is preserved exactly: identity is the *index*, and the index does not move; only the descriptor's *physical set object* changes (§2.4 covers why this is safe vs. in-flight reads).
4. Atomically swap the class's bound set pointer: subsequent `cmd_bind_bindless` binds `new_set` for class C.
5. Retire `old_pool` future-gated (§3, §3.3): freed only after every submission that bound `old_set` for class C resolves its completion future.

Re-publishing is `vkUpdateDescriptorSets` over the live-slot list (the engine already holds each class's live descriptors — it wrote them at registration; it keeps the source `VkImageView`/`VkBuffer`+range per slot in the class's slot-metadata side table so re-publish needs no slotmap walk that could race creation). On `VK_EXT_descriptor_buffer` / `VK_EXT_descriptor_heap` the re-publish is a memcpy of the old descriptor-buffer range into the larger one plus a base-address rebind (§4.2) — cheaper than per-descriptor writes.

**Cost honesty (MEL-ENGINE-III).** A grow is O(used_C) descriptor re-publishes plus one allocation; it is rare (geometric growth ⇒ amortized O(1) per slot) but not free. The grow emits a U2 device-level warning naming the class and the old→new capacity so the cost is visible; an app that wants to avoid mid-frame grows pre-sizes via `bindless_heap_create({ seed_<class> })` (§5). The grow never happens implicitly inside a hot-path record call — only inside resource *creation* (`Concurrent` per §3.7), which is already off the per-draw path.

### 2.3 Why not relocate

The rejected alternative — compact freed slots and renumber — is forbidden: it would break `handle.index == slot` for every survivor (§3.1, MEL-ENGINE-VIII "descriptor pressure never invalidates identity"). Grow-without-relocation keeps every index fixed; only capacity rises. Freed slots are reclaimed *in place* (§3) by the slotmap's free-list, never by compaction. Compaction into a *tight* heap is the explicit, opt-in `*_make_indirect` migration path (§4.4 / §5), which invalidates the source generation by design and yields an indirect handle — never silent.

### 2.4 Live-set swap safety

Step 4 swaps which set the *next* bind uses; submissions already in flight hold `old_set` and keep reading it until they retire. This is the §6.7 "writing a live slot is undefined; the engine never writes a live slot" contract applied to the *set object*: the old set is never mutated after the swap (its descriptors are frozen), the new set is fully populated before the swap, and the old pool's free is future-gated. No in-flight reader observes a half-grown set (MEL-ENGINE-VIII). The swap itself is `SerializedPerObject` on the heap (§3.7) — it coincides with the class's grow lock.

### Realization status
Realized: `cap_C` fixed at device-create; `index >= cap_C` refuses. Target: geometric grow bounded by hardware max; refuse only at the hardware wall.

---

## 3. Retirement and reclaim (future-gated, §3.3)

Two reclaim events, both gated on completion futures, never on a frame index (§3.3: "there is no public `Mel_Gpu_Frame_Index`"):

- **Slot reclaim.** `*_destroy` on a direct handle frees the slotmap index; the index returns to the class free-list and is reusable by the next creation **only after** the last submission that referenced the slot resolves. Until then the slot is *retired-pending*: its descriptor stays published (harmless — no live shader indexes a destroyed handle if the app honors the contract), the index is not handed out. Reclaim drains on the U3 completion pump (§3.3 "retirement is future-gated"). The realized engine already future-gates deferred-free (the §3.3 watermark in `src/d3d12/` and the Vulkan pump); slot-index reclaim joins that mechanism.
- **Pool reclaim (grow).** The `old_pool` from a grow (§2.2) is retired-pending until every submission that bound `old_set` resolves. The pump frees it then. A second grow before the first pool retires chains: each old pool carries its own gating future-set; pools free in completion order, not allocation order.

No silent default on the reclaim interval (MEL-CODE-007): reclaim is exactly "all gating futures resolved," queryable, never a fixed N-frame guess.

### Realization status
Realized: deferred-free watermark exists for slot frees; no grow ⇒ no pool reclaim. Target: pool reclaim joins the same future-gated pump.

---

## 4. Backend lowering ladder

The 5-set partition is the portable shape; each backend lowers its grow path idiomatically. Caps report the active rung (`caps.memory.bindless.binding_model`, `caps.root_record_payload`, plus the realized-rung tier below).

### 4.1 Vulkan — `descriptor_indexing` (floor)

Five `UPDATE_AFTER_BIND` + `PARTIALLY_BOUND` + `UPDATE_UNUSED_WHILE_PENDING` descriptor sets, one per class (the realized flags, re-partitioned). Grow = new larger pool + new set + re-publish + swap + future-gated old-pool free (§2.2). This is the realized rung's natural extension and has **no prerequisite** beyond the set-split refactor (§6 phasing). `slot == handle.index` holds via index-stable re-publish.

### 4.2 Vulkan — `descriptor_buffer` (transitional)

`VK_EXT_descriptor_buffer`: each class is a contiguous descriptor-buffer range; the binding is a base device address + offset (`vkCmdBindDescriptorBuffersEXT` + `vkCmdSetDescriptorBufferOffsetsEXT`). Grow = allocate a larger buffer (a U8 `Mel_Gpu_Memory_Pool` allocation, MEL-CODE-003), `memcpy` the old range into it (descriptors are plain bytes here — relocation of *storage* is fine because the *slot index* — the offset divided by descriptor size — is unchanged), rebind the base address. Cheaper than per-descriptor re-publish. `slot == handle.index` holds: slot `k` lives at `k * descriptor_size` in both buffers. Old buffer future-gated.

### 4.3 Vulkan — `descriptor_heap` (ceiling)

`VK_EXT_descriptor_heap` (Roadmap 2026, the DX12-style two-heap split — one resource heap, one sampler heap). The 5-set partition collapses onto **two heaps**: the resource heap carries the four resource classes (sampled-image, storage-buffer, uniform-buffer, storage-image as sub-ranges), the sampler heap carries samplers. Per-class grow becomes per-sub-range grow within the resource heap; the heap itself is a device-address-addressed allocation grown like §4.2. The class→set index the shader uses is a heap-relative offset on this rung; reflection (U12) emits the right addressing per granted rung from one source (MEL-ENGINE-II: the user writes one declaration). `slot == handle.index` holds within each sub-range.

### 4.4 D3D12 — `ResourceDescriptorHeap` (ceiling) vs. classic-heap floor

**Ceiling.** SM 6.6+ `ResourceDescriptorHeap` / `SamplerDescriptorHeap` direct indexing: two shader-visible descriptor heaps (CBV/SRV/UAV + sampler). The four resource classes occupy contiguous sub-ranges of the CBV/SRV/UAV heap; samplers occupy the sampler heap. Grow = create a larger shader-visible heap, `CopyDescriptorsSimple` the old heap into it (D3D12 descriptors are CPU-copyable; the destination index equals the source index ⇒ `slot == handle.index` preserved), set the new heap on the command list (`SetDescriptorHeaps`), future-gate the old heap's release. D3D12 allows only **one** shader-visible CBV/SRV/UAV heap bound at a time, so all four resource classes must share that one heap as sub-ranges — the per-class sets are *logical* on D3D12, *physical* on Vulkan; the source declaration is identical (MEL-ENGINE-IX).

**Classic-heap floor (the realized D3D12 rung — addresses the readme "no slot reclaim").** The in-box Win10 floor cannot honor `ResourceDescriptorHeap` direct indexing (readme; `design/gpu-d3d12.md`); bindless lowers to classical descriptor heaps + root-signature tables. The round-3 writeup records: **classic D3D12 heaps have no slot reclaim.** The growable design fixes both axes:

- **Reclaim.** The classic heap gets the same future-gated free-list as Vulkan (§3): a freed table slot returns to the class free-list once its gating submission resolves. The realized "no reclaim" is because the classic path never wired slot-index reclaim to the §3.3 pump; this spec wires it — same mechanism, no new concept (MEL-ENGINE-IX).
- **Grow.** Classic heaps grow exactly like the ceiling: larger non-shader-visible staging heap → `CopyDescriptorsSimple` → larger shader-visible heap → `SetDescriptorHeaps` → future-gate old. The root-signature table ranges are re-pointed at the new heap; `slot == handle.index` preserved by index-stable copy. The root signature's descriptor-table declaration is unbounded-range (`D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE` + a range size that the heap's current `cap_C` parameterizes at bind, not bake) so a grow does not force root-signature recreation.

D3D12's no-prerequisite sub-unit is **classic-heap reclaim** (§6), because it is a pure addition (wire existing free-list to existing pump) and unblocks every classic-path renderer regardless of grow.

### 4.5 WebGPU — 4-bind-group floor

No 5-set partition (`maxBindGroups = 4`); the §6.7 packing stands. Grow = reconstruct the affected bind group at the larger `sized binding array` size and rebind. WebGPU bind groups are immutable post-create, so "grow" is "create a new bind group, copy nothing (descriptors are the resource references the engine re-supplies from slot metadata), bind the new group." Future-gate the old group's drop. `GPUResourceTable` (Milestone 3, when it lands) makes this a true growable table; until then sized-binding-arrays + group reconstruction is the honest floor (MEL-ENGINE-VII: degrade gracefully, do not deform). Past the WebGPU hardware array cap, creation hard-errors (P1 sync-impossible refinement, §6.7).

### Realization status
Realized rungs: Vulkan `descriptor_indexing` (one set, five bindings, no grow); D3D12 classic heap (no reclaim, no grow). Target rungs: all of the above with grow + reclaim; `descriptor_buffer` / `descriptor_heap` / `ResourceDescriptorHeap` ceilings additive.

---

## 5. Public-surface deltas

No new resource API; growth is invisible to the common path (MEL-ENGINE-II — the simple path is the same path further along). The deltas:

- **`bindless_heap_create({ seed_<class>?, growth_factor?, writable_partitions? })`** (the §3.7 heap-create already cited for `writable_partitions`) gains optional per-class **seed** sizes and a **growth_factor**. Absent ⇒ engine seeds at a documented small default and grows; **the default is the *seed*, not a *ceiling*** — this is not the forbidden silent default (MEL-CODE-007) because exceeding it grows rather than failing; the seed only trades startup allocation against first-grow latency, and the chosen seed is reported in caps so it is never hidden.
- **`BindlessSlotExhausted` semantics tighten.** Today it fires at the engine's fixed cap. Target: it fires **only** at the device hardware max (`caps.memory.bindless.max_<class>_slots`), and the diagnostic names that hardware max plus the suggestion to migrate to indirect/compacted (§4.4) — distinct from `MissingBindlessSlot` at pipeline-create (see §8 last item for the growable-heap reconciliation of `MissingBindlessSlot` vs `MissingFeature`).
- **`caps.memory.bindless`** gains `growth_model = fixed | growable` and per-class `seed_<class>` so a power user can branch (a renderer that must never stall mid-frame pre-sizes; one that does not, grows). `binding_model` and `root_record_payload` are unchanged.
- **`*_make_indirect(direct) → indirect`** (§3.1) is the explicit migration to a compacted/tight heap for a class under genuine hardware-max pressure; it invalidates the source generation and yields an indirect handle resolved via `*_bindless_slot`. This is the P2 escape for the rare app that outgrows the hardware direct-heap limit (§7).

---

## 6. Phasing — no-prerequisite-first

Per the new-feature workflow (spec → granular → implement no-prereq-first). Sub-units and their dependency edges:

- **B1 — Vulkan set-split refactor (NO PREREQUISITE).** Re-partition the realized one-set-five-bindings into five per-class `VkDescriptorSet`s with the fixed class→set map (§1). Pure refactor: capacities stay fixed (still `min(seed, hw_max)`), behavior unchanged, all suites stay green. This is the seam every grow path needs and changes no public API. **Start here.**
- **B2 — Vulkan `descriptor_indexing` grow (depends: B1).** New-pool + re-publish + swap + future-gated old-pool free (§2.2, §4.1). `BindlessSlotExhausted` retreats to the hardware max. Add `growth_model`/`seed_<class>` caps.
- **B3 — D3D12 classic-heap reclaim (NO PREREQUISITE, parallel to B1).** Wire the classic heap's freed table slots to the §3.3 future-gated pump (§4.4). Pure addition; fixes the round-3 "no slot reclaim" debt independently of grow. Can land before or alongside B1.
- **B4 — D3D12 classic-heap grow (depends: B3).** `CopyDescriptorsSimple` to a larger heap + `SetDescriptorHeaps` + unbounded-range root signature + future-gated old-heap release (§4.4).
- **B5 — pre-sizing surface (depends: B2 or B4).** `bindless_heap_create` seed/growth_factor params (§5); grow U2 warning.
- **B6 — ceiling rungs (depends: B2; additive).** `descriptor_buffer` (§4.2), `descriptor_heap` (§4.3), D3D12 `ResourceDescriptorHeap` (§4.4 ceiling). Each is purely additive — a granted rung replaces the grow *mechanism* for that backend without touching the public surface.
- **B7 — WebGPU growable floor (depends: M4 WebGPU backend).** Bind-group reconstruction grow (§4.5); `GPUResourceTable` when it lands.
- **B8 — AS bindless class (depends: B1, M3 ray tracing).** Append set 5 for `Mel_Gpu_Accel_Struct` bindless (§1, §6.6); additive.

B1 and B3 are the two no-prerequisite roots; they are independent (different backends) and can be done concurrently by different agents.

---

## 7. P2 escape

The growable direct heap is the convenience; the P2 peer is full app-owned control (§6.7 "heaps as resources, with P2 introspection"):

- **App-owned heap.** The user creates a `Mel_Gpu_Memory_Pool`-backed descriptor buffer/heap directly (§5.3 placed allocations, §6.7 P2-direct writers), sizes it themselves, writes descriptor entries via the introspection surface, and binds it with their own root-record layout. The engine does not grow it — the app owns the lifetime and the grow policy. The §3.7 live-slot rule and future-gated retire still bind the app's writes (the engine cannot police past the hatch, MEL-ENGINE-VIII).
- **Classic descriptor sets** (`bind_group.h`, the realized P2 peer) remain the per-pipeline app-owned alternative with no heap at all — a renderer that wants no bindless heap declares classic `set_layouts` and never instantiates the growable partition.
- **`*_make_indirect`** (§5) is the escape for migrating a class into a tight/compacted heap the app manages when the direct heap hits the hardware wall.

Test for P2 completeness (§15): can the app fully reimplement the growable heap? Yes — app-owned pool + introspection + own root-record layout + own grow policy reproduces every engine behavior. If any primitive is missing, the escape is incomplete.

---

## 8. Failure modes the design must survive

- **Grow during concurrent creation.** Creation is `Concurrent` (§3.7); a grow is `SerializedPerObject` on the heap. A creation that triggers a grow takes the class grow-lock; concurrent same-class creations block on it briefly, other-class creations proceed (per-class sets ⇒ per-class locks). No global heap lock (MEL-ENGINE-IX).
- **Grow during in-flight reads.** Old set frozen + future-gated free (§2.4); no reader sees a half-grown set.
- **Hardware-max exhaustion.** Loud `BindlessSlotExhausted` naming the hardware max and the `*_make_indirect` remedy (§5); never silent (MEL-ENGINE-VIII).
- **Capture-replay across a grow.** A `CAPTURE_REPLAY` heap (§6.7) must reproduce identical slot indices on replay; grow preserves indices by construction (§2.3), so a replayed capture that grew reproduces the same index space. The descriptor-buffer/heap rungs use the `descriptorBufferCaptureReplay` / D3D12 manual-write-tracking discipline so the grown allocation's addresses replay deterministically.
- **Pipeline-create against a class smaller than the layout demands.** With a growable heap, a layout demanding slot N when `cap_C < N+1` is **not** `MissingBindlessSlot` if `N < hw_max_C` — it is a seed-too-small hint: the heap will grow on the next create that reaches N. Pipeline-create succeeds; the first creation past `cap_C` grows. `MissingBindlessSlot` is reserved for `N >= hw_max_C` (the genuine unsatisfiable case). This tightens the §6.7 / readme `MissingBindlessSlot` vs `MissingFeature` split for the growable world.
