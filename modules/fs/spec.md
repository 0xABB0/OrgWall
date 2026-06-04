# fs — spec

## Model

`Mel_Fs` owns a worker-thread pool and a slotmap of in-flight ops, bound to one `Mel_Reactor`.
Blocking syscalls run on a worker; completion is `mel_reactor_post`-ed to the loop, which resolves
the op's `Mel_Future` and runs its continuation on `deliver` (default: the reactor executor).

## Affinity

Submission, cancel, destroy, and future resolution all happen on the reactor's owner thread
(asserted in debug). Workers only touch the op's owned input copies and its result union; the loop
owns the slotmap and the future state machine. Submitted ops copy their path/data inputs so the
caller's buffers need not outlive the call.

## Cancellation

`mel_fs_cancel(fs, op)` is generation-checked through the slotmap. A not-yet-started op settles
`CANCELLED` immediately; an in-flight op is marked, runs to completion on the worker, then settles
`CANCELLED` when its completion reaches the loop (the syscall is not interruptible). Destroy marks
every pending op cancelled, drains the workers, then settles them.

## Status

`Mel_Fs_Status` = 2-bit severity (`OK`/`WARNED`/`ERROR`) + cause flags (`NOT_FOUND`, `EXISTS`,
`PERMISSION`, `NOT_A_DIRECTORY`, `IS_A_DIRECTORY`, `NOT_EMPTY`, `NO_SPACE`, `LOOP`, `NAME_TOO_LONG`,
`PARTIAL`, `CROSS_DEVICE`, `READ_ONLY`, `CANCELLED`, `UNAVAILABLE`). Results also carry the raw
`os_error`. `stat`/`exists` on a missing path are `OK` with `exists=false`, not an error.

## Atomicity

`copy`/`write_file` with `.atomic` write to a sibling temp then `rename`/`MoveFileEx` over the
target — a reader sees either the old file or the complete new one, never a torn write.

## Standard locations

`Mel_Fs_Folder` is a `u32` of `#define` constants (a closed OS-protocol set, kept off the reflection
path). `pref` derives its leaf from `mel_fs_pref_identity(.org, .app, .bundle_id)`; without it,
the platform default (bundle id / data-home root) is used. Folders a platform cannot serve return
`MEL_FS_ERROR | MEL_FS_UNAVAILABLE` — no silent fallback (MEL-CODE-007).

## Enums

No reflection enum is introduced. `Mel_Fs_Kind`, `Mel_Fs_Status`, the job-dispatch tags, and
`Mel_Fs_Folder` are all `u32` bit/constant typedefs.
