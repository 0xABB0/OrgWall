# libcamera — inventory
> api gen: libcamera 0.3 (control/property baseline; 0.4+ and 0.5/0.6 additions are annotated inline)
> covers matrix columns: linux+libcamera
> capture-only stack for MIPI/CSI-2 + ISP sensors (Raspberry Pi VC4/PiSP, Intel IPU3/IPU6, RkISP1, …). Userspace camera framework over V4L2/media-controller; the C++ public API lives in `namespace libcamera`. There is no stable C API — symbols below are the C++ surface.
> version notes: the controls/properties baseline here is the 0.3 set. `ExposureTimeMode`/`AnalogueGainMode`/`AeFlickerDetected`/`FrameWallClock` are 0.4-era additions; `Hue`/`WdrMode`/`WdrStrength`/`WdrMaxBrightPixels`/`LensDewarpEnable`/`LensShadingCorrectionEnable` are ~0.5/0.6-era. Marked inline; never assume a 0.3 deployment exposes the post-0.3 ones.

## devices & enumeration
- `CameraManager::CameraManager()` — construct the manager; one instance owns enumeration + the libcamera event loop — source: include/libcamera/camera_manager.h (raspberrypi/libcamera main), https://libcamera.org/api-html/classlibcamera_1_1CameraManager.html
- `CameraManager::start()` — `int start()` — begin device enumeration and start the camera manager thread/event loop; must be called before `cameras()` — source: camera_manager.h
- `CameraManager::stop()` — `void stop()` — stop the manager and release all cameras — source: camera_manager.h
- `CameraManager::cameras()` — `std::vector<std::shared_ptr<Camera>> cameras() const` — list of all currently-enumerated cameras — source: camera_manager.h
- `CameraManager::get(id)` — `std::shared_ptr<Camera> get(std::string_view id)` — look up one camera by its stable id string — source: camera_manager.h
- `CameraManager::version()` — `static const std::string &version()` — library version string — source: camera_manager.h
- `CameraManager::cameraAdded` — `Signal<std::shared_ptr<Camera>>` — hot-plug: a camera became available — source: camera_manager.h
- `CameraManager::cameraRemoved` — `Signal<std::shared_ptr<Camera>>` — hot-plug: a camera was disconnected — source: camera_manager.h
- `Camera::id()` — `const std::string &id() const` — stable per-camera identifier — source: include/libcamera/camera.h, https://libcamera.org/api-html/classlibcamera_1_1Camera.html
- `Camera::acquire()` — `int acquire()` — take **exclusive** ownership of the camera; required before configure/start; fails if another process holds it (this is the arbitration mechanism) — source: camera.h
- `Camera::release()` — `int release()` — drop exclusive ownership — source: camera.h
- `Camera::properties()` — `const ControlList &properties() const` — the static `properties::` list for this camera (Model, Location, etc.) — source: camera.h
- `Camera::controls()` — `const ControlInfoMap &controls() const` — the dynamic controls this camera supports, with ranges (see fine-grained control) — source: camera.h
- `Camera::streams()` — `const std::set<Stream *> &streams() const` — streams owned by the camera — source: camera.h
- `Camera::disconnected` — `Signal<>` — emitted when this specific camera vanishes mid-session — source: camera.h
- `properties::Model` — string — sensor model name, human-readable ASCII — source: property_ids_core.yaml, https://libcamera.org/api-html/namespacelibcamera_1_1properties.html
- `properties::Location` — int32 enum — mounting location; values `CameraLocationFront` (0) / `CameraLocationBack` (1) / `CameraLocationExternal` (2). NOTE: libcamera has no `BUILTIN` value — built-in is expressed as Front/Back; only External marks pluggable — source: property_ids_core.yaml
- `properties::Rotation` — int32 — angular difference (degrees) between camera and scene reference systems (fixed mount rotation) — source: property_ids_core.yaml
- `properties::PixelArraySize` — Size — full readable pixel-array dimensions in pixels — source: property_ids_core.yaml
- `properties::PixelArrayActiveAreas` — Rectangle[] — valid (active) sub-regions of the readable pixel matrix usable for image acquisition — source: property_ids_core.yaml
- `properties::PixelArrayOpticalBlackRectangles` — Rectangle[] — optical-black regions (used for black-level calibration) — source: property_ids_core.yaml
- `properties::UnitCellSize` — Size — physical pixel unit-cell size in nanometres — source: property_ids_core.yaml
- `properties::ScalerCropMaximum` — Rectangle — maximum valid rectangle for the `controls::ScalerCrop` control (the digital-zoom bound) — source: property_ids_core.yaml
- `properties::SensorSensitivity` — float — relative sensitivity of the chosen sensor readout mode — source: property_ids_core.yaml
- `properties::SystemDevices` — int64[] — `dev_t` device numbers of the underlying kernel devices backing this camera — source: property_ids_core.yaml
- `properties::PipelineHandler` — string — name of the pipeline handler managing this camera (e.g. `rpi/vc4`, `rkisp1`, `uvcvideo`) — source: property_ids_core.yaml
- `properties::ColorFilterArrangement` — int32 enum (**draft**) — Bayer/CFA layout of top-left 2×2; values `RGGB`(0) `GRBG`(1) `GBRG`(2) `BGGR`(3) `RGB`(4, non-Bayer 3×16-bit) `MONO`(5, single channel) — source: property_ids_draft.yaml
- device↔mic association — **none**; libcamera is video-only and has no concept of an associated audio capture device. Any camera/mic pairing must be resolved by a separate axis (e.g. PipeWire node grouping or app-side device topology) — source: libcamera scope (no audio in `namespace libcamera`)

## topology & streams
- `enum class StreamRole { Raw, StillCapture, VideoRecording, Viewfinder }` — predefined intended-use hints that seed `generateConfiguration()`; not a hard contract — source: include/libcamera/stream.h, https://libcamera.org/guides/application-developer.html
- `Camera::generateConfiguration(roles)` — `std::unique_ptr<CameraConfiguration> generateConfiguration(Span<const StreamRole>)` — produce a starting `CameraConfiguration` (one `StreamConfiguration` per requested role) with the pipeline's ideal defaults; empty role list → empty config; returns nullptr if the role set is unsupportable — source: camera.h, application-developer guide
- `Camera::configure(config)` — `int configure(CameraConfiguration *config)` — apply a (validated) configuration; rejects anything not `Valid` — source: camera.h
- `CameraConfiguration` — ordered vector of `StreamConfiguration`; iterable; **multiple streams per camera** is the model (e.g. Viewfinder + StillCapture + Raw simultaneously, subject to validate()) — source: https://libcamera.org/api-html/classlibcamera_1_1CameraConfiguration.html
- `CameraConfiguration::validate()` — `virtual Status validate() = 0` — **THE feature-combination feasibility mechanism.** "Try" semantics: adjusts an impossible config to the nearest achievable one rather than rejecting; returns `Valid` / `Adjusted` / `Invalid`. This is how you discover at runtime whether a given (format × size × stream-count × colorspace × sensor-mode) combination is supported on this pipeline. Always call before `configure()`. — source: classlibcamera_1_1CameraConfiguration, application-developer guide
- `CameraConfiguration::Status::Valid` — requested config fully supported, unchanged — source: camera.h
- `CameraConfiguration::Status::Adjusted` — config was modified to fit platform constraints (caller must re-inspect what it got) — source: camera.h
- `CameraConfiguration::Status::Invalid` — config cannot be made workable even after adjustment — source: camera.h
- `CameraConfiguration::at(i)` / `operator[]` — `StreamConfiguration &at(unsigned int)` — access the i-th stream config — source: camera.h
- `CameraConfiguration::size()` / `empty()` — stream count / emptiness — source: camera.h
- `CameraConfiguration::addConfiguration(cfg)` — `void addConfiguration(const StreamConfiguration &)` — append an extra stream entry beyond the generated roles — source: camera.h
- `CameraConfiguration::orientation` — `Orientation` member — requested output orientation (see OS integration / Orientation enum) — source: camera.h
- `CameraConfiguration::sensorConfig` — `std::optional<SensorConfiguration>` member — optional explicit sensor-mode pinning (see capture modes) — source: camera.h
- `StreamConfiguration::pixelFormat` — `PixelFormat` — output pixel format for the stream — source: include/libcamera/stream.h, https://libcamera.org/api-html/structlibcamera_1_1StreamConfiguration.html
- `StreamConfiguration::size` — `Size` — output width×height — source: stream.h
- `StreamConfiguration::stride` — `unsigned int` — bytes between successive image rows; valid only after `validate()` — source: stream.h
- `StreamConfiguration::frameSize` — `unsigned int` — total bytes for one frame across all planes; valid only after `validate()` — source: stream.h
- `StreamConfiguration::bufferCount` — `unsigned int` — requested number of buffers to allocate for this stream (an in-flight-depth / latency knob; a patch series proposes removing it but it is present in 0.3/0.4) — source: stream.h
- `StreamConfiguration::colorSpace` — `std::optional<ColorSpace>` — requested colour space; adjusted by `validate()` if undeliverable — source: stream.h
- `StreamConfiguration::stream()` / `setStream()` — associated `Stream*` (set by pipeline during configure; apps read it) — source: stream.h
- `StreamConfiguration::formats()` — `const StreamFormats &` — advisory supported-format/size enumeration for this stream (deprecated-leaning; prefer validate()) — source: stream.h
- `StreamFormats::pixelformats()` — `std::vector<PixelFormat>` — advisory list of supported pixel formats for a stream — source: classlibcamera_1_1StreamFormats, stream.h
- `StreamFormats::sizes(fmt)` — `std::vector<Size>` — advisory discrete sizes for a format (computed from ranges; not all guaranteed) — source: stream.h
- `StreamFormats::range(fmt)` — `SizeRange` — advisory min/max (+step) size for a format; step 0 ⇒ range generated, not all sizes valid — source: stream.h. NOTE: all `StreamFormats` output is advisory; nothing is supported until `validate()` confirms — source: classlibcamera_1_1StreamFormats
- `SensorConfiguration` — explicit sensor-mode pinning struct, optionally attached to `CameraConfiguration::sensorConfig`; lets the app demand a specific sensor readout (bit depth / binning / crop) instead of letting the pipeline pick — source: include/libcamera/camera.h, https://libcamera.org/api-html/classlibcamera_1_1SensorConfiguration.html
- `SensorConfiguration::bitDepth` — `unsigned int` — requested sensor ADC bit depth (e.g. 8/10/12) — source: camera.h
- `SensorConfiguration::analogCrop` — `Rectangle` — sensor-level analog crop region (field-of-view / readout window) — source: camera.h
- `SensorConfiguration::binning` — `struct { unsigned int binX, binY; }` — horizontal/vertical binning factors — source: camera.h
- `SensorConfiguration::skipping` — `struct { unsigned int xOddInc, xEvenInc, yOddInc, yEvenInc; }` — pixel-skipping (decimation) increments — source: camera.h
- `SensorConfiguration::outputSize` — `Size` — sensor output size after binning/skipping/crop — source: camera.h
- `SensorConfiguration::isValid()` — `bool isValid() const` — sanity-check the requested sensor mode before configure — source: camera.h

## fine-grained control
> Surface is `namespace controls`; per-camera availability + ranges come from `Camera::controls()` → `ControlInfoMap` (`ControlInfo::min()/max()/def()/values()`). Listing the FULL 0.3 core namespace below, then mode-split (0.4), then later additions.
- `controls::AeEnable` — bool — master AEGC on/off. In 0.3 this is the single AE toggle; in 0.4+ it is a convenience that sets both `ExposureTimeMode` and `AnalogueGainMode` — source: control_ids_core.yaml, https://libcamera.org/api-html/namespacelibcamera_1_1controls.html
- `controls::AeState` — int32 enum — AEGC state report; `AeStateIdle` / `AeStateSearching` / `AeStateConverged` (replaces deprecated `AeLocked`) — source: control_ids_core.yaml
- `controls::AeMeteringMode` — int32 enum — `MeteringCentreWeighted` / `MeteringSpot` / `MeteringMatrix` / `MeteringCustom` — source: control_ids_core.yaml
- `controls::AeConstraintMode` — int32 enum — `ConstraintNormal` / `ConstraintHighlight` / `ConstraintShadows` / `ConstraintCustom` — source: control_ids_core.yaml
- `controls::AeExposureMode` — int32 enum — `ExposureNormal` / `ExposureShort` / `ExposureLong` / `ExposureCustom` (exposure/gain trade-off bias) — source: control_ids_core.yaml
- `controls::ExposureValue` — float — EV compensation parameter (stops), applied on top of AE — source: control_ids_core.yaml
- `controls::ExposureTime` — int32 — manual shutter time in microseconds; effective only when exposure is in manual mode — source: control_ids_core.yaml
- `controls::AnalogueGain` — float — manual analogue (sensor) gain; effective only when gain is in manual mode — source: control_ids_core.yaml
- `controls::AeFlickerMode` — int32 enum — `FlickerOff` / `FlickerManual` / `FlickerAuto` (mains-flicker avoidance) — source: control_ids_core.yaml
- `controls::AeFlickerPeriod` — int32 — manual flicker period in microseconds (for `FlickerManual`) — source: control_ids_core.yaml
- `controls::Brightness` — float — fixed brightness offset — source: control_ids_core.yaml
- `controls::Contrast` — float — fixed contrast multiplier — source: control_ids_core.yaml
- `controls::Lux` — float — estimated current scene illuminance (metadata report) — source: control_ids_core.yaml
- `controls::AwbEnable` — bool — auto white balance on/off — source: control_ids_core.yaml
- `controls::AwbMode` — int32 enum — `AwbAuto` / `AwbIncandescent` / `AwbTungsten` / `AwbFluorescent` / `AwbIndoor` / `AwbDaylight` / `AwbCloudy` / `AwbCustom` — source: control_ids_core.yaml
- `controls::AwbLocked` — bool — report AWB lock status (metadata) — source: control_ids_core.yaml
- `controls::ColourGains` — float[2] — manual red & blue WB gains; only applies when AWB disabled — source: control_ids_core.yaml
- `controls::ColourTemperature` — int32 — colour temperature in kelvin (report; writable in newer gens) — source: control_ids_core.yaml
- `controls::Saturation` — float — fixed saturation multiplier — source: control_ids_core.yaml
- `controls::SensorBlackLevels` — int32[] — reported sensor black levels used in processing (metadata) — source: control_ids_core.yaml
- `controls::Sharpness` — float — sharpening intensity — source: control_ids_core.yaml
- `controls::FocusFoM` — int32 — focus Figure-of-Merit; how in-focus the frame is (metadata; basis for app-side AF/quality) — source: control_ids_core.yaml
- `controls::ColourCorrectionMatrix` — float[9] — 3×3 camera-RGB→sRGB CCM — source: control_ids_core.yaml
- `controls::ScalerCrop` — Rectangle — crop rectangle fed to scaler = **digital zoom / pan** (bounded by `properties::ScalerCropMaximum`) — source: control_ids_core.yaml, https://libcamera.org/api-html/camera-model.html
- `controls::DigitalGain` — float — digital gain applied during ISP processing (metadata/report) — source: control_ids_core.yaml
- `controls::FrameDuration` — int64 — instantaneous frame duration (microseconds), exposure start to next (metadata) — source: control_ids_core.yaml
- `controls::FrameDurationLimits` — int64[2] — min/max frame duration in microseconds = **FPS clamp** (the framerate control) — source: control_ids_core.yaml
- `controls::SensorTemperature` — float — sensor die temperature in Celsius (metadata) — source: control_ids_core.yaml
- `controls::SensorTimestamp` — int64 — capture timestamp, nanoseconds, CLOCK_BOOTTIME (metadata-only) — source: control_ids_core.yaml (see timing)
- `controls::AfMode` — int32 enum — `AfModeManual` / `AfModeAuto` / `AfModeContinuous` — source: control_ids_core.yaml
- `controls::AfRange` — int32 enum — `AfRangeNormal` / `AfRangeMacro` / `AfRangeFull` — source: control_ids_core.yaml
- `controls::AfSpeed` — int32 enum — `AfSpeedNormal` / `AfSpeedFast` — source: control_ids_core.yaml
- `controls::AfMetering` — int32 enum — `AfMeteringAuto` / `AfMeteringWindows` — source: control_ids_core.yaml
- `controls::AfWindows` — Rectangle[] — AF metering windows (used when `AfMeteringWindows`) — source: control_ids_core.yaml
- `controls::AfTrigger` — int32 enum — `AfTriggerStart` / `AfTriggerCancel` (drives a one-shot AF scan in `AfModeAuto`) — source: control_ids_core.yaml
- `controls::AfPause` — int32 enum — `AfPauseImmediate` / `AfPauseDeferred` / `AfPauseResume` (pauses continuous AF) — source: control_ids_core.yaml
- `controls::LensPosition` — float — manual focus lens position, dioptres (also reported) — source: control_ids_core.yaml
- `controls::AfState` — int32 enum — `AfStateIdle` / `AfStateScanning` / `AfStateFocused` / `AfStateFailed` (metadata) — source: control_ids_core.yaml
- `controls::AfPauseState` — int32 enum — `AfPauseStateRunning` / `AfPauseStatePausing` / `AfPauseStatePaused` (metadata) — source: control_ids_core.yaml
- `controls::HdrMode` — int32 enum — `HdrModeOff` / `HdrModeMultiExposureUnmerged` / `HdrModeMultiExposure` / `HdrModeSingleExposure` / `HdrModeNight` — source: control_ids_core.yaml
- `controls::HdrChannel` — int32 enum — `HdrChannelNone` / `HdrChannelShort` / `HdrChannelMedium` / `HdrChannelLong` (which HDR sub-frame this is; metadata) — source: control_ids_core.yaml
- `controls::Gamma` — float — fixed gamma value — source: control_ids_core.yaml
- `controls::DebugMetadataEnable` — bool — enable extra debug metadata in returned ControlList — source: control_ids_core.yaml
- `controls::ExposureTimeMode` — int32 enum (**0.4+**) — `ExposureTimeModeAuto` / `ExposureTimeModeManual`; splits exposure auto/manual from gain — source: control_ids_core.yaml (added post-0.3)
- `controls::AnalogueGainMode` — int32 enum (**0.4+**) — `AnalogueGainModeAuto` / `AnalogueGainModeManual` — source: control_ids_core.yaml (added post-0.3)
- `controls::AeFlickerDetected` — int32 (**0.4+**) — auto-detected flicker period in microseconds (metadata) — source: control_ids_core.yaml (added post-0.3)
- `controls::FrameWallClock` — int64 (**0.4+**) — CLOCK_REALTIME wall-clock ns matching `SensorTimestamp` (metadata-only) — source: control_ids_core.yaml (added post-0.3); see timing
- `controls::Hue` — float (**~0.5/0.6**) — image hue rotation in degrees (e.g. RkISP1 cproc) — source: control_ids_core.yaml (added well after 0.3) `?` exact version
- `controls::WdrMode` — int32 enum (**~0.5/0.6**) — global tone-map WDR; `WdrOff` / `WdrLinear` / `WdrPower` / `WdrExponential` / `WdrHistogramEqualization` — source: control_ids_core.yaml (post-0.3) `?` exact version
- `controls::WdrStrength` — float (**~0.5/0.6**) — WDR strength — source: control_ids_core.yaml (post-0.3) `?`
- `controls::WdrMaxBrightPixels` — float (**~0.5/0.6**) — % of allowed saturated pixels under WDR — source: control_ids_core.yaml (post-0.3) `?`
- `controls::LensDewarpEnable` — bool (**~0.5/0.6**) — enable/disable lens dewarp — source: control_ids_core.yaml (post-0.3) `?`
- `controls::LensShadingCorrectionEnable` — bool (**~0.5/0.6**) — enable/disable lens-shading correction — source: control_ids_core.yaml (post-0.3) `?`
- `Camera::controls()` → `ControlInfoMap` — per-camera enumerated controls + `ControlInfo` ranges; this is how you discover which of the above an actual camera honours and their min/max/def/discrete-values — source: include/libcamera/controls.h, https://libcamera.org/api-html/classlibcamera_1_1ControlInfoMap.html
- `ControlInfo::min()/max()/def()/values()` — per-control range / default / discrete value set — source: controls.h
- `ControlId::id()/name()/type()` + `ControlType{None,Bool,Byte,Integer32,Integer64,Float,String,Rectangle,Size}` — control identity + value type taxonomy — source: controls.h

## mechanical controls
- PTZ — **none.** libcamera has no mechanical pan/tilt/zoom controls; there is no motorized-PTZ or UVC-PTU surface in `namespace controls`. "Zoom" exists only as digital `controls::ScalerCrop` (crop+scale), and "pan" only as moving that crop within `properties::ScalerCropMaximum`. Motorized lens movement is limited to focus (`controls::LensPosition`). Mechanical PTZ, if needed, is a separate axis (e.g. UVC XU / V4L2 PTZ ioctls outside libcamera) — source: control_ids_core.yaml (no PTZ controls present)

## capture modes
- Raw capture — `StreamRole::Raw` + a Bayer `formats::S*` stream yields unprocessed sensor data (Bayer/mono); the raw mosaic comes straight from the sensor — source: stream.h, formats.yaml
- HDR — `controls::HdrMode` (`Off`/`MultiExposureUnmerged`/`MultiExposure`/`SingleExposure`/`Night`) with `controls::HdrChannel` tagging sub-frames; this is libcamera's only in-stack multi-exposure mode — source: control_ids_core.yaml
- sensor pixel modes — `SensorConfiguration` selects binning / skipping / analog-crop / bit-depth (full-res vs binned readout) — source: camera.h
- exposure-bracketing / ZSL / computational-photo (HDR+ merge, night-mode stacking, portrait, pano) — **none at the libcamera core.** No bracketing control, no zero-shutter-lag buffer ring, no burst-merge primitive. These are app-level or IPA/vendor-level concerns; libcamera exposes only the per-request controls above and lets the app orchestrate bursts itself — source: control_ids_core.yaml (no such controls); `controls::draft::NoiseReductionModeZSL` is only an NR hint, not a ZSL pipeline
- `controls::draft::TestPatternMode` — int32 enum — sensor test pattern: `Off`/`SolidColor`/`ColorBars`/`ColorBarsFadeToGray`/`Pn9`/`Custom1` — source: control_ids_draft.yaml

## depth/3D/calibration
- depth / stereo / 3D — **none built-in.** libcamera is a 2D ISP capture stack; no disparity, no depth stream, no multi-camera stereo rig sync at the public API (RPi-vendor `controls::rpi::SyncMode/SyncReady/SyncTimer/SyncFrames` is frame-start sync, not stereo rectification). No structured-light / ToF abstraction — source: control_ids_core.yaml (no depth controls)
- IR / monochrome — via raw formats only: `formats::R8/R10/R12/R16` greyscale and `properties::ColorFilterArrangement == MONO`. No dedicated IR-illuminator control — source: formats.yaml, property_ids_draft.yaml
- calibration controls — **none exposed.** Lens/sensor calibration (CCM, lens-shading tables, black-level) lives in the IPA tuning files, not as runtime controls. `controls::ColourCorrectionMatrix` is settable but is a processing override, not a calibration API; `controls::draft::LensShadingMapMode` only reports map availability. No intrinsics/extrinsics/distortion-coefficient surface — source: control_ids_core.yaml, control_ids_draft.yaml

## live effects
- live effects — **none.** No beautify / background-blur / segmentation / AR / filter controls in `namespace controls`. The only image-shaping knobs are the tone/colour primitives (`Brightness`/`Contrast`/`Saturation`/`Sharpness`/`Gamma`/`ColourGains`/`ColourCorrectionMatrix`, plus post-0.3 `Hue`/`WdrMode`). An IPA could implement effects internally but there is no public effect-selection control — source: control_ids_core.yaml (no effect controls)

## frame memory
- `FrameBuffer` — a multi-plane DMA buffer; planes carry dmabuf fds for **zero-copy** sharing with GPU/encoder/display — source: include/libcamera/framebuffer.h, https://libcamera.org/api-html/classlibcamera_1_1FrameBuffer.html
- `FrameBuffer::Plane{ SharedFD fd; unsigned int offset; unsigned int length; }` — per-plane dmabuf fd + offset + size — source: framebuffer.h
- `FrameBuffer::planes()` — `Span<const Plane>` — the buffer's planes — source: framebuffer.h
- `FrameBuffer::cookie()` / `setCookie(uint64)` — opaque app tag carried with the buffer — source: framebuffer.h
- `FrameBuffer::metadata()` — `const FrameMetadata &` — completion metadata for the last capture into this buffer — source: framebuffer.h
- `FrameBuffer::request()` — owning `Request*` (when queued) — source: framebuffer.h
- `FrameBufferAllocator::FrameBufferAllocator(camera)` — convenience allocator bound to a camera (allocates from the pipeline's preferred memory) — source: include/libcamera/framebuffer_allocator.h, https://libcamera.org/api-html/classlibcamera_1_1FrameBufferAllocator.html
- `FrameBufferAllocator::allocate(stream)` — `int allocate(Stream *)` — allocate this stream's `bufferCount` buffers — source: framebuffer_allocator.h
- `FrameBufferAllocator::free(stream)` — `int free(Stream *)` — release a stream's buffers — source: framebuffer_allocator.h
- `FrameBufferAllocator::allocated()` — `bool allocated() const` — whether any buffers are held — source: framebuffer_allocator.h
- `FrameBufferAllocator::buffers(stream)` — `const std::vector<std::unique_ptr<FrameBuffer>> &` — the allocated buffers for a stream — source: framebuffer_allocator.h
- NOTE on import: apps may also supply their own externally-allocated dmabuf-backed `FrameBuffer`s (import path) instead of using `FrameBufferAllocator`, e.g. buffers from a GBM/EGL/V4L2 encoder — source: framebuffer.h (FrameBuffer ctor from planes)
- `Request` — unit of capture: bundles per-stream buffers + a control list; submitted via `Camera::queueRequest()`; buffers/requests are reused across frames (queue-and-recycle) — source: include/libcamera/request.h, https://libcamera.org/api-html/classlibcamera_1_1Request.html
- `Request::addBuffer(stream, buffer, fence)` — `int addBuffer(const Stream *, FrameBuffer *, std::unique_ptr<Fence> &&={})` — attach a buffer to a stream for this request, with optional sync `Fence` — source: request.h
- `Request::findBuffer(stream)` — `FrameBuffer *` — buffer attached for a stream — source: request.h
- `Request::buffers()` — `const BufferMap &` — stream→buffer map — source: request.h
- `Request::controls()` — `ControlList &` — per-request control settings (AE/AF/etc. applied to this frame) — source: request.h
- `Request::metadata()` — `const ControlList &` — per-request returned metadata (3A results etc.) — source: request.h
- `Request::sequence()` — `uint32_t` — request sequence number — source: request.h
- `Request::cookie()` — `uint64_t` — app cookie set at `createRequest(cookie)` — source: request.h
- `Request::status()` + `enum Status { RequestPending, RequestComplete, RequestCancelled }` — completion status — source: request.h
- `Request::reuse(flags)` + `enum ReuseFlag { Default, ReuseBuffers }` — recycle a request for resubmission, optionally keeping its buffers — source: request.h
- `Request::hasPendingBuffers()` / `toString()` — incomplete-buffer check / debug string — source: request.h
- `Camera::createRequest(cookie)` — `std::unique_ptr<Request> createRequest(uint64_t cookie = 0)` — allocate a request — source: camera.h
- `Camera::queueRequest(req)` — `int queueRequest(Request *)` — submit a request for capture — source: camera.h
- `Camera::requestCompleted` — `Signal<Request *>` — request finished (completion event; check `status()`/`RequestComplete` vs `RequestCancelled`) — source: camera.h
- `Camera::bufferCompleted` — `Signal<Request *, FrameBuffer *>` — a single buffer within a request completed (early per-buffer callback) — source: camera.h
- `PixelFormat` — fourcc-style format handle (+ optional modifier); `formats::` are the named constants — source: include/libcamera/pixel_format.h, formats.yaml
- `formats::` RGB — `R8 R10 R12 R16 RGB565 RGB565_BE RGB888 BGR888 XRGB8888 XBGR8888 RGBX8888 BGRX8888 ARGB8888 ABGR8888 RGBA8888 BGRA8888 RGB161616 BGR161616` — source: formats.yaml
- `formats::` YUV packed — `YUYV YVYU UYVY VYUY AVUY8888 XVUY8888` — source: formats.yaml
- `formats::` YUV semi-planar/planar — `NV12 NV21 NV16 NV61 NV24 NV42 YUV420 YVU420 YUV422 YVU422 YUV444 YVU444` — source: formats.yaml
- `formats::` greyscale/mono — `R8 R10 R12 R16` (greyscale) `MONO_PISP_COMP1` (PiSP-compressed mono) — source: formats.yaml
- `formats::` Bayer 8/10/12/14/16 — `SRGGB8 SGRBG8 SGBRG8 SBGGR8` / `SRGGB10 SGRBG10 SGBRG10 SBGGR10` / `SRGGB12 …` / `SRGGB14 …` / `SRGGB16 SGRBG16 SGBRG16 SBGGR16` — source: formats.yaml
- `formats::` Bayer CSI-2 packed — `R10_CSI2P R12_CSI2P` + `S{RGGB,GRBG,GBRG,BGGR}{10,12,14}_CSI2P` — source: formats.yaml
- `formats::` Bayer IPU3 packed — `SRGGB10_IPU3 SGRBG10_IPU3 SGBRG10_IPU3 SBGGR10_IPU3` — source: formats.yaml
- `formats::` PiSP-compressed Bayer — `RGGB_PISP_COMP1 GRBG_PISP_COMP1 GBRG_PISP_COMP1 BGGR_PISP_COMP1` — source: formats.yaml
- `formats::MJPEG` — compressed JPEG-per-frame (UVC webcams) — source: formats.yaml

## timing
- `controls::SensorTimestamp` — int64, nanoseconds, **CLOCK_BOOTTIME** (monotonic since boot; the canonical per-frame capture time; metadata-only; set by pipeline handlers from the V4L2 capture device) — source: control_ids_core.yaml, https://libcamera.org/api-html/structlibcamera_1_1FrameMetadata.html, patchwork.libcamera.org/patch/9414
- `FrameMetadata::timestamp` — uint64 ns — the buffer-level timestamp; same clock domain as `SensorTimestamp` (BOOTTIME). Caveat: ultimately sourced from V4L2, monotonic but susceptible to NTP slew, and V4L2 does not strictly guarantee the source clock — source: framebuffer.h, patchwork 9414
- `FrameMetadata::sequence` — unsigned int — monotonically increasing frame counter; **gaps indicate dropped frames** — source: framebuffer.h
- `controls::FrameWallClock` (**0.4+**) — int64 ns, **CLOCK_REALTIME** wall clock paired to `SensorTimestamp` via the `ClockRecovery` class (BOOTTIME↔REALTIME model); for cross-device sync (metadata-only) — source: control_ids_core.yaml, patchwork.libcamera.org/patch/22213
- SMPTE / genlock / LTC timecode — **none.** No SMPTE timecode, no hardware genlock; only the monotonic BOOTTIME stamp + optional REALTIME estimate — source: framebuffer.h (no timecode field)
- IMU correlation — no IMU API in libcamera; correlation is the app's job using `SensorTimestamp` in the CLOCK_BOOTTIME domain (match against `clock_gettime(CLOCK_BOOTTIME)` on the IMU side). NOTE the clock-domain pitfall: do not compare against CLOCK_MONOTONIC or REALTIME — source: patchwork 9414

## metadata
- per-request `Request::metadata()` → `ControlList` — 3A and stats results returned per frame: e.g. `controls::ExposureTime` `controls::AnalogueGain` `controls::DigitalGain` `controls::ColourGains` `controls::ColourTemperature` `controls::Lux` `controls::FocusFoM` `controls::SensorTimestamp` `controls::AeState` `controls::AfState` `controls::SensorBlackLevels` `controls::SensorTemperature` — whichever the pipeline populates — source: request.h, control_ids_core.yaml
- `FrameMetadata{ Status status; unsigned int sequence; uint64_t timestamp; Span<Plane> planes; }` — buffer-level completion record; `Status` = `FrameSuccess` / `FrameError` / `FrameCancelled` / `FrameStartup`; `Plane{ unsigned int bytesused; }` — source: framebuffer.h
- face / scene detection — **not in libcamera core.** Exists only as `controls::draft` and is rarely implemented (see obscure corners). No scene-classification, no smile/blink, no object detection in the stable API — source: control_ids_draft.yaml

## egress
- virtual-camera publish — **none. libcamera is capture-only.** There is no API to register/publish a virtual or loopback camera that other apps consume; nothing analogous to a sink/output device. Producing a shareable camera node (so other apps see "Melody Camera") requires a separate layer entirely — PipeWire (the libcamera→PipeWire bridge), v4l2loopback, or a custom node — none of which is part of `namespace libcamera` — source: libcamera scope (no publish/sink API in camera.h / camera_manager.h)

## OS integration
- permission model — **none of its own.** libcamera has no consent/permission concept; acquiring a device is a raw open. On sandboxed Linux desktops the permission gate sits *above* libcamera: xdg-desktop-portal + PipeWire (deny-by-default, per-app camera-role grants, FD handed to the client). For native (non-sandboxed) apps there may be no prompt at all. This consent layer is a SEPARATE axis from the libcamera capture surface — source: docs.pipewire.org/page_portal.html, collabora libcamera-into-pipewire blog
- device arbitration — `Camera::acquire()` / `release()` give **exclusive** access (one holder at a time); contention fails loudly. NOTE: going through PipeWire instead enables multiplexing (multiple apps share one camera); raw libcamera does not multiplex — source: camera.h, collabora/pipewire
- orientation — `CameraConfiguration::orientation` (`Orientation` enum, EXIF-tag-274 semantics): `Rotate0`(1) `Rotate0Mirror` `Rotate180` `Rotate180Mirror` `Rotate90Mirror` `Rotate270` `Rotate270Mirror` `Rotate90` — requested output orientation; plus static mount `properties::Rotation`. No live device-orientation/tilt sensor integration — source: include/libcamera/orientation.h
- privacy LED / shutter — **no hook.** libcamera has no privacy-indicator API; any privacy LED is hardware/firmware-driven and not surfaced — source: camera.h (no such control)
- thermal — read-only `controls::SensorTemperature` only; no thermal-throttle callback or policy hook (engine must self-govern per MEL-ENGINE-VI/III) — source: control_ids_core.yaml

## obscure corners
- `controls::draft::AePrecaptureTrigger` — int32 enum `Idle`/`Start`/`Cancel` — Android-style AE precapture metering trigger (rarely implemented) — source: control_ids_draft.yaml
- `controls::draft::NoiseReductionMode` — int32 enum `Off`/`Fast`/`HighQuality`/`Minimal`/`ZSL` — NR strength hint (the only place "ZSL" appears, and only as an NR hint) — source: control_ids_draft.yaml
- `controls::draft::ColorCorrectionAberrationMode` — int32 enum `Off`/`Fast`/`HighQuality` — chromatic-aberration correction hint — source: control_ids_draft.yaml
- `controls::draft::AwbState` — int32 enum `Inactive`/`Searching`/`Converged`/`Locked` — draft AWB state report — source: control_ids_draft.yaml
- `controls::draft::SensorRollingShutterSkew` — int64 ns — first-to-last-row exposure-start skew (rolling-shutter readout time) — source: control_ids_draft.yaml
- `controls::draft::LensShadingMapMode` — int32 enum `Off`/`On` — whether a lens-shading map is available — source: control_ids_draft.yaml
- `controls::draft::PipelineDepth` — int32 — number of pipeline stages a frame traversed — source: control_ids_draft.yaml
- `controls::draft::MaxLatency` — int32 — max frames after submission before result sync — source: control_ids_draft.yaml
- `controls::draft::TestPatternMode` — int32 enum (see capture modes) — source: control_ids_draft.yaml
- `controls::draft::FaceDetectMode` — int32 enum `Off`/`Simple`/`Full` — face-detect mode (rarely implemented) — source: control_ids_draft.yaml
- `controls::draft::FaceDetectFaceRectangles` — Rectangle[] — detected face boxes — source: control_ids_draft.yaml
- `controls::draft::FaceDetectFaceScores` — uint8[] — per-face confidence 0–100 — source: control_ids_draft.yaml
- `controls::draft::FaceDetectFaceLandmarks` — Point[] — face landmark coordinates — source: control_ids_draft.yaml
- `controls::draft::FaceDetectFaceIds` — int32[] — stable per-face IDs while visible — source: control_ids_draft.yaml
- `controls::rpi::StatsOutputEnable` — bool — emit raw ISP statistics in metadata (RPi vendor) — source: control_ids_rpi.yaml
- `controls::rpi::Bcm2835StatsOutput` — uint8[] — binary dump of BCM2835 ISP stats (RPi vendor) — source: control_ids_rpi.yaml
- `controls::rpi::PispStatsOutput` — uint8[] — PiSP front-end ISP stats blob (RPi vendor) — source: control_ids_rpi.yaml
- `controls::rpi::ScalerCrops` — Rectangle[] — per-output-stream crop rectangles, ordered to match configured streams (multi-stream digital zoom; RPi vendor) — source: control_ids_rpi.yaml
- `controls::rpi::SyncMode` — int32 enum Off/Server/Client — frame-start synchronisation across cameras (RPi vendor; NOT stereo rectification) — source: control_ids_rpi.yaml
- `controls::rpi::SyncReady` / `SyncTimer` / `SyncFrames` — sync handshake state / countdown ns / server lead-in frames (RPi vendor) — source: control_ids_rpi.yaml
- `controls::rpi::ControlListSequence` — int64 — sequence of the request whose controls were just applied (RPi vendor) — source: control_ids_rpi.yaml
- `controls::rpi::CnnOutputTensor` / `CnnOutputTensorInfo` / `CnnEnableInputTensor` / `CnnInputTensor` / `CnnInputTensorInfo` / `CnnKpiInfo` — on-sensor/IMX500 CNN inference tensors + KPI timings (RPi vendor; AI-camera path) — source: control_ids_rpi.yaml
- `?` exact 0.3-vs-later version boundary for `Hue`/`Wdr*`/`LensDewarp*`/`LensShadingCorrectionEnable`/`AeFlickerDetected` — present in current upstream `control_ids_core.yaml`, added after the 0.3 tag (0.4 for mode-split/FrameWallClock/AeFlickerDetected; ~0.5/0.6 for Hue/Wdr/dewarp/lens-shading). Verify against the exact `v0.3.x` tag before relying on them in a 0.3 deployment — source: control_ids_core.yaml history, phoronix libcamera-0.4, patchwork RkISP1 Hue (Nov 2025)
- `?` `properties::draft` beyond `ColorFilterArrangement` — could not confirm additional draft properties (e.g. SensorPhysicalSize / LensFacing) exist in 0.3; only `ColorFilterArrangement` was confirmed in `property_ids_draft.yaml` — source: property_ids_draft.yaml (only one entry surfaced)
