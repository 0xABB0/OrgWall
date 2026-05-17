# Module Taxonomy and Control-Flow Primitives

## Problem Statement

The current `async` module is a blob. It contains fibers, coroutines, jobs, and signals — but only some of those are actually async. Fibers are not async: they are control-flow primitives that let a function suspend and resume on its own stack. Coroutines are not async either: they cooperatively yield. Signals and jobs are async (deferred execution), but lumping them with control-flow primitives makes the module name a lie. As the library grows, I want every module to mean exactly what it says, neither more nor less.

I also want both stackful and stackless coroutines as separate primitives. Stackful coroutines (built on fibers) are flexible but allocate per-instance stacks; stackless coroutines compile to state machines, allocate nothing on creation, and are perfect for generators that produce sequences cheaply — exactly what the music theory and similar generator-style code needs.

The names "stackful coroutine" and "stackless coroutine" are too verbose for module identifiers and clutter symbol names. Established CS vocabulary already names both precisely: a stackful coroutine is the original Knuth-meaning coroutine, and a stackless coroutine that produces a sequence of values by yielding is a generator. Both terms predate any specific language's use of them.

## Solution

Three parent module families with strict, non-overlapping scope:

- **async** — deferred-execution primitives only. Work that is dispatched but not necessarily run synchronously.
- **sync** — synchronization primitives. Things that block, wake, gate, or pass values between threads.
- **control** — control-flow primitives. Things that alter how control flows through code on a single thread, even though they may be used across threads in higher layers.

Coroutines split cleanly inside `control`:

- `control.coroutine` is the stackful coroutine. Built on `control.fiber`. Can yield from arbitrary nested call depth. Has its own stack; pays for the stack at creation time.
- `control.generator` is the stackless coroutine. A state machine with no stack of its own. Yields only from the coroutine's top-level body. Costs essentially nothing to create. Perfect for sequence producers.

A user who knows the established terms reads `control.coroutine` and `control.generator` and immediately understands which is which and what each costs.

## Implementation Decisions

The module tree settles into three families:

- **async**
  - `async.reactor` — event loop / dispatcher; new module (PRD 01)
  - `async.job` — worker-pool dispatch; currently exists, kept
  - `async.timer` — scheduled callbacks; deferred until needed

- **sync**
  - `sync.signal` — wake primitive; currently `async.signal`, moves here
  - `sync.counter` — fan-out/fan-in counter; currently inside `async.job`, extracted here
  - `sync.channel` — typed bounded MPSC/SPSC/MPMC channel; new module
  - `sync.mutex`, `sync.atomic`, etc. — added when first needed

- **control**
  - `control.fiber` — raw stack-swap primitive; currently `async.fiber`, moves here
  - `control.coroutine` — stackful coroutine API on top of fibers; currently `async.coroutine`, renamed and possibly split
  - `control.generator` — stackless coroutine via computed-goto state machine; new module

Dependency rules are hard:

- `control.fiber` depends on nothing but `core/` and platform asm.
- `control.coroutine` depends on `control.fiber`.
- `control.generator` depends on nothing but `core/`. No fiber, no allocation, no stack.
- `sync.*` modules depend on `core/` and C11 atomics. They do not depend on `control.*` or `async.*`.
- `async.reactor` depends on `sync.signal`, `sync.channel`, and platform pump.
- `async.job` depends on `control.fiber` (for fiber-backed jobs), `sync.counter`, `sync.signal`.

Channels live in `sync` because their identity is rendezvous-synchronization between threads. Non-blocking `try_send` / `try_recv` are conveniences, not the defining behavior.

Stackless coroutines use computed-goto (labels-as-values), since the target compiler is clang. The cost is non-portability to MSVC; the benefit is debuggability and zero `__LINE__` / `__COUNTER__` macro fragility. From a prototype-shaped sketch of the user-facing API:

```c
typedef struct MyGen {
    void* state;
    int x;
} MyGen;

MEL_GENERATOR(my_count_gen, MyGen, int /* yielded type */) {
    MEL_GEN_BEGIN(self);
    for (self->x = 0; self->x < 10; self->x++) {
        MEL_GEN_YIELD(self->x);
    }
    MEL_GEN_END;
}
```

The state struct is owned by the caller; locals that survive a yield live in the state struct. No allocation. The yield expands to a computed-goto save + suspend point.

Stackful coroutines retain their existing fiber-backed shape; the rename is purely cosmetic.

## Testing Decisions

Tests verify observable behavior — that a generator yields the expected sequence in order, that a coroutine resumes at the right point, that a channel's send blocks when full and unblocks on receive, that a counter wakes a waiter only when fully decremented. The platform asm of fiber stack-swap is not tested directly; it is exercised by the coroutine tests transitively.

Modules under test: `control.fiber`, `control.coroutine`, `control.generator`, `sync.signal`, `sync.counter`, `sync.channel`.

Prior art for control-flow tests: none in the current repo for generators or coroutines. The existing `async.fiber` tests if any (the directory shows `fiber.md` / `fiber.todo.md`) are notes, not tests. New tests follow standard patterns: deterministic input, observe yielded sequence, assert.

## Out of Scope

- Production-grade work-stealing scheduler for worker pools beyond what `async.job` already implements.
- Async/await syntax sugar at the language level. C does not have it; the generator macro vocabulary is the closest practical approximation.
- Cross-language interop for generators (e.g., exposing a generator as a Python iterator). Pure C primitives.
- Coroutine-style cancellation, structured concurrency, deadline propagation. Future PRD if needed.

## Further Notes

The renaming `async.fiber` → `control.fiber`, `async.coroutine` → `control.coroutine`, `async.signal` → `sync.signal` is mechanical but cascades through every dependent. The PRD's claim is the new placement; the rename mechanics are implementation work.

`async.coroutine` may be folded into `control.coroutine` 1:1, or split if the current code conflates stackful coroutine semantics with generator semantics. A pass-through check at migration time will determine this.
