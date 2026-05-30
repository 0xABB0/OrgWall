# GPU RHI API Review And Fixes

## Work done

Reviewed `design/gpu-rhi.md` against the repository commandments and checked unstable API claims against primary Vulkan, DirectX, and WebGPU documentation.

Updated `design/gpu-rhi.md` to fix the reviewed issues: D3D12 SM 6.9 versus SM 6.10 preview feature boundaries, D3D12 ROV versus attachment raster ordering, reconstruction frame packet output shape, IO command-list validity, C-compatible direct/indirect destroy entry points, queue fallback warnings, WebGPU resolve wording, present-wait versus present-timing caps, conditional render-graph false-edge semantics, calibrated timestamp caps, provider count, cap names, stale section references, and module/milestone references.

## Kludges

None.

## CLAUDE.md suggestions

None.

## Suggestions

Next pass should be a generated caps inventory check once headers exist, so prose examples cannot drift from `caps.h`.
