# 2026-06-10 — gui: per-component styles

Branch: `worktree-style-per-component` (worktree on the merged main). Gabbo's
correction to the previous session: no single `Mel_Style` — each component
carries its own style type.

## Work done

`<gui/style.h>` reduced to shared primitives: `Mel_Style_Color` (set-flag
optional color), `Mel_Font` (family/size/weight/italic), `Mel_Style_Surface`
(bg/border/corner/padding), with `mel_font_any`/`mel_style_surface_any`.
Every control header now defines its own `Mel_<Widget>_Style` composed from
those, a `mel_<widget>_style_any()` inline, and a per-widget setter op
`mel_<widget>_set_style(h, ...)` (macro over `_opt`, backend-owned). The
generic `mel_gui_set_style` is gone.

The per-widget shape immediately bought widget-specific properties a shared
struct could not express, with native mappings where they exist:

- `Mel_Slider_Style.track/.thumb` — both native tints on android/uikit;
  track on cocoa (`trackFillColor`) and dom (`accent-color`).
- `Mel_CheckBox_Style.tint` — `setButtonTintList` (android), `onTintColor`
  (uikit), `accent-color` (dom).
- `Mel_GroupBox_Style.title_font/.title_fg` — `<legend>` styles (dom), title
  view (android), caption font + `WM_CTLCOLOR*` (winui), `titleFont` (cocoa).
- `Mel_Splitter_Style.divider` — native divider paint on android; bg-brush on
  winui (where the divider physically is the background).

All six backends converted (cocoa, dom, androidnative, uikit, winui, xcb) to
per-widget setters over shared internal helpers; every create applies its
typed `o.style`. hello-world-gui updated to the new initializer shapes.

Verified: macos build + live run (styled title/buttons render identically to
the pre-refactor screenshot); wasm app build; ios and linux `libgui.a`;
android verified one step beyond last session — full `libmelody.so` links
from a deduped ninja file and `javap` signatures match the JNI strings
byte-for-byte (APK still blocked by the pre-existing emit bug, task #10).
readme.org/todo.org updated (per-component framing + extended matrix).

## Kludges

- **winui remains compile-unverified** — win-pilot still unreachable. The
  conversion was double-checked against headers by grep, but no MSVC/clang
  pass. Build `worktree-style-per-component` there first thing.
- **Android ninja dedupe verification** (first-wins filter over the generated
  build.ninja) is a workaround for the pre-existing task #10 emitter bug, not
  a build path a user can run.
- The headers were transformed by a throwaway python script (uniform
  insertions), then clang-formatted; one-time tooling, not committed.
- clang-format mangles `=>`/`===` inside EM_JS; the dom agent repaired those
  tokens by hand after formatting (same as last session).

## CLAUDE.md suggestions

- None new; the win32-box availability note from the previous writeup stands.

## Suggestions

- Per-component styles make a future theming table naturally typed: a
  `Mel_Theme` of per-widget default styles applied at create.
- dom slider thumb needs injected per-element stylesheet rules
  (`::-webkit-slider-thumb`) — worth doing when the drawn family raises the
  styling bar.
- The `Mel_Font`/`Mel_Style_Color` primitives are good candidates for reuse
  by the paint module's text API when font selection lands there.
