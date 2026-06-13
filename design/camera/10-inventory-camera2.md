# camera2 — inventory
> api gen: NDK ACamera (Camera2) API 34 + CameraX 1.3 + Camera2/CameraX Extensions
> covers matrix columns: android+camera2ndk, android+camerax

> Naming note: NDK tags are `ACAMERA_<SECTION>_<KEY>` (e.g. `ACAMERA_CONTROL_AE_MODE`); the Camera2 Java API exposes the identical catalog as `CaptureRequest.Key`/`CaptureResult.Key`/`CameraCharacteristics.Key` constants with the `ACAMERA_` prefix dropped (`CONTROL_AE_MODE`). They are the SAME key. Enum value names follow `ACAMERA_<TAG>_<VALUE>` / `CameraMetadata.<TAG>_<VALUE>`. NDK keys are read/written via `ACameraMetadata_getConstEntry` / `ACaptureRequest_setEntry_*`; the tag set is API-level-gated (NDK first added API 24, individual tags later).
> Verification: NDK `/ndk/reference/group/*` and `/sdk/api_diff/*` render server-side (used directly). Java/Kotlin `/reference/` pages render client-side — verbatim spelling taken from AOSP `frameworks/base` / `frameworks/av` source via `?format=TEXT`. API-level badges absent from AOSP source are mapped from release history and marked `?` where uncertain. NEVER a fabricated key.

## devices & enumeration
- `ACameraManager` — opaque NDK camera-manager handle — source: NDK Camera, developer.android.com/ndk/reference/group/camera, API 24
- `ACameraManager_create` / `ACameraManager_delete` — create/destroy manager — source: NDK Camera, API 24
- `ACameraManager_getCameraIdList` → `ACameraIdList` — enumerate camera id strings — source: NDK Camera, API 24
- `ACameraManager_deleteCameraIdList` — free the id list — source: NDK Camera, API 24
- `ACameraManager_getCameraCharacteristics` → `ACameraMetadata` — per-device characteristics blob — source: NDK Camera, API 24
- `ACameraManager_openCamera` → `ACameraDevice` — open a device with `ACameraDevice_StateCallbacks` — source: NDK Camera, API 24
- `ACameraManager_registerAvailabilityCallback` / `ACameraManager_unregisterAvailabilityCallback` — hot-plug availability (`ACameraManager_AvailabilityCallbacks`: `onCameraAvailable`, `onCameraUnavailable`) — source: NDK Camera, API 24
- `ACameraManager_registerExtendedAvailabilityCallback` / `...unregister...` — `ACameraManager_ExtendedAvailabilityCallbacks`: adds `onCameraAccessPrioritiesChanged`, `onPhysicalCameraAvailable`, `onPhysicalCameraUnavailable` — source: NDK Camera, API 29
- `ACameraMetadata_getConstEntry` / `ACameraMetadata_getAllTags` / `ACameraMetadata_copy` / `ACameraMetadata_free` — read characteristics/result entries — source: NDK Camera, API 24
- `ACameraMetadata_isLogicalMultiCamera` → physical id list — NDK helper to test logical-multicam + get physical ids — source: NDK Camera, API 29
- `ACAMERA_LENS_FACING` — lens direction key; values `ACAMERA_LENS_FACING_FRONT`(0)/`_BACK`(1)/`_EXTERNAL`(2) — source: NDK Camera, API 24
- `ACAMERA_INFO_SUPPORTED_HARDWARE_LEVEL` — `_LEGACY`/`_LIMITED`/`_FULL`/`_3`(LEVEL_3)/`_EXTERNAL` — source: NDK Camera, API 24
- `ACAMERA_INFO_VERSION` — device-version string key — source: NDK Camera, API ?
- `ACAMERA_INFO_DEVICE_STATE_ORIENTATIONS` — foldable device-state→orientation map — source: CameraCharacteristics, API 32 `?`
- `ACAMERA_REQUEST_AVAILABLE_CAPABILITIES` — the capability flag list (see capture-modes section) — source: NDK Camera, API 24
- `ACAMERA_REQUEST_AVAILABLE_CHARACTERISTICS_KEYS` / `_AVAILABLE_REQUEST_KEYS` / `_AVAILABLE_RESULT_KEYS` — introspect which keys this device honors (avoids silent-clamp; MEL-ENGINE-VIII) — source: NDK Camera, API 24/29
- camera2 `CameraManager` (Java) — `getCameraIdList()`, `getCameraCharacteristics(String)`, `openCamera(...)`, `registerAvailabilityCallback(...)` — source: reference/android/hardware/camera2/CameraManager, API 21
- `CameraManager.getCameraIdListNoLazy()` — `?` `@hide` AOSP-only — source: AOSP CameraManager.java
- `CameraManager.getConcurrentCameraIds()` → `Set<Set<String>>` — sets of camera ids that can stream concurrently — source: reference/.../CameraManager, API 30. **NDK has NO equivalent — concurrent enumeration is Java-only.**
- `CameraManager.isConcurrentSessionConfigurationSupported(Map<String,SessionConfiguration>)` — feasibility of a concurrent multi-cam session — source: reference/.../CameraManager, API 30
- `CameraManager.AvailabilityCallback.onCameraAvailable/onCameraUnavailable(String)` — hot-plug — source: reference, API 21
- `CameraManager.AvailabilityCallback.onCameraAccessPrioritiesChanged()` — priorities shifted, no avail/unavail fired (full-screen↔PiP) — source: reference, API 33
- `CameraManager.AvailabilityCallback.onPhysicalCameraAvailable/onPhysicalCameraUnavailable(String,String)` — physical sub-camera hot state — source: reference, API 29
- `PackageManager.FEATURE_CAMERA_EXTERNAL = "android.hardware.camera.external"` — external USB UVC camera support — source: AOSP PackageManager, API 20
- `LENS_FACING_EXTERNAL`(2) + `INFO_SUPPORTED_HARDWARE_LEVEL_EXTERNAL` — how UVC webcams appear (orientation undefined, limited keys) — source: CameraMetadata, API 23
- device↔mic association — **none in camera2.** No CameraCharacteristics key links a (built-in/UVC) camera to its companion mic. Closest unrelated constructs: `MediaRecorder.AudioSource.CAMCORDER` (prefers camera-oriented mic, not selectable), `AudioDeviceInfo.TYPE_USB_DEVICE`/`TYPE_USB_HEADSET` on the audio side — source: AudioDeviceInfo/MediaRecorder.AudioSource reference. UVC-audio (UAC) endpoint surfaces independently via USB-audio stack.
- **CameraX delta** — `ProcessCameraProvider.getAvailableCameraInfos()` → `List<CameraInfo>` (no raw id strings); `CameraSelector.DEFAULT_FRONT_CAMERA`/`DEFAULT_BACK_CAMERA`, `LENS_FACING_FRONT/BACK/EXTERNAL/UNKNOWN`, `CameraSelector.Builder.requireLensFacing(int)`/`addCameraFilter(CameraFilter)` — source: reference/androidx/camera/core/CameraSelector, CameraX 1.3

### feature-COMBINATION feasibility (CRITICAL — silent-clamp trap)
- `SessionConfiguration(int sessionType, List<OutputConfiguration>, Executor, CameraCaptureSession.StateCallback)` — describes a full multi-surface session; `SESSION_REGULAR`/`SESSION_HIGH_SPEED` — source: AOSP SessionConfiguration.java, API 28
- `SessionConfiguration.setSessionParameters(CaptureRequest)` / `getSessionParameters()` — params held constant for the session — source: AOSP, API 28
- `SessionConfiguration.setInputConfiguration(InputConfiguration)` — reprocessing input — source: AOSP, API 28
- `SessionConfiguration.setColorSpace(ColorSpace.Named)` / `clearColorSpace()` / `getColorSpace()` — wide-gamut session — source: AOSP, API 34
- `CameraDevice.isSessionConfigurationSupported(SessionConfiguration)` — ask the OPEN device whether a stream set co-operates — source: AOSP CameraDevice.java, API 29
- `CameraDevice.CameraDeviceSetup` (nested) — query feasibility WITHOUT opening the camera: `getSessionCharacteristics(SessionConfiguration)`, `isSessionConfigurationSupported(SessionConfiguration)`, `createCaptureRequest(int)`, `getId()`; obtained via `CameraManager.getCameraDeviceSetup(String)` — source: reference/.../CameraDevice.CameraDeviceSetup, **Android 15 / API 35**
- `CameraDeviceSetup.isSessionConfigurationWithParametersSupported(...)` — the Android-15 feature-combination query (format+feature tuple feasibility) — source: reference, API 35 `?`
- `CameraCharacteristics.INFO_SESSION_CONFIGURATION_QUERY_VERSION` — version gate for the feature-combination query surface — source: reference/.../CameraCharacteristics, API 35
- `SCALER_MANDATORY_STREAM_COMBINATIONS` → `MandatoryStreamCombination[]` — guaranteed-supported combos per hw level — source: reference, API 29
- `SCALER_MANDATORY_CONCURRENT_STREAM_COMBINATIONS` — guaranteed concurrent multi-cam combos — source: reference, API 30
- `SCALER_MANDATORY_MAXIMUM_RESOLUTION_STREAM_COMBINATIONS` — for ULTRA_HIGH_RESOLUTION sensors — source: reference, API 31
- `SCALER_MANDATORY_TEN_BIT_OUTPUT_STREAM_COMBINATIONS` — guaranteed 10-bit HDR combos — source: reference, API 33
- `SCALER_MANDATORY_PREVIEW_STABILIZATION_OUTPUT_STREAM_COMBINATIONS` — combos valid with preview stabilization — source: reference, API 33
- `SCALER_MANDATORY_USE_CASE_STREAM_COMBINATIONS` — combos valid with STREAM_USE_CASE tags — source: reference, API 33
- `MandatoryStreamCombination` class (`android.hardware.camera2.params`) — source: reference, API 29
- **CameraX delta** — `ProcessCameraProvider.getAvailableConcurrentCameraInfos()` → `List<List<CameraInfo>>` (combinations); CameraX validates the bound use-case set automatically and throws on infeasible combos; **CameraX hides** the raw `isSessionConfigurationSupported`/`CameraDeviceSetup` query — no public hook to it — source: reference/androidx/camera/lifecycle/ProcessCameraProvider, CameraX 1.3

## topology & streams
- `ACAMERA_REQUEST_AVAILABLE_CAPABILITIES_LOGICAL_MULTI_CAMERA` — device is a logical camera backed by physicals — source: NDK Camera, API 28
- `ACAMERA_LOGICAL_MULTI_CAMERA_PHYSICAL_IDS` — physical sub-camera id list — source: NDK Camera, API 28
- `ACAMERA_LOGICAL_MULTI_CAMERA_SENSOR_SYNC_TYPE` — `_APPROXIMATE`/`_CALIBRATED` (genlock quality) — source: NDK Camera, API 28
- `ACAMERA_LOGICAL_MULTI_CAMERA_ACTIVE_PHYSICAL_ID` — which physical is currently feeding (result key) — source: CameraCharacteristics/Result, API 29
- `OutputConfiguration.setPhysicalCameraId(String)` — route a stream to a specific physical sensor — source: AOSP OutputConfiguration.java, API 28
- `ACameraCaptureSession_logicalCamera_captureCallbacks` / `..._captureCallbacksV2` — per-physical-camera result callbacks (struct) — source: NDK Camera, API 29/33
- concurrent cameras — `CameraManager.getConcurrentCameraIds()` + `isConcurrentSessionConfigurationSupported(...)`; `PackageManager.FEATURE_CAMERA_CONCURRENT="android.hardware.camera.concurrent"` — source: AOSP PackageManager, API 30
- multi-resolution streams — `MultiResolutionImageReader` (`newInstance`, `getReaders()`, `acquireLatestImage()`, `flush()`, `getSurfaceGroupId()`) — source: reference/android/media/MultiResolutionImageReader, API 31
- `SCALER_MULTI_RESOLUTION_STREAM_CONFIGURATION_MAP` → `MultiResolutionStreamConfigurationMap` — source: reference, API 31
- `ACAMERA_SCALER_MULTI_RESOLUTION_STREAM_SUPPORTED` — boolean capability — source: NDK Camera, API 31
- `ACAMERA_SCALER_PHYSICAL_CAMERA_MULTI_RESOLUTION_STREAM_CONFIGURATIONS` — per-physical multi-res configs — source: NDK Camera, API 31
- `ACAMERA_SCALER_AVAILABLE_STREAM_USE_CASES` — supported use-case tags — source: NDK Camera, API 33
- `OutputConfiguration.setStreamUseCase(long)` / `getStreamUseCase()` — tag a stream; values `SCALER_AVAILABLE_STREAM_USE_CASES_DEFAULT`/`_PREVIEW`/`_STILL_CAPTURE`/`_VIDEO_RECORD`/`_PREVIEW_VIDEO_STILL`/`_VIDEO_CALL`/`_CROPPED_RAW`(API34) — source: AOSP OutputConfiguration, API 33
- `REQUEST_AVAILABLE_CAPABILITIES_STREAM_USE_CASE` — device honors use-case tags — source: CameraMetadata, API 33
- `ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS` (+ `_MAXIMUM_RESOLUTION`) — raw stream-config tuple list — source: NDK Camera, API 24/31
- `SCALER_STREAM_CONFIGURATION_MAP` → `StreamConfigurationMap`: `getOutputSizes(int|Class)`, `getOutputMinFrameDuration(int,Size)`, `getOutputStallDuration(int,Size)`, `isOutputSupportedFor(int|Class|Surface)`, `getInputSizes`, `getOutputFormats`, `getInputFormats`, `getValidOutputFormatsForInput(int)` — source: reference/.../params/StreamConfigurationMap, API 21
- `ACAMERA_SCALER_AVAILABLE_RECOMMENDED_STREAM_CONFIGURATIONS` — usecase-tagged recommended configs (enum `_PREVIEW`/`_RECORD`/`_VIDEO_SNAPSHOT`/`_SNAPSHOT`/`_ZSL`/`_RAW`/`_LOW_LATENCY_SNAPSHOT`/`_PUBLIC_END`) — source: NDK Camera, API 24
- `ACAMERA_REQUEST_MAX_NUM_OUTPUT_STREAMS` / `_MAX_NUM_INPUT_STREAMS` — stream count ceilings — source: NDK Camera, API 24
- session params — `SessionConfiguration.setSessionParameters(CaptureRequest)` — source: AOSP, API 28
- `OutputConfiguration.enableSurfaceSharing()` / `addSurface(Surface)` / `removeSurface(Surface)` — one stream → multiple consumers — source: AOSP OutputConfiguration, API 26/31
- live reconfiguration — `ACameraCaptureSession_setRepeatingRequest` swap; deferred surfaces `OutputConfiguration(Size, Class)` + `addSurface` later — source: reference, API 26
- **CameraX delta** — `Preview`/`ImageCapture`/`ImageAnalysis`/`VideoCapture` use-case abstraction; `UseCaseGroup`+`ViewPort` for shared crop/effects; `StreamSharing` transparently multiplexes one camera stream across use cases beyond the hardware stream limit (no Camera2 equivalent — you manage limits yourself); `ConcurrentCamera`/`ConcurrentCameraConfig.Builder`/`SingleCameraConfig` for front+back binding (`isConcurrentCameraModeOn()` — note: NOT `...Supported`) — source: reference/androidx/camera, CameraX 1.3

## fine-grained control
> Per-frame: every key below is set on a `CaptureRequest` (`ACaptureRequest`), applied per submitted frame; the matching `CaptureResult` (`ACameraMetadata`) reports the value actually used. Supported ranges/availability live in `CameraCharacteristics` (the `*_AVAILABLE_*` / `*_INFO_*` keys), so the app reads the ceiling rather than discovering it via silent clamp (MEL-ENGINE-VIII).

### 3A — auto exposure / focus / white balance
- `ACAMERA_CONTROL_MODE` — `_OFF`/`_AUTO`/`_USE_SCENE_MODE`/`_OFF_KEEP_STATE`/`_USE_EXTENDED_SCENE_MODE` — master 3A switch — source: NDK Camera, API 24
- `ACAMERA_CONTROL_AE_MODE` — `_OFF`/`_ON`/`_ON_AUTO_FLASH`/`_ON_ALWAYS_FLASH`/`_ON_AUTO_FLASH_REDEYE`/`_ON_EXTERNAL_FLASH`(API28) — source: NDK Camera, API 24
- `ACAMERA_CONTROL_AE_LOCK` (+ `_AE_LOCK_AVAILABLE`) — `_OFF`/`_ON` — source: NDK Camera, API 24
- `ACAMERA_CONTROL_AE_REGIONS` — metering rectangles (x,y,x,y,weight) — source: NDK Camera, API 24
- `ACAMERA_CONTROL_AE_EXPOSURE_COMPENSATION` (range `_AE_COMPENSATION_RANGE`, step `_AE_COMPENSATION_STEP`) — EV comp index — source: NDK Camera, API 24
- `ACAMERA_CONTROL_AE_TARGET_FPS_RANGE` (avail `_AE_AVAILABLE_TARGET_FPS_RANGES`) — source: NDK Camera, API 24
- `ACAMERA_CONTROL_AE_ANTIBANDING_MODE` — `_OFF`/`_50HZ`/`_60HZ`/`_AUTO` (avail `_AE_AVAILABLE_ANTIBANDING_MODES`) — flicker/power-line — source: NDK Camera, API 24
- `ACAMERA_CONTROL_AE_PRECAPTURE_TRIGGER` — `_IDLE`/`_START`/`_CANCEL` — source: NDK Camera, API 24
- `ACAMERA_CONTROL_AE_STATE` (result) — `_INACTIVE`/`_SEARCHING`/`_CONVERGED`/`_LOCKED`/`_FLASH_REQUIRED`/`_PRECAPTURE` — convergence feedback — source: NDK Camera, API 24
- `ACAMERA_CONTROL_AE_PRIORITY_MODE` — `_OFF`/`_SENSOR_SENSITIVITY_PRIORITY`/`_SENSOR_EXPOSURE_TIME_PRIORITY` (avail `_AE_AVAILABLE_PRIORITY_MODES`) — partial-manual AE — source: NDK Camera enum `acamera_control_ae_priority_mode`, API 35 `?`
- `ACAMERA_CONTROL_AF_MODE` — `_OFF`/`_AUTO`/`_MACRO`/`_CONTINUOUS_VIDEO`/`_CONTINUOUS_PICTURE`/`_EDOF` (avail `_AF_AVAILABLE_MODES`) — source: NDK Camera, API 24
- `ACAMERA_CONTROL_AF_REGIONS` — focus metering rectangles — source: NDK Camera, API 24
- `ACAMERA_CONTROL_AF_TRIGGER` — `_IDLE`/`_START`/`_CANCEL` — source: NDK Camera, API 24
- `ACAMERA_CONTROL_AF_STATE` (result) — `_INACTIVE`/`_PASSIVE_SCAN`/`_PASSIVE_FOCUSED`/`_ACTIVE_SCAN`/`_FOCUSED_LOCKED`/`_NOT_FOCUSED_LOCKED`/`_PASSIVE_UNFOCUSED` — source: NDK Camera, API 24
- `ACAMERA_CONTROL_AF_SCENE_CHANGE` (result) — `_NOT_DETECTED`/`_DETECTED` — source: NDK Camera, API 28
- `ACAMERA_CONTROL_AWB_MODE` — `_OFF`/`_AUTO`/`_INCANDESCENT`/`_FLUORESCENT`/`_WARM_FLUORESCENT`/`_DAYLIGHT`/`_CLOUDY_DAYLIGHT`/`_TWILIGHT`/`_SHADE` (avail `_AWB_AVAILABLE_MODES`) — source: NDK Camera, API 24
- `ACAMERA_CONTROL_AWB_LOCK` (+ `_AWB_LOCK_AVAILABLE`), `ACAMERA_CONTROL_AWB_REGIONS`, `ACAMERA_CONTROL_AWB_STATE` (`_INACTIVE`/`_SEARCHING`/`_CONVERGED`/`_LOCKED`) — source: NDK Camera, API 24
- `ACAMERA_CONTROL_MAX_REGIONS` — [AE,AWB,AF] max metering-rectangle counts — source: NDK Camera, API 24
- `ACAMERA_CONTROL_CAPTURE_INTENT` — `_CUSTOM`/`_PREVIEW`/`_STILL_CAPTURE`/`_VIDEO_RECORD`/`_VIDEO_SNAPSHOT`/`_ZERO_SHUTTER_LAG`/`_MANUAL`/`_MOTION_TRACKING` — source: NDK Camera, API 24
- `ACAMERA_CONTROL_SCENE_MODE` — `_DISABLED`/`_FACE_PRIORITY`/`_ACTION`/`_PORTRAIT`/`_LANDSCAPE`/`_NIGHT`/`_NIGHT_PORTRAIT`/`_THEATRE`/`_BEACH`/`_SNOW`/`_SUNSET`/`_STEADYPHOTO`/`_FIREWORKS`/`_SPORTS`/`_PARTY`/`_CANDLELIGHT`/`_BARCODE`/`_HIGH_SPEED_VIDEO`/`_HDR` (avail `_AVAILABLE_SCENE_MODES`) — source: NDK Camera, API 24
- `ACAMERA_CONTROL_EFFECT_MODE` — `_OFF`/`_MONO`/`_NEGATIVE`/`_SOLARIZE`/`_SEPIA`/`_POSTERIZE`/`_WHITEBOARD`/`_BLACKBOARD`/`_AQUA` (avail `_AVAILABLE_EFFECTS`) — source: NDK Camera, API 24

### sensor — manual exposure (REQUEST_AVAILABLE_CAPABILITIES_MANUAL_SENSOR)
- `ACAMERA_SENSOR_EXPOSURE_TIME` (ns; range `ACAMERA_SENSOR_INFO_EXPOSURE_TIME_RANGE`) — source: NDK Camera, API 24
- `ACAMERA_SENSOR_SENSITIVITY` (ISO; range `ACAMERA_SENSOR_INFO_SENSITIVITY_RANGE`, analog ceiling `ACAMERA_SENSOR_MAX_ANALOG_SENSITIVITY`) — source: NDK Camera, API 24
- `ACAMERA_SENSOR_FRAME_DURATION` (ns; max `ACAMERA_SENSOR_INFO_MAX_FRAME_DURATION`) — source: NDK Camera, API 24
- `ACAMERA_CONTROL_POST_RAW_SENSITIVITY_BOOST` (range `_POST_RAW_SENSITIVITY_BOOST_RANGE`) — digital ISO boost applied post-RAW — source: CameraCharacteristics, API 24 `?`
- `ACAMERA_SENSOR_INFO_ACTIVE_ARRAY_SIZE` / `_PIXEL_ARRAY_SIZE` / `_PRE_CORRECTION_ACTIVE_ARRAY_SIZE` / `_PHYSICAL_SIZE` / `_WHITE_LEVEL` — source: NDK Camera, API 24
- `ACAMERA_SENSOR_INFO_COLOR_FILTER_ARRANGEMENT` — `_RGGB`/`_GRBG`/`_GBRG`/`_BGGR`/`_RGB`/`_MONO`/`_NIR` — source: NDK Camera, API 24/28
- `ACAMERA_SENSOR_TEST_PATTERN_MODE` / `_TEST_PATTERN_DATA` — `_OFF`/`_SOLID_COLOR`/`_COLOR_BARS`/`_COLOR_BARS_FADE_TO_GRAY`/`_PN9`/`_CUSTOM1` — source: NDK Camera, API 24

### lens
- `ACAMERA_LENS_FOCUS_DISTANCE` (diopters; calibration `ACAMERA_LENS_INFO_FOCUS_DISTANCE_CALIBRATION` `_UNCALIBRATED`/`_APPROXIMATE`/`_CALIBRATED`; hyperfocal `_HYPERFOCAL_DISTANCE`; min `_MINIMUM_FOCUS_DISTANCE`) — source: NDK Camera, API 24
- `ACAMERA_LENS_APERTURE` (avail `ACAMERA_LENS_INFO_AVAILABLE_APERTURES`) — source: NDK Camera, API 24
- `ACAMERA_LENS_FOCAL_LENGTH` (avail `_INFO_AVAILABLE_FOCAL_LENGTHS`) — source: NDK Camera, API 24
- `ACAMERA_LENS_FILTER_DENSITY` (avail `_INFO_AVAILABLE_FILTER_DENSITIES`) — ND filter EV — source: NDK Camera, API 24
- `ACAMERA_LENS_OPTICAL_STABILIZATION_MODE` — `_OFF`/`_ON` (avail `_INFO_AVAILABLE_OPTICAL_STABILIZATION`) — OIS — source: NDK Camera, API 24
- `ACAMERA_LENS_STATE` (result) — `_STATIONARY`/`_MOVING` — source: NDK Camera, API 24

### flash / torch
- `ACAMERA_FLASH_MODE` — `_OFF`/`_SINGLE`/`_TORCH` — source: NDK Camera, API 24
- `ACAMERA_FLASH_STATE` (result) — `_UNAVAILABLE`/`_CHARGING`/`_READY`/`_FIRED`/`_PARTIAL` — source: NDK Camera, API 24
- `ACAMERA_FLASH_INFO_AVAILABLE` — `_FALSE`/`_TRUE` — source: NDK Camera, API 24
- `ACAMERA_FLASH_STRENGTH_LEVEL` / `ACAMERA_FLASH_INFO_STRENGTH_MAXIMUM_LEVEL` / `ACAMERA_FLASH_INFO_STRENGTH_DEFAULT_LEVEL` — per-frame flash brightness control (single mode) — source: NDK Camera (`FLASH_SINGLE_STRENGTH_MAX_LEVEL` in master header), API 35 `?`
- `ACAMERA_FLASH_TORCH_STRENGTH_LEVEL` / `_TORCH_STRENGTH_MAX_LEVEL` / `_TORCH_STRENGTH_DEFAULT_LEVEL` — torch brightness; also `CameraManager.turnOnTorchWithStrengthLevel(String,int)` (Java, API 33) — source: NDK Camera + reference/.../CameraManager, API 33/35 `?`

### color / tone / ISP (REQUEST_AVAILABLE_CAPABILITIES_MANUAL_POST_PROCESSING)
- `ACAMERA_COLOR_CORRECTION_MODE` — `_TRANSFORM_MATRIX`/`_FAST`/`_HIGH_QUALITY`/`_CCT`(API36?) — source: NDK Camera, API 24
- `ACAMERA_COLOR_CORRECTION_TRANSFORM` — 3x3 rational CCM — source: NDK Camera, API 24
- `ACAMERA_COLOR_CORRECTION_GAINS` — per-channel RGGB WB gains — source: NDK Camera, API 24
- `ACAMERA_COLOR_CORRECTION_ABERRATION_MODE` — `_OFF`/`_FAST`/`_HIGH_QUALITY` (avail `_AVAILABLE_ABERRATION_MODES`) — source: NDK Camera, API 24
- `ACAMERA_NOISE_REDUCTION_MODE` — `_OFF`/`_FAST`/`_HIGH_QUALITY`/`_MINIMAL`/`_ZERO_SHUTTER_LAG` (avail `_AVAILABLE_NOISE_REDUCTION_MODES`) — source: NDK Camera, API 24
- `ACAMERA_EDGE_MODE` — `_OFF`/`_FAST`/`_HIGH_QUALITY`/`_ZERO_SHUTTER_LAG` (avail `_AVAILABLE_EDGE_MODES`) — sharpening — source: NDK Camera, API 24
- `ACAMERA_HOT_PIXEL_MODE` — `_OFF`/`_FAST`/`_HIGH_QUALITY` (avail `_HOT_PIXEL_AVAILABLE_HOT_PIXEL_MODES`) — source: NDK Camera, API 24
- `ACAMERA_SHADING_MODE` — `_OFF`/`_FAST`/`_HIGH_QUALITY` (avail `_SHADING_AVAILABLE_MODES`) — lens-shading correction — source: NDK Camera, API 24
- `ACAMERA_TONEMAP_MODE` — `_CONTRAST_CURVE`/`_FAST`/`_HIGH_QUALITY`/`_GAMMA_VALUE`/`_PRESET_CURVE` (avail `_AVAILABLE_TONE_MAP_MODES`, max pts `_MAX_CURVE_POINTS`) — source: NDK Camera, API 24
- `ACAMERA_TONEMAP_CURVE` (+ `_CURVE_RED`/`_GREEN`/`_BLUE`) — per-channel tone curve points — source: NDK Camera, API 24
- `ACAMERA_TONEMAP_GAMMA` — gamma value when `_GAMMA_VALUE` mode — source: NDK Camera, API 24
- `ACAMERA_TONEMAP_PRESET_CURVE` — `_SRGB`/`_REC709` — source: NDK Camera, API 24
- `ACAMERA_BLACK_LEVEL_LOCK` — `_OFF`/`_ON` — source: NDK Camera, API 24
- `ACAMERA_CONTROL_VIDEO_STABILIZATION_MODE` — `_OFF`/`_ON`/`_PREVIEW_STABILIZATION` (avail `_AVAILABLE_VIDEO_STABILIZATION_MODES`) — EIS — source: NDK Camera, API 24/33
- `ACAMERA_SCALER_CROP_REGION` — digital zoom crop rect (cropping type `ACAMERA_SCALER_CROPPING_TYPE` `_CENTER_ONLY`/`_FREEFORM`) — source: NDK Camera, API 24
- `ACAMERA_SCALER_AVAILABLE_MAX_DIGITAL_ZOOM` — max crop-based zoom factor — source: NDK Camera, API 24
- `ACAMERA_CONTROL_ZOOM_RATIO` (range `ACAMERA_CONTROL_ZOOM_RATIO_RANGE`) — float zoom incl. <1.0 ultrawide; preferred over CROP_REGION — source: NDK Camera, API 30
- `ACAMERA_CONTROL_ZOOM_METHOD` — `_AUTO`/`_ZOOM_RATIO`/`_DIGITAL_CROP` — source: NDK Camera enum `acamera_control_zoom_method`, API 36 `?`
- `ACAMERA_SCALER_ROTATE_AND_CROP` — `_NONE`/`_90`/`_180`/`_270`/`_AUTO` (avail `_AVAILABLE_ROTATE_AND_CROP_MODES`) — source: NDK Camera, API 30 `?`
- `ACAMERA_DISTORTION_CORRECTION_MODE` — `_OFF`/`_FAST`/`_HIGH_QUALITY` (avail `_DISTORTION_CORRECTION_AVAILABLE_MODES`) — source: NDK Camera, API 28
- `ACAMERA_CONTROL_SETTINGS_OVERRIDE` — `_OFF`/`_ZOOM` (low-latency zoom override; avail `ACAMERA_CONTROL_AVAILABLE_SETTINGS_OVERRIDES`) — source: NDK Camera enum `acamera_control_settings_override`, API 34
- `ACAMERA_CONTROL_AUTOFRAMING` — `_OFF`/`_ON`/`_AUTO` (avail `_AUTOFRAMING_AVAILABLE`, state `_AUTOFRAMING_STATE` `_INACTIVE`/`_FRAMING`/`_CONVERGED`) — auto-framing/center-stage-like — source: NDK Camera enum `acamera_control_autoframing`, API 34
- `ACAMERA_CONTROL_ENABLE_ZSL` — `_FALSE`/`_TRUE` — zero-shutter-lag hint — source: NDK Camera, API 24
- read-sensor-settings — `ACAMERA_REQUEST_AVAILABLE_CAPABILITIES_READ_SENSOR_SETTINGS` makes exposure/sensitivity/lens results reliable per-frame — source: NDK Camera, API 24
- `ACameraMetadata_getConstEntry` (result) returns `ACAMERA_SENSOR_EXPOSURE_TIME`/`_SENSITIVITY`/`_FRAME_DURATION`/`ACAMERA_LENS_*` as APPLIED values — the honest "which frame did this take effect" feedback — source: NDK Camera, API 24
- **CameraX delta** — only a curated subset: `CameraControl.enableTorch(boolean)`, `setZoomRatio(float)`, `setLinearZoom(float)`, `startFocusAndMetering(FocusMeteringAction)`+`MeteringPoint`/`SurfaceOrientedMeteringPointFactory`+`FocusMeteringResult`, `cancelFocusAndMetering()`, `setExposureCompensationIndex(int)`; `CameraInfo.getZoomState()`(`ZoomState.getMinZoomRatio/getMaxZoomRatio/getZoomRatio/getLinearZoom`), `getExposureState()`(`ExposureState.getExposureCompensationRange/getExposureCompensationStep`), `hasFlashUnit()`, `getTorchState()`, `isFocusMeteringSupported(...)`. Full per-frame manual catalog requires `Camera2Interop.Extender.setCaptureRequestOption(CaptureRequest.Key,V)` / `Camera2CameraControl.setCaptureRequestOptions(CaptureRequestOptions)` — key injection only, not session ownership — source: reference/androidx/camera + camera2.interop, CameraX 1.3 (`@ExperimentalCamera2Interop`)

## mechanical controls
- PTZ (mechanical pan/tilt/zoom) — **none in Camera2/NDK.** Camera2 has no mechanical-PTZ key; `ACAMERA_CONTROL_ZOOM_RATIO`/`SCALER_CROP_REGION` are digital only. External/UVC PTZ controls (UVC `CT_PANTILT_ABSOLUTE_CONTROL`/`CT_ZOOM_ABSOLUTE_CONTROL`) are NOT surfaced through camera2 to apps `?` — Android exposes UVC cameras as ordinary camera2 devices with no PTZ passthrough. Mark `?` (no public app path).
- `ACAMERA_CONTROL_AUTOFRAMING` is the closest software analogue (digital reframing), not mechanical — source: NDK Camera, API 34

## capture modes — REQUEST_AVAILABLE_CAPABILITIES enum
> Each flag is a capability AREA. Values of `ACAMERA_REQUEST_AVAILABLE_CAPABILITIES` (enum `acamera_metadata_enum_acamera_request_available_capabilities`):
- `..._BACKWARD_COMPATIBLE` — guarantees the legacy preview/JPEG/3A baseline — source: NDK Camera, API 24
- `..._MANUAL_SENSOR` — unlocks SENSOR_EXPOSURE_TIME/SENSITIVITY/FRAME_DURATION, AE_LOCK, full manual exposure — source: NDK Camera, API 24
- `..._MANUAL_POST_PROCESSING` — unlocks COLOR_CORRECTION/TONEMAP/SHADING/NOISE_REDUCTION/EDGE manual control — source: NDK Camera, API 24
- `..._RAW` — RAW_SENSOR/RAW10/RAW12 output + DNG metadata (sensor cal matrices, noise profile) — pairs with Java `DngCreator` — source: NDK Camera, API 24
- `..._PRIVATE_REPROCESSING` — PRIVATE-format reprocessing session (ZSL pipeline) — source: NDK Camera, API 24
- `..._READ_SENSOR_SETTINGS` — reliable per-frame applied exposure/lens results — source: NDK Camera, API 24
- `..._BURST_CAPTURE` — rapid full-res burst at a guaranteed rate — source: NDK Camera, API 24
- `..._YUV_REPROCESSING` — YUV_420_888 reprocessing session — source: NDK Camera, API 24
- `..._DEPTH_OUTPUT` — DEPTH16/DEPTH_POINT_CLOUD/DEPTH_JPEG streams — source: NDK Camera, API 24
- `..._CONSTRAINED_HIGH_SPEED_VIDEO` — 120/240fps slow-mo session — source: NDK Camera, API 24
- `..._MOTION_TRACKING` — CAPTURE_INTENT_MOTION_TRACKING, fixed-FPS, REALTIME timestamps for AR — source: NDK Camera, API 26
- `..._LOGICAL_MULTI_CAMERA` — physical-stream access on a logical device — source: NDK Camera, API 28
- `..._MONOCHROME` — Y8/Y16, no Bayer CFA — source: NDK Camera, API 28
- `..._SECURE_IMAGE_DATA` — DRM/secure stream (`SCALER_DEFAULT_SECURE_IMAGE_SIZE`) — source: NDK Camera, API 29
- `..._SYSTEM_CAMERA` — `?` privileged/system-camera flag — source: NDK Camera, API 29
- `..._OFFLINE_PROCESSING` — `ACameraCaptureSession` offline switch (`ACameraCaptureSession_setRepeatingRequest` → release device, finish processing) — source: NDK Camera, API 29
- `..._ULTRA_HIGH_RESOLUTION_SENSOR` — SENSOR_PIXEL_MODE_MAXIMUM_RESOLUTION (quad-Bayer remosaic) — source: NDK Camera, API 31
- `..._REMOSAIC_REPROCESSING` — remosaic reprocessing path — source: NDK Camera, API 31
- `..._DYNAMIC_RANGE_TEN_BIT` — 10-bit HDR profiles (HLG10/HDR10/HDR10+/Dolby Vision) — source: NDK Camera, API 33
- `..._STREAM_USE_CASE` — honors SCALER stream-use-case tags — source: NDK Camera, API 33
- `..._COLOR_SPACE_PROFILES` — wide-gamut color-space selection — source: NDK Camera, API 34

### supporting capture-mode symbols
- `ACameraDevice_createCaptureSession` / `..._createCaptureSessionWithSessionParameters` — session creation (NDK) — source: NDK Camera, API 24
- `CameraDevice.createReprocessableCaptureSession(InputConfiguration, List<Surface>, ...)` / `..._ByConfigurations` + `ImageWriter` + `createReprocessCaptureRequest(TotalCaptureResult)` — reprocessing (ZSL/RAW→YUV/JPEG) — source: AOSP CameraDevice.java, API 23/24
- `CameraDevice.createConstrainedHighSpeedCaptureSession(List<Surface>, ...)` → `CameraConstrainedHighSpeedCaptureSession.createHighSpeedRequestList(CaptureRequest)`; ranges via `StreamConfigurationMap.getHighSpeedVideoFpsRanges()`/`getHighSpeedVideoSizes()`/`getHighSpeedVideoSizesFor(Range)`/`getHighSpeedVideoFpsRangesFor(Size)`; `SESSION_HIGH_SPEED` — source: AOSP CameraDevice.java + reference, API 23
- request templates (`ACameraDevice_request_template` / `CameraDevice.createCaptureRequest(int)`): `TEMPLATE_PREVIEW`/`TEMPLATE_STILL_CAPTURE`/`TEMPLATE_RECORD`/`TEMPLATE_VIDEO_SNAPSHOT`/`TEMPLATE_ZERO_SHUTTER_LAG`/`TEMPLATE_MANUAL` — source: AOSP CameraDevice.java, API 21/24
- `ACAMERA_SENSOR_PIXEL_MODE` — `_DEFAULT`/`_MAXIMUM_RESOLUTION` (full-res vs binned) — source: NDK Camera, API 31
- `ACAMERA_SENSOR_RAW_BINNING_FACTOR_USED` (result) — `_TRUE`/`_FALSE` — source: NDK Camera, API 31
- JPEG encode + EXIF — `ACAMERA_JPEG_ORIENTATION`/`_QUALITY`/`_THUMBNAIL_QUALITY`/`_THUMBNAIL_SIZE`/`_GPS_*`(Java `JPEG_GPS_LOCATION`) — source: NDK Camera, API 24
- HEIC encode — `ACAMERA_HEIC_AVAILABLE_HEIC_STREAM_CONFIGURATIONS` (+`_MAXIMUM_RESOLUTION`), `ImageFormat.HEIC` — source: NDK Camera, API 30
- Ultra HDR JPEG — `ImageFormat.JPEG_R`, `ACAMERA_JPEGR_AVAILABLE_JPEG_R_STREAM_CONFIGURATIONS` (+`_MAXIMUM_RESOLUTION`) — source: NDK Camera / api_diff 34, API 34
- HEIC Ultra HDR — `ACAMERA_HEIC_AVAILABLE_HEIC_ULTRA_HDR_STREAM_CONFIGURATIONS` (enum present) — source: NDK Camera, API 35 `?`
- `ACAMERA_REPROCESS_EFFECTIVE_EXPOSURE_FACTOR` / `ACAMERA_REPROCESS_MAX_CAPTURE_STALL` — reprocessing tuning — source: NDK Camera, API 24
- time-lapse — no dedicated key; achieved via long `SENSOR_FRAME_DURATION` / repeating-request cadence — source: derived, not a key
- **CameraX delta** — `ImageCapture.CAPTURE_MODE_MINIMIZE_LATENCY`/`_MAXIMIZE_QUALITY`/`_ZERO_SHUTTER_LAG`, `FLASH_MODE_AUTO/ON/OFF/SCREEN`, `takePicture(...)`, `OutputFileOptions`; `Recorder`/`PendingRecording`/`Recording`/`VideoRecordEvent` is a full encode+mux pipeline (Camera2 gives frames only). **CameraX hides** RAW reprocessing, constrained high-speed sessions, remosaic — drop to Camera2 — source: reference/androidx/camera, CameraX 1.3

## depth / 3D / calibration
- `ACAMERA_REQUEST_AVAILABLE_CAPABILITIES_DEPTH_OUTPUT` — depth capability — source: NDK Camera, API 24
- `ACAMERA_DEPTH_AVAILABLE_DEPTH_STREAM_CONFIGURATIONS` (+`_MAXIMUM_RESOLUTION`) — depth stream configs — source: NDK Camera, API 24/31
- `ACAMERA_DEPTH_AVAILABLE_DEPTH_MIN_FRAME_DURATIONS` / `_DEPTH_STALL_DURATIONS` — depth timing — source: NDK Camera, API 24
- `ACAMERA_DEPTH_DEPTH_IS_EXCLUSIVE` — `_FALSE`/`_TRUE` (depth & color mutually exclusive) — source: NDK Camera, API 24
- `ACAMERA_DEPTH_AVAILABLE_DYNAMIC_DEPTH_STREAM_CONFIGURATIONS` (+`_MAXIMUM_RESOLUTION`) — depth-JPEG (dynamic depth) — source: NDK Camera, API 29
- formats `DEPTH16` / `DEPTH_POINT_CLOUD` / `DEPTH_JPEG` (`AIMAGE_FORMAT_DEPTH16`/`_DEPTH_POINT_CLOUD`/`_DEPTH_JPEG`) — source: ImageFormat / NDK media, API 23-30
- `ACAMERA_LENS_INTRINSIC_CALIBRATION` — fx,fy,cx,cy,skew — source: NDK Camera, API 28 `?`
- `ACAMERA_LENS_DISTORTION` — radial/tangential distortion coeffs (Brown-Conrady) — source: NDK Camera, API 28
- `ACAMERA_LENS_RADIAL_DISTORTION` — **deprecated**, superseded by LENS_DISTORTION — source: NDK Camera, API 23
- `ACAMERA_LENS_POSE_TRANSLATION` — sensor offset (meters) from reference point — source: NDK Camera, API 28 `?`
- `ACAMERA_LENS_POSE_ROTATION` — sensor orientation quaternion — source: NDK Camera, API 28 `?`
- `ACAMERA_LENS_POSE_REFERENCE` — `_PRIMARY_CAMERA`(0)/`_GYROSCOPE`(1)/`_UNDEFINED`(2)/`_AUTOMOTIVE`(3) — source: NDK Camera, API 28
- `ACAMERA_REQUEST_AVAILABLE_CAPABILITIES_MONOCHROME` — mono sensor, Y8/Y16 — source: NDK Camera, API 28
- `AIMAGE_FORMAT_Y8` (`ImageFormat.Y8`=0x20203859) — 8-bit luma (mono/NIR) — source: NDK media / ImageFormat, API 29
- IR/NIR — `SENSOR_INFO_COLOR_FILTER_ARRANGEMENT_NIR` exposes near-IR sensors as mono — source: CameraMetadata, API 28
- segmentation/portrait matte — **not a camera2 stream**; provided only via Extensions BOKEH or vendor — see live effects
- **CameraX delta** — depth is NOT a first-class CameraX use case; access via `Camera2Interop` + raw `ImageReader(DEPTH16)`; `CameraInfo` surfaces no calibration keys (use `Camera2CameraInfo.getCameraCharacteristic(...)`) — source: reference/androidx/camera, CameraX 1.3

## live effects
- `CameraExtensionCharacteristics` (Camera2 native) — `EXTENSION_AUTOMATIC`/`EXTENSION_HDR`/`EXTENSION_NIGHT`/`EXTENSION_BOKEH`/`EXTENSION_FACE_RETOUCH` — source: reference/.../CameraExtensionCharacteristics, API 31 (all five)
- `CameraExtensionCharacteristics.getSupportedExtensions()` → `List<Integer>` — source: reference, API 31
- `getExtensionSupportedSizes(int extension, int|Class format)` — source: reference, API 31
- `getEstimatedCaptureLatencyRangeMillis(int extension, Size, int format)` → `Range<Long>` — source: reference, API 31
- `isPostviewAvailable(int extension)` / `isCaptureProcessProgressAvailable(int extension)` — source: reference, API 34 `?`
- `CameraDevice.createExtensionSession(ExtensionSessionConfiguration)` → `CameraExtensionSession` (`capture(...)`, `setRepeatingRequest(...)`) — source: AOSP CameraDevice.java, API 31
- `ACAMERA_EXTENSION_STRENGTH` (request) / `ACAMERA_EXTENSION_CURRENT_TYPE` (result) — per-frame extension strength + active type — source: NDK Camera EXTENSION section, API 34 `?`
- `ACAMERA_EXTENSION_NIGHT_MODE_INDICATOR` (result) — `_UNKNOWN`/`_OFF`/`_ON` (night-mode hint) — source: NDK Camera enum `acamera_extension_night_mode_indicator`, API 35 `?`
- `ACAMERA_CONTROL_EXTENDED_SCENE_MODE` — `_DISABLED`/`_BOKEH_STILL_CAPTURE`/`_BOKEH_CONTINUOUS` (avail `_AVAILABLE_EXTENDED_SCENE_MODE_MAX_SIZES` + `_ZOOM_RATIO_RANGES`) — native continuous bokeh (distinct from Extensions) — source: NDK Camera enum `acamera_control_extended_scene_mode`, API 30
- `ACAMERA_CONTROL_LOW_LIGHT_BOOST_STATE` (result) — `_INACTIVE`/`_ACTIVE`; engaged via `AE_MODE_ON_LOW_LIGHT_BOOST_BRIGHTNESS` — source: NDK Camera enum `acamera_control_low_light_boost_state`, API 35 `?`
- `ACAMERA_CONTROL_AUTOFRAMING` — auto-framing (center-stage analogue), see fine-grained control — source: NDK Camera, API 34
- eye-contact / reactions / background blur (non-bokeh) — **vendor-specific**, no public camera2 key; surfaced only if a vendor exposes them via the standard EXTENSION modes — mark vendor-specific
- **CameraX delta** — `ExtensionsManager.getInstanceAsync(Context, CameraProvider)`, `isExtensionAvailable(CameraSelector,int)`, `getExtensionEnabledCameraSelector(CameraSelector,int)`, `getEstimatedCaptureLatencyRange(...)` `?`; `ExtensionMode.AUTO/HDR/NIGHT/BOKEH/FACE_RETOUCH/NONE` (package `androidx.camera.extensions`). CameraX makes extensions trivial (selector → bindToLifecycle); same 5 modes, preview+still only, NO video extension on either API. `CameraEffect`/`OverlayEffect` (`androidx.camera.effects`, **CameraX 1.4**) for app GL overlays — source: reference/androidx/camera/extensions + effects, CameraX 1.3/1.4

## frame memory
- NDK `AImageReader_new` / `AImageReader_newWithUsage` / `AImageReader_newWithDataSpace`(API34) — create reader (size/format/usage/dataspace) — source: NDK Media, developer.android.com/ndk/reference/group/media, API 24/26/34
- `AImageReader_getWindow` → `ANativeWindow` (the Surface producers write to) — source: NDK Media, API 24
- `AImageReader_acquireNextImage` / `AImageReader_acquireLatestImage` (+`...Async` with release fence) — source: NDK Media, API 24/26
- `AImageReader_setImageListener` (`AImageReader_ImageListener`) / `AImageReader_setBufferRemovedListener` (`AImageReader_BufferRemovedListener`) — source: NDK Media, API 24/26
- `AImageReader_getMaxImages` — buffer-queue depth ceiling (back-pressure boundary) — source: NDK Media, API 24
- `AImageReader_getFormat` / `_getWidth` / `_getHeight` / `_delete` — source: NDK Media, API 24
- `AImage_getPlaneData` / `_getPlaneRowStride` / `_getPlanePixelStride` / `_getNumberOfPlanes` — CPU plane access + stride/layout — source: NDK Media, API 24
- `AImage_getTimestamp` / `_getFormat` / `_getCropRect` / `_getWidth` / `_getHeight` — source: NDK Media, API 24
- `AImage_getHardwareBuffer` → `AHardwareBuffer*` — zero-copy handoff to GPU — source: NDK Media, API 26
- `AImage_getDataSpace` / `_getTransform` — source: NDK Media, API 34
- `AImage_delete` / `_deleteAsync` (release fence) — source: NDK Media, API 24/26
- `AHardwareBuffer_allocate` / `_lock` / `_lockPlanes`(API29) / `_lockAndGetInfo`(API29) / `_unlock` / `_describe` / `_isSupported`(API29) / `_acquire` / `_release` / `_getId`(API31) — source: NDK AHardwareBuffer, group/a-hardware-buffer, API 26/29/31
- `AHardwareBuffer_fromHardwareBuffer` / `_toHardwareBuffer` — Java `android.hardware.HardwareBuffer` bridge — source: NDK, API 26
- usage flags `AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE` / `_GPU_FRAMEBUFFER` (alias `_GPU_COLOR_OUTPUT`) / `_GPU_DATA_BUFFER` / `_CPU_READ_*` / `_CPU_WRITE_*` / `_PROTECTED_CONTENT` / `_VIDEO_ENCODE` / `_COMPOSER_OVERLAY` / `_FRONT_BUFFER` / `_SENSOR_DIRECT_DATA` / `_VENDOR_0..19` — source: NDK, API 26+
- **`AHARDWAREBUFFER_USAGE_CAMERA_READ` / `_CAMERA_WRITE` do NOT exist in the public NDK** — system gralloc bits only `?`
- formats `AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM` / `_R8G8B8X8_UNORM` / `_R8G8B8_UNORM` / `_R5G6B5_UNORM` / `_R16G16B16A16_FLOAT` / `_R10G10B10A2_UNORM` / `_R8_UNORM` / `_BLOB` / `_D16_UNORM` / `_D24_UNORM` / `_D24_UNORM_S8_UINT` / `_D32_FLOAT` / `_D32_FLOAT_S8_UINT` / `_S8_UINT` / `_Y8Cb8Cr8_420` (NDK name for YUV420, **not** `_YCbCr_420_888`) / `_YCbCr_P010` / `_YCbCr_P210` — source: NDK, API 26+
- zero-copy → Vulkan: ext `VK_ANDROID_external_memory_android_hardware_buffer`, `vkGetAndroidHardwareBufferPropertiesANDROID`, `vkGetMemoryAndroidHardwareBufferANDROID`, `VkImportAndroidHardwareBufferInfoANDROID` — source: Vulkan registry
- zero-copy → EGL/GL: ext `EGL_ANDROID_get_native_client_buffer`, `eglGetNativeClientBufferANDROID`, target `EGL_NATIVE_BUFFER_ANDROID` in `eglCreateImageKHR` — source: Khronos
- Java `ImageReader.newInstance(w,h,format,maxImages)` / `(w,h,format,maxImages,usage)`(API29); `Image`/`Image.Plane.getBuffer/getRowStride/getPixelStride`; `acquireLatestImage`/`acquireNextImage`; `discardFreeBuffers()`(API25); `getHardwareBufferFormat()`(API28 `?`); `getUsage()`(API29); `getDataSpace()`(API34); `OnImageAvailableListener` — source: AOSP ImageReader.java / reference, API 19+
- back-pressure / frame-drop — `acquireLatestImage` drops older buffers (frame-drop boundary); `maxImages` exhaustion stalls the producer (CameraX surfaces this as `STRATEGY_KEEP_ONLY_LATEST`/`STRATEGY_BLOCK_PRODUCER`) — source: reference, API 19
- pixel formats (`android.graphics.ImageFormat` / `AIMAGE_FORMAT_*`): `YUV_420_888`(0x23), `YUV_422_888`(0x27), `YUV_444_888`(0x28), `YV12`, `NV21`, `NV16`, `PRIVATE`(0x22), `JPEG`(0x100), `JPEG_R`(0x1005, API34), `HEIC`(API28), `RAW_SENSOR`(0x20; NDK=`AIMAGE_FORMAT_RAW16`), `RAW10`(0x25), `RAW12`(0x26), `RAW_PRIVATE`(0x24, API24), `DEPTH16`, `DEPTH_POINT_CLOUD`(0x101), `DEPTH_JPEG`(0x69656963, API29), `Y8`(0x20203859, API29), `YCBCR_P010`(0x36, API31), `RGB_565` — source: AOSP ImageFormat.java / NDK media, API per-format
  - `AIMAGE_FORMAT_YCBCR_P010` / `AIMAGE_FORMAT_JPEG_R` — **not confirmed verbatim in the rendered NDK media group**; likely exist API 31/34 — `?`
  - `FLEX_RGB_888`/`FLEX_RGBA_8888` (`@hide`), `HEIC_ULTRAHDR`/`YCBCR_P210` (`@FlaggedApi`) — non-public — `?`
- **CameraX delta** — `ImageAnalysis.setAnalyzer(Executor, Analyzer)` → `ImageProxy`; `OUTPUT_IMAGE_FORMAT_YUV_420_888`/`_RGBA_8888`; `STRATEGY_KEEP_ONLY_LATEST`/`STRATEGY_BLOCK_PRODUCER`; `setImageQueueDepth(int)`. **CameraX hides** raw ImageReader format choice beyond YUV/RGBA/JPEG and AHardwareBuffer access — source: reference/androidx/camera/core/ImageAnalysis, CameraX 1.3

## timing
- `ACAMERA_SENSOR_TIMESTAMP` (result, ns) — per-frame start-of-exposure timestamp — source: NDK Camera, API 24
- `ACAMERA_SENSOR_INFO_TIMESTAMP_SOURCE` — `_UNKNOWN`/`_REALTIME` (REALTIME = `SystemClock.elapsedRealtimeNanos`, correlatable with IMU/SensorEvent) — source: NDK Camera, API 24
- `ACAMERA_SENSOR_ROLLING_SHUTTER_SKEW` (result, ns) — top-to-bottom readout skew — source: NDK Camera, API 24
- `ACAMERA_SENSOR_FRAME_DURATION` (result) — per-frame duration applied — source: NDK Camera, API 24
- `ACAMERA_SENSOR_READOUT_TIMESTAMP` — `_NOT_SUPPORTED`/`_HARDWARE`; with `OutputConfiguration.setReadoutTimestampEnabled(boolean)` switches timestamps to end-of-readout — source: NDK Camera, API 33 `?`
- `OutputConfiguration.setTimestampBase(int)` — `TIMESTAMP_BASE_DEFAULT`/`_SENSOR`/`_MONOTONIC`/`_REALTIME`/`_CHOREOGRAPHER_SYNCED` — pick the clock domain — source: AOSP OutputConfiguration, API 33
- `CaptureResult.getFrameNumber()` / `ACaptureRequest` sequence id via `ACameraCaptureSession_capture` return + `onCaptureSequenceCompleted` (`ACameraCaptureSession_captureCallbacks`) — source: NDK Camera, API 24
- `ACAMERA_REQUEST_PIPELINE_DEPTH` (result) / `ACAMERA_REQUEST_PARTIAL_RESULT_COUNT` / `ACAMERA_SYNC_FRAME_NUMBER` / `ACAMERA_SYNC_MAX_LATENCY` (`_PER_FRAME_CONTROL`/`_UNKNOWN`) — when a setting takes effect — source: NDK Camera, API 24
- multi-cam sync — `ACAMERA_LOGICAL_MULTI_CAMERA_SENSOR_SYNC_TYPE` `_APPROXIMATE`/`_CALIBRATED` (genlock quality) — source: NDK Camera, API 28
- IMU correlation — TIMESTAMP_SOURCE_REALTIME makes `SENSOR_TIMESTAMP` comparable to motion-sensor event timestamps; the per-frame gyro/OIS samples are in STATISTICS_OIS_* (metadata section) — source: NDK Camera, API 24
- genlock / SMPTE timecode — **none in camera2** (no timecode key; genlock only via the logical-multicam CALIBRATED sync type) — source: derived
- **CameraX delta** — `ImageProxy.getImageInfo().getTimestamp()`; CameraX hides clock-domain/timestamp-base selection (use Camera2Interop) — source: reference/androidx/camera, CameraX 1.3

## metadata
- `ACAMERA_STATISTICS_FACE_DETECT_MODE` — `_OFF`/`_SIMPLE`/`_FULL` (avail `_INFO_AVAILABLE_FACE_DETECT_MODES`, max `_INFO_MAX_FACE_COUNT`) — source: NDK Camera, API 24
- `ACAMERA_STATISTICS_FACES` (result) — face rectangles + (FULL mode) landmarks (eyes/mouth) + ids + scores — source: NDK Camera, API 24
- `ACAMERA_STATISTICS_LENS_SHADING_MAP` (+ mode `ACAMERA_STATISTICS_LENS_SHADING_MAP_MODE` `_OFF`/`_ON`) — per-channel shading gain grid — source: NDK Camera, API 24
- `ACAMERA_STATISTICS_HOT_PIXEL_MAP` (+ mode `_HOT_PIXEL_MAP_MODE`) — hot-pixel coordinate list — source: NDK Camera, API 24
- `ACAMERA_STATISTICS_SCENE_FLICKER` (result) — `_NONE`/`_50HZ`/`_60HZ` — source: NDK Camera, API 24
- `ACAMERA_STATISTICS_OIS_DATA_MODE` — `_OFF`/`_ON` (avail `_INFO_AVAILABLE_OIS_DATA_MODES`); results `ACAMERA_STATISTICS_OIS_TIMESTAMPS` / `_OIS_X_SHIFTS` / `_OIS_Y_SHIFTS` — per-frame OIS sample stream (for EIS/stabilization correlation) — source: NDK Camera, API 28
- `ACAMERA_STATISTICS_HISTOGRAM_MODE` / `_SHARPNESS_MAP_MODE` / `ACAMERA_STATISTICS_INFO_MAX_HISTOGRAM_COUNT` / `_MAX_SHARPNESS_MAP_VALUE` — histogram + sharpness statistics (legacy/limited availability) — source: NDK Camera, API 24
- predicted color gains/transform — `STATISTICS_PREDICTED_COLOR_GAINS` / `_PREDICTED_COLOR_TRANSFORM` — **deprecated/`@hide`** in modern API `?`
- barcode/QR — **NOT camera metadata**; only `CONTROL_SCENE_MODE_BARCODE` (a tuning hint). Decode is downstream (`barcode` module) — source: NDK Camera, API 24
- **CameraX delta** — no first-class face/metadata API; read via `Camera2Interop` `CaptureResult` callbacks or `MlKitAnalyzer` (separate ML library, downstream) — source: reference/androidx/camera, CameraX 1.3

## egress (virtual camera publish)
> **Honest reality: a normal third-party app CANNOT register an OS-wide virtual camera that arbitrary other apps consume on stock Android.** Both publish paths are privileged/system-gated. (MEL-ENGINE-VII degrade-honestly: Android egress is local-only / unavailable to ordinary apps.)
- `android.companion.virtual.VirtualDeviceManager` — system service; `createVirtualDevice(int associationId, VirtualDeviceParams)` — source: reference/.../VirtualDeviceManager + AOSP, API 34
- `android.Manifest.permission.CREATE_VIRTUAL_DEVICE` — required; "only available to system apps holding specific roles"; also requires a CompanionDeviceManager (CDM) association — source: AOSP VirtualDeviceManager.java, API 34
- `VirtualDeviceManager.VirtualDevice.createVirtualCamera(VirtualCameraConfig)` → `VirtualCamera` — `@FlaggedApi(FLAG_VIRTUAL_CAMERA)`; needs `VirtualDeviceParams.POLICY_TYPE_CAMERA` = `DEVICE_POLICY_CUSTOM` — source: AOSP, API 35
- `android.companion.virtualdevice.camera.VirtualCameraConfig` (source pkg `android.companion.virtual.camera`) — **`@SystemApi @hide`**, NOT public SDK; `Builder.addStreamConfig(...)` / `setLensFacing(int)` / `setSensorOrientation(int)` / `setVirtualCameraCallback(Executor, VirtualCameraCallback)` / `build()` — source: AOSP VirtualCameraConfig.java, API 35
- `VirtualCamera` / `VirtualCameraCallback` / `VirtualCameraStreamConfig` — all `@SystemApi` — source: AOSP, API 35
- **scope:** a custom-policy virtual camera is visible ONLY to apps running in that virtual device's context (its associated virtual display), NOT to default-device apps. `CameraManager.getCameraIdList()` is scoped by `getDeviceId()` — source: AOSP CameraManager.java
- **DeviceAsWebcam** (UVC gadget, Android 14 QPR1+) — **system-image only, NOT app-controllable.** Service `com.android.deviceaswebcam`, gated by system property `ro.usb.uvc.enabled` (OEM-set); NO `PackageManager.FEATURE_*` for it; activated via Settings/`adb shell svc usb setFunctions uvc`; uses `SCALER_AVAILABLE_STREAM_USE_CASES_VIDEO_CALL`. Consumers are USB hosts, not Android apps — source: source.android.com/docs/core/camera/webcam
- **bottom line:** for an ordinary signed-but-unprivileged app, registering an OS-wide virtual camera is **not possible** — must fall back to local-only / in-process synthetic source (matches the charter's honest-fallback)

## OS integration
- `android.permission.CAMERA` — runtime, protection level **dangerous**; `<uses-permission android:name="android.permission.CAMERA"/>` — source: reference/android/Manifest.permission, API 1
- `Activity.requestPermissions(String[],int)` / `shouldShowRequestPermissionRationale(String)` — source: reference, API 23
- `AppOpsManager.OPSTR_CAMERA = "android:camera"` — source: AOSP AppOpsManager.java, API 23
- `<uses-feature>` strings: `android.hardware.camera`(API7), `android.hardware.camera.any`(`FEATURE_CAMERA_ANY`,API17), `android.hardware.camera.front`(API9), `android.hardware.camera.external`(`FEATURE_CAMERA_EXTERNAL`,API20), `android.hardware.camera.autofocus`(API7), `android.hardware.camera.flash`(API7), `android.hardware.camera.level.full`(`FEATURE_CAMERA_LEVEL_FULL`,API24), `android.hardware.camera.capability.raw`(`FEATURE_CAMERA_CAPABILITY_RAW`,API24), `android.hardware.camera.capability.manual_sensor`(`FEATURE_CAMERA_CAPABILITY_MANUAL_SENSOR`,API24), `android.hardware.camera.concurrent`(`FEATURE_CAMERA_CONCURRENT`,API30) — source: AOSP PackageManager / uses-feature docs
- foreground service — `android.permission.FOREGROUND_SERVICE_CAMERA`(API34) + `android:foregroundServiceType="camera"` (`ServiceInfo.FOREGROUND_SERVICE_TYPE_CAMERA`, API29); background camera open is otherwise blocked — source: reference, API 29/34
- arbitration — `CameraDevice.StateCallback.onError(CameraDevice,int)` with `ERROR_CAMERA_IN_USE`(1)/`ERROR_MAX_CAMERAS_IN_USE`(2)/`ERROR_CAMERA_DISABLED`(3)/`ERROR_CAMERA_DEVICE`(4)/`ERROR_CAMERA_SERVICE`(5); `onDisconnected(CameraDevice)` fires when evicted by a higher-priority client; NDK `ACameraDevice_StateCallbacks.onError`/`onDisconnected` + `ACAMERA_ERROR_CAMERA_DISCONNECTED` — source: reference/.../CameraDevice.StateCallback + NDK, API 21/24
- camera access priority (API 34) — foreground/top apps win; a device unavailable to a lower-priority background client can still open for a higher-priority caller; signaled via `AvailabilityCallback.onCameraAccessPrioritiesChanged()` — source: AOSP CameraManager.java, API 33/34
- privacy indicators — Android 12 green camera/mic dot; `SensorPrivacyManager` (`@SystemApi @hide`) with `Sensors.CAMERA`/`.MICROPHONE`, `TOGGLE_TYPE_SOFTWARE`/`_HARDWARE`, `supportsSensorToggle(int)`; when the camera toggle is OFF the framework reports cameras unavailable (`onCameraUnavailable`) and `openCamera` fails — apps only OBSERVE, cannot read/flip — source: reference/.../SensorPrivacyManager, API 31
- orientation — `ACAMERA_SENSOR_ORIENTATION` (0/90/180/270 clockwise vs natural); `Surface.ROTATION_0/_90/_180/_270` for display transform; front mirroring is an app-applied display convention (no camera2 mirror key) — source: NDK Camera / reference/android/view/Surface, API 21/24/1
- shutter sound — `MediaActionSound` with `SHUTTER_CLICK`(0)/`FOCUS_COMPLETE`(1)/`START_VIDEO_RECORDING`(2)/`STOP_VIDEO_RECORDING`(3), `load(int)`/`play(int)`; camera2 has NO API to disable it; regional mandatory-shutter enforcement is below the API (legacy Camera1 exposed `CameraInfo.canDisableShutterSound`/`enableShutterSound(boolean)`) — source: reference/android/media/MediaActionSound, API 16
- thermal — `PowerManager.getThermalHeadroom(int forecastSeconds)`(API30, 0.0→1.0 at ≈SEVERE), `addThermalStatusListener(OnThermalStatusChangedListener)`/`getCurrentThermalStatus()`(API29), `THERMAL_STATUS_NONE`(0)/`LIGHT`(1)/`MODERATE`(2)/`SEVERE`(3)/`CRITICAL`(4)/`EMERGENCY`(5)/`SHUTDOWN`(6); NDK `android/thermal.h`: `AThermal_acquireManager`/`_releaseManager`(API30), `AThermal_getCurrentThermalStatus`(API30), `AThermal_getThermalHeadroom`(API31), `ATHERMAL_STATUS_*` — source: reference/android/os/PowerManager + NDK thermal, API 29/30/31
- zero-shutter-lag — `ACAMERA_CONTROL_ENABLE_ZSL` + `TEMPLATE_ZERO_SHUTTER_LAG` + PRIVATE_REPROCESSING — source: NDK Camera, API 24

## obscure corners
- `ACAMERA_REQUEST_AVAILABLE_CAPABILITIES_SYSTEM_CAMERA` — privileged "system camera" only enumerable with a system permission; ordinary apps never see these ids `?` — source: NDK Camera, API 29
- `ACAMERA_AUTOMOTIVE_LOCATION` (`_INTERIOR`/`_EXTERIOR_*`/`_EXTRA_*`) + `ACAMERA_AUTOMOTIVE_LENS_FACING` — automotive camera placement keys (rear-view/surround) — source: NDK Camera enum `acamera_automotive_*`, API 31
- `ACAMERA_SHARED_SESSION_*` — shared-session keys (multi-client shared camera session) appear in the master NDK header — source: AOSP NdkCameraMetadataTags.h `?` (very new, API 36 `?`)
- `ACAMERA_DESKTOP_EFFECTS` — section present in the master NDK header (desktop/Chromebook camera-effects) — source: AOSP NdkCameraMetadataTags.h `?`
- `ACAMERA_DEMOSAIC` / `ACAMERA_QUIRKS` / `ACAMERA_LED` / `ACAMERA_REQUEST_METADATAMODE` / `ACAMERA_REQUEST_FRAME_COUNT` / `ACAMERA_REQUEST_TYPE` — legacy/`@hide`/HAL-internal sections retained in the tag enum but not app-facing `?` — source: AOSP NdkCameraMetadataTags.h
- `ACAMERA_SENSOR_REFERENCE_ILLUMINANT1`/`2`, `_CALIBRATION_TRANSFORM1`/`2`, `_COLOR_TRANSFORM1`/`2`, `_FORWARD_MATRIX1`/`2`, `_NEUTRAL_COLOR_POINT`, `_NOISE_PROFILE`, `_GREEN_SPLIT`, `_PROFILE_HUE_SAT_MAP`, `_PROFILE_TONE_CURVE`, `_BLACK_LEVEL_PATTERN`, `_DYNAMIC_BLACK_LEVEL`, `_DYNAMIC_WHITE_LEVEL` — full DNG/RAW characterization metadata (consumed by `DngCreator`) — source: NDK Camera, API 24
- `ACAMERA_SCALER_DEFAULT_SECURE_IMAGE_SIZE` / `ACAMERA_SCALER_RAW_CROP_REGION` (CROPPED_RAW use case) — source: NDK Camera, API 29/34 `?`
- `ACAMERA_HEIC_INFO_SUPPORTED` / `ACAMERA_HEIC_INFO_MAX_JPEG_APP_SEGMENTS_COUNT` — HEIC encoder limits — source: NDK Camera, API 30 `?`
- `ACAMERA_CONTROL_AVAILABLE_HIGH_SPEED_VIDEO_CONFIGURATIONS` (+`_MAXIMUM_RESOLUTION`) — constrained-high-speed fps×size tuples — source: NDK Camera, API 24/31
- 10-bit HDR detail — `REQUEST_AVAILABLE_DYNAMIC_RANGE_PROFILES_MAP` → `DynamicRangeProfiles`: `STANDARD`/`HLG10`/`HDR10`/`HDR10_PLUS`/`DOLBY_VISION_10B_HDR_REF`/`_REF_PO`/`_OEM`/`_OEM_PO`/`DOLBY_VISION_8B_HDR_REF`/`_REF_PO`/`_OEM`/`_OEM_PO`; `getProfileCaptureRequestConstraints(long)`, `isExtraLatencyPresent(long)`; `OutputConfiguration.setDynamicRangeProfile(long)`; `REQUEST_RECOMMENDED_TEN_BIT_DYNAMIC_RANGE_PROFILE` — source: AOSP DynamicRangeProfiles.java, API 33
- color-space — `REQUEST_AVAILABLE_COLOR_SPACE_PROFILES_MAP` → `ColorSpaceProfiles` (`getSupportedColorSpaces(int)`, `getSupportedDynamicRangeProfiles(ColorSpace.Named,int)`); `SessionConfiguration.setColorSpace(ColorSpace.Named)`; `ColorSpace.Named.SRGB/DISPLAY_P3/BT2020_HLG/BT2020_PQ/...` — source: AOSP ColorSpaceProfiles.java, API 34
- `CameraX` `DynamicRange.SDR/HLG_10_BIT/HDR10_10_BIT/HDR10_PLUS_10_BIT/DOLBY_VISION_10_BIT/DOLBY_VISION_8_BIT/UNSPECIFIED` + `VideoCapture.Builder.setDynamicRange(DynamicRange)` + `MirrorMode.MIRROR_MODE_OFF/ON/ON_FRONT_ONLY` `?` — source: reference/androidx/camera/core/DynamicRange, CameraX 1.3
- `CameraX` `Camera2CameraInfo.getCameraCharacteristic(CameraCharacteristics.Key)` / `getCameraId()` / `from(CameraInfo)` — the only path to the full characteristics catalog from CameraX — source: reference/androidx/camera/camera2/interop/Camera2CameraInfo, CameraX 1.3
