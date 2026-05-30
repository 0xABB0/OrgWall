# `frame.pacing` — per-target pacing source and per-tick budget context

Pacing decides **when** the engine wakes to do per-frame work and **what budget that frame inherits**. Rendering is one consumer; simulation tick, audio mixdown cadence, and any reactor-driven cycle ride the same clock. `frame.pacing` therefore lives under `frame.*` — sibling to `frame.latency` and any future `frame.budget` — not under `render.*`. The GPU RHI consumes the pacing tick to drive `present()`; it does not own the clock.

## Inherited principles

- **P1 — Emulate-to-equivalent.** Where a platform exposes no native vsync (headless, embedded reactor-only host), pacing falls back to a high-precision reactor timer at the requested cadence. The API shape — `Mel_Frame_Pacing_Source`, `Frame_Info`, mode enum — is identical; only the lowering changes. Timestamp fields that the platform refuses to grant (e.g. predicted next-present on a runtime without present-timing feedback) are **absent**, never fabricated (MEL-ENGINE-VIII).
- **P2 — Full-control escape.** A custom pacing source is a peer of the engine's four modes, not a degraded subset. The escape exposes the same hook the built-in modes ride: a "tick at this monotonic deadline" arming primitive on the engine reactor, plus a path to populate `Frame_Info`. OpenXR-driven pacing, video-encoder pull-clock pacing, and network-heartbeat pacing all reimplement against it.
- **MEL-ENGINE-III — no stolen cycles.** `OnDemand` **suspends the vsync callback entirely** on idle. The reactor does not wake to render a frame the app has not requested. A single-mode-with-a-knob would miss this; the mode is a distinct primitive precisely so the platform-level suspension is unambiguous.
- **MEL-ENGINE-V — respect the user's product.** Pacing reports the budget; the app holds the policy. The engine never lowers internal resolution, drops to `Capped(30)`, or suspends compute on its own — it surfaces thermal tier, power source, low-power-mode, GPU/CPU time of the prior frame, and predicted next-present where granted, and the app's content model chooses the response.
- **MEL-ENGINE-VI — respect every device.** Battery-aware modes (`OnDemand`, `Capped`) and background-window auto-drop are first-class, not afterthoughts.
- **MEL-ENGINE-VIII — fail with honor.** Missing timing fields are reported absent. A custom pacing source that misses its deadline is logged at the site; the engine does not silently coalesce ticks.

## Public objects

- **`Mel_Frame_Pacing_Source`** — per-swapchain (more generally, per-target) pacing object, wired to the underlying native vsync or custom source. Carries the mode, the bound reactor, the target descriptor it paces, and the timing-feedback channel the platform grants it.
- **`Frame_Info`** — the per-tick budget context handed to the render / sim callback. Strictly a pacing-tick context: it is **not** a frame-retirement index and never exposes the GPU RHI's internal retirement counter.
- **Mode enum** — `Continuous | OnDemand | Capped(target_fps) | Adaptive(target_frame_ms)`. Mode is set at source creation and may be reseated at any pacing tick boundary; transitions are atomic with respect to the next tick. A `Continuous → OnDemand` transition that lands while a vsync callback is in flight completes the in-flight tick under `Continuous` semantics and applies the suspension immediately after; a `OnDemand → Continuous` transition re-arms the platform vsync subscription before returning from the mode-set call.

## The four modes

Each mode is a distinct semantic primitive so each backend applies the mode-specific platform optimization without conditionals scattered through the lowering. The modes are not interchangeable shapes of one underlying behavior; they differ in **whether the reactor is armed at all**, **what the reactor is armed against**, and **how the cap is enforced**.

- **`Continuous`** — render every vsync the platform delivers. Game default. The reactor is armed against the platform vsync source for the lifetime of the swapchain. Under VRR the inter-vsync interval varies; pacing forwards the variable interval to `Frame_Info` rather than pretending it is fixed.
- **`OnDemand`** — render only after `invalidate()`. UI default. The engine **suspends the vsync callback entirely** on idle: Choreographer unsubscribed, `CADisplayLink.isPaused = YES`, DXGI waitable removed from the reactor wait set, `requestAnimationFrame` not re-queued, Vulkan present-wait fence not waited on. On `invalidate()` the source re-arms the platform vsync for exactly one tick, fires, and re-suspends if no further invalidate has arrived. Coalescing rule: multiple `invalidate()` calls between two ticks collapse to one tick — the tick is a level, not an edge. The static-scene cost is reactor-idle, not vsync-aligned-spin (MEL-ENGINE-III, MEL-ENGINE-VI). A single-mode-with-a-knob design would have to treat "idle" as a degenerate case of "rendering"; here it is its own primitive and the platform-level suspension is unambiguous.
- **`Capped(target_fps)`** — render at most `target_fps`, skipping vsyncs to hold the cap (e.g. 60 fps on a 120 Hz panel by skipping every other vsync). The mobile battery lever, and the mechanism behind a user-facing "60 / 90 / 120 fps" toggle. The cap is enforced at the pacing source by skipping platform vsync ticks, **not** by a sleep at the end of the frame: the platform never delivers the skipped vsync callbacks, so the reactor stays asleep through them. Where the platform exposes a "preferred frame rate" API (iOS `CADisplayLink.preferredFrameRateRange`, Android `Surface.setFrameRate`) the cap is forwarded so the OS itself participates in the enforcement (display-controller dim-the-clock, where granted). When `target_fps` does not divide the display refresh evenly, the cap rounds **down** to the nearest achievable cadence rather than up; the actual achieved cadence is reported in `Frame_Info.mode_state`.
- **`Adaptive(target_frame_ms)`** — pacing targets a frame-time budget. Each tick exposes `headroom_ns = target_frame_ms − max(prior_cpu_ns, prior_gpu_ns)` in `Frame_Info`; the app's quality-scaling system decides what to do with positive or negative headroom. The reactor is armed against the platform vsync (the budget is per-frame, not per-second), so cadence under `Adaptive` matches `Continuous` and the mode differs only in the budget channel. The engine **does not** itself lower resolution, drop a shadow pass, or suspend a compute job — it reports (MEL-ENGINE-V).

## `Frame_Info` contents

Delivered to the render / sim callback at every pacing tick. `Frame_Info` is the **per-tick budget context**, not a frame-retirement index — §3.3 of the GPU RHI keeps retirement future-gated and never exposes the internal retirement counter publicly. The fields below are populated where the platform grants them and absent otherwise; the app branches on presence, never on a sentinel value (MEL-ENGINE-VIII).

- **`prior_gpu_ns`** — wall time of the prior frame on the GPU, from timestamp queries straddling the prior submission. The query primitive lives in the GPU RHI's recording surface (U24); pacing consumes the resolved result. Absent on the first tick of a source, absent on backends where the timestamp tier is ungranted.
- **`prior_cpu_ns`** — wall time of the prior frame on the CPU between two pacing ticks. Always defined after the first tick.
- **`predicted_next_present_ns`** — monotonic timestamp of the predicted next present, populated only when the platform grants present-timing feedback: Vulkan `VK_EXT_present_timing` or `VK_GOOGLE_display_timing` (the older Android fallback); Metal via `CAMetalDrawable.presentedTime` running-history extrapolation; D3D12 via `IDXGISwapChain1::GetFrameStatistics`. Absent on Web (no timing channel in WebGPU at this spec time), absent on Vulkan runtimes without either timing extension, absent on first tick.
- **`headroom_ns`** — remaining budget against the active mode's target. In `Adaptive`, defined as `target_frame_ms − max(prior_cpu_ns, prior_gpu_ns)`. In `Continuous` and `Capped`, defined as `tick_interval − max(prior_cpu_ns, prior_gpu_ns)` where `tick_interval` is the observed inter-tick delta (variable under VRR). Undefined in `OnDemand`. May be negative when the prior frame overran its budget; the app is told the truth, not clamped to zero.
- **`mode_state`** — mode-specific fields: `Capped` reports `vsyncs_skipped` and `achieved_fps` (which may differ from `target_fps` when the cap does not divide the refresh evenly); `OnDemand` reports `invalidate_coalesce_count` since the last tick; `Adaptive` reports `target_frame_ms` (echoed for callback convenience); `Continuous` reports the empty unit.
- **`jitter_offset_current`**, **`jitter_offset_prior`** — subpixel jitter offsets for the current and prior frame. `render.reconstruction` consumes both so TAA / upscaling reprojection stays reproducible without engine-side per-pass bookkeeping. Pacing owns the jitter sequence because it owns the pacing tick, and the sequence is per-source so two swapchains with independent reconstruction do not collide on a shared global sequence. The sequence is deterministic given a seed, which the app may pin per source for reproducibility (capture replay, test goldens).
- **`thermal_tier`** — `nominal | fair | serious | critical`. Sourced from `sensor.thermal`; not redefined here. Updated at the pacing tick from the most recent `thermal_pressure_changed` event. Constant across ticks until the platform raises a transition.
- **`power_source`** — `ac | battery | unknown`. Sourced from `sensor.power`. `unknown` is a real value on hosts where the platform refuses to disclose (some Linux desktops without UPower, some Web embeddings).
- **`low_power_mode`** — `off | on`. Sourced from `sensor.power`. Independent of `thermal_tier`: a laptop on AC may be in low-power mode (the user wants quiet, not cool); a phone at 90 % battery may be in low-power mode (the user is conserving for later). The app must read both, not pick one as a proxy for the other.
- **`latency_context`** — the current latency-marker context from `frame.latency`. Pacing carries the handle; the marker taxonomy (`SimStart`, `SimEnd`, `RenderSubmitStart`, `RenderSubmitEnd`, `PresentStart`, `PresentEnd`, `InputSample`) and the `mel_*_latency_sleep` API live in `frame.latency`. The GPU-side `cmd_latency_mark(point)` recording command stays in the GPU RHI's recording surface; pacing only carries the context handle through `Frame_Info`.

## Native vsync wiring

Each platform's pacing source binds the highest-fidelity source it grants.

- **Android — `Choreographer`.** `Choreographer.postFrameCallback` per tick in `Continuous` / `Capped`; unsubscribed in `OnDemand`. Skip-vsync arithmetic in `Capped` rides the Choreographer interval directly.
- **iOS / macOS — `CADisplayLink`**, or **`CAMetalDisplayLink`** on Apple Silicon where it is the native pacing primitive bound to the Metal drawable. `isPaused` toggles for `OnDemand`. `preferredFramesPerSecond` / `preferredFrameRateRange` carries `Capped` to the platform so the OS itself enforces the cap (battery-respectful).
- **Vulkan — `VK_KHR_present_wait2` + `VK_KHR_present_id2`** for per-surface frame waits; **`VK_KHR_swapchain_maintenance1`'s `FIFO_LATEST_READY` present mode** for FIFO pacing with reduced latency; **`VK_EXT_present_timing`** for the timing-feedback channel that populates `predicted_next_present_ns`; **`VK_GOOGLE_display_timing`** as the older fallback on Android Vulkan runtimes that ship it. Pacing waits on the per-id fence the GPU RHI's swapchain returned for the prior present.
- **Windows D3D12 — DXGI frame-latency waitable object** for the wait, **`IDXGISwapChain1::GetFrameStatistics`** for the timing-feedback channel. `OnDemand` releases the waitable handle from the reactor's wait set rather than spinning on a zeroed timeout.
- **Web — `requestAnimationFrame`** in `Continuous` / `Capped` (cap implemented by re-queueing only every N callbacks); not re-queued in `OnDemand`.
- **Fallback — high-precision reactor timer** where no native vsync source exists (offscreen embedded host, future headless mode). Timer cadence honors the active mode; timing-feedback fields are absent.

The GPU RHI's swapchain surface owns the underlying present-wait, present-id, and frame-statistics primitives. `frame.pacing` consumes their results; it does not re-export them. Apps that want the raw primitives reach through `gpu.swapchain`, not through `frame.pacing`.

A capability tier `caps.pacing.timing_feedback = none | basic | predicted` reports what the platform-bound source can populate in `Frame_Info`:

- **`none`** — no timing channel; `predicted_next_present_ns` is always absent, `prior_gpu_ns` requires the GPU timestamp tier separately. Web sits here.
- **`basic`** — prior-present timestamps available; `predicted_next_present_ns` extrapolated from running history. Metal `presentedTime`, D3D12 `GetFrameStatistics`, Android Vulkan with `VK_GOOGLE_display_timing`.
- **`predicted`** — explicit predicted-present from the platform. Vulkan with `VK_EXT_present_timing` granted; OpenXR via `xrFrameState.predictedDisplayTime` for custom XR sources.

## Multi-swapchain coordination

Each swapchain (or other paced target) carries its own `Mel_Frame_Pacing_Source`. One reactor pumps all of them. Sources are independent: a 60 Hz secondary monitor and a 120 Hz primary tick at their own cadences; a foreground game window in `Continuous` and a background tools-palette window in `OnDemand` coexist without cross-coupling. Per-source `Frame_Info` is delivered to the per-target render callback the app registered; there is no single global `Frame_Info`.

**Background-window auto-drop.** When the platform raises a window-occluded, window-deactivated, or window-minimized event upward through `platform.surface`, the engine auto-drops the affected swapchain's pacing source to `OnDemand` (battery on multi-window UIs; MEL-ENGINE-VI). On the inverse event the source restores its prior mode. The app may opt out per-source — a background-rendered capture window or a recording overlay legitimately needs `Continuous` — by setting `auto_drop_on_background = false` at source creation. The opt-out is per-source, not global, because mixed-policy multi-window apps are common.

**Cross-source coherence.** Where two sources must tick coherently — mirrored displays, stereo rigs on separate Metal layers, projection blends across two outputs of one GPU — the app installs a custom pacing source that fans the same underlying clock out to both targets. The engine does not invent a "primary" source and does not provide a built-in mirror mode; coherence is a P2 concern because what "coherent" means is content-model-specific (genlocked? best-effort? aligned at the next common multiple?).

**No global frame counter.** Frame counters, where the app needs them, are per-source. Two swapchains paced independently produce two unrelated sequences; the engine refuses to invent a synthetic global index that would imply coherence it does not enforce.

## VRR and adaptive sync

Variable-refresh-rate / adaptive-sync displays (G-SYNC, FreeSync, ProMotion, Android VRR) are transparent at the pacing layer. The underlying `FIFO` — or `FIFO_LATEST_READY` where Vulkan grants `VK_KHR_present_mode_fifo_latest_ready` — present mode handles the variable interval at the display controller; the pacing source observes variable inter-tick deltas in `Continuous` and `Capped` and forwards them. `predicted_next_present_ns` continues to populate where present-timing feedback is granted, and the reported value reflects the variable cadence, not a synthesized fixed one. Pacing does not pretend the interval is fixed and does not lock the display to a fixed rate; the VRR-vs-fixed-rate decision lives in `display`'s mode-set surface, not here.

## P2 — custom pacing source

A custom `Mel_Frame_Pacing_Source` is a first-class peer of the four built-in modes, not a degraded subset and not a callback bolted onto `Continuous`. It rides the same engine-reactor arming primitive the built-in modes use, populates the same `Frame_Info`, and binds to the same target (a swapchain, an offscreen render target, or a non-render target such as a video encoder). The custom-source contract is small, and that smallness is what makes the escape a peer rather than a knob:

- an **arming function** the engine calls to ask "wake me at this monotonic deadline" or "wake me when this event fires" — same reactor primitive the built-in modes ride;
- a **tick callback** the source invokes when its wake condition is satisfied;
- the **right to populate any `Frame_Info` field** the platform-built-in sources would populate — `predicted_next_present_ns`, `prior_gpu_ns`, `prior_cpu_ns`, `headroom_ns`, `mode_state`, the jitter offsets — drawing from whatever timing channel the custom source's underlying clock grants;
- the **right to leave absent any field** it cannot honestly fill — `predicted_next_present_ns` on a clock with no future-prediction channel is absent, not zeroed (MEL-ENGINE-VIII);
- a **deadline-miss signal** the engine logs at the source site when the custom source fails to call back within its declared interval. The engine never silently coalesces missed ticks.

Canonical cases:

- **OpenXR-driven pacing.** XR swapchains (`design/xr.md`) are paced by `xrWaitFrame`'s predicted-display-time, not by the platform vsync. The XR target installs a custom pacing source that fires on the OpenXR runtime's frame-loop callback and writes `xrFrameState.predictedDisplayTime` into `Frame_Info.predicted_next_present_ns`. `prior_gpu_ns` / `prior_cpu_ns` are populated identically to the built-in modes; the custom source diverges only in the wake-up mechanism. This is the load-bearing P2 case — XR is a first-class target family, not an exotic — and the contract above is sized exactly to admit it without privileging it.
- **Video-encoder pull-clock.** A live-streaming or recording pipeline whose encoder cadence drives render — the encoder pulls a frame at every encode tick, and that pull is the pacing event. The custom source fires on encoder-frame-due, `predicted_next_present_ns` carries the encoder's PTS, and the render output flows through `media.video` (see `design/media-video.md`) without a display present at all. The pacing target is the encoder session, not a swapchain.
- **Network heartbeat.** Multi-machine setups — LED-wall video walls, projection cluster, lock-step simulation — paced by a network sync packet from a primary node. The custom source fires on packet arrival; `predicted_next_present_ns` carries the cluster-synchronized display-time. Each node's pacing source is custom; the network protocol is application-level (Melody does not impose a cluster-sync wire format).

Built-in modes are themselves implemented against this contract — there is no privileged path. The escape is a peer (MEL-ENGINE-IV: the architecture bends, never breaks).

## Source lifecycle

A `Mel_Frame_Pacing_Source` is created against a target descriptor (typically a swapchain handle, optionally a non-swapchain target for custom sources), an initial mode, and an optional reactor handle (defaults to the engine's per-target reactor). Creation **does not arm** the platform vsync; the first arming happens on `start()`. `pause()` un-arms without destroying the source — useful when the target is hidden but expected to return. `stop()` un-arms and tears down the platform subscription. `destroy()` releases the handle.

Mode transitions are atomic with respect to ticks (see "Public objects" above). Target re-binding — same source, different swapchain because the swapchain was recreated on a window resize — is allowed and preserves the source's jitter sequence so reconstruction does not see a discontinuity on resize.

The source carries no internal frame-counter. Where the app needs one, it counts ticks in the render callback; the counter is the app's, not the engine's.

## Reactor integration

`frame.pacing` rides the engine reactor; it does not own its own thread. One reactor pumps every pacing source bound to it — typically the engine's main reactor pumps every per-window source, with custom reactors used only where the app has a reason (a dedicated render thread bound to a dedicated CPU, a worker reactor for an offscreen capture pipeline).

Tick delivery is **single-threaded per source**: the render callback for a given source runs to completion before the next tick for that source is scheduled. Two sources on the same reactor are serialized through the reactor's run-loop; two sources on different reactors run concurrently and the app is responsible for any cross-source synchronization.

The pacing source does not pre-empt itself: if the render callback overruns the inter-tick interval, the next tick is **late**, not dropped. The miss surfaces in `Frame_Info.mode_state` (negative `headroom_ns`, `Capped.vsyncs_skipped` increment) and the engine logs at the source site (MEL-ENGINE-VIII). It is the app's responsibility to decide whether to scale quality down or let the cadence slip.

## Sibling-module dependencies

Upstream — pacing consumes:

- `sensor.thermal` — `thermal_pressure_changed(tier)` events drive `Frame_Info.thermal_tier`.
- `sensor.power` — `power_source_changed` and `low_power_mode_changed` events drive `Frame_Info.power_source` and `Frame_Info.low_power_mode`.
- `platform.surface` / `display` — window-occluded / activation events trigger background-window auto-drop; display refresh-rate descriptors inform the `Capped` cap arithmetic.
- `frame.latency` — the latency-marker context populated by `cmd_latency_mark` (recording-side; GPU RHI) and the `mel_*_latency_sleep` API ride here; pacing carries the context handle through `Frame_Info`.
- The GPU RHI's swapchain surface (`gpu.swapchain`) — present-wait / present-id / frame-statistics results feed `predicted_next_present_ns` and the prior-frame GPU timestamps feed `prior_gpu_ns`.

Downstream — pacing is consumed by:

- The render / sim callback the app installs on the reactor — primary consumer.
- `render.reconstruction` — TAA and upscaling read `jitter_offset_current` / `jitter_offset_prior` from `Frame_Info`.
- `render.graph` — the render graph compiles against the current `Frame_Info.headroom_ns` for any quality-scaling pass that opts into pacing feedback.
- `xr` — the XR target installs a custom pacing source against the contract defined here.
- `media.video` — encoder pull-clock pacing rides the same custom-source contract.
- `io.asset` — streaming heuristics may consult `thermal_tier` / `low_power_mode` to back off background residency work; pacing is the carrier, `sensor` is the source.
- `media.video` (decode side, distinct from the encoder pull-clock case above) — decoder-driven playback consults pacing to align display-frame fetches with the swapchain's pacing tick.

The bidirectional appearance of `media.video` in both lists is honest, not a contradiction: the encoder pull-clock is a custom pacing **source** (writes `Frame_Info`); a video player is a pacing **consumer** (reads it). The module's role flips with the use case, which is the whole point of designing pacing as a thin contract rather than a render-coupled mechanism.
