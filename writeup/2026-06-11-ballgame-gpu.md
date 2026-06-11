# 2026-06-11 — ballgame: canvas → GPU rewrite

## Work done

Gabbo's correction on the prior session: ballgame had to be a **GPU** game, not a
GUI-canvas/painter game. Rewrote the app onto the gpu module.

- **apps/ballgame/src/setup.c** — full rewrite. The GUI frame now hosts a
  `mel_gpu_view` (keyboard + pointer callbacks live on the view, same WASD/arrows +
  drag scheme); gpu instance/device at setup, surface + swapchain + pipeline on the
  view's first resize, frames paced by a 60 Hz `mel_gpu_render_source` (sim runs in
  the render callback on its dt — the vat tick + frame clock + invalidate trio is
  gone). Scene: vertical-gradient background, 24-node fading trail, additive-looking
  glow, ball, specular highlight — all SDF circle sprites from one alpha-blended
  pipeline, CPU-built NDC vertices uploaded each frame into a 3-deep UPLOAD vertex
  buffer ring. Movement got a velocity model (exponential approach to desired
  velocity, wall bounce with restitution) instead of the old constant-speed step.
- **apps/ballgame/shaders/slang/ballgame.slang** — one vs/fs pair; vertex carries
  `pos.xy (NDC), uv.xyz (sprite uv + feather), color.rgba`; fragment does the
  smoothstep circle mask. `uv = (0,0)` makes a solid fill (background path).
- **apps/ballgame/shaders/gen_bundles.sh** — bundle generator (adapted from
  hello-gpu's), minted **src/ballgame_bundle.h** with SPIR-V + MSL + WGSL.
  **src/bundle_select.h** copied verbatim from hello-gpu (apps are self-contained;
  no cross-app include path exists).
- **build.c** — deps now boot, vat, allocator, core, gui, gpu, log, string
  (paint/color/math/time dropped).
- **readme.md** — rewritten; documents the deliberate floor (no push constants, no
  bindless, plain vertex buffers — WebGPU-core compatible) and the platform matrix.

Data-path choice: WebGPU core has no push constants and no bindless, and the classic
descriptor-set path is Vulkan-only today, so per-frame data rides in the vertex
stream — the only path every backend floor accepts (MEL-ENGINE-VII: honest
alternative, not a broken shadow).

Verified:
- macos **metal** (default): build + windowed run; screenshot shows gradient, trail,
  glow, ball, highlight rendering correctly; input-driven motion observed.
- macos **vulkan** (`--gpu=vulkan`, MoltenVK + validation): build + 4 s run, device +
  3-image swapchain up, **zero validation errors**.
- **wasm**: builds + links (WGSL lane compiled in; not executed in a browser).

## Platform regression (pre-existing, tree-wide — not introduced here)

`./nob build ballgame ios|android` refuses: **gpu hard-depends on slang**
(`modules/gpu/build.c`), and slang has no ios/android artifact
(`third-party/slang/readme.md` — marked `mel_unavailable`, fix is a Gabbo decision).
This blocks **every** gpu app — `hello-gpu ios` refuses identically. The canvas
ballgame did run there, so ballgame-on-GPU loses ios/android until slang is either
vendored for those platforms or gpu's slang dependency is platform-gated with the
from-slang API loud-failing where absent. The second option needs no upstream
artifact and would restore gpu apps on mobile; it touches gpu's public surface
(`gpu/shader.h` includes `<slang/compile.h>`), so left for a deliberate pass.

## Kludges (MEL-ENGINE-VIII — full confession)

- **bundle_select.h duplicated** from hello-gpu into ballgame, byte-identical. Two
  copies will drift. It wants to be a tiny shared helper (gpu module or a `gpu-app`
  exemplar lib) — same for the gen_bundles.sh near-copy (paths + shader list differ;
  the emit/compile machinery is duplicated).
- **No DXIL in the bundle.** `BALLGAME_HAS_DXIL 0`; the DXIL lane needs the
  Windows-only DXC pass (`MEL_GEN_DXIL_ONLY=1` on win-pilot). win32's default
  backend is Vulkan (SPIR-V lane), so only `--gpu=d3d12` is affected; it will
  loud-fail shader create (TARGET_UNSUPPORTED) until minted.
- **win32 not smoked.** No commit/push happened this session (work sits on the
  worktree branch), so win-pilot never built it. SPIR-V lane is the same code path
  verified via MoltenVK, but nobody ran it on Windows.
- **wasm not executed.** Link-verified only; the WGSL blob and the webgpu swapchain
  path were not exercised in a browser.
- **`Mel_Gpu_Adapter* adapters[8]`** fixed-size out-array for `mel_gpu_adapters`,
  copied from gpu_host.c — it is the API's calling convention (count-capped out
  array), not a growth point, but it is still a literal 8.
- **Screenshot smoke saw motion without injected input** — almost certainly live
  keystrokes/pointer on the host while the window had focus. Harmless (it proved
  input works), but the smoke is not hermetic.

## CLAUDE.md suggestions (recommendations only, not applied)

- None.

## Suggestions

- Promote `bundle_select.h` + the gen_bundles machinery into a shared home before a
  third app copies them.
- Decide the slang-on-mobile story (vendor prebuilts vs platform-gate gpu's slang
  dep); it currently blocks every gpu app on ios/android and the d3d12 runtime-slang
  lane is the same knot.
- Mint ballgame's DXIL on win-pilot once convenient
  (`MEL_GEN_DXIL_ONLY=1 shaders/gen_bundles.sh` there).
- A swapchain-driven present-paced source (true vsync callback) would replace the
  fixed 60 Hz timer for games; same note as the prior ballgame session's
  `mel_vat_vsync_open` suggestion, now on the gpu side.
