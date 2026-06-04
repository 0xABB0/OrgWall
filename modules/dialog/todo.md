# dialog todo

- Chosen-filter reporting: win32 (`GetFileTypeIndex`) and the GTK fallback (filter-label
  match) report it; macos `NSOpenPanel` and the linux portal do not. The portal response
  carries `current_filter` — parse and map it to the request index to reach win32/gtk
  parity. Android SAF has no filter-index concept (the JNI bridge always flags
  `WARN_FILTER_IGNORED`).
- The portal request now sends `default_path` via the `current_folder` ('ay') option, at
  parity with the GTK fallback's `gtk_file_chooser_set_current_folder`.
- Web save-file is not serviceable yet: it resolves `WARNED | WARN_SAVE_UNSUPPORTED`
  rather than fabricating an empty OK. Implement `window.showSaveFilePicker` once the
  save-content contract lands.
- ios save-file currently exports an empty temporary file via the document picker; wire
  it to the caller's content once a save-flow contract is settled.
- android requires the host to forward `Activity.onActivityResult` to
  `MelodyDialog.onActivityResult` and to call `MelodyDialog.attach(activity)`; surface
  this in the app/runtime layer so consumers need not wire it by hand.

## Thread model & loop-thread affinity

The global dialog slotmap (`g.jobs`) is not internally synchronized. The core (`src/dialog.c`)
binds to the loop thread at `mel_dialog_init` when a reactor is supplied: it asserts the caller
is the reactor owner, then captures that thread id (`mel_thread_current_id`). Every core entry
point that mutates the slotmap on the loop thread — `job_new` (slotmap insert, reached by the
four launch ops), `job_storage_free` (slotmap remove, reached by `mel_dialog_future_free` and
`mel_dialog_shutdown`), and `mel_dialog_shutdown` itself (slotmap free) — asserts that affinity
against the captured id. The check is against the captured id, never a re-dereferenced reactor
pointer, so it stays valid after the reactor is torn down (the spawn frees the reactor on quit,
yet teardown still runs on the loop thread). When no reactor is bound (`reactor == NULL`,
synchronous host usage) there is no loop to be affine to and the assert is intentionally inert —
this is an explicit absence of a binding, not a silent default.

Deliberate omission (macOS / iOS): both backends hop to `dispatch_get_main_queue` in
`mel_dialog__plat_run`, then call `mel_dialog__job_from_token` (a slotmap *read*) and
`mel_dialog_job_emit_path` / `mel_dialog_job_resolve` from the main-queue block. The main queue
thread is not, in general, the reactor loop thread, so an affinity assert on those reads would
fire spuriously and is therefore not placed there. The resulting hazard is honest and bounded:
the main-queue read of `g.jobs` can race a concurrent loop-thread mutation (`job_new` /
`job_storage_free`) since the slotmap is unsynchronized. The token-from-slotmap lookup tolerates
a freed job (it resolves to NULL and the block returns), but a concurrent insert/remove
reallocating the slotmap storage during the read is not guarded. Closing this requires either
posting the completion back onto the reactor's executor before touching the slotmap (so all
slotmap access is loop-thread-only, matching the win32/linux/android backends which run their
completion on the calling/loop thread), or giving the slotmap its own lock. The former is
preferred and is the intended follow-up; until then macOS/iOS callers must not launch or free
dialogs from a thread other than the one driving the reactor while a dialog is in flight.
