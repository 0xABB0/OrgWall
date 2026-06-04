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
