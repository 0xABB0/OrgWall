# shell — spec

## Surface

```c
void mel_shell_init(const Mel_Alloc* alloc, Mel_Reactor* reactor);
void mel_shell_shutdown(void);
bool mel_shell_available(void);

Mel_Future* mel_shell_open_url(str8 url, ...);       // .deliver, .alloc, .out_op
Mel_Future* mel_shell_reveal_path(str8 path, ...);

bool             mel_shell_cancel(Mel_Shell_Op op);
Mel_Shell_Status mel_shell_future_status(const Mel_Future* f);
void             mel_shell_future_free(Mel_Future* f);
void*            mel_shell_native(void);
```

## Status

`Mel_Shell_Status` = severity (`OK`/`WARNED`/`ERROR`, mask `0x3`) OR a result/warning bitset. No
enums, no strings. Predicates are static-inline (`mel_shell_failed/warned/ok/cancelled`).

- Result bits: `CANCELLED`, `NO_BACKEND`, `NO_HANDLER`, `DENIED`, `NOT_FOUND`, `BAD_TARGET`,
  `SPAWN_FAIL`.
- Warning bits: `LAUNCH_UNVERIFIED`, `SCHEME_UNTRUSTED`, `REVEAL_DEGRADED`.

The future stores only severity; the full bitset is recovered through `mel_shell_future_status`,
which maps `MEL_FUTURE_CANCELLED` to `ERROR | CANCELLED`.

## Async model

Each call allocates a job (slotmap-tracked, generational handle), inits a `Mel_Future`, copies the
target with the job's allocator, then dispatches to the platform backend. A backend resolves
synchronously (Win32, Linux, Android, Web) or defers and resolves later by recovering the job from
its packed token (macOS/iOS completion handlers). `.out_op` reports the generational handle for
`mel_shell_cancel`. Loop-thread affinity is the future's: deferred resolution runs on the platform's
loop (the main run loop for the Apple callbacks).

## Failure (MEL-ENGINE-VIII)

- Empty target → `ERROR | BAD_TARGET`, no backend call.
- `mel_shell_available()` false → `ERROR | NO_BACKEND`, logged.
- `reveal_path` on a missing path → `ERROR | NOT_FOUND` (where the backend can stat).
- No handler / launch refused → `ERROR | NO_HANDLER` (or `DENIED` on Web pop-up block).
- `reveal_path` that can only open the parent folder → `WARNED | REVEAL_DEGRADED`.

`shutdown` cancels every unresolved job; jobs with a continuation are freed by the future's
continuation path, the rest by shutdown directly.

## Backends

`open_url` then `reveal_path` per platform:

- macOS — `NSWorkspace` async; `activateFileViewerSelectingURLs:`.
- iOS — `UIApplication` async; reveal honest-absent (`NO_HANDLER`).
- Win32 — `ShellExecuteW` `open` verb; `explorer.exe /select,`.
- Linux — `xdg-open`; `FileManager1.ShowItems`, degrading to `xdg-open` on the parent folder.
- Android — `Intent.ACTION_VIEW`; `ACTION_VIEW resource/folder` (degraded).
- Web — `window.open`; reveal honest-absent (`NO_HANDLER`).
