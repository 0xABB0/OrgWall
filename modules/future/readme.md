# future

One-shot result of Melody's async substrate: a `Mel_Future` is a write-once cell that transitions
`pending -> (resolved | cancelled)` under a single CAS, fires at most one registered continuation
exactly once, and never allocates on the resolve/cancel path.

Deps: core, allocator, executor. (The test additionally pulls collection for `mel_container_of` and
the counting allocator harness.)

This is the 1->1 one-shot corner of the coordination trio. Fan-out (1->N broadcast) is **event**;
M->N streaming is **channel**. A second continuation on a future is misuse, asserted — reach for
`event` when you need more than one consumer.

## State machine

`state` is `_Atomic(u32)`: `pending(0)`, `resolved(1)`, `cancelled(2)`. `resolve` and `cancel` each
attempt one CAS `pending -> terminal`. Exactly one wins; the loser is a no-op. A losing `resolve`
frees its rejected value through the future's result allocator (`free_value`, supplied at init), so a
value handed to a future that has already been cancelled is never leaked and never double-freed.

`resolve` rejects a status carrying the `CANCELLED` bit (assert); cancellation is reached only through
`cancel`, which stamps `ERROR | CANCELLED`.

## then: register-then-recheck

`mel_future_then(f, cont, target_executor)` stores the continuation `Mel_Task*` and its target
executor, then re-tests the terminal flag and self-delivers if the future already resolved — closing
the lost-wakeup window between a producer that resolved first and a consumer that registered after.

Delivery is single-fired by a one-shot CAS that only one of `{resolve/cancel, then}` can win, and
only after observing **both** a terminal state and a registered continuation. Whichever of the two
runs last sees both conditions and submits; the earlier one saw at most one and returned. The recheck
is a mutual-flag rendezvous: `resolve`/`cancel` publishes the terminal `state`, `then` publishes
`cont`, and each then re-reads the other. For neither party to miss the other's flag, the publish and
the recheck-load of the two coordination words must be **sequentially consistent** — acquire/release
alone permits StoreLoad reordering and loses the wakeup when both writers race. With the seq-cst
recheck there is no lost wakeup and no double delivery.

The continuation is delivered by `submit`ting `cont` (a `Mel_Task` the caller embeds and recovers by
`mel_container_of`) to `target_executor`. A cancelled future still delivers `cont`; the continuation
reads `mel_future_status` and observes the `CANCELLED` bit. A cancelled await therefore resumes its
continuation with a cancelled status and never leaks the task.

## Cross-thread visibility

Value/status visibility rides the terminal `state` publish: the producer writes `value`/`status`
**then** releases the terminal `state` word, so the publish of `state` carries the payload writes with
it. The deliver-winner — which may be the registrar, not the producer — acquires `state` before
submitting, so the payload is visible to it and, through the executor's release-enqueue /
acquire-dequeue, to the continuation. (A bare release **fence** with no atomic store sequenced after it
does *not* publish the payload to a registrar-winner; the synchronizing edge must be a release/seq-cst
store on `state` itself.) The supported way to read a future's value across threads is **from its
continuation**, not a bare `mel_future_value` poll on another thread.

## Cost

`Mel_Future` carries only what it needs: two atomics (state, deliver gate), the value+status cell, the
continuation task pointer + its executor, and the result allocator/free hook. `resolve` and `cancel`
are a CAS plus at most one `submit` (one wake) and **zero allocation**. The continuation re-submit
cell is the caller/future-owned `Mel_Task`, not a per-resolve allocation.

## Combinators

`mel_future_when_all` resolves its aggregate once every input is terminal; the aggregate status is the
worst input severity OR-merged with the union of input bits, plus `PARTIAL` when any input was not
OK. `mel_future_when_any` resolves the aggregate from the first input to fire (one-shot gate),
carrying that input's value and status; a first-to-fire cancellation propagates as a cancelled
aggregate. Empty `when_all` resolves OK immediately; empty `when_any` cancels immediately (no input
can ever win).

**Allocation (inherent).** A `when` allocates exactly two blocks from the supplied allocator: the
`Mel_Future_When` aggregate record and one array of per-input join nodes (`n` intrusive
`Mel_Task`s). This is inherent — joining N inputs needs N continuation slots, and a future admits one
continuation each, so the join owns N nodes. Each join node is registered on its input via
`mel_future_then` against `mel_executor_inline()`; completions trampoline through the inline drain.
The aggregate future itself is read via `mel_future_when_future`; the whole record (aggregate + nodes)
is released by `mel_future_when_free`. The allocator is a required parameter — no silent default.

## Structured cancellation: scope (folded, not separate)

`Mel_Future_Scope` lives here rather than in a separate `scope` module. The substrate design left this
open; the deciding factor is dependency surface and primitive cohesion: a scope that "cancels its
children and wakes them cancelled before their target executor is torn down" is nothing but a batch of
`mel_future_cancel` calls over a tracked set — it needs `Mel_Future` and an allocator-backed dynamic
array, and nothing else. A separate module would carry the exact same deps (core, executor, allocator)
and add a module boundary around a dozen lines that only ever call into `future`. Folding keeps the
teardown-ordering primitive adjacent to the cancellation it drives. Should a future scope variant grow
real machinery (cross-executor lifetime trees, nursery-style join-on-exit) the split can be revisited;
today separation is unjustified.

`mel_future_scope_adopt(scope, f)` tracks a pending future. `mel_future_scope_cancel(scope)` cancels
every tracked child (already-terminal children are no-ops, by the one-shot CAS).
`mel_future_scope_teardown(scope)` cancels all children **then** frees the tracking array — the
ordering primitive: children are cancelled and their continuations woken cancelled **before** the
caller proceeds to tear down the target executor, so no continuation is ever submitted to a freed
executor.

## Status

`Mel_Future_Status` is a `u32`: severity in the low 2 bits (`OK`/`WARNED`/`ERROR`) plus a result
bitset (`CANCELLED`, `TIMED_OUT`, `BROKEN`, `PARTIAL`). It is **not** an enum (MEL-CODE-001), mirroring
`Mel_Clip_Status`; new result bits extend the set without breaking the closed-severity floor. Query
with `mel_future_status_failed` / `_warned` / `_cancelled`.
