# dialog

Native file and folder pickers. Open-file (single and multi), save-file, and
open-folder, each launched asynchronously and delivering the user's selection as a
list of paths plus the index of the filter the user chose.

## Why it exists

Almost every desktop and mobile application needs the host's native picker: it is the
only UI that respects the platform's file conventions, sandbox grants, and recent-files
history. Reimplementing a picker would violate MEL-ENGINE-VII (build on what the
platform offers). `dialog` wraps each platform's native picker behind one async surface
so the call site stays a single line (MEL-ENGINE-II), while every platform's full
capability — type filters, default location, modal parent, multi-select — is reachable.

A launch returns a `Mel_Future*` resolving to a `Mel_Dialog_Selection` (a by-value
snapshot of the chosen path list, chosen filter, and status) on the caller's executor,
exactly like every other Melody async op. Cancel resolves to an empty selection with
`MEL_DIALOG_CANCELLED` set, never an error.

## Public surface

- `dialog/dialog.h` — `Mel_Dialog_Status` bitset (severity mask + condition/warning
  flags, inline predicates, no error strings per MEL-ENGINE-VIII); `Mel_Dialog_Filter`
  (label + extension patterns); the per-mode opt structs and their variadic
  `mel_dialog_open_file` / `mel_dialog_open_files` / `mel_dialog_save_file` /
  `mel_dialog_open_folder` macros; `Mel_Dialog_Selection` result view; and the
  `mel_dialog_future_selection` / `mel_dialog_future_status` / `mel_dialog_future_free`
  accessors.
- `dialog/backend.h` — the platform-layer contract (`mel_dialog__plat_available`,
  `mel_dialog__plat_run`) and the job accessors a backend uses to read the request and
  emit results. One translation unit per platform implements it, build-time selected.

The picker mode (open / save / folder) and multi-select are carried as a request
bitset on the job, not an enum (MEL-CODE-001): a backend reads the bits it understands
and warns on the ones it cannot honor.

## Backends

- macos — `NSOpenPanel` / `NSSavePanel`, dispatched to the main queue; filters map to
  `UTType` (`allowedContentTypes`) on macOS 11+, `allowedFileTypes` below. `NSOpenPanel`
  has no chosen-filter concept, so the chosen filter index is not reported on macOS.
- ios — `UIDocumentPickerViewController` (open / export / folder modes), presented from
  the key window's root controller or a supplied `Mel_Window`; security-scoped URLs are
  resolved to filesystem paths.
- win32 — `IFileOpenDialog` / `IFileSaveDialog` COM, run modally on the calling thread;
  filters via `SetFileTypes`, default location via `SetFolder`, multi-select via
  `FOS_ALLOWMULTISELECT`, folder pick via `FOS_PICKFOLDERS`; chosen filter reported via
  `GetFileTypeIndex`.
- linux — `org.freedesktop.portal.FileChooser` over D-Bus (sandbox-correct, no toolkit
  dependency), with a runtime-`dlopen`ed GTK 3 fallback when no portal is present.
- android — Storage Access Framework intents (`ACTION_OPEN_DOCUMENT` /
  `ACTION_CREATE_DOCUMENT` / `ACTION_OPEN_DOCUMENT_TREE`) via JNI; results are
  `content://` URIs delivered through the activity's `onActivityResult` forwarded to a
  registered native callback.
- wasm — `showOpenFilePicker` where available, `<input type=file>` otherwise; chosen
  files are written into the Emscripten virtual FS and their VFS paths emitted, so the
  rest of the framework's `io` can read them (MEL-ENGINE-VII: an honest alternative, not
  a broken shadow). Save-file is not yet serviceable on the web: it resolves with
  `MEL_DIALOG_WARNED | MEL_DIALOG_WARN_SAVE_UNSUPPORTED` rather than fabricating success,
  pending a save-content contract.

## Dependencies

`core`, `allocator`, `collection`, `string`, `executor`, `future`, `event`, `vat`,
`window`, `log`, `platform`.
