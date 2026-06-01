# 2026-06-01 — allocator.tracking

## Work done

Added a profiling + leak-detection allocator decorator, `allocator.tracking`,
and folded the immature `leak` module into it as a preset.

- `modules/allocator/include/allocator/tracking.{h,cfg.h,fwd.h}` — public API.
- `modules/allocator/src/tracking.c` — the decorator: per-instance spinlock,
  global live/peak/total counters, a side registry (`ptr -> header`), interned
  per-site and per-tag accumulator tables, a thread-local scope stack, opt-in raw
  backtrace capture. All core-only (no `collection`/`log`/`debug`), reporting via
  caller callbacks — forced by the dependency cycle (those three modules depend on
  `allocator`).
- `modules/allocator/src/leak.c` — reimplemented as a process-global `tracking`
  preset backed by `heap`, init-once like the guarded heap. The previous
  thread-unsafe, libc-bound global singleton is gone. Public API (`mel_alloc_leak_detect`,
  `mel_leak_dump`, `Mel_Leak_Report_Cb`) is unchanged; no external consumers existed.
- `modules/allocator/test/test.tracking.c` + `build.c` — six tests
  (counts roundtrip, site aggregation, tag-scope attribution, realloc accounting,
  live-set leak dump, leak preset). Registered the first `mel_add_test` target for
  this module; all pass.
- Module readme `allocator.tracking.md` and todo. (The freeform design spec
  written under `design/` during authoring was removed once the module gained
  these, per MEL-SPEC-002.)

Why: `guard` already covers corruption; the missing axis was *who allocated what,
how much is live, what leaked* — and `leak` as written was a thread-unsafe,
non-composable singleton. `tracking` supplies the profiling axis idiomatically
(a composable decorator, mirroring `guard`'s and `leak`'s existing idioms) and
absorbs `leak`.

## Kludges (MEL-ENGINE-VIII — confessing all)

- **Thread-local scope stack uses libc `malloc`/`realloc`/`free`**, not an engine
  allocator (MEL-CODE-003). The push/pop API are free functions with no allocator
  in scope; the prior `leak.c` used libc malloc in the same spirit. Freed on
  pop-to-empty, so no persistent per-thread leak.
- **64-bit hash treated as identity for site/tag keys.** Two distinct
  `(file,line)`/tags colliding on the hash would alias one bucket. Probability is
  negligible; documented, not silent. Site/tag objects retain the real key for reporting.
- **`assert` is libc `assert`, not `mel_assert`.** `mel_assert` is currently an
  empty no-op in both branches of `debug/assert.h`; the rest of the allocator
  module uses libc `assert`. Consequence: shutdown-with-leaks and free-of-untracked
  fire loudly in debug but vanish under `NDEBUG`. Pre-existing, inherited.
- **Backtrace capture is wired but untested.** `execinfo`/`CaptureStackBackTrace`
  compiles and fills the record; no test asserts on it (raw addresses are
  nondeterministic, symbolization is the consumer's). Covered by construction, not by test.
- **Dump callbacks run under the tracker spinlock.** A callback that re-enters the
  tracked allocator self-deadlocks. Documented contract, not enforced.
- **`meta != self` is a documented contract, not an assertion** — the interface is
  constructed lazily, so init can't cheaply compare against it.

## Surfaced (pre-existing, not introduced)

- **`MEL_CONFIG_DEBUG_ALLOCATOR` is defined nowhere in the build.** Without it,
  `mel_alloc` passes `NULL/NULL/0`, so `guard`'s corruption reports and all
  site/leak provenance are blank. The test defines it locally. Tracking's site
  value depends on it.
- **The allocator module's `test/*.c` (basic, aligned, page, policy, quarantine)
  are written but registered in no `build.c`** — i.e. never built or run. I added
  the first test target; the others remain orphaned.

## CLAUDE.md suggestions (recommendations only — not applied)

- Tie `MEL_CONFIG_DEBUG_ALLOCATOR` to `MEL_MEMORY_DEBUG` so provenance turns on
  with the memory-debug policy rather than being separately, silently off.
- Implement `mel_assert` so MEL-ENGINE-VIII's "fail loud" actually fires; it is
  presently a no-op, which quietly defeats the commandment.

## Suggestions

- Land granular spec 5: a consumer-side `log` reporter (route the callbacks to
  `mel_log_*` from a layer that may depend on `log`/`debug`), backtrace
  symbolication, and a snapshot/diff for per-frame deltas. Tracked in the module todo.
- Wire the orphaned allocator tests into `build.c`.
