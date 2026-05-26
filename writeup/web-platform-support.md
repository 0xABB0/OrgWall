# Web Platform Support: Emscripten + WASI

This documents the work that brought Melody to the web as a first-class
platform with two toolchains — Emscripten (DOM, runs a real GUI in the browser)
and wasi-sdk (standards-only wasm, compute targets) — plus the build-system
plumbing, the runtime backends, and the verification.

It implements PRD 12 (web platform support) on top of the PRD 10 build system.

## What was delivered

- The build framework targets the web with either toolchain, selected per
  target from `build.c`. Compile, archive, link and package all route to the
  right tools (`emcc`/`emar` or the wasi-sdk `clang`/`llvm-ar`).
- `hello-world-gui` compiles to a wasm bundle and runs in a browser: every
  widget is a real DOM element, layout is the same C layout engine, and input
  events round-trip back into the C capability callbacks. Verified end to end
  in headless Chromium.
- A `melody-wasi` compute target compiles the web-supporting subset of melody
  under wasi-sdk to a standalone `app.wasm` and runs under a WASI host
  (`node:wasi`), exercising the heap allocator, slotmap, and str8.

## The two toolchains, and why both exist

Emscripten and wasi-sdk are not interchangeable; they are different runtimes
that happen to both produce wasm.

Emscripten is the path for GUI apps. It gives a POSIX-ish environment, JS
interop (so C can create and drive DOM nodes), the browser event loop, and a
"live runtime" model where the wasm module stays resident and reacts to DOM
events. This is what makes `hello-world-gui` actually render and respond.

wasi-sdk is the path for compute. It is a cleaner standards story and produces
smaller modules, but it has no DOM and no JS interop — it is meant to run under
a WASI host (wasmtime, `node:wasi`) like a command-line program. A GUI app
cannot run under wasi without DOM polyfills, which are out of scope; wasi's job
here is to compile and run the non-GUI parts of melody.

Default toolchain is Emscripten (smallest viable GUI app: single-threaded, no
Asyncify). A target opts into wasi or flips feature knobs in its `build.c`.

## Platform model: one platform, a toolchain sub-axis

`MEL_PLATFORM_WEB` is a single build platform, but the two toolchains are
genuinely different runtimes, so the code distinguishes them.

`core/platform.h` now sets `MEL_PLATFORM_WEB` for both `__EMSCRIPTEN__` and
`__wasi__`, and additionally sets one of two finer flags:

- `MEL_PLATFORM_EMSCRIPTEN` under `__EMSCRIPTEN__`
- `MEL_PLATFORM_WASI` under `__wasi__`

Runtime code that is common to the web uses `MEL_PLATFORM_WEB`; code that is
specific to one runtime uses the finer flag.

### Source chains

The build resolves per-platform sources through a "chain" of subdirectories
where a more specific directory shadows a more general one. The web platform's
chain depends on the selected toolchain:

- Emscripten: `emscripten` then `web` then `posix`
- wasi: `wasi` then `web` then `posix`

So `modules/<m>/src/emscripten/` is Emscripten-only, `modules/<m>/src/wasi/` is
wasi-only, and `modules/<m>/src/web/` (if present) is web-common. Both pick up
`posix` as the base. This is why the Emscripten DOM GUI backend lives in
`modules/gui/src/emscripten/` and wasi, which has no DOM, simply gets no GUI
backend (a non-GUI wasi app never references it, so the library still links).

## Build-system changes (`tools/build/`)

### Web configuration API (`build.h`, `build.c`)

Three setters, callable inside a target's `project()`:

- `mel_build_web_toolchain(t, "emscripten" | "wasi-sdk")` — toolchain choice.
- `mel_build_web_threading(t, bool)` — real Workers + SharedArrayBuffer; needs
  COOP/COEP headers (the dev server sets them).
- `mel_build_web_asyncify(t, bool)` — Asyncify for blocking C into JS async I/O.

The toolchain and feature knobs are resolved once from the root target at the
start of a build (in `mel_build_main`) and bind the whole dependency graph, so
melody compiles with the same toolchain as the app linking it.

### Toolchain routing (`runner.c`)

- Compiler: `emcc` (Emscripten) or `<wasi-sdk>/bin/clang` (wasi), else `clang`.
- Archiver: `emar` or `<wasi-sdk>/bin/llvm-ar`, else the native `ar`.
- wasi compile/link add `--target=wasm32-wasip1` and
  `--sysroot=<wasi-sdk>/share/wasi-sysroot`. Emscripten needs none (emcc drives
  target selection itself).
- `-fPIC` is dropped on web (it means SIDE_MODULE to emscripten and is rejected
  by wasi clang).
- `-D_GNU_SOURCE` is added on web: musl (the libc behind both toolchains) gates
  POSIX/GNU symbols behind feature macros that strict `-std=c23` leaves
  undefined; the desktop Apple/glibc headers expose them anyway, so this only
  bites on web.
- wasi-sdk location: `$WASI_SDK_PATH`, else `~/wasi-sdk`.

### Link, output layout, run

Web bundles are written target-first so the wasm, loader, and shell sit
together ready to host:

- `build/<target>/web/<config>/app.html` + `app.js` + `app.wasm` (Emscripten)
- `build/<target>/web/<config>/app.wasm` (wasi; bare module)

The Emscripten link uses `tools/build/web/shell.html` (a minimal shell with a
`#mel-root` container), `-sALLOW_MEMORY_GROWTH=1`, and adds `-sASYNCIFY` /
`-pthread -sPTHREAD_POOL_SIZE` when those flags are on.

`nob run <target> web` serves the bundle with `tools/build/web/serve.py` (which
sets COOP/COEP when threading is enabled) and opens the browser. For a wasi
target it explains that the module must run under a WASI host instead.

### Excluding web-incompatible code

melody depends on native third-party libraries that do not build for wasm
(sdl3, mongoose, sqlite3, mpfr, gmp). Those targets are now declared
native-only (`mel_build_set_platforms` without web), so the graph never tries
to build them for wasm and never adds their link flags on web.

The melody modules and individual sources that need them are excluded on web in
`modules/build.c`: the `server`, `music.theory`, and `music.midi` modules, and
the `real.c`, `frequency.c`, and `log.sink.sqlite.c` sources. Their use of GMP,
MPFR, SQLite and mongoose is fully contained; nothing in the gui/app/core/
reactor path touches them.

## Runtime changes

### Reactor

The reactor backend is selected by the finer platform flag: Emscripten includes
the web backend, wasi includes a new wasi backend, everything else stays as it
was.

`reactor/src/wasi/reactor_backend.inl` is single-threaded and has no
`pipe()`/eventfd, so wake is a no-op (nothing to wake) and `wait` uses `poll()`
(wasi-libc backs it with `poll_oneoff`; with no fds it degrades to a timeout
sleep). wasi runs the reactor in normal blocking (THREADED) mode, like a native
CLI.

`mel_reactor_spawn(MEL_REACTOR_THREADED, ...)` routes to the main-loop backend
only under Emscripten — a blocking run would freeze the browser tab, so the
reactor hands the browser one iteration at a time and returns control while
keeping the module alive. wasi takes the normal blocking path.

(The Emscripten web reactor backend itself is an event-driven model — rAF for
idle/fast sources, setTimeout for timer deadlines, nothing when idle — owned in
`reactor/src/web/reactor_backend.inl`.)

### Thread / futex

`thread/src/posix/futex.c` gained a wasi branch: wasm32-wasip1 is
single-threaded, so wait returns and wake does nothing (no sibling thread can
change the value or be woken).

### The Emscripten DOM GUI backend (`modules/gui/src/emscripten/`)

This mirrors the win32 backend exactly in shape: each widget creates a real
native object — here a DOM element via `EM_JS` — and stores the element's
integer id (an index into a JS-side registry) in `node->native`. Container
widgets put their content host id in `node->content` so children parent into
the right box. Per-element callback state lives in a C-side control record
indexed by id. DOM event listeners call exported `mel_web__ev_*` dispatchers,
which fire the stored capability callbacks. The painter wraps a `<canvas>` id
and each op draws onto its 2D context.

Files:

- `backend.c` — JS element registry and DOM ops, the C control registry, the
  event dispatchers, the backend init/destroy hooks, the generic widget ops
  (`set_text`/`get_text`/`set_bounds`/`set_visible`/`set_enabled`/`set_focus`/
  `invalidate`), and screen navigation.
- `frame.c` — top-level frame (a `<div>` in `#mel-root`).
- `controls.c` — label, button, textfield, checkbox, slider.
- `containers.c` — panel, groupbox (fieldset + legend + body), scrollview
  (overflow div + sized inner content).
- `canvas.c` — canvas widget and the painter ops on the 2D context.
- `structural.c` — tabview/tab, splitter/splitpane, dialog.
- `web.h` — the shared internal header.

The element mapping is the natural one: a button is a `<button>`, a label a
`<div>`, a textfield an `<input>`, a checkbox an `<input type=checkbox>` inside
a `<label>`, a slider an `<input type=range>`, a canvas a `<canvas>`. The same
C layout engine positions everything absolutely.

## How to build, run, deploy, open

Prerequisites: Emscripten (`emcc`/`emar` on PATH) and wasi-sdk (in `~/wasi-sdk`
or `$WASI_SDK_PATH`). Both must support C23.

### Emscripten (GUI in the browser)

Default web toolchain; nothing extra needed in `build.c`.

    ./nob build hello-world-gui web              # debug
    ./nob build hello-world-gui web --release    # release
    ./nob run   hello-world-gui web              # build, serve, open browser

Output: `build/hello-world-gui/web/<config>/app.{html,js,wasm}`.

To serve manually (e.g. for a different port or to keep it running):

    python3 tools/build/web/serve.py build/hello-world-gui/web/debug 8080 0
    # then open http://localhost:8080/app.html
    # the trailing 0 is "cross-origin isolation off"; pass 1 when web threading
    # is enabled (sets COOP/COEP for SharedArrayBuffer)

Deploy: the contents of `build/<target>/web/<config>/` are a ready-to-host
static bundle. If web threading is on, the host must send
`Cross-Origin-Opener-Policy: same-origin` and
`Cross-Origin-Embedder-Policy: require-corp`.

### wasi-sdk (compute module)

A target opts in from its `build.c`:

    mel_build_web_toolchain(t, "wasi-sdk");

Then:

    ./nob build melody-wasi web                  # -> build/melody-wasi/web/debug/app.wasm

Run under a WASI host. With Node:

    node --experimental-wasi-unstable-preview1 - <<'EOF' build/melody-wasi/web/debug/app.wasm
    import { WASI } from 'node:wasi';
    import { readFile } from 'node:fs/promises';
    const wasi = new WASI({ version: 'preview1', args: ['app'], env: {} });
    const bytes = await readFile(process.argv[2]);
    const mod = await WebAssembly.compile(bytes);
    const inst = await WebAssembly.instantiate(mod, wasi.getImportObject());
    wasi.start(inst);
    EOF

Or with wasmtime: `wasmtime build/melody-wasi/web/debug/app.wasm`.

`nob run ... web` is browser-only and will tell you to use a WASI host for a
wasi target.

## Verification

- Emscripten end to end, headless Chromium (Playwright): `hello-world-gui`'s
  main screen renders one frame, four buttons with the correct C-defined
  labels, the labels including the live status line built from C state, the
  text/checkbox/range inputs, and the canvas. Clicking the tap button three
  times drives its label `Taps: 0` to `Taps: 3`, proving the full round-trip:
  DOM click -> JS dispatcher -> C `on_click` -> `mel_gui_set_text` -> DOM update.
- wasi: `melody-wasi` runs under `node:wasi` and prints
  `melody on wasi-sdk: 16 slots, sum of squares 0..15 = 1240`, exercising the
  heap allocator, slotmap, and str8.
- Native (macOS) compile stays clean — the web changes are guarded so they do
  not touch the desktop build.

## Known limitations and future work

- The melody library archive lives at `build/web/<config>/libmelody.a`, shared
  by both toolchains. Switching Emscripten <-> wasi recompiles melody (the
  object cache is keyed on the compiler identity, so it is correct, just not
  co-resident). Keying the library and object directories by toolchain would
  let both coexist without rebuilds.
- wasi has no DOM, so there is no GUI under wasi — by design. A WASI GUI would
  need DOM polyfills, which the PRD puts out of scope.
- Web threading and Asyncify are wired as build flags but not yet exercised by
  a target; the COOP/COEP serving path exists for when threading is turned on.
- The structural widgets (tabview, splitter, dialog) are implemented but only
  lightly exercised; the main and detail screens cover labels, buttons,
  textfield, checkbox, slider, canvas, panels, groupboxes and scrollviews.

## File inventory

Build framework:

- `tools/build/build.h`, `tools/build/build.c` — web config API.
- `tools/build/internal.h` — target struct fields for the web knobs.
- `tools/build/runner.c` — toolchain selection, source chains, web link/run.
- `tools/build/web/shell.html`, `tools/build/web/serve.py` — shell + dev server.

Runtime:

- `modules/core/include/core/platform.h` — web/emscripten/wasi platform flags.
- `modules/reactor/src/reactor.c` — backend selection, THREADED routing.
- `modules/reactor/src/wasi/reactor_backend.inl` — wasi reactor backend.
- `modules/thread/src/posix/futex.c` — wasi futex branch.
- `modules/gui/src/emscripten/` — the DOM GUI backend.
- `modules/build.c` — web module/source exclusions.
- `third-party/{gmp,mpfr,sqlite3,mongoose,sdl3}/build.c` — native-only.

Demo:

- `apps/melody-wasi/` — the wasi compute target.
