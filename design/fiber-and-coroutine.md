# Fibers and Coroutines — Concepts

## Why this document exists

The two words are routinely conflated, and Melody's current layout reflects that confusion (`modules/async/fiber/` + `modules/async/coroutine/`). This document settles the vocabulary, picks an implementation strategy for Melody, and records why.

## Definitions

**Coroutine** — a *language-level* concept about control flow. A function that can suspend itself at explicit points (`yield`, `await`) and be resumed later. Coroutines say nothing about *who* runs them or how stacks are managed.

**Fiber** — a *runtime-level* concept. A thread of execution with its own stack, scheduled cooperatively in user space (no kernel involvement). A fiber switches when it explicitly yields; a scheduler picks the next one to run.

These describe different axes. A coroutine describes the *suspension semantics* of a function. A fiber describes a *scheduled execution context with its own stack*. The overlap is why people conflate them.

## Stackful vs stackless

The distinction that actually matters in practice:

**Stackful coroutines** (Lua, Ruby Fibers, Boost.Coroutine, Melody's current `async.coroutine`) — each coroutine owns a full stack. Can suspend from any nested call depth. Effectively *are* fibers with coroutine-flavored suspension semantics.

**Stackless coroutines** (C++20, Rust `async`, Python `async def`, Kotlin) — the compiler transforms the function into a state machine. No separate stack. Can only suspend at explicit suspension points *in the coroutine body itself* — never inside a helper function.

Cost/capability tradeoff:
- Stackful: heavy (one stack per coroutine, 8KB–1MB), flexible (suspend anywhere in the call chain).
- Stackless: cheap (one heap frame per coroutine, kilobytes or elidable), restrictive (cannot suspend through helper calls — function coloring is viral).

## How stackless coroutines work in C

C has no language support, so the technique is hand-rolled. The classic version is Simon Tatham's coroutines / Protothreads: a `switch` whose case labels are `__LINE__` values, with a per-coroutine "frame" struct holding the resume point plus any locals that must survive a yield.

```c
typedef struct {
    int state;
    int i;
    int sum;
} CountFrame;

#define CORO_BEGIN(f)   switch ((f)->state) { case 0:
#define CORO_YIELD(f,v) do { (f)->state = __LINE__; return (v); case __LINE__:; } while(0)
#define CORO_END(f)     } (f)->state = -1; return 0

int count_step(CountFrame* f) {
    CORO_BEGIN(f);
    f->sum = 0;
    for (f->i = 0; f->i < 5; f->i++) {
        f->sum += f->i;
        CORO_YIELD(f, f->sum);
    }
    CORO_END(f);
}
```

Two non-obvious mechanics:

1. **Locals must live in the frame struct, not on the stack.** If `i` and `sum` were real locals, they would be reborn on every call. That's why stackless coroutines are cheap (one struct allocation) but restrictive (no taking addresses of "locals" naively, no yielding from arbitrary nested calls — only from the coroutine body).
2. **Switch-fallthrough across case labels works** because C `switch` is essentially a computed goto. `CORO_YIELD` returns; the next entry jumps right past the return.

Limitations compared to stackful:
- Cannot yield from a helper function (the helper has no state machine).
- Cannot have `for (int x = ...)` cross a yield safely without lifting `x` into the frame.
- Cannot use `switch` inside the coroutine without nesting.

## What C++20 does

C++20 coroutines are exactly this transformation, done by the compiler. Writing `co_yield` / `co_await` / `co_return` marks the function as a coroutine; the compiler generates a hidden frame struct holding all surviving locals + the resume point, heap-allocates it (elidable via HALO optimization), and rewrites the body into the state-machine form. The user-supplied `promise_type` gets called at each suspension point and controls scheduling, allocation, and awaiter behavior.

Mechanically identical to the C macro version, just hidden behind language syntax.

## Decision for Melody — stackful

Melody's user-facing concurrency unit is **stackful**, built on `runtime/fiber`. Reasons:

1. **No function coloring.** A fiber can call any helper function, and that helper can call `await(...)` or `mel_coro_wait(...)`. Stackless coroutines force every async-touching function up the call graph to be marked as such — viral, and effectively unworkable in C-with-macros for general-purpose code.
2. **Async-first is the goal.** Async-first means I/O calls suspend the calling fiber. With stackful, the call site looks synchronous and the await happens anywhere. This is the single biggest enabler of an ergonomic async-first C library.
3. **Cost is manageable.** Default stack sizes can be small (8–16KB) with guard pages; `allocator.vmem` is already the substrate. Thousands of fibers fit fine for typical workloads. The cost only bites at hundreds-of-thousands-of-connections scale, which Melody is not targeting.

Stackless coroutines are not built. They're a *different* primitive, not a refinement — and one Melody doesn't need.

## Where these belong in the module layout

Melody's current layout puts both under `modules/async/`. This is a category error: fibers are not async, they are a control-flow primitive. Correct layout:

- `modules/runtime/fiber/` — the context-switch primitive (was `async/fiber`)
- `modules/runtime/coroutine/` — cooperative tasks layered on fibers (was `async/coroutine`)

The "async" naming should be reserved for the layer that actually integrates with the I/O reactor — see `async-first.md`.

See also: `runtime.md`, `async-first.md`.
