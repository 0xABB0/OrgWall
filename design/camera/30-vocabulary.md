# camera — capability vocabulary
> mutable till freeze (P7); then APPEND-ONLY: new cap = new ID, never rename/reuse.
> the vocab — not any module list — is the real contract. fineness is load-bearing: every ID classifies to ONE status per axis in P4; a cell wanting "native, but…" means the ID is too coarse → split, never annotate.

Conventions
- IDs are axis-neutral + transport-neutral. a control's *binding* (UVC-control-transfer vs VISCA-IP vs GenICam-register) is a provider detail, never in the ID — this is what makes a future VISCA/NDI-PTZ transport additive (charter).
- `normalizes` axis keys: `avf` (macos+ios) · `camera2` (android camera2-NDK + CameraX) · `libcamera` · `mf` (win32 MF+WinRT) · `pipewire` · `v4l2` · `web` · `uvc` (uvc-direct) · `genicam`.
- `[CEILING]` = no consumer-OS backing; pure hardware-standard/industrial ceiling. classifies `deny` on all consumer axes, `native` only via a future provider plane (MEL-ENGINE-I). collected in §13.
- `?` in a meaning = research-scheduled / existence-or-classification genuinely uncertain (never a guess).
- cross-area dedups + retained-overlaps recorded in §14. drops fold native entries into the canonical ID.

Areas: 1 devices&enum · 2 topology&streams · 3 fine-grained control · 4 mechanical (PTZ) · 5 capture modes · 6 depth/3D/calib · 7 live effects · 8 frame memory · 9 timing · 10 metadata · 11 egress + test ingest · 12 OS integration · 13 CEILING index · 14 reconciliation.

---

## 1. devices & enumeration — `cap.device.*` / `cap.enum.*`

| capability id | meaning (≤1 line) | normalizes |
|---|---|---|
| `cap.enum.list` | Enumerate currently-available capture devices | avf:`AVCaptureDeviceDiscoverySession`; camera2:`ACameraManager_getCameraIdList`/CameraX`getAvailableCameraInfos`; libcamera:`CameraManager::cameras()`; mf:`MFEnumDeviceSources`/`DeviceInformation.FindAllAsync`/DShow; pipewire:`pw_registry`+`media.class=Video/Source`; v4l2:`/dev/videoN`+`VIDIOC_QUERYCAP`; web:`enumerateDevices`; genicam:`TLOpenInterface`/`IFOpenDevice`/GVCP |
| `cap.device.id.stable` | Persistent per-device unique identifier | avf:`uniqueID`; camera2:camera-id; libcamera:`Camera::id()`; mf:`SYMBOLIC_LINK`/`DeviceInformation.Id`; pipewire:`node.name`; v4l2:bus_info/sysfs; web:`deviceId`(origin-opaque); genicam:`DeviceSerialNumber`/`DeviceUserID` |
| `cap.device.name.human` | Human-readable display name | avf:`localizedName`; libcamera:`properties::Model`; mf:`FriendlyName`; pipewire:`node.description`; v4l2:`v4l2_capability.card`; web:`label`(empty pre-grant); genicam:`DeviceModelName` |
| `cap.device.vendor.info` | Manufacturer / model / firmware identity | avf:`manufacturer`/`modelID`; v4l2:`driver`/`bus_info`; genicam:`DeviceVendorName`/`DeviceVersion`/`DeviceFirmwareVersion`; uvc:USB VID/PID |
| `cap.device.transport.type` | Physical connection/bus transport | avf:`transportType`; mf:`VIDCAP_CATEGORY`; v4l2:`bus_info`; genicam:`DeviceTLType`(GigEVision/USB3Vision/CameraLink/CoaXPress); uvc:USB |
| `cap.device.class.physical` | Direct single-sensor physical camera | avf:`...WideAngle`/`...UltraWide`/`...Telephoto`; camera2:non-logical id; libcamera:Front/Back; mf:`KSCATEGORY_VIDEO_CAMERA`; web:`videoinput`; genicam:`DeviceType_Transmitter` |
| `cap.device.class.virtual` | Logical device fused from ≥2 constituents (device-class flag only; decomposition→§2) | avf:`isVirtualDevice`/`...Dual`/`...DualWide`/`...Triple`; camera2:`...LOGICAL_MULTI_CAMERA`/`isLogicalMultiCamera` |
| `cap.device.class.external.uvc` | External/USB-UVC webcam | avf:`...External`; camera2:`LENS_FACING_EXTERNAL`/`HARDWARE_LEVEL_EXTERNAL`; libcamera:`CameraLocationExternal`; v4l2:`CAMERA_ORIENTATION_EXTERNAL`+uvcvideo; uvc:entity model |
| `cap.device.class.capture_card` | HDMI/SDI capture-card source as camera | mf:`KSCATEGORY_CAPTURE`; v4l2:capture node+DV-timings; genicam:CameraLink/CoaXPress grabber `IF_HANDLE`; [beyond-os: DeckLink/AJA/Magewell] |
| `cap.device.class.continuity` | Phone-as-Mac-camera Continuity device | avf:`...ContinuityCamera`/`isContinuityCamera` |
| `cap.device.class.deskview` | Virtual overhead desk-view camera | avf:`...DeskViewCamera`/`companionDeskViewCamera` |
| `cap.device.class.depth_sensor` | Dedicated depth/IR-pair sensor device class | avf:`...TrueDepth`/`...LiDARDepthCamera`; mf:`KSCATEGORY_SENSOR_CAMERA`; v4l2:depth-format node |
| `cap.device.class.machine_vision` | Industrial GenICam/Vision device `[CEILING]` | genicam:`DEV_HANDLE`/`DeviceType`/`DeviceScanType` |
| `cap.device.class.software` | A published/loopback OS software virtual camera consumed as a first-class ingest source (closes the egress↔ingest loop — distinct from `.virtual` which is multi-lens fusion) | avf:CMIO-extension/published vcam as `AVCaptureDevice`; mf:DShow-enumerated virtual cam / `MFCreateVirtualCamera` output; v4l2:v4l2loopback node; pipewire:virtual source node; web:OS vcam via `getUserMedia` |
| `cap.device.facing` | Front/back/unspecified lens facing | avf:`position`; camera2:`LENS_FACING`; libcamera:`properties::Location`; v4l2:`CAMERA_ORIENTATION`; web:`facingMode` |
| `cap.device.mount.rotation` | Fixed mount/sensor rotation relative to scene | camera2:`SENSOR_ORIENTATION`; libcamera:`properties::Rotation`; v4l2:`CAMERA_SENSOR_ROTATION` |
| `cap.device.sensor.geometry` | Physical sensor pixel-array + active areas | camera2:`SENSOR_INFO_ACTIVE_ARRAY_SIZE`/`_PIXEL_ARRAY_SIZE`; libcamera:`PixelArraySize`/`PixelArrayActiveAreas`; genicam:`SensorWidth`/`Height`/`WidthMax` |
| `cap.device.sensor.pixel_pitch` | Physical pixel/unit-cell size | camera2:`SENSOR_INFO_PHYSICAL_SIZE`; libcamera:`UnitCellSize`; genicam:`SensorPixelWidth`; avf:`pixelSize` |
| `cap.device.sensor.cfa` | Color-filter-array / Bayer layout | camera2:`SENSOR_INFO_COLOR_FILTER_ARRANGEMENT`; libcamera:`ColorFilterArrangement`; genicam:`PixelColorFilter`; v4l2:Bayer pixfmt |
| `cap.device.sensor.scan_type` | Areascan vs linescan topology `[CEILING]` | genicam:`DeviceScanType` |
| `cap.device.sensor.white_level` | Sensor white-level / DR bounds | camera2:`SENSOR_INFO_WHITE_LEVEL`; genicam:`PixelDynamicRangeMin`/`Max` |
| `cap.device.lens.aperture.fixed` | Reported lens aperture (f-number) characteristic | avf:`lensAperture`; camera2:`LENS_INFO_AVAILABLE_APERTURES`; genicam:`Aperture`/`NumericalAperture` |
| `cap.device.lens.focal_length` | Available/native focal length(s) | camera2:`LENS_INFO_AVAILABLE_FOCAL_LENGTHS`; genicam:`FocalLength`; mf:KS`FOCAL_LENGTH`; uvc:`CT`/MS-XU |
| `cap.device.lens.min_focus_distance` | Closest focusable distance | avf:`minimumFocusDistance`; camera2:`LENS_INFO_MINIMUM_FOCUS_DISTANCE`/`_HYPERFOCAL_DISTANCE` |
| `cap.device.mic.association` | Correlate a camera with its companion mic | avf:via uniqueID/transportType (no explicit accessor `?`); mf:`MediaFrameSourceGroup`/`AUDCAP_ENDPOINT_ID`; web:`groupId`; v4l2/uvc:shared USB parent only |
| `cap.device.hardware_level` | Coarse capability tier of device | camera2:`INFO_SUPPORTED_HARDWARE_LEVEL`(LEGACY/LIMITED/FULL/3/EXTERNAL); genicam:`DeviceSFNCVersion` |
| `cap.enum.caps.per_device` | Query one device's static caps without opening | camera2:`getCameraCharacteristics`; libcamera:`Camera::properties()`/`controls()`; web:`InputDeviceInfo.getCapabilities`; v4l2:`QUERYCAP`+ctrl walk; genicam:GenApi node map; mf:device attributes |
| `cap.enum.media_types` | Device declares supported media types | avf:`hasMediaType:`; mf:`FRAMESOURCE_TYPES`(Color/IR/Depth); v4l2:`device_caps`; web:`kind` |
| `cap.enum.feasibility.query_closed` | Test config/combo feasible WITHOUT opening device | camera2:`CameraDeviceSetup.getSessionCharacteristics`/`isSessionConfigurationWithParametersSupported`; genicam:node-map availability pre-acquisition |
| `cap.enum.feasibility.query_open` | Ask open device whether a stream-set co-operates | camera2:`isSessionConfigurationSupported`; web:`track.getCapabilities`; avf:Format introspection |
| `cap.enum.feasibility.combination_tables` | Vendor-declared guaranteed-compatible tuples | avf:`AVCaptureDeviceFormat`/`supportedDepthDataFormats`; camera2:`SCALER_MANDATORY_STREAM_COMBINATIONS`(+concurrent/max-res/ten-bit variants); mf:`MediaCaptureVideoProfile`/`KSCAMERAPROFILE` |
| `cap.enum.feasibility.try_adjust` | Submit config; engine returns nearest achievable | libcamera:`CameraConfiguration::validate()`→`Valid`/`Adjusted`/`Invalid` |
| `cap.enum.feasibility.silent_clamp` | Set/try mutates fields silently to fit (trap) | v4l2:`TRY_FMT`/`S_PARM` step-rounding; mf:WinRT`.Supported` post-preview-only |
| `cap.enum.feasibility.constraints` | Constraint-satisfaction w/ hard reject | web:`MediaTrackConstraints`(`exact`/`min`/`max`/`ideal`)→`OverconstrainedError`(`.constraint`) |
| `cap.enum.feasibility.per_control_range` | Per-control range/step/default reported honestly | avf:Format min/max; camera2:`*_AVAILABLE_*`/`*_INFO_*`; libcamera:`ControlInfo`; mf:`GetRange`; pipewire:`SPA_CHOICE_Range`/`_Step`; v4l2:`QUERYCTRL`/`QUERYMENU`; web:`getCapabilities`{min,max,step}; genicam:`GetMin`/`Max`/`Increment` |
| `cap.enum.feasibility.cost_budget` | Combo gated by a thermal/power/hardware cost ledger | avf:`AVCaptureMultiCamSession.hardwareCost`/`systemPressureCost`(<1.0) |
| `cap.enum.concurrent_sets` | Which devices can stream simultaneously | avf:`supportedMultiCamDeviceSets`/`isMultiCamSupported`; camera2:`getConcurrentCameraIds`/`isConcurrentSessionConfigurationSupported`/`FEATURE_CAMERA_CONCURRENT` |
| `cap.enum.include_hidden` | Reveal OS-hidden virtual/screen/software cameras in enumeration (off by default — silent-default trap, MEL-CODE-007) | avf:CMIO `kCMIOHardwarePropertyAllowScreenCaptureDevices=1`; mf:DShow enumeration (MF hides virtual cams); v4l2/pipewire:nodes already visible |
| `cap.device.access.raw_under_os` | Reach device controls under the high-level OS API (transport-neutral) | uvc:`UVCIOC_CTRL_QUERY`/`IKsControl::KsProperty`/IOKit·`(unitID,selector,request)`·`GET_INFO`/`GET_LEN`; v4l2:`UVCIOC_CTRL_MAP`; mf:`PROPSETID_VIDCAP_EXTENSION_UNIT` (deny ios/web) |
| `cap.device.access.extension_unit` | Vendor GUID-addressed extension-unit controls | uvc:XU(16-byte GUID)/`MS_CAMERA_CONTROL_XU`; v4l2:`UVCIOC_CTRL_MAP`; mf:`KSPROPERTYSETID_ExtendedCameraControl`; genicam:vendor-XML node |
| `cap.device.access.sensor_register` | Direct R/W of a bare sensor's control registers (I²C/SPI/SCCB) — the bare-metal analog of raw_under_os, no OS to go under | baresensor:I²C/SCCB register map (exposure/gain/window/binning/PLL regs)·esp32`set_reg`/`get_reg`·v4l2`VIDIOC_DBG_S/G_REGISTER`; genicam:`GCReadPort`/`GCWritePort` (register-map analog); [deny on OS HALs — they own the sensor] |
| `cap.device.bringup` | Host-driven sensor bring-up: generate the master clock (XCLK), configure the PLL/clock-tree, run the PWDN/RESET power-up sequence | baresensor:MCU-generated XCLK (PWM/LEDC)·sensor PLL register tree·datasheet-ordered PWDN/RESET sequencing; [deny on every OS HAL — the OS owns sensor bring-up] |
| `cap.device.hotplug` | Arrival/removal notification (device-list refresh side) | avf:`WasConnected`/`WasDisconnected`; camera2:`registerAvailabilityCallback`; libcamera:`cameraAdded`/`Removed`; mf:`DeviceWatcher`/`WM_DEVICECHANGE`; pipewire:`global`/`global_remove`; v4l2:udev; web:`devicechange`; genicam:GVCP re-scan |
| `cap.device.connected_state` | Whether a device is currently present | avf:`isConnected`; web:`readyState`/`ended`; genicam:`DeviceConnectionStatus`/`DeviceAccessStatus`; mf:`DEVICE_INVALIDATED` |

---

## 2. topology & streams — `cap.topology.*` / `cap.stream.*`

| capability id | meaning (≤1 line) | normalizes |
|---|---|---|
| `cap.topology.session.graph` | Explicit input→connection→output capture-graph wiring | avf:`AVCaptureSession`/`AVCaptureConnection`/`addInputWithNoConnections:`; camera2:`SessionConfiguration`/`OutputConfiguration`; mf:`IMFCaptureEngine`+sinks; pipewire:`pw_stream_connect`; v4l2:`S_FMT`; genicam:`DevOpenDataStream`; web:`MediaStream`/`addTrack` |
| `cap.topology.multicam.concurrent` | ≥2 cameras streaming in one coordinated session | avf:`AVCaptureMultiCamSession`; camera2:`getConcurrentCameraIds`/`FEATURE_CAMERA_CONCURRENT`/`ConcurrentCamera`; web:two-getUserMedia (limited) |
| `cap.topology.multicam.deviceset.enumerate` | Enumerate which device combos may run concurrently | avf:`supportedMultiCamDeviceSets`; camera2:`getConcurrentCameraIds`/`SCALER_MANDATORY_CONCURRENT_STREAM_COMBINATIONS` |
| `cap.topology.multicam.cost.hardware` | Reported hardware-budget fraction (gate <1.0) | avf:`AVCaptureMultiCamSession.hardwareCost` |
| `cap.topology.multicam.cost.systempressure` | Reported system-pressure cost (gate <1.0) | avf:`AVCaptureMultiCamSession.systemPressureCost` |
| `cap.topology.multicam.perstream.cap` | Per-stream res/rate ceiling under multicam | avf:`AVCaptureDeviceFormat.isMultiCamSupported`; camera2:`SCALER_MANDATORY_CONCURRENT_STREAM_COMBINATIONS` |
| `cap.topology.multicam.participation` | Whether a device/format may join multicam | avf:`AVCaptureDevice.isMultiCamSupported`; camera2:presence in concurrent-id sets |
| `cap.topology.multicam.control-independence` | Queryable: whether each camera in a multicam session accepts *independent* manual control (or is locked to shared/AE) | avf:`AVCaptureMultiCamSession` (manual not independently per-cam — a declared restriction); camera2:per-physical-camera requests (more independent); genicam:per-device independence |
| `cap.topology.logical.is` | Device is a logical cam of ≥2 physical constituents | avf:`isVirtualDevice`/`...Dual`/`...Triple`; camera2:`...LOGICAL_MULTI_CAMERA`/`isLogicalMultiCamera` |
| `cap.topology.logical.constituents.enumerate` | List physical sub-devices behind a logical cam | avf:`constituentDevices`; camera2:`LOGICAL_MULTI_CAMERA_PHYSICAL_IDS`; genicam:`SourceSelector`/`SourceCount` |
| `cap.topology.logical.constituent.ports` | Split a logical input into per-constituent ports | avf:`AVCaptureDeviceInput.ports(for:...)`/`AVCaptureInputPort.sourceDeviceType`; camera2:`OutputConfiguration.setPhysicalCameraId` |
| `cap.topology.logical.constituent.activephysical` | Report which constituent currently feeds the stream | avf:`primaryConstituentDevice`/`activePrimaryConstituentDevice`; camera2:`LOGICAL_MULTI_CAMERA_ACTIVE_PHYSICAL_ID` |
| `cap.topology.logical.switchover.zoomfactors` | Zoom factors at which a logical cam switches lens | avf:`virtualDeviceSwitchOverVideoZoomFactors` |
| `cap.topology.logical.switchover.behavior` | Configure lens-switchover regime + fallback set | avf:`setPrimaryConstituentDeviceSwitchingBehavior:`/`supportedFallbackPrimaryConstituentDevices` |
| `cap.topology.logical.switchover.lock.recording` | Lock constituent switchover during a recording | avf:`AVCaptureMovieFileOutput.isPrimaryConstituentDeviceSwitchingBehaviorForRecordingEnabled` |
| `cap.topology.logical.sensor.synctype` | Genlock quality between a logical cam's sensors | camera2:`LOGICAL_MULTI_CAMERA_SENSOR_SYNC_TYPE`(Approximate/Calibrated) |
| `cap.topology.physical.hotplug` | Per-physical-sub-camera availability change | camera2:`registerExtendedAvailabilityCallback`(onPhysicalCameraAvailable/Unavailable) |
| `cap.stream.multi.concurrent` | Multiple output streams off one camera at once | avf:multiple outputs; camera2:multi-Surface/`REQUEST_MAX_NUM_OUTPUT_STREAMS`; libcamera:multiple `StreamConfiguration`; mf:preview+record+photo sinks; v4l2:media-controller/multi-node; genicam:multiple DataStream; web:multiple tracks |
| `cap.stream.multi.perstream.format` | Each concurrent stream carries its own format | avf:per-output `videoSettings`; camera2:per-`OutputConfiguration`; libcamera:per-`StreamConfiguration`; mf:per-sink `IMFMediaType`; genicam:`PixelFormat`/`Width[RegionSelector]`; web:per-track constraints |
| `cap.stream.multi.perstream.usecase` | Tag each stream with its role | avf:`AVCaptureSessionPreset`/output classes; camera2:`setStreamUseCase`/`SCALER_AVAILABLE_STREAM_USE_CASES`; libcamera:`StreamRole`; mf:`SINK_TYPE_*`/`KSCAMERAPROFILE_VideoConferencing` |
| `cap.stream.multi.streamcount.ceiling` | Reported max simultaneous stream count | camera2:`REQUEST_MAX_NUM_OUTPUT_STREAMS`/`_INPUT_STREAMS`; genicam:`DeviceStreamChannelCount` |
| `cap.stream.share.fanout` | One camera stream fed to multiple consumers | camera2:`enableSurfaceSharing`/`StreamSharing`; avf:port on multiple connections; web:`track.clone()`; genicam:`RegionDestination` |
| `cap.stream.multiresolution` | One logical stream delivering multiple resolutions | camera2:`MultiResolutionImageReader`/`SCALER_MULTI_RESOLUTION_STREAM_*` |
| `cap.stream.source.multivs` | One device exposing multiple pins of differing kind | uvc:multi-VideoStreaming/`VS_PROBE`/`VS_COMMIT`; mf:`FRAMESOURCE_TYPES`/`MediaFrameSourceGroup`; camera2:physical-stream split; genicam:`SourceSelector`/`ComponentSelector`; v4l2:per-node |
| `cap.stream.source.kind.classify` | Per-stream source-kind classification (color/IR/depth) | mf:`MediaFrameSourceKind`; avf:`AVCaptureInputPort.mediaType`; genicam:`ComponentSelector`(Intensity/Range/Confidence/Infrared) |
| `cap.stream.source.format_change_event` | Source signal-format auto-detect + change event (resolution/fps/format changes *underneath* a live session — distinct from hot-plug) | v4l2:`V4L2_EVENT_SOURCE_CHANGE`+`QUERY_DV_TIMINGS`; mf:capture-engine format-change; [capture-card] DeckLink `bmdVideoInputEnableFormatDetection`→`VideoInputFormatChanged`; [consumer cams negotiate fixed format] |
| `cap.stream.sync.timealigned` | Time-aligned delivery of multiple outputs as one set | avf:`AVCaptureDataOutputSynchronizer`; mf:`MultiSourceMediaFrameReader`; genicam:GenDC multi-component |
| `cap.stream.sync.clock.port` | Per-port capture clock exposed for cross-output sync | avf:`AVCaptureInputPort.clock`(CMClock); uvc:`dwClockFrequency`; genicam:`DeviceClockSelector`/`DeviceClockFrequency` |
| `cap.stream.reconfigure.live` | Change topology/format mid-session without teardown | avf:`beginConfiguration`/`commitConfiguration`; camera2:request-swap/deferred surface; mf:`SetCurrentMediaType`; uvc:`VS_PROBE`/`COMMIT` renegotiate; web:`applyConstraints`; (libcamera/genicam: none/locked) |
| `cap.stream.reconfigure.deferred.surface` | Add an output surface to a live session post-creation | camera2:`OutputConfiguration(Size,Class)`+`addSurface`-later |
| `cap.stream.reconfigure.params.session` | Session-wide params held constant across stream set | camera2:`setSessionParameters`; genicam:`TLParamsLocked`/`TLParamsLockedSelector` |
| `cap.stream.acquisition.mode` | Single/multi(N)/continuous acquisition selection | genicam:`AcquisitionMode`/`AcquisitionFrameCount`; camera2:capture vs repeating; avf:still-vs-video outputs |
| `cap.stream.transfer.usercontrolled` | User-paced transfer of frames out of device buffer `[CEILING]` | genicam:`TransferControl`(`TransferStart`/`Stop`/`Pause`/`Resume`/`TransferQueueMaxBlockCount`) |
| `cap.stream.channel.transport.config` | Configure the physical transport link — GigE channel (packet/zones) · CSI-2 lanes/link-freq/virtual-channel/data-type demux · parallel-DVP bus-width/sync | genicam:`GevStreamChannelSelector`/`GevSCPSPacketSize`/`GevSCZoneCount`; baresensor:CSI-2 lane-count/`LINK_FREQ`/VC/DT·DVP bus-width/PCLK-edge/HSYNC-VSYNC-vs-embedded-sync (broadened from genicam-only `[CEILING]` — native on baresensor) |

---

## 3. fine-grained control — `cap.control.*`
Transport-neutral throughout (a future VISCA/IP control transport exposes the same IDs). Optical zoom + iris live in §4/aperture; see §14.

### 3a. plumbing
| capability id | meaning (≤1 line) | normalizes |
|---|---|---|
| `cap.control.lock.hardware-config` | Acquire exclusive config-write access before setters | avf:`lockForConfiguration:`; v4l2:`G/S_PRIORITY`+EBUSY; genicam:`TLParamsLocked`; uvc:`GET_INFO` SET-validity |
| `cap.control.apply.per-frame` | A control value attaches to a specific submitted frame | camera2:`CaptureRequest` keys; genicam:`SequencerControl`; v4l2:`MEDIA_IOC_REQUEST_ALLOC`+`REQUEST_FD`; libcamera:`Request::controls()`; web:`applyConstraints` |
| `cap.control.apply.applied-value-feedback` | Read back which value the device actually used per frame | camera2:`CaptureResult`; libcamera:`Request::metadata()`; mf:`MF_CAPTURE_METADATA_*`; genicam:`Chunk*` (→§10 owns the per-quantity split) |
| `cap.control.catalog.introspect` | Enumerate honored controls + range/step/default | camera2:`*_AVAILABLE_*`; libcamera:`ControlInfo`; v4l2:`QUERYCTRL`/`QUERYMENU`; mf:`GetRange`; web:`getCapabilities()`; genicam:`GetMin/Max/Increment`; uvc:`GET_MIN/MAX/RES/DEF` |
| `cap.control.catalog.vendor-passthrough` | Reach a vendor/extension control not in the std catalog | v4l2:`UVCIOC_CTRL_MAP`/`_QUERY`; uvc:XU; mf:`IKsControl`; pipewire:`SPA_PROP_START_CUSTOM`; genicam:vendor-XML |

### 3b. focus
| capability id | meaning (≤1 line) | normalizes |
|---|---|---|
| `cap.control.focus.af-mode` | Continuous / single-shot / locked AF regime | avf:`focusMode`; camera2:`CONTROL_AF_MODE`; libcamera:`AfMode`; v4l2:`FOCUS_AUTO`; mf:`EXTENDED_FOCUSMODE`; web:`focusMode`; uvc:`CT_FOCUS_AUTO`; genicam:`FocusAuto` |
| `cap.control.focus.af-trigger` | Fire a one-shot AF scan | camera2:`AF_TRIGGER`; libcamera:`AfTrigger`; v4l2:`AUTO_FOCUS_START`/`_STOP`; web:`"single-shot"` |
| `cap.control.focus.af-range-restriction` | Restrict AF search (near/macro/normal/far/infinity) | avf:`autoFocusRangeRestriction`; camera2:`AF_MODE_MACRO`; libcamera:`AfRange`; v4l2:`AUTO_FOCUS_RANGE`; uvc:`CT_FOCUS_SIMPLE` |
| `cap.control.focus.af-speed` | Continuous-AF transition speed | libcamera:`AfSpeed`; avf:`isSmoothAutoFocusEnabled` |
| `cap.control.focus.af-pause` | Pause/resume continuous-AF without dropping mode | libcamera:`AfPause`(Immediate/Deferred/Resume) |
| `cap.control.focus.region` | Point/rect metering ROI for AF | avf:`focusPointOfInterest`/`focusRectOfInterest`; camera2:`AF_REGIONS`; libcamera:`AfMetering`+`AfWindows`; mf:`ROI_ISPCONTROL`; web:`pointsOfInterest`; uvc:`CT_REGION_OF_INTEREST` |
| `cap.control.focus.manual-lens-position-normalized` | Manual focus by normalized 0..1 lens position | avf:`setFocusModeLockedWithLensPosition:`; libcamera:`LensPosition` |
| `cap.control.focus.manual-distance-diopters` | Manual focus by focus distance in diopters | camera2:`LENS_FOCUS_DISTANCE`; libcamera:`LensPosition` |
| `cap.control.focus.manual-distance-meters` | Manual focus by focus distance in meters | web:`focusDistance` |
| `cap.control.focus.manual-position-absolute-units` | Manual focus by raw absolute device units | v4l2:`FOCUS_ABSOLUTE`; uvc:`CT_FOCUS_ABSOLUTE`; mf:`Focus`/`EXTENDED_FOCUS` |
| `cap.control.focus.manual-relative` | Relative/stepper focus move | v4l2:`FOCUS_RELATIVE`; uvc:`CT_FOCUS_RELATIVE`; genicam:`FocusStepper` |
| `cap.control.focus.range-bounds` | Min/hyperfocal focus distance readout | avf:`minimumFocusDistance`; camera2:`LENS_INFO_*`; genicam:`ObjectSensorDistance` |
| `cap.control.focus.distance-calibration-quality` | Uncalibrated/approximate/calibrated accuracy | camera2:`LENS_INFO_FOCUS_DISTANCE_CALIBRATION` |
| `cap.control.focus.face-driven` | Bias AF onto detected faces | avf:`isFaceDrivenAutoFocusEnabled`; mf:`EXTENDED_FACEDETECTION`(AF) |
| `cap.control.focus.hunting-state` | Lens hunting / motion state | avf:`isAdjustingFocus`; camera2:`AF_STATE`/`LENS_STATE`; libcamera:`AfState`/`LensState`; v4l2:`AUTO_FOCUS_STATUS`; mf:`FOCUSSTATE` |
| `cap.control.focus.scene-change` | AF detected a scene change warranting refocus | camera2:`AF_SCENE_CHANGE`; avf:`subjectAreaDidChange` |
| `cap.control.focus.priority` | Allow/deny AF blocking the capture | mf:`EXTENDED_FOCUSPRIORITY`; camera2:`AF_MODE_EDOF` |
| `cap.control.focus.figure-of-merit` | Focus quality figure-of-merit readout | libcamera:`FocusFoM` |
| `cap.control.focus.ramp-with-rate` | Smooth manual focus *pull* to a target lens-position at a specified rate | [emulate-everywhere — framework steps `cap.control.focus.manual-*` at a rate; no single-call native, cf. zoom's `cap.control.zoom.ramp-with-rate`] |

### 3c. exposure / ISO / gain
| capability id | meaning (≤1 line) | normalizes |
|---|---|---|
| `cap.control.exposure.ae-mode` | AE regime (auto/locked/custom/off) | avf:`exposureMode`; camera2:`CONTROL_AE_MODE`/`CONTROL_MODE`; libcamera:`AeEnable`/`ExposureTimeMode`; v4l2:`EXPOSURE_AUTO`; mf:`EXTENDED_EXPOSUREMODE`; web:`exposureMode`; uvc:`CT_AE_MODE`; genicam:`ExposureAuto` |
| `cap.control.exposure.ae-lock` | Freeze current AE result | avf:`exposureMode=Locked`; camera2:`AE_LOCK`; v4l2:`3A_LOCK`(EXPOSURE); web:`"manual"` |
| `cap.control.exposure.ae-region` | Point/rect metering ROI for AE | avf:`exposurePointOfInterest`; camera2:`AE_REGIONS`; mf:`ROI_ISPCONTROL`; web:`pointsOfInterest`; uvc:`CT_REGION_OF_INTEREST` |
| `cap.control.exposure.ae-metering-mode` | Average/center/spot/matrix metering pattern | libcamera:`AeMeteringMode`; v4l2:`EXPOSURE_METERING` |
| `cap.control.exposure.ae-constraint-mode` | Highlight/shadow constraint bias | libcamera:`AeConstraintMode` |
| `cap.control.exposure.ae-trade-bias` | Exposure-vs-gain trade preference | libcamera:`AeExposureMode` |
| `cap.control.exposure.compensation-ev` | Exposure target bias in EV stops | avf:`exposureTargetBias`; camera2:`AE_EXPOSURE_COMPENSATION`; libcamera:`ExposureValue`; v4l2:`AUTO_EXPOSURE_BIAS`; mf:`EXTENDED_EVCOMPENSATION`; web:`exposureCompensation`; uvc:MSXU EV |
| `cap.control.exposure.metered-offset` | Metered EV offset from AE target (readback) | avf:`exposureTargetOffset` |
| `cap.control.exposure.manual-time` | Manual shutter/exposure time | avf:`setExposureModeCustomWithDuration:`; camera2:`SENSOR_EXPOSURE_TIME`; libcamera:`ExposureTime`; v4l2:`EXPOSURE_ABSOLUTE`; mf:`ExposureControl`; web:`exposureTime`; uvc:`CT_EXPOSURE_TIME_ABSOLUTE`; genicam:`ExposureTime` |
| `cap.control.exposure.manual-time-relative` | Relative exposure-time step | uvc:`CT_EXPOSURE_TIME_RELATIVE` |
| `cap.control.exposure.ae-max-time-clamp` | Cap AE's longest permitted shutter | avf:`activeMaxExposureDuration` |
| `cap.control.exposure.priority-mode` | Partial-manual AE: pin sensitivity-or-time priority | camera2:`AE_PRIORITY_MODE`; v4l2:`EXPOSURE_SHUTTER_PRIORITY`/`_APERTURE_PRIORITY`; mf:KS`AUTO_EXPOSURE_PRIORITY`; uvc:`CT_AE_PRIORITY` |
| `cap.control.exposure.ae-frame-rate-tradeoff` | Allow AE to drop FPS for exposure | v4l2:`EXPOSURE_AUTO_PRIORITY`; mf:`EXTENDED_VFR` |
| `cap.control.exposure.ae-state` | AE convergence/searching/converged/flash-required | camera2:`AE_STATE`; libcamera:`AeState`; avf:`isAdjustingExposure` |
| `cap.control.exposure.ae-precapture-trigger` | Trigger an AE precapture metering sequence | camera2:`AE_PRECAPTURE_TRIGGER`; libcamera:`draft::AePrecaptureTrigger` |
| `cap.control.exposure.face-driven` | Bias AE onto detected faces | avf:`isFaceDrivenAutoExposureEnabled`; mf:`EXTENDED_FACEDETECTION`(AE) |
| `cap.control.exposure.range-bounds` | Min/max exposure-time readout | avf:`minExposureDuration`/`maxExposureDuration`; camera2:`SENSOR_INFO_EXPOSURE_TIME_RANGE`; genicam:Min/Max |
| `cap.control.exposure.ramp-with-rate` | Smooth manual exposure *pull* (time/ISO/EV) to a target at a specified rate | [emulate-everywhere — framework steps `cap.control.exposure.manual-*` at a rate] |
| `cap.control.iso.manual` | Manual ISO / sensitivity | avf:`...ISO:`; camera2:`SENSOR_SENSITIVITY`; v4l2:`ISO_SENSITIVITY`; mf:`IsoSpeedControl`/`EXTENDED_ISO_ADVANCED`; web:`iso` |
| `cap.control.iso.auto` | Auto-ISO toggle | v4l2:`ISO_SENSITIVITY_AUTO`; mf:`EXTENDED_ISO` |
| `cap.control.iso.analog-ceiling` | Max analog sensitivity before digital boost | camera2:`SENSOR_MAX_ANALOG_SENSITIVITY` |
| `cap.control.iso.post-raw-digital-boost` | Digital ISO boost applied post-RAW | camera2:`POST_RAW_SENSITIVITY_BOOST` |
| `cap.control.iso.range-bounds` | Min/max ISO readout | camera2:`SENSOR_INFO_SENSITIVITY_RANGE` |
| `cap.control.gain.analog-manual` | Manual analog sensor gain | camera2:`SENSOR_SENSITIVITY`(analog); libcamera:`AnalogueGain`; v4l2:`GAIN`; uvc:PU gain; genicam:`Gain[Analog]`; pipewire:`SPA_PROP_gain`; mf:`VideoProcAmp_Gain` |
| `cap.control.gain.analog-auto` | Analog AGC mode | v4l2:`AUTOGAIN`; genicam:`GainAuto`; libcamera:`AnalogueGainMode` |
| `cap.control.gain.digital-manual` | Digital (ISP) gain | camera2:digital; libcamera:`DigitalGain`; genicam:`Gain[DigitalAll]`; mf:`ISO_GAINS`(Digital) |
| `cap.control.gain.selector` | Choose which gain stage/channel to address | genicam:`GainSelector`(All/Red/Green/Blue/Tap/Analog/Digital) |
| `cap.control.gain.auto-balance` | Auto gain balance across channels/taps | genicam:`GainAutoBalance` |
| `cap.control.gain.digital-multiplier` | UVC digital multiplier + limit | uvc:PU digital-multiplier/limit; mf:`VideoProcAmp_DigitalMultiplier`/`_Limit` |
| `cap.control.gain.sensitivity-readback` | Relative sensitivity of chosen readout mode | libcamera:`SensorSensitivity`; camera2:`SENSOR_SENSITIVITY`(result) |

### 3d. white balance
| capability id | meaning (≤1 line) | normalizes |
|---|---|---|
| `cap.control.wb.mode` | AWB regime (auto/locked/continuous/off) | avf:`whiteBalanceMode`; camera2:`AWB_MODE`; libcamera:`AwbEnable`/`AwbMode`; v4l2:`AUTO_WHITE_BALANCE`/`AUTO_N_PRESET_WHITE_BALANCE`; mf:`EXTENDED_WHITEBALANCEMODE`; web:`whiteBalanceMode`; uvc:PU WB-auto; genicam:`BalanceWhiteAuto` |
| `cap.control.wb.lock` | Freeze current WB | avf:`setWhiteBalanceModeLocked…`; camera2:`AWB_LOCK`; v4l2:`3A_LOCK`(WB) |
| `cap.control.wb.region` | Metering ROI for AWB | camera2:`AWB_REGIONS`; mf:`ROI_ISPCONTROL` |
| `cap.control.wb.manual-temperature-kelvin` | Manual WB by color temperature (Kelvin) | avf:`...ForTemperatureAndTintValues:`; v4l2:`WHITE_BALANCE_TEMPERATURE`; mf:`WhiteBalance`; web:`colorTemperature`; uvc:PU WB-temperature; libcamera:`ColourTemperature` |
| `cap.control.wb.manual-tint` | Manual WB tint (paired w/ temperature) | avf:`AVCaptureWhiteBalanceTemperatureAndTintValues` |
| `cap.control.wb.manual-gains-rgb` | Manual per-channel R/G/B WB gains | avf:`setWhiteBalanceModeLockedWithDeviceWhiteBalanceGains:`; camera2:`COLOR_CORRECTION_GAINS`; libcamera:`ColourGains`; mf:`WHITEBALANCE_GAINS` |
| `cap.control.wb.manual-gains-component` | Manual WB by red/blue balance components | v4l2:`RED_BALANCE`/`BLUE_BALANCE`; uvc:PU WB-component; genicam:`BalanceRatio[Selector]` |
| `cap.control.wb.manual-chromaticity-xy` | Manual WB by CIE xy chromaticity | avf:`deviceWhiteBalanceGainsForChromaticityValues:` |
| `cap.control.wb.gain-ceiling` | Per-channel max WB gain readout | avf:`maxWhiteBalanceGain` |
| `cap.control.wb.gray-world` | Neutral gray-world WB gains | avf:`grayWorldDeviceWhiteBalanceGains` |
| `cap.control.wb.one-shot` | Single-shot WB action | v4l2:`DO_WHITE_BALANCE`; genicam:`BalanceWhiteAuto=Once`; web:`"single-shot"` |
| `cap.control.wb.state` | AWB convergence state | avf:`isAdjustingWhiteBalance`; camera2:`AWB_STATE`; libcamera:`AwbLocked`/`draft::AwbState` |

### 3e. aperture / ND (absorbs UVC/v4l2 iris — see §14)
| capability id | meaning (≤1 line) | normalizes |
|---|---|---|
| `cap.control.aperture.manual` | Manual aperture/iris f-number | camera2:`LENS_APERTURE`; v4l2:`IRIS_ABSOLUTE`; mf:`Iris`/KS`CameraControl_Iris`; uvc:`CT_IRIS_ABSOLUTE`; genicam:`Aperture`/`NumericalAperture` |
| `cap.control.aperture.relative` | Relative iris move | v4l2:`IRIS_RELATIVE`; uvc:`CT_IRIS_RELATIVE` |
| `cap.control.aperture.readback` | Current f-number readout | avf:`lensAperture` |
| `cap.control.nd-filter.density` | Neutral-density filter EV | camera2:`LENS_FILTER_DENSITY`; genicam:`Filter` |

### 3f. zoom (digital/imaging — optical-motorized zoom is §4)
| capability id | meaning (≤1 line) | normalizes |
|---|---|---|
| `cap.control.zoom.digital-crop-rect` | Digital zoom via crop rectangle | camera2:`SCALER_CROP_REGION`; libcamera:`ScalerCrop`; v4l2:`S_SELECTION`; mf:`EXTENDED_ZOOM`(Q16) |
| `cap.control.zoom.ratio` | Float zoom factor (incl. <1.0 ultrawide) | avf:`videoZoomFactor`; camera2:`CONTROL_ZOOM_RATIO`; web:`zoom`; mf:`Zoom`/`ZoomControl` |
| `cap.control.zoom.method-select` | Choose ratio vs digital-crop zoom path | camera2:`CONTROL_ZOOM_METHOD`; web:`ZoomControl.Mode` |
| `cap.control.zoom.ramp-with-rate` | Smooth zoom transition at a specified rate | avf:`rampToVideoZoomFactor:withRate:`/`cancelVideoZoomRamp`/`isRampingVideoZoom` |
| `cap.control.zoom.bounds` | Current-config min/max zoom readout | avf:`minAvailableVideoZoomFactor`/`max...`; camera2:`SCALER_AVAILABLE_MAX_DIGITAL_ZOOM`; web:`getCapabilities().zoom` |
| `cap.control.zoom.upscale-threshold` | Factor beyond which digital upscaling begins | avf:`videoZoomFactorUpscaleThreshold` |
| `cap.control.zoom.native-resolution-stops` | Extra non-upscaled native zoom stops | avf:`secondaryNativeResolutionZoomFactors` |
| `cap.control.zoom.low-latency-override` | Low-latency zoom settings-override path | camera2:`CONTROL_SETTINGS_OVERRIDE`(ZOOM) |

### 3g. ISP / tone pipeline
| capability id | meaning (≤1 line) | normalizes |
|---|---|---|
| `cap.control.isp.contrast` | Contrast | libcamera:`Contrast`; v4l2:`CONTRAST`; mf:`VideoProcAmp_Contrast`; web:`contrast`; pipewire:`SPA_PROP_contrast`; uvc:PU contrast |
| `cap.control.isp.brightness` | Brightness/black-level offset | libcamera:`Brightness`; v4l2:`BRIGHTNESS`; mf:`...Brightness`; web:`brightness`; pipewire:`SPA_PROP_brightness`; uvc:PU brightness |
| `cap.control.isp.saturation` | Color saturation | libcamera:`Saturation`; v4l2:`SATURATION`; mf:`...Saturation`; web:`saturation`; pipewire:`SPA_PROP_saturation`; uvc:PU saturation |
| `cap.control.isp.sharpness` | Sharpening intensity | libcamera:`Sharpness`; v4l2:`SHARPNESS`; mf:`...Sharpness`; web:`sharpness`; pipewire:`SPA_PROP_sharpness`; uvc:PU sharpness |
| `cap.control.isp.hue` | Hue rotation | libcamera:`Hue`; v4l2:`HUE`(+`HUE_AUTO`); mf:`...Hue`; pipewire:`SPA_PROP_hue`; uvc:PU hue |
| `cap.control.isp.gamma` | Gamma value | libcamera:`Gamma`; v4l2:`GAMMA`; mf:`...Gamma`; pipewire:`SPA_PROP_gamma`; uvc:PU gamma; genicam:`Gamma` |
| `cap.control.isp.edge-enhancement` | Edge-enhancement pipeline mode | camera2:`EDGE_MODE` |
| `cap.control.isp.noise-reduction` | Spatial noise-reduction mode/strength | camera2:`NOISE_REDUCTION_MODE`; libcamera:`draft::NoiseReductionMode` |
| `cap.control.isp.temporal-denoise` | Temporal denoise on/off/auto | mf:`EXTENDED_VIDEOTEMPORALDENOISING` |
| `cap.control.isp.hot-pixel-correction` | Hot/defective-pixel correction mode | camera2:`HOT_PIXEL_MODE` |
| `cap.control.isp.lens-shading-correction` | Vignette/lens-shading correction | camera2:`SHADING_MODE`; libcamera:`LensShadingCorrectionEnable`/`draft::LensShadingMapMode` |
| `cap.control.isp.chromatic-aberration-correction` | Chromatic-aberration correction mode | camera2:`COLOR_CORRECTION_ABERRATION_MODE`; libcamera:`draft::ColorCorrectionAberrationMode` |
| `cap.control.isp.geometric-distortion-correction` | Barrel/geometric distortion correction | avf:`isGeometricDistortionCorrectionEnabled`; camera2:`DISTORTION_CORRECTION_MODE`; libcamera:`LensDewarpEnable` |
| `cap.control.isp.color-correction-matrix` | Manual 3×3 CCM | camera2:`COLOR_CORRECTION_TRANSFORM`; libcamera:`ColourCorrectionMatrix`; mf:`EXTENDED_MCC`; genicam:`ColorTransformationValue` |
| `cap.control.isp.color-correction-mode` | CCM application mode | camera2:`COLOR_CORRECTION_MODE`; genicam:`ColorTransformationEnable` |
| `cap.control.isp.color-effect-preset` | Fixed color FX preset (mono/sepia/negative) | camera2:`CONTROL_EFFECT_MODE`; v4l2:`COLORFX`; mf:WSE`CREATIVEFILTER` |
| `cap.control.isp.black-level-lock` | Freeze sensor black-level compensation | camera2:`BLACK_LEVEL_LOCK` |
| `cap.control.isp.black-level-manual` | Manual analog black level | genicam:`BlackLevel[Selector]`; camera2:`SENSOR_BLACK_LEVEL`(report) |
| `cap.control.isp.white-clip` | Video-signal clipping ceiling | genicam:`WhiteClip[Selector]` |
| `cap.control.isp.color-killer` | Force monochrome output | v4l2:`COLOR_KILLER` |
| `cap.control.isp.chroma-gain` | Chroma gain / chroma AGC | v4l2:`CHROMA_GAIN`/`CHROMA_AGC` |
| `cap.control.isp.backlight-compensation` | Backlight compensation | v4l2:`BACKLIGHT_COMPENSATION`; mf:`...BacklightCompensation`; uvc:PU backlight-comp |
| `cap.control.isp.anti-flicker-power-line` | Power-line-frequency / anti-flicker (50/60/auto) | camera2:`AE_ANTIBANDING_MODE`; libcamera:`AeFlickerMode`/`AeFlickerPeriod`; v4l2:`POWER_LINE_FREQUENCY`/`BAND_STOP_FILTER`; mf:`...PowerlineFrequency`; uvc:PU power-line-freq |
| `cap.control.isp.test-pattern` | Sensor/ISP test-pattern generator (canonical; absorbs capture/sensor dups) | camera2:`SENSOR_TEST_PATTERN_MODE`; libcamera:`draft::TestPatternMode`; genicam:`TestPattern[Selector]` |
| `cap.control.catalog.scene-illuminance` | Estimated scene lux readout | libcamera:`Lux` |

### 3h. tone / dynamic-range knobs
| capability id | meaning (≤1 line) | normalizes |
|---|---|---|
| `cap.control.tone.tonemap-curve` | Per-channel programmable tonemap/contrast curve | camera2:`TONEMAP_CURVE`/`TONEMAP_MODE`; genicam:`LUTValue[Selector][Index]` |
| `cap.control.tone.tonemap-gamma-value` | Tonemap by single gamma value | camera2:`TONEMAP_GAMMA` |
| `cap.control.tone.tonemap-preset-curve` | Tonemap by preset curve (sRGB/Rec709) | camera2:`TONEMAP_PRESET_CURVE` |
| `cap.control.tone.global-tone-mapping` | Global tone-mapping enable | avf:`isGlobalToneMappingEnabled` |
| `cap.control.tone.wide-dynamic-range` | WDR/global-tonemap mode + strength | libcamera:`WdrMode`/`WdrStrength`; v4l2:`WIDE_DYNAMIC_RANGE` |
| `cap.control.tone.lut-enable` | Enable/select programmable LUT | genicam:`LUTEnable`/`LUTSelector` |

### 3i. 3A orchestration
| capability id | meaning (≤1 line) | normalizes |
|---|---|---|
| `cap.control.3a.master-mode` | Top-level 3A enable/scene/extended-scene switch | camera2:`CONTROL_MODE`; libcamera:`AeEnable`+`AwbEnable` |
| `cap.control.3a.scene-mode` | Named scene preset (portrait/night/sports) | camera2:`CONTROL_SCENE_MODE`; v4l2:`SCENE_MODE`; mf:`EXTENDED_SCENEMODE` |
| `cap.control.3a.combined-lock` | Lock AE+AWB+AF together in one op | v4l2:`3A_LOCK`(bitmask); avf:per-A locks |
| `cap.control.3a.region-max-counts` | Max metering-rectangle counts per A | camera2:`CONTROL_MAX_REGIONS`; mf:`RegionsOfInterestControl.MaxRegions`; web:`pointsOfInterest` |
| `cap.control.3a.region-unified-roi` | One user ROI driving AE/AF/AWB jointly (metering sense; subject-tracking→§7) | mf:`ROI_ISPCONTROL`; uvc:`CT_REGION_OF_INTEREST`(bmAutoControls); genicam:`RegionDestination` |
| `cap.control.3a.capture-intent` | Hint the 3A pipeline with capture intent | camera2:`CONTROL_CAPTURE_INTENT` |

### 3j. flash / torch
| capability id | meaning (≤1 line) | normalizes |
|---|---|---|
| `cap.control.flash.still-mode` | Still flash mode (off/on/auto/redeye) | avf:`flashMode`/`supportedFlashModes`; camera2:`FLASH_MODE_SINGLE`/`AE_MODE_ON_*_FLASH`; mf:`EXTENDED_FLASHMODE`; web:`fillLightMode` |
| `cap.control.flash.still-strength` | Per-shot flash brightness level | camera2:`FLASH_STRENGTH_LEVEL`; mf:`FLASH_POWER` |
| `cap.control.flash.preflash-redeye` | Red-eye-reduction preflash | avf:`isAutoRedEyeReductionEnabled`; web:`redEyeReduction` |
| `cap.control.flash.scene-detect` | Is the scene flash-requiring (metering) | avf:`isFlashScene`; camera2:`AE_STATE_FLASH_REQUIRED` |
| `cap.control.flash.charge-state` | Flash ready/charging/fired state | camera2:`FLASH_STATE`; avf:`isFlashAvailable` |
| `cap.control.flash.torch-mode` | Continuous torch on/off/auto | avf:`torchMode`/`isTorchAvailable`; camera2:`FLASH_MODE_TORCH`; v4l2:`ILLUMINATORS_1`/FLASH class; mf:`EXTENDED_TORCHMODE`/`TorchControl`; web:`torch` |
| `cap.control.flash.torch-strength` | Torch brightness level | avf:`setTorchModeOnWithLevel:`/`torchLevel`; camera2:`FLASH_TORCH_STRENGTH_LEVEL` |
| `cap.control.flash.ir-torch` | IR illuminator torch (canonical; absorbs depth `cap.ir.torch`) | mf:`EXTENDED_IRTORCHMODE`/`InfraredTorchControl`; uvc:`MSXU_CONTROL_IR_TORCH`; v4l2:`ILLUMINATORS_2` |
| `cap.control.flash.external-flash-mode` | Drive an external flash unit | camera2:`AE_MODE_ON_EXTERNAL_FLASH` |
| `cap.control.flash.strobe-output` | Assert a sensor STROBE/FLASH pin during the exposure window (LED-strobe sync) | baresensor:OmniVision`STROBE`/`FSIN`·Sony`XTRIG`·AR0234`FLASH` pin; genicam:`LineSource=ExposureActive`/strobe (overlaps `cap.timing.genlock.sync-pins` — that is multi-cam framesync, this is illumination sync) |

### 3k. UVC/V4L2 catalog leftovers
| capability id | meaning (≤1 line) | normalizes |
|---|---|---|
| `cap.control.catalog.scanning-mode` | Progressive/interlaced sensor scanning mode | uvc:`CT_SCANNING_MODE`; mf:KS`SCANMODE` |
| `cap.control.catalog.privacy-block` | Firmware privacy: block image acquisition | v4l2:`PRIVACY`; mf:KS`PRIVACY`; uvc:`CT_PRIVACY` |
| `cap.control.catalog.analog-video-standard` | Analog video standard + lock-status (capture dongles) | uvc:PU analog-video-standard/lock-status |

---

## 4. mechanical controls (PTZ) — `cap.ptz.*`
Transport-neutral: the same IDs are exposable later by a VISCA/NDI-PTZ control transport (additive — charter). Optical-motorized zoom + iris live here (digital zoom is §3f; iris also surfaced as `cap.control.aperture.*`, see §14).

| capability id | meaning (≤1 line) | normalizes |
|---|---|---|
| `cap.ptz.pan.absolute` | Set mechanical pan to an absolute angular position | uvc:`CT_PANTILT_ABSOLUTE`(pan); v4l2:`PAN_ABSOLUTE`; mf:`CameraControl_Pan`/`PANTILT`(pan); web:`pan` |
| `cap.ptz.pan.relative` | Nudge mechanical pan by a signed relative step | uvc:`CT_PANTILT_RELATIVE`(pan); v4l2:`PAN_RELATIVE`; mf:`PAN_RELATIVE`/`PANTILT_RELATIVE`(pan) |
| `cap.ptz.pan.speed` | Drive pan continuously at a signed speed until stopped | v4l2:`PAN_SPEED`; mf:`PAN_RELATIVE`(speed)/`PANTILT_RELATIVE` |
| `cap.ptz.pan.reset` | Return pan to default/home | v4l2:`PAN_RESET` |
| `cap.ptz.tilt.absolute` | Set mechanical tilt to an absolute angular position | uvc:`CT_PANTILT_ABSOLUTE`(tilt); v4l2:`TILT_ABSOLUTE`; mf:`CameraControl_Tilt`/`PANTILT`(tilt); web:`tilt` |
| `cap.ptz.tilt.relative` | Nudge mechanical tilt by a signed relative step | uvc:`CT_PANTILT_RELATIVE`(tilt); v4l2:`TILT_RELATIVE`; mf:`TILT_RELATIVE`/`PANTILT_RELATIVE`(tilt) |
| `cap.ptz.tilt.speed` | Drive tilt continuously at a signed speed until stopped | v4l2:`TILT_SPEED`; mf:`TILT_RELATIVE`(speed)/`PANTILT_RELATIVE` |
| `cap.ptz.tilt.reset` | Return tilt to default/home | v4l2:`TILT_RESET` |
| `cap.ptz.roll.absolute` | Set mechanical/optical roll to an absolute position | uvc:`CT_ROLL_ABSOLUTE`; mf:`CameraControl_Roll`/`ROLL` |
| `cap.ptz.roll.relative` | Nudge roll by a signed relative step | uvc:`CT_ROLL_RELATIVE`; mf:`ROLL_RELATIVE` `?` |
| `cap.ptz.zoom.optical.absolute` | Drive motorized zoom lens to an absolute optical position | uvc:`CT_ZOOM_ABSOLUTE`; v4l2:`ZOOM_ABSOLUTE`; mf:`CameraControl_Zoom`; web:`zoom`(device-optical); genicam:`FocalLength`+`Initialize/Status/Stepper` |
| `cap.ptz.zoom.optical.relative` | Step motorized zoom by a signed relative amount | uvc:`CT_ZOOM_RELATIVE`; v4l2:`ZOOM_RELATIVE` |
| `cap.ptz.zoom.optical.speed` | Drive motorized zoom continuously at a signed speed | v4l2:`ZOOM_CONTINUOUS`; uvc:`CT_ZOOM_RELATIVE`(speed sub-fields) |
| `cap.ptz.privacy.shutter` | Engage/disengage hardware privacy shutter | uvc:`CT_PRIVACY`; v4l2:`PRIVACY`; mf:`CAMERACONTROL_PRIVACY` |
| `cap.ptz.position.query` | Read range/step/default + current for a PTZ axis | uvc:`GET_MIN/MAX/RES/DEF/CUR`+`GET_INFO`; v4l2:`QUERYCTRL`+`G_CTRL`; mf:`GetRange`/`BASICSUPPORT`; web:`getCapabilities()`/`getSettings()` |
| `cap.ptz.auto.mode` | Hand mechanical PTZ to device auto-positioning `[CEILING]` | mf:`CameraControlFlags{Auto}` |
| `cap.ptz.lens.shutter` | Actuate a motorized mechanical shutter blade `[CEILING]` | genicam:`Shutter`+`Initialize/Status/Stepper` |
| `cap.ptz.lens.filter` | Actuate a motorized filter wheel `[CEILING]` | genicam:`Filter`+`Initialize/Status/Stepper` |
| `cap.ptz.lens.magnification` | Drive motorized magnification `[CEILING]` | genicam:`Magnification`+`Initialize/Status/Stepper` |
| `cap.ptz.controller.lifecycle` | Init/abort/disconnect/status of a motorized optic controller `[CEILING]` | genicam:`OpticControllerSelector`/`Initialize`/`Disconnect`/`Abort`/`Status` |

---

## 5. capture modes — `cap.capture.*`

| capability id | meaning (≤1 line) | normalizes |
|---|---|---|
| `cap.capture.still` | Fire a single still-image capture | avf:`capturePhotoWithSettings:delegate:`; camera2:`TEMPLATE_STILL_CAPTURE`/`ImageCapture.takePicture`; mf:`LowLagPhotoCapture`; web:`ImageCapture.takePhoto`; libcamera:single Request; v4l2:DQBUF; genicam:single-frame |
| `cap.capture.still.maxdims` | Select high-res still dims distinct from preview | avf:`maxPhotoDimensions`; camera2:still output size; mf:`KSCAMERAPROFILE_HighQualityPhoto`; web:`PhotoSettings.imageWidth/Height`; v4l2:`V4L2_MODE_HIGHQUALITY` |
| `cap.capture.still.qualityprioritization` | Bias still toward speed vs quality | avf:`AVCapturePhotoQualityPrioritization`/`maxPhotoQualityPrioritization`; camera2:`CAPTURE_MODE_MINIMIZE_LATENCY`/`_MAXIMIZE_QUALITY` |
| `cap.capture.still.grabframe` | Cheap preview-res snapshot without reconfigure | web:`ImageCapture.grabFrame`; avf:`AVCaptureVideoDataOutput` tap |
| `cap.capture.raw.bayer` | Unprocessed sensor Bayer/mono mosaic RAW | avf:`isBayerRAWPixelFormat:`+`14Bayer_*`; camera2:`RAW_SENSOR`/`RAW10`/`RAW12`; libcamera:`StreamRole::Raw`/`formats::S*`; v4l2:`SRGGB*`; pipewire:`SPA_MEDIA_SUBTYPE_bayer`; genicam:Bayer PFNC |
| `cap.capture.raw.fused` | Post-ISP fused/computational RAW DNG (not pure Bayer) | avf:`isAppleProRAW*`/`isAppleProRAWPixelFormat:` |
| `cap.capture.raw.format` | Select RAW pixel-format / bit-depth | avf:`availableRawPhotoPixelFormatTypes`/`rawPhotoPixelFormatType`; camera2:RAW10/12/PRIVATE; v4l2:bit-depth; libcamera:`SensorConfiguration` |
| `cap.capture.raw.codec` | Select RAW codec/compression | avf:`availableRawPhotoCodecTypes`; v4l2:`SBGGR10ALAW8`/`DPCM8` |
| `cap.capture.raw.sidecar` | RAW w/ embedded thumbnail / sidecar metadata | avf:`rawEmbeddedThumbnailPhotoFormat`/`rawFileType`; camera2:`DngCreator`+RAW cal metadata |
| `cap.capture.raw.plusprocessed` | RAW + processed image from one shot | avf:RAW init w/ `processedFileType`; camera2:RAW+JPEG/YUV multi-surface |
| `cap.capture.encodedstill.jpeg` | Platform-encoded JPEG still | camera2:`ImageFormat.JPEG`+`JPEG_QUALITY`; avf:`AVVideoCodecType.JPEG`; mf:`ImageEncodingProperties`(Jpeg); web:UA JPEG; v4l2:`V4L2_PIX_FMT_JPEG`; genicam:JPEG payload |
| `cap.capture.encodedstill.heic` | Platform-encoded HEIC/HEIF still | avf:`AVFileTypeHEIC`; camera2:`ImageFormat.HEIC`+`HEIC_AVAILABLE_*`; mf:`ImageEncodingProperties`(Heif) |
| `cap.capture.encodedstill.dng` | Platform-encoded DNG container still | avf:`AVFileTypeDNG`; camera2:`DngCreator` |
| `cap.capture.encodedstill.ultrahdr` | Gain-map Ultra-HDR JPEG/HEIC still | camera2:`JPEG_R`/`JPEGR_AVAILABLE_*`/`HEIC_AVAILABLE_*_ULTRA_HDR_*` |
| `cap.capture.encodedstill.embeddedmeta` | ISP-embedded EXIF/orientation/thumbnail in still | camera2:`JPEG_ORIENTATION`/`_QUALITY`/`_THUMBNAIL_*`; avf:`embeddedThumbnailPhotoFormat`+EXIF; mf:`MF_CAPTURE_METADATA_EXIF` |
| `cap.capture.encodedstill.embeddedgps` | ISP-embedded GPS tag in still | camera2:`JPEG_GPS_*`/`JPEG_GPS_LOCATION` |
| `cap.capture.readout.binning` | Select pixel-binning readout (prescriptive) | libcamera:`SensorConfiguration`(binning); genicam:`BinningHorizontal`/`Vertical`; v4l2:subdev binned `[trap]` |
| `cap.capture.readout.decimation` | Select skipping/decimation readout | libcamera:`SensorConfiguration`(skipping); genicam:`DecimationHorizontal`/`Vertical` |
| `cap.capture.readout.binningfactor` | Read which binning factor the sensor applied | camera2:`SENSOR_RAW_BINNING_FACTOR_USED`/`SENSOR_INFO_BINNING_FACTOR` |
| `cap.capture.readout.pixelmode` | Select full-res vs binned sensor pixel mode | camera2:`SENSOR_PIXEL_MODE`+`ULTRA_HIGH_RESOLUTION_SENSOR`; libcamera:`SensorConfiguration` |
| `cap.capture.readout.remosaic` | Quad-Bayer/Nona remosaic to full-res CFA | camera2:`ULTRA_HIGH_RESOLUTION_SENSOR`+`REMOSAIC_REPROCESSING` |
| `cap.capture.readout.cropregion` | Analog/digital sensor crop-region (single ROI) | camera2:`SCALER_CROP_REGION`/`RAW_CROP_REGION`; libcamera:`SensorConfiguration`/`ScalerCrop`; genicam:`OffsetX`/`Y`+`Width`/`Height` |
| `cap.capture.readout.multiroi` | Multi-region-of-interest readout (≥2 ROIs) `[CEILING]` | genicam:`RegionSelector`/`RegionMode` |
| `cap.capture.readout.shuttermode` | Select global vs rolling vs global-reset shutter `[CEILING]` | genicam:`SensorShutterMode` |
| `cap.capture.zsl` | Zero-shutter-lag still from a buffered ring | avf:`isZeroShutterLag*`; camera2:`CONTROL_ENABLE_ZSL`+`TEMPLATE_ZERO_SHUTTER_LAG`; mf:`LowLagPhotoCapture` |
| `cap.capture.zsl.reprocess` | Reprocess a captured PRIVATE/YUV/RAW frame | camera2:`PRIVATE_REPROCESSING`/`YUV_REPROCESSING`/`createReprocessableCaptureSession` |
| `cap.capture.responsive` | Overlapped/fast-prioritized shutter responsiveness | avf:`isResponsiveCaptureSupported`/`isFastCapturePrioritizationSupported`/`captureReadiness` |
| `cap.capture.bracket.exposure` | Auto/manual exposure (EV) bracket sequence | avf:`AVCaptureAuto/ManualExposureBracketedStillImageSettings`/`maxBracketedCapturePhotoCount`; mf:`VariablePhotoSequenceCapture`(EV); genicam:`SequencerControl` |
| `cap.capture.bracket.focus` | Focus (lens-position) bracket sequence | mf:`VariablePhotoSequenceCapture`(focus); genicam:`SequencerControl`(focus); avf:`AVCapturePhotoBracketSettings` `?` |
| `cap.capture.bracket.lensstabilization` | Lens/OIS stabilization across a bracket | avf:`AVCapturePhotoBracketSettings.isLensStabilizationEnabled`/`AVCaptureLensStabilizationStatus` |
| `cap.capture.burst` | Rapid full-res burst at a guaranteed rate | camera2:`BURST_CAPTURE`/`SNAPSHOT` recommended; mf:`LowLagPhotoSequenceCapture`/`AdvancedPhotoCapture` |
| `cap.capture.constituentdelivery` | Per-lens stills from a virtual multi-lens device | avf:`isVirtualDeviceConstituentPhotoDeliverySupported`/`isVirtualDeviceFusionSupported` |
| `cap.capture.stillduringvideo` | Capture a full still while recording video | camera2:`TEMPLATE_VIDEO_SNAPSHOT`/use-case `_VIDEO_SNAPSHOT`; avf:photo+movie outputs; mf:photo during record |
| `cap.capture.hdr.video.tenbit` | 10-bit HDR video output profile | camera2:`DYNAMIC_RANGE_TEN_BIT`/`DynamicRangeProfiles`/`MANDATORY_TEN_BIT_*`; avf:`x420`; genicam:10/12-bit |
| `cap.capture.hdr.video.hlg` | HLG (BT.2020) HDR transfer | camera2:`DynamicRangeProfiles.HLG10`; avf:`...HLG_BT2020` |
| `cap.capture.hdr.video.hdr10` | HDR10 / HDR10+ PQ profile | camera2:`DynamicRangeProfiles.HDR10`/`HDR10_PLUS` |
| `cap.capture.hdr.video.dolbyvision` | Dolby Vision profile | camera2:`DynamicRangeProfiles.DOLBY_VISION_*` |
| `cap.capture.hdr.video.log` | Log-curve capture (e.g. Apple Log) | avf:`...AppleLog`(+`AppleLog2` ceiling); genicam:tonemap `?` |
| `cap.capture.hdr.video.widegamut` | Wide-gamut (P3/BT.2020) color-space capture | avf:`...P3_D65`/`automaticallyConfiguresCaptureDeviceForWideColor`; camera2:`COLOR_SPACE_PROFILES`/`setColorSpace`; web:`VideoColorSpace`(ro) |
| `cap.capture.hdr.video.builtin` | Engage a built-in (uncontrollable) HDR-video algorithm | mf:`HdrVideoControl`; camera2:`SCENE_MODE_HDR`; uvc:`MSXU_CONTROL_VIDEO_HDR` |
| `cap.capture.hdr.still.sensormerge` | In-stack multi-exposure HDR merge at capture | libcamera:`HdrMode`+`HdrChannel`; v4l2:`WIDE_DYNAMIC_RANGE`/`HDR_SENSOR_MODE` |
| `cap.capture.hdr.multislope` | Multi-slope/DOL companded sensor HDR readout (canonical; absorbs control dup) `[CEILING]` | genicam:`MultiSlopeMode`/`MultiSlopeKneePointSelector`; v4l2:`HDR_SENSOR_MODE`(thin) |
| `cap.capture.highspeed` | High-frame-rate / slow-motion (120/240fps) session | camera2:`CONSTRAINED_HIGH_SPEED_VIDEO`/`createConstrainedHighSpeedCaptureSession`; avf:high-fps format+equal min/max frame duration; mf:`KSCAMERAPROFILE_HighFrameRate`; genicam:high `AcquisitionFrameRate` |
| `cap.capture.timelapse` | Interval / time-lapse capture cadence | camera2:long `SENSOR_FRAME_DURATION`; genicam:low `AcquisitionFrameRate` |
| `cap.capture.compphoto.selectable` | Explicitly select an OS comp-photo mode (HDR/Night/Bokeh/Portrait) | camera2:`CameraExtensionCharacteristics`(EXTENSION_*)/`createExtensionSession`; camera2:`EXTENDED_SCENE_MODE`(BOKEH_*); mf:`AdvancedPhotoMode` |
| `cap.capture.compphoto.strength` | Set strength / read active type of a selectable mode | camera2:`EXTENSION_STRENGTH`/`EXTENSION_CURRENT_TYPE`/`EXTENSION_NIGHT_MODE_INDICATOR` |
| `cap.capture.compphoto.propertycontrolled` | Toggle a property-controlled comp-photo mode (Windows Studio) | mf:`CAMERACONTROL_EXTENDED_BACKGROUNDSEGMENTATION`/`KSPROPERTYSETID_WindowsStudioEffects`; uvc:`MSXU_CONTROL_DIGITALWINDOW`/`_VIDEO_HDR` |
| `cap.capture.compphoto.implicit` | Observe-only OS comp-photo the app cannot select `[trap]` | avf:`isFlashScene`/`photoSettingsForSceneMonitoring`; mf:`SceneAnalysisEffect` (Apple Night/SmartHDR/DeepFusion run by OS, no app trigger) |
| `cap.capture.compphoto.deferred` | OS returns a proxy resolved later by comp-photo pipeline `[trap: deferral retired MEL-ENGINE-I]` | avf:`isAutoDeferredPhotoDeliverySupported`/`AVCaptureDeferredPhotoProxy` |
| `cap.capture.onboardencode.h264` | Receive on-camera H.264 / frame-based bitstream | uvc:H.264 frame-based/video-class payload; v4l2:`H264`/`H264_NO_SC`/`_MVC`; pipewire:`SPA_MEDIA_SUBTYPE_h264`; mf:`usbvideo.sys` |
| `cap.capture.onboardencode.hevc` | Receive on-camera HEVC bitstream | v4l2:`HEVC`; pipewire:`SPA_MEDIA_SUBTYPE_h265`; uvc:HEVC-over-UVC (vendor/XU) `?` |
| `cap.capture.onboardencode.mjpeg` | Receive on-camera MJPEG stream | v4l2:`MJPEG`; pipewire:`SPA_MEDIA_SUBTYPE_mjpg`; uvc:MJPEG payload |
| `cap.capture.onboardencode.othercodec` | Receive other on-camera codec streams (VP8/9/TS/DV) | v4l2:`VP8`/`VP9`; pipewire:`_dv`/`_mpegts`/`_vc1`/`_vp8`/`_vp9`; uvc:MPEG-2 TS/MPEG-4 SL/VC1/DV |
| `cap.capture.onboardencode.bitratectl` | Set on-camera encoder bitrate/rate-control/QP at runtime | uvc:initial/runtime bitrate/QP/rate-control/level; mf:H.264 UVC encoder controls |
| `cap.capture.onboardencode.keyframe` | Force keyframe / I-frame period on camera encoder | uvc:force-keyframe, iframe-period |
| `cap.capture.onboardencode.ltr` | Configure long-term-reference frames | uvc:LTR frames |
| `cap.capture.onboardencode.gopconfig` | Configure slice/CABAC/SEI/reorder/leaky-bucket params | uvc:slice-mode, entropy/CABAC, SEI, num-reorder, leaky-bucket |
| `cap.capture.stillpipe.method` | UVC still-image pipeline negotiation (still res ≠ video) | uvc:`VS_STILL_PROBE`/`VS_STILL_COMMIT` (Methods 1/2/3) |
| `cap.capture.stillpipe.trigger` | Trigger a UVC still capture (software) | uvc:`VS_STILL_IMAGE_TRIGGER` |
| `cap.capture.stillpipe.hwbutton` | Hardware shutter-button trigger via status-interrupt endpoint | uvc:Method 3 hardware-button |
| `cap.capture.trigger.software` | Software single-shot / software trigger | genicam:`TriggerSoftware`/`TriggerSource_Software`; camera2:per-request submit (closest) |
| `cap.capture.trigger.acquisitioncontrol` | Start/stop/single-frame acquisition control | genicam:`AcquisitionStart`/`Stop`/`AcquisitionMode`; avf/camera2:repeating-request start/stop |
| `cap.capture.trigger.hardwareline` | Hardware-line trigger w/ delay/overlap/divider `[CEILING]` | genicam:`TriggerSelector`/`TriggerSource`/`TriggerActivation`/`TriggerOverlap`/`TriggerDelay`/`TriggerDivider` |
| `cap.capture.trigger.scheduled` | Network-synced scheduled action-command trigger (PTP firing) `[CEILING]` | genicam:`ActionControl`+GigE Scheduled Action Commands |
| `cap.capture.sequencer` | Hardware per-frame setting-table sequencer (canonical; absorbs timing dup) `[CEILING]` | genicam:`SequencerControl`(`SequencerSetSelector`/`Save`/`Load`/`Next`/`Active`) |
| `cap.capture.userset` | Persist/load whole-camera config to on-device NVM `[CEILING]` | genicam:`UserSetControl`(`UserSetSave`/`Load`/`Default`/`Selector`) |

---

## 6. depth / 3D / calibration — `cap.depth.*` / `cap.calib.*` / `cap.ir.*`

| capability id | meaning (≤1 line) | normalizes |
|---|---|---|
| `cap.depth.map.float` | Per-pixel metric depth map (float) | avf:`DepthFloat16`/`32`/`AVDepthData`; mf:`DepthMediaFrame`+`DepthScaleInMeters`; camera2:`DEPTH16`; v4l2:`Z16`; genicam:`Coord3D_C16`/`C32f`/`Scan3dOutputMode=CalibratedAC` |
| `cap.depth.map.disparity` | Per-pixel disparity (inverse-depth) map | avf:`DisparityFloat16`/`32`; genicam:`Scan3dOutputMode=DisparityC` |
| `cap.depth.pointcloud` | Depth as a 3-axis point cloud (XYZ) | camera2:`DEPTH_POINT_CLOUD`; genicam:`Coord3D_ABC32f`/`Scan3dCoordinateSystem`/`CalibratedABC_Grid` |
| `cap.depth.accuracy` | Depth accuracy absolute(metric) vs relative | avf:`depthDataAccuracy`; mf:`MinReliableDepth`/`MaxReliableDepth` |
| `cap.depth.quality` | Quality/precision grade of depth result | avf:`depthDataQuality` |
| `cap.depth.confidence` | Per-pixel confidence/validity map (semantics; delivery layout→§8) | v4l2:`CNF4`; genicam:`ComponentSelector=Confidence`/`Confidence*` |
| `cap.depth.invalid` | Sentinel marking no-return/invalid pixels | genicam:`Scan3dInvalidDataFlag`+`Value`/`Scan3dAxisMin`/`Max` |
| `cap.depth.unit` | Distance unit + coordinate-system declaration | mf:`DepthScaleInMeters`; genicam:`Scan3dDistanceUnit`/`CoordinateSystem`/`CoordinateScale`/`Offset` |
| `cap.depth.filter` | Temporal/spatial smoothing + hole-fill | avf:`AVCaptureDepthDataOutput.isFilteringEnabled`/`isDepthDataFiltered` |
| `cap.depth.format.convert` | Convert delivered depth between depth/disparity | avf:`availableDepthDataTypes`/`depthDataByConvertingToDepthDataType:` |
| `cap.depth.stream.rate` | Independent frame-rate/format for the depth stream | avf:`activeDepthDataFormat`/`activeDepthDataMinFrameDuration`; camera2:`DEPTH_AVAILABLE_DEPTH_STREAM_CONFIGURATIONS`/`_MIN_FRAME_DURATIONS` |
| `cap.depth.still` | Depth map delivered with a still capture | avf:`isDepthDataDeliverySupported`/`embedsDepthDataInPhoto`/`AVCapturePhoto.depthData`; camera2:`DYNAMIC_DEPTH_*`/`DEPTH_JPEG` |
| `cap.depth.exclusive` | Depth & color streams mutually exclusive | camera2:`DEPTH_DEPTH_IS_EXCLUSIVE` |
| `cap.depth.coordmap` | 2D↔3D / cross-source pixel mapping from depth+intrinsics | mf:`DepthCorrelatedCoordinateMapper`/`TryCreateCoordinateMapper` |
| `cap.depth.zoom.cofeasible` | Zoom×depth-delivery combination-feasibility list | avf:`supportedVideoZoomFactorsForDepthDataDelivery` |
| `cap.depth.raw.tof` | Raw ToF correlation frames (pre-depth stage) `[CEILING]` | (HAL — ToF sensor correlation; no consumer-OS surface) |
| `cap.depth.raw.structured` | Raw structured-light frames (pre-depth stage) `[CEILING]` | (HAL — structured-light pattern frames) |
| `cap.depth.raw.lidar` | Raw LiDAR correlation frames (pre-depth stage) `[CEILING]` | (HAL — LiDAR correlation; OS gives only depth result) |
| `cap.depth.pdaf.raw` | Raw PDAF phase/disparity maps (sensor AF stage) `[CEILING]` | (IMX586/HP2 — OS gives only AF result) |
| `cap.depth.container.multicomponent` | One container carrying range+intensity+confidence `[CEILING]` | genicam:GenDC (`GenDCStreamingMode`/`Descriptor`/`ComponentSelector`/`Enable`/`GroupSelector`) |
| `cap.seg.matte.portrait` | Grayscale foreground/background portrait matte | avf:`AVPortraitEffectsMatte`(+`isPortraitEffectsMatteDelivery*`/`embeds...`); mf:`BACKGROUNDSEGMENTATION_MASK` |
| `cap.seg.matte.semantic` | Per-class soft semantic matte (skin/hair/teeth/glasses) | avf:`AVSemanticSegmentationMatte`/`AVSemanticSegmentationMatteType`/`available...MatteTypes` |
| `cap.seg.mask.person` | Live person/background mask as a track output | mf:`BACKGROUNDSEGMENTATION_MASK`; web:`backgroundSegmentationMask` |
| `cap.seg.matte.orient` | Re-orient a delivered matte to EXIF orientation | avf:`AVPortraitEffectsMatte applyingExifOrientation:`/`AVSemanticSegmentationMatte ...` |
| `cap.calib.intrinsics.matrix` | Intrinsic matrix fx,fy,cx,cy(,skew)+ref dims | avf:`intrinsicMatrix`/`intrinsicMatrixReferenceDimensions`; camera2:`LENS_INTRINSIC_CALIBRATION`; mf:`CameraIntrinsics.FocalLength`/`PrincipalPoint`; uvc:`MSXU_CONTROL_CAMERA_INTRINSICS`; genicam:`Scan3dFocalLength`/`PrincipalPointU`/`V` |
| `cap.calib.intrinsics.perframe` | Intrinsic matrix as a per-frame attachment (canonical; absorbs meta dup) | avf:`...CameraIntrinsicMatrix`+`isCameraIntrinsicMatrixDeliveryEnabled`; genicam:`ChunkScan3dFocalLength`/`PrincipalPoint*`; v4l2:`D4XX` intrinsics |
| `cap.calib.pixelsize` | Physical sensor pixel pitch | avf:`pixelSize`; genicam:`SensorPixelWidth`/`Height` |
| `cap.calib.distortion.model` | Parametric distortion coeffs (radial/tangential, Brown-Conrady) | camera2:`LENS_DISTORTION`(+deprecated `RADIAL_DISTORTION`); mf:`RadialDistortion`/`TangentialDistortion` |
| `cap.calib.distortion.lut` | Distortion as a LUT + optical center (fwd/inverse) | avf:`lensDistortionLookupTable`/`inverse...`/`lensDistortionCenter` |
| `cap.calib.distortion.warp` | Built-in (un)distort-point / projection ops | mf:`UndistortedProjectionTransform`/`UndistortPoint(s)`/`DistortPoint`/`ProjectOntoFrame` |
| `cap.calib.extrinsics.matrix` | Camera extrinsic pose (rotation+translation) | avf:`extrinsicMatrix`; uvc:`MSXU_CONTROL_CAMERA_EXTRINSICS`; genicam:`Scan3dCoordinateTransformSelector`+`TransformValue`/`CoordinateReference*` |
| `cap.calib.pose.translation` | Sensor offset (m) from a reference point | camera2:`LENS_POSE_TRANSLATION` |
| `cap.calib.pose.rotation` | Sensor orientation quaternion | camera2:`LENS_POSE_ROTATION` |
| `cap.calib.pose.reference` | Reference frame for pose (primary-cam/gyro/automotive) | camera2:`LENS_POSE_REFERENCE`; genicam:`Scan3dCoordinateSystemReference` |
| `cap.calib.delivery` | Opt-in delivery of calibration data with a still | avf:`isCameraCalibrationDataDeliverySupported`/`...Enabled`/`AVDepthData.cameraCalibrationData` |
| `cap.calib.stereo.baseline` | Stereo-rig physical baseline distance | genicam:`Scan3dBaseline` |
| `cap.ir.stream` | Dedicated IR/NIR/mono (grey) image stream | mf:`MediaFrameSourceKind.Infrared`; camera2:`MONOCHROME`/`Y8`; v4l2:`GREY`/`Y10..Y16`(+packed); libcamera:`R8`/`R10`/`R12`/`R16`; pipewire:`GRAY8`/`GRAY16`; genicam:`Mono*`/`ComponentSelector=Infrared`; uvc:multi-VS IR pin; avf:TrueDepth IR |
| `cap.ir.cfa.nir` | Sensor CFA reported as near-IR / mono | camera2:`COLOR_FILTER_ARRANGEMENT_NIR`; libcamera:`ColorFilterArrangement==MONO`; genicam:`PixelColorFilter=None` |
| `cap.ir.stereo.interleaved` | Two-source interleaved grey (stereo IR) | v4l2:`Y8I`/`Y12I` |
| `cap.ir.depth.interleaved` | Combined IR(Y10)+depth(Z16) interleaved | v4l2:`INZI` |
| `cap.ir.auth.mode` | Secure IR face-auth streaming (Windows-Hello-class) | mf:Windows Hello IR under `KSCATEGORY_SENSOR_CAMERA`; uvc:`MSXU_CONTROL_FACE_AUTHENTICATION` |
| `cap.spatial.video` | Stereo / spatial (3D) video capture | avf:spatial-video movie output `[>pin]`; capture-cards dual-link 3D stereo |
| `cap.spatial.comfort` | Scene-unsuitability hints for comfortable stereo | avf:`spatialCaptureDiscomfortReasons` |

---

## 7. live effects — `cap.effect.*`
Each effect split into toggle / config / support_query / state so an axis that runs the effect read-only classifies `deny` on `.toggle` while `.state` stays `native`. raw-UVC lifts DIGITALWINDOW/VIDEO_HDR-bearing webcams `deny`→`emulate` on macos/win32/linux (a P4 matter, not a new ID).

| capability id | meaning (≤1 line) | normalizes |
|---|---|---|
| `cap.effect.autoframe.toggle` | Enable/disable OS auto-framing (Center Stage / digital-window) | avf:`centerStageEnabled`/`centerStageControlMode`; camera2:`CONTROL_AUTOFRAMING`; mf:`EXTENDED_DIGITALWINDOW`/WSE`AUTOMATICFRAMINGKIND`; web:`faceFraming`; uvc:`MSXU_CONTROL_DIGITALWINDOW` |
| `cap.effect.autoframe.config` | Configure auto-framing window/zoom/FoV bounds | avf:`videoMin/MaxZoomFactorForCenterStage`; mf:`DIGITALWINDOW_CONFIGCAPS`/WSE`SENSORCENTERCROP`; uvc:`MSXU_CONTROL_DIGITALWINDOW_CONFIG` |
| `cap.effect.autoframe.support_query` | Query whether auto-framing exists here | avf:`isCenterStageSupported`; camera2:`CONTROL_AUTOFRAMING_AVAILABLE`; mf:`DIGITALWINDOW_CONFIGCAPS`/WSE`WINDOWSSTUDIO_SUPPORTED`; web:capability presence |
| `cap.effect.autoframe.state` | Read whether auto-framing is active/converged | avf:`isCenterStageActive`; camera2:`CONTROL_AUTOFRAMING_STATE` |
| `cap.effect.autoframe.regime` | App vs user vs cooperative control arbitration | avf:`AVCaptureCenterStageControlMode` |
| `cap.effect.bg_blur.toggle` | Enable/disable OS background/portrait blur | mf:`BACKGROUNDSEGMENTATION_BLUR`; web:`backgroundBlur`; camera2:`EXTENSION_BOKEH`/`EXTENDED_SCENE_MODE`(BOKEH) |
| `cap.effect.bg_blur.config` | Tune blur strength / shallow-focus intensity | mf:`BACKGROUNDSEGMENTATION_SHALLOWFOCUS`; camera2:`EXTENSION_STRENGTH`/`EXTENDED_SCENE_MODE_ZOOM_RATIO_RANGES` |
| `cap.effect.bg_blur.support_query` | Query whether OS blur/bokeh is available | mf:WSE`WINDOWSSTUDIO_SUPPORTED`; camera2:`getSupportedExtensions()`/`isExtensionAvailable`; web:capability array; avf:`isPortraitEffectSupported` |
| `cap.effect.bg_blur.state` | Read whether OS portrait/blur is on (read-only) | avf:`isPortraitEffectActive` |
| `cap.effect.bg_replace.toggle` | Opt into / enable OS background replacement | avf:`isBackgroundReplacementEnabled`; mf:WSE background-replace |
| `cap.effect.bg_replace.state` | Read whether background replacement is active | avf:`isBackgroundReplacementActive` |
| `cap.effect.bg_replace.support_query` | Query background-replacement support | avf:`isBackgroundReplacementSupported` |
| `cap.effect.segmentation_mask.deliver` | Receive person/bg seg MASK as output for app compositing | mf:`BACKGROUNDSEGMENTATION_MASK`; web:`backgroundSegmentationMask` |
| `cap.effect.studio_light.toggle` | Enable/disable OS studio/stage relighting | mf:WSE`STAGELIGHT` |
| `cap.effect.studio_light.state` | Read whether studio light is active | avf:`isStudioLightActive` |
| `cap.effect.studio_light.support_query` | Query studio-light support | avf:`isStudioLightSupported`; mf:WSE`WINDOWSSTUDIO_SUPPORTED` |
| `cap.effect.eye_contact.toggle` | Enable/disable OS eye-contact / gaze correction | mf:`EXTENDED_EYEGAZECORRECTION`; web:`eyeGazeCorrection` |
| `cap.effect.eye_contact.config` | Select eye-contact variant (normal/teleprompter) | mf:EYEGAZECORRECTION Teleprompter; web:`eyeGazeCorrection="stare"` |
| `cap.effect.reaction.toggle` | Enable/disable reactions feature | avf:`reactionEffectsEnabled` |
| `cap.effect.reaction.gesture_toggle` | Enable/disable gesture-triggered reactions | avf:`reactionEffectGesturesEnabled` |
| `cap.effect.reaction.fire` | Programmatically trigger a reaction overlay | avf:`performEffectForReaction:` |
| `cap.effect.reaction.catalog` | Enumerate available reaction types | avf:`availableReactionTypes`/`AVCaptureReactionType`/`reactionEffectsSupported` |
| `cap.effect.reaction.state` | Read in-progress reaction effects (type+timing) | avf:`reactionEffectsInProgress`/`AVCaptureReactionEffectState` |
| `cap.effect.subject_roi.set` | Hand the OS a subject ROI auto-algorithms track (tracking sense; metering→§3i) | mf:`EXTENDED_ROI_ISPCONTROL`/`RegionsOfInterestControl`; uvc:`CT_REGION_OF_INTEREST`(bmAutoControls); web:`pointsOfInterest`; camera2:`AE/AF/AWB_REGIONS` |
| `cap.effect.creative_filter.toggle` | Enable OS creative/stylized live filter | mf:WSE`CREATIVEFILTER` |
| `cap.effect.show_system_ui` | Surface the OS-owned effects control panel | avf:`showSystemUserInterface:`/`AVCaptureSystemUserInterface` |
| `cap.effect.change_notify` | Be notified when user/OS toggles an effect (re-read state) | web:`"configurationchange"`; mf:`MFCreateCameraControlMonitor`→`OnChange` |

---

## 8. frame memory — `cap.frame.*`
`cap.frame.format.*` = deliverability ("camera can DELIVER format X"); the byte-layout/colorimetry DESCRIPTOR is `image`'s, HDR transfer/profile is `color`'s (see §14). Zero-copy surface KIND is the capability, not a transport.

### 8a. delivery formats
| capability id | meaning (≤1 line) | normalizes |
|---|---|---|
| `cap.frame.format.rgb.packed8` | 8-bit/ch packed RGB/BGR(A/X) | avf:`32BGRA`; camera2:`R8G8B8A8/X8`/`FLEX_RGBA_8888`; mf:`RGB32/ARGB32/RGB24`; pipewire:`RGB/RGBA/BGRA`; v4l2:`RGB24..RGBA32`; web:`RGBA/RGBX/BGRA/BGRX`; libcamera:`RGB888..BGRA8888`; genicam:`RGB8/BGR8` `[CEILING]` |
| `cap.frame.format.rgb.packed16` | 16-bit/ch packed RGB | camera2:`R16G16B16A16_FLOAT`; v4l2:`RGB48/BGR48`; libcamera:`RGB161616`; genicam:`RGB16` `[CEILING]` |
| `cap.frame.format.rgb.lowbit` | Sub-8-bit packed RGB (565/555/444/332) | camera2:`R5G6B5`/`RGB_565`; mf:`RGB565` `?`; v4l2:`RGB332..XRGB444`; libcamera:`RGB565` |
| `cap.frame.format.rgb.hdr10bit` | 10-bit-component packed RGB (2101010) | camera2:`R10G10B10A2`; v4l2:`ARGB2101010/RGBA1010102`; genicam:`RGB10/RGB12` `[CEILING]` |
| `cap.frame.format.yuv.packed422_8` | 8-bit packed 4:2:2 (YUYV/UYVY) | mf:`YUY2/UYVY`; pipewire:`YUY2/UYVY`; v4l2:`YUYV/UYVY/YVYU/VYUY`; libcamera:`YUYV..VYUY`; genicam:`YUV422_8/YCbCr422_8` `[CEILING]` |
| `cap.frame.format.yuv.packed422_hdr` | 10/12/16-bit packed 4:2:2 | mf:`Y210/Y216/P210/P216`; v4l2:`Y210/Y212/Y216/P210`; genicam:`YCbCr422 10/12` `[CEILING]` |
| `cap.frame.format.yuv.packed444` | Packed 4:4:4 YUV/AYUV | mf:`AYUV/Y410/Y416`; libcamera:`AVUY8888/XVUY8888`; genicam:`YCbCr8` `[CEILING]` |
| `cap.frame.format.yuv.semiplanar420_8` | 8-bit semi-planar 4:2:0 (NV12/NV21) | avf:`420v/420f`; camera2:`Y8Cb8Cr8_420`/`NV21`/`YUV_420_888`; mf:`NV12`; pipewire:`NV12`; v4l2:`NV12/NV21(M)`; web:`NV12`; libcamera:`NV12/NV21` |
| `cap.frame.format.yuv.semiplanar422_8` | 8-bit semi-planar 4:2:2 (NV16/NV61) | camera2:`NV16`/`YUV_422_888`; v4l2:`NV16/NV61`; libcamera:`NV16/NV61` |
| `cap.frame.format.yuv.semiplanar444_8` | 8-bit semi-planar 4:4:4 (NV24/NV42) | camera2:`YUV_444_888`; v4l2:`NV24/NV42`; libcamera:`NV24/NV42` |
| `cap.frame.format.yuv.semiplanar420_hdr` | 10/12-bit semi-planar 4:2:0 (P010/P016/NV15) | avf:`x420`; camera2:`YCbCr_P010`; mf:`P010/P016`; v4l2:`NV15/P010/P012` |
| `cap.frame.format.yuv.semiplanar422_hdr` | 10-bit semi-planar 4:2:2 (P210) | camera2:`YCbCr_P210` `?`; v4l2:`P210` |
| `cap.frame.format.yuv.planar420_8` | 8-bit tri-planar 4:2:0 (I420/YV12) | camera2:`YV12`; mf:`YV12/I420/IYUV`; pipewire:`I420`; v4l2:`YUV420/YVU420(M)`; web:`I420/I420A`; libcamera:`YUV420/YVU420` |
| `cap.frame.format.yuv.planar422_8` | 8-bit tri-planar 4:2:2 | v4l2:`YUV422P/M`; web:`I422/I422A`; libcamera:`YUV422/YVU422` |
| `cap.frame.format.yuv.planar444_8` | 8-bit tri-planar 4:4:4 | v4l2:`YUV444M`; web:`I444/I444A`; libcamera:`YUV444/YVU444` |
| `cap.frame.format.yuv.planar_subsampled` | Coarse planar 4:1:1 / 4:1:0 | v4l2:`YUV411P/YUV410/YVU410` |
| `cap.frame.format.yuv.planar_hdr` | 10/12-bit tri-planar YUV (I010…) | web:`I010/I420P10/I422P10/I444P10/12(+A)`; v4l2:P-family |
| `cap.frame.format.yuv.tiled` | Vendor-tiled YUV layouts (GPU/codec import) | v4l2:`NV12MT/NV12_4L4/NV12_16L16` |
| `cap.frame.format.bayer8` | 8-bit raw Bayer | v4l2:`SBGGR8..SRGGB8`; libcamera:`SRGGB8..SBGGR8`; camera2:via RAW_SENSOR; genicam:`BayerRG8` `[CEILING]` |
| `cap.frame.format.bayer10` | 10-bit raw Bayer (incl packed/CSI2) | camera2:`RAW10`; v4l2:`SBGGR10(P)`; libcamera:`SRGGB10/_CSI2P/_IPU3`; genicam:`BayerRG10/10p` `[CEILING]` |
| `cap.frame.format.bayer12` | 12-bit raw Bayer (incl packed/CSI2) | camera2:`RAW12`; v4l2:`SBGGR12(P)`; libcamera:`SRGGB12/_CSI2P`; genicam:`BayerRG12/12p` `[CEILING]` |
| `cap.frame.format.bayer14` | 14-bit raw Bayer (incl packed/CSI2) | v4l2:`SBGGR14(P)`; libcamera:`SRGGB14/_CSI2P`; avf:`14Bayer_*` |
| `cap.frame.format.bayer16` | 16-bit raw Bayer | camera2:`RAW16`; v4l2:`SBGGR16..SRGGB16`; libcamera:`SRGGB16..SBGGR16`; genicam:`BayerRG16` `[CEILING]` |
| `cap.frame.format.bayer.compressed` | Companded/compressed raw Bayer (A-law/DPCM/PiSP) | v4l2:`SBGGR10ALAW8`/`DPCM8`; libcamera:`*_PISP_COMP1` |
| `cap.frame.format.gray8` | 8-bit greyscale/mono | avf:`L008`; camera2:`Y8`; mf:`L8` `?`; pipewire:`GRAY8`; libcamera:`R8`; genicam:`Mono8` `[CEILING]` |
| `cap.frame.format.gray_highbit` | 10/12/16-bit (+float) greyscale | avf:`OneComponent16Half/32Float`; mf:`L16` `?`; pipewire:`GRAY16_LE/BE`; v4l2:`Y16_BE`+R10/12/16; libcamera:`R10/12/16`; genicam:`Mono10..16` `[CEILING]` |
| `cap.frame.format.3d.coord` | 3D coordinate/range/disparity component frames (delivery facet; semantics→§6) `[CEILING]` | genicam:`Coord3D_*`/`ComponentSelector Range/Disparity` |
| `cap.frame.format.3d.confidence` | Per-pixel confidence component frames (delivery facet; semantics→§6) `[CEILING]` | genicam:`Confidence*`/`ComponentSelector Confidence` |
| `cap.frame.format.compressed_perframe` | Compressed-per-frame payloads (MJPEG/coded) | mf:`MJPG/H264/HEVC`; pipewire:`ENCODED`; v4l2:`HEVC/VP8/VP9`+MJPEG; libcamera:`MJPEG`; camera2:`JPEG/JPEG_R/HEIC`; genicam:`ImageCompressionMode` `[CEILING]` |
| `cap.frame.format.opaque_private` | Implementation-private/opaque format (no CPU layout) | camera2:`PRIVATE`/`RAW_PRIVATE`/`BLOB`; web:`VideoFrame` GPU-resident |
| `cap.frame.format.enumerate` | Enumerate which delivery formats a stream supports | avf:`availableVideoCVPixelFormatTypes`; camera2:ImageFormat set/`OUTPUT_IMAGE_FORMAT_*`; mf:`MFVideoFormat_*`; pipewire:`SPA_PARAM_EnumFormat`; v4l2:format enum; web:`VideoFrame.format`; libcamera:`StreamFormats`; genicam:`PixelFormat` `[CEILING]` |
| `cap.frame.format.select` | Request a specific delivery format for a stream | avf:`videoSettings`(pixelFormatType); camera2:`AImageReader_new(format)`; mf:`SetCurrentMediaType`; pipewire:`SPA_PARAM_Format`; v4l2:`S_FMT`; web:constraints/`copyTo`; libcamera:`pixelFormat`; genicam:`PixelFormat` `[CEILING]` |

### 8b. zero-copy import targets / pooling / mapping
| capability id | meaning (≤1 line) | normalizes |
|---|---|---|
| `cap.frame.zerocopy.iosurface` | Hand off as IOSurface/CVPixelBuffer | avf:`CVPixelBufferGetIOSurface`/`CVMetalTextureCache*` |
| `cap.frame.zerocopy.ahardwarebuffer` | Hand off as AHardwareBuffer | camera2:`AImage_getHardwareBuffer`/Vulkan `external_memory_android_hardware_buffer`/EGL |
| `cap.frame.zerocopy.dmabuf` | Hand off as dmabuf fd(s) | libcamera:`FrameBuffer::Plane.fd`; pipewire:`SPA_DATA_DmaBuf`; v4l2:`EXPBUF`/`MEMORY_DMABUF` |
| `cap.frame.zerocopy.dmabuf.modifiers` | Negotiate DRM format modifiers (tiled/compressed) | pipewire:`SPA_FORMAT_VIDEO_modifier`+`DONT_FIXATE`; libcamera:modifier |
| `cap.frame.zerocopy.dmabuf.explicit_sync` | Carry explicit GPU sync fences w/ the dmabuf | pipewire:`SPA_DATA_SyncObj`/`spa_meta_sync_timeline`; libcamera:`Request::addBuffer(...,Fence)` |
| `cap.frame.zerocopy.d3d11` | Hand off as D3D11 texture | mf:`IMFDXGIBuffer::GetResource`/`MFCreateDXGIDeviceManager`/`MF_SA_D3D11_AWARE`; WinRT:`IDirect3DSurface` |
| `cap.frame.zerocopy.webgpu_videoframe` | Hand off as WebGPU external texture | web:`GPUDevice.importExternalTexture({source:VideoFrame})` |
| `cap.frame.zerocopy.import_external` | Accept an app-allocated external buffer as capture target | libcamera:external dmabuf `FrameBuffer`; v4l2:`USERPTR`/`DMABUF` import; genicam:user `BUFFER_HANDLE` `[CEILING]` |
| `cap.frame.map.cpu` | Map frame into CPU address space for read | avf:CVPixelBuffer base addr; camera2:`AImage_getPlaneData`; mf:`IMFMediaBuffer::Lock`/`IMF2DBuffer::Lock2D`; pipewire:`SPA_DATA_MemPtr`/`MemFd`; v4l2:`MMAP`+mmap; web:`VideoFrame.copyTo`; libcamera:plane mmap; genicam:CPU buffer `[CEILING]` |
| `cap.frame.map.plane_layout` | Report per-plane stride/offset/pixel-stride/count | avf:CVPixelBuffer plane API; camera2:`AImage_getPlaneRowStride`/`PixelStride`/`NumberOfPlanes`; mf:`IMF2DBuffer` stride; pipewire:`spa_chunk`/`SPA_PARAM_BUFFERS_*`; v4l2:`bytesperline`/`v4l2_plane[]`; web:`PlaneLayout`; libcamera:`Plane.offset/length`; genicam:`LinePitch`/`PayloadSize` `[CEILING]` |
| `cap.frame.map.cache_hint` | Control cache-coherency of mapped buffer memory | v4l2:`MEMORY_FLAG_NON_COHERENT`/`BUF_CAP_MMAP_CACHE_HINTS` |
| `cap.frame.pool.allocate` | Allocate/own a recycled buffer pool for a stream | avf:`CVPixelBufferPool*`; camera2:`AImageReader_new(...maxImages)`; v4l2:`REQBUFS`/`CREATE_BUFS`; pipewire:`SPA_PARAM_Buffers`; libcamera:`FrameBufferAllocator`; genicam:`DSAllocAndAnnounceBuffer` `[CEILING]` |
| `cap.frame.pool.queue_depth` | Set/read the pool's buffer-queue depth | camera2:`getMaxImages`/`setImageQueueDepth`; v4l2:`REQBUFS count`/`MIN_BUFFERS_FOR_CAPTURE`; pipewire:`SPA_PARAM_BUFFERS_buffers`; web:`maxBufferSize`; libcamera:`bufferCount`; genicam:`TransferQueueMaxBlockCount` `[CEILING]` |
| `cap.frame.pool.lifecycle_events` | Be notified when pool buffers are added/removed | camera2:`setBufferRemovedListener`/`discardFreeBuffers`; pipewire:`add_buffer`/`remove_buffer` |
| `cap.frame.pool.recycle` | Explicitly return/recycle a buffer to the pool | camera2:`AImage_delete`/`_deleteAsync`; pipewire:`pw_stream_queue_buffer`; v4l2:`QBUF`; web:`VideoFrame.close()`; libcamera:`Request::reuse`; mf:`Unlock`/release |
| `cap.frame.deliver.next` | Acquire the next/oldest queued frame | camera2:`AImageReader_acquireNextImage`; pipewire:`pw_stream_dequeue_buffer`; v4l2:`DQBUF`; web:ReadableStream read; libcamera:`requestCompleted`; avf:`didOutputSampleBuffer`; mf:`ReadSample` |
| `cap.frame.drop.policy_latest` | Back-pressure keeping only the freshest frame | avf:`alwaysDiscardsLateVideoFrames`; camera2:`acquireLatestImage`/`STRATEGY_KEEP_ONLY_LATEST`; web:source-side drop; mf:oldest auto-Close |
| `cap.frame.drop.policy_block` | Back-pressure stalling producer when pool exhausted | camera2:`maxImages` exhaustion/`STRATEGY_BLOCK_PRODUCER`; pipewire:hold stalls; v4l2:no-free-buffer stall; libcamera:queue-recycle stall |
| `cap.frame.drop.signal` | Signal that a frame was dropped/lost/corrupt | avf:`didDropSampleBuffer`; mf:`STREAMTICK` gap; pipewire:`CHUNK_FLAG_CORRUPTED`/`_EMPTY`; v4l2:`BUF_FLAG_ERROR`/seq gaps; libcamera:`RequestCancelled` |
| `cap.frame.drop.reason` | Report the *reason* a frame was dropped | avf:`DroppedFrameReason`(FrameWasLate/OutOfBuffers/Discontinuity); mf:`PROPSETID_VIDCAP_DROPPEDFRAMES`; (camera2/v4l2/libcamera signal-only `?`) |
| `cap.frame.drop.stats` | Cumulative dropped-frame counters | mf:`PROPSETID_VIDCAP_DROPPEDFRAMES`; v4l2:`sequence` gap counting |
| `cap.frame.cookie` | Attach an opaque app tag carried with a buffer | libcamera:`FrameBuffer::cookie`/`Request::cookie` |

---

## 9. timing — `cap.timing.*`

| capability id | meaning (≤1 line) | normalizes |
|---|---|---|
| `cap.timing.frame.timestamp` | Per-frame capture timestamp delivered with each frame | avf:`CMSampleBufferGetPresentationTimeStamp`; camera2:`SENSOR_TIMESTAMP`; libcamera:`SensorTimestamp`/`FrameMetadata::timestamp`; mf:`IMFSample::GetSampleTime`; pipewire:`spa_meta_header.pts`; v4l2:`v4l2_buffer.timestamp`; web:`VideoFrame.timestamp`; uvc:per-frame PTS; genicam:`ChunkTimestamp` |
| `cap.timing.frame.timestamp.clock-domain-id` | Which monotonic/realtime clock domain the stamp is in | avf:host-time clock; camera2:`SENSOR_INFO_TIMESTAMP_SOURCE`; libcamera:CLOCK_BOOTTIME; mf:QPC; pipewire:graph clock; v4l2:`TIMESTAMP_MONOTONIC`/`_UNKNOWN`/`_COPY` |
| `cap.timing.frame.timestamp.clock-domain-select` | Choose which clock domain stamps are against | camera2:`OutputConfiguration.setTimestampBase` |
| `cap.timing.frame.timestamp.capture-point` | Timestamp marks start-of-exposure vs end-of-readout | camera2:`SENSOR_READOUT_TIMESTAMP`+`setReadoutTimestampEnabled`; v4l2:`TSTAMP_SRC_SOE`/`_EOF` |
| `cap.timing.frame.wallclock` | Per-frame realtime/wall-clock stamp paired to monotonic | libcamera:`FrameWallClock`; mf:`MetadataTimeStamps.Presentation` |
| `cap.timing.frame.sequence-id` | Monotonic frame counter whose gaps reveal drops | libcamera:`FrameMetadata::sequence`; pipewire:`spa_meta_header.seq`; v4l2:`v4l2_buffer.sequence`; web:`presentedFrames`; uvc:FID toggle; genicam:`ChunkFrameID` |
| `cap.timing.frame.request-id` | Per-request frame number correlating request→frame | camera2:`CaptureResult.getFrameNumber()`/`SYNC_FRAME_NUMBER`; mf:`REQUESTED_FRAME_SETTING_ID` |
| `cap.timing.rolling-shutter.skew` | First-to-last-row readout skew per frame (canonical; absorbs meta dup) | camera2:`SENSOR_ROLLING_SHUTTER_SKEW`; libcamera:`draft::SensorRollingShutterSkew` |
| `cap.timing.rolling-shutter.line-time-derived` | Skew reconstructable from line/blanking + SOE stamp | v4l2:`IMAGE_SOURCE`(vblank/hblank)+`TSTAMP_SRC_SOE`/`SUBDEV_G/S_FRAME_INTERVAL` |
| `cap.timing.sensor-readout-rate` | Measured per-frame sensor readout rate | mf:`MF_CAPTURE_METADATA_SENSORFRAMERATE` |
| `cap.timing.settings-applied.latency` | How many frames after submission a setting takes effect | camera2:`SYNC_MAX_LATENCY`/`REQUEST_PIPELINE_DEPTH`; libcamera:`draft::MaxLatency`/`PipelineDepth` |
| `cap.timing.exposure-event` | Async exposure-end/frame-trigger event w/ timestamp `[CEILING]` | genicam:`EventExposureEnd`/`EventFrameTrigger`/`AcquisitionStatus[ExposureActive]` |
| `cap.timing.frame-sync-event` | Async per-frame VSYNC/frame-start event | v4l2:`V4L2_EVENT_VSYNC`/`_FRAME_SYNC` (`SUBSCRIBE_EVENT`/`DQEVENT`) |
| `cap.timing.device-clock.rate` | Negotiated device-clock frequency backing device stamps | uvc:`dwClockFrequency`; genicam:`DeviceClockSelector`+`Frequency` |
| `cap.timing.device-clock.pts` | Per-frame PTS in raw device-clock units | uvc:payload PTS; mf:`MetadataTimeStamps.Device` |
| `cap.timing.device-clock.scr-sof` | Device source-clock sampled w/ 1kHz USB SOF (device↔host map) | uvc:SCR=STC+SOF; v4l2:`V4L2_META_FMT_UVC`/`_UVC_MSXU_1_5`/`META_CAPTURE`; mf:`MetadataId_UsbVideoHeader` |
| `cap.timing.device-clock.timestamp-counter` | Read/reset/latch a free-running device counter `[CEILING]` | genicam:`Timestamp`/`TimestampReset`/`TimestampLatch`+`Value` |
| `cap.timing.av-clock.shared-session` | A+V in one session share a common capture time-base | avf:`AVCaptureSession.synchronizationClock`; mf:QPC A/V common clock; web:`getUserMedia({video,audio})` timeline; (SDI) DeckLink embedded-audio clock-locked |
| `cap.timing.av-clock.cross-output-map` | Map a time value across two capture clocks | avf:`CMSyncConvertTime`/`CMSyncGetRelativeRate`/`AVCaptureInputPort.clock`; pipewire:`pw_stream_get_time_n`/`get_nsec` |
| `cap.timing.av-clock.app-timebase` | App-controlled timeline rebased onto a master clock | avf:`CMTimebase`/`SetRate`/`CopyMasterClock` |
| `cap.timing.av-sync.device-ts-trust` | Trust device frame ts vs rebase (drift threshold) `[CEILING]` | OBS:`timing_adjust = os_gettime_ns()-frame.timestamp`, trust within `MAX_TS_VAR` |
| `cap.timing.av-sync.rebase` | Per-source rebasing of frame ts onto the host clock `[CEILING]` | OBS:per-source `timing_adjust`; `obs_source_set_async_unbuffered` |
| `cap.timing.av-sync.jitter-buffer` | Jitter/buffering to nearest target time, drop stale `[CEILING]` | OBS:`get_closest_frame`(2ms slack, `MAX_ASYNC_FRAMES`) |
| `cap.timing.av-sync.ts-jump-recovery` | Detect+recover from discontinuous ts jump `[CEILING]` | OBS:`handle_ts_jump`(>2s→reset+flush) |
| `cap.timing.av-sync.offset` | Apply/maintain an A/V sync offset w/ smoothing `[CEILING]` | OBS:`sync_offset`+`TS_SMOOTHING_THRESHOLD`(70ms) |
| `cap.timing.discontinuity-marker` | Explicit gap/discontinuity tick, no frame payload (drop-reason→§8) | mf:`STREAMTICK`; pipewire:`CHUNK_FLAG_CORRUPTED`/`_EMPTY` |
| `cap.timing.imu-correlation.clock` | Frame ts mappable into the motion-sensor clock domain | camera2:`TIMESTAMP_SOURCE_REALTIME`↔`SensorEvent`; libcamera:`SensorTimestamp` CLOCK_BOOTTIME↔IMU; avf:`CMMotionManager`→`CMSyncConvertTime` |
| `cap.timing.timecode.smpte` | SMPTE timecode (RP188/VITC/LTC) per frame | v4l2:`BUF_FLAG_TIMECODE`+`v4l2_timecode`; avf:`AVMediaTypeTimecode`+`kCMTimeCodeFormatType_TimeCode32`; (SDI) DeckLink `IDeckLinkTimecode` |
| `cap.timing.genlock.sync-pins` | Hardware frame-sync/genlock pins (XVS/XHS) `[CEILING]` | genicam:`DigitalIOControl`(`LineSource`); (sensor) XVS/XHS/XMASTER — embedded-Linux only |
| `cap.timing.genlock.sdi-reference` | SDI genlock / tri-level / house-reference sync `[CEILING]` | (capture-card) DeckLink `HasReferenceInput`; v4l2:`QUERY/G/S/ENUM_DV_TIMINGS`(partial) |
| `cap.timing.genlock.ptp-clock-sync` | IEEE-1588/PTP network clock discipline `[CEILING]` | genicam:`PtpControl`(`PtpEnable`/`PtpStatus`/`PtpOffsetFromMaster`/`PtpClockID`) |
| `cap.timing.frame-rate.clamp` | Frame-rate / frame-duration limit control (canonical; absorbs capture dup) | avf:`activeVideoMin/MaxFrameDuration`/`AVFrameRateRange`; camera2:`AE_TARGET_FPS_RANGE`/`SENSOR_FRAME_DURATION`; libcamera:`FrameDurationLimits`; genicam:`AcquisitionFrameRate`/`LineRate` |

---

## 10. metadata — `cap.meta.*`
Per-frame metadata the camera stack itself emits. "camera emits payload X" is here; "decode X into a result" is downstream (`barcode`/CV-ML).

### 10a. access
| capability id | meaning (≤1 line) | normalizes |
|---|---|---|
| `cap.meta.access.bag` | Per-frame metadata as a keyed attachment/bag w/ each frame | avf:`kCMSampleBufferAttachmentKey_*`; camera2:`CaptureResult`; mf:`MFSampleExtension_CaptureMetadata`; libcamera:`Request::metadata()`; v4l2:`META_CAPTURE` node; uvc:`MSXU_CONTROL_METADATA`; genicam:`ChunkDataControl`/`ChunkModeActive`; pipewire:`SPA_PARAM_Meta` |
| `cap.meta.access.rawblob` | Raw unparsed driver/device metadata blob | mf:`FRAME_RAWSTREAM`/`KSCAMERA_METADATA_ITEMHEADER`; v4l2:`V4L2_META_FMT_UVC`; uvc:payload-header raw; genicam:`ChunkImage`/`ChunkXMLEnable` |
| `cap.meta.access.selectable` | Per-chunk selectable + extensible enable of emitted metadata | mf:`EXTENDED_METADATA`; genicam:`ChunkSelector`/`ChunkEnable[]`; v4l2:`CAP_META_CAPTURE` negotiation |

### 10b. applied 3A / settings echo (canonical home for per-frame applied values)
| capability id | meaning (≤1 line) | normalizes |
|---|---|---|
| `cap.meta.applied.exposuretime` | Per-frame applied exposure/shutter duration | avf:EXIF`ExifExposureTime`; camera2:`SENSOR_EXPOSURE_TIME`(result); mf:`EXPOSURE_TIME`; libcamera:`ExposureTime`(meta); genicam:`ChunkExposureTime` |
| `cap.meta.applied.iso` | Per-frame applied ISO/sensitivity | avf:EXIF`ExifISOSpeedRatings`; camera2:`SENSOR_SENSITIVITY`(result); mf:`ISO_SPEED` |
| `cap.meta.applied.analogdigitalgain` | Per-frame applied analog vs digital gain split | camera2:`SENSOR_SENSITIVITY`+`POST_RAW_SENSITIVITY_BOOST`; mf:`ISO_GAINS`; libcamera:`AnalogueGain`+`DigitalGain`(meta); genicam:`ChunkGain` |
| `cap.meta.applied.expcompensation` | Per-frame applied EV/exposure-compensation | camera2:`AE_EXPOSURE_COMPENSATION`(result); mf:`EXPOSURE_COMPENSATION`; libcamera:`ExposureValue`(meta) |
| `cap.meta.applied.aperture` | Per-frame applied lens aperture (f-number) | avf:EXIF`ExifApertureValue`/`FNumber`; camera2:`LENS_APERTURE`(result) |
| `cap.meta.applied.lensposition` | Per-frame applied focus/lens position | camera2:`LENS_FOCUS_DISTANCE`+`LENS_STATE`; mf:`LENS_POSITION`; libcamera:`LensPosition`(meta) |
| `cap.meta.applied.frameduration` | Per-frame applied frame-duration/readout cadence | camera2:`SENSOR_FRAME_DURATION`; mf:`SENSORFRAMERATE`; libcamera:`FrameDuration`(meta) |
| `cap.meta.applied.wbtemperature` | Per-frame applied WB correlated color temperature | mf:`WHITEBALANCE`(Kelvin); libcamera:`ColourTemperature`(meta) |
| `cap.meta.applied.wbgains` | Per-frame applied per-channel WB gains | avf:`deviceWhiteBalanceGains`; camera2:`COLOR_CORRECTION_GAINS`; mf:`WHITEBALANCE_GAINS`; libcamera:`ColourGains`(meta) |
| `cap.meta.applied.colortransform` | Per-frame applied CCM | camera2:`COLOR_CORRECTION_TRANSFORM`; libcamera:`ColourCorrectionMatrix`(meta) |
| `cap.meta.applied.tonemapcurve` | Per-frame applied tonemap/contrast curve | camera2:`TONEMAP_CURVE`/`GAMMA` |
| `cap.meta.applied.blacklevel` | Per-frame applied/dynamic black levels | camera2:`SENSOR_DYNAMIC_BLACK_LEVEL`/`BLACK_LEVEL_PATTERN`; libcamera:`SensorBlackLevels`(meta) |
| `cap.meta.applied.scenemode` | Per-frame applied scene mode | mf:`SCENE_MODE` |
| `cap.meta.applied.flashstate` | Per-frame applied flash fired/power state | camera2:`FLASH_STATE`; mf:`FLASH`/`FLASH_POWER` |
| `cap.meta.applied.zoomfactor` | Per-frame applied zoom factor | camera2:`CONTROL_ZOOM_RATIO`; mf:`ZOOMFACTOR`(Q16) |
| `cap.meta.applied.cropregion` | Per-frame applied crop/scaler region (echo; set→§3f) | camera2:`SCALER_CROP_REGION`; libcamera:`ScalerCrop`(meta); pipewire:`SPA_META_VideoCrop`; web:`codedRect`/`visibleRect` |
| `cap.meta.applied.digitalwindow` | Per-frame applied digital-window/auto-framing rect | mf:`DIGITALWINDOW`; camera2:`CONTROL_AUTOFRAMING_STATE` |
| `cap.meta.applied.binningreadout` | Per-frame applied binning/readout-mode echo | camera2:`SENSOR_RAW_BINNING_FACTOR_USED`; genicam:`ChunkBinning*`/`ChunkDecimation*` |
| `cap.meta.applied.geometryecho` | Per-frame echo of applied geometry/pixfmt/ROI/flip | genicam:`ChunkOffsetX/Y`/`Width/Height`/`PixelFormat`/`ReverseX/Y`/`LinePitch` |

### 10c. convergence states / regions
| capability id | meaning (≤1 line) | normalizes |
|---|---|---|
| `cap.meta.state.ae` | Per-frame AE convergence state | camera2:`AE_STATE`; libcamera:`AeState`(meta) |
| `cap.meta.state.af` | Per-frame AF convergence state | avf:`isAdjustingFocus`; camera2:`AF_STATE`; mf:`FOCUSSTATE`; libcamera:`AfState`/`AfPauseState`(meta) |
| `cap.meta.state.awb` | Per-frame AWB convergence state | camera2:`AWB_STATE`; libcamera:`draft::AwbState`/`AwbLocked`(meta) |
| `cap.meta.state.exptargetoffset` | Per-frame metered exposure offset from AE target | avf:`exposureTargetOffset` |
| `cap.meta.regions.ae` | Per-frame applied AE metering rectangles | camera2:`AE_REGIONS`(result) |
| `cap.meta.regions.af` | Per-frame applied AF metering rectangles | camera2:`AF_REGIONS`(result) |
| `cap.meta.regions.awb` | Per-frame applied AWB metering rectangles | camera2:`AWB_REGIONS`(result) |

### 10d. detection metadata (OS-emitted; decode downstream)
| capability id | meaning (≤1 line) | normalizes |
|---|---|---|
| `cap.meta.face.rect` | Per-frame detected face bounding boxes | avf:`AVMetadataFaceObject.bounds`; camera2:`STATISTICS_FACES`(rects); mf:`FACEROIS`; libcamera:`draft::FaceDetectFaceRectangles`; web:`DetectedFace.boundingBox` |
| `cap.meta.face.id` | Per-frame stable per-face tracking ID | avf:`faceID`; camera2:`STATISTICS_FACES`(ids); libcamera:`FaceDetectFaceIds` |
| `cap.meta.face.score` | Per-frame per-face confidence score | camera2:`STATISTICS_FACES`(scores); mf:`FaceRectInfo.ConfidenceLevel`; libcamera:`FaceDetectFaceScores` |
| `cap.meta.face.landmarks` | Per-frame face landmark points | camera2:`STATISTICS_FACES` FULL landmarks; libcamera:`FaceDetectFaceLandmarks`; mf:WSE`FACEMETADATA`; web:`DetectedFace.landmarks` |
| `cap.meta.face.pose` | Per-frame face roll/yaw angles | avf:`rollAngle`/`yawAngle` |
| `cap.meta.face.expression` | Per-frame face blink/smile scores | mf:`FACEROICHARACTERIZATIONS`{Blink,Smile}; camera2:`EXTENDED_FACEDETECTION` `_BLINK`/`_SMILE` |
| `cap.meta.body.rect` | Per-frame detected human/pet body & head boxes | avf:`AVMetadataBodyObject`/`HumanFullBody`/`Cat/DogBody`/`Head` |
| `cap.meta.salient.rect` | Per-frame salient-object bounding box | avf:`AVMetadataSalientObject` |
| `cap.meta.code.payload` | Per-frame OS-emitted barcode/QR payload + geometry | avf:`AVMetadataMachineReadableCodeObject.stringValue`/`.corners`/`.descriptor`; web:`DetectedBarcode{rawValue,boundingBox,cornerPoints}` |
| `cap.meta.scene.change` | Per-frame scene-change detection flag | camera2:`AF_SCENE_CHANGE`(result) |
| `cap.meta.scene.flicker` | Per-frame detected flicker / power-line freq | camera2:`STATISTICS_SCENE_FLICKER`; libcamera:`AeFlickerDetected`(meta) |
| `cap.meta.scene.nighthint` | Per-frame night/low-light scene indicator | camera2:`EXTENSION_NIGHT_MODE_INDICATOR`/`LOW_LIGHT_BOOST_STATE` |
| `cap.meta.scene.illuminance` | Per-frame estimated scene illuminance (lux) | libcamera:`Lux`(meta) |
| `cap.meta.scene.focusfom` | Per-frame focus figure-of-merit | libcamera:`FocusFoM`(meta) |

### 10e. statistics maps / sensor telemetry
| capability id | meaning (≤1 line) | normalizes |
|---|---|---|
| `cap.meta.stats.histogram` | Per-frame camera-emitted histogram | camera2:`STATISTICS_HISTOGRAM_MODE`; mf:`HISTOGRAM`; v4l2:`VSP1_HGO`/`_HGT` |
| `cap.meta.stats.sharpnessmap` | Per-frame sharpness/AF statistics map | camera2:`STATISTICS_SHARPNESS_MAP_MODE` |
| `cap.meta.stats.lensshadingmap` | Per-frame lens-shading gain grid | camera2:`STATISTICS_LENS_SHADING_MAP`; libcamera:`draft::LensShadingMapMode`(availability) |
| `cap.meta.stats.hotpixelmap` | Per-frame hot-pixel coordinate map | camera2:`STATISTICS_HOT_PIXEL_MAP` |
| `cap.meta.stats.predictedcolor` | Per-frame predicted color gains/transform | camera2:`STATISTICS_PREDICTED_COLOR_GAINS`/`_TRANSFORM`(deprecated `?`) |
| `cap.meta.stats.ispblob` | Per-frame opaque ISP statistics blob (vendor) | libcamera:`rpi::StatsOutputEnable`/`Bcm2835StatsOutput`/`PispStatsOutput`; v4l2:`RK_ISP1_STAT_3A`/`IPU3_3A`/`MALI_C55_STATS`/`C3ISP_STATS`/`RPI_BE`/`FE_STATS` |
| `cap.meta.stats.ispparams` | Per-frame ISP config/params metadata (round-tripped) | v4l2:`RK_ISP1_PARAMS`/`EXT_PARAMS`/`IPU3_PARAMS`/`MALI_C55_PARAMS`/`C3ISP_PARAMS`/`RPI_BE_CFG`/`FE_CFG` |
| `cap.meta.ois.samples` | Per-frame OIS displacement sample stream (canonical; absorbs timing dup) | camera2:`STATISTICS_OIS_DATA_MODE`/`_OIS_TIMESTAMPS`/`_OIS_X_SHIFTS`/`_OIS_Y_SHIFTS` |
| `cap.meta.sensortemperature` | Per-frame sensor die temperature | libcamera:`SensorTemperature`(meta) |

### 10f. embedded / transport / misc
| capability id | meaning (≤1 line) | normalizes |
|---|---|---|
| `cap.meta.embedded.sensorlines` | Per-frame embedded CSI-2 sensor metadata lines `[CEILING]` | v4l2:`GENERIC_8`/`CSI2_10..24`; beyond-os:CSI-2 embedded lines |
| `cap.meta.embedded.uvcheader` | Per-frame UVC payload-header status bits (FID/EOF/error/STI) | uvc:UVC 1.5 §2.4.3.3; v4l2:`V4L2_META_FMT_UVC`/`_UVC_MSXU_1_5` flags |
| `cap.meta.chunk.iostate` | Per-frame I/O line / counter / timer state at frame-start | genicam:`ChunkLineStatusAll`/`ChunkCounterValue`/`ChunkTimerValue`/`ChunkEncoderValue` |
| `cap.meta.chunk.frameid` | Per-frame camera-emitted frame ID (chunk) | genicam:`ChunkFrameID`/`ChunkTransferBlockID` |
| `cap.meta.transform.orientation` | Per-frame applied rotation/flip orientation (echo; control→§12) | pipewire:`SPA_META_VideoTransform`; web:`VideoFrame.{rotation,flip}` |
| `cap.meta.damage.region` | Per-frame changed/damage region rects | pipewire:`SPA_META_VideoDamage` |
| `cap.meta.illumination.ir` | Per-frame IR active-illumination on/off (canonical; absorbs depth dup) | mf:`FRAME_ILLUMINATION`/`InfraredMediaFrame.IsIlluminated` |
| `cap.meta.segmask` | Per-frame camera/driver-emitted segmentation mask | mf:`BACKGROUNDSEGMENTATION_MASK` |
| `cap.meta.exifblob` | Per-frame camera-stack-emitted EXIF/TIFF blob | mf:`MF_CAPTURE_METADATA_EXIF`; camera2:`JPEG_*` |
| `cap.meta.bracketcorrelation` | Per-frame bracket/variable-sequence setting-ID tag | mf:`REQUESTED_FRAME_SETTING_ID` |
| `cap.meta.event.exposurephase` | Async camera event on exposure-end/frame-trigger w/ timestamp `[CEILING]` | genicam:`EventExposureEnd`/`EventFrameTrigger`/`EventError` |
| `cap.meta.custom.blob` | Per-frame OEM/vendor custom metadata blob (gyro/OIS/IMU OEM payloads) | mf:`MF_CAPTURE_METADATA_<Custom GUID>`; libcamera:`DebugMetadataEnable`/`rpi::CnnOutputTensor`; pipewire:`SPA_PARAM_Tag` |
| `cap.meta.hdr.static` | HDR static metadata emitted with the stream — Rec.2020 + PQ/HLG, mastering-display, MaxCLL/MaxFALL | [capture-card] DeckLink `IDeckLinkVideoFrameMetadataExtensions`; mf:`MF_MT_*` HDR attributes; camera2:`DynamicRangeProfiles` (transfer only); CEA-861.3 |

---

## 11. egress + test ingest — `cap.egress.*` / `cap.testsrc.*`
The install/packaging ceiling is modeled as its own capability dimension (each native publish path carries a different install status). Frame push/pull plumbing overlaps the frame-transport seam (see §14).

| capability id | meaning (≤1 line) | normalizes |
|---|---|---|
| `cap.egress.publish` | Register an app frame surface as an OS-visible virtual camera | avf:`CMIOExtensionProvider/Device/Stream`+`OSSystemExtensionRequest`; mf:`MFCreateVirtualCamera`/`IMFVirtualCamera.Start`/DShow source filter; pipewire:`pw_stream` OUTPUT `Video/Source`; v4l2:v4l2loopback OUTPUT |
| `cap.egress.publish.frame_push` | Push (vend) an app-produced frame to consumers | avf:`CMIOExtensionStream.sendSampleBuffer`; mf:custom-source `IMFSample` feed; pipewire:on-demand produce; v4l2:write()/QBUF OUTPUT |
| `cap.egress.publish.frame_pull` | Pull-model frame delivery driven by consumer demand | avf:`StreamDirectionSink`+`consumeSampleBufferFromClient`; pipewire:`pw_stream_trigger_process`/`is_driving` |
| `cap.egress.publish.local_only_fallback` | Honest local-only surface where no OS publish path exists | camera2:in-process synthetic; libcamera:no-publish→separate layer; web:canvas/track-generator in-page; avf:iOS no publish |
| `cap.egress.consumer.attach_events` | Attach/detach notifications as consumers come+go | avf:`connectClient`/`disconnectClient`/`streamingClients`; mf:`IMFMediaEvent` on Start; pipewire:PAUSED→STREAMING |
| `cap.egress.consumer.identity` | Per-consumer identity to the producer (pid/signing-id) | avf:`CMIOExtensionClient.clientID`/`.pid`/`.signingID`/`authorizedToStartStreamForClient` |
| `cap.egress.consumer.format_negotiate` | Negotiate format/res/frame-duration per consumer | avf:`CMIOExtensionStreamFormat`+`ActiveFormatIndex`; mf:per-stream media types+`IAMStreamConfig`; v4l2:producer S_FMT defines visible format |
| `cap.egress.controls.expose` | Expose app-defined controls ON the published cam | avf:CMIOExtension property bags+`CMIOObjectGet/SetPropertyData`; mf:KSProperty/`IKsControl` on vcam; pipewire:ad-hoc `SPA_PARAM_PropInfo` `?` |
| `cap.egress.controls.config_dialog` | Per-consumer-invoked config UI surfaced by the published cam | mf:DShow `ISpecifyPropertyPages` |
| `cap.egress.share_mode` | Controlling-vs-sharing arbitration among consumers | mf:`FRAMESERVER_SHARE_MODE`+`FRAMESERVER_SHARED` |
| `cap.egress.flow_control` | Sink-stream flow-control/buffering knobs | avf:`StreamSinkBuffersRequiredForStartup`/`SinkBufferQueueSize`/`SinkEndOfData`/`SinkBufferUnderrunCount` |
| `cap.egress.wrap_physical` | Published cam wraps/arbitrates real physical cameras | mf:`IMFVirtualCamera.AddDeviceSourceInfo`+`ASSOCIATED_CAMERA_SOURCES` |
| `cap.egress.in_use_feedback` | Producer learns published cam is in use / invalidate on teardown | mf:Remove→`DEVICE_INVALIDATED`; pipewire:STREAMING state; avf:`streamingClients` non-empty |
| `cap.egress.mic_association` | Associate a published mic/audio with the published cam | avf:CMIOExtension audio-stream pairing `?`; mf:vcam audio companion `?` |
| `cap.egress.lifetime.session` | Published cam lives only for the producing process/session | mf:`MFVirtualCameraLifetime Session`; avf:sysext active while host runs; pipewire:node w/ pw_core |
| `cap.egress.lifetime.system` | Published cam persists OS-wide across reboot/all-users | mf:`MFVirtualCameraLifetime System`+AllUsers; avf:installed sysext persists |
| `cap.egress.legacy_consumer_compat` | Bridge so legacy-only consumers (DShow/V4L2) see the published cam | mf:DShow filter+DevicePath reg gotcha; pipewire:pw-v4l2/v4l2loopback bridge; v4l2:`exclusive_caps` OUTPUT→CAPTURE flip |
| `cap.egress.install.systemext` | Install ceiling: signed+notarized System Extension w/ user approval | avf:`OSSystemExtensionRequest`+sysext entitlements+`CMIOExtensionMachServiceName` |
| `cap.egress.install.com_admin` | Install ceiling: admin HKLM COM registration of a media source/filter | mf:HKLM CLSID registration (admin)+regsvr32 DShow |
| `cap.egress.install.kernel_module` | Install ceiling: out-of-tree kernel module (DKMS) | v4l2:v4l2loopback modprobe/DKMS |
| `cap.egress.install.userspace_node` | Install ceiling: userspace graph/session node, no driver install | pipewire:`pw_stream` node |
| `cap.egress.install.privileged_denied` | Publish path exists but gated to system/privileged apps | camera2:`VirtualDeviceManager.createVirtualCamera`(@SystemApi+CDM); camera2:DeviceAsWebcam system-image-only |
| `cap.egress.scope.virtual_device_only` | Published cam visible only within a scoped virtual-device context | camera2:`VirtualCamera` scoped by `getDeviceId`/`POLICY_TYPE_CAMERA` |
| `cap.testsrc.provider` | In-process synthetic camera provider from the same publish machinery | mf:custom-source consumed in-process; web:`MediaStreamTrackGenerator`/`captureStream`; genicam:GenTL software/replay Device `[CEILING]` |
| `cap.testsrc.frame_feed` | Feed app-produced frames into the synthetic source | web:`MediaStreamTrackGenerator.writable<VideoFrame>`/`requestFrame`; mf:`IMFSample` feed |
| `cap.testsrc.test_pattern` | Emit a synthetic test pattern/payload from the source | genicam:`TestPayloadFormatMode` `[CEILING]`; libcamera:`draft::TestPatternMode`; v4l2:`TIMESTAMP_COPY` loopback crumb |
| `cap.testsrc.manual_clock` | Drive synthetic-source timing manually (frame-on-demand) | avf:`customClockConfiguration` sink variant; web:`captureStream(0)`+`requestFrame`; pipewire:`trigger_process` |

---

## 12. OS integration — `cap.os.*`

| capability id | meaning (≤1 line) | normalizes |
|---|---|---|
| `cap.os.consent.prompt` | Trigger the user-facing camera-access prompt at runtime | avf:`requestAccessForMediaType:`; camera2:`requestPermissions`; web:first `getUserMedia()` prompt; pipewire:`AccessCamera(handle_token)`→portal; mf:`AppCapability`→prompt |
| `cap.os.consent.status_query` | Query authorization status without prompting | avf:`authorizationStatusForMediaType:`; web:`permissions.query({name:"camera"})`; mf:`AppCapability.CheckAccess()`/ConsentStore Value |
| `cap.os.consent.status_change_event` | Be notified when authorization status changes | web:`PermissionStatus` change/onchange |
| `cap.os.consent.rationale_hint` | Query whether to show a pre-prompt rationale UI | camera2:`shouldShowRequestPermissionRationale` |
| `cap.os.consent.deeplink_settings` | Open the OS privacy settings page for camera consent | mf:`ms-settings:privacy-webcam` via `Launcher.LaunchUriAsync` `?` |
| `cap.os.consent.usage_string` | Static packaging: mandated human-readable purpose string | avf:`NSCameraUsageDescription` |
| `cap.os.consent.manifest_capability` | Static packaging: manifest/appx capability declaration | camera2:`<uses-permission ...CAMERA>`; mf:manifest `DeviceCapability webcam` |
| `cap.os.consent.sandbox_entitlement` | Static packaging: sandbox entitlement for camera access | avf:`com.apple.security.device.camera` |
| `cap.os.feature.requirement_declaration` | Static packaging: declare camera-hardware (sub)feature requirement | camera2:`<uses-feature> android.hardware.camera*`/`.autofocus`/`.flash`/`.level.full`/`.capability.raw`/`.manual_sensor`/`.concurrent`/`.external`/`.front` |
| `cap.os.consent.foreground_service_decl` | Static packaging: foreground-service-type for non-fg camera open | camera2:`FOREGROUND_SERVICE_CAMERA`+`foregroundServiceType="camera"` |
| `cap.os.consent.policy_gate` | Org/admin policy force-allow/deny independent of user | web:`Permissions-Policy: camera=`+`<iframe allow>`; mf:Group Policy/HKLM ConsentStore |
| `cap.os.consent.usage_log` | Read OS-recorded recent camera-usage log | mf:ConsentStore 7-day "Recent activity" `?` |
| `cap.os.session.start` | Start the running capture session | avf:`startRunning`; mf:`MediaCapture.InitializeAsync` |
| `cap.os.session.stop` | Stop the running capture session | avf:`stopRunning` |
| `cap.os.session.running_state_query` | Observe whether the session is running | avf:`isRunning`(KVO)/`DidStart/StopRunningNotification` |
| `cap.os.hotplug.arrival` | Event: a camera appeared/was connected | pipewire:`pw_registry global`; avf:device-discovery notif; web:`devicechange` |
| `cap.os.hotplug.removal` | Event: a camera was disconnected/removed | pipewire:`global_remove`; web:`readyState`+`ended`; camera2:`onDisconnected` (also eviction—§14) |
| `cap.os.arbitrate.exclusive_open` | Acquire device w/ exclusive control; fail loudly if contended | libcamera:`Camera::acquire()`/`release()`; v4l2:`open()`→EBUSY; mf:`SHARE_MODE`=0; genicam:`GevCCP`/`DeviceAccessStatus` |
| `cap.os.arbitrate.shared_open` | Open device shared/read-only coexisting w/ other readers | mf:`SHARE_MODE`=1/`FRAMESERVER_SHARED`; pipewire:WirePlumber multiplex |
| `cap.os.arbitrate.in_use_query` | Query whether device is held by another client | avf:`isInUseByAnotherApplication`; genicam:`DeviceAccessStatus`(Busy/ReadOnly) |
| `cap.os.arbitrate.contention_error` | Error/signal that open failed: another client holds device | avf:`AVErrorDeviceInUseByAnotherApplication`; camera2:`ERROR_CAMERA_IN_USE`/`MAX_CAMERAS_IN_USE`; web:`NotReadableError`; v4l2:`EBUSY`; mf:`E_ACCESSDENIED` |
| `cap.os.arbitrate.priority_hint` | Set cooperative client priority influencing control | v4l2:`S_PRIORITY`/`G_PRIORITY`; genicam:`GevPrimaryApplicationSwitchoverKey` |
| `cap.os.arbitrate.preemption_event` | Notification this client was evicted by a higher-priority client | camera2:`onDisconnected`(evicted); avf:`VideoDeviceInUseByAnotherClient` |
| `cap.os.arbitrate.priority_change_event` | Notification access-priority ranking changed | camera2:`onCameraAccessPrioritiesChanged()` |
| `cap.os.lifecycle.interruption_event` | Event: the capture session was interrupted | avf:`WasInterruptedNotification`/`isInterrupted`(KVO); mf:`Failed`/`CameraStreamStateChanged` |
| `cap.os.lifecycle.interruption_ended_event` | Event: a prior interruption ended | avf:`InterruptionEndedNotification` |
| `cap.os.lifecycle.interruption_reason` | Read why the session was interrupted | avf:`AVCaptureSessionInterruptionReason`(`InBackground`/`AudioDeviceInUse`/`MultipleForegroundApps`/`SystemPressure`) |
| `cap.os.lifecycle.background_block` | OS blocks/permits camera while backgrounded | camera2:bg-open-blocked-without-FGS; avf:`VideoDeviceNotAvailableInBackground`; web:Page Visibility throttle-mute |
| `cap.os.lifecycle.multitask_access` | Permit camera while sharing screen w/ other fg apps | avf:`isMultitaskingCameraAccessEnabled` `?`/`NotAvailableWithMultipleForegroundApps` |
| `cap.os.lifecycle.os_mute_event` | OS/UA forcibly muted the live stream (privacy toggle) | web:`MediaStreamTrack.muted`+`mute`/`unmute` |
| `cap.os.orientation.sensor_mount` | Read sensor's fixed mount orientation vs natural orientation | camera2:`SENSOR_ORIENTATION`; v4l2:`CAMERA_ORIENTATION`/`CAMERA_SENSOR_ROTATION`; libcamera:`properties::Rotation` |
| `cap.os.orientation.display_rotation` | Compute capture/preview rotation vs current UI orientation | avf:`AVCaptureDeviceRotationCoordinator`; camera2:`Surface.ROTATION_*`; mf:`CameraRotationHelper` |
| `cap.os.orientation.output_rotation_apply` | Apply a rotation transform to delivered frames (emitted echo→§10) | avf:`AVCaptureConnection.videoRotationAngle`; mf:`MF_MT_VIDEO_ROTATION`/`SetRotation`; libcamera:`CameraConfiguration::orientation`; pipewire:`SPA_META_VideoTransform` |
| `cap.os.orientation.front_mirror` | Apply/query horizontal mirroring for front capture | avf:`isVideoMirrored`/`automaticallyAdjustsVideoMirroring`; mf:`SetMirror`/`SetPreviewMirroring`; camera2:`MirrorMode`; v4l2:HFLIP/VFLIP |
| `cap.os.orientation.facing_hint` | Coarse front/back facing hint (no geometry) | web:`facingMode` |
| `cap.os.privacy.indicator_state` | Read the system in-use privacy indicator (LED/dot) | avf:not readable; camera2:`SensorPrivacyManager` observe-only; web:not readable; mf:OS-driven (not readable) |
| `cap.os.privacy.indicator_force_software` | Force a software privacy indicator where no HW LED exists | mf:HKLM `NoPhysicalCameraLED=1` |
| `cap.os.privacy.toggle_state_query` | Query OS hardware/software camera kill-switch state | camera2:`SensorPrivacyManager.supportsSensorToggle`; v4l2:`PRIVACY` |
| `cap.os.privacy.indicator_led_mode` | Configure on-device status-LED behavior `[CEILING]` | genicam:`DeviceIndicatorMode`(Active/Inactive/ErrorStatus) |
| `cap.os.shutter_sound.play` | Play the standard system shutter/capture sound | camera2:`MediaActionSound`(`SHUTTER_CLICK`/`FOCUS_COMPLETE`/`START/STOP_VIDEO_RECORDING`) |
| `cap.os.shutter_sound.disable_query` | Query/control whether the mandatory shutter sound can be disabled | camera2:no API (legacy `canDisableShutterSound`); avf/mf:none (regional mandate — deny) |
| `cap.os.capture_input.shutter_button` | Receive hardware shutter / volume-button capture events | avf:`AVCaptureEventInteraction`/`onCameraCaptureEvent`; uvc:still-trigger via Status-Interrupt endpoint `[>pin]` |
| `cap.os.capture_input.control_surface` | Bind to a dedicated hardware camera-control surface | avf:`AVCaptureControl`/`AVCaptureSlider`/`AVCaptureIndexPicker`/`AVCaptureToggle`/`AVCaptureSystem*Slider`+`addControl:` |
| `cap.os.thermal.state_query` | Read current thermal-pressure level driving degradation | avf:`systemPressureState`/`AVCaptureSystemPressureLevel`; camera2:`getCurrentThermalStatus()`/`AThermal_getCurrentThermalStatus`; genicam:`DeviceTemperature`; libcamera:`SensorTemperature` |
| `cap.os.thermal.state_event` | Be notified when thermal pressure changes | avf:`systemPressureState`(KVO); camera2:`addThermalStatusListener` |
| `cap.os.thermal.pressure_factors` | Read which factors drive thermal pressure | avf:`AVCaptureSystemPressureFactors`(SystemTemperature/PeakPower/DepthModuleTemperature) |
| `cap.os.thermal.headroom_forecast` | Read normalized thermal headroom / forecast | camera2:`getThermalHeadroom`/`AThermal_getThermalHeadroom` |
| `cap.os.thermal.power_throttle_hint` | Request power/framerate throttle on capture `[CEILING]` | mf:`EXTENDED_FRAMERATE_THROTTLE`/`OPTIMIZATIONHINT`; uvc:`FRAMERATE_THROTTLE` XU |
| `cap.os.thermal.pressure_interruption_state` | Read exact pressure state that forced interruption/shutdown | avf:`AVCaptureSessionInterruptionSystemPressureStateKey` |

---

## 13. CEILING index
Caps with **no** consumer-OS backing on any of the 9 axes. They classify `deny` on every consumer axis and `native` only via a future provider plane (machine-vision / sensor-subdev / capture-card) — MEL-ENGINE-I keeps them in vocab, never pruned.

- enum/topology: `cap.device.class.machine_vision` · `cap.device.sensor.scan_type` · `cap.stream.transfer.usercontrolled` · `cap.stream.channel.transport.config`
- ptz: `cap.ptz.auto.mode` · `cap.ptz.lens.shutter` · `cap.ptz.lens.filter` · `cap.ptz.lens.magnification` · `cap.ptz.controller.lifecycle`
- capture: `cap.capture.readout.multiroi` · `cap.capture.readout.shuttermode` · `cap.capture.hdr.multislope` · `cap.capture.trigger.hardwareline` · `cap.capture.trigger.scheduled` · `cap.capture.sequencer` · `cap.capture.userset`
- depth: `cap.depth.raw.tof` · `cap.depth.raw.structured` · `cap.depth.raw.lidar` · `cap.depth.pdaf.raw` · `cap.depth.container.multicomponent`
- frame: `cap.frame.format.3d.coord` · `cap.frame.format.3d.confidence` (the inline `[CEILING]` against *genicam* entries on other format IDs marks only that axis-entry, not the whole ID)
- timing: `cap.timing.exposure-event` · `cap.timing.device-clock.timestamp-counter` · `cap.timing.genlock.sync-pins` · `cap.timing.genlock.sdi-reference` · `cap.timing.genlock.ptp-clock-sync`
- metadata: `cap.meta.embedded.sensorlines` · `cap.meta.event.exposurephase`
- os: `cap.os.privacy.indicator_led_mode`

**Distinct sub-class — emulate-everywhere, not deny.** The shared-A/V-clock machinery `cap.timing.av-sync.{device-ts-trust,rebase,jitter-buffer,ts-jump-recovery,offset}` has no OS API on any axis (sourced only from OBS `libobs/obs-source.c`), but it is *framework-implementable data-plane logic* — so it classifies `emulate` on every axis, not `deny`. Carried here for visibility; P4 must not mislabel it `deny`. `cap.os.thermal.power_throttle_hint` is mixed (mf `EXTENDED_FRAMERATE_THROTTLE` is consumer-native; the uvc `FRAMERATE_THROTTLE` XU is the ceiling slice).

## 14. reconciliation — cross-area ownership

### 14a. dedups (duplicate IDs folded to one canonical home; native entries moved with them)
- feasibility → `cap.enum.feasibility.*` (dropped topology `cap.stream.combination.feasibility.query`; topology keeps `multicam.cost.*`).
- logical-device decomposition → `cap.topology.logical.*` (dropped `cap.device.virtual.constituents`, `cap.device.virtual.switchover_zoom`; devices keeps only the `cap.device.class.virtual` flag).
- lens switchover → `cap.topology.logical.switchover.{zoomfactors,behavior}` (dropped control's `cap.control.zoom.lens-switchover-*`).
- optical-motorized zoom → `cap.ptz.zoom.optical.*` (dropped control's `cap.control.zoom.optical-focal-length`/`-relative`; control keeps digital-crop/ratio/ramp/method).
- iris/aperture → `cap.control.aperture.{manual,relative}` (dropped `cap.ptz.iris.*`; their UVC/v4l2 entries already live on the aperture IDs).
- sensor/ISP test pattern → `cap.control.isp.test-pattern` (dropped `cap.capture.readout.testpattern`; `cap.testsrc.test_pattern` is distinct — synthetic source, not sensor).
- multi-slope HDR readout → `cap.capture.hdr.multislope` (dropped `cap.control.exposure.hdr-multi-slope`).
- rolling-shutter skew → `cap.timing.rolling-shutter.skew` (dropped `cap.meta.rollingshutterskew`).
- per-frame applied settings → `cap.meta.applied.*` (per-quantity split; dropped coarse `cap.timing.settings-applied.readback`; timing keeps `settings-applied.latency` = when-applied).
- OIS sample stream → `cap.meta.ois.samples` (dropped `cap.timing.imu-correlation.gyro-ois-samples`; timing keeps `imu-correlation.clock`).
- per-frame intrinsics delivery → `cap.calib.intrinsics.perframe` (dropped `cap.meta.chunk.intrinsics`).
- IR torch → `cap.control.flash.ir-torch` (dropped depth `cap.ir.torch`).
- IR illumination state → `cap.meta.illumination.ir` (dropped depth `cap.ir.illumination.state`).
- frame-rate clamp → `cap.timing.frame-rate.clamp` (dropped `cap.capture.framerateclamp`).
- shutter-mode select → `cap.capture.readout.shuttermode` (dropped `cap.timing.shutter-mode.select`).
- hardware sequencer → `cap.capture.sequencer` (dropped `cap.timing.hw-sequencer`).
- scheduled action-command firing → `cap.capture.trigger.scheduled` (dropped `cap.timing.genlock.scheduled-action`; timing keeps `genlock.ptp-clock-sync` = the clock discipline).
- removed `cap.meta.absent.web3a` — a web DENY marker, not a capability; it is a P4 matrix cell, not a vocab ID.

### 14b. retained overlaps (two IDs kept — distinct facets, intentional)
- `cap.depth.confidence` (semantics) ∥ `cap.frame.format.3d.confidence` (delivery byte-layout).
- `cap.control.3a.region-unified-roi` (metering ROI) ∥ `cap.effect.subject_roi.set` (subject-tracking ROI).
- `cap.timing.discontinuity-marker` (STREAMTICK gap tick) ∥ `cap.frame.drop.*` (drop signal + reason).
- `cap.os.orientation.output_rotation_apply` (rotation control) ∥ `cap.meta.transform.orientation` (emitted echo).
- `cap.meta.applied.cropregion` (echo) ∥ `cap.control.zoom.digital-crop-rect` (the set knob).
- `cap.device.sensor.cfa` (general CFA arrangement) ∥ `cap.ir.cfa.nir` (the NIR/MONO case).
- `cap.capture.trigger.scheduled` (firing) ∥ `cap.timing.genlock.ptp-clock-sync` (clock).
- `cap.egress.publish.frame_{push,pull}` ∥ a future frame-transport/shared-capture-clock seam (push/pull plumbing is also data-plane).

### 14c. boundaries to other modules (named, not minted here)
- `image` owns the pixel-format **descriptor** (byte layout, colorimetry); §8 mints only deliverability.
- `color` owns HDR transfer/profile + gamut science; §5 `cap.capture.hdr.*` selects the profile, §8 mints only the pixel container.
- `audiocapture`/`audiomixer` own mic identity/DSP/consent/gain; camera owns the shared time-base (§9) + bundled-session + SDI-embedded-audio *sourcing* + the published/ingest cam↔mic *association* only.
- `barcode`/CV-ML own decoding; §10d mints only the OS-emitted detection payload.
- `gpu` owns the import targets §8b hands off to.

### 14d. research-scheduled (`?`) — classification/existence to resolve before freeze (P7)
`cap.device.mic.association`(avf accessor) · `cap.ptz.roll.relative`(mf) · `cap.capture.bracket.focus`(avf) · `cap.capture.hdr.video.log`(genicam) · `cap.capture.onboardencode.hevc`(UVC-vendor) · `cap.frame.format.rgb.lowbit`/`gray8`/`gray_highbit`(mf L8/L16/565) · `cap.frame.format.yuv.semiplanar422_hdr`(camera2 P210) · `cap.frame.drop.reason`(camera2/v4l2/libcamera signal-only) · `cap.meta.stats.predictedcolor`(camera2 deprecated) · `cap.egress.controls.expose`(pipewire) · `cap.egress.mic_association`(avf/mf) · `cap.os.consent.deeplink_settings` · `cap.os.consent.usage_log` · `cap.os.lifecycle.multitask_access`(avf). Bit-level `?` from P2 (ROI `bmAutoControls` layout, H.264-frame descriptor fields) stay inventory-level — not vocab-gating.

### 14e. bare-sensor axis additions (P5-gate reopen — `10-inventory-baresensor.md`)
The `embedded+baresensor` axis added **+3 caps** (579→582), all classifying `native` on baresensor / `deny` on the OS HALs (which own the sensor):
- `cap.device.access.sensor_register` (cameradevice) — direct I²C/SCCB register R/W; the no-OS analog of `cap.device.access.raw_under_os`.
- `cap.device.bringup` (cameradevice) — host-driven XCLK generation + PLL/clock-tree + PWDN/RESET power sequence (collapses clock-gen/PLL/power — they always co-classify).
- `cap.control.flash.strobe-output` (cameracontrol) — sensor strobe pin asserted during exposure (illumination sync; distinct from `cap.timing.genlock.sync-pins`'s multi-cam framesync).
Plus **1 broadened**: `cap.stream.channel.transport.config` now spans CSI-2/DVP link config (was genicam-only `[CEILING]`; native on baresensor).
**Rejected as downstream** (not camera caps — the inference-stays-downstream seam): host *software-3A* (auto-3A `deny`→fallback-manual on baresensor; the app/3A-module closes the loop on `cap.control.exposure/gain.*`) and *no-ISP demosaic* (camera emits `cap.capture.raw.bayer`; `image`/`gpu` debayers).

### 14f. P6 stress findings (582→589)
Seven reference apps stressed on paper (`60-stress-*.md`). **+7 caps:**
- `cap.device.class.software` (cameradevice) — ingest a published software vcam; **closes the egress↔ingest loop** Zoom exposed (`cap.device.class.virtual` is multi-lens fusion, not a published cam). [Zoom]
- `cap.enum.include_hidden` (cameradevice) — reveal OS-hidden virtual/screen cams. [OBS+Zoom]
- `cap.stream.source.format_change_event` (cameradevice) — source format changes underneath a live session. [OBS]
- `cap.meta.hdr.static` (camerameta) — HDR static metadata on ingest. [OBS]
- `cap.control.focus.ramp-with-rate` + `cap.control.exposure.ramp-with-rate` (cameracontrol) — manual focus/exposure *pulls*; emulate-everywhere. [FiLMiC]
- `cap.topology.multicam.control-independence` (cameradevice) — queryable independent-manual-under-multicam composition. [FiLMiC]

**2 reclassifications** (deny→emulate, framework-provides-what-the-OS-doesn't): `cap.meta.bracketcorrelation` on ios/android (synthetic bracket-ID via `cap.frame.cookie`) [compphoto]; `cap.stream.sync.timealigned` on android (framework timestamp-correlation, cf. av-sync) [AR].
**2 cut moves** (recorded in `50-planes.md`): `cap.os.session.{start,stop,running_state_query}` policy→cameracapture (capture lifecycle, not policy) [embedded]; `cap.meta.ois.samples` camerastats→cameracapture (AR motion-alignment, not skip-me telemetry) [AR].
**Boundaries held** (no leak): encode (FiLMiC), fusion (compphoto), SLAM/pose (AR) all stop at synced frames + intrinsics + IMU-timestamps. Halide clean.





