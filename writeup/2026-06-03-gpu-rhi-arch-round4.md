# GPU RHI — architecture round 4 (design only)

Design-only mandate: zero code, zero header changes. Owned paths `design/**` and this writeup. Three deliverables plus the realized-vs-target reconciliation of `gpu-rhi.md`.

## Work done

### 1. Flagship — `design/gpu-bindless-growable.md` (new)
Granular spec for the deferred bindless restructure: replace the fixed-capacity heap (over-capacity creation fails `..._CREATE_BINDLESS_SLOT_EXHAUSTED`) with grow-on-demand. Grounded in the realized Vulkan binding (`src/vulkan/binding.c`: one `VkDescriptorSet` at set 0, five fixed `descriptorCount` array bindings, `cap_C = min(default, caps-max)`, refuse at `index >= cap`). Covers:
- **5-set partition** — one descriptor set per resource class (sampled-image / sampler / SSBO / UBO / storage-image), the prerequisite for per-class grow without cross-class relocation; fixed class→set map as a layout contract (additive set 5 for AS; mutable-collapse is the fewer-sets direction).
- **Grow-on-demand** — geometric `cap_C` growth bounded only by the hardware max; new-pool + index-stable re-publish + atomic set-swap + future-gated old-pool free. `slot == handle.index` survives because identity is the *index* and the index never moves; only the physical set object grows. Compaction/relocation rejected (MEL-ENGINE-VIII identity rule); the sole relocation path is the explicit opt-in `*_make_indirect`.
- **Backend ladder** — Vulkan `descriptor_indexing → descriptor_buffer → descriptor_heap`; D3D12 `ResourceDescriptorHeap` ceiling vs classic-heap floor, with the round-3 "classic heaps have no slot reclaim" debt addressed (wire freed table slots to the §3.3 future-gated pump) and classic grow via `CopyDescriptorsSimple` + unbounded-range root signature; WebGPU 4-bind-group reconstruction floor.
- **Retire/reclaim** — slot reclaim and pool reclaim both future-gated (§3.3), never frame-indexed.
- **P2 escape** — app-owned pool + introspection + own grow policy; classic descriptor sets; `*_make_indirect`.
- **Phasing** — two no-prerequisite roots: **B1** Vulkan set-split refactor (pure refactor, no API change, suites stay green) and **B3** D3D12 classic-heap reclaim (pure addition). They are independent (different backends) and concurrent-safe. Grow (B2/B4), pre-sizing (B5), ceiling rungs (B6), WebGPU (B7), AS class (B8) layer on top.

### 2. Async deferred contracts — `design/gpu-async-resolve-transfer.md` (new)
The two async paths the round-3 writeup deferred, specified for the next-round Vulkan implementer:
- **Part A — async query-resolve** (`query_pool_resolve → Mel_Gpu_Query_Resolve_Future`, §11.2). Resolve-copy-into-readback + future-gated readback lifetime; per-backend lowering (Vulkan `vkCmdCopyQueryPoolResults`+fence, D3D12 `ResolveQueryData`+fence, Metal counter-resolve+post, WebGPU `resolveQuerySet`+`mapAsync`+tick). The realized synchronous Vulkan helper re-layers as the `*_sync` wrapper. No-prerequisite sub-unit **A-VK** (wrap the existing resolve copy in the U3 future).
- **Part B — transfer-queue upload.** Route `buffer_write` / `texture_write` `DEVICE`-staging onto a `dedicated` `Transfer` queue with a U17 timeline edge + §7.3 ownership release/acquire pair; future carries both, consumable CPU-await or GPU-wait. Path selection per `host_visible_device_local` (UMA direct / ReBAR window / staging). Future-gated staging lifetime; relationship to AssetIo clarified. No-prerequisite sub-unit **B-VK-QUEUE** (acquire dedicated transfer family in the §5.2 queue plan, route the existing copy to it).

### 3. Reconcile `gpu-rhi.md` §3.1 / §6.7 (edited, prose-only, no API invented)
Added explicit **"Realization status"** notes so a future reader without chat context sees the realized-vs-target split:
- **§3.1** — direct family + `slot==index` realized for buffer/texture-view/sampler; indirect family realized for **`Mel_Gpu_Sampler_Indirect` only** and **engine-owned** (no foreign import); the broad indirect-for-imports surface (buffer/texture-view/accel-struct indirect, `Borrowed`, `*_make_indirect`, indirect `*_bindless_slot`, `*_indirect_destroy`) is target-state, not in headers. `Owned`/`Borrowed` field exists but only `Owned` exercised.
- **§6.7** — realized = Vulkan `descriptor_indexing` floor as **one set / five bindings** (narrower than the "per-type tables" floor wording, and *not* growable today); push-constant root-record carrier; D3D12 classic-heap floor (with the no-reclaim debt named); Vulkan classic path realized, D3D12 classic rejected; `MissingBindlessSlot` vs `MissingFeature` realized. WebGPU packing / mutable collapse / capture-replay lowering / AS heap / heap introspection not yet realized. Pointers to `gpu-bindless-growable.md`.
- Milestone cross-references: M3 U14 → growable spec; M2 U24 → async Part A; M2 U9 → async Part B.

## Kludges / debt (MEL-ENGINE-VIII — confess all)
- **None in code** (design-only mandate; wrote no code, changed no headers).
- **Spec debt acknowledged, not introduced.** The growable spec documents two pre-existing realized debts rather than fixing them (out of scope for design): the fixed-capacity heap is a runtime-numbered `[MEL_MAX_*]` (MEL-CODE-002) until B1/B2 land, and D3D12 classic heaps lack slot reclaim until B3 lands. Both are now specified with no-prerequisite remediation paths; the debt persists in the tree until an implementer picks them up.
- **Seed default tension with MEL-CODE-007.** `gpu-bindless-growable.md` §5 proposes a default *seed* size for `bindless_heap_create`. I argued this is not a forbidden silent default because exceeding it grows (does not fail) and the chosen seed is reported in caps. This is a judgement call an implementer should confirm with Gabbo; if the no-silent-default reading is stricter, the seed must be a required parameter. Flagged, not assumed.
- **Unverified queue-plan exercise.** Async Part B assumes `queue_request(Transfer, dedicated: true)` returns a transfer-only family; the spec instructs the implementer to verify this on the test GPUs before building on it (the §5.2 surface is specified but its M2 exercise on the dev hardware is unconfirmed from a design seat). Not a debt I can clear without running code.

## CLAUDE.md suggestions (recommendations only — not applied)
- The repo would benefit from a `modules/gpu/spec.md` once the binding model stabilizes; per MEL-SPEC-002, `gpu-bindless-growable.md` and `gpu-async-resolve-transfer.md` should fold into it (or into `modules/gpu/`) once their units land, and the realization notes I added to `gpu-rhi.md` §3.1/§6.7 should migrate to the module readme/spec. Recording as a hygiene recommendation, not acting on it (the units are unimplemented; moving now would orphan the spec).
- No CLAUDE.md content change recommended this round.

## Suggestions (feature direction + hygiene)
- **Sequence B1 + B3 next round.** Both are no-prerequisite, independent, and concurrent-safe (different backends). B1 is a pure refactor provable by green suites; B3 clears a named correctness debt (classic D3D12 reclaim). Assigning them to two agents in parallel is the natural round-5 split. B2 (Vulkan grow) is the first real capability gain and depends only on B1.
- **A-VK is the cheapest async win.** Wrapping the existing Vulkan timestamp resolve in the U3 future (Part A no-prereq) is small, reuses the fence→future pump, and removes the only synchronous-host-wait helper on the query surface, aligning it with §11.2's async-only contract before WebGPU/D3D12 force the issue.
- **Hygiene:** the round-3 writeup's unfiled items remain open and are not mine to action but worth tracking — retire the false-premise test `conc_tracker.device_accepts_flag_but_tracker_is_unwired`, and the `…_VK_FAILED → …_BACKEND_FAILED` cross-backend status rename (a single mechanical pass). Neither touches design.
- **`design/` tidiness (MEL-SPEC-002):** `design/` now holds 12 GPU-adjacent specs. None has a module home yet (no `modules/gpu/spec.md`, no `modules/render/`, etc.). They are correctly in `design/` until their modules materialize; flagging so the eventual migration is not forgotten.
