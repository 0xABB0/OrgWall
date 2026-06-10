# routine

A fiber-pool scheduler for frame-cadenced routines (formerly the `coroutine` module; renamed —
it is not a coroutine primitive, that is `coro`). A routine is a stackful fiber the context
steps once per `mel_routine_update(ctx, dt)` tick; inside it, `mel_routine_wait(ctx, ms)` and
`mel_routine_yield(ctx)` / `mel_routine_yieldn(ctx, n)` suspend until the elapsed time or update
count passes. Stacks are pooled and recycled across invocations.

Namespace `<routine/...>`, prefix `mel_routine_`.

## Dependencies

`core`, `allocator` (explicit allocator at `mel_routine_create`), `fiber` (the stackful switch).
