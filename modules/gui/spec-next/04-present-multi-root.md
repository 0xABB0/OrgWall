# 04 — present as new Root, with per-backend degrade

Parent: `../spec-next.md`. Depends on 02 and 03. Turns the temporary `present =
push` alias into the real multi-Root verb.

## Goal

`mel_app_present(name, arg)` opens a **new Root** — a new top-level host surface
owning its own Navigator rooted at `name`. Generalise the single global Navigator
of spec 02 to a dynamic set of Navigators, one per Root.

## Data model change

A dynamic array of `Mel_Navigator` (off `mel_gui__alloc()`), each with
`own_root = true` for a Root opened by `present`. The `from` handle on the in-stack
verbs (`push`/`replace`/`back`) now resolves to its Navigator via
`toplevel_of(from)` → the Navigator whose root frame is that toplevel. The
"foreground" Navigator (for the omitted-`from` convenience case) is the most
recently focused Root.

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

```c
bool mel_gui_backend_supports(Mel_Gui_Cap cap);   /* MULTI_ROOT */
```

`MULTI_ROOT` is true on desktop/xr, false on phone web/mobile unless the opt-in
flag is set. An app may branch its UX on it (e.g. a master-detail split instead of
a push) but the API never forbids `present`.

Note: `Mel_Gui_Cap` is the one place a closed set is unavoidable; per MEL-CODE-001
get Gabbo's sign-off before adding the cap enum, or model caps as named
`bool mel_gui_supports_multi_root(void)` predicates to avoid the enum entirely.
Prefer the predicate form unless a cap *set* is genuinely needed.

## Done when

- Desktop `present` opens a second coequal window; each window's `push`/`back`
  drives its own stack independently.
- Phone `present` degrades to a push; the opt-in flag yields a real second
  scene/task.
- Web `present` pushes a route by default; `window.open` reachable as opt-in.
- `mel_gui_supports_multi_root()` reports honestly per backend.
