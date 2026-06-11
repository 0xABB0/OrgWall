# io: extract Mel_IO_Op to shared public header

## Work done

The stream-op record `{ Mel_Future future; Mel_IO_Result result; const Mel_Alloc* alloc; bool owned; }` existed as three independent copy-paste definitions:

- `Mel_IO_Op` in `modules/io/src/io_internal.h` (private to io)
- `Pipe_Op` in `modules/process/src/process_pipe.c`
- `Conn_Stream_Op` in `modules/net/src/tcp.c`

The latter two carried `static_assert` guards documenting the implicit ABI contract with io's `mel_stream_future_result` / `mel_stream_future_release`, which use `mel_container_of(f, Mel_IO_Op, future)` and therefore require the future to lead the record and the full layout to match.

Changes:

- Added `modules/io/include/io/op.h` — the single authoritative definition of `Mel_IO_Op`, pulling in its dependencies (`<io/status.h>`, `<io/stream.h>`, `<future/future.h>`, `<allocator/allocator.fwd.h>`, `<core/types.h>`).
- `modules/io/src/io_internal.h` — removed the local `Mel_IO_Op` typedef; replaced with `#include <io/op.h>`.
- `modules/process/src/process_pipe.c` — replaced the `Pipe_Op` struct + `static_assert` with `typedef Mel_IO_Op Pipe_Op;` and `#include <io/op.h>`. Removed `<stddef.h>` (was there only for `offsetof`).
- `modules/net/src/tcp.c` — replaced the `Conn_Stream_Op` struct + `static_assert` with `typedef Mel_IO_Op Conn_Stream_Op;` and `#include <io/op.h>`. Removed `<stddef.h>`.

All three build clean: `./nob build io`, `./nob build process`, `./nob build net`.

## Kludges

None. The typedef aliases (`Pipe_Op`, `Conn_Stream_Op`) preserve the existing local names so no further churn is required in each file's internal call sites, at zero cost.

## CLAUDE.md suggestions

None.

## Suggestions

The typedef aliases are noise; a follow-up could delete them and use `Mel_IO_Op` directly throughout both files. Worth doing when touching those files for another reason.

`mel_stream_future_result` and `mel_stream_future_release` in `io/src/stream.c` use bare `mel_container_of(f, Mel_IO_Op, future)`. Since `Mel_IO_Op` is now public, any stream implementor that rolls its own op type without aliasing `Mel_IO_Op` will silently misuse those functions. A typed accessor pattern (passing the result type through a function pointer on `Mel_Stream_Iface`) would enforce correctness at compile time, but that is a larger API change.
