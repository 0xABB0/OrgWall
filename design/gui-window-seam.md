# Melody `gui` on `window` — Integration Spec (Model B)

How `gui`'s Frame, Navigator, and screens sit on the extracted `window` module. Decision: **one durable window per Root; the Navigator swaps screen content, never windows.** This splits the current `window = Frame = screen` fusion into three lifetimes — window, Navigator, screen.

## 1. Fusion today
A Frame is a top-level `Mel_Gui_Node` whose `native` holds the `NSWindow` (`frame.m`). The Navigator (`nav.c`) is a stack of Frames; `push` makes a new Frame/window and hides the predecessor, `back` destroys it. The Frame→window mapping is already platform-divergent — the `mel_gui__nav_replace` doc notes "desktop swaps window visibility, Android drives the fragment back stack." Model B makes it uniform: content-swap-in-one-window everywhere.

## 2. Shape
- **Root → one `Mel_Window`.** `mel_app_present` is the only window-creating verb (new Root = new window + Navigator). `!mel_gui_supports_multi_root` degrades present to a push — under Model B literally a content swap, so the degrade stops being special.
- **Navigator → a stack of screen content subtrees** mounted in the window's content container (`mel_window_content_native`).
- **Screen → a content subtree** rooted at a parented node, not a toplevel.

## 3. Verbs (names and `gui` home unchanged; mechanics change)
- **present** — create window + Navigator, build first screen, show.
- **push** — build next, mount, hide (not destroy) predecessor. `mel_gui__nav_replace` → mount-next/hide-prev.
- **replace** — build next, mount, destroy outgoing.
- **back / pop_to / pop_to_root** — show retained predecessor, destroy popped. `mel_gui__nav_back` → show-prev/unmount-cur.
- **OS back** (`mel_gui__nav_os_back`) — unchanged in intent.

Predecessors are retained-hidden via the node `hidden` flag (as predecessor windows were); `back` re-shows, never rebuilds — rebuilding would lose screen state. Layout skips hidden subtrees.

## 4. Close
`on_closed` fires while the window handle is valid (the `mel_gui__frame_closed` contract). Closing the window tears down the whole Navigator: `on_leave` the top, `on_destroy` each entry, drop the Navigator — not one Frame.

## 5. `Mel_Frame_Opt` collapse
- Window-shaped fields → `Mel_Window_Opt`, consumed once at `present` with Root defaults only.
- Per-screen chrome is set in the screen's `on_enter`, which holds the window handle and mutates title/style/bounds directly — no override block, no registry field.
- The callback fields are window-level signals the active screen consumes: `on_resize`→relayout, keyboard/focus→focused widget (§6), insets→window property, close→§4. The screen API stays `Mel_Screen_Opt` (`build` + enter/leave/destroy).

## 6. Input & liveness
Window-level raw input (`Mel_Window_Input_Cb`) routed to widgets by `gui` (`design/window.md` §7). Window count and quit-on-last move to `window`+`app` (`design/window.md` §8); `gui` keeps only Navigator bookkeeping. `mel_gui_shutdown`'s invariant — reset `g_navs` before backend teardown — holds, now reading "before tearing down host windows."

## 7. Scope
Secondary windows are not Navigator content: modal dialogs (`dialog.m`, `owner`), palettes, "open in new window" are additional `Mel_Window`s with an owner relation. Model B governs only navigation within a Root, so multi-window stays intact and push (content) versus present (own window) become distinct verbs rather than both spawning windows.

## 8. Backend hook delta
- `mel_gui__present_root` — size the host window to content (desktop); no-op where the scene owns size.
- `mel_gui__nav_replace(next, prev)` — mount next, hide prev.
- `mel_gui__nav_back(prev, cur)` — show prev, unmount cur.
- `mel_gui__frame_closed` — re-home at Navigator-teardown granularity (§4).
- `mel_gui__resized` — driven by the host window's `on_resize`.
- `mel_gui__backend_destroy` — for a screen root, unmount + destroy the subtree; the window's destruction is `window`'s.

`gui` depends on `window`; never the reverse.
