# Melody Asset IO — `io.asset`

Storage-IO queue that produces resources. Reads bytes from a file, mapped region, or platform source into a destination — typically a GPU buffer or texture subregion today, optionally GPU-decompressing along the way. The abstraction is *decompress these bytes from disk into this destination*; the destination happens to be a GPU resource, but the module is storage, not rendering. A future CPU-side asset cache is the obvious second destination.

This document is bound by the Ten Commandments of the Engine. Where a decision turns on one, the commandment is cited by tag (`MEL-ENGINE-N`).

---

## 1. Module identity

Parent module: `io` — a new top-level namespace for storage and bitstream IO. Future siblings — `io.network`, `io.archive` — are design space, not specified here; each lands on its own merit and is not implicitly admitted by the parent's existence (MEL-ENGINE-I: embrace the hard problem when the domain demandeth, not pre-emptively).

`io.asset` is the asset-IO submodule. It *consumes* the GPU module's queue surface (§5.2 of gpu-rhi.md) through the `AssetIo` role; the role enum and its fallback chain stay in `gpu`. This module owns the queue's command-list shape, the source wrap, and the codec dispatch.

---

## 2. Inherited principles

- **P1 — emulate-to-equivalent absent faking.** Where the platform lacks an addressable IO queue (Vulkan today, browser WebGPU), the engine emulates through a CPU-staged path on a Transfer queue — submission shape unchanged. Where no efficient asset-IO primitive exists at all (WebGPU), the cap reports `none` honestly; the engine does not pretend (MEL-ENGINE-VIII).
- **P2 — full-control escape hatches.** Apps that need a custom streamer (compression-dictionary management, paged virtual-texture residency, network-streamed bitstreams) bypass the queue and use GPU `buffer_write` / `cmd_copy_*` over their own thread pool. The primitives exposed are sufficient to reimplement the queue.
- **MEL-ENGINE-I — don't shy from the hard problem.** Vulkan has no shipped equivalent of DirectStorage; proposals are discussion-stage, vendor extensions (`VK_NV_memory_decompression` and analogs) cover only the decompress step. The engine still surfaces a unified asset-IO object so Melody apps do not rewrite their streamer per platform. Refusal would be cowardice.
- **MEL-ENGINE-II — hide complexity, not power.** The simple path is decompression-aware load through `cmd_io_load(src, src_offset, dst, codec)`; the powerful path exposes `cmd_io_decompress` and `cmd_io_decode_block_compressed` as separate primitives so a custom streamer can drive each stage.
- **MEL-ENGINE-III — no stolen cycles.** The IO queue is opt-in. An app that never calls `queue_request(AssetIo)` pays nothing — no thread, no command pool, no codec dispatch shader compiled.
- **MEL-ENGINE-VIII — codec absence is reported, not faked.** Caps enumerate the supported codec set per backend; a request for an unsupported codec returns an honest error, not a silent CPU-decompress detour that the caller has no way to audit.

---

## 3. Public objects

### 3.1 `Mel_Io_Asset_Queue`

Sibling queue type to `Mel_Gpu_Queue` (§5.2 of gpu-rhi.md), requested through the same availability/acquire model with role `AssetIo`. Submission to the asset-IO queue is independent of the graphics / compute timelines; cross-queue handoff flows through timeline semaphores (U17) identically to any other queue pair. The completion future returned by submission is the ordering edge any subsequent graphics or compute submission waits on.

The queue is not a `Mel_Gpu_Queue` — its command-list class differs (§3.2), and the platforms it lowers to are storage runtimes (`IDStorageQueue`, `MTLIOCommandQueue`) rather than the rendering command queues. Treating it as a peer of `Mel_Gpu_Queue` would conflate two distinct submission shapes and force an `if-platform-then-tag` discipline on every caller (MEL-ENGINE-IX: parts compose by being distinct, not by being conflated).

### 3.2 `Mel_Io_Asset_Command_List`

The recording surface for asset-IO commands. The three operations listed in §4 are valid here and *only* here; they are rejected on an ordinary `Mel_Gpu_Command_List`. Conversely, ordinary draw / dispatch commands are rejected on the asset-IO command list. The separation is enforced at recording, not at submit; misuse fires a debug assertion at the call site (MEL-ENGINE-VIII).

### 3.3 `Mel_Io_Asset_Source`

Wraps the bytes the queue reads from. The source is one of: a file handle (`IDStorageFile` on D3D12, `MTLIOFileHandle` on Metal, a raw fd on POSIX, a `HANDLE` on Win32 where the IO runtime is absent), a memory-mapped region, or a platform-native source (a `fetch()` Response on Web). The descriptor is `{ kind, native_handle, size_hint?, name? }`.

Sources are imported as `Borrowed` (peer of the U5 external-resource discipline) — the engine never closes the underlying handle. The caller owns the source's lifecycle: the OS file descriptor, the `IDStorageFile` reference, the `MTLIOFileHandle` retain count, the streaming HTTP response — Melody touches none of them at destroy time. This is MEL-ENGINE-IV (conventions yes, but the user owns the source's lifecycle): the engine has an opinion about how IO is shaped, never about who closes the file.

---

## 4. Operations

Pinned set, recorded into `Mel_Io_Asset_Command_List`:

- `cmd_io_load(src, src_offset, dst_buffer | dst_texture_region, decompress_codec)` — reads bytes from `src` starting at `src_offset` into a GPU buffer or texture subregion, optionally GPU-decompressing along the way. The compressed length is implicit in the source descriptor or the codec stream; the destination extent is taken from `dst_buffer.size` or `dst_texture_region.extent_xyz`. `decompress_codec = none` is the raw-copy path.
- `cmd_io_decompress(src_buffer, dst_buffer, codec)` — decompresses an in-memory buffer into another buffer without touching disk. Useful when the bitstream has already been staged (network, archive demuxer, custom streamer) but the GPU-side decode is still wanted.
- `cmd_io_decode_block_compressed(src_buffer, dst_texture_view, format)` — runs the GPU-side block-compression decoder for BCn / ASTC / ETC2 on a buffer of raw blocks, writing the decoded texels into a texture view. Block-aligned offsets and extents are debug-asserted (4×4 for BCn, the per-format block dimensions for ASTC and ETC2) — the same convention as `texture_write`'s block-compressed path (§6.2 of gpu-rhi.md).

Synchronization between asset-IO and graphics / compute flows through timeline semaphores and the completion future returned by submission. There is no implicit barrier into the graphics queue; the caller signals a timeline value at submit and the graphics queue waits it.

---

## 5. Codec enum

Pinned, exhaustive:

- `none` — raw copy, no decompression.
- `gdeflate` — D3D12 retail (DirectStorage GDeflate), Vulkan vendor (`VK_NV_memory_decompression` and analogs).
- `lz4` — engine-portable, CPU-decompress on Vulkan-no-extension and WebGPU.
- `apple_lzfse` — Apple Lossless family, `MTLIOCompressionMethod.lzfse`.
- `apple_lzbitmap` — Apple Lossless family, `MTLIOCompressionMethod.lzBitmap`.
- `zstd` — engine-side fallback codec; compute-shader decompress on any backend that ships none of the above.

Caps enumerate the supported codec set per backend; a request for a codec absent from caps returns an honest error (MEL-ENGINE-VIII).

---

## 6. Per-backend lowerings

- **D3D12** — `IDStorageQueue` plus `IDStorageFile` plus GDeflate (RTX IO on supporting hardware). On hardware lacking GPU decompress the runtime falls to a CPU-staged path *through the same `IDStorageQueue`* so the submission shape is unchanged from the caller's perspective; `caps.asset_io` reports `cpu_staged` versus `gpu_decompress`.
- **Metal** — `MTLIOCommandQueue` plus `MTLIOFileHandle` plus `MTLIOCompressionMethod` for the codec axis. Apple-native codecs (`lzfse`, `lzBitmap`) map 1:1; cross-platform codecs lower to the engine compute-shader path where Metal lacks a native enum.
- **Vulkan with no extension** — `cmd_io_load` lowers to a CPU-staged read into an UPLOAD buffer plus a graphics-queue (or Transfer-queue, per §7) copy; `caps.asset_io = cpu_staged`. GPU decompress lowers to a compute shader running the codec — the `zstd` and `lz4` paths in particular live entirely in engine-shipped Slang. Where `VK_NV_memory_decompression` (or analog) is granted, `gdeflate` lowers natively and `caps.asset_io = gpu_decompress`.
- **WebGPU** — `caps.asset_io = none` honestly. There is no efficient asset-IO primitive in WebGPU; apps stream through `fetch()` plus `queueWriteBuffer` and pay the staging round-trip. The engine surfaces the same `cmd_io_load` shape (so the caller's code does not branch) but reports the staging round-trip in the completion future's status.

---

## 7. Caps integration

`caps.asset_io` lives on the GPU module (§3 of gpu-rhi.md exposes it; this module reads it):

- `caps.asset_io = none` — WebGPU. Submissions still resolve; they fall through `fetch()` plus `queueWriteBuffer`.
- `caps.asset_io = cpu_staged` — Vulkan without `VK_NV_memory_decompression` (or analog), D3D12 hardware lacking GPU decompress. The IO runtime exists; GPU-side decompress does not.
- `caps.asset_io = gpu_decompress` — D3D12 with RTX IO / equivalent, Metal, Vulkan with the vendor extension granted.

Codec availability is a separate caps query enumerating the supported codec set per backend; a granted tier of `gpu_decompress` does not imply every codec is GPU-accelerated.

---

## 8. Queue role chain

Cross-reference §5.2 of gpu-rhi.md. The `AssetIo` role's fallback chain is:

    AssetIo → CPU-staged Transfer

Where the platform lacks an addressable IO queue (Vulkan today, browser WebGPU), `queue_request(AssetIo)` lowers to a Transfer queue that the `Mel_Io_Asset_Queue` services through CPU staging. The chain widens upward through Transfer's own chain (`Transfer → Compute → Graphics`) if no dedicated Transfer family exists. The fallback is reported by `caps.asset_io` and through a U2 warning naming the substitution.

The lowered queue is still surfaced as a `Mel_Io_Asset_Queue` — the type does not change with the tier. The command-list class is still `Mel_Io_Asset_Command_List`; the engine internally translates each command into the equivalent CPU-read-plus-copy sequence on the granted Transfer queue.

---

## 9. Borrowed-source discipline

Sources are imported as `Borrowed`. The engine retains a reference for the lifetime of pending submissions, but never closes, unmaps, or finalizes the underlying handle. When a source is destroyed (`source_destroy`), the engine drops its reference; the OS resource is released by whoever owns it.

This is the U5 import discipline applied to storage. A file is not a GPU resource — it is owned by the OS subsystem the app uses to open it — and the engine treats it that way (MEL-ENGINE-IV: the engine has an opinion about how IO is shaped, never about who closes the file).

---

## 10. P2 escape

Apps that need a custom streamer — compression-dictionary management, paged virtual-texture residency with explicit page tables, network-streamed bitstreams that interleave demux and decompress, archive-format demuxers with non-aligned chunk boundaries — bypass the queue and use direct GPU `buffer_write` / `cmd_copy_buffer` / `cmd_copy_buffer_to_texture` over their own thread pool. The primitives exposed by this module are sufficient to reimplement the queue: source wrap (`Mel_Io_Asset_Source` accepts any of the supported source kinds), decompress codec dispatch (`cmd_io_decompress` is callable in isolation), and completion-future bridging (the same future shape any GPU submission returns).

The simple path *is* the powerful path further along; the engine never hides the primitive beneath the convenience (MEL-ENGINE-II).
