# dialog todo

- Reporting the chosen filter is host-complete on macos and win32; the linux portal
  returns `current_filter` (parse and map to the request index) and android SAF has no
  filter-index concept (the JNI bridge always flags `WARN_FILTER_IGNORED`).
- ios save-file currently exports an empty temporary file via the document picker; wire
  it to the caller's content once a save-flow contract is settled.
- android requires the host to forward `Activity.onActivityResult` to
  `MelodyDialog.onActivityResult` and to call `MelodyDialog.attach(activity)`; surface
  this in the app/runtime layer so consumers need not wire it by hand.
