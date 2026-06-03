# event

Many-shot 1→N broadcast over the executor waist. One `Mel_Event` channel carries opaque items of a
fixed `item_size` (chosen at creation) to N subscribers. `mel_event_fire` is callable from any thread
(`async-substrate.md`); it fans the item to each subscriber and, for push subscribers, hands delivery
to that subscriber's own executor.

## Concurrency

Fire-from-any-thread is honored. Two structures cooperate:

- **Subscriber-set traversal** is over an atomically-published immutable **snapshot** array, not the
  slotmap's swap-removed dense array. `fire` acquires the current snapshot (refcounted), walks it
  without holding the membership lock, then releases it. `subscribe`/`unsubscribe` mutate the slotmap
  under a per-channel spinlock and republish a fresh snapshot. A node listed by a live snapshot is
  ref-held; a node removed mid-fanout is reclaimed only after the last snapshot (and the last in-flight
  delivery) referencing it drops its ref. This makes **sibling**-unsubscribe-during-fanout safe and
  correct: the firing walk sees a stable membership and never dereferences a freed node (ASan-clean).
- **Per-subscriber item transport** is an intrusive FIFO over a bounded node pool, guarded by a
  per-subscriber spinlock with O(1) critical sections that never span the user callback. Concurrent
  fires push under that lock; the single consumer (delivery task or `mel_event_pull`) pops under it.
  Delivery detaches a node, releases the lock, runs the callback in place on the node's payload, then
  recycles the node — the callback may re-enter `event` (unsubscribe self or sibling, fire) without
  deadlock. Push delivery is coalesced: at most one delivery task is outstanding per subscriber
  (an armed gate with clear-then-recheck), each tied to exactly one node ref.

Validated under ThreadSanitizer: N producer threads firing concurrently, plus a churn thread doing
subscribe/pull/unsubscribe, race-free; every fired item is delivered exactly once (no loss, no
double-delivery), and lag is zero when capacity covers the burst.

Identity is a generation-checked handle (`collection.slotmap`, the recovery idiom clipboard uses).
Stale handles resolve to nothing; a reused slot carries a distinct generation. Self-unsubscribe from
inside one's own delivery callback is safe (the in-flight delivery holds a node ref).

## Lifetime / teardown

The channel is refcounted (mirroring the node and snapshot refcounts). The born ref is the owner's;
each push delivery takes a channel ref before `submit` and drops it after the callback runs (alongside
the node ref it already holds). `mel_event_destroy` drops the owner ref — the `Mel_Event` and all its
nodes/pools/buffered items are actually freed only when the **last** ref drops (owner plus every
outstanding delivery). So `destroy` is safe to call with deliveries in flight on **any** executor
(reactor/job included): the channel outlives its outstanding deliveries and is reclaimed when the last
one completes.

Free order in a delivery's completion is exact: drop the node ref first (`node_free` may read
`ev->alloc`, still valid because the delivery still holds the channel ref), then drop the channel ref
(which may free the channel). `node_free` never touches a freed channel, and `deliver_run`'s
`node->owner` deref is always valid. Validated under ASan with a deferred executor: fire → destroy →
drain the deferred delivery later → no use-after-free, no leak (channel/nodes/pools/buffered items all
reclaimed at the last ref).

`mel_event_fire`, `mel_event_subscribe_*`, `mel_event_unsubscribe`, `mel_event_pull*`, and
`mel_event_destroy` are all internally synchronized and callable from any thread.

## Why a spinlock, not the lock-free MPSC verbatim

The item transport uses an intrusive pooled FIFO under a per-subscriber spinlock rather than
`collection.mpsc` directly. `collection.mpsc` is multi-**producer** but single-**consumer**: a free
list recycled by many concurrent producers would be multi-consumer (each producer pops a node), which
the MPSC pop does not support, and `mel_pool`'s lock-free Treiber stack asserts on exhaustion rather
than reporting it — so policy admission (drop-oldest / refuse) on a full pool has no graceful
lock-free expression. Producer-side **drop-oldest** further needs to evict the oldest queued item,
which a single-consumer queue forbids from the producer. tokio's own `broadcast`/`watch` take a lock
on the send path for exactly these reasons. The spinlock keeps all three loss policies correct and
faithful with O(1), callback-free critical sections; the membership walk stays lock-free via the
snapshot. (MEL-ENGINE-VIII: this is a deliberate, disclosed departure from a verbatim lock-free-MPSC
transport.)

## Delivery shapes

A subscription is created with an explicit shape; the constructor is the choice, not a flag:

- `mel_event_subscribe_push(ev, exec, cb, user)` — the channel submits a `Mel_Task` to `exec` that
  drains the subscriber's buffered items and invokes `cb(item, user)` per item, in order. The task is
  intrusive (embedded in the subscriber node), armed-coalesced by the waist; no per-fire task
  allocation.
- `mel_event_subscribe_pull(ev, user)` — no callback, no executor. The subscriber drains its own
  buffer with `mel_event_pull(ev, sub, &out)`; `mel_event_pull_pending` reports how many are waiting.

Both shapes back onto the same per-subscriber bounded node pool, allocated once at subscribe time from
the channel's allocator (`ring_capacity` item nodes, each `sizeof(header) + item_size`). The steady
fire path takes a free node, copies the item in, links it into the FIFO, and (for push) submits the
coalesced delivery task — no allocation per fire; nodes are recycled to the pool on consumption.

## Loss policy (no enum)

The loss policy is chosen at channel creation and is mandatory — there is no silent default
(MEL-CODE-007). It is encoded as a behavior, not a tag: `Mel_Event_Policy` carries an `overflow`
function pointer (plus an optional `on_overflow` reporter and a `user`). When a subscriber's node pool
is exhausted (FIFO at capacity), the engine calls `overflow(&info)`; the function fills disposition fields on the `info` struct
(`drop_oldest`, `accepted`, `backpressured`) and the engine acts on what the behavior produced. There
is no `switch` over a kind anywhere — the disposition is *data produced by a behavior*, never a closed
enumerant (MEL-CODE-001). The three named constructors bind three distinct behaviors:

- `mel_event_policy_latest` — drop the oldest buffered item, admit the newest (a watch). Keeps the
  freshest; counts each evicted item as lag.
- `mel_event_policy_lossy_lag` — keep the oldest, refuse the newest, count it as lag (reported, not
  silent).
- `mel_event_policy_lossless` — refuse on full and report backpressure loudly (a logged warning plus
  the `on_overflow` report); never silently drops, never reorders. Because a cross-thread `fire`
  cannot park, lossless reports backpressure rather than blocking the producer.

The encoding is open: `mel_event_policy_custom(overflow, on_overflow, user)` lets a consumer supply
its own disposition behavior. That openness is why an enum would be the wrong abstraction here — a new
policy is a new function, not a new tag plus a new `case` in every consumer.

**Constraint on `overflow`:** it runs **under the subscriber's spinlock** (the disposition must be
applied atomically with the eviction/admission it decides). It must therefore be a pure disposition
decision — read `info`, set the disposition fields, return — and must **not** re-enter `event` on any
subscriber (no `fire`/`pull`/`unsubscribe`), or it self-deadlocks on the non-recursive spinlock. The
three built-ins are pure field-setters; a custom `overflow` must be too (think of it as a comparator,
not a callback). The `on_overflow` **reporter** carries no such restriction: it runs outside the lock
and may freely re-enter `event`.

`mel_event_lag(ev, sub)` returns the running count of items lost to overflow for that subscriber.

## API

```c
Mel_Event*    mel_event_create(alloc, item_size, ring_capacity, policy);
void          mel_event_destroy(ev);
Mel_Event_Sub mel_event_subscribe_push(ev, exec, cb, user);
Mel_Event_Sub mel_event_subscribe_pull(ev, user);
void          mel_event_unsubscribe(ev, sub);
void          mel_event_fire(ev, item);                 // from any thread
bool          mel_event_pull(ev, sub, item_out);        // pull subscribers
u32           mel_event_pull_pending(ev, sub);
u64           mel_event_lag(ev, sub);
u32           mel_event_subscriber_count(ev);
```

## Not channel (Wave 3)

`event` is 1→N broadcast: every live subscriber gets every item; there is no rendezvous, no select, no
producer backpressure that parks a sender. `channel` (a separate, later module) is M→N point-to-point
stream: an item goes to *one* consumer, `send`/`recv` park (fiber) or return a future (callback),
`select` waits on multiple ops, and unbuffered channels rendezvous sender→receiver directly. The
coordination trio splits by topology: **future** 1→1 one-shot, **event** 1→N broadcast, **channel**
M→N stream. Do not reach for `event` when you need point-to-point handoff or backpressure that stalls
the producer.

Deps: core, allocator, collection (slotmap), executor, thread (spinlock), log.
