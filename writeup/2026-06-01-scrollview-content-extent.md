# 2026-06-01 — ScrollView content extent derives from children, not a constant

## Work done

Symptom: `detailscreen.c` grew its scrollview from 10 to 800 rows, but the
scrollable extent stayed frozen — rows past the original ten were reachable only
through elastic overshoot.

Root cause: every backend snapshotted `Mel_ScrollView_Opt.content_h` into the
native scroll surface at create time and never remeasured. `content_h` was an
author-maintained constant duplicating a truth the engine already measures —
`mel_gui__content_size` (`gui.c`), used since the screen-sizing path to compute a
subtree's natural extent layout-free. The scroll host simply never called it.

Fix (the "full" option — closes the `todo.org` *ScrollView nested layout* item):

- **Extent is measured.** New `mel_gui__scroll_fit` (`layout.c`) measures the host
  via `mel_gui__content_size`, clamps up to the viewport and the optional author
  floor, hands the extent to a new per-backend hook
  `mel_gui__backend_set_content_size`, then arranges the host's layout against the
  content extent rather than the viewport.
- **`content_w/content_h` demoted to a floor** (`content_floor_w/h` on the node).
  The default is now measure; the constant only pins a deliberately-oversized
  canvas. Capability retained (MEL-ENGINE-I/IV), default made honest.
- **Layout protocol carries the box.** `Mel_Layout_Vtable.arrange` now takes
  `(avail_w, avail_h)`. Ordinary nodes get their viewport; scroll hosts get the
  measured content height. `column_arrange` arranges against the passed box.
- **Relayout reaches scroll hosts.** `column`/`tabview`/`splitter` arranges recurse
  into `c->is_scroll_host` children (not only `c->layout`), so a scroll host is
  fitted whether it carries a layout or hosts absolute children, and composes
  inside tabs/splits (MEL-ENGINE-IX).
- **Demo rewritten.** The scrollview now carries a column layout and 800 labels;
  the `8 + i*34` absolute arithmetic and the `content_h` magic number are gone —
  the extent falls out of the children.

`mel_gui__backend_set_content_size` implemented per backend: cocoa
(`documentView` frame), uikit (`UIScrollView.contentSize`), dom (inner div
bounds), winui (`Mel_Win32_Scroll` + `scroll_apply`), android (new Java
`MelScrollView.setContentSize` via JNI, dp→px). Cocoa also now enables both
scrollers with autohide, since visibility was previously computed once from the
stale seed extent.

## Kludges / debt (confessing all — MEL-ENGINE-VIII)

- **Only cocoa is run-verified** (Gabbo confirmed live). uikit/dom/winui were not
  built on this host; android was neither built nor run. Their hooks are written
  to be correct but are **unverified**. The arrange-signature ripple through
  `winui/tabview`, `winui/splitter`, `androidnative/tabview`,
  `androidnative/splitter` likewise compiles only against my reading.
- **Android carries the most risk.** New Java method + JNI methodID +
  `dp2px` on both axes, and it assumes the inner `FrameLayout`'s `LayoutParams`
  width/height drive a vertical `ScrollView`'s scroll range. Untested; the
  `setFillViewport(true)` + explicit child height interaction may need a tweak.
- **`content_h` as floor changes semantics**: an extent *smaller* than the
  children is no longer expressible. I judged that incoherent, but it is a
  behavioural change, not a pure fix.
- **No automatic relayout on dynamic child append.** `scroll_fit` runs during the
  relayout pass (driven by `nav`); appending rows after the initial build still
  requires an explicit `mel_gui_relayout`. Pre-existing, not introduced here, but
  now load-bearing for scroll extent.
- **Cross-axis arrange uses the viewport width**, not the measured content width;
  a child intrinsically wider than the viewport gets an h-scroll surface but
  stretch siblings only reach the viewport. Acceptable for vertical lists; noted.
- **No regression test.** GUI extent assertions need a live window; the repo gates
  GUI tests on the unbuilt input.replay module, so this rests on the manual run.

## Unrelated working-tree changes (NOT mine — flag)

`apps/midi-monitor/build.c` (`mel_android_manifest(...)`) and
`modules/build/android/app/build.gradle.kts` (sourceSet java/manifest wiring)
were modified in the working tree during this session; I never touched them and
the start-of-session status showed only `detailscreen.c`. Left untouched —
Gabbo should confirm their provenance before any commit.

## CLAUDE.md suggestions (recommendations only — not applied)

- `modules/gui` readme/header: document that `content_w/content_h` are *floors*,
  and that scroll extent is measured from children during relayout.
- Note the arrange protocol: `arrange(container, avail_w, avail_h)` arranges
  against the given box; scroll hosts receive their content height.

## Suggestions

- 800 live `NSTextField`s is a MEL-ENGINE-VI problem independent of extent. The
  right primitive for long homogeneous lists is a virtualizing List/Collection
  with view recycling (the deferred `ListBox`, `todo.org`), not N retained native
  controls. Separate widget, separate work — named here so it is not forgotten.
- Build the non-host backends in CI (at least compile) so signature ripples like
  this arrange change fail fast instead of on the next platform build.
