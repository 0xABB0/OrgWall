# GUI Linux desktop backend (XCB) — hello-gpu links completely on linux/vulkan

Closes the two non-Vulkan blockers the prior GPU-linux lane surfaced (writeup
`2026-06-03-gpu-vulkan-linux.md`): `modules/gui` had no linux windowing backend and
`modules/debug` had no linux stacktrace. With both landed and the gpu instance
surface-extension gap closed, `./nob build hello-gpu linux --gpu=vulkan` now links to a
complete x86-64 linux ELF with zero unresolved internal symbols. Host: macOS (Darwin
25.2.0), zig 0.16.0 (`zig cc -target x86_64-linux-gnu`) as the linux cross toolchain, same
as the GPU lane. No linux env / no Docker on the host: the RUN is honestly deferred with a
reproducible recipe.

## Backend choice: XCB (not Wayland)

Rationale:
- The gpu vulkan linux lowering (`modules/gpu/src/vulkan/linux/surface.c`) selects XCB by
  **presence** of `{xcb_connection, xcb_window}` on its `Mel_Gpu_Linux_Native`, and its XCB
  path is the simpler of the two lowerings. Pairing the gui backend with XCB matches it
  exactly.
- XCB exposes a **single fd** (`xcb_get_file_descriptor`) that drops straight into the
  reactor's poll-source mechanism. Wayland additionally needs `wl_display_get_fd` plus the
  flush / `dispatch_pending` / `prepare_read` choreography **and** an xdg-shell protocol
  handshake (a compositor round-trip to get a mappable surface) — multiples more code for a
  first MVP.
- On the cross host no system XCB headers are needed: the backend declares the minimal XCB
  typedefs/constants locally (as the gpu linux `surface.c` did) and **dlopen's libxcb at
  runtime**, so there is no link-time libxcb and no new third-party.

## Files

New (gui XCB backend, `modules/gui/src/xcb/`):
- `linux.h` — minimal XCB typedefs/constants, the dlopen'd `mel_xcb_api` function table,
  `Mel_Xcb_State`, and the gui↔gpu native-handle struct `Mel_Gui_Xcb_Native` (its field
  layout mirrors gpu's `Mel_Gpu_Linux_Native` exactly; the contract is the layout, gui and
  gpu stay decoupled per `gui/controls/gpu_view.h`).
- `backend.c` — `mel_gui__backend_init` (dlopen libxcb → `xcb_connect` → screen/atoms →
  attach a reactor poll-source on the XCB fd), `mel_gui__backend_destroy`, the event pump
  (ConfigureNotify→resize, ClientMessage/WM_DELETE_WINDOW→close, Button/Motion→pointer), all
  `mel_gui_set_*`, `mel_gui__nav_*`, `mel_gui_supports_multi_root`,
  `mel_gui__backend_set_content_size`, the child-window helpers.
- `frame.c` — `mel_frame_create_opt` (top-level window: title via WM_NAME, WM_DELETE_WINDOW
  registered for graceful close), `mel_frame_insets` (zero), `mel_gui__screen_new`
  (pass-through child window).
- `widgets.c` — `mel_label_create_opt`, `mel_button_create_opt` (child windows occupying the
  layout slot; **stubbed loud**, see below).
- `gpu_view.c` — `mel_gpu_view_create_opt` (child window + a persistent `Mel_Gui_Xcb_Native`
  populated with the live xcb connection + window xid), `mel_gpu_view_surface` (returns that
  struct ptr — exactly what gpu's xcb lowering reads), plus resize/pointer routing.

New (debug linux):
- `modules/debug/src/linux/stacktrace.c` — `mel__platform_stacktrace_capture` via glibc
  `backtrace()` + `dladdr()` (the macOS file's twin; guard is `MEL_PLATFORM_LINUX`).

Build (additive, gated):
- `modules/gui/build.c` — `mel_sources(... MEL_ON(LINUX), "src/xcb/*.c")`, `-ldl`, and a
  linux-private `-Imodules/log/include` cflag (see debt).
- `modules/debug/build.c` — `mel_sources(... MEL_ON(LINUX), "src/linux/*.c")`.

Shared / gpu-lane edit (FLAGGED):
- `modules/gpu/src/vulkan/instance.c` — added an `#elif defined(__linux__)` block enabling
  `VK_KHR_xcb_surface` + `VK_KHR_wayland_surface` on the instance (each only if
  `mel_gpu__instance_ext_available`), mirroring the win32/android pattern. **macОS bytes are
  unchanged** — it is an `#elif`, invisible to the Apple compile; verified by the 48/48 run
  whose log still shows only `VK_EXT_metal_surface` enabled.

## The complete hello-gpu linux link (success bar)

```
$ ./nob build hello-gpu linux --gpu=vulkan
[88/88] LINK apps/hello-gpu/build/linux-debug/hello-gpu
build: emitted apps/hello-gpu/build/linux-debug/build.ninja    (exit 0)

$ file hello-gpu
ELF 64-bit LSB executable, x86-64, dynamically linked,
interpreter /lib64/ld-linux-x86-64.so.2, for GNU/Linux, with debug_info, not stripped

$ llvm-readelf -d  →  DT_NEEDED: libvulkan.so.1, libc.so.6, ld-linux-x86-64.so.2,
                       libpthread.so.0, libdl.so.2
```

Undefined-symbol audit (`llvm-nm`): the ONLY undefined symbols are (a) glibc dynamics —
`dlopen/dlsym/dlerror/dladdr/backtrace/...`, resolved by libc/libdl at runtime — and (b)
Vulkan loader entry points (`vkCreateInstance`, `vkCreateXcbSurfaceKHR`,
`vkCreateWaylandSurfaceKHR`, …), resolved by `libvulkan.so.1` at runtime. **Zero**
`mel_gui_*`, `mel_gpu_view_*`, `mel_frame_*`, `mel_label/button_*`,
`mel__platform_stacktrace_capture` remain undefined — gui + debug + gpu all resolved.
**libxcb is deliberately NOT a DT_NEEDED** (dlopen'd at runtime) — `libdl.so.2` is needed
for that.

The 16 gui symbols + 1 debug symbol the prior lane enumerated as missing are now all
defined: `mel_gui__backend_init/destroy`, `mel_gui_set_text/bounds/visible/focus`,
`mel_gui__nav_replace/back`, `mel_gui__screen_new`, `mel_gui__backend_set_content_size`,
`mel_gui_supports_multi_root`, `mel_frame_create_opt`, `mel_label_create_opt`,
`mel_button_create_opt`, `mel_gpu_view_create_opt`, `mel_gpu_view_surface`,
`mel__platform_stacktrace_capture`.

## macOS regression (required gate) — PASS

`DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1 ./nob test gpu-vulkan macos
--gpu=vulkan` → **48 passed, 0 failed, 0 skipped, of 48.** `./nob build hello-gpu macos`
also links+packages clean (cocoa path untouched). `./nob configure hello-gpu win32` and
`android` both exit 0 (the win32 `llvm-rc` resource warning is a pre-existing cross-host
quirk, unrelated).

## RUN status — environmentally deferred, honestly (MEL-ENGINE-VIII)

No linux box and no Docker on this mac host (`docker` absent, confirmed by the prior lane).
The window cannot be opened/presented here. NOT faked. Recipe (runs on any linux box or a
Docker-capable host), extending the GPU lane's lavapipe image with an X server:

```dockerfile
# Dockerfile.xcb — software-Vulkan + headless X for the windowed hello-gpu
FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends \
      mesa-vulkan-drivers vulkan-tools libvulkan1 \
      libxcb1 xvfb x11-utils \
      curl xz-utils git ca-certificates clang \
 && rm -rf /var/lib/apt/lists/*
RUN curl -fSL https://ziglang.org/download/0.16.0/zig-linux-x86_64-0.16.0.tar.xz \
      | tar -xJ -C /opt && ln -s /opt/zig-linux-x86_64-0.16.0/zig /usr/local/bin/zig
ENV VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json
ENV LIBGL_ALWAYS_SOFTWARE=1
WORKDIR /src
```

```bash
docker build -f Dockerfile.xcb -t mel-xcb .
docker run --rm -v "$PWD":/src mel-xcb bash -lc '
  clang -std=c23 -g -Imodules/build -o nob nob.c &&
  ./nob build hello-gpu linux --gpu=vulkan &&
  Xvfb :99 -screen 0 1280x800x24 & sleep 1 &&
  DISPLAY=:99 ./apps/hello-gpu/build/linux-debug/hello-gpu &
  sleep 3 && DISPLAY=:99 xwininfo -root -tree   # window should be mapped
'
# Verify lavapipe is the device: `vulkaninfo | grep -i llvmpipe`.
# Expected: an XCB window mapped under Xvfb, the gpu_view child presenting the demo via
# the lavapipe swapchain. On a real X session (a linux desktop) the window is on-screen.
```

A real linux desktop (native X11 or XWayland) is the simplest verification: install
`libxcb1` + a Vulkan ICD, build, run — the backend dlopen's libxcb and connects to `$DISPLAY`.

## Kludges / debt (MEL-ENGINE-VIII — full confession, bar is zero)

1. **Label/button render no text.** XCB core has no font/text or push-button widget (those
   live in Xft/Pango/a toolkit). `mel_label_create_opt`/`mel_button_create_opt` create child
   windows that occupy the correct layout slot (so geometry, parenting, and the gpu_view's
   placement are right) but draw no glyphs and fire no native click. This is **logged loudly
   once** ("renders no text … install an Xft/Pango path"). Honest, not silent. The GPU
   surface — the point of hello-gpu — is fully functional; the chrome label is blank.
2. **`-Imodules/log/include` cflag instead of `mel_depends(gui, "log")`.** The backend logs
   via `mel_log_*` (genuine, for MEL-ENGINE-VIII). `gui`'s public dep closure does not include
   `log`, and `mel_depends` has **no WHEN-gated form** (same gap the GPU lane flagged), so an
   unconditional `mel_depends(gui,"log")` would pull log+sqlite3 into **every** gui-only
   consumer on **every** platform — out of my linux scope and a real footprint change. I
   instead inject log's include path linux-privately and rely on hello-gpu's existing
   `gpu→log` edge to resolve the symbols at link. Verified: macОS gui untouched, linux links.
   **Coupling debt:** a hypothetical linux *gui-only* binary (none exist today) that does not
   also pull `gpu`/`log` would not resolve `mel_log_*`. Clean fix: a `mel_depends_when` build
   API (recommended below), then `mel_depends_when(gui, "log", WHEN(MEL_ON(LINUX)))`.
3. **Keyboard input is unmapped.** Button/motion pointer events route to the gpu_view; key
   events are not yet translated (XCB delivers keycodes; mapping to `Mel_Key` needs an
   xkbcommon or core keymap round-trip). hello-gpu drives the GPU loop without keyboard, so
   this is unexercised — left as a follow-up, not stubbed with a wrong mapping.
4. **Buffered-event edge in the reactor source.** The event pump drains all queued XCB events
   in `dispatch` when the fd is readable, and flushes in `prepare` before blocking. The known
   XCB-in-external-loop subtlety — an event already in libxcb's internal queue without the fd
   re-signalling — is not yet guarded with `xcb_poll_for_queued_event` in `prepare`/`check`.
   Untested here (deferred run), so I did not add unverified peek logic; flagged for the run.
5. **No HiDPI / scale.** `mel_window_scale` analogue is absent; the backend assumes 1.0. Fine
   for lavapipe/Xvfb; a real HiDPI X session would need RANDR DPI.
6. **`mel_gui_get_text` returns empty** (no native text store to read back). Consistent with
   "no text rendering"; the cocoa/win32 backends read the native control, which has no XCB
   analogue here.

## CLAUDE.md suggestions (recommendations only — not applied)

- Document that linux gui uses an **XCB** backend, dlopen'd at runtime (no link-time libxcb,
  no system XCB headers in the cross sysroot), pairing with the gpu vulkan xcb surface.
- Re-state the GPU lane's ask for a **WHEN-gated `mel_depends`** (`mel_depends_when`): it would
  let a platform-specific backend pull a helper module (here: `log` on linux only) without
  over-coupling the module on every platform. This is now the second lane to hit it.

## Suggestions

- **Text path next.** An Xft or Pango+Cairo (or a Melody `paint`-driven) glyph path turns the
  blank label/button into real chrome and unblocks the non-GPU gui widgets on linux.
- **Wayland backend** as a sibling `src/wayland/` once XCB is proven on a real box — the gpu
  lowering already supports it; only the windowing + xdg-shell handshake is missing.
- **Keyboard via xkbcommon** (dlopen'd, same pattern) for full input parity.
