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

## Per-backend call sites

Each backend's existing close path calls `mel_gui__frame_closed(frame_h)` before
it runs `mel_gui__destroy_tree` / `frames_dec`, so the nav layer reconciles while
the handle is still valid:

- cocoa `frame.m windowWillClose` (and `dialog.m` if dialogs join the stack).
- winui `frame.c WM_CLOSE`.
- uikit: the nav-controller `popViewControllerAnimated` driven by the swipe-back
  gesture / system back — bridge the `didShow`/`willShow` delegate to
  `frame_closed` for the popped controller.
- android: the activity/fragment back (`mel_back`) path → `frame_closed` for the
  popped frame.
- dom: the `popstate` event (browser back/forward) → `frame_closed` for the
  entry leaving the top.

The invariant: every way a Root can vanish — user close box, swipe, hardware
back, browser back — routes through `mel_gui__frame_closed`, so the C stack never
holds a dead top.

## Reentrancy

`frame_closed` can fire mid-teardown (e.g. app shutdown destroys every frame).
Removing an entry must tolerate the Navigator being torn down and must not
re-enter the backend destroy. Guard against double-removal (entry already gone).

## Done when

- Present a screen, close its window via the OS close box, present it again — it
  reappears (no brick).
- iOS swipe-back and Android hardware back leave the C stack consistent with the
  OS stack (verified by a subsequent programmatic `back` behaving correctly).
- App shutdown tearing down all frames does not double-free or re-enter.
