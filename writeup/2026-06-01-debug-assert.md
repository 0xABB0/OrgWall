# Fix `mel_assert`

## Work done

`modules/debug/include/debug/assert.h` defined `mel_assert(...)` as empty in *both*
branches of `#if MEL_ASSERT_ENABLED` — the macro never asserted. Consumers
(`paint`) called it as a no-op (recorded in `modules/paint/todo.md`).

- Enabled branch now evaluates the condition; on failure it calls
  `mel_assert_fail` with the stringized condition and `file:line`, then fires
  `MEL_BREAKPOINT()` at the call site so the debugger stops in the caller's frame
  (MEL-ENGINE-VIII: loud, immediate, with a stack trace).
- Disabled branch expands to `((void)sizeof(__VA_ARGS__))` so condition symbols
  stay referenced (no unused-variable warnings) without evaluation.
- `mel_assert_fail` (new `modules/debug/src/assert.c`) captures a stacktrace via
  the module's existing `mel_stacktrace_*`, formats it, writes the report to
  `stderr`, and frees. It lives in its **own** translation unit — see Kludges.
- The `mel_assert_fail` declaration, its includes, and its definition are all
  guarded by `#if MEL_ASSERT_ENABLED` — they are referenced only by the enabled
  macro, so in release the header imposes no includes and `assert.c` compiles to
  an empty TU (MEL-ENGINE-III: no cost the user didn't ask for).
- Adjacent pre-existing bug fixed: `mel_stacktrace_free` dereferenced
  `frame.function_name.data` unconditionally, but that member is compiled out
  when `MEL_STACKTRACE_HAS_FUNCTION_NAMES` is 0 (release) — so the debug module
  never built in `--release`. Guarded the loop with the same macro
  `mel_stacktrace_format` already uses.

Verified: debug + `paint` compile (debug & release); the throwaway probe links
*without* `-dead_strip` and at runtime prints condition + location + trace and
terminates with SIGTRAP (exit 133).

## Kludges

- The module's designed assert path is `mel_assert_dialog` →
  `mel__native_assert_dialog`. The latter has **no implementation anywhere** and
  the `platform` module exposes no message-box primitive, so routing the macro
  through it would leave every debug-build consumer with an undefined symbol. I
  did not wire the macro to that path; `mel_assert` is self-contained instead.
  The dialog code remains dormant (declared, defined, uncalled) — an unfinished
  GUI assert path, not introduced here but left standing.
- `mel_assert_fail` was deliberately split into `src/assert.c` rather than added
  to `debug.c`, because `debug.c` also defines `mel_assert_dialog`. Same-TU
  object granularity means linking the assert helper would drag the dialog and
  its undefined `mel__native_assert_dialog`; nob's `-dead_strip` (emit.c:262)
  masks this on macOS, but other linkers (GNU ld without `--gc-sections`) would
  fail. Isolating the helper makes the assert path linker-agnostic.
- Stacktrace skip count is fixed at 3 (platform-capture → capture → assert_fail
  → caller). Correct for separate-TU debug builds; LTO could collapse frames and
  shift it. Capture depth is a fixed 64.

## CLAUDE.md suggestions

None.

## Suggestions

- Either implement `mel__native_assert_dialog` per platform (NSAlert / MessageBox
  / Android dialog) and route `mel_assert` through `mel_assert_dialog` for GUI
  apps, or delete the dormant dialog path. As-is it is a latent undefined-symbol
  landmine surviving only on `-dead_strip`.
- `mel_assert_dialog` in `debug.c` ignores its `bool condition` parameter (it
  always formats and shows). If that path is revived, it must early-out when the
  condition holds.
- `mel_assert_fail` writes to `stderr` directly; once the `log` module's
  dependency direction is settled, routing it through a log sink would unify
  diagnostic output.
