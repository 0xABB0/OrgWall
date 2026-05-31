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

## As built

- **Predicate** `mel_gui_supports_multi_root()` declared in `<gui/init.h>`,
  implemented per-backend next to each `nav_back`: cocoa/winui `true`, uikit /
  android / dom `false`. No enum.
- **Degrade** lives in portable `nav.c` `mel_app_present`, gated on the predicate:
  when `false` and a Root already exists, it pushes onto `g_navs[0]` (the single
  Root, which degrades never move past) via `nav_replace` — the backend's own
  native push (uikit `ios_show_frame`, android `presentFrame`, dom `set_visible`
  + a history entry). The first `present` always creates the root. Desktop
  (`true`) keeps the new-window path untouched.
- **OS-back reconciliation:**
  - cocoa/winui: window close box → `frame_closed` (spec 03).
  - uikit: `MelViewController didMoveToParentViewController:nil` → `frame_closed`
    (fires on swipe, system back, and programmatic pop; `set_visible` reveal
    no-ops because the popped VC's predecessor is already the nav top).
  - android: hardware/predictive back → `MelGui.nativeOsBack()` →
    `mel_gui__nav_os_back()` → `nav_back` drives the Java `back()` pop + node
    destroy. Returns false at root so the system finishes the activity.
  - dom: `popstate` (browser back/forward) → `mel_web__ev_popstate` →
    `mel_gui__nav_os_back()`. Forward navigation calls `history.pushState`.

## Build/verify status (this host: macOS)

- cocoa: builds + runs, no regression (degrade is inert when multi-root is true).
- android: full APK builds — JNI `nativeOsBack` + `MelGui`/`MelodyActivity` Java
  + `mel_gui_supports_multi_root` all compile and link into the `.so`.
- ios: my edits compile (frame.o/backend.o build); the **link** fails on
  `_objc_opt_isKindOfClass` / `_objc_setProperty_atomic` — a pre-existing
  objc-runtime link-flag gap on this host, independent of this change.
- web: the current build system does not recognise the `web` platform, so the dom
  edits are **unverified even at compile** here. Recheck once `web` is wired.

## Known gaps (deferred, documented honestly per MEL-ENGINE-VIII)

- **iOS swipe leaks the node.** `frame_closed` reconciles the stack but does not
  destroy the popped VC's `Mel_Gui_Node` (it must not, or it would double-destroy
  on cocoa/winui). On iOS nothing else tears that node down after a swipe pop, so
  it leaks — consistent with the destroy-walk limitations already in `todo.org`.
  Fix when iOS is testable (destroy the node from the iOS pop path specifically).
- **Web URL leads the stack by one.** `mel_app_back` advances the app without
  calling `history.back`, so the route can be one entry ahead of the stack.
  Browser back still pops correctly; full bidirectional URL sync is a refinement.
- **Multi-root opt-in escape hatches** (iPad `UIWindowScene` / Android own-task
  via `MEL_FRAME_OWN_ACTIVITY`; web real `window.open`) are not built — only the
  default degrade. They need a `Mel_Frame_Opt` flag plumbed; tracked for later.
