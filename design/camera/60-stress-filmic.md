# P6 stress — FiLMiC Pro / DoubleTake (slug `filmic`, ios + android)

Multi-cam video director: simultaneous multi-camera capture, per-camera manual everything, focus/exposure/zoom pulls, log + 10-bit HDR, high frame rate, SMPTE timecode, synced-frame output (encode/mux is downstream). The key stress is the **multicam thermal/power cost budget** — feasibility carries a cost dimension, not just format compat (§20 finding, `hardwareCost`/`systemPressureCost` < 1.0).

> verbatim caps + modules from `30-vocabulary.md` / `50-planes.md`. ios/android cells from `40-matrix.csv`.

## feature → cap → module

| feature | cap ids | owning module |
|---|---|---|
| simultaneous front+back / multi-lens **multi-camera** capture | `cap.topology.multicam.concurrent` · `cap.topology.multicam.deviceset.enumerate` · `cap.topology.multicam.participation` · `cap.enum.concurrent_sets` | cameradevice |
| **multicam thermal/power cost budget** (feasibility has a cost dimension) | `cap.enum.feasibility.cost_budget` · `cap.topology.multicam.cost.hardware` · `cap.topology.multicam.cost.systempressure` | cameradevice |
| per-camera independent format | `cap.stream.multi.perstream.format` · `cap.topology.multicam.perstream.cap` | cameradevice |
| simultaneous multi-stream (preview + record per cam) | `cap.stream.multi.concurrent` · `cap.stream.multi.perstream.usecase` · `cap.stream.share.fanout` | cameradevice |
| manual exposure time per camera | `cap.control.exposure.manual-time` · `cap.control.exposure.ae-mode` · `cap.control.exposure.ae-lock` | cameracontrol |
| manual ISO per camera | `cap.control.iso.manual` · `cap.control.iso.range-bounds` | cameracontrol |
| manual focus per camera | `cap.control.focus.manual-lens-position-normalized` · `cap.control.focus.manual-distance-diopters` · `cap.control.focus.af-mode` | cameracontrol |
| manual WB per camera | `cap.control.wb.manual-temperature-kelvin` · `cap.control.wb.manual-tint` · `cap.control.wb.manual-gains-rgb` · `cap.control.wb.mode` | cameracontrol |
| smooth **focus PULL** (ramp to target lens-pos at rate) | `cap.control.focus.af-speed` (continuous-AF speed only) · `cap.control.focus.manual-lens-position-normalized` | cameracontrol |
| smooth **exposure PULL** (ramp exposure/ISO to target at rate) | `cap.control.exposure.manual-time` · `cap.control.iso.manual` · `cap.control.exposure.compensation-ev` | cameracontrol |
| **zoom ramping** with rate | `cap.control.zoom.ramp-with-rate` · `cap.control.zoom.ratio` · `cap.control.zoom.bounds` | cameracontrol |
| lens switch-over factors (logical multi-lens) | `cap.topology.logical.switchover.zoomfactors` · `cap.topology.logical.switchover.behavior` · `cap.topology.logical.switchover.lock.recording` · `cap.topology.logical.constituent.activephysical` | cameradevice |
| log color profile (Apple Log) | `cap.capture.hdr.video.log` | cameraphoto |
| 10-bit HDR video | `cap.capture.hdr.video.tenbit` · `cap.capture.hdr.video.hlg` · `cap.capture.hdr.video.widegamut` | cameraphoto |
| high frame rate (120/240) | `cap.capture.highspeed` · `cap.timing.frame-rate.clamp` | cameraphoto · cameracapture |
| SMPTE / timecode | `cap.timing.timecode.smpte` | cameracapture |
| shared A/V clock (audio device/gain is audio domain) | `cap.timing.av-clock.shared-session` · `cap.timing.av-clock.cross-output-map` · `cap.timing.av-clock.app-timebase` | cameracapture |
| synced frame output (the encode boundary) | `cap.stream.sync.timealigned` · `cap.timing.frame.timestamp` · `cap.timing.frame.sequence-id` · ALL `cap.frame.format.*` | cameracapture |
| high-bitrate encode / mux | — (OUT of scope: media domain) | — |

## cost-budget stress — PASS

`cap.enum.feasibility.cost_budget` ("Combo gated by a thermal/power/hardware cost ledger") + `cap.topology.multicam.cost.hardware` + `cap.topology.multicam.cost.systempressure` together express **"feasible AND within thermal/power budget."** The cost dimension is carried in the feasibility/topology vocab as first-class, not annotated onto a boolean. ios = native (`AVCaptureMultiCamSession.hardwareCost`/`systemPressureCost` < 1.0); android = deny (`top-no-costbudget` / `dev-nocostbudget`, fallback → `cap.enum.concurrent_sets`: concurrency is boolean-feasible only, not cost-graded). Honest platform divergence with a sanctioned fallback — not a gap.

## encode-boundary stress — PASS

Camera delivers synced frames and stops there: `cap.stream.sync.timealigned` + `cap.timing.av-clock.*` + `cap.timing.frame.timestamp` + `cap.frame.format.*`. Encode/mux/bitrate is **not** a camera cap. The only encode-adjacent caps are `cap.capture.onboardencode.*` — receiving the *camera's own* on-board UVC bitstream (a camera deliverable, like a platform-encoded still), which is irrelevant to phone multicam (deny on ios/android) and does NOT model host-side ProRes/H.265 transcode. No feature forces an encode cap into camera; no scope leak.

## per-cam manual under multicam — flagged (§20 caveat, no deny-without-fallback)

The matrix classifies the manual-control caps standalone (native/emulate on ios+android). But §20 records that AVF multicam is **capped 1080p/stream and manual is not independently per-cam** — a platform restriction on the *combination* (manual control + multicam participation), not on either cap alone. The vocab has no cap that expresses "manual control is restricted *under* multicam"; `cap.topology.multicam.perstream.cap` carries only the res/rate ceiling, not the control-independence ceiling. This is a P5-adjacent expressiveness gap (see gaps).

## gaps

- **focus PULL (ramp to target manual lens-position at a specified rate)** ⇒ no cap. `cap.control.zoom.ramp-with-rate` exists for zoom; `cap.control.focus.af-speed` is *continuous-AF transition speed* (auto mode), NOT an app-driven ramp of a *manual* focus position to a target at a rate. FiLMiC's signature focus pull is unmodeled. New cap `cap.control.focus.ramp-with-rate` ("Smooth manual-focus transition to a target lens position at a specified rate"), area §3b, classified `emulate`-everywhere (app-computed interpolation over `manual-lens-position-normalized`/`manual-distance-diopters`, same sub-class as the `cap.timing.av-sync.*` framework-implementable data-plane logic — never `deny`) ⇒ **(P3)**

- **exposure PULL (ramp exposure-time/ISO/EV to target at a specified rate)** ⇒ no cap. No exposure ramp exists at all (zoom has one, focus needs one). New cap `cap.control.exposure.ramp-with-rate` ("Smooth manual-exposure transition — time/ISO/EV — to a target at a specified rate"), area §3c, `emulate`-everywhere over `manual-time`/`iso.manual`/`compensation-ev` ⇒ **(P3)**

- **manual-control independence restricted *under* multicam** ⇒ no cap expresses the cross-feature restriction (§20: AVF multicam disallows independently-per-cam manual + caps 1080p/stream). The per-cam manual caps and `cap.topology.multicam.participation` compose, but the *denial of their composition* under multicam has no carrier; an app cannot `query-caps` "is independent manual allowed while this device is in multicam?". Either extend `cap.topology.multicam.perstream.cap` meaning to cover control-independence, or add `cap.topology.multicam.control-independence` ⇒ **(P5)** (awkward scatter — the restriction lives across cameradevice topology + cameracontrol with no joining cap).

## deny-without-fallback cells (ios / android)

These platform-denials have **fallback: none** — the feature simply cannot be served on that axis (honest MEL-ENGINE-VII divergence, not a vocab gap, but FiLMiC must degrade):

- `cap.capture.hdr.video.log` — **android deny, fallback:none**. Apple Log is iOS-only; android has no log-curve video profile. (ios native.)
- `cap.timing.timecode.smpte` — **android deny** (`tim-nosmpte`). SMPTE/RP188 per-frame timecode unreachable on camera2/CameraX. (ios native via `AVMediaTypeTimecode`.)
- `cap.control.zoom.ramp-with-rate` — **android deny** (`ctl-c2missing`/`ctl-cxmissing`). No `rampToVideoZoomFactor:withRate:` analogue; android zoom-ramp must be app-emulated frame-by-frame over `cap.control.zoom.ratio`. (ios native.)
- `cap.control.focus.af-speed` — **android deny** (`ctl-c2missing`/`ctl-cxmissing`). No continuous-AF speed control. (ios native via `isSmoothAutoFocusEnabled`.)
- `cap.enum.feasibility.cost_budget` + `cap.topology.multicam.cost.{hardware,systempressure}` — **android deny** (`dev-nocostbudget`/`top-no-costbudget`), fallback → `cap.enum.concurrent_sets` (boolean-feasible, no cost grading). The thermal/power budget is iOS-only; android multicam thermal management is the app's problem.
- `cap.stream.sync.timealigned` + `cap.stream.sync.clock.port` — **android deny** (`top-camera2-nosyncprimitive`/`-noportclock`). No `AVCaptureDataOutputSynchronizer` analogue; android multicam frame alignment falls to the `cap.timing.av-sync.*` rebase/jitter machinery (emulate). (ios native — major android gap for a multicam director.)
- `cap.capture.highspeed` — **camerax deny** (CameraX hides constrained high-speed sessions, no Camera2Interop session-type hook). Native on camera2ndk + ios ⇒ the **camera2ndk-vs-camerax split bites here**: FiLMiC on android must use the camera2ndk axis, not CameraX, for HFR.

camera2ndk-vs-camerax split note: under CameraX, the manual surface (`exposure.manual-time`, `iso.manual`, `wb.manual-gains-rgb`, `focus.manual-distance-diopters`, `logical.constituent.*`, `multicam.perstream.cap`) is `emulate` via Camera2Interop key-injection — workable but escape-hatch. camera2ndk gives these native. A pro director on android should sit on camera2ndk.
