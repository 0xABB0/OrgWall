# `render.reconstruction` — temporal upscaling, denoising, ray reconstruction, frame generation

A provider-backed image-pipeline stage that turns an internally-rendered, jittered, motion-vector-bearing frame stream into the presentation stream the swapchain consumes. Three sibling submodules share this doc because they share lowering machinery, cap negotiation, and the per-frame descriptor: `render.reconstruction` proper (super resolution + frame generation + denoise), `render.reconstruction.ray_regen` (separable path-traced GI reconstruction), and `render.reconstruction.radiance_cache` (separable cached-radiance GI provider). All three sit downstream of `render.graph` passes and upstream of the swapchain — never the other way around — and all three resolve provider identity at request time, never by inference.

## Inherited principles

- **P1 — emulate-to-equivalent.** Engine TAAU and a CAS/RCAS-like spatial sharpener exist as honest fallbacks when no vendor provider is loaded. The API shape — context, frame packet, cap surface, frame descriptor — is identical across providers; only the lowering differs. Where a backend cannot grant a cap, the cap reports `none` (∴ the app sees absence, never a fabricated success). MEL-ENGINE-VII applies: on a backend without DLSS 5 (every backend at 2026-05-28), the request fails the cap check rather than silently degrading to DLSS 4.5.
- **P2 — full-control escape.** Streamline / FidelityFX SDK / XeSS / MetalFX / GSR provider handles expose their native SDK objects through `gpu.native` interop headers for the app that needs to drive the SDK directly. The NGX/NVAPI direct path is a separate P2 provider sibling to the Streamline-mediated NVIDIA path.
- **MEL-ENGINE-V — respect the user's product.** The app supplies provider preference order in the create descriptor; the engine walks the list in order, takes the first that satisfies the requested caps, and reports the resolved provider back. The engine does not impose a default provider; the engine TAAU fallback is the floor, not the recommendation.
- **MEL-ENGINE-VIII — fail with honor.** Provider-not-shipped resolves to `MissingProvider` (DLSS 5 today). Provider-cap-insufficient resolves to a typed cap-check failure. The engine never substitutes a different provider than the one the app requested; the engine never generates a hidden present.
- **MEL-ENGINE-X — the engine is the wingman.** Melody ships no headline upscaler. The engine TAAU exists so the app's pipeline does not collapse when a vendor SDK is absent on a target; it is not a competitor to DLSS/FSR/XeSS/MetalFX/GSR.

## Public objects

- **`Mel_Reconstruction_Context`** — the provider-backed object for super resolution, denoise, and frame generation. One context per pipeline stage instance; the app may hold multiple (game view + minimap, primary swapchain + spectator swapchain) with different provider preferences and different cap requirements per context.
- **`Mel_Reconstruction_Frame_Packet`** — the per-dispatch result. Carries:
  - `real_output` — the reconstructed frame derived from the real simulation tick.
  - `generated_outputs[]` — zero or more generated frames (empty for pure SR/denoise providers; one for `single_interpolated`; up to N for `multi_frame`).
  - `present_records[]` — one entry per output (real and generated), each carrying its present ID and pacing record.
  - `latency_records[]` — one entry per output, carrying the latency-marker record the provider emitted.
  - provider status — the resolved provider identity, granted-cap snapshot, ABI / SDK / driver / architecture identifiers (these participate in pipeline and effect cache keys upstream).
  - This packet shape is required by `frame_generation = multi_frame`; a single texture result cannot encode multiple generated frames without hidden presents (∴ MEL-ENGINE-VIII).
- **`Mel_Ray_Regeneration_Context`** — the separable ray-reconstruction context. Distinct object kind because its inputs (path-traced GI samples, ray hits) and its output (denoised radiance) differ from the pixel-domain inputs/outputs of `Mel_Reconstruction_Context`. The two contexts may coexist on the same frame: ray regen runs in the GI stage, reconstruction proper runs after final shading.
- **`Mel_Radiance_Cache_Context`** — the separable GI provider context. Output is a sparse cached-radiance volume the renderer samples back in subsequent frames, not a presented frame. Distinct from frame generation because the consumer is a shader's sampling code, not the swapchain.

## Provider enum

The full enum, version qualifiers preserved verbatim. Version is part of the request descriptor; the engine never picks a version for the app.

- `EngineTaaU` — engine TAAU fallback.
- `EngineSpatialUpscale` — engine spatial upscaler fallback (CAS/RCAS-like sharpening).
- `NvidiaDlss { version: 3 | 4 | 4_5 | 5 }`
  - DLSS 4.5 (March 31 2026, NVIDIA driver 595.79 WHQL) ships Dynamic MFG and MFG-6× (6× requires RTX 50).
  - DLSS 5 pre-announced at GTC 2026, Fall 2026 launch; the request resolves to `MissingProvider` until the SDK lands.
- `NvidiaNis` — NVIDIA Image Scaling, the spatial-only path.
- `AmdFsr { version: 3_x | 4_x_redstone }`
  - FSR 3.x is the Vulkan-capable cross-vendor path.
  - FSR 4.x ("Redstone" suite, FidelityFX SDK 2.2 March 2026) is the RDNA 4 + SM 6.4 + DX12-only path that bundles Upscaling 4.1, Ray Regeneration 1.1, Frame Generation 4.0, and Radiance Caching 0.9 (technical preview). On Vulkan the request resolves to FSR 3.x or the engine fallback; FSR 4 Redstone fails the cap check on every non-D3D12 backend.
- `IntelXeSS { version: 2_x | 3 }`
  - XeSS 3 SDK (~March 2026) supersedes 2.x; adds XeSS-FG (up to 3 AI frames) and XeLL low-latency. Cross-vendor on SM 6.4 hardware, closed-source binaries.
- `AppleMetalFX` — spatial scaler, temporal scaler, frame interpolator, and temporal denoised scaler where the OS revision grants them.
- `QualcommSnapdragonGsr { version: 1 | 2 }` — mobile spatial / temporal provider.
- `RuntimeXrReprojection` — XR-runtime-provided reprojection / timewarp. The XR-safe default path. Selected automatically when the output enters an XR runtime and no provider with `xr_safe = provider_certified` was loaded.

## Capability surface

Six caps. Each provider reports its granted values at request time; the app's cap requirements gate the request.

- `super_resolution = none | spatial | temporal | ml_temporal`
- `ray_reconstruction = none | denoise | provider_ml`
- `frame_generation = none | single_interpolated | multi_frame`
- `hud_separation = none | app_composited_after | provider_composited`
- `xr_safe = false | runtime_only | provider_certified`
- `hdr = none | sdr_only | hdr10 | sc_rgb | platform_hdr`

The cap surface is the negotiation interface. The app declares "I need `frame_generation ≥ single_interpolated` and `hdr ≥ hdr10`"; the engine walks the preference order and returns the first provider that satisfies. A satisfied cap surface is part of the pipeline and effect cache keys, alongside provider ABI version, SDK version, driver version, and GPU architecture identifiers — a cache hit demands all of them match (∴ a driver update invalidates the affected entries rather than silently reusing them).

## Creation and dispatch

`reconstruction_context_create(device, desc) -> future<Mel_Reconstruction_Context>`

The create descriptor pins, at minimum: provider preference order; internal render size; output size; color space; HDR metadata; motion-vector convention (scale + low-res-vs-output-res flag); depth convention (linear or non-linear, near-vs-far reversed); jitter convention (jitter source identifier, matching the pacing source's jitter generator); exposure convention (pre-multiplied luminance vs separate exposure texture); reactive-mask convention; whether the output enters a normal swapchain or an XR runtime. The descriptor's fields participate in the effect cache key alongside the resolved provider identity.

`reconstruction_dispatch(context, frame_desc) -> future<Mel_Reconstruction_Frame_Packet>`

The future resolves when the provider has emitted all outputs and recorded all latency / pacing markers. The render graph treats the dispatch as a graph pass; the provider's internal command-list recording rides the graph's command-list lifetime.

`reconstruction_context_release(context)` releases the provider's internal state. Pipeline cache entries keyed on this context survive release; the next create with a matching descriptor restores them.

`Mel_Ray_Regeneration_Context` and `Mel_Radiance_Cache_Context` follow the same `create / dispatch / release` triad against their own descriptor and frame-descriptor shapes (described in their submodule sections).

## Per-frame `frame_desc`

The descriptor the app populates for `reconstruction_dispatch`. Every field is explicit; the engine fabricates nothing.

- color input.
- depth input.
- motion vectors, with declared scale and declared whether they are low-resolution or output-resolution.
- exposure or luminance texture.
- jitter offsets — current and prior, consumed from `Frame_Info.jitter_offset_current` and `Frame_Info.jitter_offset_prior`. Pacing owns the jitter sequence; reconstruction consumes it. See docs/frame-pacing.md.
- camera matrices — current and prior.
- near and far planes.
- FOV per view (one entry under flat rendering, two under stereo XR).
- reactive / transparency / disocclusion masks where the app's pipeline produces them.
- HUD / UI texture if the resolved provider's `hud_separation = provider_composited`; otherwise UI is composited after reconstruction.
- frame ID, present ID, and the pacing metadata from `Frame_Info` (`predicted_next_present_ns` where granted, `mode_state`, thermal / power signals). See docs/frame-pacing.md.

## Provider lowerings

- **NVIDIA.** Streamline / DLSS for super resolution, ray reconstruction, frame generation, and Reflex coordination. The Streamline mediation is the default path. NGX / NVAPI direct path remains a P2 provider for the app that needs to drive NGX without the Streamline layer.
- **AMD.** FidelityFX SDK for FSR super resolution and frame generation, with Anti-Lag coordination. FSR 4 Redstone's Ray Regeneration 1.1 and Radiance Caching 0.9 lower through the FidelityFX SDK 2.2 entry points and surface through the ray_regen and radiance_cache contexts, not through `Mel_Reconstruction_Context` (∴ separability is real, not cosmetic).
- **Intel.** XeSS for super resolution, XeSS-FG for frame generation, XeLL for latency pacing. Closed-source binaries; the engine treats the XeSS DLL/`.so` as a loadable provider whose presence the build records.
- **Apple.** MetalFX spatial scaler, temporal scaler, frame interpolator, and temporal denoised scaler where the OS revision grants them. The temporal denoised scaler is the ray-reconstruction provider on Metal.
- **Qualcomm.** Snapdragon Game Super Resolution / GSR2 as a mobile spatial or temporal provider where the device's Adreno generation grants it.
- **Engine fallback.** TAAU, CAS/RCAS-like sharpening, and a simple spatial upscaler. No frame generation in the engine fallback (∴ `frame_generation = none` for `EngineTaaU` and `EngineSpatialUpscale`).

## Provider resolution

The create descriptor's preference order is walked in order. For each entry, the engine: loads the provider through the provider registry (see docs/provider.md) if not already loaded; queries the provider's cap surface against the descriptor's required caps; queries the backend's support against the provider's hard requirements (`AmdFsr { version: 4_x_redstone }` demands D3D12 + RDNA 4 + SM 6.4; `NvidiaDlss { version: 5 }` demands the not-yet-shipped DLSS 5 SDK; etc.); and, on first satisfaction, binds the provider and returns the context. A descriptor whose preference order exhausts without a satisfaction returns a typed failure (the engine does not silently substitute an unrequested provider). The resolved provider's identity, granted caps, and version identifiers are recoverable through the context for the app's logging, telemetry, and UI ("Reconstruction: DLSS 4.5, ML temporal, single-interpolated frame-gen").

Failure modes are typed (∴ MEL-ENGINE-VIII):

- `MissingProvider` — the provider's SDK / driver / hardware is not present on this target (e.g. `NvidiaDlss { version: 5 }` at 2026-05-28; `AmdFsr { version: 4_x_redstone }` on Vulkan).
- `InsufficientCaps` — the provider is present but its granted caps do not satisfy the descriptor's required caps.
- `IncompatibleBackend` — the provider is present and capable, but the backend is wrong (e.g. requesting `AmdFsr { version: 4_x_redstone }` on a Metal backend).
- `LicensingAbsent` — the build did not ship this provider (licensing / redistribution constraints are build-system facts; the runtime reports the absent provider honestly).

## Multi-context composition

The three context kinds compose on a single frame without special pleading (∴ MEL-ENGINE-IX). A pipeline that runs all three looks like: the GI stage feeds `Mel_Radiance_Cache_Context` (the cache updates against this frame's primary-hit samples) and `Mel_Ray_Regeneration_Context` (the path-traced GI samples denoise into final radiance); the shading stage samples the cache and consumes the denoised radiance; the post-shading stage feeds `Mel_Reconstruction_Context` (super resolution + frame generation). Each context owns its own provider handle, cap surface, and frame descriptor; the render graph sequences their passes against the rest of the frame's barrier discipline.

Cross-context coupling is explicit. The radiance cache's prior-frame state is the app's responsibility to persist (the cache handle survives across frames; the context does not own frame retirement). The denoised radiance flows from `ray_regen` to the shading pass through a normal render-graph resource. The reconstruction proper consumes the final shaded frame, exactly as it would if no ray_regen or radiance_cache provider were loaded.

## Frame-generation rules

Pinned per MEL-ENGINE-VIII; load-bearing.

- The engine never generates hidden presents. Every real and generated frame has a present ID, a pacing record, and a latency-marker record, surfaced in the frame packet.
- UI / HUD and pointer layers are either provider-composited through the explicit `hud_separation = provider_composited` provider input, or composited after the generated frames. Never injected silently into a generated frame's color buffer (∴ MEL-ENGINE-VIII).
- Generated frames cannot consume game simulation state that does not exist. The game simulation callback runs once per real simulation frame; the generated frames interpolate or extrapolate from the real frame's state, never invent new state. The render-graph passes that feed reconstruction run once per real frame; only the provider's internal extrapolation kernel runs per generated frame.
- XR frame generation is off by default. Enabled only through an XR-runtime-approved path or through a provider whose granted caps report `xr_safe = provider_certified`. Otherwise XR uses `RuntimeXrReprojection` and the runtime's reprojection / timewarp path. See docs/xr.md.

## `render.reconstruction.ray_regen`

A separable submodule with its own context (`Mel_Ray_Regeneration_Context`), its own provider request, and its own cap surface. Separability is not cosmetic: ray reconstruction consumes path-traced GI samples and ray hits and produces denoised radiance; it does not consume a final-shaded frame and does not produce pixels for the swapchain. A single pipeline may run ray regen in the GI stage and run `Mel_Reconstruction_Context` in the post-shading stage; the two are orthogonal (∴ MEL-ENGINE-IX).

Distinct from `render.gpu.§9.5` ray-and-path-tracing optimization providers: §9.5 covers SER hints, opacity micromaps, and BVH-builder tuning — RT-pipeline-tuning that runs upstream of any reconstruction. `ray_regen` is the image-pipeline provider that consumes §9.5's output.

Providers granted at this slot:

- `NvidiaDlss { version: 4 | 4_5 | 5 }` — DLSS Ray Reconstruction, the ML denoiser path. Resolved through Streamline.
- `AmdFsr { version: 4_x_redstone }` — FSR 4 Redstone's Ray Regeneration 1.1, lowered through FidelityFX SDK 2.2. RDNA 4 + SM 6.4 + DX12-only; fails the cap check on every non-D3D12 backend.
- `AppleMetalFX` — temporal denoised scaler where the OS revision grants it.
- `EngineTaaU` — fallback path: temporal accumulation across GI samples, no ML kernel. The fallback's granted `ray_reconstruction = denoise` rather than `provider_ml`.

Cap surface for `ray_regen`:

- `ray_reconstruction = none | denoise | provider_ml`
- `hdr = none | sdr_only | hdr10 | sc_rgb | platform_hdr` (the denoised radiance carries the same color-space discipline as the post-shading frame).

The per-frame descriptor for `ray_regen` carries path-traced GI samples, ray hit metadata, prior denoised radiance, motion vectors and jitter (same convention as `Mel_Reconstruction_Context`), and the camera matrices.

## `render.reconstruction.radiance_cache`

A separable submodule with its own context (`Mel_Radiance_Cache_Context`). Separability is real: the output is a sparse cached-radiance volume the renderer samples back in subsequent frames (∴ the consumer is a shader's sampling code), not a presented frame and not a denoised image. Distinct from frame generation because no frame is presented; distinct from ray_regen because the data lives across frames and is sampled, not consumed as a one-shot denoise.

Providers granted at this slot:

- `AmdFsr { version: 4_x_redstone }` — FSR 4 Redstone Radiance Caching 0.9 technical preview, lowered through FidelityFX SDK 2.2. RDNA 4 + SM 6.4 + DX12-only.
- Vendor SDKs as they emerge — NVIDIA's RTX-GI lineage and Intel's analogous work will surface here when their SDKs ship. Until then the request resolves to `MissingProvider` for those vendors (∴ MEL-ENGINE-VIII).
- No engine fallback at 2026-05-28. A radiance cache is a substantial provider artifact; emulating one in-engine as a P1 fallback would falsely report a granted cap. The honest absence path is: cap reports `none`, the app's pipeline does not request radiance caching, and the GI stage falls back to whatever sampling strategy the app's render graph already encodes.

Cap surface for `radiance_cache`:

- `radiance_cache = none | sparse_volume | dense_volume` (the radiance-cache-specific cap; layered on the six-cap base surface).
- `hdr` per the base surface.

The per-frame descriptor for `radiance_cache` carries scene primary-hit samples, GI samples, the prior cache state handle, camera matrices, and the scene-bounds descriptor the cache is parameterized against.

## Sibling-module dependencies

- **docs/provider.md** — upstream. Providers are loaded through the provider registry; the request / release / cap-query API lives there. Reconstruction is one provider-kind consumer among several (latency, RT optimization, GPU profiling, crash diagnostics, etc.).
- **docs/frame-pacing.md** — upstream. `Frame_Info` supplies `jitter_offset_current` / `jitter_offset_prior` (which reconstruction consumes for reprojection), `predicted_next_present_ns` (where the platform grants it), `mode_state`, `thermal_tier`, `power_source`, and `latency_context`. Pacing owns the jitter sequence; reconstruction does not maintain its own per-pass jitter bookkeeping.
- **docs/frame-latency.md** — peer. Frame-generation contexts register `GeneratedPresentSubmit` latency markers; the marker taxonomy lives in `frame.latency`, the reconstruction frame packet carries the records.
- **docs/render-graph.md** — peer. Reconstruction passes compose inside graph passes; the render-graph's barrier discipline applies to reconstruction's inputs and outputs. A reconstruction pass is a graph node whose resource declarations the graph schedules against the rest of the frame.
- **docs/platform-display.md, docs/platform-surface.md, modules/sensor/spec.md** — upstream. The `hdr` cap surface negotiates against the platform's color-space grant; thermal and power signals reach reconstruction through `Frame_Info`.
- **docs/media-video.md** — peer. Video-decode output may enter reconstruction as color input (e.g. an in-engine cutscene upscaled to display resolution).
- **docs/io-asset.md** — upstream. Provider DLLs / `.so` / `.framework` binaries are asset artifacts the build system stages; provider load resolves through the asset path.
- **docs/xr.md** — consumer. `RuntimeXrReprojection` is the XR-safe default path; XR frame generation requires a `provider_certified` provider.

## P2 escape — raw vendor SDK access

Each provider's underlying SDK object — Streamline feature handle, FidelityFX SDK effect context, XeSS context, MetalFX scaler object, GSR context, NGX feature handle — is reachable through `gpu.native` interop headers. The app that needs to drive Reflex Frame Warp directly, or to call a Streamline feature Melody does not yet expose, can retrieve the native handle from the `Mel_Reconstruction_Context` (or `Mel_Ray_Regeneration_Context`, or `Mel_Radiance_Cache_Context`) and call the SDK directly. The engine documents the handle's lifetime against the context's lifetime, and the app is responsible for honoring the SDK's threading and submission contracts. This is P2; the engine does not validate calls made through this path. The NGX/NVAPI direct provider is the same shape: a sibling provider kind whose handle the app may use without Streamline mediation.
