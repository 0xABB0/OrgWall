# 2026-06-01 — `Mel_Gpu_Surface`: the swapchain bind-handle

Implemented `design/platform-surface.md` — lifting the swapchain's OS-bind-point out of
each backend's `swapchain_create` into a first-class `Mel_Gpu_Surface` the swapchain
consumes. The spec's §0 supersedes the stale `gpu-rhi.md §7.4` draft (the rejected
`Mel_Platform_Surface` with a six-state lifecycle and twelve-event taxonomy); what landed
is thin — a `gpu`-owned handle over a vended native region, two signals only.

## Work done — what changed, and why

**Public surface (`include/gpu/surface.h`, `types.h`, `swapchain.h`, `gpu.h`).**
- New opaque `Mel_Gpu_Surface` (pointer handle, matching every sibling `Mel_Gpu_*` — no
  value-handle churn for one object). Four verbs: `mel_gpu_surface_create(dev, native)` /
  `_destroy` / `_reconfigure(w,h)` / `_rebuild(new_native)`. The latter two are the spec's
  two signals (§3): reconfigure on extent change, rebuild on backing lost/replaced.
- `Mel_Gpu_Swapchain_Opt.native_window` (raw `void*`) → `.surface` (the handle). The
  swapchain stops caring whether the native came from a `gui` view, a `window` content
  region, or an app borrow (MEL-ENGINE-IX); it sees one handle.

**Ownership split, per backend.** The surface owns the bind-handle; the swapchain owns the
presentation built atop it (device/format config, swap images, sync, drawable acquisition).
- **metal** (`src/metal/surface.m` new) — surface owns the `CAMetalLayer` (created, attached
  `view.layer`/`wantsLayer`, `contentsScale` + drawable size). Swapchain sets `pixelFormat`
  and acquires drawables, reading `sc->surface->layer`. `command.m` reads through the surface.
- **vulkan** (`src/vulkan/surface.c` new) — surface owns `VkSurfaceKHR` (+ retained
  `CAMetalLayer` on apple) and the `VkInstance` for teardown, reusing the existing apple/
  android helpers. Swapchain reads `sc->surf->surface` for caps/present-support/format-
  negotiation/`vkCreateSwapchainKHR`; it no longer destroys the surface or layer.
- **webgpu** (`src/webgpu/surface.c` new) — surface owns `WGPUSurface` + the emscripten
  canvas `selector`; the canvas-size `EM_JS` moved here from the swapchain. Swapchain reads
  `sc->surface->surface` for configure/get-current-texture/present.

**Common path stays one verb (MEL-ENGINE-II).** `mel_gpu_swapchain_resize` forwards extent to
its borrowed surface (`_reconfigure`) before rebuilding swap images, so apps call one resize.
The surface is host-owned and borrowed by the swapchain — durable across image rebuilds, the
re-point target on backing loss.

**Caller (`apps/hello-gpu/src/gpu_host.c`).** Holds a `Mel_Gpu_Surface*`; creates it from the
vended native, then the swapchain from the surface; `teardown` releases swapchain then surface.

**Build (`modules/gpu/build.c`).** The new per-backend `surface.{c,m}` are absorbed by the
existing `src/<gpu>/*` globs. One rename was forced: the vulkan apple helper `surface.m` →
`surface_apple.m`, because `emit.c:29` derives the object path from the extension-stripped
stem, so `surface.c` + `surface.m` would both emit `surface.o` and collide.

**Verification.** All three backends compile on macOS (`metal` default, `--gpu=vulkan`,
`--gpu=webgpu`); `hello-gpu` links on all three. Gabbo confirmed the metal runtime manually —
window shows the native GUI label above a live GPU surface; create → resize → present flows
through the new handle.

## Kludges — every shortcut, sanctioned or not (MEL-ENGINE-VIII)

- **`mel_gpu_surface_rebuild` has no runtime exercise.** It is implemented for all three
  backends but no in-repo caller drives the backing-replace path; `hello-gpu` still uses the
  pre-existing teardown+recreate on zero-size (Android `surfaceDestroyed`). So `_rebuild`
  compiles everywhere but is runtime-untested. Debt: the `on_content_replaced` window signal
  (`design/window.md` §6) is the intended driver; until a consumer wires it, treat rebuild as
  unproven.
- **clang-format reformatted whole files.** Per MEL-CODE-004 I ran clang-format over every file
  I touched; because the repo's committed code diverges from its own `.clang-format`
  (30 of 41 sampled `gpu`/`core` files would be reformatted — Allman braces are honored but
  short-ifs and manual column alignment are not), the formatter rewrote large pre-existing
  regions (every enum/switch in `types.h`, `metal.h`, `vulkan_backend.h`) far beyond my
  semantic change. The diff now mixes a format pass with the feature, and my touched files are
  strictly conformant while their neighbors are not — local inconsistency. (I first wrongly
  reverted the format to match surrounding hand-style; Gabbo corrected: follow clang-format.)
- **vulkan surface is inert on linux/win32.** `mel_gpu_surface_create` returns NULL there (no
  window-system surface lowering yet) — preserved from the swapchain's prior `#else return
  NULL`, not new debt, but the surface object now exists-yet-fails on those platforms rather
  than the swapchain failing. Honest NULL, no silent corruption.
- **Headless is unbuilt.** `mel_gpu_surface_create(dev, NULL)` → NULL; the owned-texture
  swapchain replacement (§2 fourth source) is deferred, matching `gpu-rhi.md` ("headless is
  not in this unit"). Spec-sanctioned, flagged.
- **`char selector[96]`** (webgpu) is a fixed-size array (MEL-CODE-002). Relocated from the
  swapchain, not introduced; left as-is to keep the change minimal. Worth a `str8`/dynamic
  fix when the webgpu surface is next touched.

## CLAUDE.md suggestions (recommendations only — not applied)

- The repo-wide divergence from `.clang-format` (30/41 sampled files) means MEL-CODE-004
  ("format often") is not actually enforced, so touching a file forces a choice between a
  noisy whole-file reformat and matching un-formatted neighbors. Consider either a one-shot
  `clang-format -i` sweep of the tree (so the baseline conforms and future diffs stay small)
  or a pre-commit/CI format-check. As written, the rule and the codebase disagree, and a fresh
  contributor (or agent) cannot tell which is authoritative without being told.
- `docs/coding-guidelines.md` could state the brace/alignment intent in one line so the
  `.clang-format` is not the sole, contradicted source of truth.

## Suggestions

- **Wire `on_content_replaced` → `mel_gpu_surface_rebuild`** in a host (the `gui` gpu-view or
  the extracted `window`) to give the rebuild path a real driver and a runtime test; that also
  closes the Android backing-loss loop without a full swapchain teardown.
- **Fold `mel_gpu_swapchain_resize` semantics onto the surface signals explicitly** if the
  drawn/persistent backend (paint's deferred GPU path) ever needs to keep the swapchain across
  a surface rebuild — today resize forwards to the surface, which is the right seam for that.
- **Replace the webgpu `selector[96]`** with the canonical string type when convenient.
