# `xr` — extended-reality session, frame loop, and composition-layer surface

XR is a first-class target family, not a swapchain mode. The `xr` module owns the OpenXR / Compositor-Services boundary: it brings up an XR instance, negotiates a system and a session, locates per-frame view poses, wraps runtime-owned swapchain images as borrowed GPU textures, and submits composition layers to the platform compositor. The RHI does not present XR images to an OS window — there is no window in this path. `frame.pacing` does not drive XR — the XR runtime does, and pacing observes it. The render graph receives one pass DAG per XR frame with XR-specific access records; those records are owned by `render.graph`, produced here.

The module is split as a design space into `xr` (the unified, target-agnostic surface) and two lowerings, `xr.openxr` (`DesktopOpenXR` + `MetaQuestStandalone`) and `xr.visionos` (`AppleVisionPro` via Compositor Services). The split is private to the implementation; everything in this document is the surface the app sees, and that surface is identical across all three targets.

## Inherited principles

- **P1 — Emulate-to-equivalent.** Runtime reprojection / timewarp is the XR-safe comfort default on every target the engine ships. Where a runtime exposes vendor frame generation, it is admitted only behind `xr_safe = provider_certified` (see `docs/render-reconstruction.md`); the API shape does not differ between the certified and uncertified case, only the gate. The custom-pacing-source primitive that `frame.pacing` documents as its P2 example is the same primitive an XR swapchain installs on `xrWaitFrame`.
- **P2 — Full-control escape.** Native OpenXR handles (`XrInstance`, `XrSession`, `XrSwapchain`, action-set handles) and Compositor-Services handles (`cp_layer_renderer`, the per-frame `cp_frame` / `cp_drawable`) are exposed through a dedicated escape on the `Mel_Xr_*` objects, so an app may issue runtime calls Melody does not yet wrap without leaving the engine's resource lifetime model.
- **MEL-ENGINE-I — don't shy from the hard problem.** XR is a target family covering desktop OpenXR runtimes, Quest standalone, and visionOS Compositor Services. It is not modelled as a swapchain mode, not as a presentation extension, and not as a per-backend feature flag.
- **MEL-ENGINE-V — respect the user's product.** Provider selection (reprojection, frame interpolation, foveation discipline) is the app's choice, not the engine's; the engine surfaces caps and the runtime's report, and lets the app pick.
- **MEL-ENGINE-VI — respect every device.** Quest is a mobile thermal envelope; the Quest target couples to `platform.sensors.thermal` and budgets in milliseconds, not nominal FPS. Vision Pro pacing rides the layer-renderer, not a display link the engine fabricated.
- **MEL-ENGINE-VIII — fail with honor.** The engine does not fake head tracking, hand tracking, passthrough, or foveation when the runtime does not expose it. Missing fields are reported absent. Dropped frames and extended reprojection are reported to the app, not hidden.

## Instance bring-up and capability negotiation

XR is unique among Melody's target families in that the runtime is negotiated, not selected by the engine. The bring-up is a three-phase handshake the app threads through; the engine sequences each phase but never silently substitutes a value the runtime refused.

- **Phase 1 — instance.** The app declares the extension set it wants (`XR_KHR_*` core extensions, plus optional `XR_FB_*` / `XR_META_*` / `XR_EXT_*` / `XR_ANDROID_*` extensions for the target). The engine enumerates what the runtime exposes, intersects the requested set with the granted set, and reports the result. A required extension the runtime refuses fails instance creation loudly (MEL-ENGINE-VIII); an optional extension the runtime refuses produces an absent cap the app branches on.
- **Phase 2 — system.** `xrGetSystem` picks the headset; the runtime reports `XrSystemProperties` (vendor, tracker layout, supported view configurations, supported swapchain formats). The engine surfaces the system's recommended view configuration (`primary_stereo`, `primary_quad_varjo`, the Quest first-person observer profile where exposed) and the recommended image extent per view. The app may request a non-recommended view configuration; the runtime may refuse, and refusal is loud.
- **Phase 3 — graphics binding.** Required GPU device adapter / `VkPhysicalDevice` / `MTLDevice` is published by the runtime; the GPU RHI's device bring-up consumes that constraint. On desktop OpenXR with `XR_KHR_vulkan_enable2`, the runtime additionally publishes the required Vulkan instance / device extensions and feature set; the GPU module honours that list. On D3D12, the runtime publishes the required `LUID` and minimum feature level. On visionOS, the layer-renderer's `cp_layer_renderer_capabilities` publishes the texture layout (`layered`, `dedicated`, `shared`), pixel format set, and view count.

The intersection result is materialized as `Mel_Xr_Capabilities` on the instance, queryable for every cap mentioned in the platform sections below. Caps that depend on runtime state (refresh-rate set, available foveation tiers, active interaction profile) update over the session lifetime and are republished through the XR event stream.

## Targets and non-goals

Targets:

- **`DesktopOpenXR`** — PC VR through OpenXR runtimes: SteamVR, Meta Quest Link, Windows Mixed Reality / OpenXR-compatible runtimes, Varjo, Pimax, Vive, Monado, and similar conformant runtimes.
- **`MetaQuestStandalone`** — Horizon OS / Android on Quest, OpenXR runtime, Vulkan RHI backend.
- **`AppleVisionPro`** — visionOS, Compositor Services, Metal RHI backend.

Non-goals:

- **OpenVR is not the primary API.** It can be admitted as a P2 compatibility provider later; nothing in the `Mel_Xr_*` surface is shaped to OpenVR's session model.
- **XR frame generation outside a runtime-approved path is off by default.** Vendor frame generation participates only when the runtime / provider explicitly reports XR-safe support; the engine does not insert generated frames into an XR composition stream on its own.
- **The engine does not fake what the runtime does not grant.** No synthetic head pose, no fabricated hand skeleton, no software passthrough where the runtime exposes none, no shader-side foveation when the runtime owns foveation. Missing capabilities surface as absent caps (MEL-ENGINE-VIII).

## XR object model

Eight public objects span the surface; their identity model matches the slotmap-per-type model of the GPU RHI.

- **`Mel_Xr_Instance`** — owns the OpenXR `XrInstance` or the platform XR bridge (on visionOS, the Compositor-Services bridge object). One per process under normal usage; multiple are admissible for tooling.
- **`Mel_Xr_System`** — the selected headset / runtime / device profile. On OpenXR this is the `XrSystemId` plus the runtime's reported `XrSystemProperties`; on visionOS this is the system-reported layer-renderer configuration (view count, recommended drawable extent, supported foveation, supported MetalFX modes).
- **`Mel_Xr_Session`** — the running interaction session. Owns lifetime of the runtime-bound graphics binding (`XrGraphicsBinding*` on OpenXR; the `cp_layer_renderer` on visionOS), owns action-set state, and owns the XR-side frame loop.
- **`Mel_Xr_Space`** — reference spaces (`Stage`, `Local`, `View`, `LocalFloor`) and app-defined spaces (anchor spaces, controller-grip spaces, hand-joint spaces). Identity is per-session.
- **`Mel_Xr_View_Set`** — per-frame view poses, projection matrices, recommended image size, visibility mask, foveation profile, and environment blend mode. One `View_Set` per `Mel_Xr_Frame`. Every per-eye view carries its own jitter offset, projection, culling frustum, TAA history pointer, and foveation center; the `View_Set` is the surface where those per-view fields live. The visibility mask is the runtime-supplied hidden-area mesh (the cookie-cutter region the lens never shows) and the inner-area / line / outer-area meshes for HMDs that expose them — rendering inside the hidden area is wasted bandwidth, and the engine threads the mask into the graph's first-pass stencil prefill (a `view_local_resource`) so the cost is paid once per frame regardless of pass count. Environment blend mode is `opaque | additive | alpha_blend` and is forwarded to the composition layer record; an AR session on a passthrough device reports `alpha_blend` and the app must respect the alpha contract (premultiplied, with a declared background) or the composite is wrong.
- **`Mel_Xr_Image_Set`** — borrowed runtime swapchain images wrapped as GPU textures and views through the GPU RHI's borrowed-image-set swapchain path (the same primitive that wraps externally-owned drawables for media decode and platform-composition surfaces). Lifetime of the textures inside the set is the runtime's, not the engine's; the engine never assumes it may retain them beyond the runtime's release rules.
- **`Mel_Xr_Frame`** — frame state from the runtime: predicted display time, frame ID, environment blend mode for the frame, view count for the frame (which may change mid-session under `XR_EXT_view_configuration_views_change`), and the runtime ownership token that gates image release. Identity is per-tick; the frame object does not outlive its submit.
- **`Mel_Xr_Composition_Layer`** — projection, quad, cylinder, equirect, cube, passthrough, depth, and platform-specific layers (Quest passthrough geometry, visionOS system-composited passthrough). The layer's record carries the textures it references, the sub-image rectangles on those textures, the per-view sub-images for stereo projection, and the composition flags (`unpremultiplied_alpha`, `blend_texture_source_alpha`, `correct_chromatic_aberration` where the layer kind admits it). The layer record is what the session submits at frame end; multiple layers per frame are admitted, ordered back-to-front, with the runtime free to short-circuit composition (e.g. a fully opaque projection layer occluding everything beneath it).

## Object relationships

- **GPU device selection and XR system selection are coordinated.** On desktop OpenXR the runtime may dictate the graphics adapter through `xrGetD3D12GraphicsRequirementsKHR` / `xrGetVulkanGraphicsRequirements2KHR` — the engine respects the runtime's LUID / `VkPhysicalDevice` choice and constructs the `Mel_Gpu_Device` on that adapter, not on the host's default. On Quest the device is effectively fixed (the Adreno on the headset). On Vision Pro the device is fixed (the Apple Silicon GPU under visionOS); the layer-renderer is bound to it implicitly. The XR module never instantiates the GPU device behind the app's back — it reports the constraint and the app's bring-up code threads the device construction through it.
- **GPU borrowed-image-set swapchains are the bridge between XR images and the RHI.** A `Mel_Xr_Image_Set` is a borrowed-image-set swapchain in the RHI's lifetime model: the engine wraps `XrSwapchain` images / `cp_drawable` textures as GPU textures and views, owns the views' descriptors, and releases them back to the runtime under the runtime's ownership rules (acquire / wait / release on OpenXR; encode / present on Compositor Services).
- **`frame.pacing` source is the XR runtime frame loop, not the desktop display link.** An XR session installs a custom `Mel_Frame_Pacing_Source` fired on `xrWaitFrame` (OpenXR) or on the layer-renderer's frame timing (`cp_frame_timing_query`-style query, visionOS). This is the canonical P2 custom-pacing-source example that `docs/frame-pacing.md` references — XR pacing is not a special case in pacing, it is one instance of pacing's custom-source escape. `Frame_Info.predicted_next_present_ns` is populated from the runtime's predicted display time; the mirror window (where one exists, desktop only) runs an independent ordinary swapchain on its own pacing and never drives XR pacing.
- **`render.graph` receives one pass DAG per XR frame, with per-view resource ranges and XR access records.** Those records — `view_mask`, `view_local_resources`, `shared_view_resources`, `late_latched_uniforms`, `composition_layer_output`, `runtime_ownership` — are defined in `docs/render-graph.md`. The `xr` module produces them on the passes it records; it does not own them as a type.

## XR frame loop

Portable across all three targets, with platform-specific lowerings beneath:

1. **Runtime wait begins the frame and returns predicted display time.** OpenXR: `xrWaitFrame` followed by `xrBeginFrame`. visionOS: the layer-renderer's frame-begin hand-off plus the `cp_frame`'s predicted-display-time query. The predicted display time populates `Frame_Info.predicted_next_present_ns` for the XR-driven pacing source.
2. **Runtime locates views for the requested reference space.** `xrLocateViews` against the active `Mel_Xr_Space` (or the layer-renderer's per-`cp_drawable` view transforms on visionOS). The result is materialized in the frame's `Mel_Xr_View_Set` — per-eye pose, projection, recommended extent, visibility mask, foveation profile, environment blend mode.
3. **RHI wraps/acquires runtime swapchain images as borrowed GPU texture views.** `xrAcquireSwapchainImage` + `xrWaitSwapchainImage` on OpenXR, or the `cp_drawable` query on visionOS. The acquired images become a `Mel_Xr_Image_Set`; the borrowed-image-set swapchain path tags them with `runtime_ownership` access state so the graph compiler emits the correct acquire / release transitions.
4. **Render graph records multiview or per-eye passes.** The graph's XR access records carry `view_mask` and the per-view resource ranges; multiview is preferred when granted, per-eye passes are the fallback. Per-eye TAA history, per-eye motion vectors, and per-eye foveation maps are `view_local_resources`; the depth target shared across eyes (in instanced-stereo configurations) is `shared_view_resources`.
5. **Queue submit returns the normal completion future.** XR submit is not a special completion path — the GPU RHI's frame retirement future gates everything downstream of submit, including the moment the engine may release the `Mel_Xr_Image_Set` back to the runtime.
6. **Runtime composition layers are submitted with color, depth, optional motion / foveation metadata.** `xrEndFrame` with the constructed `XrCompositionLayer*` list on OpenXR; the layer-renderer's frame-end hand-off on visionOS. Depth submission is first-class — `XR_KHR_composition_layer_depth` or the platform equivalent — so the runtime's reprojection has correct depth.
7. **Completion futures gate image reuse; runtime ownership rules gate image release.** The engine does not reuse a borrowed image until both the GPU submission's completion future fires *and* the runtime's release rule admits it. Failed frames return runtime image ownership before propagating the error — the runtime is never left holding an unreleased image (MEL-ENGINE-VIII).

Frame-loop rules:

- **`Frame_Info.predicted_next_present_ns` is the XR predicted display time.** Not the desktop display link. Not a mirror-window present timestamp.
- **Every per-eye view has its own jitter, projection, culling frustum, TAA history, foveation center.** The `Mel_Xr_View_Set` is shaped so the graph and the reconstruction provider see those per-view fields without per-pass bookkeeping in app code.
- **Multiview rendering is preferred when backend/runtime supports it; per-eye is the fallback.** Capability is reported through GPU caps (multiview support on the backend) intersected with XR caps (multiview admitted by the runtime's view-configuration). The XR module reports the intersection; the graph compiles to whichever the cap admits.
- **Depth submission is first-class** through OpenXR depth layers or platform equivalents. The composition-layer output access record carries depth alongside color when the layer kind admits it.
- **The app supplies stable world scale, near / far planes, and tracking-origin transforms.** Reprojection is only correct against the depth values the app actually rendered; mis-declared near / far breaks runtime reprojection silently if the engine does not insist. The XR session debug-asserts on missing world-scale / tracking-origin declarations.

## Session state machine and lifecycle

The XR session is not a steady-state object — it transitions through runtime-driven states (`Idle → Ready → Synchronized → Visible → Focused → Stopping → LossPending → Exiting` on OpenXR; an analogous `Initialized → Running → Backgrounded → Invalidated` chain on Compositor Services). The engine surfaces the state as an event stream the app subscribes to; state transitions arrive on the pacing tick alongside `Frame_Info`. Three transitions are load-bearing for app correctness:

- **`Focused`** — only `Focused` sessions deliver action input. An app that polls actions in `Visible` (but not `Focused`) receives the last-known state, not new input; the engine reports the distinction rather than silently delivering stale poses as fresh (MEL-ENGINE-VIII).
- **`Stopping`** — the runtime is taking the session away (user removed the headset, system overlay took focus, runtime is shutting down). The app must release per-frame resources but the `Mel_Xr_Session` is still alive; a subsequent `Ready` transition resumes the same session without re-instancing.
- **`LossPending` / `Exiting`** — the session is being destroyed. The engine drains the GPU's outstanding submissions, releases all borrowed image sets back to the runtime, and tears the session down in the order the runtime mandates.

Action sets are bound to the session, not to the instance. Action-set creation describes the action layout (`pose`, `boolean`, `float`, `vector2f`, `haptic_output`); interaction-profile suggestion describes how those actions map onto controller / hand / eye-gaze profiles (`khr/simple_controller`, `oculus/touch_controller`, `valve/index_controller`, `ext/hand_interaction`, `ext/eye_gaze_interaction`, and the Meta multimodal profiles where granted). The runtime, not the app, chooses the active profile per device; the app declares mappings for every profile it wishes to support, and the engine reports which mapping the runtime activated. Haptic output rides the same action-set surface — a `haptic_output` action is triggered with amplitude, frequency, and duration, and the runtime routes it to the device the active profile binds.

## Desktop VR through OpenXR

Required features:

- OpenXR instance and session creation.
- Graphics binding for D3D12 and / or Vulkan, depending on runtime support.
- Stereo projection layers.
- Runtime-owned swapchain images.
- Predicted display time from the OpenXR frame loop.
- Action sets for controllers, hands, and haptics.

Important extensions modelled by the desktop path:

- `XR_KHR_D3D12_enable`
- `XR_KHR_vulkan_enable2`
- `XR_KHR_vulkan_swapchain_format_list`
- `XR_KHR_composition_layer_depth`
- `XR_EXT_hand_tracking`
- `XR_EXT_eye_gaze_interaction`
- Foveated / quad-view extensions where the runtime exposes them.

Desktop provider policy:

- **D3D12 is the preferred backend on Windows** when the chosen runtime's D3D12 binding is strong (SteamVR's D3D12 binding has historically been thinner than its D3D11; the engine reports the runtime's binding strength through a cap and the app may override).
- **Vulkan is the preferred cross-platform backend and the Linux / OpenXR (Monado, SteamVR-on-Linux) path.**
- **The runtime chooses final scanout timing.** `frame.pacing` observes and feeds the runtime, not the other way around. The pacing source's `predicted_next_present_ns` comes from the runtime; the engine never substitutes a synthesized prediction.
- **Runtime reprojection / timewarp is the default comfort path.** Vendor frame generation is admitted only if the runtime / provider explicitly reports XR-safe support — the gate is `xr_safe = provider_certified` from `docs/render-reconstruction.md`. An uncertified frame-generation provider is unconditionally refused on the XR target family regardless of how it is configured for the same provider on a flat target.
- **Desktop mirror-window rendering is a separate ordinary swapchain and never drives XR pacing.** The mirror window has its own `Mel_Frame_Pacing_Source` bound to the desktop display link; the XR session's pacing source is independent.

## Meta Quest standalone

- Platform: Horizon OS / Android.
- XR API: OpenXR.
- GPU backend: **Vulkan only** for the RHI target. GLES is not a peer backend for this spec.
- Presentation: OpenXR projection layers, optional passthrough and overlay layers.
- Pacing: OpenXR frame loop with Quest refresh targets exposed to `frame.pacing`.

Quest-specific caps:

- `quest_refresh_rates` — runtime-reported subset of the 72 / 90 / 120 Hz family (Quest 2 / 3 / Pro vary).
- `quest_foveation` — `none | fixed | dynamic | eye_tracked`.
- `quest_space_warp` — deprecated; superseded by `XR_EXT_frame_synthesis`. The cap remains for runtimes that still expose only the legacy path.
- `quest_passthrough` — `none | compositor_passthrough | camera2_rgb`.
- `quest_scene` — `none | anchors | scene_mesh | semantic_scene`.
- `quest_hand_tracking` — `none | hands | simultaneous_hands_and_controllers`.
- `quest_thermal` — `advisory | enforced_budget`.

Extension families modelled by the Quest path:

- `XR_FB_foveation`, `XR_FB_foveation_configuration`, `XR_FB_foveation_vulkan`, `XR_FB_swapchain_update_state`.
- `XR_META_foveation_eye_tracked` where hardware / runtime grants eye-tracked foveation.
- `XR_FB_display_refresh_rate`.
- `XR_EXT_frame_synthesis` — preferred over the deprecated `XR_FB_space_warp`. The engine carries both lowerings, prefers `frame_synthesis` where granted, falls back to `FB_space_warp` on runtimes that still expose only the legacy path.
- `XR_FB_passthrough` and companion geometry / colour extensions where granted.
- `XR_EXT_hand_tracking` plus Meta multimodal extensions where granted.
- `XR_EXT_view_configuration_views_change` — runtime-driven view-configuration changes (resolution / view-count) within a session; the engine threads the change through `Mel_Xr_View_Set` mid-session without tearing down the session.
- `XR_EXT_interaction_profile_battery_state_display` — controller battery state surfaced through the action system.
- `XR_ANDROID_spatial_anchor_space` — Quest spatial anchors via the Android-bridged path.

**Frame budgets in milliseconds, not just FPS.** The engine reports the budget in ms because the budget is what the app must hit:

- 72 Hz ≈ 13.9 ms.
- 90 Hz ≈ 11.1 ms.
- 120 Hz ≈ 8.3 ms.

These budgets feed `frame.pacing` as `Adaptive(target_frame_ms)` targets; the app's quality governor consumes `headroom_ns` against them. A nominal "120 Hz" budget published as 8.3 ms is honest about what the app actually has to spend (MEL-ENGINE-VI).

**Foveation / dynamic-resolution coordination.** Dynamic foveation and dynamic resolution can fight each other if both are enabled by independent governors. On Quest, platform dynamic foveation wins over generic dynamic resolution when both are requested — the engine surfaces both caps, and when an app turns on platform foveation the generic dynamic-resolution path is suspended for the session with a one-time log line (MEL-ENGINE-VIII over silent override).

**Passthrough / camera privacy.** Passthrough camera frames are device-user data. Where the runtime grants `camera2_rgb`-style access, importing those frames into GPU textures carries a `protected` / `privacy_labeled` flag through the GPU resource flags and through validation; shaders that sample protected textures must declare the capability, and writing a protected sample into a non-protected target is a debug-mode assertion (matches the engine-wide privacy-propagation discipline in `docs/media-video.md`).

**Quest thermal pressure** is a `platform.sensors.thermal` event the app receives at the pacing tick; the engine does not silently downgrade quality (MEL-ENGINE-V). The app's quality governor reads `Frame_Info.thermal_tier` and responds. The Quest path additionally publishes a `quest_thermal_advice` field on the XR event stream when the runtime hints a recommended action (lower refresh rate, lower foveation tier, suspend a workload); the engine forwards the advice rather than acting on it. An app that ignores `critical` thermal pressure will eventually be killed by the OS — that is the runtime's prerogative, and the engine does not paper over it.

**Quest rendering policy.** Passes are tile-friendly and bandwidth-light: prefer multiview, prefer memoryless / transient attachments for depth and MSAA (the Adreno's on-tile framebuffer is where the bandwidth is saved), prefer `DontCare` on attachments whose contents will not be read again, coordinate dynamic foveation, render scale, and reconstruction provider selection through one quality governor so they do not contradict each other. These are the tile-aware compilation contracts the render graph already honours; the XR module surfaces the constraint to the graph compiler through the tiler profile (`Mel_Gpu_Tiler_Profile`) the GPU device reports on the Adreno.

## Apple Vision Pro

- Platform: visionOS.
- XR API: Compositor Services (`cp_layer_renderer`).
- GPU backend: Metal.
- Presentation: Compositor Services `LayerRenderer` drawables wrapped as borrowed GPU texture views; layer-renderer pacing, not a window display link.

Vision Pro caps:

- `visionos_compositor = compositor_services`.
- `visionos_layered_drawables = stereo | capture_extended`.
- `visionos_foveation = runtime_managed | app_profiled` — as exposed by Compositor Services.
- `visionos_passthrough = system_composited` — passthrough is owned by the system compositor; the app never touches passthrough texels.
- `visionos_input = hands | gaze_indirect | controller_optional | arkit_anchors` — depending on permissions and APIs used. Gaze is *indirect*: the app receives a tap intent against a gazed UI element, not raw gaze rays, unless an explicit privacy-gated extended-permission flow has been granted.
- `visionos_metalfx = none | spatial | temporal | frame_interpolator | temporal_denoised`.

Rules:

- **Vision Pro is not routed through OpenXR in this spec.** The session is Compositor Services directly.
- **A fully immersive Metal app draws scene content for both eyes;** the system compositor owns final display timing and passthrough environment composition. The `LayerRenderer` drawables are the borrowed image set the engine renders into.
- **`LayerRenderer` drawables are borrowed image sets.** The engine never assumes it may retain them beyond the compositor's lifetime rules — release follows the layer-renderer's frame-end hand-off.
- **Rendering must honour compositor-provided view transforms, projection, texture layout, pixel format, timing.** The engine surfaces those through `Mel_Xr_View_Set`; an app that overrides projection or extent is fighting the compositor and the result is undefined visually.
- **RealityKit / SwiftUI layers can coexist with the RHI path,** but the RHI path is Compositor Services plus Metal — RealityKit is not in this spec's surface (it is a peer SDK the host app may use independently).
- **MetalFX as a reconstruction provider** is integrated through `docs/render-reconstruction.md`. Spatial and temporal MetalFX are unconditionally admitted on the immersive layer; **frame interpolation is enabled only if the compositor / provider reports it as XR-safe** for the immersive layer (`xr_safe = provider_certified`). The engine refuses to insert MetalFX-interpolated frames into the Compositor-Services stream without the certified flag.
- **Eye / gaze and camera-derived data are privacy-gated.** The RHI surfaces capability and permission status; it does not promise raw sensor access. The `visionos_input = gaze_indirect` cap is the engine being honest that gaze arrives as intent, not as a ray.
- **Layered drawables are the texture layout.** `visionos_layered_drawables = stereo` lays both eyes out as array slices of one texture (the multiview-equivalent on Metal); `capture_extended` exposes the additional drawable surface used for screen capture / mirroring of an immersive session. The engine reports the texture layout the layer-renderer chose, and the graph compiles per-eye access against array-slice indices rather than per-eye textures — this is the multiview path on the Metal backend.
- **Foveation discipline.** `runtime_managed` foveation is the compositor's variable-rasterization-rate map applied to the drawable extent; the app renders into the full extent and the compositor reads with the variable-rate pattern. `app_profiled` foveation lets the app provide a per-tile rate map; the engine routes the map as a `view_local_resource` and threads it into pipeline-cache keys where the lowering depends on it. The app does not choose foveation tier per pass — the foveation profile is per `Mel_Xr_View_Set` and stable across the frame's passes.

## Composition layer kinds

The runtime composites layers, not the app's render pass. Choosing the right layer kind for the content matters more than rendering quality: a UI panel composited as a `quad` layer at the runtime's native resolution is sharper than the same UI rendered into the projection layer at the projection layer's per-eye resolution, because the runtime samples the quad once per output pixel through a non-perspective-distorted path. The layer kinds the engine wraps:

- **`projection`** — the immersive stereo (or quad-view) layer the scene renders into. Carries per-view sub-images, projection matrices that match the rendered frustum, and (when `XR_KHR_composition_layer_depth` is granted, or its platform equivalent) a depth sub-image per view. Reprojection requires the depth sub-image; an immersive app that submits a projection layer without depth on a runtime that admits depth-layers is logged as a comfort regression.
- **`quad`** — a rectangle in world space (or in view space, head-locked, when the quad is a HUD). The runtime samples it with the right filter for the eye-to-quad geometry. UI, menus, subtitles, and floating panels belong here, not in the projection layer.
- **`cylinder`** — a rectangular texture mapped to a section of a cylinder. Curved UIs.
- **`equirect`** / **`equirect2`** — equirectangular skybox / 360 video layers. The runtime samples through the longitude / latitude parameterization the layer declares; the projection layer never has to render a skybox into the eye buffer.
- **`cube`** — a cubemap layer; same idea as equirect but with a cube parameterization.
- **`passthrough`** — the runtime-composited passthrough layer (Quest `XR_FB_passthrough`, visionOS system-composited passthrough). The app does not render passthrough texels; the layer record carries the geometry (full-screen, projected mesh, layer-style mask) and the runtime composites the camera feed against it.
- **`depth`** — submitted alongside `projection`; not a standalone visible layer. The runtime consumes the depth sub-image for reprojection and (where supported) for environment occlusion against passthrough.
- **Platform-specific** — Quest passthrough colour-LUT, geometry mesh, and style extensions; visionOS layer-renderer's progressive-immersion handle. These ride the same `Mel_Xr_Composition_Layer` shape, with platform-specific payload variants the layer's tag discriminates.

The engine validates layer ordering (back-to-front) and the alpha contract per layer kind (the `unpremultiplied_alpha` flag versus the textures' actual encoding). A layer whose flags contradict its texture's premultiplication state is a debug-mode assertion.

## XR render graph requirements

The XR-specific render-graph access records — `view_mask`, `view_local_resources`, `shared_view_resources`, `late_latched_uniforms`, `composition_layer_output`, `runtime_ownership` — are defined in **`docs/render-graph.md`**. The `xr` module produces those access records on the graph passes it records each XR frame; it does not own them as a type, and this document does not duplicate their semantics. See `render.graph` for the per-eye TAA history aliasing rule, the depth-for-reprojection requirement, the per-view motion-vector convention, the foveation-map pipeline-cache-key rule, and the mirror-output-is-dependent-copy rule.

## XR comfort and safety contract

The engine cannot guarantee comfort; it makes failures loud (MEL-ENGINE-VIII).

- **Frame-budget overruns are reported, not hidden.** When the runtime drops frames or reprojection extends past one display cycle, the event arrives at the app through the pacing source's mode-state channel and through an XR-specific event stream. The engine does not silently lower resolution to mask a missed budget — that is the app's quality-governor decision (MEL-ENGINE-V).
- **Generated frames cannot consume game-simulation state that does not exist.** The game callback runs once per real simulation frame; XR frame generation (when admitted at all) interpolates *display* frames, never re-invokes simulation. Anything that depends on the simulation tick — input application, networking, audio mixdown cadence — runs on the simulation cadence, not the display cadence.
- **XR frame generation is off by default.** The runtime-reprojection / timewarp path is the comfort baseline. Vendor frame generation participates only when the provider reports `xr_safe = provider_certified` (`docs/render-reconstruction.md`).
- **Passthrough / camera / eye-gaze data is privacy-protected at the API boundary.** The engine never exposes raw frames to general shaders without an explicit opt-in capability; protected textures propagate the flag through every downstream pass (`docs/media-video.md` for the broader propagation discipline).
- **The frame loop must be runtime-paced.** Blocking the XR frame loop on unrelated file IO, shader compile, or asset streaming is a debug-mode assertion; in release it logs at the site (`mel_log_error("xr", ...)`).
- **World scale and tracking origin are declared by the app.** A session that has not declared them debug-asserts on first frame submit; missing declarations are not silently defaulted, because the default is wrong for almost every game-world unit convention.
- **Runtime image ownership must be returned even when rendering fails.** Failed frames submit a safe fallback layer if the runtime allows it; the runtime is never left holding an unreleased image.
- **Quality governors must prefer resolution / foveation / LOD reductions over missing frame deadlines.** The engine reports the budget and the headroom; the app's governor is the authority that picks the response. The governor receives `prior_gpu_ns`, `prior_cpu_ns`, `headroom_ns`, `thermal_tier`, the runtime's reprojection-extension event count, and the active refresh rate — every input it needs to decide whether to lower render scale, raise foveation tier, drop a shadow cascade, or request a lower refresh-rate target (`XR_FB_display_refresh_rate` on Quest, layer-renderer reconfiguration on visionOS).
- **The engine does not block the XR frame loop on shader compilation.** Pipeline cache warming is a session bring-up cost the app pays before entering `Focused`; a pipeline first encountered inside the frame loop is a debug-mode assertion (matches the render graph's `recordFn`-side debug assertions in `docs/render-graph.md`). The engine's PSO cache cross-frame priming and the GPU's pipeline-cache persistence are the mechanism — the XR module enforces the rule at the session boundary.
- **Asset streaming runs off the XR critical path.** The asset module's IO queues honour the XR pacing source as a priority signal; an asset whose decode would extend the XR frame is deferred to the next tick. The engine reports streaming pressure to the app the same way it reports thermal pressure — through the pacing tick, never by silently dropping work that was already in flight.

## Sibling-module dependencies

- **`docs/provider.md`** — upstream. `XrRuntimeBridge` provider kind selects among OpenXR runtimes / Compositor Services / future P2 OpenVR.
- **`docs/platform-sensors.md`** — consumer. Quest thermal pressure flows in through `platform.sensors.thermal`; the XR session does not invent its own thermal channel.
- **`docs/platform-surface.md`** — XR sessions do not bind to a normal window surface. The surface module flags XR sessions as an explicit non-surface-bound target; the XR module documents that the binding is to the runtime, not to a window.
- **`docs/platform-display.md`** — XR view configurations are reported here, but display-mode selection is handled by the XR runtime, not by `display`'s desktop / mobile mode-set path.
- **`docs/frame-pacing.md`** — consumer. The XR runtime-driven custom `Mel_Frame_Pacing_Source` is the canonical P2 custom-pacing-source example pacing's spec points to.
- **`docs/frame-latency.md`** — consumer. The XR session is a latency target; the `SimStart`, `RenderSubmitStart`, `PresentStart` markers carry through to the runtime's reprojection where the runtime accepts latency hints.
- **`docs/render-graph.md`** — peer. Hosts the XR access records; receives one pass DAG per XR frame from this module.
- **`docs/render-reconstruction.md`** — peer. `RuntimeXrReprojection` provider lives there; `xr_safe = provider_certified` gating is enforced there.
- **`docs/media-video.md`** — consumer. Passthrough cameras (Quest `camera2_rgb`, Vision Pro extended-permission camera) propagate privacy-protected flags through the video / media import path.
- **`docs/io-asset.md`** — XR overlays and panel content load through the standard asset path; the XR module does not redefine asset loading.

## P2 escape — native runtime handles

Every `Mel_Xr_*` object exposes a typed accessor to its underlying runtime handle:

- `Mel_Xr_Instance` → `XrInstance` on OpenXR targets, the Compositor-Services bridge object on visionOS.
- `Mel_Xr_Session` → `XrSession` / the bound `cp_layer_renderer`.
- `Mel_Xr_Space` → `XrSpace`.
- `Mel_Xr_Image_Set` → the underlying `XrSwapchain` (OpenXR) or the per-frame `cp_drawable` (visionOS).
- `Mel_Xr_Frame` → the runtime frame token (`XrFrameState` snapshot, or the `cp_frame`).
- `Mel_Xr_Composition_Layer` → the constructed `XrCompositionLayer*` chain on OpenXR; on visionOS, the layer-renderer's per-layer record.

Action sets, hand-tracking handles, eye-gaze handles, and any runtime extension this module does not yet wrap are reachable through the session's native accessor. Apps that need a runtime call Melody does not yet model do not have to leave the engine — they reach through the accessor, call the runtime directly, and rely on Melody's resource lifetime model for everything else (MEL-ENGINE-IV: Melody constrains naming, not capability). The accessor returns the handle; what the app does with it is the app's discipline.
