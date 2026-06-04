# app — todo

## WILL_TERMINATE termination-ordering race (linux / win32 / android)

On these three backends the host's terminate signal is observed on a foreign context
and handed to the loop asynchronously, never emitted inline:

- linux  — `SIGTERM`/`SIGINT` handler writes a self-pipe; a reactor source reads the
  pipe on the loop thread and then calls `mel_app__emit(WILL_TERMINATE)`.
- win32  — `console_handler` runs on a foreign OS thread and `mel_reactor_post`s
  `post_will_terminate` onto the loop.
- android — `nativeOnDestroy` (JNI thread) `mel_reactor_post`s `emit_on_loop` with
  the `WILL_TERMINATE` phase.

The handoff is honest (async-signal-safe / cross-thread correct), but it introduces an
ordering race against teardown: the posted/pipe-pending `WILL_TERMINATE` is only emitted
when the loop next turns. If the reactor stops or `mel_app_quit` runs before that turn —
the OS kills the process, or the program's own shutdown wins the race — the queued phase
is dropped and `WILL_TERMINATE` is never delivered to subscribers. The macos and ios
backends do not share this race: their notifications fire synchronously on the main/loop
thread via `NSNotificationCenter` / `UIApplication` observers.

This is an inherent property of cooperatively draining an asynchronous terminate signal
on a loop that may already be tearing down; it is disclosed here rather than masked. A
fix would require either a synchronous last-chance drain on the terminate path or a
host-blocking acknowledgement, both of which trade their own hazards (blocking the OS
shutdown deadline, reentrancy during teardown) and are deferred until a real consumer
needs guaranteed terminate delivery.
