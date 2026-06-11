# Module axis-folder restructure

## Work done

Reorganized module/app source layout from `src/<axis>/…` + `include/<m>/<axis>/…`
to a hoisted per-axis folder:

    <m>/
      include/<m>/        # common public headers
      src/                # common sources + internal headers
      <axis>/
        include/<m>/<axis>/   # axis public headers (consumed unchanged as <m/axis/...>)
        src/                  # axis sources + internal headers
        AndroidManifest.xml, java/   # axis resources at the axis root

"Axis" = a `src/` subdir gated by a non-`ALWAYS` `WHEN(...)` in the module's
`build.c` (platforms, gpu backends, backends, runtimes). Organizational subdirs
gated `ALWAYS` (e.g. `compress/src/<codec>`, `barcode/src/decode`, `image/src/gpu`,
`fs/src/posix` shared `.inl`) were left under `src/` by design.

Driven by three throwaway scripts (classify → move → rewrite), then verified by build:

- **Files** moved with `git mv` (355 renames). Axis sources+internal headers →
  `<axis>/src/`; `AndroidManifest.xml` and `java/` → `<axis>/`; public axis headers →
  `<axis>/include/<m>/<axis>/`.
- **build.c** rewritten: `"src/<axis>/…"` → `"<axis>/src/…"`; android manifest/java
  paths relocated; a `WHEN`-gated `mel_includes(t, MEL_PUBLIC, …, "<axis>/include")`
  inserted next to each axis's first source line, for every target that compiles it
  (libs and recompiling tests alike). `mel_exclude_source` and nested gpu backend
  globs (`metal/src/ios`, `vulkan/src/windows`, …) handled by the same path rewrite.
- **Source includes** in hoisted depth-1 files rewritten `#include "../X"` →
  `#include "../../src/X"` (depth changed by one). Nested files (gpu `metal/src/ios`
  etc.) keep `../` — they move as a self-contained subtree. `fs`'s cross-dir
  `"../posix/fs_posix_ops.inl"` correctly became `"../../src/posix/…"` since `posix`
  stays in `src/`.
- **Docs** (`readme/spec/todo`) path references in prose updated by the same transform.
- **CLAUDE.md** + **modules/build/platforms.md** updated to describe the new layout
  (Gabbo explicitly requested the CLAUDE.md update as part of the task).

### Skipped (per Gabbo's "skip & report" call on misfits)

- **gui** — its `include/gui/` subdirs (`appkit`, `controls`, `layouts`, `win32`) do
  not name-match its src axes (`cocoa`, `dom`, `uikit`, `winui`, `xcb`); the include
  side cannot be hoisted 1:1. Left entirely untouched.
- **fiber** — only axis is `src/asm`, one folder shared across 5 `platform+arch`
  selectors; the folder name is not a single axis. Left untouched. (Note: it *would*
  hoist cleanly to `fiber/asm/src/` if you want it — say the word.)
- **compress / barcode / image** — no WHEN-gated axes; their subdirs are
  organizational and stayed under `src/` (correct, not a regression).
- **display / midi** — orphan `src/emscripten`, `src/wasi` dirs that no `build.c`
  references were left in place (dead, not part of any build).

## Verification

- **macos host**: built platform, display, geolocation, window, cpu, gpu (metal+webgpu,
  272 files), fs, net, port, boot, tray, notification, storage, camera, gamepad, hid,
  input; apps melody-showcase, music-companion, hello-gpu, midi-monitor; app display-gui.
  Ran `nob test platform-core` (recompiles the macos axis + uses no hoisted include) → 11/11 pass.
- **wasm**: fs, net, cpu, display → OK (exercises web/wasm axes + the `../../src/` rewrite).
- **linux (zig cross)**: ~20 modules OK, exercising the hoisted `<axis>/include` `-I`
  emission and `../../src/` rewrites. The only failures (hid `libudev.h`, dialog/
  notification `dbus/dbus.h`, display `X11/Xlib.h`) are missing **system** dev headers
  in the cross-sysroot — pre-existing/environmental; every module-local path resolves
  (compiler reaches the system `#include`, well past all local ones).
- **Not compiled here**: win32, android, ios (need the remote box / SDKs). They use the
  identical mechanical transform validated on macos/linux/wasm — same path rewrite, same
  include hoist, same `../../src/` edit. Recommend a remote `win-pilot` build to close
  the loop on the win32 axes (esp. the `win32/include` hoists in platform/display/tray).

## Kludges

- None in the shipped tree. The transform was scripted but the scripts are throwaway
  (lived in the job tmp dir, not committed); the repo carries only the resulting moves
  and edits. No fixed arrays, enums, comments, or `mel_malloc` introduced — this was
  pure relocation + path rewriting.
- One transient bug caught in dry-run before any write: the doc rewriter first turned
  `src/android/java` into `android/src/java`; fixed to special-case java/manifest
  (matching the build.c logic) before applying.

## CLAUDE.md suggestions (recommendations only)

- The repo now has a hard convention worth stating explicitly in CLAUDE.md: **a
  `src/` subdir is hoisted to a top-level `<axis>/` folder iff it is WHEN-gated in
  build.c; ALWAYS-gated subdirs stay under `src/`.** I added a layout block to the
  "Sources & modules" section already; consider promoting the WHEN-gated rule to a
  one-line invariant so future modules don't drift.
- Consider a lint in the build discovery that flags a `src/<dir>/` containing
  compiled sources gated by `WHEN` (i.e. an un-hoisted axis) so the convention is
  enforced mechanically (MEL-ENGINE-VIII: fail loudly on drift).

## Suggestions

- **gui** deserves a deliberate pass: rename its include subdirs to match its src
  axes (`appkit`→`cocoa`, `win32`→`winui`?) and split `controls`/`layouts` out as
  non-axis common headers, then it can hoist like the rest.
- **fiber/asm**: trivially hoistable to `fiber/asm/src/`; do it for uniformity unless
  you prefer keeping assembly visually distinct.
- **display/midi orphans**: `src/emscripten` and `src/wasi` are referenced by no
  build.c — either wire them up or delete them; right now they're dead weight.
