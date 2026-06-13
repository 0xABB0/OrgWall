# camera — plane cut
> sole owner of the module boundary; `wireframe` consumes, never re-derives.

Cut along stable planes, **then split each plane to the ship-granular maximum** — a module is a shippable static lib, so any independently-optional cluster carrying distinct information becomes its own lib; an app that never touches it links none of its code (MEL-ENGINE-III/VI). Split stops short of pathological micro-libs (no 2-cap modules). Features (depth, comp-photo, multicam) assemble *across* modules (MEL-ENGINE-IX) — a module is a shippable unit, never a feature.

Thirteen modules. Two **core** (nothing works without them); eleven **opt-in** — linked only if the app uses that cluster.

**Invariant (all modules).** API stays small: `open` / `configure` / `start` / `stop` / `query-caps`. Capabilities are runtime-enumerable **DATA** (the vocab IDs), not API surface — set via `configure(cap-id, value)`, discovered via `query-caps`. Growth = a new cap-ID behind `query-caps`, never a new function. Unknown cap-ID → `deny` on an old impl.

Dependency order (wireframe handoff, smallest-dep first):
**cameradevice → cameracapture → {cameracontrol · cameraptz · cameraphoto · cameracalib · camerapolicy} → {cameradepth · camerameta · cameraeffects} → {cameradetect · camerastats} → cameravirtual**

---

## module: cameradevice  · CORE
planes: device-lifecycle
What devices exist, what they are, how a session/stream opens, reconfigures, tears down. Host OS = provider 0; external/virtual/machine-vision devices register via the shared provider face (mirrors `audioin`/`audioout`/`sensor`). Owns the under-OS raw-access gateway.
caps:
- ALL `cap.device.*` (31) — classes (+`.class.software`: ingest a published vcam — closes the egress↔ingest loop), identity, vendor, transport, facing, mount, sensor geometry/pitch/cfa/white-level, lens characteristics, mic-association, hardware-level, hotplug, connected-state, **`cap.device.access.raw_under_os` + `.extension_unit` + `.sensor_register` (bare-metal I²C)** + **`cap.device.bringup`** (host-driven clock/PLL/power)
- ALL `cap.enum.*` (13) — list, caps-per-device, media-types, the five feasibility shapes + cost-budget, concurrent-sets, **`.include_hidden`** (reveal OS-hidden virtual/screen cams)
- ALL `cap.topology.*` (17) — session.graph, multicam (+cost +`.control-independence`), logical (constituents/switchover/sync-type), physical.hotplug
- ALL `cap.stream.*` (17) — multi (format/usecase/count), source (multivs/kind/**`.format_change_event`**), sync (timealigned/clock-port), reconfigure (live/deferred/params), acquisition.mode, transfer, channel.transport.config

## module: cameracapture  · CORE
planes: data
The frames themselves — memory, layout, lifetime, time. Every modality's bytes ride this machinery; the modality's *meaning* is its own module. Owns the shared capture clock the audio subsystem composes against, and the per-frame attachment channel the metadata modules read.
caps:
- ALL `cap.frame.*` (54) — format deliverability, zero-copy import (iosurface/ahardwarebuffer/dmabuf/d3d11/webgpu), pooling/back-pressure/queue-depth, CPU map + plane layout, drop signal/reason/stats, cookie
- ALL `cap.timing.*` (32) — per-frame timestamps + clock domains, rolling-shutter skew, device-clock (pts/scr-sof/counter), **`cap.timing.av-clock.*` + `.av-sync.*`** (shared time-base — the audio seam), genlock, frame-rate clamp, imu-correlation, SMPTE timecode
- `cap.os.session.{start,stop,running_state_query}` (3) — start/stop/observe the capture session (moved here from camerapolicy — session lifecycle is *core capture*, not OS policy; P6/embedded)
- `cap.meta.ois.samples` (1) — per-frame OIS displacement stream (moved here from camerastats — AR motion-alignment data paired with `cap.timing.imu-correlation.clock`, not skip-me telemetry; P6/AR)

## module: cameracontrol  · opt-in
planes: control (continuous)
Continuous command of sensor, ISP, lens. Transport-neutral — a future VISCA/NDI control transport exposes the *same* cap-IDs.
caps:
- ALL `cap.control.*` (128) — focus / exposure / iso / gain / wb / aperture / zoom-digital / isp / tone / 3a / flash / catalog + `.flash.strobe-output` + manual focus/exposure **ramps** (`.focus.ramp-with-rate` / `.exposure.ramp-with-rate`, P6/FiLMiC)

## module: cameraptz  · opt-in
planes: control (mechanical)
Mechanical actuation — only PTZ/conference/UVC cameras have it, so a phone-camera app never links it. Control-plane sibling of cameracontrol.
caps:
- ALL `cap.ptz.*` (20) — mechanical pan/tilt/roll, optical-motorized zoom + iris, privacy-shutter, optic-controller lens-actuators `[CEILING]`

## module: cameraphoto  · opt-in
planes: control (capture modes)
Shooting modes — RAW, comp-photo, bracketing, on-board encode, hardware triggers. A live-preview/video app links none of it (no DNG, no comp-photo, no UVC-H.264 parse, no GenICam sequencer). Control-plane sibling. *(name provisional — covers HDR-video/high-speed/time-lapse too.)*
caps:
- ALL `cap.capture.*` (66) — still / raw / readout / encoded-still / zsl+reprocess / bracket / burst / hdr-video / high-speed / time-lapse / comp-photo / on-board-encode / still-pipeline / trigger / sequencer / userset

## module: cameracalib  · opt-in
planes: device-lifecycle (optical geometry)
Lens intrinsics, extrinsics, distortion — the optical-geometry data AR/CV/depth consume (and 2D lens-correction too). Split from both cameradevice and cameradepth: a 2D app skips it; a CV app that wants calibration needn't link depth-map code.
caps:
- ALL `cap.calib.*` (12) — intrinsics (+perframe), extrinsics, distortion (model/lut/warp), pixelsize, pose (translation/rotation/reference), stereo-baseline, delivery

## module: cameradepth  · opt-in
planes: data (3D / alternative-modality sensing)
Depth, point clouds, and non-RGB sensing — the cluster a 2D app drops entirely. Delivers through cameracapture's buffer+timing machinery; adds only the depth-specific meaning. IR (5) and spatial (2) fold in here rather than form micro-libs.
caps:
- ALL `cap.depth.*` (20) — depth/disparity/point-cloud maps, accuracy/quality/confidence/invalid/unit, filter, still, stream-rate, exclusive, coordmap, zoom-cofeasibility, container, raw ToF/structured/LiDAR/PDAF `[CEILING]`
- ALL `cap.ir.*` (5) — IR/NIR/mono streams, stereo/depth interleave, NIR-CFA, secure-auth mode
- ALL `cap.spatial.*` (2) — spatial/stereo video, viewing-comfort hints

## module: camerameta  · opt-in
planes: intelligence (emitted readout — settings)
The camera reporting its own per-frame pipeline state — applied 3A settings, convergence, regions — plus the metadata-bag access channel and embedded sensor/UVC-header lines. Passive, control-feedback-oriented. Rides cameracapture frame attachments.
caps:
- `cap.meta.access.*` (3) — bag / rawblob / selectable (the shared metadata channel; cameradetect + camerastats read through it)
- `cap.meta.applied.*` (19) — per-frame applied exposure/iso/gain/ev/aperture/lens/frameduration/wb/cct/tonemap/blacklevel/scenemode/flash/zoom/crop/digitalwindow/binning/geometry echo
- `cap.meta.state.*` + `cap.meta.regions.*` (7) — AE/AF/AWB convergence states, applied metering rectangles
- `cap.meta.embedded.*` (2) — CSI-2 sensor-metadata lines `[CEILING]`, UVC payload-header bits
- `cap.meta.chunk.*` (2) · `cap.meta.transform.orientation` · `cap.meta.damage.region` · `cap.meta.illumination.ir` · `cap.meta.segmask` · `cap.meta.exifblob` · `cap.meta.bracketcorrelation` · `cap.meta.event.exposurephase` `[CEILING]` · `cap.meta.custom.blob` · `cap.meta.hdr.static` (HDR ingest metadata, P6) (13) — chunk/transport/misc emitted metadata

## module: cameradetect  · opt-in
planes: intelligence (emitted readout — scene objects)
The OS-emitted scene-detection metadata — faces, bodies, salient objects, barcode/QR payloads, scene/flicker hints. Distinct consumer from settings-echo (a barcode app links this, not the 3A echo). *Decode/recognition is downstream (`barcode`/CV-ML) — this module only delivers the OS's payload.*
caps:
- `cap.meta.face.*` (6) — rect / id / score / landmarks / pose / expression
- `cap.meta.body.rect` · `cap.meta.salient.rect` · `cap.meta.code.payload` (3)
- `cap.meta.scene.*` (5) — change / flicker / nighthint / illuminance / focusfom

## module: camerastats  · opt-in
planes: intelligence (emitted readout — sensor telemetry)
Per-frame sensor/ISP statistics maps — histogram, lens-shading, hot-pixel, sharpness, OIS samples, ISP blobs, sensor temperature. Pro/analysis surface; most apps skip it.
caps:
- `cap.meta.stats.*` (7) + `cap.meta.sensortemperature` (1) = **8** — histogram / sharpnessmap / lensshadingmap / hotpixelmap / predictedcolor / ispblob / ispparams / sensor-temp (`cap.meta.ois.samples` moved to cameracapture — P6/AR)

## module: cameraeffects  · opt-in
planes: intelligence (computed transform)
OS-computed scene effects — the camera *changing* the frame, not reporting on it. An app that never enables an effect links none of it.
caps:
- ALL `cap.effect.*` (27) — auto-frame / background blur+replace / segmentation-mask deliver / studio-light / eye-contact / reactions / subject-ROI / creative-filter / system-UI / change-notify
- ALL `cap.seg.*` (4) — portrait + semantic mattes, person mask, matte re-orientation

## module: camerapolicy  · opt-in
planes: policy
The OS's rules and rights — consent, lifecycle, thermal — independent of device mechanics. Mirrors `audiopolicy`. (Thermal at 6 caps not worth hoisting out.)
caps:
- `cap.os.*` (46) — consent (prompt/status/packaging/policy-gate/usage-log), OS hotplug events, arbitration (exclusive/shared/contention/priority/preemption), lifecycle/interruption, orientation/rotation/mirroring, privacy-indicator, shutter-sound (regional), hardware capture-input, thermal/power-pressure *(session start/stop/running-state moved to cameracapture — capture lifecycle, not policy; P6/embedded)*

## module: cameravirtual  · opt-in
planes: transport
The egress mirror — publish any app surface as an OS-visible camera, honest local-only where no OS path exists; plus the in-process synthetic source. Builds on the same provider face as cameradevice's ingest.
caps:
- ALL `cap.egress.*` (23) — publish + frame push/pull, local-only fallback, consumer attach/identity/format-negotiate, controls-on-published-cam, share-mode, flow-control, wrap-physical, in-use, mic-association, lifetime, legacy-compat, install-ceiling, scope
- ALL `cap.testsrc.*` (4) — synthetic provider, frame-feed, test-pattern, manual-clock

---

## sibling & cross-plane seams (assembled across modules — MEL-ENGINE-IX, never re-merged)
- **control siblings** — cameracontrol · cameraptz · cameraphoto share the control plane + API shape; split for ship-granularity. Continuous-control-only apps get zero PTZ/photo code.
- **intelligence siblings** — camerameta · cameradetect · camerastats · cameraeffects share the intelligence plane. cameradetect + camerastats read the per-frame bag whose access primitive (`cap.meta.access.*`) lives in camerameta (itself riding cameracapture's frame attachments) — a detect-only app links camerameta's small base, not its applied-echo bulk; the wireframe may promote `cap.meta.access.*` to a shared micro-primitive if that base proves too heavy.
- **raw-UVC access** — cameradevice owns the gateway (`cap.device.access.*`); cameracontrol + cameraphoto consume it for their `emulate(raw-UVC)` cells (external-UVC PTZ, ISP catalog, still-pipeline, calibration-XU).
- **shared capture clock** — cameracapture owns `cap.timing.av-clock.*`/`.av-sync.*`; the audio domain composes onto this time-base. The single A/V seam.
- **depth is a feature** = cameradepth (geometry/streams) + cameracalib (intrinsics) + cameraeffects (portrait matte) — assembled across modules, never merged. Depth/calib/IR streams all ride cameracapture's buffer machinery.
- **provider/virtual pattern** — shared infra under cameradevice (ingest) + cameravirtual (egress); host OS = provider 0. Not a 14th module.

## coverage
Every vocab ID assigned exactly once; zero deferred. By prefix:
- cameradevice = `cap.device.*` + `cap.enum.*` + `cap.topology.*` + `cap.stream.*` = 31+13+17+17 = **78**
- cameracapture = `cap.frame.*` + `cap.timing.*` + `cap.os.session.{start,stop,running_state_query}` + `cap.meta.ois.samples` = 54+32+3+1 = **90**
- cameracontrol = `cap.control.*` = **128**
- cameraptz = `cap.ptz.*` = **20**
- cameraphoto = `cap.capture.*` = **66**
- cameracalib = `cap.calib.*` = **12**
- cameradepth = `cap.depth.*` + `cap.ir.*` + `cap.spatial.*` = 20+5+2 = **27**
- camerameta = `cap.meta.access.*` + `cap.meta.applied.*` + `cap.meta.state.*` + `cap.meta.regions.*` + `cap.meta.embedded.*` + `cap.meta.chunk.*` + `cap.meta.transform.*` + `cap.meta.damage.*` + `cap.meta.illumination.*` + `cap.meta.segmask` + `cap.meta.exifblob` + `cap.meta.bracketcorrelation` + `cap.meta.event.*` + `cap.meta.custom.*` + `cap.meta.hdr.static` = 3+19+7+2+2+8+1 = **42**
- cameradetect = `cap.meta.face.*` + `cap.meta.body.*` + `cap.meta.salient.*` + `cap.meta.code.*` + `cap.meta.scene.*` = 6+3+5 = **14**
- camerastats = `cap.meta.stats.*` + `cap.meta.sensortemperature` = 7+1 = **8**
- cameraeffects = `cap.effect.*` + `cap.seg.*` = 27+4 = **31**
- camerapolicy = `cap.os.*` − `cap.os.session.{start,stop,running_state_query}` = 49−3 = **46**
- cameravirtual = `cap.egress.*` + `cap.testsrc.*` = 23+4 = **27**
- `cap.meta.*` split check: 42-base (incl `.hdr.static`) + 14-detect + 8-stats + 1-`ois.samples`(in cameracapture) = 65 = 64 prior + `.hdr.static` ✓
- total = 78+90+128+20+66+12+27+42+14+8+31+46+27 = **589** ✓ (= vocab count; no leak)

> embedded ship-profile (corrected, P6/embedded): the **minimal shot spine** — bring-up → register control → Bayer frame → trigger → still — fits **cameradevice + cameracapture + cameracontrol + cameraphoto**, with the session-lifecycle move into cameracapture making that genuinely self-contained. But it is NOT true that "every other module is deny on embedded": applied-settings echo (camerameta), IR/mono (cameradepth), calibration (cameracalib), and test-pattern (cameravirtual) all read `native` on the `embedded+baresensor` column too — so a firmware wanting per-frame applied-exposure readback or an IR variant adds those opt-in modules, exactly as on any platform. The 4-module claim holds for the *shot*, not for *all-native-on-embedded*. `cap.device.bringup`/`.sensor_register` + `cap.stream.channel.transport.config` (CSI-2/DVP) are the new bare-metal surface; auto-3A/ISP/demosaic are deny→downstream.
