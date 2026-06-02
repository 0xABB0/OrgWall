# cpu — drop the heap, stack-only scratch

## Work done

`mel_cpu_info` no longer touches the heap on any platform, and the now-vestigial
allocator parameter is gone.

- The struct was already returned by value on all four backends; the only heap user
  was win32, which `mel_alloc`'d the `GetLogicalProcessorInformationEx(RelationAll, …)`
  buffer. That buffer is now `_alloca`'d (`<malloc.h>`) to the exact length the probe
  call reports — stack-resident, released when `mel_cpu__topology` returns. The
  `if (!buf)` guard is dropped (`_alloca` raises on exhaustion, never returns NULL,
  MEL-ENGINE-VIII); the `len == 0` early-out stays so we never `_alloca(0)`.
- With win32 off the heap, every backend's `alloc` argument was dead. The whole
  parameter's documented rationale (readme "Allocator": win32 scratch, MEL-ENGINE-III)
  evaporated, so the public signature is now `Mel_Cpu_Info mel_cpu_info(void)`. Removed
  the `const Mel_Alloc*` param and the `MEL_UNUSED(alloc)` stubs from apple/linux/web,
  the `<allocator/...>` includes from the header and win32, the `allocator` dep from
  both `build.c` targets, and the `<allocator/heap.h>` + `mel_alloc_heap()` from the
  test.
- readme: replaced the "Allocator" section with "No heap"; updated both signature
  blocks and the win32 lowering bullet to describe the `_alloca` buffer.

Host (macOS / apple backend) builds with no allocator dependency; `cpu-test` passes
(0 failed) — counts, L1d/L2, page size, cache line filled; L3/clock honestly 0 on
Apple Silicon, unchanged.

## Kludges

- **Uncapped `_alloca(len)` on win32.** `len` comes from the OS, not a bound we set.
  On a pathological host (very many logical processors / cache records) the RelationAll
  buffer could grow large enough to threaten the thread stack. No cap is imposed —
  deliberately: a fixed cap would be both a magic constant (MEL-CODE-007) and a
  truncation hazard, and a fixed array would break MEL-CODE-002. On realistic desktops
  the buffer is a few KB against a ≥1 MB stack, and `_alloca` overflow faults loudly
  rather than corrupting (MEL-ENGINE-VIII). Debt: the failure mode is a hard crash on
  an extreme machine, with no graceful path. A bounded chunked walk (query each
  `Relation*` separately, process in a small fixed-purpose stack window) would remove
  it if such hosts ever matter.
- **win32 verified by inspection only.** The substantive change is the win32 backend,
  which compiles only on the remote Windows box. It was not built here — apple is the
  sole compiled/run backend this session. `_alloca` via `<malloc.h>` is valid on both
  the MSVC and clang-for-windows toolchains the repo targets, but that is asserted, not
  observed. Not pushed / not remote-built (no instruction to).

## CLAUDE.md suggestions

None.

## Suggestions

- If win32 hosts with hundreds of logical processors are ever in scope, replace the
  single RelationAll `_alloca` with a per-relationship walk to bound the stack draw;
  otherwise the current form is the right tradeoff.
- A `core`-level `mel_stack_scratch`/`alloca` wrapper (with a debug-only high-water
  assert) would make "stack scratch sized from an external length" a named, auditable
  pattern instead of a raw `_alloca` per call site.
