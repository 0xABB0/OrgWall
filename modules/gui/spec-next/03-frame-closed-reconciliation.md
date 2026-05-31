# 03 — Frame-destroyed reconciliation

Parent: `../spec-next.md`. Depends on 02. Fixes the live bricked-screen bug and
unifies OS-driven back with programmatic back.

## The bug

Closing a presented window destroys the frame (`cocoa/frame.m windowWillClose`,
`winui/frame.c WM_CLOSE`, mobile pop) but never tells the navigation layer. The
old singleton kept `created=true` with a dead handle and became permanently
un-presentable. After spec 02 the analogue is a stale `Mel_Nav_Entry` whose
`frame` handle is dead, with the OS stack and the C stack desynced.

## The hook

One internal entry point, declared in `gui_internal.h`:

```c
void mel_gui__frame_closed(Mel_Gui_Handle frame);
```

`nav.c` implements it: find the entry whose `frame` matches across all
Navigators, remove it, and — if it was the visible top — reveal the new top. This
is the *same* removal path `mel_app_back` uses; OS-driven dismissal and
programmatic `back` converge on one function. The difference: `frame_closed` must
**not** ask the backend to destroy the frame again — the OS already did. Factor
the stack-pop logic out from the frame-destroy logic so `back` calls both and
`frame_closed` calls only the pop.

## What 03 actually fixes (given 02)

Because 02's `present` always instantiates a fresh frame + Navigator (no `created`
latch), the original *permanent brick* is already gone. What remains for 03:

1. **OS-closing a pushed window strands its predecessor.** `push` hid the previous
   window; if the user closes the pushed window with the OS close box instead of a
   `back` button, without reconciliation the predecessor stays hidden forever and
   the Navigator's top is a dead handle. `frame_closed` pops the dead top and
   reveals the predecessor.
2. **Stale entries/Navigators accumulate** in `g_navs` after any OS close until
   shutdown. `frame_closed` drops them promptly.

## No-op-for-verbs design

The programmatic verbs (`back`, `replace`, `pop_to`, `pop_to_root`) remove their
entry from the stack *before* calling `mel_gui_destroy`. So when the resulting
backend teardown fires `frame_closed`, the entry is already gone and it no-ops.
`frame_closed` does real work only for OS-initiated closes. No separate
pop-vs-destroy factoring was needed — the remove-before-destroy ordering already
in the verbs is what makes this safe.

## Per-backend call sites — as built

- **cocoa** `frame.m windowWillClose` → `mel_gui__frame_closed(frame_h)` as the
  first action (handle still valid, still in the Navigator). Implemented, tested
  on the host: builds clean, launches without assert.
- **winui** `frame.c WM_DESTROY` → same, before `destroy_tree`/`frames_dec`.
  Wired; build-verified only when a Windows host is available.

### Deferred to spec 04 (with the backend's own rework)

uikit, android, and dom are still built on the old single-surface / single
`UINavigationController` model and have no OS-close path that maps onto per-Root
Navigators yet. Their *programmatic* teardown (`backend_destroy`) runs after the
verbs already popped, so it needs no hook. The genuinely missing piece is the
**OS-back gesture** (iOS swipe, Android hardware back, browser back/forward),
which is entangled with the per-Root `present` *degrade* those backends gain in
spec 04 (iOS `popToViewController` delegate, android `mel_back`, dom `popstate`).
Wiring `frame_closed` there is folded into 04 so it lands together with the model
it reconciles against. Until then those backends keep their current behaviour.

## Shutdown ordering

`mel_gui__navs_reset` was moved to the *top* of `mel_gui_shutdown`, before the
node-destroy loop. The loop fires every frame's OS-close path (hence
`frame_closed`); with `g_navs` already emptied, those calls no-op instead of
reviving a predecessor window mid-shutdown. `navs_reset` destroys no frames (it
only frees handle bookkeeping), so doing it first is safe.

## Done when

- OS-closing a pushed window reveals its predecessor (cocoa). ✓ wired + builds.
- App shutdown tears down all frames without double-free or predecessor revival.
  ✓ via the navs-first ordering.
- iOS swipe-back / Android hardware back / browser back consistency → **spec 04**.
