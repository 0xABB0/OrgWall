# allocator.tracking

`allocator.tracking` is a profiling and leak-detection allocator decorator.

It wraps another `Mel_Alloc` and records every allocation with provenance,
keeping live/peak/total accounting aggregated by call-site and by scope tag,
without changing the caller-facing API or the backing pointer it returns.

## Goals

- answer "how much is live, who allocated it, what leaked"
- compose under `guard` (`heap -> guard -> tracking`); orthogonal to corruption checks
- keep release cost at zero unless explicitly instantiated

## Core-only by necessity

`collection`, `log`, and `debug` all depend on `allocator`, so a decorator inside
the `allocator` module cannot use them without a cycle. `tracking` therefore:

- uses local intrusive structures, not `collection`
- reports through caller-supplied callbacks, not `log` (the consumer routes to log)
- captures backtraces as raw return addresses, symbolized by the consumer, not `debug`

This mirrors `leak`'s callback contract and `guard`'s intrusive-header + spinlock idiom.

## Model

A side registry maps each live pointer to a heap `Mel_Track_Header` (from the
`meta` allocator), leaving the backing allocation pristine. Two further tables
intern per-site and per-tag accumulators. Scope tags come from a thread-local
stack (`mel_track_scope_push`/`pop`), global per-thread like `log` context; each
allocation is attributed to the innermost tag.

## Provenance

Default is `file`/`func`/`line`, captured at the call site (requires
`MEL_CONFIG_DEBUG_ALLOCATOR`). `MEL_TRACK_FLAG_BACKTRACE` additionally records raw
return addresses per allocation; symbolization is the consumer's.

## Contracts

- `meta` must not be this tracker's own interface (else bookkeeping recurses and
  self-deadlocks); it defaults to `backing`
- `mel_track_shutdown` requires no live allocations — dump before shutting down
- dump callbacks run under the tracker lock and must not re-enter the tracker
- tags are stable strings (literals); they are referenced, not copied

## leak

`leak` is a preset of `tracking`: a process-global instance backed by `heap`,
live-list only. `mel_alloc_leak_detect` and `mel_leak_dump` are thin adapters.
