# io

Abstract byte streams on the vat. One `Mel_Stream` interface lowered onto several
backends: file, fixed-capacity memory, const memory, growable memory, and any
app-supplied interface. A stream binds to a `Mel_Vat*` (`Mel_Stream_Opt.vat`,
`Mel_IO_File_Open_Opt.vat`); its delivery executor defaults to
`mel_vat_executor(vat)`, so continuations land as loop turns like every other
Melody future. Submission is loop-thread-only where a vat is bound
(`mel_vat_is_owner` asserted, MEL-ENGINE-VIII).

## Why it exists

Every higher module that touches bytes — asset loading, serialization, capture,
network framing — needs the same shape: a seekable, sized, readable/writable channel
behind one interface. `io` provides that shape once. Async ops (`read`/`write`/
`flush`) return `Mel_Future*`; synchronous shims sit atop the same surface so simple
call sites stay simple (MEL-ENGINE-II).

## Source disciplines (the doctrine, `probe/main_design`)

What a file stream's ops do depends on what the opened handle *is*, because the
disciplines differ physically:

- **Pipes / fifos / sockets / character devices** — readiness is meaningful. With a
  vat bound, ops ride the `port` proactor (readiness synthesized up to completion on
  the vat's waiter); `caps.async = true` and the future resolves on a later turn.
- **Regular files** — readiness is theater: kqueue reports a regular file always
  "ready" and the `read()` can still block on a cold page. The honest discipline is
  **executor offload** (a worker performs the blocking syscall and rings the vat's
  doorbell). That offload path is **owed** (see below); this wave a regular file
  reports `caps.async = false` and its ops run the synchronous step inline,
  resolving the future before returning — honest caps over async theater
  (MEL-ENGINE-VIII). Continuations attached with `mel_future_then` on
  `mel_vat_executor(vat)` still deliver on the loop, next turn.

Whole-file helpers (`mel_io_load_file` / `mel_io_save_file`) therefore resolve
synchronously this wave; they keep the future surface so call sites do not churn
when the offload lands.

## Public surface

- `io/stream.h` — `Mel_Stream`, the open-world `Mel_Stream_Iface` vtable, async
  `mel_stream_read`/`write`/`flush` (→ `Mel_Future*` resolving to `Mel_IO_Result`),
  sync shims (`mel_stream_read_sync`, `mel_stream_read_exact`, `mel_stream_write_all`),
  position ops (`seek`/`tell`/`size`), endian-typed helpers (`u8`..`u64`, LE/BE),
  `mel_stream_vat`/`mel_stream_executor`, and typed native-handle escapes
  (`mel_stream_native_fd`, `mel_stream_native_file_handle`, `mel_stream_native_memory`)
  — never a property bag.
- `io/memory.h` — `mel_io_memory_fixed`, `mel_io_memory_const`,
  `mel_io_memory_growable` (+ `len`/`data`/`detach`). Platform-agnostic, vat-free.
- `io/file.h` — `mel_io_file_open` (flags + vat), and whole-file
  `mel_io_load_file` / `mel_io_save_file` (→ `Mel_Future*`).
- `io/status.h` — `Mel_IO_Status` bitset (severity mask + loss/condition flags) with
  inline predicates. No error strings (MEL-ENGINE-VIII).

A custom stream is just a `Mel_Stream_Iface` handed to `mel_stream_create`; the kind of
a stream is its interface, not an enum (MEL-CODE-001).

## Backends

- macos / ios / linux / android — POSIX (`open`/`pread`/`pwrite`/`lseek`/`fsync`).
  `Mel_IO_File_Native.readiness` is set honestly from `fstat`: true for
  fifo/socket/chardev (port path), false for regular files (sync step).
- win32 — CRT fd (`_open`/`_read`/`_write`/`_lseeki64`/`FlushFileBuffers`); native
  `HANDLE` exposed via `mel_stream_native_file_handle`. The CRT handle is
  synchronous, so `readiness = false` and ops run inline. True overlapped file I/O
  is pending a win-pilot build (see `todo.md`).
- wasm — Emscripten virtual FS (MEMFS by default; OPFS/IDBFS/NODEFS when mounted).
  `readiness = false`; ops degrade honestly to the direct virtual-FS path.

## Owed (MEL-ENGINE-VIII)

- **Regular-file executor offload**: a worker pool (or the `job` module) performs
  `pread`/`pwrite` off the loop and posts the completion through `mel_vat_post`,
  flipping `caps.async` to true for regular files. The shape mirrors what `fs`
  already does for metadata ops: per-op record embeds a `Mel_Task`, worker runs the
  syscall, posts the task, the task resolves the future on the loop;
  `mel_vat_retain`/`release` bracket the in-flight window. Until it lands, a long
  read on a vat thread blocks that turn — visible in the caps, never hidden.

## Dependencies

`core`, `allocator`, `collection`, `executor`, `future`, `vat`, `port`, `log`.
