# tray

The desktop-class system tray / status-icon surface. A `Mel_Tray` is a status icon (image plus
tooltip) carrying a top-level menu; the menu nests submenus and entries (button, checkbox,
separator) with enabled/checked state and per-entry callbacks. Create, update, and destroy are
explicit; the icon image is a raw RGBA buffer or a file path.

Handles are generational value handles over `Mel_SlotMap_Handle` (the `display` idiom):
`Mel_Tray`, `Mel_Tray_Menu`, `Mel_Tray_Item`, each with `_NULL` / `_equal` / `_alive`. Status is a
`u32` severity (`Ok | Warned | Error`) plus a warning/error bitset with branch-free predicates — no
strings, no enum (the `port`/`vibration` idiom). Entry kind is a flag bitset
(`MEL_TRAY_ITEM_BUTTON | CHECKBOX | SEPARATOR | SUBMENU`), not an enum; enabled/checked are flags.

Backends register a `Mel_Tray_Provider_Desc` vtable (the `vibration` idiom); one host backend owns
the system tray per platform:

- macOS — `NSStatusItem` on the system `NSStatusBar`, with an `NSMenu` per tray; menu-item clicks
  dispatch through a shared target. Templated icons honored.
- win32 — `Shell_NotifyIconW` on a `HWND_MESSAGE` window; the context menu is a popup `HMENU`
  tracked with `TrackPopupMenu`; clicks arrive as `WM_COMMAND`. Title is dropped (warned): the
  shell tray has no text label.
- Linux — `libayatana-appindicator3` / `libappindicator3` + GTK3, loaded with `dlopen` (no hard
  link dependency); menus are `GtkMenu`. Honest-absent when neither library nor a display is
  present (`mel_tray_supported()` returns false). The shell tray protocol carries no tooltip;
  tooltip is dropped (warned).
- iOS / Android / wasm — honest-absent: no system tray exists; the host provider registers nothing.

Events (icon activation, item click, menu open/close) are delivered through the `event` channel
with both a pull face (`mel_tray_poll_events`) and a push face
(`mel_tray_subscribe(Mel_Executor*, cb, user)`), mirroring `display`.

Image via a raw RGBA8 buffer (`Mel_Tray_Image.rgba`) or a file path (`Mel_Tray_Image.path`); the
module copies the buffer/string into its allocator, so the caller's storage need not outlive the
call.

Spec: `spec.md`. Dependencies: `core`, `allocator`, `collection`, `string`, `event`, `executor`,
`log`.
