# camera — charter

Acquisition of live image/video streams and still captures from physical camera
devices: enumeration of devices and their sensor/lens capabilities, configuration
of the capture pipeline (resolution, format, framerate, focus, exposure, white
balance, zoom, torch, stabilization, multi-stream, depth, RAW, and vendor
computational-photography modes — HDR/Night/Bokeh/Portrait), and delivery of
timestamped frames (plus camera-produced metadata: 3A state, face/scene rects,
depth maps, per-frame sensor settings) to the application, at the full ceiling
each platform's sensor + ISP affords. Frames are handed off CPU- or GPU-resident;
what the app does with them (encode, display, infer) belongs to other domains.

Control is **fine-grained**: not auto-vs-manual toggles but the underlying physical
quantities and the full ISP surface — lens position / focus distance, exposure time
(sub-ms), ISO / analog+digital gain, aperture, white-balance temperature (Kelvin)
and per-channel color gains, crop/zoom region, min/max frame duration, plus the
tone pipeline (contrast, sharpness, saturation, noise reduction, edge enhancement,
tonemap/gamma, color-correction matrix, lens-shading and hot-pixel correction) and
3A modes with region metering, locks, and triggers. Where the platform allows,
settings apply **per-frame** (burst, exposure/focus bracketing), with honest
feedback on which frame a change took effect and on 3A convergence state. Each
control reports its supported range / step / availability per device, never a
silent clamp (MEL-ENGINE-VIII).

The domain is **bidirectional**. Egress: register an app-produced frame source as
an OS-visible **virtual camera** that other applications consume as an ordinary
camera input — the frames coming from anywhere the app has them (a GPU surface, a
composited scene, decoded media). This mirrors `audioin`'s publish model: where a
platform has a real publish path the virtual camera is OS-wide; where it does not,
visibility degrades honestly to local-only (MEL-ENGINE-VII), never silently. The
same enumeration that lists physical cameras also lists OS-exposed virtual cameras,
so a published camera is a first-class capture source on the ingest side.

OS integration is first-class throughout, not an afterthought: camera
**permission/consent** prompts and authorization status, device **hot-plug**
arrival/removal events, **arbitration** when multiple apps contend for one sensor,
sensor **orientation/rotation** relative to the device/UI, and **privacy-indicator**
(in-use LED) state are all part of the surface.

Synchronized A/V capture is in scope as a *timing* capability: camera owns a
shared capture clock / common time-base so video frames correlate with audio
frames, and — where the platform's capture session natively bundles audio with
video (AVFoundation, getUserMedia, Media Foundation) — sources that audio on the
shared session clock for the audio subsystem to consume. The microphone device,
consent, gain, DSP, and audio encoding are NOT camera's: they stay in
`audioin`/`audiocapture`/`audiopolicy`, composed with over the shared time-base.

## north star
The ceiling is set by **power apps**, not by what is easy. Whatever a best-in-class
app in this domain does — Halide, OBS, FiLMiC Pro, Zoom — the framework must afford,
and must let the user build something **competitive** with it (MEL-ENGINE-I/II). A
capability a power app relies on is in scope by definition; "hard" is not a reason
to omit it, only a reason it is worth doing.

## capability surface
All first-class:
- **devices & enumeration** — physical / virtual / external (UVC webcams, HDMI capture cards, Continuity Camera, Android external) / machine-vision (GenICam/USB3-Vision/GigE-Vision) / **bare image sensor (embedded/RTOS, sensor-direct via I²C register map + CSI-2/DVP, no OS HAL)**; **raw-UVC under-the-OS access** (reach UVC device controls the high-level API denies — PTZ / XU / still-pin / MS-XU calibration — via per-platform gateways); facing / position / mount / sensor characteristics; device↔mic association (a webcam's built-in mic); hot-plug; capabilities query per device; feature-combination feasibility (which format+feature sets co-operate — `isSessionConfigurationSupported` / Android feature-combination query, multi-cam format tuples, KSCameraProfile — per-control ranges alone are a silent-clamp trap).
- **topology & streams** — concurrent multi-*camera*; logical/physical lens constituents; simultaneous multi-stream (preview / record / still / analysis) each with its own format + use-case; synchronized multi-output (video + depth + metadata time-aligned); live reconfiguration without session teardown (continuity + cost reported).
- **fine-grained control** — focus / exposure / WB / ISO / analog+digital gain / aperture / zoom as physical quantities with per-device range+step; full ISP+tone pipeline; per-frame application; bracketing; locks / regions / triggers; 3A modes + convergence feedback; flash/torch strength + preflash metering; UVC/V4L2 control catalog (backlight compensation, anti-flicker / power-line-frequency, privacy control); zoom ramping with rate + lens switch-over zoom factors on logical cameras.
- **mechanical controls** — pan / tilt / zoom (PTZ) on conference / UVC cameras, distinct from digital zoom/crop.
- **capture modes** — RAW/ProRAW; sensor readout modes (pixel binning, crop region, full-res vs binned, quad-Bayer remosaic); zero-shutter-lag / reprocessing; exposure+focus bracketing; still-during-video; HDR video (10-bit, HLG / Dolby Vision, log profiles, wide gamut); high-speed slow-mo; time-lapse; vendor comp-photo modes (HDR/Night/Bokeh/Portrait); platform-encoded stills (JPEG / HEIC / ProRAW DNG) with ISP-embedded metadata (EXIF / GPS / orientation).
- **depth / 3D / calibration** — depth maps; segmentation / portrait mattes; stereo / spatial video; LiDAR / point cloud; camera intrinsics + lens distortion + extrinsics; infrared / NIR / mono streams (TrueDepth IR, Windows Hello IR, UVC/V4L2 grey formats).
- **live effects** — auto-framing / Center Stage; portrait / studio-light / eye-contact / reactions; subject-tracking ROI.
- **frame memory** — pixel formats (YUV / RGB / Bayer / 10-12-16-bit, planar/packed); zero-copy GPU import (IOSurface/CVPixelBuffer · AHardwareBuffer · dmabuf · WebGPU VideoFrame); buffer pooling + back-pressure + queue depth; CPU map + stride/plane layout; frame-drop reporting with reasons.
- **timing** — per-frame sensor timestamps + clock domain; rolling-shutter skew; exposure-applied frame; frame# / sequence; multi-cam genlock; shared A/V capture clock; camera↔IMU clock correlation (timestamps mappable to the platform motion-sensor clock; per-frame gyro/OIS sample metadata where the stack emits it); SMPTE timecode from capture devices.
- **metadata** — 3A state / regions / results; face rects + landmarks; scene / flicker detection; histograms + statistics maps (lens-shading / tonemap / hot-pixel); platform barcode/QR metadata.
- **egress** — publish any app surface as an OS virtual camera; honest local-only fallback; consumer-side surface: attach/detach events, per-consumer format negotiation, controls exposed on the published camera (CMIO / KSProperty), in-use feedback; published cam↔mic association (mirrors ingest device↔mic).
- **test ingest** — local synthetic camera provider (same machinery as egress, consumed in-process).
- **OS integration** — permission / consent; consent declaration + packaging (usage strings, manifest features, appx capability; macOS publish ships as a packaged+notarized System Extension); arbitration (exclusive vs shared) + preemption; background / interruption lifecycle; orientation / rotation / front-camera mirroring; privacy-indicator; shutter-sound restriction (regional mandates); hardware capture inputs (shutter / volume button, Apple Camera Control); thermal / power-aware degradation (MEL-ENGINE-VI).

## composes with
Camera is not an island (MEL-ENGINE-IX). External seams — all **exist** unless marked:
- **`image`** — the CPU frame container camera fills and barcode / GPU-upload / encode read; planar YUV / RGB / Bayer / HDR formats as open descriptors; `mel_image_wrap` borrows a frame's per-plane memory zero-alloc. Frame format / plane / stride lives here, never re-invented.
- **`gpu`** — zero-copy GPU import target (IOSurface/CVPixelBuffer · AHardwareBuffer · dmabuf · WebGPU VideoFrame → texture); the GPU-resident frame path.
- **`color`** — color spaces + science for WB / color-correction / gamut (P3, Rec.2020) / tonemap.
- **`sensor`** — shared time-base + the per-frame motion metadata the camera stack emits (gyro/OIS). IMU *device* stays sensors'; camera only correlates.
- **`audioin` / `audiocapture` / `audiopolicy` / `pcm`** — A/V sync, bundled-session audio sourcing, interruptions/focus. Mic identity / DSP / consent / gain stay theirs.
- **`barcode`** (+ future CV/ML) — software decode of platform-emitted detection metadata, downstream.
- **`event` / `executor` / `future` / `channel` / `vat`** — control + async + event backbone (same backbone audio uses).
- **`time` · `power`/`thermal` · `collection`/`allocator`/`core`/`math`/`string`/`quark`/`thread`/`platform`** — capture clock, degradation signals, foundation.
- provider/virtual-device **pattern** reused (not a new module): mirrors `audioin`/`audioout`/`sensor` — host OS = provider 0, virtuals register via a provider face, published/virtual cams + synthetic test source on a built-in virtual provider, hot-plug on an executor.
- **SPAWN GAP — media/video encode + mux** *(no module; `design/media-video.md` only)*. A **separate downstream domain** that consumes camera's synced A/V frames; it conforms to this module's frame output, not the reverse — so it is scoped *against* camera, never before it (scoping it first risks an incompatible frame contract). Out of this design; the recording loop closes only once that domain is defined against this one.

## gating axes            # matrix columns
| axis | api gen pinned |
|------|----------------|
| macos+avfoundation | capture: AVFoundation/AVCaptureSession, macOS 14 · publish: CoreMediaIO CMIOExtension (System Extension) |
| ios+avfoundation | capture: AVFoundation + TrueDepth/LiDAR, iOS 17 · publish: none (no public virtual-camera API) |
| android+camera2ndk | capture: NDK ACameraManager/ACameraDevice (Camera2), API 34 · publish: VirtualDeviceManager virtual camera (API 34, limited) · DeviceAsWebcam UVC gadget (Android 14; system-image service — likely deny, pinned so the matrix asks) |
| android+camerax | capture: CameraX 1.3 + Camera2/CameraX Extensions (Java/JNI) |
| linux+v4l2 | capture: V4L2 uAPI (videodev2.h) + UVC · publish: v4l2loopback / PipeWire virtual node |
| linux+libcamera | capture: libcamera 0.5 (≥0.4 for FrameWallClock + control mode-split — our A/V clock-correlation cap) · publish: none |
| linux+pipewire | capture: PipeWire camera nodes via xdg-desktop-portal (libcamera-backed) — the sole Linux consent path (sandboxed/Wayland-era) · publish: PipeWire virtual node |
| win32+mediafoundation | capture: MF IMFSourceReader/IMFMediaSource **+ WinRT MediaCapture** — parallel surfaces, disjoint subsets (D3D zero-copy + IMFVirtualCamera are Win32-MF; AdvancedPhotoCapture + RegionsOfInterest are WinRT), Win10+ (DirectShow enumeration alongside MF for legacy virtual cams) · publish: MFCreateVirtualCamera (Win11 22000+) / DirectShow filter |
| web+getusermedia | capture: getUserMedia + MediaStreamTrack + ImageCapture + WebCodecs VideoFrame · publish: none (canvas.captureStream in-page only) |
| uvc-direct | UVC 1.5 device's own surface reached UNDER the OS — macOS IOKit/IOUSBHost · win32 IKsControl/KsProperty · linux UVCIOC_CTRL_QUERY/libusb (deny: ios/web sandbox). Device-class axis: UVC-class devices only (external webcams, capture dongles). Holds PTZ/XU/still-pin/MS-XU caps the high-level API denies. |
| genicam | GenICam SFNC 2.7 + USB3-Vision + GigE-Vision (machine-vision). Device-class axis via the provider plane: industrial cameras (hardware trigger, multi-ROI, sequencer, chunk-data, action-commands). |
| embedded+baresensor | Bare image sensor driven directly — RTOS/bare-metal, **no OS camera HAL: Melody IS the driver**. Control via I²C/SPI register map (SCCB); pixel data via MIPI CSI-2 (or parallel DVP) into a receiver/DMA; on-SoC ISP **optional**, absent = the pinned floor (demosaic/3A/tonemap are downstream or app-built on manual registers, not native). Holds sensor-direct caps every OS HAL hides. **Upstream dependency: presupposes a Melody `embedded`/`rtos` platform target — not this module's to add.** |

## out of scope
- video/still **encoding** + container muxing — owned by media/video domain. Carve-back: stills the platform camera stack itself encodes (JPEG / HEIC / ProRAW DNG, metadata embedded) are camera deliverables; software (re-)encode stays media's.
- **IMU / motion-sensor device** — sensors domain; camera owns only the shared time-base and the per-frame motion metadata the camera stack itself emits (gyro/OIS samples). Same seam shape as audio.
- **tethered** still cameras over USB-PTP/MTP (gPhoto2-class DSLR tethering) — separate protocol family, same reason as RTSP; UVC-class external cameras stay in scope.
- frame **display / preview compositing** — GPU surface; camera delivers, GPU draws.
- **mic device identity, input consent, audio gain, audio DSP/format-conversion, audio encoding** — owned by `audioin`/`audiocapture`/`audiopolicy`. Camera composes with these over a shared capture clock; it never enumerates or processes audio. (Synchronized A/V *timing* and bundled-session audio *sourcing* are in scope — see domain def.)
- on-device **ML / computer-vision inference and decoding** — camera emits frames + the metadata the platform *itself* produces (3A, face rects, barcode payloads, scene hints); turning frames into higher-level results composes downstream — software barcode decode → `barcode`, pose/segmentation/object detection → CV/ML modules — same shape as the audio seam. Camera never bundles an inference engine.
- **out-of-band camera-control protocols over network/serial transports** — VISCA / VISCA-over-IP / NDI-PTZ / ONVIF-PTZ (driving a camera whose *video* arrives by a separate path). Out-of-scope **as transports, not as capabilities**: the PTZ / exposure / focus capabilities they carry are already in scope (UVC `CT_PANTILT_*` et al.) and their capability IDs are **transport-neutral** — the control-transport binding is a provider/device detail, never part of the cap ID. Addable later as a new control-transport binding with **zero module rewrite**, because control-reachability is *already* decoupled from frame-delivery for three in-scope axes — uvc-direct reaches controls under-the-OS while frames flow via AVFoundation/MF/V4L2; SDI capture cards ingest video with no in-band source control; GenICam splits GVCP control from GVSP data. That **control⊥data-transport decoupling is a load-bearing invariant the matrix (P4) and plane cut (P5) must preserve.** The cameras' *network video* (NDI/RTSP/ONVIF streams) stays excluded under the network-frame-source rule below, re-publishable via egress.
- **acquiring** frames from IP/network cameras (RTSP/ONVIF), screen-capture, or decoded media — those frame *sources* belong to media/display domains. (Such frames CAN be re-published as a virtual camera via the egress path — publishing is in scope; producing the frames is not.)

## reference apps (P6)
- **OBS Studio** — bidirectional stress test: enumerate + capture many cameras/capture-cards at once, hot-plug, and **publish a composited surface as an OS virtual camera** other apps consume; A/V together. Exercises egress + multi-source + OS integration in one app.
- pro stills (Halide/Obscura) — RAW, full manual 3A, focus peaking, lens/sensor switching, high-res still while previewing.
- video conferencing (Zoom/Meet) — hot-swap devices, depth/segmentation background, simultaneous capture, continuous reconfigure; *consumes* virtual cameras (incl. OBS's) — tests ingest of published cams.
- multi-cam director (FiLMiC Pro/DoubleTake) — simultaneous front+back, multi-stream, log color, high bitrate, manual everything.
- computational night/portrait — exposure bracketing, multi-frame, vendor extension modes (HDR/Night/Bokeh).
- AR/pose/fitness — low-latency high-framerate preview, continuous AF, depth stream, zero-copy GPU import.
- embedded/bare-sensor (DIY smart-glasses shot-on-button · machine-vision inspection) — sensor-direct register control, host-driven bring-up, GPIO/hardware-line-triggered still, raw Bayer, no OS, ships only cameradevice+cameracapture+cameracontrol+cameraphoto. Stresses the `embedded+baresensor` axis + the ship-granular split.

## changelog
- (initial)
- android axis = camera2-NDK + CameraX (2 columns); linux = v4l2 + libcamera (2 columns); vendor comp-photo extension modes in scope as capabilities.
- audio: synchronized A/V capture (shared clock + bundled-session audio sourcing) pulled into scope; mic identity/DSP/consent/gain/encode remain audioin/audiocapture/audiopolicy.
- bidirectional: virtual-camera **publish/egress** (register app surfaces as OS cameras; honest local-only where no publish path) pulled into scope, mirroring audioin publish. OBS added as reference app; axis pins extended with publish APIs.
- north star added: ceiling = whatever a power app needs, competitive with it. Full capability surface enumerated first-class — topology/multi-camera, depth+mattes+stereo+calibration, advanced capture modes, live effects, external/UVC/capture-cards, frame-memory/zero-copy/pooling, recording timing metadata, detection metadata, synthetic test ingest, thermal/power lifecycle. Detection metadata = compose (camera emits platform metadata; barcode/CV/ML decode downstream).
- gap review: platform-encoded stills + embedded metadata carved back into scope (software re-encode stays media's); camera↔IMU clock correlation + per-frame motion metadata in scope (IMU device stays sensors'); tethered PTP/MTP cameras ruled out of scope; axes — linux+pipewire portal column added (sole Linux consent path), win32 ingest notes DirectShow-only virtual cams, android egress pins DeviceAsWebcam; surface — feature-combination feasibility, egress consumer-side surface + published cam↔mic association, IR/NIR/mono streams, frame-drop reasons, live reconfiguration without teardown, zoom ramping + switch-over factors, mirroring, shutter-sound restriction, SMPTE timecode, consent declaration/packaging.
- composition surface mapped: substrate exists (`image` frame container, `gpu` import, `color`, `sensor` time-base, audio stack, `barcode`, event/vat backbone); provider/virtual pattern reused from audioin/audioout/sensor; single spawn gap = media/video encode+mux (downstream, not a prerequisite for camera capture+publish design).
- scope sealed: surface confirmed complete; media/encode confirmed a separate downstream domain scoped *against* camera, never before it — camera is the foundation the recording domain conforms to.
- P1 inventory complete (7 files, ~1450 entries). Pin corrections forced by inventory: libcamera 0.3→0.5 (FrameWallClock/mode-split land post-0.3); win32 spans MF + WinRT MediaCapture (disjoint subsets). No new capability area — 14-area surface holds.
- P2 beyond-OS complete (`20-beyond-os.md`): UVC, pro capture cards/SDI, sensor/MIPI, machine-vision, OBS, pro-mobile-capture. Gate decisions (all maximally ambitious): (1) raw-UVC under-the-OS = IN scope → new `uvc-direct` axis + a P5 raw-device-access plane (PTZ/XU/still-pin/calibration flip deny→emulate on macos/win32/linux); (2) machine-vision GenICam = FIRST-CLASS axis → new `genicam` column, inventory pending; (3) sensor-internal HAL caps = KEEP as vocab IDs (deny-consumer / deeper-embedded-Linux). Axes now 11. Cross-cutting: feasibility gains a thermal/power cost dimension; shared-capture-clock is a substantial data-plane cap (OBS machinery); RAW splits sensor-Bayer vs fused-RAW; comp-photo modes split explicit/implicit/property-controllable; SDI embedded audio = a 2nd bundled-A/V case.
- P2 scope test: VISCA / VISCA-IP / NDI-PTZ / ONVIF-PTZ ruled out-of-scope **as control transports** (the capabilities they carry are in-scope + transport-neutral); additive later with zero rewrite ∵ control⊥data-transport decoupling is already mandatory for uvc-direct / capture-card / genicam — named a load-bearing P4/P5 invariant. Carried into P3 vocabulary: raw ToF/structured-light/LiDAR correlation frames as a `deny`-consumer/deeper-embedded cap; live-effects (Center Stage / Studio Effects / Reactions) share comp-photo's 3-way classification. P2 sealed → P3.
- P3 vocabulary frozen-for-review (`30-vocabulary.md`): 579 axis-neutral capability IDs across the 12 areas, each traced to verbatim native entries; ~30 whole-ID `[CEILING]` caps kept per MEL-ENGINE-I; 21 cross-area dedups + 16 `?` recorded (§14). Granularity (control=125, meta=64, capture=66) is the load-bearing bet — fineness so every cell classifies to one status. Approved.
- P4 matrix built (`40-matrix.csv` + `40-matrix-notes.md`): 6 369 cells = 579 caps × 11 axis-columns. Distribution native 1785 / emulate 598 / deny 3929 / `?` 57. Load-bearing findings: CameraX is a systematic emulate column (179 emulate vs 265 native on camera2-NDK — vindicates the 2-column split); raw-UVC inverts macOS>iOS for external-UVC control; av-sync + app-timebase classify `emulate` on all 11 axes (framework data-plane, never deny); CEILING caps are clean genicam-only monopolies. Approved; design unlocked.
- P5 plane cut (`50-planes.md`): planes split further by ship-granularity (a module = a shippable static lib; an unused cluster must not link — MEL-ENGINE-III/VI). 13 modules at the ship-granular maximum, 2 core + 11 opt-in: cameradevice (72) · cameracapture (86) [core] · cameracontrol (125) · cameraptz (20) · cameraphoto (66) · cameracalib (12) · cameradepth (27) · camerameta (41) · cameradetect (14) · camerastats (9) · cameraeffects (31) · camerapolicy (49) · cameravirtual (27). Every cap-prefix assigned wholesale (579, zero deferred). Split so a 2D/preview/non-PTZ/non-detect app links none of the depth/photo/ptz/detect/stats/effects/calib code; split stops short of 2-cap micro-libs. Features assemble across modules (MEL-ENGINE-IX). Small-API invariant: caps are runtime-enumerable data behind query-caps. At gate.
- P5 gate reopened P0/P1/P3/P4 (sanctioned — reopening = process working): embedded RTOS + bare-sensor pulled IN SCOPE. New axis `embedded+baresensor` (sensor-direct: I²C/SPI registers + CSI-2/DVP DMA, no OS HAL, ISP-optional). Additive, no rewrite — the provider plane (host=provider 0 → on bare-metal, provider 0 = the sensor driver) + caps-as-data + the ship-granular split (embedded links only cameradevice+cameracapture+cameracontrol+cameraphoto) already accommodate it; the split is what makes it viable on an MCU. P1 inventory `10-inventory-baresensor.md` added (sensor-class split: SoC-ISP sensors do RGB/YUV/JPEG + on-chip 3A; raw-Bayer sensors emit mosaic, host does the rest). Vocab +3 caps (`cap.device.access.sensor_register`, `cap.device.bringup`, `cap.control.flash.strobe-output`; 579→582) + 1 broadened (`cap.stream.channel.transport.config` now spans CSI-2/DVP link); matrix +1 column (582 caps × 12 axes). Two findings rejected as downstream (inference-stays-downstream seam): software-3A (auto-3A deny→fallback-manual; app/3A-module closes the loop) and no-ISP demosaic (camera emits raw Bayer; `image`/`gpu` debayers). Classification shape: sensor-direct caps (manual exposure/gain/readout/binning, Bayer formats, hardware-line/GPIO trigger, embedded metadata lines, frame-sync pins, register access) flip `native`; OS-integration / effects / comp-photo / auto-3A / ISP-tone / egress flip `deny` (fallback to manual sensor caps or none — demosaic/3A/tonemap are downstream, not camera's software burden). Upstream blocker: real builds need a Melody embedded platform (outside camera's scope).
- P6 stress complete (7 `60-stress-*.md`): OBS · Halide · Zoom · FiLMiC · night/portrait · AR · embedded-glasses, each feature→cap-IDs→module + gaps. Breaks caught & resolved: **+7 caps (582→589)** — the standout `cap.device.class.software` closes the egress↔ingest loop Zoom exposed (`cap.device.class.virtual` was multi-lens fusion, not a published cam); plus `cap.enum.include_hidden`, `cap.stream.source.format_change_event`, `cap.meta.hdr.static`, `cap.control.{focus,exposure}.ramp-with-rate`, `cap.topology.multicam.control-independence`. **2 reclassifications** (deny→emulate, framework-provides): `cap.meta.bracketcorrelation` + `cap.stream.sync.timealigned`. **2 cut moves**: `cap.os.session.*`→cameracapture (capture lifecycle, not policy), `cap.meta.ois.samples`→cameracapture (AR motion data); embedded ship-profile note corrected (overclaimed "4 modules only"). Boundaries held (encode/fusion/SLAM stop at synced frames+intrinsics+IMU-ts); Halide clean. Matrix → 589×12 = 7068 cells; cross-artifact integrity verified (589 = 589 = 589, zero orphans). Known cosmetic: matrix note column mixes keyed + inline notes across fragments (every cell documented; cleanup deferred).
- P7 freeze **v1** (`70-freeze.md`): 589 caps × 12 axes × 13 modules sealed. Append-only (new cap = new ID, never rename/reuse; cap #590 = +1 row, not an ABI break); caps are runtime-enumerable data behind a fixed open/configure/start/stop/query-caps API; descriptors grow by trailing fields + size/version head. Handoff: 13 modules → 13 `wireframe` trios, smallest-dep first (cameradevice → cameracapture → control/ptz/photo/calib/policy → depth/meta/effects → detect/stats → cameravirtual); each module's frozen cap-set = its scope. 71 `?` cells = research-scheduled, resolved at wireframe-time, non-blocking. Design phase complete.
