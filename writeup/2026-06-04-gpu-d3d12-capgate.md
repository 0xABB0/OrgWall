# gpu-d3d12: honest shader target after the #14 cap-gate (task #28)

## Regression

A direct win-pilot run of `gpu-d3d12 --gpu=d3d12` dropped from 22/23 to **15 passed / 7 failed /
1 skipped**. All 7 failures shared one error, e.g.:

    test_d3d12.c:798: required: !mel_gpu_failed(sh.status)
    [ERROR] [gpu] shader_create_from_bytecode: device reports caps.shader.bytecode_passthrough
            false for target 0; refusing  (d3d12/shader.c:41)

(d3d12/shader.c:90 for the compute call sites.)

## Root cause — what the tests actually fed

The 7 failing call sites build their bytecode with `dxc_compile(hlsl, "<stage>_6_0", ...)` — the
standalone Microsoft `dxc.exe` compiling HLSL to a **signed DXIL container** (it loads `dxil.dll`
and signs by default; the gpu-dxil writeup confirms D3D12 accepts the signed container without the
experimental-SM opt-in). The bytecode is therefore *DXIL*, not SPIR-V.

But every call passed that DXIL through the **legacy** `spirv_vertex` / `spirv_fragment` / `spirv`
fields and left `.target` unset. `.target` is the first member of the opt struct, so a designated
initializer that omits it zero-fills it to `0` = `MEL_GPU_SHADER_TARGET_SPIRV` (shader.h:21).

The task-#14 cap-gate (`d3d12/shader.c:37–43`, :86–92) keys on the requested target's cap:
`caps.shader.bytecode_passthrough.dxil = true`, `.spirv` is left `false` (caps.c sets only
`.dxil = true`; the rest of the struct is zero). The tests requested target SPIRV → the gate read
`.spirv == false` → loud `TARGET_UNSUPPORTED`. The gate is **correct**: D3D12 cannot consume
SPIR-V, and the tests were dishonestly labeling DXIL as SPIR-V. The prior 22/23 was a stale
incremental build predating the gate; a clean recompile surfaced the mislabel.

The 7 call sites (all in `modules/gpu/test/test_d3d12.c`):

- `d3d12_pipeline.graphics_create` (~:499) — VS+PS
- `d3d12_bindless.sample_texture_readback` (~:553) — VS+PS
- `d3d12_compute.storage_buffer_bindless` (~:623) — CS
- `d3d12_compute.storage_image_bindless` (~:690) — CS
- `d3d12_bind_group.classic_descriptor_set` (~:795) — VS+PS
- `d3d12_bind_group.classic_churn_under_submission` (~:934) — VS+PS
- `d3d12_bind_group.classic_uniform_buffer` (~:1115) — VS+PS

(The task named three classic bind-group tests explicitly; "the others" are these four — the two
bindless graphics/compute tests and the storage-image compute test. All 7 hit the same gate.)

## Fix — honest target declaration (test-only)

Each call now declares its real target and uses the neutral blob fields:

- graphics: `.target = MEL_GPU_SHADER_TARGET_DXIL, .vertex_blob = …, .vertex_blob_size = …,
  .fragment_blob = …, .fragment_blob_size = …`
- compute: `.target = MEL_GPU_SHADER_TARGET_DXIL, .compute_blob = …, .compute_blob_size = …`

`caps.shader.bytecode_passthrough.dxil` is true on d3d12, so the cap-gate now passes truthfully.
The neutral `*_blob` fields are the same ones the gpu-scene DXIL selector already feeds (gpu-dxil
writeup), so the from_bytecode path consuming them is exercised and correct. 7 insertions /
7 deletions, one file.

**No `d3d12/shader.c` change was needed.** The legacy-field target inference is not wrong: the
backend has no way to know DXIL-in-a-`spirv_` field is really DXIL — the *caller* asserted SPIRV by
leaving `.target = 0`. The cap-gate did exactly its job. Fixing the caller's declaration is the
honest fix; weakening the gate or auto-coercing the legacy field's target would re-hide the lie.

## Verification (win-pilot, clean build, RTX 2060 SUPER)

`build/win32-debug` was wiped (`rmdir /s /q`) and `git reset --hard` applied before the build, so
all 24 d3d12 objects recompiled from scratch — no stale objects (the prior 22/23 was a stale
incremental build that masked the regression). Build: 24/24 ok (only pre-existing `getenv`/`fopen`
MSVC deprecation warnings in the test's `dxc_compile` helper, not from this change).

- `gpu-d3d12 --gpu=d3d12`: **22 passed / 0 failed / 1 skipped of 23.** Every test `ok` except the
  one environmental skip `d3d12_swapchain.present_clear_readback` (non-interactive service window
  station has no DWM/desktop). All 7 regressed tests recovered:
  `d3d12_pipeline.graphics_create`, `d3d12_bindless.sample_texture_readback`,
  `d3d12_compute.storage_buffer_bindless`, `d3d12_compute.storage_image_bindless`,
  `d3d12_bind_group.classic_descriptor_set`, `d3d12_bind_group.classic_churn_under_submission`,
  `d3d12_bind_group.classic_uniform_buffer`.
- `gpu-scene --gpu=d3d12`: **4 passed / 0 failed / 0 skipped of 4** (triangle,
  triangle_multistream, gradient, quad). Unaffected — test-only, d3d12-gated change.

The outer `nob test` wrapper prints `FAILED / 1 failed` because it counts the single skip as a
non-pass at the suite level; the authoritative inner harness line is `22 passed, 0 failed, 1
skipped`. This skip-as-failed wrapper behavior is pre-existing (the prior 22/23 baseline carried
the same skip).

Left win-pilot clean on `main` (`git checkout main && git reset --hard origin/main`).

macOS is unaffected: `test_d3d12.c` is wholly `#if MEL_GPU_D3D12`-gated and never compiles on
macOS. No shared file touched, so `gpu-vulkan`/`gpu-metal`/`gpu-webgpu` are untouched.

## Kludges / debt (MEL-ENGINE-VIII)

- **None introduced.** The change is a pure test-correctness fix.
- **Pre-existing latent trap, flagged not fixed:** the legacy `spirv_*` fields on the d3d12 opt
  structs default `.target` to `0 = SPIRV` while actually carrying DXIL at every d3d12 call site —
  a footgun: any future d3d12 caller that copies the old pattern re-trips the gate. The legacy
  fields exist only as a transitional fallback (gpu-dxil writeup). They could be removed from the
  d3d12 from_bytecode paths entirely now that no test uses them, making DXIL-via-`*_blob` the only
  d3d12 path and the `.target` declaration mandatory. Out of this task's scope; left for Gabbo.

## CLAUDE.md suggestions (recommendations only)

- None.

## Suggestions

- The shader.h opt structs put `target` first, so a `{ .spirv = … }` initializer silently means
  "target SPIRV". A backend-agnostic guard could `mel_assert` in from_bytecode that a non-legacy
  blob field is paired with a matching non-zero target, catching the mislabel at the call site
  rather than at the cap-gate. (The gate already catches it loudly; this would just localize the
  message to the wrong-field combination.)
- Retire the d3d12 `spirv_*` legacy fallback now that nothing feeds it; force every d3d12 caller to
  declare DXIL through `*_blob` + `.target`.

## Open questions for Gabbo

1. Remove the d3d12 `spirv_*` legacy fallback from `d3d12/shader.c` (and the `spirv_*` members
   from the shared opt structs if no other backend needs them)? It is now dead on d3d12 and is the
   exact footgun that caused this regression. I left it untouched per file ownership.
2. The gpu-hygiene writeup's open question — "remove the dead SPIRV branch from the d3d12 target
   check entirely" — is now doubly motivated: no test labels DXIL as SPIRV anymore, and the cap
   rejects SPIRV regardless. Want that branch (`d3d12/shader.c:30,79`) reduced to DXIL-only?
