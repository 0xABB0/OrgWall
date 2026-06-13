# P6 stress — Zoom / Google Meet (video conferencing)

ref app `zoom` · platforms: macos · win32 · linux · web · android · ios
method: feature → cap-IDs (verbatim) → owning module (verbatim). caps confirmed against `40-matrix.csv`/`40-matrix-notes.md`. key stress = ingest-of-a-published/OS-virtual-camera as an ordinary capture source (the closure of cameravirtual's egress).

## feature → cap → module

| feature | cap ids | owning module |
|---|---|---|
| enumerate capture devices | `cap.enum.list` · `cap.device.id.stable` · `cap.device.name.human` · `cap.enum.media_types` | cameradevice |
| classify each source (physical / external-UVC / continuity / capture-card) | `cap.device.class.physical` · `cap.device.class.external.uvc` · `cap.device.class.continuity` · `cap.device.class.capture_card` · `cap.device.facing` | cameradevice |
| **pick an OS *software/published* virtual camera (OBS/v4l2loopback/CMIO-ext) as the input** | **— no cap (see gaps G1) —** closest = `cap.enum.list` (it shows up as an opaque source) but no class flag + macOS hides it without CMIO `AllowScreenCaptureDevices` | cameradevice |
| hot-SWAP camera mid-call (pick a different source, re-wire the graph) | `cap.topology.session.graph` · `cap.stream.reconfigure.live` · `cap.device.hotplug` · `cap.device.connected_state` | cameradevice |
| continuous reconfigure (res/fps) without session teardown | `cap.stream.reconfigure.live` · `cap.enum.feasibility.query_open` · `cap.enum.feasibility.constraints` (web) | cameradevice |
| simultaneous capture — local self-view preview + the encoded stream sent | `cap.stream.multi.concurrent` · `cap.stream.multi.perstream.format` · `cap.stream.multi.perstream.usecase` · `cap.stream.share.fanout` | cameradevice |
| frame delivery (the bytes both preview + encoder consume) | `cap.frame.*` (format deliverability, zero-copy import, pooling/back-pressure, CPU map) | cameracapture |
| A/V sync of camera frames to the mic on a shared clock | `cap.timing.av-clock.*` · `cap.timing.av-sync.*` (rebase/jitter-buffer/ts-jump-recovery/offset) | cameracapture |
| background blur | `cap.effect.bg_blur.toggle` · `cap.effect.bg_blur.config` · `cap.effect.bg_blur.support_query` · `cap.effect.bg_blur.state` | cameraeffects |
| background replacement | `cap.effect.bg_replace.toggle` · `cap.effect.bg_replace.support_query` · `cap.effect.bg_replace.state` | cameraeffects |
| receive seg-mask for app-side compositing (own blur/replace render) | `cap.effect.segmentation_mask.deliver` · `cap.seg.person.mask` | cameraeffects |
| auto-framing / Center Stage | `cap.effect.autoframe.toggle` · `cap.effect.autoframe.config` · `cap.effect.autoframe.support_query` · `cap.effect.autoframe.state` · `cap.effect.autoframe.regime` | cameraeffects |
| eye-contact / gaze correction | `cap.effect.eye_contact.toggle` · `cap.effect.eye_contact.config` | cameraeffects |
| studio lighting | `cap.effect.studio_light.toggle` · `cap.effect.studio_light.support_query` · `cap.effect.studio_light.state` | cameraeffects |
| reactions (thumbs-up etc.) | `cap.effect.reaction.toggle` · `cap.effect.reaction.fire` · `cap.effect.reaction.catalog` · `cap.effect.reaction.state` | cameraeffects |
| OS effects panel / change-notify (user toggles blur in OS UI) | `cap.effect.show_system_ui` · `cap.effect.change_notify` | cameraeffects |
| consent prompt + status + status-change | `cap.os.consent.prompt` · `cap.os.consent.status_query` · `cap.os.consent.status_change_event` · `cap.os.consent.usage_string` · `cap.os.consent.manifest_capability` · `cap.os.consent.policy_gate` | camerapolicy |
| in-use / privacy indicator + OS kill-switch | `cap.os.privacy.indicator_state` · `cap.os.privacy.toggle_state_query` · `cap.os.lifecycle.os_mute_event` | camerapolicy |
| interruption / lifecycle (call comes in, app backgrounded, thermal) | `cap.os.lifecycle.interruption_event` · `cap.os.lifecycle.interruption_ended_event` · `cap.os.lifecycle.interruption_reason` · `cap.os.lifecycle.background_block` · `cap.os.lifecycle.multitask_access` | camerapolicy |
| arbitration (another app already holds the cam) | `cap.os.arbitrate.in_use_query` · `cap.os.arbitrate.contention_error` · `cap.os.arbitrate.shared_open` · `cap.os.arbitrate.preemption_event` | camerapolicy |
| thermal degradation under a long call | `cap.os.thermal.state_query` · `cap.os.thermal.state_event` · `cap.os.thermal.headroom_forecast` | camerapolicy |
| orientation / front-mirror for self-view | `cap.os.orientation.display_rotation` · `cap.os.orientation.output_rotation_apply` · `cap.os.orientation.front_mirror` | camerapolicy |
| session start/stop/running | `cap.os.session.start` · `cap.os.session.stop` · `cap.os.session.running_state_query` | camerapolicy |
| web target (getUserMedia) as a first-class path | every row above carries a `web/getusermedia` cell | (cross-cutting) |
| basic per-call controls (exposure/WB/zoom the user nudges) | `cap.control.exposure.ae-mode` · `cap.control.wb.mode` · `cap.control.zoom.ratio` · `cap.control.focus.af-mode` | cameracontrol |
| publish our composited feed back out as a virtual cam (egress, the mirror side) | `cap.egress.publish` · `cap.egress.publish.frame_push` · `cap.egress.consumer.attach_events` · `cap.egress.install.*` | cameravirtual |

## gaps

### G1 — ingest of a published/OS *software* virtual camera is unexpressed (P3, new cap-ID)

The key stress fails. The brief assumed `cap.device.class.virtual` + enumeration closes the loop with cameravirtual's egress. It does **not**: `cap.device.class.virtual` is defined verbatim as *"Logical device fused from ≥2 constituents"* (avf `isVirtualDevice` / camera2 `LOGICAL_MULTI_CAMERA`) — i.e. **multi-lens sensor fusion**, the exact opposite of an OBS/v4l2loopback/CMIO-extension **software** camera. There is **no** `cap.device.class.software` / `.published` / `.loopback` ID, and §14a confirms no such ID was dropped (only multi-lens decomposition was folded into `cap.topology.logical.*`).

Two halves of the same hole:

1. **No device-class for a software/published virtual cam.** A consumer of cameravirtual's `cap.egress.publish` output (the OBS-virtualcam case) appears, if at all, only as an opaque `cap.enum.list` entry with no flag distinguishing it from a real sensor. cameravirtual mints the *egress* side (`cap.egress.*`) but no ingest-side cap acknowledges the published cam as a first-class **capture source** — the egress/ingest loop is open. (cameravirtual / cameradevice plane seam.)

2. **No ingest-discoverability cap.** `20-beyond-os.md` (OBS reference, source-cited) is explicit that on macOS the published/virtual/screen cams are **hidden from AVFoundation** until `kCMIOHardwarePropertyAllowScreenCaptureDevices=1` is set via the CMIO low-level API; OBS uses **DirectShow, not MF, on win32 precisely for virtualcam discoverability**. That "make the OS reveal software cameras to enumeration" toggle is a real, witnessed capability with **no vocab cap** — `cap.enum.list` silently inherits the OS default (AVFoundation/MF *hide* them), which is a silent-default trap (MEL-CODE-007): on macos+win32 a Zoom-class app cannot even see the OBS cam without it.

⇒ **`cap.device.class.software` ⇒ new cap-ID (P3)** — a device-class flag for an OS software/published/loopback camera (avf: CMIO software-device · mf/dshow: virtual source filter · v4l2: loopback node · pipewire: non-hardware `Video/Source` node). Native macos/win32/linux; `deny` web (no class surface) → falls back to `cap.enum.list` (opaque). Area 1 (devices&enum), module **cameradevice**.

⇒ **`cap.enum.list.include_software` (or `cap.enum.software_visibility`) ⇒ new cap-ID (P3)** — toggle/declare that OS-hidden software cams are surfaced into enumeration. Native macos (CMIO `AllowScreenCaptureDevices`) · win32 (DShow backend vs MF) · `native`-by-default linux/pipewire/v4l2 (loopback nodes already listed) · web `deny` (getUserMedia lists whatever the UA lists, no toggle). Area 1, module **cameradevice**. This is the literal closure of the bidirectional-ingest charter and removes the silent default.

### G2 — OS effects deny-without-fallback on linux + the whole non-OS-effect estate (P3, area + honest-degrade)

bg_blur / bg_replace / studio_light / eye_contact `.toggle` are **`deny` on all three linux axes** (v4l2/libcamera/pipewire, note `eff-noeffect`) **with `fallback none`** — and the same for ios on bg_blur, android/ios/uvc on eye_contact, everywhere-but-win32 on studio_light. The cap survives in vocab (native elsewhere — MEL-ENGINE-I is satisfied at the *vocab* level), so this is **not** a vocab gap. But for a Zoom-class app, blur on Linux is table-stakes; the deny-without-fallback is honest only because the *real* fallback (app-computed segmentation → app-side blur render) is classified `[down]`/out-of-camera-scope. That is the correct boundary, but the matrix gives the conferencing author **no in-vocab signal** that the honest degrade path exists:

- `cap.effect.segmentation_mask.deliver` is **also `deny` on every linux axis** (no OS-emitted mask), so the app cannot even get the OS matte to render its own blur — it must run its own ML segmentation entirely downstream.

⇒ **no new cap needed**, but flag for P4/wireframe: every `cap.effect.*.toggle` deny cell with `fallback none` on a conferencing-target platform (all of linux; web for bg_replace/studio_light) is an **honest-degrade deny** whose only recovery is downstream app compute. The wireframe must surface this as an explicit "OS effect absent → app-render" branch, not a silent `deny`. (No cut change; documentation/contract obligation. Marginal P5 consideration: whether a `cap.effect.*.app_render_available` advisory belongs in cameraeffects — likely no, it's downstream — but record it.)

### G3 — `cap.egress.mic_association` (publish our A/V back out) is `?` on both native axes (existing P7 `?`, conferencing-relevant)

Not new, but Zoom-as-publisher (sending its composited tile out as a virtual cam with audio) leans on `cap.egress.mic_association`, flagged `?` on avf+mf in §14d. Conferencing is the witness that elevates it from curiosity to load-bearing; recommend it stays on the P7-resolve list with conferencing cited as the driver.

## non-gaps confirmed (no deny-everywhere-without-fallback)

- hot-swap / live reconfigure: `cap.stream.reconfigure.live` `deny` on linux(all)/genicam/embedded but **native macos/win32/web/android/ios** + fallback `top-*-reconfig-teardown` (stop+reconfigure+restart) — honest degrade exists.
- simultaneous preview+stream: `cap.stream.multi.concurrent` **native on every consumer axis**; `cap.stream.share.fanout` deny on linux/win32/uvc but fallback = open two outputs off one session (`cap.stream.multi.concurrent`) — covered.
- consent: `cap.os.consent.prompt` deny on v4l2/libcamera but **native pipewire** (the portal path) — linux is covered via the pipewire axis; native everywhere else.
- privacy indicator: `cap.os.privacy.indicator_state` deny on most axes (LED not readable) — but this is an honest hardware fact (the OS owns the LED), not a fallback gap; `native` android. No conferencing feature needs to *read* it, only to *not defeat* it.
- web is first-class throughout: every conferencing cap above carries a `web/getusermedia` cell — effects are `native` (note `eff-webexp`: Chromium-experimental, Chrome 116+, Firefox/Safari absent — honest `?`-adjacent degrade, not a deny), constraints/reconfigure native, consent/lifecycle/os-mute native.

## verdict

Anything new: **YES** — one genuine vocab hole (G1, two cap-IDs) plus one contract obligation (G2). The published-virtual-camera-ingest stress is the right probe and it **broke** the design exactly where predicted-not-to: `cap.device.class.virtual` is multi-lens fusion, not a software cam, and nothing in the vocab consumes an OS-published virtual camera as a first-class source or reveals the OS-hidden software cams to enumeration.
