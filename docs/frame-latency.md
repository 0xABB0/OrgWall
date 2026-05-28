# Melody Frame Latency — `frame.latency`

End-to-end input-to-photon latency control. A marker taxonomy spanning input sampling through simulation, render submit, and present; a pre-input sleep primitive; and a per-frame report — all delivered through the same vendor-agnostic surface whether the host is NVIDIA Reflex, AMD Anti-Lag, Intel XeLL, a platform-native pacing waitable, or absent.

This module is bound by the Ten Commandments of the Engine. Where a decision turns on one, the commandment is cited by tag (`MEL-ENGINE-N`).

---

## 1. Module identity

Parent module: `frame` (peer of `frame.pacing`). Header at `modules/frame/include/frame.latency/latency.h`.

The marker taxonomy spans input sampling → simulation → render submit → present; that is a frame-level orchestration concern, not a GPU recording concern. The GPU module provides exactly one lowering point — the `cmd_latency_mark(point)` recording command of gpu-rhi.md §7.5 — and `frame.latency` is the module that defines what the points *are*, what the providers *are*, and what the pre-input sleep contract *is*. The GPU command lowers through this module's provider vtable on the GPU timeline; it does not redefine the marker semantics. (MEL-ENGINE-IX: each part owns its own concern.)

`frame.latency` does not own the swapchain, the simulation cadence, or the XR runtime timing — those belong to `gpu`, the app, and `xr` respectively. A latency provider may *suggest* pacing through its report; it never silently rewrites the swapchain image count, present mode, simulation cadence, or XR runtime predicted-display-time (MEL-ENGINE-V).

---

## 2. Inherited principles

- **P1 — emulate-to-equivalent absent faking.** When no provider is granted, `latency_provider_request` returns `MissingProvider`; markers are not silently dropped into the void, and the per-frame report carries `provider = none` rather than a fabricated breakdown (MEL-ENGINE-VIII).
- **P2 — full-control escape hatches.** Every convenience routes through `provider.md`'s registry; the consumer that wants raw vendor entry points reaches them through the GPU module's native interop headers (`gpu/<backend>/nvapi_interop.h`, `gpu/<backend>/ags_interop.h`, `gpu/<backend>/streamline_interop.h`). The simple path *is* the powerful path further along (MEL-ENGINE-II).
- **No autoload.** No provider is loaded, no DLL is mapped, no callback thread is parked until the app calls `latency_provider_request`. The reactor pump is the app's, never `frame.latency`'s. (MEL-ENGINE-III: no cycle spent that the user did not ask for; no thread spawned in shadow.)
- **Providers suggest, never impose.** A provider's report may carry recommended pre-input deadlines, suggested simulation budgets, and target frame-time hints. The app's content model decides whether to honor them. The engine refuses to overwrite the user's pacing or swapchain configuration on a provider's advice (MEL-ENGINE-V).
- **Honest provider-absent.** Absence is reported through the per-frame report's `provider` field and through `caps.presentation.latency_marker = none`; no fake markers are synthesized from frame-time variance or queue-depth heuristics (MEL-ENGINE-VIII).

---

## 3. Marker-point taxonomy

The ten points, in canonical per-frame order:

- `FrameStart` — the pacing tick fired; the frame's identity (`frame_id`) is now committed.
- `InputSample` — the app has read input. The pre-input sleep deadline (§4) is satisfied *before* this point, not after.
- `SimStart` — simulation began.
- `SimEnd` — simulation finished; render-side state is final.
- `RenderSubmitStart` — first command list of the frame entered queue submit.
- `RenderSubmitEnd` — last command list of the frame entered queue submit.
- `PresentSubmit` — the real present was enqueued on the present queue.
- `PresentDisplayed` — the real present's pixels reached the scanout engine (where the platform reports this honestly; absent otherwise).
- `GeneratedPresentSubmit` — a frame-generation provider enqueued a generated present (frame-gen contexts only; emitted per generated output, one per generated frame, not aggregated).
- `GeneratedPresentDisplayed` — a generated present's pixels reached scanout.

The ten points are stable identity. New vendor markers in future SDK revisions lower into the existing ten or are exposed through the provider's native interop header (§9); the marker enum does not grow per vendor (MEL-ENGINE-IV: constrain conventions, never capabilities).

`PresentDisplayed` and `GeneratedPresentDisplayed` are gated by `caps.presentation.present_timing_feedback` (gpu-rhi.md §7.4). Absent the timing cap, the per-frame report omits the displayed-time fields rather than guessing.

---

## 4. Public API

Given a `Mel_Frame_Latency_Provider` handle:

    Mel_Status latency_provider_request(
        Mel_Gpu_Device device,
        Mel_Latency_Target target,
        Mel_Latency_Provider_Kind preference[N],
        Mel_Frame_Latency_Provider* out_provider);

`target` is either a `Mel_Gpu_Swapchain` or an `Mel_Xr_Session`; the provider binds to one or the other ∵ the present timeline differs (a swapchain presents to scanout, an XR session presents through `xrEndFrame`). `preference[]` is the priority order — e.g. `[Reflex, Platform]` — resolved through `provider.md`'s registry against the consumer's kind catalog. The registry's `SubstitutedVariant` warning bitset reports when a later preference entry was granted (e.g. Reflex absent, platform-native granted).

    Mel_Status latency_set_mode(
        Mel_Frame_Latency_Provider provider,
        Mel_Latency_Mode mode);

Mode is provider-specific. Reflex publishes `Off | On | OnBoost`; Anti-Lag publishes `Off | On`; XeLL publishes `Off | On`; platform-native publishes `Off | LatencyWaitable(n)` where `n` is the DXGI frame-latency waitable depth. The mode enum is a tagged union; misuse (a Reflex mode passed to an Anti-Lag provider) is a loud `InvalidMode` per-action status, asserted in debug (MEL-ENGINE-VIII).

    Mel_Status latency_mark(
        Mel_Frame_Latency_Provider provider,
        Mel_Frame_Id frame_id,
        Mel_Latency_Marker_Point point);

The non-GPU peer of `cmd_latency_mark`. The GPU recording command lowers through this entry on the GPU timeline; the app calls `latency_mark` directly for CPU-side points (`InputSample`, `SimStart`, `SimEnd`, `FrameStart`). `frame_id` is the pacing tick's frame identity (`Frame_Info.frame_id` of gpu-rhi.md §7.5), not the swapchain image index.

    Mel_Status latency_sleep(
        Mel_Frame_Latency_Provider provider,
        Mel_Frame_Id frame_id,
        Mel_Time_Point before_input_sample_deadline);

Blocks the reactor until the vendor-recommended pre-input deadline, no earlier than `before_input_sample_deadline`. The sleep point is **before input sampling, not after simulation has already started** — moving the sleep after `InputSample` defeats the entire latency-reduction surface ∵ the vendor cannot align input freshness with predicted scanout once the input is already in the simulation. This rule is contractual; violation is a debug-mode assertion and a `latency.sleep_after_input` warning in release (MEL-ENGINE-VIII).

    Mel_Latency_Report latency_get_report(
        Mel_Frame_Latency_Provider provider,
        Mel_Frame_Id frame_id);

Per-frame breakdown: input-to-sim delta, sim-to-submit delta, submit-to-present delta, present-to-scanout delta (where granted), generated-present deltas (where frame-gen is active), provider's own suggested next-frame deadline, and provider identity. Absent provider returns a report with `provider = none` and the deltas the engine itself can measure (CPU-side points only).

---

## 5. Provider lowerings

The kind catalog entry in `provider.md` §2.3 — `LatencyControl` — names the four canonical lowerings. Each is reached through the registry; none is implicit.

- **NVIDIA Reflex.** D3D12 lowering through Streamline (`sl::Reflex`) or NVAPI direct (`NvAPI_D3D_SetLatencyMarker`, `NvAPI_D3D_Sleep`). Vulkan lowering through `VK_NV_low_latency2` (`vkSetLatencySleepModeNV`, `vkLatencySleepNV`, `vkSetLatencyMarkerNV`, `vkGetLatencyTimingsNV`). The provider reports `mode ∈ { Off, On, OnBoost }`; `OnBoost` is honored only when `caps.power.power_source = ac` ∵ the boost mode trades watts for latency, and respecting battery is non-negotiable (MEL-ENGINE-VI).
- **AMD Anti-Lag.** D3D12 lowering through AGS (`agsDriverExtensionsDX12_PushMarker` with the Anti-Lag marker subset) or the FidelityFX SDK's Anti-Lag 2 integration. Vulkan lowering through `VK_AMD_anti_lag` (`vkAntiLagUpdateAMD`). The provider reports `mode ∈ { Off, On }`; Anti-Lag 2's pre-input sleep is the canonical sleep entry on this provider.
- **Intel XeLL.** Xe Low Latency, reached through Intel's Game Optimizer SDK and the XeSS Frame Generation companion path. Lowering is currently D3D12-only; the provider reports `MissingProvider` on Vulkan and on non-Intel discrete GPUs (the integrated Xe lowering is gated by driver presence, not architecture family alone).
- **Platform-native.** DXGI frame-latency waitable object (`IDXGISwapChain2::GetFrameLatencyWaitableObject` plus `SetMaximumFrameLatency`) on Windows where no vendor provider is granted; Metal display timing (`CAMetalDrawable.presentedTime` + `MTLCommandBuffer.GPUStartTime` deltas, with the next-drawable wait as the sleep primitive) on Apple platforms; OpenXR frame pacing (`xrWaitFrame` predicted-display-time) on XR sessions; `requestAnimationFrame` on Web. The platform-native provider always reports honest absence for `GeneratedPresent*` ∵ no platform-native path participates in frame generation (MEL-ENGINE-VIII).

Provider entry points are contract calls in `provider.md`'s sense (§3): the registry resolves the function pointers once at `request` time, stores them in the vtable, and `latency_mark` / `latency_sleep` call through the vtable directly. No per-call registry lookup, no per-call status branch on the hot path (MEL-ENGINE-III).

---

## 6. Cap-tier integration

`frame.latency` reads — does not own — `caps.presentation.latency_marker` from the GPU module (gpu-rhi.md §3.4):

    caps.presentation.latency_marker ∈ { none, reflex, anti_lag, xess_fg, platform_native }

The cap reports the available surface; `latency_provider_request` consults it through the registry's enumeration path (`provider_enumerate` of `provider.md` §3). A `preference[]` that names only kinds the cap reports as `none` resolves to `MissingProvider` without calling any vendor init. The cap is per-device, not per-swapchain ∵ provider availability is a function of the GPU adapter (NVIDIA presence, AMD presence, Intel presence) rather than the surface; a multi-swapchain app on a single device sees one cap and may bind one provider per target through repeated `request` calls.

`caps.presentation.present_timing_feedback` gates `PresentDisplayed` and `GeneratedPresentDisplayed` ∵ those points require platform-published scanout timestamps (gpu-rhi.md §7.4). Absent timing feedback, the per-frame report omits the displayed-time fields rather than fabricating them.

---

## 7. Rules

- **Sleep before input.** `latency_sleep` is called before `InputSample`, never after `SimStart`. The provider's pre-input deadline aligns input freshness with predicted scanout; moving the sleep later forfeits the alignment. Debug asserts; release warns once per frame id (MEL-ENGINE-VIII).
- **Frame generation registers both presents.** A frame-generation provider (`render-reconstruction` kinds `FrameGeneration`, `xess_fg`, FSR FG, DLSS FG) emits `GeneratedPresentSubmit` and `GeneratedPresentDisplayed` per generated output, alongside the real `PresentSubmit` / `PresentDisplayed` for the real frame. No hidden generated presents — gpu-rhi.md §9.3 establishes this rule, and `frame.latency` enforces it through the marker contract.
- **Multi-adapter is opt-in.** Device groups and multi-adapter scenarios must be opt-in per provider through the kind catalog's request descriptor; a provider that cannot honor multi-adapter reports `MissingProvider` for the multi-adapter request and the consumer falls back to per-device providers or accepts absence. The registry never fakes multi-adapter support by joining single-adapter providers behind the scenes (MEL-ENGINE-VIII).
- **No silent timing changes.** A latency provider's report may carry suggested pre-input deadlines, target frame-times, and recommended swapchain-image counts. The provider **cannot** rewrite `Mel_Gpu_Swapchain_Desc.image_count`, switch the present mode, alter `frame.pacing`'s mode (`Continuous | OnDemand | Capped | Adaptive`), or change `xrFrameEndInfo.displayTime` from beneath the app. The app's content model decides whether to act on the suggestion (MEL-ENGINE-V, MEL-ENGINE-VIII).

---

## 8. Sibling-module dependencies

- **`provider`** (upstream) — `frame.latency` is a consumer of `provider.md`'s registry. The `LatencyControl` kind is the catalog entry; `latency_provider_request` is `provider_request(R, LatencyControl, ...)` with the latency-specific request descriptor. The cache-key contribution path of `provider.md` §6 is honored: a latency provider's identity participates in pipeline-cache keys only where the consumer explicitly contributes it, ∵ a latency provider does not produce shader output and rarely affects cached artifacts; the consumer that *does* feed latency markers into a shader-visible path (rare, e.g. a Reflex-aware HUD overlay) calls `provider_cache_key_contribute` itself.
- **`frame.pacing`** (peer) — `frame.pacing` consumes the latency-marker context as one of the fields of `Frame_Info` passed to the render callback (gpu-rhi.md §7.5). The pacing layer reads the current provider's mode and last-frame report; it does not write through. Pacing and latency are orthogonal — a frame may be `Continuous` paced with Reflex `OnBoost`, or `OnDemand` paced with platform-native, or `Capped(60)` paced with Anti-Lag (MEL-ENGINE-IX).
- **`render-reconstruction`** (peer) — frame-generation contexts in `render-reconstruction` must register `GeneratedPresentSubmit` / `GeneratedPresentDisplayed` per generated output. The latency provider's per-frame report carries the generated-present timings alongside the real ones; `render-reconstruction` reads them to surface generated-frame latency cost to the app's quality system.
- **`xr`** (consumer) — an XR session is a valid `Mel_Latency_Target`. The XR-runtime's own pacing (`xrWaitFrame` predicted-display-time) is the platform-native lowering on the XR path; a vendor latency provider (Reflex on a desktop XR runtime, Anti-Lag on an AMD XR runtime) may bind to the XR session where the runtime exposes the necessary hooks. XR frame generation is off by default and gated by the rules of gpu-rhi.md §9.3.
- **`platform.sensors`** (consumer-side) — `latency_get_report` does not consult sensors directly, but the app's content model joins thermal pressure (`platform.sensors.thermal`) and power source (`platform.sensors.power`) against the latency report to decide whether to engage Reflex `OnBoost`, drop to platform-native, or disable frame generation under sustained thermal pressure. The engine reports; the app decides (MEL-ENGINE-V).
- **`platform-surface`**, **`platform-display`**, **`media.video`**, **`io.asset`** — not direct dependencies. `media.video`'s presentation cadence may interact with the swapchain's latency provider where a media frame is composited into the render output; the interaction is mediated by `frame.pacing`, not `frame.latency`.

---

## 9. P2 escape

A consumer that wants none of the registry mediation — that wants to call `NvAPI_D3D_SetLatencyMarker`, `agsDriverExtensionsDX12_PushMarker`, `vkSetLatencyMarkerNV`, or `IDXGISwapChain2::GetFrameLatencyWaitableObject` directly — reaches them through the GPU module's native interop headers (`gpu/<backend>/nvapi_interop.h`, `gpu/<backend>/ags_interop.h`, `gpu/<backend>/streamline_interop.h`, `gpu/<backend>/d3d12_interop.h`). The `provider_native(handle)` bridge of `provider.md` §3 returns the raw module handle and symbol table for the case where the consumer wants the registry's *discovery* (ABI probe, search-path resolution, platform-lowering pick) but the consumer's own *invocation*. The cost of the escape is on the consumer: caches no longer see the provider's identity through `provider_cache_key_contribute`, and the consumer is responsible for marker-point ordering, sleep-before-input discipline, and frame-id correspondence end-to-end. That is the price of full control, paid honestly (MEL-ENGINE-II, MEL-ENGINE-IV).
