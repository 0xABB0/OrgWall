# debug

Assertions and stacktrace capture.

Depends on `core`, `allocator`, `platform`, `string`.

## Assertions

`mel_assert` / `mel_assert_release` / `mel_assert_paranoid` (and `_msg` variants)
evaluate their condition; on failure they call `mel__assert_report`, which captures
a stacktrace and invokes the installed `Mel_Assert_Handler` (or `mel_assert_default_handler`
when none is installed). The handler returns a `Mel_Assert_Response` bitset
(`RETRY | IGNORE_ONCE | IGNORE_FOREVER | ABORT | BREAK`); the macro acts on it:
`IGNORE_FOREVER` silences that call site, `BREAK` fires `MEL_BREAKPOINT()`, `ABORT`
calls `mel_abort()`, `RETRY` re-evaluates the condition.

Asserted expressions must be pure: a handler that returns `RETRY` re-evaluates the
expression, so side effects (e.g. `mel_assert(p = next())`) are repeated. The default
handler never returns `RETRY`.

`mel_assert_default_handler` writes the report to `stderr`, then aborts (and breaks
in debug builds). `mel_assert_fail(condition, location)` is the legacy single-shot
entry: it reports through `mel__assert_report` and calls `mel_abort()` when the
response carries `ABORT` — i.e. it terminates the process, unlike a bare diagnostic
print.

## Interactive resolution

`mel_assert_interactive_handler` resolves an assertion against the user where a
front end exists. It prefers a native modal dialog (`mel__assert_dialog`) when one
is available, falls back to a TTY prompt (`mel__assert_prompt`) when stdin/stderr
are terminals, and otherwise degrades to `mel_assert_default_handler` (stderr +
abort). `mel_assert_interactive_available` reports whether either path is live.

`mel_assert_dialog` (the by-value bridge in `debug.h`) raises the native dialog
directly when no handler is installed and a dialog backend is available.

Native dialog backends: macOS `NSAlert` (Abort/Retry/Ignore Once/Ignore Forever),
win32 `MessageBoxW` (Abort/Retry/Ignore).

## Native modal absence (iOS, Android, Linux, wasm)

iOS and Android expose no synchronously-blocking system alert: their alert APIs are
asynchronous, so an assertion — which must stop the failing thread until the user
chooses a disposition — cannot be served by a native modal there. This is a
deliberate, sanctioned degradation under MEL-ENGINE-VII (age forward, degrade
honestly), not a stub awaiting completion. Forcing a synchronous wait atop an async
alert would be a kludge and is forbidden.

The `nodialog` backend (shared by iOS, Android, Linux, wasm) makes the absence
explicit: `mel__assert_dialog_available()` returns `false`, so the interactive
handler never selects the dialog path; it takes the TTY prompt where stdin/stderr
are terminals (Linux), else degrades to `mel_assert_default_handler` (stderr +
abort). Should `mel__assert_dialog` be invoked despite the unavailability contract,
it emits a loud one-time stderr notice naming the platform and degrades to the
stderr report rather than silently pretending to have shown a modal (MEL-ENGINE-VIII,
MEL-CODE-007). The `nodialog` translation unit `#error`s at compile time if selected
on a platform outside this set, refusing to default silently.
