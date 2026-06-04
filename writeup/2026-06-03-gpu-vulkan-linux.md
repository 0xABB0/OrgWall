# GPU Vulkan on Linux — vendored headers, loader stub, surface lowering

Resolves the two round-4 linux/vulkan blockers: (a) the cross-compile could not find
`<vulkan/vulkan.h>`, and (b) the cross-link had no `libvulkan` to satisfy `-lvulkan`. Host:
Apple M3 Pro, macOS 26.2 (Darwin 25.2.0), zig 0.16.0 as the linux cross toolchain
(`zig cc -target x86_64-linux-gnu`).

## Work done

### 1. Vendored `third-party/vulkan-headers` (header-only)

KhronosGroup/Vulkan-Headers **v1.4.335** (matching the host MoltenVK loader version), C headers
only — `include/vulkan/*.h` + `include/vk_video/*.h`, plus `LICENSE.md` (Apache-2.0). The C++
`.hpp` headers and the registry XML are deliberately excluded; the engine is C-only.

`build.c` declares a `mel_add_third_party` with a single `mel_includes(MEL_PUBLIC,
WHEN(.gpu="vulkan", .platforms=MEL_ON(LINUX)), "include")`. A third-party with no sources/cmake/
prebuilt emits no archive (emit.c:212 short-circuits) — it exists purely to inject the include
path into dependents. The include is **linux-gated** so it never shadows the host Vulkan headers
on macOS (homebrew `/opt/homebrew/include`) or win32 (`$VULKAN_SDK/Include`).

This alone fixes the round-4 compile failure (`'vulkan/vulkan.h' file not found`).

### 2. Loader strategy: link a generated import stub with the real loader's soname

**Decision: link `-lvulkan` (mirroring macOS `-lvulkan` / win32 `-lvulkan-1`), NOT volk/dlopen.**
The shared vulkan core issues direct loader calls to ~108 entry points (vkCreateInstance, …);
volk-style dynamic dispatch would require rewriting that shared core onto a dispatch table — a
large, invasive shared-core change against the task's scope and against MEL-ENGINE-IX (no special
pleading). `-lvulkan` is the established convention on the two existing platforms; linux
uniformity honors MEL-ENGINE-IV.

The macOS cross host has no linux `libvulkan`, so the cross-**link** cannot resolve the loader
symbols. `third-party/vulkan-loader-stub` solves this: at build() time it generates a trivial
stub source by extracting every `VKAPI_CALL vkXxx` prototype from the vendored
`vulkan_core.h` + `vulkan_wayland.h` + `vulkan_xcb.h` (711 symbols), emits `void NAME(void){}`
for each, and builds a linux `.so` with zig — crucially `-Wl,-soname,libvulkan.so.1`. The
produced executable therefore records `DT_NEEDED libvulkan.so.1` and leaves the vk symbols as
undefined dynamic symbols (`U`). At runtime in the lavapipe container the **real** Mesa
`libvulkan.so.1` resolves them; the stub is link-time-only and is never loaded (its soname
matches the real loader, so the dynamic loader binds the real one). The symbol list is derived
from the vendored headers, so it cannot drift.

The generated `.so` lives under `third-party/vulkan-loader-stub/build/` (gitignored) — only the
generator `build.c` is committed; the artifact is reproducible from committed inputs. The
`-L<stubdir>` flag is injected `WHEN(.gpu="vulkan", .platforms=MEL_ON(LINUX))`.

### 3. Linux Vulkan surface (`modules/gpu/src/vulkan/linux/`)

`surface.c` + `surface.h` mirror the win32/macos lowerings. Wayland is preferred, XCB is the
fallback, selected by **presence** of the native-handle fields (no enum/tag — honors
MEL-CODE-001; degradation honors MEL-ENGINE-VII). The native handle is a
`Mel_Gpu_Linux_Native` struct `{ wl_display, wl_surface, xcb_connection, xcb_window }` that a
future linux windowing backend fills. `vulkan_wayland.h`/`vulkan_xcb.h` are included directly
(not via the platform-gated `vulkan.h`), and the three XCB typedefs (`xcb_connection_t`,
`xcb_window_t`, `xcb_visualid_t`) are provided locally so no system Wayland/XCB headers are
needed in the cross sysroot. Failure path logs loudly and returns `VK_NULL_HANDLE`
(MEL-ENGINE-VIII).

## Build commands and results

- `./nob configure hello-gpu linux --gpu=vulkan` → **OK**.
- `./nob compile hello-gpu linux --gpu=vulkan` → **all GPU/Vulkan sources compile**, including
  `src/vulkan/linux/surface.c`. (Round-4 failed every vulkan TU here.)
- All 7 gpu test suites `./nob build <suite> linux --gpu=vulkan`
  (gpu-foundation/resources/vulkan/stress/concurrency/visual/bench) → **compile clean; zero
  Vulkan-undefined symbols.** Each links down to a single non-GPU undefined symbol,
  `mel__platform_stacktrace_capture`.
- **GPU/Vulkan link proven green:** with only that one missing platform symbol stubbed,
  `gpu-visual` links to a complete x86-64 linux ELF; `readelf`/`nm -D` confirm
  `DT_NEEDED libvulkan.so.1` and `U vkCreateInstance`.
- **macOS regression (required):** `DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1 ./nob
  test gpu-vulkan macos --gpu=vulkan` → **48 passed, 0 failed, 0 skipped.** No regression.

## Out-of-scope blockers surfaced (NOT mine to fix)

The GPU/Vulkan layer is complete for linux. Full linux link of the gpu suites and of hello-gpu is
blocked **only** by missing linux **platform backends in other modules**:

- `modules/debug`: no linux stacktrace backend. `mel__platform_stacktrace_capture` is defined for
  macos/windows/android (`src/<platform>/stacktrace.c`) but not linux. Blocks every linux binary
  that links `debug` (i.e. all gpu test suites).
- `modules/gui`: no linux windowing backend. hello-gpu additionally needs 16 gui/window symbols
  (`mel_gpu_view_surface`, `mel_gpu_view_create_opt`, `mel_gui__backend_init`, `mel_frame_create_opt`,
  `mel_label_create_opt`, `mel_button_create_opt`, `mel_gui__nav_*`, `mel_gui__screen_new`,
  `mel_gui_set_*`, `mel_gui_supports_multi_root`, …) — none implemented for linux. `mel_gpu_view_*`
  live in `modules/gui/src/<backend>/gpu_view.*`, not the gpu module.

These are orthogonal to Vulkan and owned by the gui/debug/platform agents.

## RUN status (Docker) — environmentally deferred, honestly (MEL-ENGINE-VIII)

`docker version` → **`docker not found`** on this mac host. The RUN + golden-diff is therefore
deferred, not faked. It is additionally gated on the `modules/debug` linux backend above (the
headless gpu-visual binary cannot link until that symbol exists, in the container or out).

Goldens are **backend-agnostic by filename** (`modules/gpu/test/golden/*.ppm`, 13 references,
no per-backend suffix); a lavapipe run diffs against the same macОС-MoltenVK references. The
checker (`modules/gpu/test/img_golden.c`) reports `max_delta` (max per-channel abs diff) and the
offending-pixel count/fraction on mismatch — those are the deltas to capture. Cross-backend
deltas (MoltenVK→lavapipe) are expected; `MEL_GPU_GOLDEN_UPDATE` stays **off** and goldens are
**not** modified.

### Reproducible Docker recipe (runs once Docker is present and the debug linux backend lands)

```dockerfile
# Dockerfile.lavapipe — linux/amd64 software-Vulkan run host
FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends \
      mesa-vulkan-drivers vulkan-tools libvulkan1 \
      curl xz-utils git ca-certificates clang \
 && rm -rf /var/lib/apt/lists/*
# zig 0.16 (the linux cross toolchain nob uses even on a linux host)
RUN curl -fSL https://ziglang.org/download/0.16.0/zig-linux-x86_64-0.16.0.tar.xz \
      | tar -xJ -C /opt && ln -s /opt/zig-linux-x86_64-0.16.0/zig /usr/local/bin/zig
ENV VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json
ENV LIBGL_ALWAYS_SOFTWARE=1
WORKDIR /src
```

```bash
# from repo root on a Docker-capable host
docker build -f Dockerfile.lavapipe -t mel-lavapipe .
docker run --rm -v "$PWD":/src mel-lavapipe bash -lc '
  clang -std=c23 -g -Imodules/build -o nob nob.c &&
  ./nob test gpu-visual linux --gpu=vulkan -- --no-fork ;
  # repeat for hello-gpu once a linux gui backend exists:
  # ./nob run hello-gpu linux --gpu=vulkan
'
# lavapipe verification: `vulkaninfo | grep -i llvmpipe` must show the lavapipe device.
# Expect golden FAILs with reported max_delta/offending counts (cross-backend). Do NOT set
# MEL_GPU_GOLDEN_UPDATE. Record the per-image deltas; do not commit new goldens.
```

## Shared-file edits (flagged)

- `modules/gpu/src/vulkan/surface.c` (shared dispatch) — **minimal surface-dispatch hook only**:
  added a `#elif defined(__linux__)` branch calling `mel_gpu__vk_create_linux_surface`, plus a
  guarded `#include "linux/surface.h"`. No other shared core touched.
- `modules/gpu/build.c` — **additive linux+vulkan lines only** (linux surface sources, deps on
  `vulkan-headers` + `vulkan-loader-stub`, `-lvulkan`). The two `mel_depends` edges are
  unconditional (the API has no conditional-depends form); both deps are no-ops on non-linux
  (vulkan-headers' include and the stub's `-L` are linux-gated, and the stub emits no archive).

## Kludges / debt

- **Loader link stub.** A symbol-only stub `.so` is not a real loader. It is link-correct and
  the soname trick makes the runtime bind the real Mesa loader, but it is a cross-compile
  expedient. Honest alternative once a linux box exists: link the distro `libvulkan.so` directly
  (drop the stub on native-linux builds). Debt: a stub `.so` exporting 711 no-op symbols.
- **Generator runs in `build()` without platform context.** `build()` cannot see the selected
  platform (discovery precedes platform resolution), so the linux stub is generated once on first
  discovery and cached — even during a from-clean macOS build it does one `zig cc -target
  x86_64-linux-gnu` (~0.2s, idempotent, cached thereafter). Minor MEL-ENGINE-III infraction (a
  linux artifact built during a non-linux build). Clean fix needs a per-variant third-party
  custom-command hook in the build framework (does not exist today).
- **`mel_inject_thirdparty` prefix injection.** A sourceless third-party still gets a
  `-L<prefix>/lib`/`-I<prefix>/include` for a nonexistent prefix injected on every platform where
  it is a dep. Verified harmless (linkers/compilers ignore nonexistent paths; confirmed absent
  from the macOS gpu cflags/links). Noise, not breakage.

## CLAUDE.md suggestions (recommendations only)

- Document that linux builds use `zig cc -target x86_64-linux-gnu` as the cross toolchain even on
  a native-linux host, and that the Vulkan headers/loader are vendored (`third-party/vulkan-*`)
  rather than taken from a system SDK.
- The `mel_depends` API has no `WHEN`-gated form; a `mel_depends_when(t, name, when)` would let a
  module depend on a platform-specific helper target without the dependee needing to be available
  on all platforms (would have let the stub stay `mel_unavailable` off-linux).

## Suggestions

- **Linux platform backends are the next gate.** Add `modules/debug/src/linux/stacktrace.c`
  (glibc `backtrace`/`backtrace_symbols`) and a linux gui/windowing backend; the GPU/Vulkan layer
  is ready and waiting behind them.
- **Per-variant third-party custom-command hook** in the build framework would let the loader
  stub be generated lazily only for linux targets (removes the MEL-ENGINE-III debt above).
- **Backend-tagged goldens.** Since lavapipe and MoltenVK will diverge, consider keying golden
  filenames by backend (or storing a per-backend tolerance) so cross-backend runs assert against
  their own references instead of always diffing MoltenVK output.
