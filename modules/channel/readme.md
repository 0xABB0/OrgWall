# channel

CSP point-to-point stream, M producers → N consumers. Typed by a fixed `item_size`; opaque
items copied by value. Created with a capacity: `0` is an unbuffered rendezvous (a direct
sender→receiver handoff with no buffer copy when both parties are present); `N>0` is a buffered
bounded ring, allocator-fed and sized once at creation.

Two flavors, mirroring `signal`/`future`:

- **fiber-blocking** — `mel_channel_send`/`mel_channel_recv`/`mel_channel_sel_wait` park a worker
  fiber (via `signal`) until the op completes. Legal only on a job worker fiber (the parking
  primitive `mel_signal_wait` is the job runtime's). These live in `channel_fiber.c` so a consumer
  of the callback flavor never links the job runtime.
- **future-returning** — `mel_channel_send_future`/`mel_channel_recv_future` resolve a `Mel_Future`
  on a target executor when the op completes. For the callback/reactor world.

The same channel serves both flavors and the lock-free `try`/`close` surface at once.

## Status

`Mel_Channel_Status` is severity (low 2 bits) plus a bitset; never an enum. `MEL_CHANNEL_CLOSED`
is a bit. `MEL_CHANNEL_WOULD_BLOCK` is the non-blocking "not ready" of `try_*`. A send on a closed
channel is `MEL_CHANNEL_ERROR | MEL_CHANNEL_CLOSED` (MEL-ENGINE-VIII), never silent.

## Concurrency model

A per-channel `Mel_Mutex` serializes the ring (head/tail/count), the two intrusive waiter queues
(senders, receivers), and the `closed` flag. Inside the critical section the state machine is
sequential and easy to reason about; the lock is held only for the O(1) decision, never across a
park's sleep.

Parked senders/receivers are `Mel_Channel_Op` nodes embedded in the caller's stack frame (blocking,
select) or in one heap record per future op. No per-op allocation on the blocking or select steady
path. Each `Mel_Channel_Op` links into a queue via an embedded `Mel_ListNode`; O(1) retract.

### The single-CAS commit

Every waiter points at a `group_state` (`_Atomic(i32)`): `PENDING(0)`, `COMMITTED(1)`, `CLOSED(2)`.
A single-op blocking/future waiter owns a private state; a `select` shares one state across all its
candidate ops. Exactly one of *complete* or *close* wins the waiter, by

    CAS group_state: PENDING → COMMITTED   (a counterpart/buffer completed the op)
    CAS group_state: PENDING → CLOSED      (close fired the waiter)

`acq_rel` on success, `acquire` on failure. The loser of the CAS does nothing. This is the
"committed XOR closed, never both, never neither" guarantee: a `close` racing a parked `send`/`recv`
resolves through the same CAS as a counterpart would.

### Rendezvous (capacity 0)

An arriving sender claims a parked receiver (`pop_claim`: remove from queue, win its group CAS),
`memcpy`s sender slot → receiver slot directly (no intermediate buffer), sets the receiver's winner,
fires its waker; the sender returns without parking. Symmetric for an arriving receiver. With no
counterpart present the arriver parks.

### Buffered (capacity N)

Send into free ring space copies sender → ring and returns. A recv from a non-empty ring copies
ring → out, then if a sender is parked, pulls its item into the freed slot and wakes it (keeps the
ring full under backpressure). With the ring full a sender parks; with the ring empty a receiver
either takes from a parked sender directly or parks.

### Select

`mel_channel_sel_wait` acquires *all* candidate channels' locks in ascending address order
(deadlock-free; no allocation — the minimum-unlocked channel is found by scan, no sorted side
array). Under all locks held it probes each op once: the first ready completes (committing its
counterpart and the data transfer), the shared group is marked `COMMITTED`, the rest are abandoned;
if none is ready, every op is parked on its channel. All locks drop, then the fiber waits. Holding
every candidate lock during the decision removes the cross-channel commit race: no counterpart on
any candidate can be committed by another party (they need a lock we hold), and the select's own
group can be won only by us — so there is no orphaned-counterpart window. On wake the winner is read
from the shared state; losers still parked are retracted under their channel lock.

`mel_channel_sel_try` is the non-parking probe: first ready op wins, else `NULL`.

### Close

Under the lock: set `closed`, splice both waiter queues out, drop the lock, then fire each waiter
with the `PENDING → CLOSED` CAS and wake the survivors. Buffered items are left in the ring; a
subsequent `recv` drains them first and only reports `MEL_CHANNEL_CLOSED` once the ring is empty.
A parked sender woken by close completes with `ERROR | CLOSED`.

## Memory-order contract

- Waiter commit/close: `compare_exchange_strong(group_state, …, acq_rel, acquire)`. The acquire on
  the post-wait load of `group_state` pairs with the committer's release, publishing the slot copy
  (done before the CAS, under the channel lock) to the woken party.
- The channel mutex's release/acquire orders the ring and queue mutations between threads; the
  intra-critical-section work uses no atomics beyond the group CAS, save the relaxed `owner_channel`
  store/load (a parked op's home channel). It is relaxed because correctness rests on the group CAS
  and the per-channel lock; `owner_channel` is only a lockless hint for `select`'s post-wake retract,
  re-validated under the channel lock. It is atomic solely to avoid a data race (UB) when `select`
  reads it lock-free while a concurrent `pop_claim` on a loser channel clears it.
- The blocking flavor's wake is `mel_signal_set` (release on the signal state); the waiter's
  `mel_signal_wait` acquires. The post-wait `group_state` acquire load then reads the committed
  outcome and the (already published) slot bytes.
- No seq_cst anywhere; acq_rel/acquire/release/relaxed only.

## Invariants

- An item is delivered exactly once: the producer's slot is consumed under the lock and the
  receiver's group is won by a single CAS; a lost CAS transfers nothing.
- No lost wakeup: a waiter is parked under the same lock that an active counterpart or `close` takes
  to fire it; the fire either finds it parked (and CASes it) or the active op already completed it
  before it parked.
- `mel_channel_destroy` asserts both waiter queues are empty (no outstanding parked op).

## deps

core, allocator, collection, executor, future, signal, thread. The fiber-blocking flavor
additionally requires the job runtime at link time (it provides `mel_signal_wait`).
