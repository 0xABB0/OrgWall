# Windows Vulkan bring-up — gpu backend + hello-gpu graphical example

Brings the Vulkan GPU backend and the `hello-gpu` graphical example up on Windows, natively (clang / MSVC ABI),
against the Vulkan SDK loader. Tested on the `win-pilot` box (Windows 10 22H2, NVIDIA GeForce RTX 2060 SUPER,
Vulkan SDK 1.4.321.1) over SSH; built there per the `git pull` + `dev.cmd nob` workflow.

## Result

- **`gpu-vulkan` test suite: 28/28 on Windows/NVIDIA** (the same suite that is 28/28 on macOS/MoltenVK), including
  the four M2 U13 render-state tests (alpha blend, MRT, depth-compare, MSAA). The whole backend + the U13 work is
  now verified on a real discrete GPU, not just MoltenVK.
- **`hello-gpu.exe` builds, links, and runs.** It creates the instance + device on the RTX 2060, creates the Win32
  surface and swapchain, and renders the cube — confirmed by `nvidia-smi` showing the process holding a **C+G**
  (compute+graphics) context with ~540 MiB of GPU memory, with **zero validation errors / VUIDs** across the run.

The window is not *visible* over a headless SSH session (no interactive desktop), but the full render pipeline is
live; run it on the box's interactive session (Virtual Desktop) to see the cube.

## Work done

### gpu — Win32 Vulkan surface (U18 §7.4)
- `src/vulkan/windows/surface.c` — `mel_gpu__vk_create_win32_surface` via `vkCreateWin32SurfaceKHR` over the window
  module's HWND. The sole TU that pulls `windows.h` + `vulkan_win32.h` (mirrors `macos/surface.m`), so the backend
  core stays platform-clean.
- `surface.c` — `_WIN32` branch wired to it; `vk_backend.h` declares the creator; `instance.c` enables
  `VK_KHR_win32_surface` when present.
- `build.c` — win32 vulkan sources + link `vulkan-1` with `%VULKAN_SDK%` `Include`/`Lib` injected at configure time
  (vcvars does not add them); `-lvulkan` gated to macOS (was all-platform, which named the wrong lib on win32).

### gui — winui gpu_view control
- `src/winui/gpu_view.c` — the win32 `gpu_view`, absent before (only cocoa/dom/androidnative existed), so any win32
  app embedding a GPU surface failed to link. A child HWND with a NULL background (the swapchain owns the pixels),
  `WM_SIZE` → `on_resize`, `mel_gpu_view_surface` returns the HWND. Modeled on `winui/canvas.c`.

### debug — link dbghelp on win32
- `debug/build.c` — `src/windows/stacktrace.c` calls DbgHelp (`SymFromAddr` / `SymGetLineFromAddr64`) but nothing
  linked `dbghelp.lib`, so *every* win32 executable failed to link (LNK2019). Linked from the module that uses it.

### gpu — swapchain readiness log (MEL-CODE-006)
- `swapchain.c` — an info log on swapchain (re)build (`swapchain ready: N images WxH`). Swapchain success was
  previously silent, which made "is it rendering or stalled?" undiagnosable from logs on a headless box. (This
  debugging would have been one grep with it.)

## Toolchain notes (for the next win32 session)

- Build framework win32 target = bare `clang` at `x86_64-windows-msvc` + `llvm-ar` (`modules/build/toolchain.c`),
  linking via `clang … -o`. So `-l<lib>` resolves `<lib>.lib`; the Vulkan loader is `-lvulkan-1` + `-L%VULKAN_SDK%/Lib`.
- `C:\Users\Gabbo\dev.cmd` loads vcvars64 + LLVM then execs the rest; it does **not** add the Vulkan SDK to
  INCLUDE/LIB — only `VULKAN_SDK` is set, hence the build.c injection.
- The test runner runs in-process on win32 (no fork), so the suite needs no `MEL_TEST_NOFORK`.
- gmp/mpfr **build and link** on native win32 now — the "win32 GUI link blocked on gmp" note in
  `modules/build/platforms.md` is stale (math/gui linked clean here).

## Kludges and debt (confessed, MEL-ENGINE-VIII)

- **No visible-window verification.** Rendering is confirmed by `nvidia-smi` (C+G context + GPU memory) and a
  clean validation log, not by eyeballing the cube — an SSH session has no interactive desktop. A human run on the
  box closes this.
- **gpu_view input forwarding is minimal.** Pointer down/move/up are wired; the cocoa view also does tracking
  areas, focus first-responder, and keyboard. The win32 control forwards pointer + the shared focus/key dispatch
  (`mel_gui__win32_subclass_common`) but is not a full input peer yet. Irrelevant to rendering; flagged for the
  interactive pass.
- **`mel_gpu__vk_create_win32_surface` uses `GetModuleHandleW(NULL)`** for the `hinstance`. Correct for a single-exe
  process; a DLL-hosted window would want the window's own module. The field is largely informational to the loader.
- **build.c leaks two small flag strings** (`-I`/`-L` from `%VULKAN_SDK%`) — `mel_*` stores the pointer, configure
  runs once, so the leak is bounded and moot. Noted rather than hidden.
- **`platforms.md` still says "DX12 is unimplemented; win32 GPU has no real backend."** The second clause is now
  false for Vulkan; the doc should be updated (not done here — docs/CLAUDE.md changes are recommendations only).

## CLAUDE.md / repo-convention suggestions (recommendations only)
- Update `modules/build/platforms.md`: win32 now has a real Vulkan GPU backend, and the gmp/win32 GUI-link blocker
  is resolved.
- A short `docs` note on the win32 SDK-path injection pattern (getenv `VULKAN_SDK` in build.c) would help the next
  module that needs an SDK on win32.

## Suggestions
- Run `hello-gpu` interactively on the box to confirm the visible cube, then flesh out the gpu_view input peer to
  cocoa parity.
- D3D12 is the other M2 co-primary; with the box available it is now buildable/testable, when prioritized.
