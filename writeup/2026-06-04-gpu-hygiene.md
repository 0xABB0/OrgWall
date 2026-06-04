# GPU red-team hygiene fixes (three independent)

Three small, independent hygiene fixes from the GPU red-team audits, each its own commit.
macOS GPU suites run with `DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1`.

## FIX 1 (task #25) — DrawParameters validation ERROR

**Symptom.** The gradient/quad SPIR-V (Slang lowers `SV_VertexID`/base-vertex via the SPIR-V
`DrawParameters` capability) declares that capability, but the Vulkan device never enabled
`shaderDrawParameters`. MoltenVK logged one validation ERROR per affected pipeline-create:
`vkCreateShaderModule(): SPIR-V Capability DrawParameters was declared, but one of the following
requirements is required (VkPhysicalDeviceVulkan11Features::shaderDrawParameters OR
VK_KHR_shader_draw_parameters)`. Two ERRORs in the `gpu-scene` vulkan run (gradient + quad).

**Fix.** Request-and-grant (§3.4, MEL-CODE-007):
- `modules/gpu/src/vulkan/caps.c` — probe `shaderDrawParameters` via
  `VkPhysicalDeviceShaderDrawParametersFeatures` chained into the existing features-2 probe; record
  it in `out->shader.draw_parameters`.
- `modules/gpu/src/vulkan/device.c` — when the adapter exposes it, chain a
  `VkPhysicalDeviceShaderDrawParametersFeatures{.shaderDrawParameters=VK_TRUE}` into the device-create
  pNext chain (woven so it is reached whether or not dynamic-rendering is present), and report the
  granted value in `dev->caps.shader.draw_parameters`.
- `modules/gpu/include/gpu/caps.h` — new `bool draw_parameters` in `Mel_Gpu_Caps_Shader`.
- Set the cap explicitly on every backend (no silent default, MEL-CODE-007): metal `false`, webgpu
  `false` (MSL/WGSL do not carry the SPIR-V capability), d3d12 `true` (DXIL exposes SV_VertexID /
  base-vertex natively).

MoltenVK exposes `shaderDrawParameters` as a core Vulkan 1.1 feature, so no extension was added to the
enabled set (still 5: dynamic_rendering, portability_subset, swapchain, synchronization2, memory_budget).

**Verification.** `gpu-scene` vulkan: DrawParameters VUID count 0 (was 2), 3/3 pass. `gpu-vulkan`
48/48, `gpu-visual` 13/13.

## FIX 2 (task #11) — golden harness alpha coverage + diagnostics

Files: `modules/gpu/test/img_golden.{h,c}`, `test_visual.c`, the harness writeup, golden `.gitignore`.

- **(a) Opt-in opaque-alpha check.** Added `Mel_Golden_Tolerance.assert_opaque_alpha` (default OFF —
  the existing `VISUAL_TOL_EXACT/EDGE` designated initializers leave it 0, no silent default,
  MEL-CODE-007). When set, the facility asserts *every* produced alpha == 255 across the whole image,
  closing the RGB-only blind spot for opaque scenes. Wired via a new `VISUAL_TOL_EXACT_OPAQUE` on the
  five verified-opaque scenes (`ubo_bindless`, `sampled_checker`, `alpha_blend`,
  `storage_image_checker`, `sync2_barrier`).
- **(b) Writeup correction.** The harness writeup claimed "every wired test" carried a single-pixel
  `px[3]==255u` alpha guard; reality was 5/13 images. Stated accurately (MEL-ENGINE-VIII) and noted the
  new opt-in full-image check.
- **(c) M1 — precise cause.** The PPM loader now threads its precise failure cause (bad magic /
  unsupported maxval / truncation / short payload / fopen errno) out to `mel_golden_check`'s assertion
  line, replacing the generic "missing or unreadable".
- **(d) L1 — PPM grammar.** Replaced the single-`fgetc`-after-maxval assumption with a header tokenizer
  that skips whitespace runs and `#` comments between header fields, then consumes exactly one
  whitespace before the binary raster (binary pixel data is *not* skipped).
- **(e) L2 — usize.** `pixel_count = width*height` is computed in `usize` (was a `u32` multiply).

To let self-tests assert the detectors without tripping `mel_test_fail`, `mel_golden_check` now
delegates to a non-asserting `mel_golden_compare` returning `{bool pass; char message[512]}`; the
public `mel_golden_check` wraps it and calls `mel_test_fail("%s", r.message)` on failure.

Two self-tests added (run in the gpu-visual suite, no GPU device needed):
- `visual_golden.opaque_alpha_catches_regression` — a produced buffer with one alpha=200 against
  `ubo_bindless` FAILS with the opaque-alpha message; the same buffer under the non-opaque tolerance
  does not raise an opaque-alpha message.
- `visual_golden.loader_reports_precise_cause` — writes a temp golden (`__selftest_badppm.ppm`,
  gitignored) with bad magic / maxval 127 / truncated payload and asserts each precise cause surfaces.

**Verification.** `gpu-visual` 13/13 (11 scenes + 2 self-tests). `gpu-scene` all backends unaffected.

## FIX 3 (task #14) — Slang frontend hygiene

Files: `apps/hello-gpu/shaders/gen_bundles.sh`, `modules/gpu/src/{vulkan,d3d12}/shader.c`,
`modules/gpu/src/metal/macos/pipeline.m`, `modules/gpu/src/webgpu/shader.c`.

- **(a) Enforce the slang pin.** `gen_bundles.sh` no longer rewrites `SLANG_VERSION.lock` to whatever
  `slangc` it finds. It now asserts the discovered `slangc -v` equals the committed
  `SLANG_VERSION=` pin and fails loud on mismatch (or a missing/malformed lock). No silent regen.
- **(b) Blob/legacy exclusivity assert.** The shader-create paths silently preferred `*_blob` over the
  legacy `spirv_*` field when both were set. Added a debug `mel_assert` that the two are not both
  non-NULL, on the backends that carry a `spirv_*` fallback (vulkan, d3d12). MEL-CODE-007.
- **(c) Cap-gate.** `caps.shader.bytecode_passthrough.{spirv,msl,dxil,wgsl}` were set by every backend
  but never read. `shader_create_from_bytecode` (graphics + compute) now loud-fails BEFORE the backend
  call when the device's cap for the requested target is false — the cap is the single source of truth.
  Per-backend (vulkan→spirv, metal→msl, webgpu→wgsl, d3d12→dxil/spirv by requested target).

**Note on the d3d12 SPIRV target.** The d3d12 backend's target check accepts both DXIL and SPIRV, but
its honest cap is `dxil` only (`spirv=false`). The new gate keys on the requested target's cap, so a
SPIRV request to d3d12 would now be loud-rejected — which is correct, since d3d12 does not actually
consume SPIR-V. No test/app exercises that path (`mel_bundle_select_graphics` already keys on the same
caps and never selects SPIRV for d3d12), so the gate rejects no valid path.

**Verification.** Gate rejects no valid path: `gpu-vulkan` 48/48, `gpu-metal` 6/6, `gpu-webgpu` 4/4,
`gpu-scene` vulkan 3/3 / metal 3/3 / webgpu 2/3, `hello-gpu` builds macos vulkan/metal/webgpu. The pin
enforcement logic was unit-checked in isolation (matching version passes, mismatch fails loud); the
full mint was not run here because no `slangc` prefix is present in this worktree (fetched per-host).

## Final green-run counts (all required suites)

- gpu-vulkan 48/48, gpu-metal 6/6, gpu-webgpu 4/4
- gpu-visual 13/13 (was 11; +2 self-tests)
- gpu-scene: vulkan 3/3, metal 3/3, webgpu 2/3 (1 skipped, expected)
- hello-gpu builds: macos vulkan / metal / webgpu all succeed
- DrawParameters VUID in gpu-scene vulkan: 0 (was 2)

## Kludges / debt confessed (MEL-ENGINE-VIII)

- **gen_bundles.sh pin enforcement not run end-to-end here.** No `slangc` prefix exists in this
  worktree, so the full mint path was not exercised; only the version extract+compare branch was
  unit-checked. The lock pins `2026.10.2`.
- **The five opaque-alpha-wired scenes are a conservative subset.** I wired `assert_opaque_alpha` only
  on the scenes whose full-image opacity I reasoned through and which already had a single-pixel alpha
  guard. `two_targets`/`mrt` are also opaque (clear alpha 1, opaque fragments) but were left unwired to
  keep the change conservative; wiring them is a one-line tolerance swap if wanted.
- **`__selftest_badppm.ppm` is written into the committed golden dir during the self-test.** It is
  removed on success and is now gitignored (`__selftest_*.ppm`), so an interrupted run cannot leak a
  committed file — but it does touch the committed directory transiently.
- **Cap-gate is per-backend, not a shared helper.** There is no shared `modules/gpu/src/shader.c`; each
  backend has its own. The gate reads exactly one cap per backend (the backend's single accepted
  target), so it is not duplicated logic so much as each backend gating on its own truth. A future
  shared inline helper keyed on `Mel_Gpu_Shader_Target` would consolidate it if a fifth backend lands.

## CLAUDE.md suggestions (recommendations only)

- Document that `gen_bundles.sh` now ENFORCES the slang pin (fails on mismatch) rather than regenerating
  it; a bump now requires editing both `third-party/slang/build.c` (SLANG_VERSION macro) and
  `tools/build/vendor/slang/SLANG_VERSION.lock` deliberately.

## Suggestions

- A PAM/PNG golden carrier would let the reference hold alpha and diff it directly, superseding the
  opt-in opaque-alpha flag for the general (non-opaque) case.
- If a shared GPU-frontend C file ever appears, fold the per-backend cap-gate + blob/legacy-exclusivity
  assert into one place keyed on `Mel_Gpu_Shader_Target`.

## Open questions for Gabbo

- Should `two_targets` and `mrt` also carry `assert_opaque_alpha`? They are opaque; I left them unwired
  to stay conservative.
- The d3d12 backend still *accepts* `MEL_GPU_SHADER_TARGET_SPIRV` in its target check even though the
  cap-gate now rejects it (spirv cap false on d3d12). Want the dead SPIRV branch removed from the d3d12
  target check entirely, now that the cap-gate is the single source of truth?
