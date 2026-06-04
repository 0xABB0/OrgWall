# process — spec

Subprocess spawn + lifecycle on the Melody async substrate (port/reactor/io/future).

## Goals

- Spawn from `argv` (+ optional env, env_clear, cwd).
- Per-stream stdio disposition: inherit / null / pipe(app-owned) / redirect-to-stream.
- Piped stdin/stdout/stderr exposed as async `Mel_Stream`s (port-backed); `merge_stderr` (2>&1).
- Queries: pid, running.
- Lifecycle: wait (blocking `wait_sync` + async future + non-blocking `poll`), kill (TERM/KILL), destroy.
- Read-to-completion convenience (`run`): collect stdout(+stderr) and exit code.
- Detached/background mode: own session, null stdio, no exit tracking.

## Non-goals (this wave)

- pty/tty allocation.
- Per-process resource limits beyond the win32 job object's kill-on-close.
- Signals beyond TERM/KILL.
- Socket (winsock) stdio.

## Contracts honored

- Status: `u32 Mel_Process_Status`, severity mask `0x3` + flag bits + static-inline predicates; no error strings. Mirrors io/port.
- Async ops return `Mel_Future*` resolved on the port/reactor loop, delivered via the explicit `.deliver` executor through `mel_future_then`.
- Generation-checked op handle (`Mel_Process_Op {index, generation}`) for `cancel_wait`; stale cancel is a safe `false`.
- Loop-thread affinity asserted on the async entries (`mel_reactor_is_owner`), inherited from port/reactor.
- `const Mel_Alloc*` passed in; never `mel_malloc` in allocator-taking code. `Mel_X_Opt` + `mel_x_create_opt` + variadic macro.
- No new enums: stdio disposition, kill signal, severity/flags are bitset constants / descriptor structs, never `enum`.
- Honest failure: every error surfaces in `status` (+ `os_error` on spawn); asserts on contract violation; wasm `available()==false`.

## Reuse (do not reimplement)

- byte streams: `<io/stream.h>` (`Mel_Stream`) — pipes are `Mel_Stream`s.
- proactor: `<port/port.h>` — pipe fd async read/write.
- readiness loop: `<reactor/reactor.h>` — pipe sources + the exit-reap timer source.
- futures: `<future/future.h>` — wait/run completion.

## Backends

- posix (macos/ios/linux/android): `posix_spawnp` + `posix_spawn_file_actions` (dup2 + chdir), `pipe2`/`pipe`+CLOEXEC, `waitpid(WNOHANG)` reaped on a reactor timer, `kill`.
- win32: `CreateProcessW` + overlapped named pipes (bridged to CRT fds) + job object (`KILL_ON_JOB_CLOSE`) for kill-tree.
- wasm: honest-absent (`available()==false`, spawn → `ERROR | UNAVAILABLE`).
