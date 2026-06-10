# process

Subprocess spawn and lifecycle on the async substrate. Launch a child from an
`argv` (optional env, cwd), redirect each stdio stream, drive the pipes as async
byte streams, query and wait for exit, kill, and collect run-to-completion output.

deps: core, allocator, collection, executor, future, vat, port, io, time, log.
namespace: `<process/...>`, prefix `mel_process_`.

## Surface

    Mel_Process_Spawn_Result mel_process_spawn(.argv, .argc, .env?, .env_count?,
                                               .env_clear?, .cwd?,
                                               .stdin_cfg?, .stdout_cfg?, .stderr_cfg?,
                                               .merge_stderr?, .detached?,
                                               .vat?, .alloc?);
    bool        mel_process_available(void);
    void        mel_process_destroy(Mel_Process*);

    i64         mel_process_pid(const Mel_Process*);
    bool        mel_process_running(Mel_Process*);
    bool        mel_process_detached(const Mel_Process*);

    Mel_Stream* mel_process_stdin/stdout/stderr(Mel_Process*);

    Mel_Future* mel_process_wait(p, .deliver?, .out_op?);
    Mel_Process_Exit mel_process_wait_sync(Mel_Process*);
    bool        mel_process_poll(Mel_Process*, Mel_Process_Exit*);
    bool        mel_process_cancel_wait(Mel_Process*, Mel_Process_Op);

    Mel_Process_Status mel_process_kill(p, .signal = TERM | KILL);

    Mel_Future* mel_process_run(.argv, .argc, .stdin_data?, .stdin_len?,
                                .merge_stderr?, .vat, .deliver?, ...);

## Stdio disposition

Each stream is configured by a `Mel_Process_Stdio` descriptor, not an enum: a
`disposition` bit value (`INHERIT` / `NULL` / `PIPE` / `REDIRECT`) plus, for
`REDIRECT`, the target `Mel_Stream*` (its native fd is dup'd onto the child's
stream). `PIPE` exposes the stream as a `Mel_Stream` (`mel_process_stdin/stdout/
stderr`), backed by `port` for async byte transfer — it requires a vat.
`merge_stderr` ties the child's stderr to its stdout (`2>&1`).

## Status

`Mel_Process_Status` is severity (`OK`/`WARNED`/`ERROR`) plus a bitset
(`EXITED`, `SIGNALLED`, `KILLED`, `DETACHED`, `SPAWN_FAILED`, `NOT_FOUND`,
`PERMISSION`, `PIPE_FAILED`, `CANCELLED`, `UNAVAILABLE`, …) — never an enum.
`Mel_Process_Exit` carries `{exit_code, term_signal, status}`. Every failure
surfaces in `status` (+ `os_error` on spawn); nothing fails silently
(MEL-ENGINE-VIII).

## Wait & op handle

`mel_process_wait` returns a `Mel_Future` resolving with the `Mel_Process_Exit`
when the child is reaped, delivered on the explicit `.deliver` executor through
the future → `mel_future_then` waist. `.out_op` yields a generation-checked
`Mel_Process_Op`; `mel_process_cancel_wait` of a stale op is a safe no-op
returning `false`. `mel_process_wait_sync` blocks the calling thread (`waitpid`/
`WaitForSingleObject`); `mel_process_poll` is a non-blocking reap check.

## Read-to-completion

`mel_process_run` is the convenience: spawn with stdout (and stderr unless
merged) piped, optionally feed `stdin_data`, drain both pipes and the exit code,
resolve a future with `Mel_Process_Output` (`stdout_data/len`, `stderr_data/len`,
`exit_code`, `term_signal`, `status`). The process is destroyed when the future
resolves; the caller frees the buffers with `mel_process_run_future_release`. It
requires a vat and runs entirely on the loop thread; the run holds a
`mel_vat_retain` until its future resolves.

## Detached / background

`.detached = true` spawns the child in its own session (`POSIX_SPAWN_SETSID` /
`DETACHED_PROCESS`) with stdio routed to the null device and no exit tracking:
`mel_process_wait`/`wait_sync` resolve `ERROR | DETACHED`, and detached + piped
stdio is rejected at spawn (there would be no owner to drain it). `destroy`
neither kills nor reaps a detached child — it outlives the handle.

## Thread affinity (loop-thread-only)

The async entries (`mel_process_wait`, `mel_process_cancel_wait`, the pipe
stream reads/writes, `mel_process_run`) are loop-thread-only and assert
`mel_vat_is_owner(vat)`, inherited from `port`/`vat`. `wait_sync`, `poll`,
`kill`, `pid`, `running` and `destroy` are callable from the owning thread of a
vatless process or marshalled onto the loop otherwise.

## Platform selection

`build.c` selects exactly one backend translation unit per platform:

- **macOS / iOS / Linux / Android** → `src/posix/process_backend.c` —
  `posix_spawnp` with `posix_spawn_file_actions` for stdio dup2 and cwd
  (`addchdir`/`addchdir_np`), `pipe2(O_CLOEXEC)` (Linux/Android) or
  `pipe`+`FD_CLOEXEC` pipes, exit reaped with `waitpid(WNOHANG)` driven by a
  deadline-only vat source on the loop, `kill(SIGTERM/SIGKILL)`.
- **win32** → `src/win32/process_backend.c` — `CreateProcessW` with a UTF-16
  command line (MSVC argv-quoting) and environment block, overlapped named
  pipes bridged to CRT fds (`_open_osfhandle`) for the `port` overlapped engine,
  a job object with `KILL_ON_JOB_CLOSE` for kill-tree (`TerminateJobObject`).
- **wasm** → `src/wasm/process_backend.c` — `available()==false`; spawn resolves
  `ERROR | UNAVAILABLE`. A browser/WASI sandbox has no `fork`/`exec`; faking it
  would be a silent lie (MEL-ENGINE-VIII).

## Exit detection: why a deadline poll, not SIGCHLD

The posix backend reaps with `waitpid(WNOHANG)` on a deadline-only vat source
(2 ms cadence) rather than a global `SIGCHLD` handler + self-pipe. A process-wide
signal handler is global mutable state that would collide across vats and with a
host app's own `SIGCHLD` use; the source is vat-local, loop-thread, and
zero-cost when no wait is pending (opened only while a wait is outstanding, and
it `mel_vat_retain`s the vat for its lifetime). A kqueue `EVFILT_PROC` / Linux
`pidfd` wakeable would be the zero-latency ideal and slots behind the same seam
once the waiter accepts non-fd filters.

## Tests

`process-spawn` drives real host binaries: `true`/`false` exit codes,
missing-binary `NOT_FOUND`, `argv` requirement, `kill` of a sleeper surfacing
`SIGNALLED`, detached status + piped-detached rejection, and — on a vat opened
on the test thread (io waiter + fair driver + deadline-0 idle source) — `run`
collecting `printf` stdout with exit 0, feeding stdin through `cat`, and passing
an env var through `sh -c`. Runs on macOS here; the posix backend
compiles/links identically on Linux/Android (same `posix_spawn` + `waitpid` +
vat-source model). win32 is built/run on win-pilot. wasm has no surface to test
(`available()==false`).
