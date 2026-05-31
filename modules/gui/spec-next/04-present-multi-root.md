# 04 — present per-backend degrade + capability query

Parent: `../spec-next.md`. Depends on 02 and 03.

Note: the per-Root data model (a dynamic set of `Mel_Navigator`, `nav_of`
resolution, `present` creating a new Root) **already landed in spec 02** — it had
to, or the demo regressed. What remains here is the per-platform *shape* of
`present` and the capability query. On desktop (cocoa/win32) `present` is already
correct (a new window); this step makes the mobile and web backends honest.

## Goal

Give `mel_app_present` its correct shape on every backend: native coexisting
surface where the platform has one, an honest forward-navigation where it does not,
plus a capability predicate so apps can branch.

## Invariant semantics

- `push`/`replace`/`back` act *within one Navigator*: predecessor hidden, back
  reveals. Identical on every target (spec 02 already established this).
- `present` is the *only* verb yielding coexisting surfaces, and the only one that
  degrades where the platform lacks the capability.

## Per-backend present

Always available (MEL-ENGINE-IV); each backend chooses the shape, degrading with
an honest alternative, never a stub (MEL-ENGINE-VII).

- **desktop (win32/cocoa/gtk/qt):** native. New top-level `HWND`/`NSWindow`,
  coequal, both visible. New Navigator, `own_root = true`.
- **xr (drawn):** native. New floating panel in space.
- **mobile (iOS/Android):** default **`present → push`** on the single task
  Navigator (forward navigation, system back returns). Real parallel surfaces are
  a capability-gated opt-in: a frame created with the own-scene/own-activity flag
  (`MEL_FRAME_OWN_ACTIVITY`, `todo.org`) gets a real second scene via
  `startActivity` / `UIWindowScene` request → a second Navigator with
  `own_root = true`.
- **web (dom):** default **`present → route push`** via `history.pushState` in the
  single document; `popstate` + browser back/forward drive `pop` (already wired in
  spec 03). Real `window.open` is the capability-gated escape hatch and spawns a
  *separate wasm instance with its own state* — the app owns cross-context
  messaging (`postMessage`/`BroadcastChannel`/`SharedWorker`). Default never
  pretends a popup shares live state (MEL-ENGINE-VIII).

## Capability query

Per Gabbo: a named predicate, **declared once, implemented per-backend** — no cap
enum (MEL-CODE-001). Each backend returns its own compile-time constant; there is
no backend whose multi-Root availability varies at runtime.

```c
bool mel_gui_supports_multi_root(void);   /* <gui/init.h> or <gui/screen.h> */
```

- desktop (cocoa/win32/gtk/qt), xr (drawn): returns `true`.
- mobile (uikit/android), web (dom): returns `false` (the `present`→push / →route
  degrade is what runs); a future own-scene/own-activity opt-in is a separate
  query, not a flip of this one.

An app may branch its UX on it (e.g. a master-detail split instead of a push) but
the API never forbids `present`. The pre-existing
`mel_gui_backend_supports(Mel_Gui_Capability)` enum in `gui.c` is a separate,
older surface; whether to retire it in favour of predicates is a follow-up for
Gabbo, out of scope here.

## Done when

- Desktop `present` opens a second coequal window; each window's `push`/`back`
  drives its own stack independently.
- Phone `present` degrades to a push; the opt-in flag yields a real second
  scene/task.
- Web `present` pushes a route by default; `window.open` reachable as opt-in.
- `mel_gui_supports_multi_root()` reports honestly per backend.
