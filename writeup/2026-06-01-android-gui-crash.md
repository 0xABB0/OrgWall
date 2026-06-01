# Android GUI crash — root-caused on device, then emulator

## Symptom
`./nob run hello-world-gui android` installed and launched, then the activity died
immediately. Reported as "compiles and runs, GUI crashes."

## Work done

Three stacked defects, peeled by running on the device (later the emulator) and reading
logcat / tombstones. Each fix uncovered the next, previously-unreachable one.

### 1. Build broke before launch — `backtrace` on bionic
`modules/allocator/src/tracking.c` guarded the `execinfo.h`/`backtrace` path with
`#if defined(__APPLE__) || defined(__linux__)`. Android satisfies `__linux__`, but bionic
declares `backtrace` only from API 33; the NDK targets API 24 here, so the TU failed to
compile and the build stopped. Added an `__ANDROID__` arm enabling the path only at
`__ANDROID_API__ >= 33`, disabled below (degrade forward, MEL-ENGINE-VII).

### 2. Launch crash — JNI bridge symbols dead-stripped from `libmelody.so`
`UnsatisfiedLinkError: MelGui.nativeRegister(float)` at `MelGui.start` — the first native
call. `nm -D` showed all 16 widget `Java_*` symbols present but every `MelGui` bridge
symbol absent. The JNI entry points live in dedicated pure-JNI TUs
(`gui/.../backend.bridge.c`, `app/src/android/app.bridge.c`) that define no symbol any C
code references. Modules link as static archives; the linker pulls an archive member only
to resolve an undefined symbol, so those objects were dropped. The widgets survive only by
accident — each also exports a `mel_*_create_opt` the app references.

Fix is a declarative build API, not a heuristic:
- `mel_whole_archive(Mel_Target* t, Mel_When when)` — new API (`build.h`, `api.c`), stored
  as a `Mel_WhenVec` on the target (`internal.h`), mirroring `mel_unavailable`.
- `emit.c` — the executable link now wraps a dependency archive in
  `-Wl,--whole-archive … -Wl,--no-whole-archive` (or `-Wl,-force_load,<lib>` on apple ld)
  when any of the dep's `whole_archive` Whens matches the variant. To interleave linker
  flags with archives without poisoning ninja's `$in` (which must be real files), the
  `link`/`hostlink` rules gained a `$libs` variable: object files stay explicit inputs,
  archive paths move to implicit deps (`| …`) for rebuild tracking, and the flagged archive
  fragment goes in `$libs`. Listing an archive both whole-archived and (via deps) does not
  duplicate symbols — `--whole-archive` is balanced per-archive.
- `modules/gui/build.c`, `modules/app/build.c` — `mel_whole_archive(lib, WHEN(.platforms =
  MEL_ON(ANDROID)))`. Other platforms are unaffected (their Whens don't match; archives
  stay plain in `$libs`).

This was first prototyped as a filename heuristic (force-load any `*.bridge.c` object).
That was wrong — the build is ours to pilot, so the contract must be declared, not sniffed
from a suffix. Replaced before finalizing.

### 3. Post-launch JNI abort — `setText` on a `FrameLayout`
`-Xcheck:jni` aborted: *can't call TextView.setText on instance of FrameLayout*, from the
reactor loop under `nativePollOnce` while building the screen. A screen builder receives a
**screen** node (`mel_gui__screen_new` — a `MelPanel`/`FrameLayout`, `is_screen = true`,
not toplevel). cocoa/uikit/winui special-case `is_screen` in `mel_gui_set_text` (store
`screen_title`, push it to the toplevel window title); android never did and fell through to
`tv_setText` on the FrameLayout. The screen-title model (commit `89bc5f4`) landed without
updating the android backend.

Fix in `modules/gui/src/androidnative/backend.c`: handle `is_screen` in `mel_gui_set_text`
(store title, set the window title via `setTag`/`setActivityTitle`) and in `mel_gui_get_text`
(return `screen_title` via `str8_to_buf`), guarding the symmetric `tv_getText` abort.

## Verified
- Host (macOS) build of `hello-world-gui` green (the `$libs` rule change works on apple ld).
- Android on the emulator (`Medium_Phone_API_36.1`, via the new `./nob run android` boot
  path): app launches, all 5 `MelGui` JNI symbols present in the `.so`, GUI renders fully
  (title, buttons, edit="native ui", checkbox, slider=65, live status label, `get_text`
  line, canvas painting), process stays alive, no FATAL/JNI lines. Same result earlier on a
  physical device before it was detached.

## dom / web — attempted, blocked on missing runtime
I earlier claimed the web backend "can't be verified this session." That was false; I went
and tried. Findings (all in the committed tree):
- `dom/backend.c` and `dom/structural.c` had every `EM_JS` arrow/`===` mangled by a
  `clang format` pass (`=>`→`= >`, `===`→`== =`) — the glue JS would not even parse. Fixed
  the 16 sites; `node --check` is now clean.
- dom's `mel_gui_set_text` has the same `is_screen` gap: a screen's ctl kind defaults to
  `MEL_WEB_TEXT (0)`, so the title was written into the screen `div` instead of
  `document.title`. Fixed by parity (store `screen_title`, call `mel_web__el_title`).
- Could **not** run-verify: the web GUI does not start end-to-end. `posix/app.c` (the only
  `main` calling `mel_app_setup`) is gated to `MACOS|LINUX` and spawns a THREADED reactor;
  there is no wasm entry, and on wasm `time` (`nano.unix.c`) and `thread` (posix) are
  unwired and need emscripten feature macros / `-pthread`. Headless chromium confirmed:
  `Module` instantiates but `globalThis.MelWeb` never initializes — the app never runs.
  Wiring the wasm runtime (entry + time/thread + emscripten flags) is a separate buildout I
  did not take unprompted. The dom fixes above are therefore parity-/static-verified only.

## Kludges (MEL-ENGINE-VIII)
- The dom `EM_JS` fixes restore valid JS but do not address the root cause: `clang format`
  re-mangles `=>`/`===` inside `EM_JS` bodies (they are C-preprocessed tokens, not strings).
  The next format run re-breaks the web build. Durable fix needs either clang-format guards
  (a comment — forbidden here without your say-so) or moving the JS to a `--js-library`
  file. Left for a decision.
- `is_screen` handling is now duplicated across five backends; the divergence is exactly
  what let android (and dom) rot. Lifting it into common `gui` is the right move.

## CLAUDE.md suggestions (recommendations only)
- Document `mel_whole_archive` and the `*.bridge.c` JNI-entry-point convention in
  `modules/build/platforms.md`.

## Suggestions
- Lift the screen-title behaviour into common `gui` (store `screen_title` + one
  `set_window_title` backend hook) so a new backend cannot omit it.
- Add a CI smoke check that an Android `.so` exports the expected `Java_*` symbols
  (`nm -D`), catching JNI dead-stripping without a device.
- Protect `EM_JS` JS bodies from clang-format (guards or a `.js` library), and decide
  whether to complete the wasm runtime (entry + time/thread) so the web target runs.
