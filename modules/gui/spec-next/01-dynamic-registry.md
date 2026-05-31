# 01 — Dynamic screen registry

Parent: `../spec-next.md`. No prerequisite. Mechanical; changes no navigation
semantics. Land this first.

## Goal

Replace the fixed `g_screens[MEL_GUI_MAX_SCREENS]` array with a dynamic array off
`mel_gui__alloc()`, and replace silent overflow/duplicate handling with loud
assertions. The registry becomes identity-only — it holds no per-instance
navigation state.

## Scope of change

`modules/gui/src/screen.c` registry storage only. `mel_app_present` /
`mel_app_replace` / `mel_app_back` keep their *current* behaviour in this step;
they are rewritten in spec 02–04. This step must compile and run
`hello-world-gui` with identical observable behaviour.

## Data model

This step swaps storage and adds assertions only. The existing `Mel_Gui_Screen`
struct (with its live `frame` / `created` / `back` fields) is kept intact so the
step is exactly behaviour-preserving; the nav verbs still read those fields. The
identity/instance split — extracting an identity-only `Mel_Screen_Def` and moving
`frame`/`created`/`back` onto a Navigator instance — is wholly spec 02's job,
landing immediately after, so **no scratch field is introduced and none lingers**.

Storage moves from the fixed `g_screens[MEL_GUI_MAX_SCREENS]` to
`Mel_Array(Mel_Gui_Screen)` (`collection.array`, grown via `mel_gui__alloc()`).
Remove `MEL_GUI_MAX_SCREENS`. Because entries now live in a reallocating array,
the navigation verbs must not hold a `Mel_Gui_Screen*` across a `find`/register
that could grow the array; resolve by value or re-find. (Spec 02 removes these
pointers entirely.)

## Failure discipline (MEL-ENGINE-VIII, MEL-CODE-002)

- No `MEL_MAX_*`; the array grows.
- `mel_app_register_screen` with a null `build` asserts.
- Registering a name already present asserts (duplicate identity is a bug, not a
  silent overwrite).
- `find_screen` returning NULL for a name passed to a nav verb asserts in debug
  (navigating to an unregistered screen is a programmer error).

## API

Unchanged signature:

```c
void mel_app_register_screen(str8 name, Mel_Screen_Build build, void* user);
```

`user` is stored as `default_user`. Per-navigation `arg` override arrives in
spec 02; this step keeps `default_user` as the only payload.

## Done when

- `MEL_GUI_MAX_SCREENS` is gone from the tree.
- Registering 33+ screens works; null build and duplicate name assert.
- `hello-world-gui` behaves exactly as before.
