# Screen / Navigation — corrected design (spec-next)

Supersedes the "Frames and the Screen model" section of `readme.org`. This is the
specification to iterate against; `readme.org` remains authoritative for the rest
of the module until this lands and is folded back in.

## Why the current model is wrong

The shipped `screen.c` conflates two things that must be separate:

- **Screen identity** — a registered `name` plus a builder callback. A singleton.
- **Navigation instance** — a live frame sitting on a history, with a predecessor
  it returns to. Inherently plural and ephemeral.

Per-instance state (`back`, `frame`, `created`) lives on the per-identity
singleton (`Mel_Gui_Screen`). Every concrete defect descends from that:

1. `back` is a single field on the shared struct, so `A→B→A→B` clobbers the first
   `B.back`; cycles and repeats corrupt the history.
2. A screen cannot have two live instances (two "details" windows).
3. `ensure_created` latches `created` and never re-runs the builder, contradicting
   the readme's claim that a Screen maps to an OS Activity/scene the system may
   destroy and recreate.
4. `present` never routes through `nav_*`; it is `set_visible+focus` only. So it
   means "additive new window" on desktop but "stack push (predecessor hidden)" on
   iOS/Android. Whether the previous screen is visible after `present(x)` is
   target-dependent — a divergence in *behaviour*, not the sanctioned divergence
   in *shape*. Violates MEL-ENGINE-IX and the module's own "not LCD" promise.
5. `back` is only ever set by `replace`; `present` records nothing, yet on iOS
   `present` is what pushes the real `UINavigationController`. The C `back` chain
   and the OS stack desync; the system back gesture is invisible to the model.
6. Closing a presented window (`windowWillClose`, `WM_CLOSE`) destroys the frame
   but never notifies `screen.c`. The singleton keeps `created=true` with a dead
   `frame` handle, so the screen is **permanently un-presentable**. Live bug.
7. Navigation carries no payload: `user` is bound once at register time, and
   `present`/`replace` take no argument. "Show details for record N" is
   inexpressible; apps work around it with file-global state.
8. `g_screens[MEL_GUI_MAX_SCREENS]` is a fixed 32-array — a direct MEL-CODE-002
   violation — and the 33rd registration silently returns (MEL-ENGINE-VIII).
9. `autosize_frame` bakes desktop-window sizing (320×240 floor, 24px margin,
   content fit) into the portable layer, meaningless on a full-screen scene or a
   web route.

## The reframe: Navigator and Root

Two orthogonal concepts replace the overloaded `present`.

**Navigator** — an ordered stack of screen *instances*. The universal,
first-class concept; it exists honestly on every target. Exactly one visible top
per Navigator.

**Root** — a top-level host surface owning one Navigator. The adaptive `Frame`:
desktop window, XR floating panel, mobile task/scene, web document. Multiplicity
of *Roots* is the "multiple windows" concept.

With the split, the visibility rule is invariant everywhere:

- `push` / `replace` / `pop` act *inside one Navigator* → predecessor hidden, back
  reveals it. Identical meaning on all six targets.
- Opening a new *Root* is the only verb that yields coexisting surfaces, and the
  only one that must degrade where the platform lacks the capability.

The desktop "two windows visible at once" that today's `present` produces by
accident is reclassified as *opening a second Root* — explicit, not a side effect.

## The verbs

`from` is a frame handle naming *which* Navigator (formalising the existing
`toplevel_of(from)` instinct). For the single-Navigator case (mobile, web, simple
desktop) it is inferred from the foreground Root and may be omitted.

- `push(from, name, arg)` — new instance atop `from`'s Navigator; predecessor
  retained and hidden; `back` returns to it.
- `replace(from, name, arg)` — swap the top instance; predecessor destroyed, not
  on the back stack.
- `back(from)` — pop the top, reveal predecessor.
- `pop_to(from, name)` / `pop_to_root(from)` — unwind multiple frames.
- `present(name, arg)` — open a **new Root** with its own Navigator rooted at
  `name`. The multi-window verb.

Every verb carries a per-navigation `arg`, distinct from the register-time
default. Builder signature becomes `build(frame, arg)`.

Naming: `Navigator`, `push`/`pop`/`replace` (UINavigationController/Flutter
pedigree). `present` is settled — it keeps its current desktop "new top-level
surface" instinct, now meaning "open a new Root".

## `present` (multi-Root): per-platform shape and degrade

The verb is *always* available (MEL-ENGINE-IV); each backend chooses its shape,
degrading with an honest alternative, never a stub (MEL-ENGINE-VII). An app may
query `mel_gui_backend_supports(MULTI_ROOT)` to branch its UX, but the API never
forbids the verb.

**Desktop (win32, cocoa, gtk, qt):** native. A new top-level `HWND`/`NSWindow`,
coequal, both visible.

**XR (drawn):** native. A new floating panel in space alongside the existing one —
the spatial analogue of a desktop window.

**Mobile (iOS, Android):** a phone has no coequal-window concept, so the default
is **`present → push`**: the new Root collapses into a forward navigation on the
single task Navigator, reached back via the system gesture/button. Mobile is not a
flat "no", though: iPadOS `UIWindowScene` and Android freeform/multi-task offer
real parallel surfaces. So multi-Root is a **capability-gated opt-in** there — an
app that sets the own-scene/own-activity flag (`MEL_FRAME_OWN_ACTIVITY`, already
in `todo.org`) gets a real second scene via `startActivity`/scene request;
everyone else gets the push degrade.

**Web (DOM):** one document is one Navigator backed by the History API —
`pushState` for `push`, the `popstate` event plus the browser back/forward buttons
driving `pop`. **`present` default-degrades to a route push** within the single
document. Real multiple windows exist (`window.open`), but a second browsing
context is a *second wasm instance with its own JS heap and app state* — not
another view of the same state; sharing across them needs
`postMessage`/`BroadcastChannel`/`SharedWorker`. So honest default: route push;
real `window.open` is the capability-gated escape hatch, and the app owns the
cross-context messaging. Pretending a popup shares live state would be the silent
corruption MEL-ENGINE-VIII forbids.

Throughline: `push` means the same thing everywhere; `present` means "new coequal
surface where the platform has them, an honest forward-navigation where it
doesn't," and the app can query which it got.

## Data model

Registry (identity), dynamic, off `mel_gui__alloc()`:

```c
typedef struct {
    str8             name;
    Mel_Screen_Build build;          /* build(Mel_Gui_Handle frame, void* arg) */
    void*            default_user;   /* register-time default; overridable per-nav */
} Mel_Screen_Def;
```

Navigation instance and stack (per Root), both dynamic arrays:

```c
typedef struct {
    str8           name;             /* into the registry */
    Mel_Gui_Handle frame;           /* this instance's live frame */
    void*          arg;             /* per-navigation payload */
} Mel_Nav_Entry;

typedef struct {
    Mel_Nav_Entry* entries;          /* stack; top is last */
    u32            count, cap;
    bool           own_root;         /* this Navigator hosts its own top-level Root */
} Mel_Navigator;
```

`back` is not a field; it is stack order. A registry entry holds no live state, so
repeats, cycles, and two instances of one screen all work. The builder runs per
pushed instance, restoring rebuild-on-re-entry.

## Lifecycle

- Builder runs on each push; `created` as a global latch is gone.
- Hooks `on_enter` / `on_leave` / `on_destroy` so app state survives leave/return.
- The serializable unit for mobile process-death is the instance stack — each
  entry's `name` + `arg`. That is what feeds `savedInstanceState` (the gap
  `todo.org` records). Honesty boundary: in-process `arg` is a raw pointer;
  cross-process restore needs the app to supply encode/decode, else restore
  rebuilds names without args. Document the line; do not paper it.

## Frame-destroyed reconciliation (fixes the live bug)

One internal hook — `mel_gui__frame_closed(Mel_Gui_Handle)` — called from every
backend close path that already exists (cocoa `windowWillClose`, winui `WM_CLOSE`,
the mobile pop). The nav layer subscribes and removes the matching instance, so
*OS-driven* dismissal (window close box, iOS swipe-back, Android back button,
browser back) flows through the **same** pop path as programmatic `back`. This is
what keeps the C model and the OS stack from desyncing.

## Layering

The Navigator stack is intrinsically about frames and visibility — it stays in the
gui module, in a new `nav.c`. The named-screen registry stays in `screen.c`. The
`mel_app_*` surface is the thin app-facing wrapper over both.

The registry stays in gui, not `app`. The dependency direction is `app → gui`
(`app/src/android/app.bridge.c` includes `<gui/init.h>`; gui includes nothing from
app). `nav.c` must read the registry to build screens, so a registry in `app`
would force `gui → app` and cycle. Both files therefore live in gui under the
`mel_app_*` surface.

No geometry constants in the portable layer: `autosize_frame`'s window sizing
moves into the desktop backend's Root-presentation path; the portable layer asks
the backend to "present this frame as a Root" and the backend sizes it. Mobile and
web ignore sizing.

## Failure discipline (MEL-ENGINE-VIII, MEL-CODE-002)

- Registry and every Navigator stack are dynamic arrays — no `MEL_MAX_*`.
- Duplicate-name or null-builder registration asserts loudly; never a silent
  return.
- Navigating to an unregistered name asserts in debug.

## Implementation order (no-prerequisite first)

1. Dynamic registry + assert-on-overflow, replacing the fixed array. Pure
   mechanical; unblocks everything, changes no semantics yet.
2. `Mel_Navigator` instance stack + `push`/`replace`/`back`/`pop_to`, single
   implicit Navigator, `arg` threaded through the builder. Desktop-emulated
   hide/show within one Navigator.
3. `mel_gui__frame_closed` hook wired from each backend close path → stack
   reconciliation. Kills the bricked-screen bug; unifies OS-driven back.
4. `present` as new-Root, with the per-backend degrade (desktop native, mobile
   push-default + opt-in scene, web route-default + opt-in popup).
5. `MULTI_ROOT` capability query; lifecycle hooks; move `autosize` into the
   desktop backend.
6. Fold the result back into `readme.org`; retire this file.
