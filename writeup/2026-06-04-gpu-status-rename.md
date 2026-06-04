# GPU status rename: `*_VK_FAILED` → `*_BACKEND_FAILED`

## Work done

Renamed the Vulkan-flavored per-resource status enumerators to backend-neutral
names across the shared GPU headers and all four backends. The symbolic name
read wrong whenever a non-Vulkan backend (D3D12 on `HRESULT`, Metal, WebGPU)
returned it; the numeric value was already correct. Pure, behavior-identical
token rename `_VK_FAILED` → `_BACKEND_FAILED`. No numeric status value, no
severity encoding, no result-struct shape changed.

Enumerators renamed (old → new), each keeping its `MEL_GPU_STATUS(code, sev)`
value and low-2-bit `Mel_Gpu_Severity` encoding (design/gpu-rhi.md §3.2):

- `MEL_GPU_DEVICE_CREATE_VK_FAILED`       → `MEL_GPU_DEVICE_CREATE_BACKEND_FAILED`        (code 4, ERROR)
- `MEL_GPU_SHADER_CREATE_VK_FAILED`       → `MEL_GPU_SHADER_CREATE_BACKEND_FAILED`        (code 1, ERROR)
- `MEL_GPU_QUERY_POOL_CREATE_VK_FAILED`   → `MEL_GPU_QUERY_POOL_CREATE_BACKEND_FAILED`    (code 3, ERROR)
- `MEL_GPU_QUERY_RESOLVE_VK_FAILED`       → `MEL_GPU_QUERY_RESOLVE_BACKEND_FAILED`        (code 2, ERROR)
- `MEL_GPU_SYNC_CREATE_VK_FAILED`         → `MEL_GPU_SYNC_CREATE_BACKEND_FAILED`          (code 1, ERROR)
- `MEL_GPU_SAMPLER_CREATE_VK_FAILED`      → `MEL_GPU_SAMPLER_CREATE_BACKEND_FAILED`       (code 1, ERROR)
- `MEL_GPU_BUFFER_CREATE_VK_FAILED`       → `MEL_GPU_BUFFER_CREATE_BACKEND_FAILED`        (code 2, ERROR)
- `MEL_GPU_TRANSFER_VK_FAILED`            → `MEL_GPU_TRANSFER_BACKEND_FAILED`             (code 2, ERROR)
- `MEL_GPU_TEXTURE_CREATE_VK_FAILED`      → `MEL_GPU_TEXTURE_CREATE_BACKEND_FAILED`       (code 2, ERROR)
- `MEL_GPU_TEXTURE_VIEW_CREATE_VK_FAILED` → `MEL_GPU_TEXTURE_VIEW_CREATE_BACKEND_FAILED`  (code 1, ERROR)
- `MEL_GPU_PIPELINE_CREATE_VK_FAILED`     → `MEL_GPU_PIPELINE_CREATE_BACKEND_FAILED`      (code 1, ERROR)

11 enumerators across 9 headers (device.h, shader.h, query.h ×2, sync.h,
sampler.h, buffer.h, transfer.h, texture.h ×2, pipeline.h).

Return sites updated per backend: vulkan 23, metal 12, d3d12 9, webgpu 6
(50 total). 28 files, 61 insertions / 61 deletions; every changed line is a
single-token swap, no other edits.

No tests referenced the old symbols by name (the only out-of-`src` matches were
historical `writeup/` recaps, left untouched).

## Verification

macOS (`DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1`):
- gpu-vulkan --gpu=vulkan: 48/48
- gpu-metal: 6/6
- gpu-webgpu --gpu=webgpu: 4/4
- gpu-visual --gpu=vulkan: 13/13
- gpu-scene: vulkan 3/3, metal 3/3, webgpu 2/3 (1 skip: `scene_shared.quad`,
  WebGPU core lacks push constants — pre-existing capability skip, unrelated)
- hello-gpu builds clean on vulkan / metal / webgpu

WIN-PILOT (d3d12 — only compiles on Windows; the whole point, since d3d12 was
returning these enumerators on HRESULT):
- `nob build gpu-d3d12 --gpu=d3d12`: compiles clean with the neutral names
- `nob test gpu-d3d12 --gpu=d3d12`: result recorded in the orchestrator report

## Kludges

None in the code. Pure rename; no shortcuts.

Process note (not a code kludge): the macOS edits + test runs were performed in
the shared primary checkout rather than this worktree, because the verification
`cd`'d to the shared repo root. The shared checkout was reverted to clean `main`
(no commit, no branch move), and the identical rename was reproduced in this
worktree; the two diffs were verified byte-identical before committing here. The
macOS green results above were produced against that identical content on the
same base commit, so they hold for this worktree.

## CLAUDE.md suggestions

None.

## Suggestions

- A follow-up audit of `vk`-prefixed identifiers in shared (non-`src/vulkan`)
  code could catch any remaining Vulkan-flavored naming leakage now that the
  public enumerators are backend-neutral.
