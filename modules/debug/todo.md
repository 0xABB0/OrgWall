# debug — todo

## Sanctioned degradations (not bugs, not stubs)

- **No native assert modal on iOS/Android/Linux/wasm.** A synchronously-blocking
  system alert does not exist on iOS or Android (their alert APIs are async-only); an
  assertion must block the failing thread, so no native modal can serve it there.
  This is a deliberate honest-absence under MEL-ENGINE-VII, served by the `nodialog`
  backend: `mel__assert_dialog_available()` reports `false`, and resolution degrades to
  the TTY prompt (Linux) or the stderr report. Do NOT "implement" a native assert modal
  on these platforms by wrapping an async alert in a spun wait — that is a forbidden
  kludge. If a future platform gains a genuine synchronous blocking modal, give it its
  own `assert_dialog` backend and drop it from the `nodialog` source set.

## Compiler-unverified

- **win32 `MessageBoxW` dialog** (`src/windows/assert_dialog.c`) is unverified on this
  host; it requires a build on win-pilot. Accepted posture, not a defect.

## Tolerated carve-outs

- **`char buf[4096]`** in `src/windows/assert_dialog.c` formats the report on the
  already-failed assertion path. A fixed snapshot buffer here is a tolerated carve-out
  (MEL-CODE-002): the path is terminal and pre-abort, the text is truncated safely with
  a NUL terminator, and threading an allocator-backed builder through a process that is
  about to die buys nothing.
