# GPU golden-image regression harness

## Work done

Built a programmatic golden-image compare facility for the GPU module and wired the
existing `gpu-visual` readback tests to assert against a committed macOS-Vulkan reference set.

### New facility — `modules/gpu/test/img_golden.{h,c}`

- `mel_golden_check(backend, name, produced_rgba, w, h, tol, file, line)` loads the reference
  `modules/gpu/test/golden/<name>.ppm` (binary P6, maxval 255), compares its RGB against the
  produced RGBA8 readback, and returns a verdict.
- Tolerance is two-axis: `Mel_Golden_Tolerance { u8 max_channel_delta; f32 max_fraction_exceeding }`.
  A pixel is *offending* when its worst per-channel absolute delta exceeds `max_channel_delta`;
  the image passes when the offending fraction is `<= max_fraction_exceeding`.
- On mismatch it fails the test (via `mel_test_fail` at the caller's `__FILE__/__LINE__`) with full
  diagnostics: offending-count / total, offending fraction vs allowed, max channel delta, the first
  offender's `(x,y)` with expected-RGB and actual-RGBA, and the paths of the two artifacts it writes.
- Artifacts on failure: `<name>.<backend>.produced.ppm` and `<name>.<backend>.diff.ppm`
  (abs-delta amplified ×4, saturated) next to the golden. The `<backend>` infix keeps a later
  Metal/WebGPU/D3D12 run from clobbering Vulkan's artifacts while diffing the **same** golden.
- `MEL_GOLDEN(backend, name, rgba, w, h, tol)` macro wraps the call and aborts the test on failure.
- Reference loading uses the heap allocator (`mel_alloc_heap`) — the test-code idiom — never
  `mel_malloc`/`mel_mallc` (MEL-CODE-003). Header malformation, wrong magic, wrong maxval, truncated
  payload, dimension mismatch, and a missing reference all fail loudly with the exact cause and the
  regenerate hint (MEL-ENGINE-VIII).

### Update escape hatch (default OFF)

`MEL_GPU_GOLDEN_UPDATE=1` makes `mel_golden_check` *rewrite* the reference from the produced image
and return pass-without-assert, logging a WARN naming the file and backend. The gate is strict
(`v[0]=='1' && v[1]=='\0'`), so a stray non-empty value does not silently enable rewrite. Absent the
var, the facility always asserts — no silent default (MEL-CODE-007).

    # mint / re-mint references:
    DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1 MEL_GPU_GOLDEN_UPDATE=1 ./nob test gpu-visual macos --gpu=vulkan
    # assert against references:
    DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1 ./nob test gpu-visual macos --gpu=vulkan

### Reference set minted — `modules/gpu/test/golden/*.ppm`

13 references, oracle = macOS-Vulkan (MoltenVK on Apple M3 Pro): `ubo_bindless`, `sampled_checker`,
`alpha_blend`, `two_targets_0`, `two_targets_1`, `storage_image_checker`, `msaa_resolve_edge`,
`depth_boundary`, `mrt_target_0`, `mrt_target_1`, `wireframe_solid`, `wireframe_wire`, `sync2_barrier`.
A dir-local `.gitignore` excludes the transient `*.produced.ppm` / `*.diff.ppm` failure artifacts.

### Wiring — `modules/gpu/test/test_visual.c`, `modules/gpu/build.c`

Each readback test keeps its existing `test_dump_ppm` and its narrow per-pixel sanity checks (cheap
guard that the golden is not garbage) and adds one `MEL_GOLDEN(...)` after the dump. The
`dispatch_indirect` test produces no image and is untouched. `build.c` gains exactly one additive
line: `img_golden.c` added to the `gpu-visual` sources. `log` is reachable transitively (gpu→log).

## Tolerances chosen + rationale

- `VISUAL_TOL_EXACT = { delta 2, fraction 0.0 }` for the flat-fill / nearest-sampled / blend / MRT /
  depth-boundary / sync2 tests. These render constant or hard-edged regions; on the **same** backend
  the readback is bit-stable, so even fraction 0.0 holds. The ±2 channel slack exists for the
  cross-backend future: it absorbs UNORM rounding and small FP/blend-arithmetic differences between
  MoltenVK and a native backend without false-positiving, while still catching a true regression
  (a wrong color is off by tens of levels, as the perturbation proof shows: delta 77).
- `VISUAL_TOL_EDGE = { delta 8, fraction 0.05 }` for `msaa_resolve_edge` and the two wireframe
  images. These are exactly the rasterizer/coverage-sensitive cases: MSAA resolve weights and
  line/edge rasterization rules differ across backends. ±8 over up-to-5%-of-pixels lets a different
  rasterizer's edge antialiasing diverge along the silhouette while still flagging a gross structural
  change (a missing triangle moves far more than 5% of pixels far more than 8 levels).

Numbers are small but nonzero and are the cross-backend substrate; the same-backend self-check is
effectively exact at these settings.

## Verification

    DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1 MEL_GPU_GOLDEN_UPDATE=1 ./nob test gpu-visual macos --gpu=vulkan   # mint: 11 passed
    DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1                          ./nob test gpu-visual macos --gpu=vulkan   # compare: 11 passed, 0 failed

Perturbation proof: zeroed the first red byte of `ubo_bindless.ppm` (77→0). Run produced exactly
one failure with the precise diagnostic —

    test_visual.c:151: golden[vulkan] 'ubo_bindless': 1/64 pixels exceed delta>2 (0.0156 > allowed 0.0000);
    max channel delta 77; first offender (0,0) expected RGB(0,140,204) actual RGBA(77,140,204,255);
    produced=...ubo_bindless.vulkan.produced.ppm diff=...ubo_bindless.vulkan.diff.ppm

— and wrote both artifacts (produced first pixel = real 77,140,204; diff first pixel = 255,0,0,
isolating the red-channel divergence). Restored the golden byte-identically, removed the artifacts,
re-ran: 11 passed, 0 failed.

Note the suite reports 11 test cases (not 13 images): `two_targets` and `mrt_single_pipeline` each
assert two images inside one test case.

## Kludges / debt confessed

- **PPM carries no alpha; comparison is RGB-only.** The golden stores 3 channels; the facility diffs
  RGB and ignores the produced alpha. An alpha-channel regression on an opaque scene is therefore
  invisible to the golden diff itself.
  *Correction (MEL-ENGINE-VIII): the original claim that "every wired test" carried a `MEL_EXPECT_EQ(px[3], 255u)`
  alpha guard was overstated.* The single-pixel alpha guard was present on only **5 of the 13 images**
  (`ubo_bindless`, `sampled_checker`, `alpha_blend`, `storage_image_checker`, `sync2_barrier`); the
  remaining 8 (`two_targets_{0,1}`, `msaa_resolve_edge`, `depth_boundary`, `mrt_target_{0,1}`,
  `wireframe_{solid,wire}`) had no alpha assertion at all, single-pixel or otherwise.
  This was later closed with an **opt-in full-image alpha check**: `Mel_Golden_Tolerance.assert_opaque_alpha`
  (default OFF, no silent default — MEL-CODE-007) makes the facility assert *every* produced alpha == 255
  across the whole image. It is wired (`VISUAL_TOL_EXACT_OPAQUE`) on the five verified-opaque scenes.
  A PAM/PNG carrier would additionally let the golden hold and diff alpha against a reference — flagged, not done.
- **Reference paths are CWD-relative** (`modules/gpu/test/golden/...`), valid only because `nob test`
  launches the binary from the repo root. A direct `./gpu-visual` from another CWD would report the
  reference as missing (loudly, with the regenerate hint — not a silent pass). No env override for the
  golden root was added; the task did not ask for one.
- **Fixed 512-byte path scratch buffers** (`char path[512]`) in the facility mirror the pre-existing
  idiom in `test_dump_ppm`. These are bounded `snprintf` scratch, not growable collections, so
  MEL-CODE-002 (no `[MEL_MAX_*]` collection arrays) is not in play; still, they are a fixed cap.
- **The narrow per-pixel `MEL_EXPECT` checks were left in place** alongside the golden assertion. They
  are now largely redundant with the golden but cost nothing and document intent per test; I did not
  rip them out to keep the change additive and the diff minimal.
- **Edge tolerances are reasoned, not cross-backend-measured.** No Metal/WebGPU/D3D12 run has yet
  diffed these goldens, so the ±8 / 5% edge budget is an engineering estimate, not an empirically
  fitted bound. The first cross-backend run may need them retuned.

## CLAUDE.md suggestions (recommendations only)

- Document the golden-update affordance (`MEL_GPU_GOLDEN_UPDATE=1`) and the macOS-Vulkan-oracle
  convention somewhere discoverable (a `modules/gpu/test/readme.md` or the build doc), so a future
  agent re-minting after an intentional visual change knows the exact incantation.

## Suggestions

- When the Metal / WebGPU / D3D12 visual suites land, point their readback tests at the **same**
  `golden/*.ppm` via `MEL_GOLDEN("metal"/"webgpu"/"d3d12", ...)` — the facility already takes the
  backend label and writes per-backend artifacts, so cross-backend regression is one macro call away.
- Consider promoting `img_golden.{h,c}` out of `modules/gpu/test/` if other modules (UI, image,
  text rasterization) want the same file-backed pixel-regression substrate; it has no GPU dependency.
- A PAM/PNG carrier would let the golden hold alpha and close the RGB-only gap above; PNG additionally
  shrinks the committed references.
