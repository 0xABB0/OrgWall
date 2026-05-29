# Platforms & backends

Operational reference for the platform, UI-backend, GPU-backend and runtime axes,
the web toolchains, and GPU backend selection. Design notes live in
`writeup/web-platform-support.md` and `writeup/gpu-support.md`.

## Selecting a variant

A build resolves four axes, bound once from the root target and applied to the
whole dependency graph. Precedence is CLI > target override (in `build.c`) >
framework default.

- **Platform** — the positional argument, spelled `platform[:backend[:runtime]]`.
  An empty axis field falls back to that axis's default, so `web::wasi` keeps the
  default backend but forces the wasi runtime. Platform defaults to the host.
  Canonical names: `macos`, `ios`, `linux`, `android`, `win32`, `web`.
- **UI backend** — the second positional field. Defaults: `cocoa` (macOS),
  `uikit` (iOS), `androidnative` (Android), `winui` (win32), `dom` (web); Linux
  has none (headless). Override in `build.c` with
  `mel_build_use_backend_on(t, p, name)`.
- **Runtime** — the third positional field. `native` everywhere except web, whose
  default is `emscripten` (the other is `wasi`). Override with
  `mel_build_use_runtime_on(t, p, name)`.
- **GPU backend** — the `--gpu <metal|vulkan|dx12|webgpu>` flag (also
  `--gpu=<…>`), independent of the UI backend. Defaults: `metal` (macOS/iOS),
  `vulkan` (Linux/Android), `dx12` (win32), `webgpu` (web). Override with
  `mel_build_use_gpu_backend_on(t, p, name)`. Per-platform validity: macOS accepts
  `metal` or `vulkan`, win32 accepts `dx12` or `vulkan`, the rest accept only
  their default.

`--release`/`--debug` may appear anywhere on the line (debug is the default). The
resolved variant is logged as `platform/backend/gpu/runtime`, e.g.
`macos/cocoa/metal/native`, with `+threading`/`+asyncify` appended when on, and
`headless`/`no-gpu` shown for an absent backend.

### How the axes reach sources

Each module's `src/` is collected as common sources plus, additively, the
subdirectory named for each active axis value: `src/<backend>/`,
`src/<gpu_backend>/`, and a platform/runtime **chain** in which a more specific
directory shadows a more general one. The web chains are `emscripten → web →
posix` and `wasi → web → posix`; thus `src/emscripten/` is Emscripten-only,
`src/wasi/` wasi-only, `src/web/` web-common, both falling back to `posix`.

### Per-axis build properties

Flags can be gated to one axis value so they never reach a toolchain that rejects
them:

- platform-gated: `mel_build_add_{cflag,include,define,link_flag}_on(t, vis, p, …)`
- runtime-gated: `mel_build_add_{cflag,link_flag}_on_runtime(t, vis, rt, …)`
- gpu-gated: `mel_build_add_{cflag,link_flag}_on_gpu(t, vis, gpu, …)`

Whole modules or single sources are dropped per platform or runtime with
`mel_build_exclude_module_on`, `mel_build_exclude_module_on_runtime`, and
`mel_build_exclude_source_on`.

## Web platform (Emscripten and WASI)

The web is one platform (`web`) with two genuinely different wasm runtimes.
Emscripten (the default) gives a POSIX-ish environment with JS interop and a live
event loop, and runs a real DOM GUI in the browser. wasi (wasi-sdk) is a
standards-only compute runtime with no DOM and no JS interop, run under a WASI
host. `core/platform.h` sets `MEL_PLATFORM_WEB` for both and additionally
`MEL_PLATFORM_EMSCRIPTEN` or `MEL_PLATFORM_WASI`.

Pick the runtime in `build.c` — `mel_build_use_runtime_on(t, MEL_PLATFORM_WEB,
"wasi")` — or on the CLI with the runtime field, `web::wasi`. Two feature knobs,
both off by default (smallest viable GUI app):

    mel_build_web_threading(t, true);   // Workers + SharedArrayBuffer; needs COOP/COEP
    mel_build_web_asyncify(t, true);    // Asyncify; ~doubles wasm size

Toolchain routing: Emscripten uses `emcc`/`emar` (must be on PATH); wasi uses the
wasi-sdk `clang`/`llvm-ar` found at `$WASI_SDK_PATH` (else `~/wasi-sdk`), adding
`--target=wasm32-wasip1` and `--sysroot=<wasi-sdk>/share/wasi-sysroot`. Both must
support C23. `-fPIC` is dropped on web and `-D_GNU_SOURCE` is added (musl gates
POSIX/GNU symbols that strict `-std=c23` leaves undefined).

### Emscripten (GUI in the browser) — default

    ./nob build hello-world-gui web                 # debug
    ./nob build hello-world-gui web --release
    ./nob run   hello-world-gui web                 # build, serve at :8080, open browser

Output: `build/<target>/web/<config>/app.{html,js,wasm}`. The link uses
`tools/build/web/shell.html` (a minimal shell with a `#mel-root` container) and
`-sALLOW_MEMORY_GROWTH=1`, adding `-sASYNCIFY` / `-pthread -sPTHREAD_POOL_SIZE`
when those knobs are on.

Serve manually (different port, or to keep it running):

    python3 tools/build/web/serve.py build/<target>/web/<config> 8080 0
    # open http://localhost:8080/app.html
    # final arg: "1" sets COOP/COEP (use when web threading is on); 0/omitted = off

Deploy: `build/<target>/web/<config>/` is a ready-to-host static bundle. With web
threading on, the host must send `Cross-Origin-Opener-Policy: same-origin` and
`Cross-Origin-Embedder-Policy: require-corp`.

### WASI (compute module)

Set `mel_build_use_runtime_on(t, MEL_PLATFORM_WEB, "wasi")`. Example target:
`melody-wasi`.

    ./nob build melody-wasi web                     # -> build/melody-wasi/web/debug/app.wasm

The output is a bare `app.wasm`; run it under a WASI host (`nob run … web` is
browser-only and will redirect you here for a wasi target):

    wasmtime build/melody-wasi/web/debug/app.wasm
    # or under node:wasi, passing the module to a small preview1 runner

### What builds where on web

- `gmp`, `mpfr` and `sqlite3` cross-compile to both wasm runtimes, so the
  arbitrary-precision math, `music.theory`/`midi`, and the sqlite log sink
  build on web.
- `mongoose` and `sdl3` are native-only; the `server` module (which needs a
  listening socket) is excluded on web.
- wasi is DOM-less and surface-less, so the `gui` and `gpu` modules are excluded
  under the wasi runtime; a non-GUI wasi app never references them and still
  links.
- The Emscripten DOM GUI backend is `modules/gui/src/emscripten/`; the wasi
  reactor backend is `modules/reactor/src/wasi/`.

Note: both toolchains share `build/web/<config>/libmelody.a`, so switching
runtimes recompiles melody. The object cache is keyed on the compiler identity,
so the result is correct, just not co-resident.

## Codegen — enum-to-string

A target may have the engine synthesize `str8 <EnumType>_to_string(<EnumType>)`
from annotated enums, sparing the hand-written `switch`. The declaration is a real
prototype in the enum's own header (so the LSP sees it with no generated header to
include); codegen emits only the `.c` that implements it. Opt in from `build.c`:

    mel_build_generate_enum_str(t, "display/display.h");   // one or more include-spellings

Annotate the enum and declare its to-string in the header (`#include <reflect/enum.h>`):

    typedef enum {
        MEL_DISPLAY_CONNECTOR_UNKNOWN     MEL_SKIP = 0,        // no case emitted
        MEL_DISPLAY_CONNECTOR_INTERNAL    MEL_STR("Internal"), // explicit label
        MEL_DISPLAY_CONNECTOR_HDMI,                            // default label: "HDMI"
        MEL_DISPLAY_CONNECTOR_USB_C       MEL_STR("USB-C"),
    } Mel_Display_Connector;
    MEL_ENUM_TO_STRING_DEFAULT(Mel_Display_Connector, "Unknown");

- `MEL_ENUM_TO_STRING(EnumType)` / `MEL_ENUM_TO_STRING_DEFAULT(EnumType, default)`
  declare `str8 EnumType_to_string(EnumType)` and mark it for codegen. The
  default is the `switch`'s `default:` return (`"?"` for the plain form). On a
  non-clang compiler the macro still declares the prototype; the annotation
  vanishes.
- `MEL_STR(label)` overrides one constant's string; with no override the label is
  the constant spelling minus the enum's common prefix (verbatim, so `HDMI`, not
  `Hdmi`). `MEL_SKIP` omits the constant (sentinels, `*_COUNT`). Both sit on the
  enumerator, before any `= initializer`.

The annotations are `__attribute__((annotate(...)))` with zero runtime cost. Source
of truth is the enum itself, so a new constant cannot drift from its string table.

Mechanics: `tools/codegen/enum_str_gen.c` is a libclang tool that parses the named
headers, finds the annotated function declarations, recovers each enum from the
parameter type, and emits `enum_to_string.generated.c` under
`build/<variant>/<config>/<target>/generated/`, added to the target's sources.
Returned `str8`s are plain literal-backed struct literals — no `S8`/string-module
dependency. Regeneration is mtime-gated on the headers and the tool. The
implementation is compiled into the opting target, so exactly one target per link
closure may own a given enum's strings — opt in from the consumer, not the library
whose header declares the enum, unless that library wants to export them.

The tool links libclang via the C API (`clang-c/Index.h`), which Xcode ships only
as the `libclang.dylib` binary — not the header. The header lives in a full LLVM
install; the build looks under `/opt/homebrew/opt/llvm` (Homebrew, mirroring the
macOS Vulkan prefix above) or `$MEL_LIBCLANG_PREFIX`. On macOS it parses against
the host SDK (`xcrun --show-sdk-path`) plus libclang's own builtin-include dir.
This is a host build-tool dependency; install with `brew install llvm`.

## GPU rendering

The `gpu` module is a backend-agnostic abstraction (device, swapchain, buffer,
shader, pipeline, command/frame, reactor render source) over opaque handles. The
backend is chosen by the GPU axis described above — `--gpu <backend>` or
`mel_build_use_gpu_backend_on(t, p, name)` — independent of the UI backend.
Sources live in `modules/gpu/src/<backend>/`; only the selected one compiles.

Implemented: **Metal** (macOS/iOS), **WebGPU** (web, via the Emscripten
`emdawnwebgpu` port), **Vulkan** (Linux/Android natively, macOS over MoltenVK).
**DX12 is not implemented** — selecting it (the win32 default) compiles no real
backend.

A GPU surface is a GUI component, `mel_gpu_view` (not a window): it hosts a
swapchain inside an ordinary frame beside native widgets, implemented for the
cocoa, dom and androidnative GUI backends. `mel_gpu_view_surface` returns the
platform handle the swapchain attaches to (an `NSView*`, a `<canvas>` selector
string, or an `ANativeWindow*`); gui and gpu have no compile-time dependency on
each other, so the app glues them. The render loop is a reactor source
(`mel_gpu_render_source_new`).

Example — `apps/hello-gpu`, a native window whose buttons each open a top-level
window hosting a graphical app rendered into a `mel_gpu_view`:

    ./nob run hello-gpu macos                       # Metal
    ./nob run hello-gpu macos --gpu vulkan          # Vulkan via MoltenVK
    ./nob run hello-gpu web                          # WebGPU (emdawnwebgpu) in the browser
    ./nob run hello-gpu android                      # Vulkan on device

### GPU build plumbing

In `modules/build.c`:

- `--use-port=emdawnwebgpu` (cflag + link flag) and the `--pre-js
  modules/gpu/src/webgpu/device_preinit.js` are runtime-gated to `emscripten`, so
  they never reach the wasi-sdk clang. The pre-js acquires the WebGPU
  adapter/device behind an `addRunDependency` gate to keep device creation
  synchronous.
- `-lvulkan` is gpu-gated to `vulkan`. The loader comes from the NDK on Android,
  Homebrew on macOS (MoltenVK is the driver), system paths on Linux.
- macOS Vulkan headers/loader are found through a hardcoded Homebrew prefix
  (`-I/opt/homebrew/include`, `-L/opt/homebrew/lib`), kept to macOS so it never
  shadows the NDK's Vulkan headers on Android. This is Apple-Silicon Homebrew
  only — no SDK discovery yet.
- `surface.m` (the Apple Objective-C Vulkan surface) is excluded on Android and
  Linux; those use `android_surface.c` / a window-system surface instead.

### GPU toolchain prerequisites

- macOS Vulkan: Homebrew `vulkan-loader` + `molten-vk`.
- Web WebGPU: `emcc` with the `emdawnwebgpu` port (fetched by `--use-port`) and a
  WebGPU-capable browser.
- Android: NDK with API ≥ 24 (`libvulkan.so` first ships at android-24; the
  native floor was raised to 24 for all Android apps).

Shaders are authored per backend for now — MSL (Metal), WGSL (WebGPU), and HLSL
compiled to SPIR-V with `glslc` (Vulkan) — until Slang is integrated.
