# dialog spec

## Surface

- Four launch ops, each `_opt` + variadic macro, returning `Mel_Future*`:
  - `mel_dialog_open_file`   — single file.
  - `mel_dialog_open_files`  — multiple files.
  - `mel_dialog_save_file`   — destination file (+ default name).
  - `mel_dialog_open_folder` — directory.
- Each opt carries: optional modal `parent` (`Mel_Window`), `title`, `default_path`,
  filters (`Mel_Dialog_Filter[]`: label + extension patterns), `reactor`, `deliver`
  executor, `alloc`. Save adds `default_name`.
- Result: `Mel_Dialog_Selection` — `paths` (count), `chosen_filter`, `status`.
  Resolved on the caller's executor; cancel is `MEL_DIALOG_OK | MEL_DIALOG_CANCELLED`
  with zero paths, never an error.

## Status

`u32` bitset. Severity mask `0x3` (OK / WARNED / ERROR), then condition flags
(`CANCELLED`, `NO_BACKEND`, `DENIED`, `BAD_PARENT`, `UNAVAILABLE`) and warning flags
(`FILTER_IGNORED`, `MULTI_UNSUPPORTED`, `DEFAULT_PATH_IGNORED`, `PARENT_IGNORED`).
Static-inline predicates. No error strings (MEL-ENGINE-VIII).

## Core / backend split

The core (`src/dialog.c`) owns the job lifecycle: it deep-copies the request (title,
paths, filters), registers the job in a generational slotmap keyed by a packed token,
and resolves the future from emitted paths. Exactly one platform translation unit
implements `mel_dialog__plat_run`, reading the request through job accessors and
emitting paths + chosen filter before calling `mel_dialog_job_resolve`. A backend that
completes asynchronously stashes the token and resolves the job later from its native
completion callback; the token resolves to NULL once the job is freed, so a torn-down
job is safely ignored (MEL-ENGINE-VIII).

## Request model (no enums)

Mode is a request bitset on the job: `OPEN_FILE`, `MULTI`, `SAVE_FILE`, `OPEN_DIR`.
A backend honors the bits it supports and warns on the rest (MEL-CODE-001).

## Filters

`Mel_Dialog_Filter` is label + a list of extension patterns (`"*.png"`, `"png"`, or
`"*.*"`). Each backend lowers patterns to its native representation (`UTType`,
`COMDLG_FILTERSPEC`, portal `a(us)` globs, GTK patterns, web `accept`). Patterns it
cannot express raise `MEL_DIALOG_WARN_FILTER_IGNORED`.
