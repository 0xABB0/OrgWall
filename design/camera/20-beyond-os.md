# camera — beyond the OS

What the hardware + pro apps reach that the 7 OS inventories hide or half-expose.
Tags: `[in]` consumer-reachable in scope · `[down]` downstream/out-of-camera-scope ·
`[ind]` industrial source-class (future provider, not a consumer axis) · `[trap]` half-exposure pitfall.
The recurring lesson (MEL-ENGINE-I/VII): a high-level OS API denying a capability ≠ the platform cannot do it.

## hw standards

### UVC (USB Video Class 1.5)
The under-OS primitive is the class control transfer `(unitID, selector, request)`; three host gateways:
Linux `UVCIOC_CTRL_QUERY`/libusb · Windows `IKsControl::KsProperty`/`DeviceIoControl` · macOS IOKit/IOUSBHost
(matched to `AVCaptureDevice.uniqueID`). **iOS = no public raw-USB path (App Store sandbox) — the real floor.**
- `[in]` **Extension Units (XU)** — 16-byte-GUID vendor controls (sensor regs, IR/depth modes, firmware). AVFoundation absent · MF needs a vendor DLL · V4L2 only pre-mapped bytes · reach raw via the 3 gateways — USB-IF UVC 1.5 §3.7.2.6; kernel uvcvideo; MS `PROPSETID_VIDCAP_EXTENSION_UNIT`.
- `[in]` **Camera-Terminal controls** — PTZ (`CT_PANTILT_ABSOLUTE/RELATIVE` 0x0d/0e, `CT_ROLL_*` 0x0f/10, `CT_ZOOM_*` 0x0b/0c), `CT_PRIVACY` 0x11, AE mode/priority + abs/rel exposure, focus auto/abs/rel/`CT_FOCUS_SIMPLE` 0x12, iris, `CT_REGION_OF_INTEREST` 0x14. **PTZ etc. ABSENT from AVFoundation but reachable via IOKit raw UVC ⇒ macOS PTZ is `emulate(raw-UVC)`, not `deny`.** getUserMedia exposes only pan/tilt/zoom (permission-gated) — USB-IF UVC 1.5 §4.2.2.1; uvc-util.
- `[in]` **Processing-Unit controls** — WB by temperature OR R/B component, digital-multiplier, power-line-frequency, gamma, backlight-comp — absent from AVFoundation, reachable raw — USB-IF UVC 1.5 §4.2.2.3.
- `[in]` **`MS_CAMERA_CONTROL_XU`** GUID `{0F3F95DC-2632-4C4E-92C9-A04782F43BC8}` — published-GUID XU: focus/exposure/EV/WB, FACE_AUTHENTICATION (IR), **CAMERA_EXTRINSICS 0x07 + CAMERA_INTRINSICS 0x08 (calibration)**, IR_TORCH, DIGITALWINDOW (digital PTZ), VIDEO_HDR, FIELDOFVIEW2. Reachable as a raw XU on macOS/Linux even though only Windows maps it to `KSPROPERTY_CAMERACONTROL_EXTENDED_*` ⇒ IR/HDR/calibration achievable off-Windows *if firmware implements it* — MS `uvc-extensions-1-5` §2.2.2.
- `[in]` **On-camera H.264 / frame-based encoding** (UVC 1.5) + encoder controls (bitrate, force-keyframe, LTR, QP) — Windows MF native (`usbvideo.sys`); Linux uvcvideo does NOT decode H.264 payloads; macOS raw isoc. Receiving the camera's own bitstream = `[in]` (camera deliverable, like platform-encoded stills); host transcode = `[down]` — USB-IF Frame-Based Payload; MS `camera-encoder-h264-uvc-1-5`.
- `[in]` **Still-image pipeline (Methods 1/2/3)** — `VS_STILL_PROBE/COMMIT/TRIGGER`, still resolution ≠ video, hardware-button trigger via Status-Interrupt endpoint. Off-by-default on Windows (INF), limited on Linux mainline — USB-IF UVC 1.5 §4.3.1.2.
- `[in]` **Per-frame PTS/SCR metadata** (device clock + 1kHz SOF) — device→host clock correlation. Linux `V4L2_META_FMT_UVC`, Windows `MetadataId_UsbVideoHeader`, macOS raw-parse — USB-IF UVC 1.5 §2.4.3.3.
- multi-VS-interface (RGB+IR+depth from one device) via Probe/Commit; `GET_INFO` cap-bitmap + `GET_LEN` gate every control — USB-IF UVC 1.5 §4.3.1.1.
- `[?]` `CT_REGION_OF_INTEREST` `bmAutoControls` bit layout; H.264 frame-based descriptor fields — not read at bit level from the USB-IF PDF. H.265-over-UVC is vendor/XU, not base standard.

### Pro capture cards / SDI-HDMI (Blackmagic DeckLink · AJA · Magewell)
"Treat an SDI/HDMI signal as a camera." All ingest-side; OBS uses DeckLink cross-platform.
- `[in]` **Per-frame SMPTE timecode** (RP188/VITC/LTC via `IDeckLinkTimecode`) — no OS camera API exposes signal-embedded TC; confirms the charter timecode cap — Blackmagic DeckLink SDK; AJA NTV2; SMPTE ST 12M.
- `[in]` **Genlock / reference / tri-level sync** (`BMDDeckLinkHasReferenceInput`) — the real hardware multi-cam frame alignment; consumer APIs have no reference-clock concept. Truth: the card reads already-aligned signals; alignment comes from genlocking the *source cameras* to house ref — Blackmagic FAQ; SMPTE ST 274.
- `[in]` **Embedded multi-channel SDI audio, clock-locked to video, same frame callback** — a *second* "bundled A/V" case (alongside AVCaptureSession) ⇒ the audio seam must route this signal-embedded audio to `audiocapture` on the shared clock — DeckLink/AJA/Magewell SDKs.
- `[in]` **Input signal-format auto-detect + change events** (`bmdVideoInputEnableFormatDetection` → `VideoInputFormatChanged`) — a source whose resolution/fps/format changes *underneath you*; the OS model negotiates a fixed format up front (V4L2 `VIDIOC_QUERY_DV_TIMINGS` is partial) — DeckLink SDK.
- `[in]` **HDR static metadata on ingest** (Rec.2020 + PQ/HLG, mastering display, MaxCLL/MaxFALL via `IDeckLinkVideoFrameMetadataExtensions`); **10/12-bit 4:2:2 / RGB 4:4:4 up to 8K/12G-SDI**; **raw VANC/HANC ancillary** (CC 608/708, VPID, AFD, custom DID/SDID); **dual-link 3D stereo** — DeckLink SDK; CEA-861.3.
- `[down]` fill+key (output/playout, not ingest), frame encoding, ST 2110 IP ingest (separate transport class).

### Image sensor / MIPI CSI-2 (mostly HAL-internal: `deny` on consumer, deeper on embedded-Linux subdevs)
- `[trap]` **Apple ProRAW ≠ pure Bayer** on flagship 48MP main sensors — it's a post-ISP *fused* linear DNG (Smart-HDR/Deep-Fusion baked in); pure pre-demosaic Bayer survives mainly on secondary lenses ⇒ the RAW cap must split **sensor-Bayer vs fused-RAW** — Apple ProRAW docs.
- **PDAF raw phase/disparity maps** — OS gives only the AF *result* (`LENS_FOCUS_DISTANCE`/AF state); raw phase hidden — IMX586/Samsung HP2 datasheets.
- **Staggered/DOL-HDR readout** (per-exposure on CSI-2 virtual channels) — OS gives output 10-bit profiles (`DynamicRangeProfiles`), never the sensor readout config — IMX415; MIPI CSI-2 v3.0.
- **Dual-conversion-gain + on-sensor HDR/WDR combine** (PWL companding) — HAL-internal, no consumer access — OV50H/OX03C10.
- `[in-half]` **Quad-Bayer/Nona binning + remosaic** — Android surfaces `ULTRA_HIGH_RESOLUTION_SENSOR`/`SENSOR_PIXEL_MODE`/`SENSOR_INFO_BINNING_FACTOR`/`REMOSAIC_REPROCESSING` but you can't pick intermediate bin ratios or native non-Bayer CFA — Samsung Tetra²; Android.
- `[in-half]` **Sensor readout modes** (bin/skip/crop) — collapsed into the (size, fps) list; Linux media-controller subdevs reach deeper — IMX415 datasheet.
- `[in-half]` **Global-vs-rolling shutter + readout/line time** — only `SENSOR_ROLLING_SHUTTER_SKEW` crumb; no shutter-mode select — Sony Pregius.
- **Embedded sensor metadata lines** (exposure/gain/counter in CSI-2) — HAL consumes; surfaced raw only via Linux `V4L2_BUF_TYPE_META_CAPTURE` on embedded — TI E2E.
- **Hardware frame-sync / genlock pins** (XVS/XHS/XMASTER master-slave) — board-wiring, the real multi-sensor genlock; `deny` on mobile, possible on embedded-Linux — FRAMOS; IMX415.

### Machine-vision (GenICam/SFNC v2.7 · USB3-Vision · GigE-Vision) — `[ind]` industrial source-class
The EMVA SFNC feature ceiling — what a camera *can* expose by standard. A future provider-plane source, not one of the 9 consumer axes (MEL-ENGINE-I: never "never").
- `[in]` **software-trigger / single-shot** abstraction (the consumer-reachable slice of `TriggerSelector`/`TriggerMode`/`TriggerSource`); hardware-line trigger + delay/overlap/divider is `[ind]` — EMVA SFNC §Acquisition.
- `[in-half]` **multi-ROI readout** (`RegionSelector`/`RegionMode`) — Android has only single `SCALER_CROP_REGION`, no multi-ROI, usually post-sensor — SFNC Region.
- `[in]` **selectable chunk-data** (`ChunkSelector`/`ChunkEnable`, per-frame camera-emitted metadata) — parallels `CaptureResult` but selectable/extensible vs OS-fixed — SFNC Chunk Data.
- `[in-half]` **prescriptive binning/decimation** (`BinningHorizontal`/`DecimationVertical`) — Android `SENSOR_INFO_BINNING_FACTOR` is descriptive (read-only), not prescriptive — SFNC Image Format.
- `[in]` **precise acquisition-framerate clamp** (`AcquisitionFrameRate`) — Android `CONTROL_AE_TARGET_FPS_RANGE` is AE-coupled, less precise — SFNC Acquisition.
- `[in]` **user-sets** (`UserSetSave`/`UserSetLoad`/`UserSetDefault`, on-device config persistence) — no consumer OS persists configs on the device; framework-providable — SFNC User Set.
- `[ind]` **hardware sequencer** (per-frame setting tables at sensor rate); **action commands + PTP scheduled** (network multi-cam trigger within µs) — define the multi-cam/burst ceiling consumer OS cannot reach — SFNC §17; GigE Vision 2.0.

## demanding apps

### OBS Studio — the bidirectional reference impl (source-cited @ obsproject/obs-studio master)
- **capture paths**: mac AVFoundation + **`kCMIOHardwarePropertyAllowScreenCaptureDevices=1`** (CMIO low-level) to list virtual/screen cams AVFoundation hides; **win DirectShow (libdshowcapture), NOT MF** — broadest device coverage + virtualcam discoverability; linux V4L2 + PipeWire-portal — `mac-avcapture/OBSAVCapture.m`, `win-dshow/`, `linux-v4l2/`, `linux-pipewire/`.
- ★ **multi-source clock-sync — the common-time-base solved** (`libobs/obs-source.c`): per-source rebasing `timing_adjust = os_gettime_ns() - frame.timestamp`; trust device-ts if within `MAX_TS_VAR` (2s) else rebase; jitter buffer `get_closest_frame` (drop stale, 2ms slack); `handle_ts_jump` recovery (>2s → reset+flush); A/V `sync_offset` + `TS_SMOOTHING_THRESHOLD` (70ms); per-device buffered/unbuffered (`obs_source_set_async_unbuffered`, auto for "delayed devices"); decoupled timebase; `MAX_ASYNC_FRAMES 30`. **This is the data-plane spec for our shared-capture-clock cap.**
- **publish**: mac CMIOExtension via `OSSystemExtensionRequest` (signed sysext + user approval); win DirectShow filter + **shared-memory NV12 cross-process queue**; linux v4l2loopback. Each carries an **install ceiling** (admin COM reg / signed-notarized sysext / out-of-tree kernel module).
- **format/color**: wide YUV/RGB + 10-bit (P010/V210/210LE) + HDR (PQ/HLG) across all backends; range/space/matrix from signal/format; zero-copy IOSurface (mac) / **DMA-BUF + DRM modifiers + explicit-sync** (PipeWire) vs CPU (win/V4L2).
- **hot-plug**: linux udev netlink (`video4linux`), win `OnReactivate` auto-reconnect. **control**: linux full `VIDIOC_QUERYCTRL` walk → exposure/focus/WB/brightness UI.
- `[down]` compositing, encoding, RTMP/SRT/WHIP streaming, CC decode, Qt UI. `?` OBS master not yet on `MFCreateVirtualCamera` (still DShow); NDI ingest is a separate plugin.

### Pro mobile capture (Halide/Obscura · FiLMiC · night/portrait)
- ★ **capture-vs-compute line**: the load-bearing capture primitive is **per-frame full-bit-depth delivery incl. RAW streaming** — *no* mainstream pro app relies on camera-emitted statistics; histogram/waveform/zebra/peaking/false-color are all **app-computed `[down]`** from frames.
- `[in]` RAW still (Bayer vs ProRAW/fused), selectable format/bit-depth, RAW+sidecar, DNG — `AVCapturePhotoOutput.availableRawPhotoPixelFormatTypes`, `appleProRAWEnabled`.
- `[in]` full manual exposure(`setExposureModeCustom`)/focus(`setFocusModeLockedWithLensPosition`)/WB(gains)/lock/bias/priority. `[trap]` `photoQualityPrioritization` defaults `.balanced` and **silently overrides** manual ISO/shutter — must set `.speed` (MEL-CODE-007).
- `[in]` exposure + focus bracketing (`AVCapturePhotoBracketSettings`, RAW-capable); HDR merge / focus-stack fusion `[down]`.
- `[in]` color pipeline: wide gamut, 10-bit HDR, HLG, **Apple Log** (`activeColorSpace`); standard + high fps (120/240) via `activeVideoMin/MaxFrameDuration`.
- `[in]` **concurrent multi-camera with a thermal/power cost budget** — `AVCaptureMultiCamSession` gates on `hardwareCost`/`systemPressureCost` < 1.0 ⇒ feasibility has a **cost dimension**, not just format compatibility; multicam capped 1080p/stream, manual not independently per-cam.
- `[in]` depth + segmentation mattes synchronized (`AVCaptureDepthDataOutput` + `AVCaptureDataOutputSynchronizer`, `AVPortraitEffectsMatte`/`AVSemanticSegmentationMatte`); blur render `[down]`.
- `[in]` ZSL (`isZeroShutterLagEnabled`) + documented **SIS** multi-frame fusion opt-in; ZSL disabled under manual/bracket/flash.
- vendor comp-photo modes classify **three ways**: Android Extensions explicit-selectable · **Apple implicit-uncontrollable** (Night/Smart-HDR/Deep-Fusion not API-triggerable) · Windows Studio Effects property-controllable.
- `[down]` all visualization overlays, fusion, log-curve remap, codec/bitrate/ProRes, PiP compositing, portrait-blur render.

## ceiling deltas → matrix / vocabulary implications
- **New cross-cutting mechanism: raw-UVC under-the-OS access** — lifts PTZ / manual controls / still-pin / XU / calibration from `deny` → `emulate` on macos/win32/linux; `deny` only on ios/web. Reshapes many matrix cells; likely a "raw device access" concern in the plane cut (P5).
- **Feasibility is multi-shaped + cost-bearing** — query-without-open (Android API35) · declared combination tables (AVF/Camera2) · try-and-adjust (`libcamera validate()→Adjusted`) · silent-clamp (V4L2 `TRY_FMT`) · constraints+OverconstrainedError (web); plus a **thermal/power cost budget** (`hardwareCost`<1.0). One vocab cap, several classifications.
- **Shared-capture-clock is a substantial data-plane cap** (OBS machinery): per-source clock rebasing + jitter buffer + ts-jump recovery + sync-offset + per-device buffering.
- **RAW splits** sensor-Bayer vs fused-RAW. **comp-photo modes split** explicit / implicit-uncontrollable / property-controllable.
- **Egress is native only on macos/win32/linux**, `deny`→local-only on ios/android/web; every native path carries an **install/packaging ceiling** (signed-notarized sysext · admin COM reg · kernel module).
- **A second bundled-A/V case** (SDI embedded audio) joins AVCaptureSession — the audio seam handles both.

## scope decisions for the P2 gate
1. **Machine-vision / industrial (GenICam/USB3-Vision/GigE-Vision)** — record its caps as vocab IDs (`deny` on consumer axes, `native` on a future provider per the provider-plane pattern), but NOT a first-class axis now? (recommended: yes, vocab-only.)
2. **Sensor-level HAL-internal caps** (DCG, staggered-HDR config, PDAF raw, genlock pins) — keep as vocab IDs classified `deny` on consumer / deeper on embedded-Linux, vs prune as out-of-reach? (recommended: keep — they're the honest ceiling, MEL-ENGINE-I, and embedded-Linux reaches some.)
