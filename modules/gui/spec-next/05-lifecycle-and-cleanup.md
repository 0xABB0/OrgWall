# 05 — Lifecycle hooks, autosize relocation, restore boundary

Parent: `../spec-next.md`. Depends on 02–04. The finishing pass: per-screen
lifecycle, move desktop geometry out of the portable layer, and state the
process-death restore boundary honestly.

## Lifecycle hooks

Per-instance callbacks so app state survives leave/return without file-global
scratch:

- `on_enter(frame, arg)` — fired after a push/present makes the instance the
  visible top, and again when a `back`/`pop` re-reveals it.
- `on_leave(frame)` — fired when the instance stops being the visible top (a push
  on top of it, or it being popped).
- `on_destroy(frame)` — fired when the instance's frame is torn down (replace,
  back, or `frame_closed`).

Decide the carrier: either fields on the screen registration
(`mel_app_register_screen` gains an opt struct) or the existing frame
`Mel_Gui_Lifecycle_Cb`. Prefer reusing the frame lifecycle cb where it already
covers the event (`on_show`/`on_hide`/`on_destroy`) and only add nav-specific
hooks for what it cannot express (re-enter vs first-enter). Note in `todo.org`
that `on_destroy` "no longer fires from inverted backends" (existing open item) is
subsumed here.

## Autosize relocation

`autosize_frame`'s window sizing (320×240 floor, 24px margin, content fit, the
direct `node->width/height` poke) leaves the portable layer. The portable
`instantiate(entry)` asks the backend to "present this frame as a Root"; the
desktop backend performs sizing there. Mobile and web ignore it (full-screen
scene / route). No geometry constants remain in `nav.c` or `screen.c`.

This likely means a new internal backend hook, e.g.
`mel_gui__present_root(Mel_Gui_Handle frame)`, called once per Root creation, that
desktop implements as size-to-content + show and mobile/web implement as their
native present. Keep it distinct from `mel_gui_set_visible`.

## Restore boundary (honesty, not papering)

The serializable unit for mobile process-death is the instance stack: each
entry's `name` + `arg`. State the boundary explicitly:

- In-process, `arg` is a raw `void*`; navigation and `back` work fully.
- Across process death (Android `savedInstanceState`, iOS state restoration), only
  the `name` chain restores for free. Restoring `arg` requires the app to supply
  an encode/decode for its payload; absent that, restore rebuilds the `name` chain
  with `arg = NULL` (builders fall back to `default_user`).

Specify the app-facing shape (an optional per-screen `arg_encode`/`arg_decode`
pair, or a documented "args are not preserved across process death") and record
the chosen contract. Wiring the actual `savedInstanceState`/scene-restoration
plumbing stays a `todo.org` entry; this spec only fixes the *contract*.

## Done when

- `on_enter`/`on_leave`/`on_destroy` fire at the specified transitions.
- No geometry constants outside the desktop backend.
- The restore contract is written down and the in-process path honours it
  (args present in-process, builders tolerate `arg = NULL`).
