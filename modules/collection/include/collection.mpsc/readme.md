# collection.mpsc

Intrusive, lock-free, multi-producer / single-consumer queue (Vyukov). This is the never-fail
unbounded wakeup queue the executor wakeup path needs (async-substrate.md, "Waist").

Deps: core. Header: `<collection.mpsc/mpsc.h>`.

## Intrusive contract

The caller embeds `Mel_Mpsc_Node` in its own struct and recovers the owner by `mel_container_of`
(`<collection.list/list.h>`). The node is one atomic next pointer and nothing else. `push`/`pop`
allocate nothing, take nothing, free nothing; the queue owns no memory. Node lifetime is the
caller's: a node may not be re-pushed nor freed until the consumer has popped it.

The queue carries an internal sentinel `stub` so it is never empty in the linked sense; this costs
one node embedded in `Mel_Mpsc`, not a per-element allocation.

## API

- `void mel_mpsc_init(Mel_Mpsc* q)` — initializes head/tail to the embedded stub. Required before
  use; there is no silent default state.
- `void mel_mpsc_push(Mel_Mpsc* q, Mel_Mpsc_Node* node)` — any thread, any number of producers.
- `Mel_Mpsc_Node* mel_mpsc_pop(Mel_Mpsc* q)` — single consumer only; returns NULL on (possibly
  transient) empty. Concurrent consumers are a contract violation.

## Ordering contract

C11 atomics, minimal explicit orders.

- Push is a single `acq_rel` exchange of the tail, then a `release` store linking the predecessor's
  `next` to the new node. The release publishes every prior write the producer made to its owner
  struct.
- Pop reads `next` with `acquire`, which synchronizes-with that release: once the consumer observes a
  node, it observes all of the producer's writes that preceded the push. This is the
  release-enqueue / acquire-dequeue contract the substrate assumes on `submit`/`wake`.
- Per-producer FIFO: a single producer's nodes pop in push order. No cross-producer order is
  promised — interleaving among producers is whatever the tail exchanges resolve to, matching the
  substrate invariant "per-producer FIFO; no global cross-thread order."

## Complexity

- `push`: O(1), wait-free for the producer (one exchange + one store); never blocks, never fails.
- `pop`: O(1) amortized, lock-free; on the boundary it re-inserts the stub (one push) to relink.

## Caveats

- Transient empty: between a producer's tail exchange and its `next` store, the queue holds a node
  the consumer cannot yet reach; `pop` returns NULL though an element is in flight. The consumer must
  retry (the executor's `armed` coalescing flag absorbs this on the wakeup path). This is intrinsic
  to the algorithm, not a bug.
- Single consumer: `consumer_head` is plain (non-atomic) state mutated only by `pop`. Two concurrent
  poppers corrupt it. Enforce one consumer.
- False sharing: `Mel_Mpsc` aligns `producer_tail`, `consumer_head`, and `stub` to 64 bytes so the
  producer-contended tail and the consumer-private head sit on distinct cache lines; producers and
  the consumer never ping-pong the same line. Place a caller's node likewise if its owner struct is
  hot on a different core than the consumer.
- Debug builds count pushes/pops (`MEL_COLLECTION_MPSC_DEBUG`) for assertions/diagnostics; release
  builds carry no such field and no extra cycles.

## Validation

`test/test_mpsc.c`: single-thread FIFO, interleaved push/pop, empty and single-element edges,
drain-and-reuse, 8-producer/1-consumer stress (800k elements) with count + xor-checksum +
per-producer FIFO invariants, a 200k single-producer ordering check, oversubscription
(4×hardware-concurrency producers), a 200k-round pop-vs-last-push race exercising the transient-empty
boundary, node reuse across 50k drain cycles, and a `container_of` recovery with the node embedded at
a non-zero offset. Clean under ThreadSanitizer (the build system exposes no sanitizer flag; TSan was
run by a standalone instrumented compile of the test plus the mpsc, thread, and allocator sources,
`MEL_TEST_NOFORK=1`).
