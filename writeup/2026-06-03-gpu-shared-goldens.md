# Shared cross-backend golden scenes + cap-driven shader selection

## Work done

### Half A — cap-driven shader selection (hello-gpu triangle)

`apps/hello-gpu/src/triangle.c` previously chose MSL when `caps.shader.bytecode_passthrough.msl`,
else shipped SPIR-V **without consulting the spirv cap** — the else-branch handed a SPIR-V blob to
WebGPU, whose vendored Dawn Release has no SPIR-V reader, so the triangle loud-failed to clear-only.

Replaced that with a complete, cap-gated choice over all four passthrough forms. Factored the logic
into a reusable header `apps/hello-gpu/src/bundle_select.h`:

- `Mel_Bundle_Graphics` carries every form's blob+size (spirv / msl / wgsl / dxil) plus entries.
- `mel_bundle_select_graphics(dev, bundle)` checks `caps.shader.bytecode_passthrough.{msl,wgsl,dxil,spirv}`
  in turn, picks the matching form **only if the bundle carries it**, and on no match logs a precise
  error (which caps the device offers vs which forms the bundle has) and returns
  `MEL_GPU_SHADER_CREATE_TARGET_UNSUPPORTED` — loud failure, no silent default (MEL-CODE-007 / MEL-ENGINE-VIII).

`triangle.c` now fills a `Mel_Bundle_Graphics` (spirv+msl+wgsl; dxil NULL) and calls the selector. Under
WebGPU it selects WGSL and the triangle renders; under Vulkan SPIR-V, under Metal MSL.

The gradient/quad **app screens** (`gallery.c`, `layers.c`, `postprocess.c`) were inspected and do NOT
share the triangle pattern: they hard-wire `.spirv_vertex` blit/quad shaders (SPIR-V only) via the older
field API and mix bundle fragments with `BLIT_VERT_SPV`. They never used the `bytecode_passthrough`
cap, so per the task's "if they share the pattern" they are out of scope and untouched. Only `triangle.c`
carried the cap-driven selection.

### Half B — shared goldens + backend-agnostic `gpu-scene` suite

- New `modules/gpu/test/test_scene.c`: one suite, pure RHI, that renders triangle/gradient/quad via the
  **cap-selected bundle** (`mel_bundle_select_graphics`) and diffs against shared goldens through the
  existing `MEL_GOLDEN` harness, labelled by the selected backend. No per-backend rendering fork — a single
  `scene_render_readback` (RT + readback at 64×64, explicit barriers that Metal/WebGPU no-op) drives all three.
- New target `gpu-scene` in `modules/gpu/build.c` (additive): `test_scene.c` + `img_golden.c` + runner,
  includes `apps/hello-gpu/src` for the bundles + selector, defines `MEL_GPU_{VULKAN,METAL,WEBGPU}=1` per `--gpu`.
- Shared goldens minted on the **macOS-Vulkan oracle** and committed:
  `modules/gpu/test/golden/shared/{triangle,gradient,quad}.ppm` (P6, 64×64).

## Measured cross-backend deltas (vs the Vulkan-oracle goldens, 64×64)

Captured by rendering each backend through `gpu-scene` and diffing its readback against the committed
oracle goldens (per-channel max delta over the 4096 px):

- vulkan triangle / gradient / quad: max delta 0 (oracle == self; asserted exact, band {delta 2, frac 0.0}).
- metal   triangle / gradient / quad: max delta **0**, 0 offending px.
- webgpu  triangle / gradient: max delta **0**, 0 offending px. quad: **skipped** (see below).

On macOS all three converge bit-for-bit: native Metal, Dawn-on-Metal, and Vulkan-via-MoltenVK funnel
into the same Metal rasterizer. The cross-backend band is therefore unnecessary *on this host*.

### Tolerance set

- Vulkan (oracle): `{ max_channel_delta = 2, max_fraction_exceeding = 0.0 }` — near-exact (rounding only).
- Metal / WebGPU: `{ max_channel_delta = 4, max_fraction_exceeding = 0.02 }` — measured delta is 0, so this
  band is held small and non-zero **only** to tolerate cross-HOST rasterizer divergence (edge coverage,
  interpolation precision) when a real Vulkan/D3D12 device mints or diffs elsewhere. Tighter than the prior
  draft (delta 6, frac 0.05) because the data says 0; not zero because a single-host 0 must not become a
  brittle cross-host contract.

### WebGPU triangle renders (proof)

`gpu-scene --gpu=webgpu` renders the triangle through the cap-selected **WGSL** bundle and diffs it against
`golden/shared/triangle.ppm` (an actual RGB triangle: center RGB(124,69,62), dark corners RGB(20,26,33)),
passing at delta 0. A clear-only frame would be a flat RGB(20,26,33) and fail the diff massively. That diff
is the readback proof the triangle now rasterizes under WebGPU rather than clearing. `hello-gpu macos
--gpu=webgpu` also builds clean with the cap-driven `triangle.c`.

## Green-run counts

- gpu-scene vulkan: 3/3 (exact). gpu-scene metal: 3/3 (band, measured 0). gpu-scene webgpu: 2/2 + 1 skip.
- gpu-vulkan 48/48, gpu-metal 4/4, gpu-webgpu 3/3, gpu-visual 11/11 — all still green.

## Kludges / debt

- **DXIL bundles owed.** triangle/gradient/quad bundles carry SPIR-V+MSL+WGSL; the Slang lane emitted no
  DXIL (no D3D12 there). So D3D12 cannot render these scenes via bundle. The selector handles it honestly:
  `caps.dxil` true but `bundle->dxil_vertex == NULL` → falls through to the loud no-match error. No D3D12
  device exists on the macOS host, so `gpu-scene` is not run there; when DXIL bundles land, D3D12 joins the
  matrix with no code change (the selector already branches on it). This is the headline owed debt.
- **quad on WebGPU is skipped, not diffed.** The quad scene drives the pipeline via push constants; WebGPU
  core has no push constants (the backend's `pipeline_create` refuses with MissingFeature). The scene
  `MEL_SKIP`s under `#if MEL_GPU_WEBGPU` with that reason — an honest MEL-ENGINE-VII degradation, not a
  silent pass. The skip is gated on the backend macro rather than a cap because there is no
  push-constant cap field and caps.h is out of my ownership (see open questions).
- **Vulkan validation error on gradient/quad SPIR-V (pre-existing).** Minting the goldens logs a MoltenVK
  validation ERROR: the gradient/quad Slang-emitted SPIR-V declares the `DrawParameters` capability
  (vertex-index addressing) but the device is created without `shaderDrawParameters` /
  `VK_KHR_shader_draw_parameters`. MoltenVK tolerates it and the draws are correct (the goldens are rich and
  verified), but this violates MEL-ENGINE-VIII's "fail loudly and correctly" — the feature is used without
  being enabled. This is in the backend device-creation path and the bundle SPIR-V, both outside my file
  ownership; reported, not fixed.

## CLAUDE.md suggestions

- None.

## Suggestions

- Add a `bool push_constants` (or fold into an existing tier) to `Mel_Gpu_Caps_Raster` so consumers can gate
  push-constant paths on a runtime cap instead of a `#if MEL_GPU_WEBGPU` macro. That would let `gpu-scene`'s
  quad skip become cap-driven and backend-agnostic, and would generalize to any future core-without-push
  backend (MEL-ENGINE-IX). Requires touching caps.h + each backend's cap fill — out of scope here.
- Enable `VK_KHR_shader_draw_parameters` (or the Vulkan 1.1 `shaderDrawParameters` feature) in the Vulkan
  device-create path so the gradient/quad vertex-index SPIR-V is valid, clearing the validation ERROR.
- Emit DXIL in the Slang bundle lane so D3D12 joins the shared-golden matrix (the selector and `gpu-scene`
  are already DXIL-ready; only the bundles are missing).
- Once DXIL+a Windows runner exist, re-measure the cross-host band on real Vulkan/D3D12 and tighten or widen
  the non-Vulkan tolerance from data (it is presently a cross-host guess, since this host measures 0).

## Open questions for Gabbo

1. The non-Vulkan tolerance {delta 4, frac 0.02} is a cross-HOST guard — on macOS every backend diffs at 0.
   Accept this conservative band, or assert exact (delta 0) on macOS and only widen when a divergent host
   actually appears?
2. Push-constant detection for the quad skip is `#if MEL_GPU_WEBGPU` because there is no push-constant cap
   and caps.h is outside my ownership. Want a caps field added (separate task), or is the macro acceptable?
3. The MoltenVK `DrawParameters` validation ERROR on gradient/quad is pre-existing and in backend src —
   should I open a follow-up to enable the feature, or is it already tracked?
