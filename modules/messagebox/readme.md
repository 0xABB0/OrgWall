# messagebox

Modal native alerts. A synchronous, init-free facility: every call blocks the caller until
the user dismisses the dialog and may be invoked **before** any subsystem is initialized, so a
startup failure can be reported to the user before the app loop exists (MEL-ENGINE-VIII).

## API

- `mel_msgbox_alert(title, message, ...)` — the simple OK-only variant; returns a
  `Mel_Msgbox_Status`.
- `mel_msgbox_show(...)` — the buttoned variant; returns a `Mel_Msgbox_Result` carrying the
  chosen button id and the status. With no `buttons` it presents an implicit OK.
- `mel_msgbox_available()` — whether a native dialog backend exists on this platform/session.

Each button is a `{ str8 label; i32 id; }` descriptor; the caller owns the array for the
duration of the call. `default_id` (return key) and `escape_id` (escape/cancel) fall back to
the first and last button respectively unless `has_default_id`/`has_escape_id` are set.
Severity is `Mel_Msgbox_Severity` (info/warn/error) — a `u32` value, not an enum. Optional
`accent`/`text`/`background` colors are `Mel_Msgbox_Color { has_value; mel_color8 }`; `right_to_left`
reorders the buttons; `parent` (a `Mel_Window`) makes the dialog sheet-modal to that window.

Status is a severity-masked bitset (`MEL_MSGBOX_OK|WARNED|ERROR` in bits 0..1) plus flags
(`RESULT_*`, `WARN_*`) and `static inline` predicates `mel_msgbox_failed`/`mel_msgbox_warned`.

## Backends

- macos — `NSAlert` (`runModal`). Custom buttons with default/escape key equivalents. Per-button
  colors and RTL are not exposed by `NSAlert`; dropped with a warning.
- ios — `UIAlertController` presented on the foreground view controller, blocked on a nested
  runloop. Accent tint and RTL honored; text/background dropped with a warning. Absent before a
  foreground scene exists.
- win32 — `TaskDialogIndirect` (custom buttons, default, severity icon, `TDF_RTL_LAYOUT`).
  Arbitrary colors dropped with a warning.
- linux — GTK 3 `GtkMessageDialog`, `dlopen`'d at runtime (no link-time GTK on the cross host).
  Custom buttons, default response, RTL via widget direction, colors via a CSS provider. Absent
  when GTK cannot load or no display is present.
- android — `AlertDialog` via a JNI bridge to `MelodyMessagebox.show`, blocking the caller on a
  latch while the dialog runs on the UI thread. AlertDialog has three button slots; more than
  three collapse with a warning. Accent/text/background tinting honored.
- wasm — `window.alert` (1 button) / `window.confirm` (2 buttons); more than two collapse with a
  warning (honest single-decision degrade, MEL-ENGINE-VII). Colors, RTL and parent dropped.

## Dependencies

`core`, `string`, `color`, `window` (for the `Mel_Window` parent type), `allocator`, `log`;
`platform` on android (JNI environment).
