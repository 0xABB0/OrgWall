# io

Async-first abstract byte streams. One `Mel_Stream` interface lowered onto several
backends: file (via the `port` proactor on native platforms), fixed-capacity memory,
const memory, growable memory, and any app-supplied interface.

## Why it exists

Every higher module that touches bytes — asset loading, serialization, capture,
network framing — needs the same shape: a seekable, sized, readable/writable channel
whose long operations do not block the loop thread. `io` provides that shape once.
Async ops (`read`/`write`/`flush`) return `Mel_Future*`; the file backend lowers them
onto the existing `port` proactor (kqueue/epoll/IOCP-shaped readiness), so completions
arrive as loop-thread continuations like every other Melody future. Synchronous
convenience shims sit atop the same surface for the memory backends and for direct
file syscalls, so simple call sites stay simple (MEL-ENGINE-II).

## Public surface

- `io/stream.h` — `Mel_Stream`, the open-world `Mel_Stream_Iface` vtable, async
  `mel_stream_read`/`write`/`flush` (→ `Mel_Future*` resolving to `Mel_IO_Result`),
  sync shims (`mel_stream_read_sync`, `mel_stream_read_exact`, `mel_stream_write_all`),
  position ops (`seek`/`tell`/`size`), endian-typed helpers (`u8`..`u64`, LE/BE),
  and typed native-handle escapes (`mel_stream_native_fd`,
  `mel_stream_native_file_handle`, `mel_stream_native_memory`) — never a property bag.
- `io/memory.h` — `mel_io_memory_fixed`, `mel_io_memory_const`,
  `mel_io_memory_growable` (+ `len`/`data`/`detach`). Platform-agnostic.
- `io/file.h` — `mel_io_file_open` (flags + reactor), and whole-file
  `mel_io_load_file` / `mel_io_save_file` (→ `Mel_Future*`).
- `io/status.h` — `Mel_IO_Status` bitset (severity mask + loss/condition flags) with
  inline predicates. No error strings (MEL-ENGINE-VIII).

A custom stream is just a `Mel_Stream_Iface` handed to `mel_stream_create`; the kind of
a stream is its interface, not an enum (MEL-CODE-001).

## Backends

- macos / ios / linux / android — POSIX (`open`/`pread`/`pwrite`/`lseek`/`fsync`),
  async lowered onto `port`.
- win32 — CRT fd (`_open`/`_read`/`_write`/`_lseeki64`/`FlushFileBuffers`); the same fd
  feeds `port`'s IOCP-shaped backend; native `HANDLE` exposed via
  `mel_stream_native_file_handle`.
- wasm — Emscripten virtual FS (MEMFS by default; OPFS/IDBFS/NODEFS when mounted).
  Synchronous: the `port` proactor is absent on wasm, so file ops degrade honestly to
  the direct virtual-FS path rather than failing.

## Dependencies

`core`, `allocator`, `collection`, `executor`, `future`, `reactor`, `port`, `log`.
