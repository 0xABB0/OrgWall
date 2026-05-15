# Melody Modules — Tiered Overview

## Scope

Melody is **Gabbo's reusable substrate across personal projects**. Not a stdlib, not a foundation library, not a framework — a personal library that aims to cover the recurring needs of every project Gabbo builds. Domain modules (GUI, audio, image, video, GPU, scripting, DB) are in scope. The library is **async-first** at every layer where async makes sense (see `async-first.md`).

This document lays out the full module landscape by tier, marks the status of current modules, and lists what is missing.

The library does **not** have to be built all at once. This is a map, not a backlog.

## Tier 1 — Core primitives (language-level)

Pure CPU, no runtime dependency, no async. The lowest layer.

**Have:**
- `core/` — compiler, platform, types
- `collection/` — array, list, linkedlist, ring, deque, queue, mpmc, workstealingqueue, heap, set, bitset, hashmap, btree, rbtree, trie, skiplist, slab, pool, slotmap, rcu (strong; 19+ structures)
- `math/` — vector, mat, geo
- `allocator/` — arena, block, buddy, guard, heap, leak, ring, stack, vmem (the most complete part of the repo)
- `hash/` — xxh (non-cryptographic only)
- `string/` — str8, path

**Missing:**

- `error/` — unified error model. One canonical "function failed, here's why". Without it, every module reinvents `bool ok / int retcode / NULL on failure` and composition gets ugly. Pick: tagged enum, out-parameter `Mel_Error*` with code+message+source-loc, or Result-via-macros. **Build this first.**
- `encoding/` — primitive transforms: base16/32/64, varint/LEB128, UTF-8↔UTF-16/32 conversion + validation, byte-order helpers, percent-encoding. Belongs near `string/`.
- `string/` extensions — UTF-8 codepoint iteration, normalization (NFC/NFD, optional), case folding, format-replacement-for-printf, builder, split/join, regex or glob.

**Reorg:**

- `hash/xxh` is misleadingly non-cryptographic. Rename to `hash.fast/` or split internally. Cryptographic hashes live in `crypto/` (Tier 5), not here.

## Tier 2 — Runtime substrate

The execution model. See `runtime.md` for the full breakdown.

**Have (after reorg):**
- `runtime/fiber/` — context-switch primitive (was `async/fiber`)
- `runtime/scheduler/` — job system (was `async/job`)
- `runtime/coroutine/` — cooperative tasks on fibers (was `async/coroutine`)

**Missing:**
- `runtime/thread`, `runtime/sync`, `runtime/channel`
- `runtime/cancel`, `runtime/buffer`
- `runtime/timer`, `runtime/reactor`, `runtime/task`
- `runtime/lifecycle`, `runtime/panic`, `runtime/plugin`

## Tier 3 — OS surface

The kernel. Mostly async (`async-first.md`).

**Have:**
- `process/` (move to `os/process`)

**Missing:**
- `os/file` — open/read/write/seek/close, memory-mapped files, file locking. **Async.**
- `os/fs` — paths (partially in `string.path`), directory iteration, stat, mkdir/rmdir, rename, remove, temp files, file watching. **Async.**
- `os/io` — the reactor surface itself (`runtime/reactor` exposes it; `os/io` is the typed-completion API)
- `os/signal` — POSIX signal handling, signal-safe, blocked-and-handled-on-a-thread pattern. **Async** (wait-for-signal).
- `os/ipc` — pipes, Unix domain sockets, shared memory, named events. **Async.**
- `os/env` — env vars, current dir, executable path, user dirs (config/cache/data). Mostly sync.
- `os/tty` — terminal capability, raw mode, color, size. Defer unless writing CLIs.

## Tier 4 — Network

Built on `os/io`. Fully async.

**Have:**
- `server/` — move up the stack; absorb into `net/http` once `net/` exists.

**Missing:**
- `net/socket` — TCP, UDP, Unix domain. Blocking + non-blocking, reactor-integrated.
- `net/dns` — async resolution. Do not trust libc `getaddrinfo` for serious work.
- `net/url` — parse, build, normalize, percent-encode. Sync.
- `net/tls` — wrap OpenSSL/mbedTLS/BoringSSL/rustls-via-C. Do not roll own.
- `net/http` — client first, server second. Absorbs current `server/`.
- `net/ws` — WebSocket on top of HTTP.

Decide scope: full HTTP/2+ and TLS are years of work. Wrap, don't reimplement.

## Tier 5 — Encoding / interop

Moving structured data in and out of the world.

**Missing:**
- `serial/json` — SAX + DOM, streaming. **Streaming variant is async; in-memory is sync.**
- `serial/cbor` or `serial/msgpack` — at least one binary format.
- `serial/binary` — typed binary stream reader/writer (varint, length-prefixed).
- `serial/reflect` — *optional, deferred.* Type descriptors for generic ser/de. Skip until you feel the pain twice.
- `compress/` — wrap zstd + lz4. Don't implement. gzip if format compat needed. **Sync core; async wrapper via offload pool.**
- `crypto/` — AEAD (ChaCha20-Poly1305, AES-GCM), cryptographic hashes (BLAKE3, SHA-256), HMAC, KDF (Argon2/HKDF), constant-time helpers. Wrap libsodium or BoringSSL. **Sync.**
- `rand/` — split `rand.fast` (xoshiro/PCG, seedable, reproducible) and `rand.secure` (OS CSPRNG). Mixing them is the bug the cryptographer finds.

## Tier 6 — Time / calendar

**Have:**
- `time/` — clock, nano, timer, frequency (the monotonic/duration half)

**Missing:**
- `time/calendar` — civil date/time, timezones, ISO 8601 parse/format, locale-free strftime-equivalent, day arithmetic. Timezones are data-driven; wrap `tzdata` via a small library if possible.

**Note:** `time/timer` may move under `runtime/timer` (the timer wheel is runtime infrastructure; the clock primitives stay in `time/`).

## Tier 7 — Configuration / input

**Missing:**
- `config/argv` — argv parser. Long/short options, subcommands, help generation.
- `config/file` — load TOML or INI or JSON config, merge with env vars and argv. A layered resolver more than a parser.

## Tier 8 — Observability

**Have:**
- `log/` — with sinks (console, file, sqlite). Strong.
- `debug/` — assert, stacktrace.

**Missing:**
- `telemetry/metric` — counters, gauges, histograms. Pull or push. Independent from logging (logs are events, metrics are aggregates).
- `telemetry/trace` — spans with parent/child, baggage propagation. OpenTelemetry-shaped if interop is desired. Substrate piece (creating spans, attaching to fiber/task context) is runtime-adjacent.
- `debug/profile` — sampling profiler hooks, scope timers.

## Tier 9 — Events (optional)

**Missing (or already exists under `async/signal`):**
- `event/bus` — in-process pub/sub. Typed channels are an alternative; a bus with subscriber lists is another. Pick one.

If `async/signal` is pub/sub, move it here.

## Tier 10 — Domain modules

These are application-shaped, not substrate. They sit on every tier below.

**Have:**
- `server/` — really `net/http` server (Tier 4); move it.
- `music.theory/` — domain module for Gabbo's music-related work.

**Planned (per Gabbo's project needs):**
- `gui/` — windowing + widgets. Reactor-integrated (the GUI event loop *is* a reactor; integrate it, don't fight it). macOS/Windows force main-thread UI — the reactor must accept thread pinning.
- `audio/` — split: **realtime callback path** (no allocations, no syscalls, no waiting — not async); **control path** (async — loading samples, building graphs, parameter changes). Fed by lock-free SPSC queues from the async side.
- `image/` — decode/encode primitives. Sync core, async wrappers via offload pool.
- `video/` — decode/encode. CPU-heavy with realtime-ish deadlines; worker pool with priority, not a reactor.
- `gpu/` — command record is sync; GPU waits are async via fences integrated into the reactor (Vulkan timeline semaphores, D3D12 fences, Metal events). Do not model GPU work as fibers.
- `db/` — driver per backend. Every query is async.
- `script/` — embedded VM (Lua/etc.). Concurrency model depends on VM; bridge to runtime.
- `clang/` — Clang/LLVM integration. Sync for parsing, async for builds.

These are domain-shaped and allowed to be incomplete. The substrate below them is not.

## Reorg summary (one pass)

```
modules/async/fiber/          → modules/runtime/fiber/
modules/async/job/            → modules/runtime/scheduler/
modules/async/coroutine/      → modules/runtime/coroutine/
modules/async/signal/         → modules/os/signal/  OR  modules/event/bus/
modules/process/              → modules/os/process/
modules/server/               → modules/net/http/  (once net/ exists)
modules/hash/                 → split into hash.fast/ (xxh) and crypto/ (Tier 5)
```

Do this reorg **before** building new runtime modules. Otherwise the names lie.

## Order of operations

Build outward from the runtime substrate. The first stack proves the design:

1. **`error`** — every async API will use it; nail it first.
2. **`runtime/thread`** + **`runtime/sync`** — trivial OS wrappers + atomics; unblocks everything.
3. **`runtime/cancel`** + **`runtime/buffer`** — the pieces nobody wants to design first but everyone regrets skipping.
4. **`runtime/timer`** — small, but the reactor needs it.
5. **`runtime/reactor`** — start with one backend (io_uring or kqueue). Add others later.
6. **`runtime/task`** — unify fiber-awaiting-completion with job-on-worker under one handle.
7. **`os/file`** — first concrete async API. Proves the stack end-to-end.
8. **`os/fs`** — second proof.
9. **`net/socket`** — third proof.
10. Everything else slots in opportunistically: dns → tls → http → db → encoding → serial → telemetry → domain modules.

Do **not** try to make every module async-first in one pass. Substrate first, prove it on file/fs/net, then propagate one domain at a time.

See also: `runtime.md`, `async-first.md`, `fiber-and-coroutine.md`.
