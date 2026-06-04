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
win32 `MessageBoxW` (Abort/Retry/Ignore). Platforms with no self-contained blocking
modal (iOS, Android, Linux, wasm) report the dialog unavailable and rely on the TTY
prompt or stderr fallback.
