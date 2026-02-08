# Mainloop / Multi-Application Architecture

Status: **Early Discussion**

## Problem

The engine needs to support multiple independent "applications" running inside a single process. Examples:

- Game running at 144fps on one monitor, editor running at 30fps on another
- Multiple windows at different refresh rates on different monitors
- Demo executable spawning multiple sub-applications
- A server that has no rendering at all but still ticks

The current architecture has a single main/melody/game indirection that doesn't support any of this.

## Constraints

- Must have **zero dependencies** on engine systems (no graphics, no ECS, no assets)
- Loops must be able to run at independent tick rates
- Must handle OS events (which arrive on a single process-wide queue via SDL)
- Loops must be able to communicate with each other
- Must work single-threaded (cooperative) first; multi-threaded is a future option, not a requirement
- Must support loops that are purely reactive (only events, no tick) and purely periodic (only tick, no events)

## Open Questions

1. **Event dispatcher — separate module or part of the loop system?**
   SDL gives us one event queue for the whole process. Something needs to drain it and route events to the right loop. Is this a separate "event router" module, or is it built into the loop system?

2. **Who owns the SDL event pump?**
   `SDL_PumpEvents` / `SDL_PollEvent` must be called from the main thread. If loops can run on different threads, only one of them (or a dedicated dispatcher) can pump. How does this work in single-threaded cooperative mode vs potential future multi-threaded mode?

3. **Inbox data structure**
   Each loop needs an inbox for routed events and inter-loop messages. Ring buffer? Dynamic array? What allocator?

4. **Loop identity and addressing**
   How do you refer to a loop? Handle? Name? Index? Needed for event routing ("send this to loop X") and inter-loop messaging.

5. **Tick rate control**
   Fixed timestep? Variable with cap? VSync-driven? Per-loop configurable? Some loops (editor) want to be lazy and only tick when events arrive. Others (game) want fixed 60/144hz.

6. **Loop lifecycle**
   Can loops be created/destroyed at runtime? What happens to messages in a dead loop's inbox? What about events targeting a window owned by a dead loop?

7. **The "root" question**
   Is there a root/master loop that owns the process lifetime? Or is the process alive as long as any loop is alive?

## Discussion

### 2025-02-08 — Gabbo & Mel

**Starting point:** The current main/melody/game indirection is ugly and doesn't model the actual architecture we want.

**Key realization:** A loop is fundamentally just "something that ticks" — it doesn't know about rendering, windows, or any engine system. The code that *runs inside* the tick knows about those things, but the loop itself is just a scheduler.

**But it's not just ticking.** OS events arrive asynchronously. A loop needs to both process events relevant to it AND tick its logic. The pattern per iteration is:
1. Drain your event inbox
2. Tick your logic
3. (Optionally) sleep until next tick target

**Event routing is the hard part.** SDL gives one queue per process. A dispatcher sits between the OS queue and the individual loops, routing events by window_id or type.

**Inter-loop communication:** Message queues. Each loop has an inbox. Post to another loop's inbox. Drain at top of tick. No shared mutable state on the hot path.

**Rough picture:**
```
           Event Dispatcher
    (drains SDL queue, routes events)
         |              |
         v              v
     Loop A          Loop B
    [inbox]         [inbox]
    tick_fn()       tick_fn()
```

**Unresolved:** Is the dispatcher a separate module or part of the loop system? This shapes whether it's one module or two.

**Gabbo's concern:** This feature might be too big to design all at once. Needs to be broken down.

## Decision Log

(Nothing decided yet)
