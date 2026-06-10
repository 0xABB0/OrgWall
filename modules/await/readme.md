# await

The suspension waist: one verb, "await", whose mechanism — fiber park, coro yield, loop turn —
is the callee's affair. Thin glue over existing pieces; this module owns no scheduler, no queue,
no thread. Namespace `<await/...>`, prefix `mel_await_`.

The unifying shape (validated by the external `test-suspension` probe): a scheduler-facing
parkable — resume → done/ready/blocked — under which stackful fibers and stackless coros park on
the *same* waitlists and wake each other. Melody's waist already exists (`Mel_Task` +
`Mel_Executor` + `Mel_Waker`; channel ops carry a waker; futures deliver a `Mel_Task`), so both
directions reduce to it:

## Coro driven as a task

`mel_await_coro_start(c, desc)` drives a stackless frame as a `Mel_Task` on `desc.exec`. The
resume callback (`bool resume(void* frame, Mel_Await_Step* out)`, the shape of a generated
`<name>__resume` from the `coro` module) returns `false` when done, or `true` with a step naming
what it waits on:

- `.future` — the task is registered with `mel_future_then(f, task, exec)`;
- `.channel` + `.slot` + `.is_send` — the op rides `mel_channel_send_future`/`recv_future`
  (allocates one wait node from `desc.alloc`, freed on settle), then `then` as above;
- `.after_ns` — armed on `desc.timers` (`Mel_Vat_Timers`; requires a vat binding);
- `.reschedule` — resubmitted to `desc.exec` (a cooperative yield);
- `.status_out` — where the adapter writes the awaited future's status before the next resume.

On completion the adapter resolves `desc.done` with the frame pointer (if set), calls
`desc.on_done(user)` (if set), and releases the vat. If `desc.vat` is set, the coro holds a
`mel_vat_retain` for its whole live span — the fs op pattern — so `mel_vat_run` does not return
while a suspended coro is in flight; `desc.exec` must then be `mel_vat_executor(desc.vat)` and
`start` must be called on the vat's thread.

## Fiber awaits a future

`mel_await_future(f)` parks the calling fiber on a `Mel_Signal` counter until `f` resolves (the
resolve path sets the counter through a then-task on the inline executor), then returns
`mel_future_value(f)`. Callable from job-worker fibers (the channel module's fiber flavor is the
same pattern). It consumes the future's single continuation slot.

Both directions compose across a channel: a coro task can send/recv against a channel while a
fiber recv/sends the other side — see `test/test_await.c` for the probe's bridge scenario
rebuilt on melody primitives.

## Dependencies

`core`, `allocator`, `collection`, `executor`, `future`, `channel`, `signal`, `vat`, `time`.
The `job` module is a test-only dependency (fibers to park).

## Owed

- Cancellation propagation: cancelling `desc.done` should retract the coro's pending wait
  (channel op retract, timer cancel) and stop resuming; today a cancelled awaited future just
  surfaces through `.status_out`.
- Vat-affine coro spawn sugar: a one-call `mel_await_coro_spawn(vat, frame, resume, ...)` that
  allocates the adapter from the vat's allocator and owns its lifetime.
- A blocking-thread flavor (`mel_await_future_blocking`) for non-fiber threads, if a consumer
  appears.
