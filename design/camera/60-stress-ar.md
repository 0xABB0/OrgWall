# P6 stress — reference app class: AR / pose / fitness (`ar`)
> target platforms: ios + android. method: features → cap-IDs → owning module; deny-without-fallback on ios/android = GAP.
> consumes: 30-vocabulary.md · 50-planes.md · 40-matrix.csv · 40-matrix-notes.md · 20-beyond-os.md.
> SLAM/pose/plane-detection are the AR domain CONSUMING camera — verified the design delivers frames + intrinsics + IMU-synced timestamps and STOPS. pose is not a camera cap.

## feature → cap → module

| feature | cap ids | owning module |
|---|---|---|
| low-latency / high-fps preview (60/120fps) | `cap.timing.frame-rate.clamp` | cameracapture |
| back-pressure: keep freshest frame (latency floor) | `cap.frame.drop.policy_latest` | cameracapture |
| zero-copy GPU import (iOS: IOSurface→Metal) | `cap.frame.zerocopy.iosurface` | cameracapture |
| zero-copy GPU import (Android: AHardwareBuffer→Vulkan/GL) | `cap.frame.zerocopy.ahardwarebuffer` | cameracapture |
| continuous AF that doesn't hunt | `cap.control.focus.af-mode` · `cap.control.focus.af-speed` · `cap.control.focus.af-pause` · `cap.control.focus.hunting-state` | cameracontrol |
| per-frame camera intrinsics (reprojection) | `cap.calib.intrinsics.perframe` (fallback `cap.calib.intrinsics.matrix`) | cameracalib |
| lens distortion riding the frame path | `cap.calib.distortion.model` · `cap.calib.distortion.lut` · `cap.calib.distortion.warp` | cameracalib |
| frame timestamp in a mappable clock domain | `cap.timing.frame.timestamp` · `cap.timing.frame.timestamp.clock-domain-id` · `cap.timing.frame.timestamp.clock-domain-select` | cameracapture |
| camera↔IMU clock correlation | `cap.timing.imu-correlation.clock` | cameracapture |
| per-frame gyro / OIS sample stream | `cap.meta.ois.samples` | camerastats |
| wide FoV / ultrawide | `cap.device.class.physical` (UltraWide) · `cap.control.zoom.ratio` (<1.0) · `cap.device.lens.focal_length` | cameradevice · cameracontrol |
| depth stream (LiDAR/ToF) | `cap.depth.map.float` · `cap.depth.map.disparity` · `cap.depth.stream.rate` · `cap.depth.accuracy` · `cap.depth.confidence` · `cap.depth.unit` | cameradepth |
| depth+color time-alignment | `cap.stream.sync.timealigned` | cameradevice |
| rolling-shutter skew (motion deblur / reprojection) | `cap.timing.rolling-shutter.skew` | cameracapture |
| frame sequence / drop detection | `cap.timing.frame.sequence-id` · `cap.frame.drop.signal` | cameracapture |
| IR stream (TrueDepth / active depth assist) | `cap.ir.stream` | cameradepth |

## ios/android classification of the load-bearing caps

| cap | ios | android (camera2ndk) | android (camerax) |
|---|---|---|---|
| `cap.frame.zerocopy.iosurface` | native | deny → `.ahardwarebuffer` | deny → `.ahardwarebuffer` |
| `cap.frame.zerocopy.ahardwarebuffer` | deny → `.iosurface` | native | native |
| `cap.frame.drop.policy_latest` | native | native | native |
| `cap.timing.frame-rate.clamp` | native | native | native |
| `cap.timing.frame.timestamp` | native | native | native |
| `cap.timing.frame.timestamp.clock-domain-select` | deny → `.clock-domain-id` | native | emulate (interop) |
| `cap.timing.imu-correlation.clock` | native | native | emulate (interop) |
| `cap.meta.ois.samples` | **deny → none** | native | emulate (interop) |
| `cap.calib.intrinsics.perframe` | native | **deny → `.intrinsics.matrix`** | **deny → `.intrinsics.matrix`** |
| `cap.calib.intrinsics.matrix` | native | native | emulate (interop) |
| `cap.calib.distortion.model` | deny → `.distortion.lut` | native | emulate (interop) |
| `cap.calib.distortion.lut` | native | deny → `.distortion.model` | deny → `.distortion.model` |
| `cap.calib.distortion.warp` | deny → none (host-side) | deny → none (host-side) | deny → none (host-side) |
| `cap.stream.sync.timealigned` | native | **deny → none** | **deny → none** |
| `cap.depth.map.float` | native | native | emulate (interop) |
| `cap.depth.stream.rate` | native | native | emulate (interop) |
| `cap.depth.accuracy` | native | deny → `.map.float` | deny → `.map.float` |
| `cap.depth.confidence` | deny → none | deny → none | deny → none |
| `cap.depth.unit` | deny → `.map.float` | deny → `.map.float` | deny → `.map.float` |
| `cap.depth.coordmap` | deny → `.map.float` | deny → `.map.float` | deny → `.map.float` |
| `cap.ir.stream` | native | native | emulate (interop) |

## SLAM / pose boundary — confirmed
Camera delivers: frames (`cap.frame.*`, zero-copy GPU), per-frame intrinsics + distortion (cameracalib), IMU-mappable frame timestamps (`cap.timing.imu-correlation.clock` + clock-domain), OIS/gyro samples (camerastats), depth (cameradepth). It STOPS there. No `cap.pose.*` / `cap.slam.*` / `cap.plane.*` exists in the vocab, and none should — SLAM, 6DoF pose, plane fitting, anchor tracking are the downstream AR domain consuming these. The boundary is clean and correct (MEL-ENGINE-IX: pose composes from camera outputs, never inside camera).

## the four key stresses — verdict

1. **zero-copy GPU import per platform** — PASS. `cap.frame.zerocopy.iosurface` native on ios, `.ahardwarebuffer` native on android. The realtime path is native on each target. The cross-axis denies are honest (surface kinds never cross — `frm-surfkind`) and each falls back to the *other* target's native kind, ultimately `cap.frame.map.cpu`. An AR app links one kind per platform; no gap.

2. **per-frame intrinsics + distortion** — PARTIAL. `cap.calib.intrinsics.perframe` native on ios, **deny on android (both camera2 + camerax)** with fallback `cap.calib.intrinsics.matrix` (native camera2 / emulate camerax). For AR this is a real degradation, not a true deny: android delivers a *static* intrinsic matrix per session, not a per-frame attachment — which matters when zoom/crop change intrinsics mid-stream. Fallback exists, so not a deny-without-fallback gap, but flag it as a fidelity cliff. Distortion is split iOS-LUT / android-parametric with mutual fallback; `.warp` (built-in undistort/project) is deny-everywhere → host-side compute `[down]`, which is correct (that math is the AR app's job).

3. **camera↔IMU correlation + OIS stream** — the awkward seam. `cap.timing.imu-correlation.clock` is **cameracapture**; `cap.meta.ois.samples` is **camerastats**. An AR consumer pulling OIS samples must link camerastats (a "pro/analysis surface most apps skip" per 50-planes) purely to get motion-to-frame alignment data — yet OIS displacement is core to AR reprojection on OIS-equipped phones, not a pro-analysis nicety. The clock-correlation living in cameracapture while the OIS samples live in camerastats means one realtime feature (motion-to-frame alignment) is split across two modules of *different plane intent* (data vs intelligence-telemetry). Worse: **`cap.meta.ois.samples` is deny-without-fallback on ios** (`met-avfnoois` — no OIS sample stream on AVFoundation at all). So an iOS AR app gets imu-correlation.clock but NOT per-frame OIS displacement — it must disable OIS or treat it as noise. This is a genuine deny-without-fallback on a primary target.

4. **high-fps + low-latency** — PASS. `cap.timing.frame-rate.clamp` native on ios + both android paths; `cap.frame.drop.policy_latest` native on all three. Clean.

## depth+color time-alignment — three-module assembly
`cap.stream.sync.timealigned` (cameradevice) + depth streams (cameradepth) + timing (cameracapture) for one realtime feature. Two findings:
- **Cross-module assembly is reasonable, NOT a cut problem.** The three modules carry genuinely distinct information: cameradevice owns topology/stream-set wiring (the synchronizer is a stream-graph object), cameradepth owns the depth *meaning*, cameracapture owns buffer+clock. An AR app already links all three for unrelated reasons (it needs cameradepth for depth, cameracapture is CORE, cameradevice is CORE). So "three modules for one feature" costs zero extra linkage — the two it would not otherwise link (none) is empty. MEL-ENGINE-IX holds: these compose, they don't beg to be merged.
- **BUT the realtime synchronizer denies on android.** `cap.stream.sync.timealigned` is native on ios (`AVCaptureDataOutputSynchronizer`) and **deny-without-fallback on android** (`top-camera2-nosyncprimitive` — camera2 has per-result timestamps but no time-aligned bundle-delivery primitive). On android the AR app must hand-align depth+color by `cap.timing.frame.timestamp` correlation itself. That is a deny-without-fallback on a primary target for the time-aligned *delivery*, though the raw material (per-frame timestamps) to do it manually is native. Flag as a fidelity gap, not an absolute one.

## gaps

- **OIS sample stream on iOS** — `cap.meta.ois.samples` is deny-without-fallback (`met-avfnoois`) on ios, a primary AR target. AVFoundation surfaces no per-frame OIS displacement stream. This is the honest ceiling (no API exists), so it is not a vocab gap — but it IS a real deny-without-fallback cell that an AR app on iOS hits. No new cap warranted; record as platform reality. **The seam IS awkward**: OIS-displacement-for-AR is split camerastats (samples) ∥ cameracapture (clock), across two plane intents → flag for P5 recheck whether `cap.meta.ois.samples` should be promoted out of camerastats (intelligence-telemetry) toward the timing/motion-alignment surface an AR consumer actually reaches for. ⇒ `cap.meta.ois.samples` seam ⇒ P5 recheck (module placement).

- **time-aligned depth+color delivery on android** — `cap.stream.sync.timealigned` deny-without-fallback on android (both camera2 + camerax). Honest ceiling (no camera2 bundle-delivery primitive); the per-frame-timestamp manual path exists but is not a fallback the matrix records. No new cap; the manual-correlation path is `cap.timing.frame.timestamp` (native). Consider adding an explicit emulate classification (host-side timestamp-correlation alignment) rather than bare `deny` — parallels the OBS `cap.timing.av-sync.*` emulate-everywhere treatment. ⇒ `cap.stream.sync.timealigned` android ⇒ P5 recheck (deny vs framework-emulate, mirroring av-sync).

- **per-frame intrinsics on android** — `cap.calib.intrinsics.perframe` deny on android, fallback `cap.calib.intrinsics.matrix` (static). Has a fallback → not a deny-without-fallback gap. Fidelity cliff only (static vs per-frame intrinsics under live zoom/crop). No action; recorded.

No new cap-IDs needed (P3 clean). Two P5 module/classification rechecks surfaced (OIS placement; android timealigned deny-vs-emulate). The vocab covers every AR feature; the gaps are platform-ceiling denies and one cross-module-plane awkwardness, not missing capabilities.

## harder stress to suggest next
This class exercised the realtime-frame + geometry + motion-sync spine. It did not stress: multi-cam concurrent AR (front+back simultaneously, `cap.topology.multicam.*` + the `hardwareCost`/`systemPressureCost` budget — note macOS denies multicam entirely, and android concurrency is boolean not cost-graded); thermal/power throttling under sustained 120fps AR (`cap.os.thermal.*` — MEL-ENGINE-VI battery dignity); and orientation/mirroring of intrinsics under device rotation (`cap.os.orientation.*` ∥ `cap.meta.transform.orientation` — does the intrinsic matrix re-orient with the frame?). A "sustained multi-cam AR on a thermally-constrained phone" stress would hit the cost-budget + thermal seams this run left cold.
