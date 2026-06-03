# GPU async query-resolve and async transfer-upload

Granular spec for the two async contracts the round-3 integration deferred (writeup/2026-06-03-gpu-rhi-round3-integration.md): the **resolve-to-buffer future** for query results and the **transfer-queue upload** path. Both are the spec's *portable* async forms; the realized round-3 timestamp resolve is a Vulkan-only synchronous helper. Bound by `design/gpu-rhi.md` §3.3 (futures, future-gated retire), §5.2 (queues), §5.3 (allocator roles), §6.1 (`buffer_write`), §6.2 (`texture_write`), §11.2 (GPU queries, "Result retrieval is async-only"). Cites `MEL-ENGINE-N` where a decision turns on a commandment.

Target state. "Realization status" closes each unit with realized-vs-target.

---

## Part A — Async query-resolve (resolve-to-buffer future)

### A.1 Contract

§11.2 pins the model: **result retrieval is async-only.** `query_pool_resolve(pool, indices_range) → future<results>`. The engine records the resolve copy into an engine-managed readback buffer (U8 `READBACK` role, §5.3), submits it, and the U3 completion pump resolves the future when that submission completes and the readback maps. There is **no** raw `vkGetQueryPoolResults` / synchronous getResults path on the public surface: WebGPU has no CPU getResults and D3D12 needs the heap resolved to a buffer, so a sync form would lie on those backends (P1 sync-impossible refinement, MEL-ENGINE-VIII).

The realized round-3 helper (`query_pool_resolve` in `query.c`) is **synchronous and Vulkan-only**: it issues `vkCmdCopyQueryPoolResults`, waits, and returns ns directly via `timestamp_period_ns`. That helper is the *tooling/startup* `*_sync` convenience (§3.3 permits a `*_sync` wrapper that pumps the reactor until resolved, asserting if called from the reactor thread); the async future is the portable base it must wrap. Target: invert the layering — the async future is primary, the sync helper pumps it.

### A.2 Future shape and lowering

`query_pool_resolve(pool, indices_range, dst?) → Mel_Gpu_Query_Resolve_Future`. The future resolves to `{ results, status }` where `results` is a typed view over the resolved bytes (one `u64` per timestamp; one `u64` per occlusion query; one `u64` per set bit of the pipeline-statistics mask in declared order, §11.2; per-counter records for `Mel_Gpu_Perf_Counter_Pool`). Optional `dst` lets the caller supply their own `READBACK` buffer (the P2 peer — engine-managed readback is the convenience).

Per-backend lowering of the resolve copy:

- **Vulkan** — `vkCmdCopyQueryPoolResults(... VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT?)` into the readback buffer on a command list, submitted; the **completion future of that submission** is the resolve future's gating edge (§3.3 native-waitable poll via fence fd). `WAIT_BIT` is *not* used to block the host — the host never blocks; the GPU-side wait orders the copy after the queries, and the host learns of completion through the pump. Timestamp ns conversion (`timestamp_period_ns`) and the calibrated-clock model (§11.2 `timestamp_to_cpu_ns`) are applied on the resolved bytes in the future's continuation.
- **D3D12** — `ResolveQueryData(heap, type, start, count, dst, dst_offset)` into the readback buffer, submitted; the `ID3D12Fence` completion is the gating edge. The realized round-3 path does not yet wire this; it joins the existing D3D12 fence→future poller (`design/gpu-d3d12.md` phase 1).
- **Metal** — `MTLCounterSampleBuffer` resolved via `resolveCounterRange`; the command-buffer completion handler bridges to the future through `mel_reactor_post` (§3.3 thread-callback bridge). (M4 backend.)
- **WebGPU** — `GPUCommandEncoder.resolveQuerySet(querySet, first, count, dst, dst_offset)` into a `QUERY_RESOLVE | COPY_SRC` buffer, then `copyBufferToBuffer` into a `MAP_READ` readback buffer, then `mapAsync`; the `ProcessEvents` tick-source resolves the future (§3.3 pump-on-tick). (M4 backend.) `caps.queries.timestamp_query = quantized_100us` is honored — the resolved values carry the 100 µs quantization honestly (MEL-ENGINE-VIII).

### A.3 Future-gated readback lifetime

The engine-managed readback buffer is a U8 ring slice or `READBACK` allocation whose lifetime is **gated on the resolve submission's completion future** (§3.3 retirement is future-gated), identical to `buffer_alloc_transient` (§6.1). The slice is not reused until the future resolves and the caller has consumed `results` (the future holds the slice live; dropping the future without consuming logs a U2 warning rather than silently leaking — MEL-ENGINE-VIII). No frame-index lifetime (§3.3 "no public `Mel_Gpu_Frame_Index`").

### A.4 Concurrency

`query_pool_resolve` is `Concurrent` across distinct pools, `SerializedPerObject` on the same pool (§3.7). The resolve submission goes to whichever queue the caller names (default: the queue that recorded the queries, since timestamps are queue-local); cross-queue resolve of queue-A timestamps from queue B is rejected with a status naming the mismatch (timestamp validity bits are per-queue-family, §5.2 `queue_info`).

### A.5 P2 escape

The caller may supply `dst` (own `READBACK` buffer) and read it themselves after awaiting the future, bypassing the typed `results` view. The native `VkQueryPool` / `ID3D12QueryHeap` / `MTLCounterSampleBuffer` is reachable through U5 interop for a fully hand-rolled resolve. Engine-managed resolve is the convenience; both are peers (P2).

### A.6 Realization status
Realized: synchronous Vulkan-only `query_pool_resolve` returning ns directly. Target: async `Mel_Gpu_Query_Resolve_Future` primary on all backends; the sync form is a `*_sync` wrapper over it (§3.3). No-prerequisite sub-unit: **A-VK** (wrap the existing Vulkan resolve copy in the U3 future instead of a host wait) — it reuses the existing fence→future pump and changes no public type beyond adding the future return.

---

## Part B — Async transfer-upload (transfer queue)

### B.1 Contract

§6.1 / §6.2 pin `buffer_write` / `texture_write` as the **portable streaming primitives** that return a future / sync edge; the base call never hides an untracked GPU copy behind a `void` return (MEL-ENGINE-VIII). §5.2 defines the `Transfer` and `AsyncCompute` queue roles and the `Transfer → Compute → Graphics` widening chain; `dedicated: bool` requests a transfer-only family for true overlap. The deferred piece is routing the staging copy of a `DEVICE`-role upload onto a **dedicated transfer queue** so it overlaps graphics instead of serializing on the graphics frontend, with the completion future as the ordering edge a subsequent graphics submission waits on.

The realized `buffer_write` / `texture_write` (M2) lower the `DEVICE` staging blit onto the graphics queue (the classical path). That is correct but serializes upload against rendering. Target: when a `dedicated` `Transfer` queue is granted, the staging copy runs there and the returned future carries the cross-queue ordering.

### B.2 Path selection (per §5.3 host-visible tiers)

`buffer_write` / `texture_write` already select per `caps.memory.host_visible_device_local`:
- `full_uma` — direct write, no staging, no queue (§5.3). Transfer queue irrelevant; future resolves eagerly.
- `rebar` — direct write into the ReBAR window for upload-streaming-heavy resources; staging only for the rest.
- `none` / `DEVICE` target — staging buffer + copy. **This is where the transfer queue applies.**

When staging is needed and a `Transfer` queue (preferably `dedicated`) is acquired, the copy lowers to that queue. Absent a transfer queue, the chain widens to compute then graphics (§5.2); the future contract is identical regardless of which queue runs the copy (MEL-ENGINE-IX — one ordering mechanism).

### B.3 Cross-queue ordering edge

The returned `Mel_Gpu_Buffer_Write_Future` / `Mel_Gpu_Texture_Write_Future` carries the **transfer submission's completion**, and additionally a **timeline-semaphore signal** (§7.3 U17) that a subsequent graphics/compute submission `wait`s on so the consumer does not read before the transfer lands. Two consumption shapes:

- **CPU-await** — the caller awaits the future (e.g. at load time) before issuing dependent work; the future resolves via the U3 pump.
- **GPU-wait** — the caller threads the future's timeline value into the dependent `queue_submit({ wait[] })` (§5.2); no CPU round-trip. This is the hot-path form — upload on transfer queue, render on graphics queue, graphics waits the transfer's timeline value (§5.2 "async-compute overlap is just submit signaling a timeline value; the other queue waits it").

### B.4 Queue-family ownership

On Vulkan `Exclusive` resources (§6.1 default `queue_sharing`), a transfer-queue upload of a graphics-consumed resource needs an **ownership release/acquire pair** (§7.3 `cmd_queue_ownership_release` on transfer, `cmd_queue_ownership_acquire` on graphics), fenced by the same timeline edge. The engine emits this pair automatically for engine-managed `buffer_write` / `texture_write` staging when source and consumer queues differ; the `acquire_hint` defaults to `Modified` (the transfer just wrote it). `Concurrent` resources skip the pair (§7.3 no-op). D3D12 state is queue-agnostic (no pair, enhanced barriers); Metal/WebGPU no-op. The render graph (§8.2) does the same for graph-scheduled uploads; explicit issue is the P2 peer.

### B.5 Staging lifetime (future-gated)

The staging buffer (U8 ring slice or transient, §6.1 `buffer_alloc_transient`) lives until the transfer submission's completion future resolves (§3.3 future-gated retire), not until a frame boundary — async-compute or transfer work outlasting the graphics frame keeps the slice live (§6.1). The ring's reset wedge advances only past resolved futures.

### B.6 Concurrency

`buffer_write` / `texture_write` are `Concurrent` across distinct resources, `SerializedPerObject` on the same resource (§3.7). A `dedicated` transfer queue acquired `internally_synchronized` (§5.2) admits concurrent submit from many upload threads with no submit lock — the canonical asset-streaming shape (§5.2 "the internally-synchronized path is the default request for media / asset-IO queues where producers naturally run on many threads"). The transfer queue is **not** silently substituted: if `dedicated` is unavailable the request widens with a U2 warning (§5.2), never silently downgrades to graphics (MEL-ENGINE-VIII).

### B.7 Relationship to AssetIo (§6.8)

`Transfer`-queue upload here is the *generic* staging path for `buffer_write` / `texture_write`. The `AssetIo` queue (§5.2, `design/io-asset.md`) is the *DirectStorage / MTLIOCommandQueue* path for compressed asset streaming and is a distinct role; on backends lacking an addressable IO queue, `AssetIo` itself lowers to a `Transfer` queue serviced through CPU staging (§5.2 `AssetIo → CPU-staged Transfer`). This spec covers the Transfer path; the AssetIo decompression path is `io.asset`'s. They share the queue-acquisition and future-gating machinery (MEL-ENGINE-IX).

### B.8 P2 escape

The P2 peer is explicit: the user acquires their own `Transfer` queue (§5.2), allocates their own staging buffer (U8 placed allocation, §5.3), records `cmd_copy_buffer` / `cmd_copy_buffer_to_texture` + the ownership pair themselves, submits with their own timeline signal, and threads it into the consumer submit. `buffer_write` / `texture_write` on the transfer queue is the convenience over exactly this; the app can fully reimplement it (P2 test, §15).

### B.9 Realization status
Realized: `buffer_write` / `texture_write` stage onto the graphics queue (M2). Target: staging copy routes to a `dedicated` `Transfer` queue with the cross-queue timeline edge + ownership pair, future carrying both. No-prerequisite sub-unit: **B-VK-QUEUE** (acquire the dedicated transfer family at device-create per the §5.2 queue plan and route the existing staging copy to it; the timeline edge and ownership pair are §7.3 primitives already specified) — depends only on the realized U7 queue model and U17 timeline semaphores, both M2.

---

## Notes for the Vulkan implementer (next round)

- Both contracts reuse mechanisms that already exist: the U3 fence→future pump, U17 timeline semaphores, U8 `READBACK`/staging allocations, §7.3 ownership-transfer commands, the §3.3 future-gated retire wedge. Neither needs a new concept (MEL-ENGINE-IX) — they wire existing primitives into the async shape the spec mandates.
- Order to attempt: **A-VK** (async query-resolve future) first — smallest, reuses the existing resolve copy, no new queue. Then **B-VK-QUEUE** (transfer-queue upload) — needs the dedicated transfer family in the device-create queue plan, which is a §5.2 surface already specified but possibly not yet exercised; verify `queue_request(Transfer, dedicated: true)` returns a transfer-only family on the test GPUs before building on it.
- Keep the realized synchronous query helper as the `*_sync` wrapper (§3.3) — do not delete it; re-layer it over the future.
