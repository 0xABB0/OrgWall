# execution

C-callable seam over stdexec (the NVIDIA senders/receivers reference). The
framework's async vocabulary is stdexec; this module type-erases stdexec senders
behind opaque C handles so C translation units can construct, run, and await
async work without naming C++ types.

## Surface

- `Mel_Execution_Sender` — opaque, type-erased stdexec sender. One-shot.
- `mel_execution_sender_create(alloc, work, ctx)` — a sender that, when run,
  invokes `work(ctx)` and yields its `void*` result. Handle storage is allocated
  through `alloc` (MEL-CODE-003).
- `mel_execution_sync_wait(sender)` — drives the sender to completion on the
  calling thread; returns the value, or NULL if it completed stopped. Consumes
  the sender.
- `mel_execution_sender_destroy(alloc, sender)` — destroys and frees the handle.

## Dependencies

core, allocator, @stdexec.

## Constraints

- Bazel-only: not discovered by nob (C++23/stdexec is outside nob's toolchain).
- macOS host only so far; stdexec is unproven on the cross toolchains.
- The handle's own storage routes through `alloc`; stdexec's internal
  (SBO-overflow) allocations do not yet route through Mel allocators — that
  bridge lands with the executor-replacement design.
- No scheduler. Work runs inline at `sync_wait`. Schedulers, composition
  (`then` / `when_all` / …), and the `executor` replacement await their own
  design; this module is the type-erasure seed they build on.
