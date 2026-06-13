# camera — matrix notes (P4)
> keys referenced by `40-matrix.csv` note column. emulate → cost; deny → fallback cap-ID or `none`; `?` → what is unresolved.
> long-form CSV: 1 row / cap×axis over 11 columns; pivot to read. +platform = +rows (MEL-ENGINE-I: a deny cell keeps the cap in vocab, native elsewhere).

## dev
# 40-notes-dev — devices & enumeration matrix notes

Note keys for the emulate/deny/? cells of `40-matrix-dev.csv`. Plain native rows carry no note.

## cross-cutting / shared mechanism
- dev-rawuvc — emulate cost: raw-UVC under-the-OS gateway (macOS IOKit/IOUSBHost · win32 IKsControl/DeviceIoControl · linux-libcamera/pipewire via libusb), vendor-firmware-dependent; lifts raw-device access from deny.
- dev-webdeny — fallback: none. web exposes no transport/bus/vendor/sensor surface (anti-fingerprinting + spec gap); the only device facts are opaque deviceId/groupId/label/kind.
- dev-uvcnoentry — fallback: none on the uvc-direct column. uvc-direct,uvc is native only for caps with a `uvc:` normalizes entry; everything else deny here.
- dev-genconsumer — fallback: none. consumer-only cap with no `genicam:` SFNC entry; deny on genicam,sfnc.
- dev-ceiling — fallback: none (consumer); `[CEILING]` cap (§13) — native only via a future provider plane, deny on every consumer axis, native on genicam,sfnc where a `genicam:` entry exists.

## ios divergence (macOS-only avf sources)
- dev-iosonly — fallback: none. TrueDepth/LiDAR depth-sensor device class is iOS-only avf → macos deny.
- dev-macosonly — fallback: none. DeskView is macOS-only avf (API_UNAVAILABLE(ios)) → ios deny.
- dev-iostransport — fallback: cap.device.id.stable (native). transportType is macOS-relevant only; iOS has no transport accessor.
- dev-ioscapcard — fallback: cap.device.class.external.uvc (native). iOS exposes no HDMI/SDI capture-card class.
- dev-iosrawfloor — fallback: none. iOS App Store sandbox = no public raw-USB path; the real floor for raw_under_os / extension_unit.
- dev-macosnomulticam — fallback: cap.enum.list (native). AVCaptureMultiCamSession + supportedMultiCamDeviceSets are API_UNAVAILABLE(macos); macOS ceiling is single-cam.

## avf `?`
- dev-avfmicq — ?-unresolved. §14d: no explicit camera↔mic accessor on AVCaptureDevice beyond companionDeskViewCamera; correlation by uniqueID/modelID/transportType is inferred, not a sanctioned accessor — contested on both macos+ios avf columns.

## avf emulate
- dev-avfformatintro — emulate cost: no getCameraCharacteristics-style static-caps blob; walk AVCaptureDevice.formats + per-format introspection without opening (escape-hatch via Format objects).
- dev-capcardinfer — emulate cost: macOS lacks a capture-card device class; infer from transportType + KSCATEGORY-equivalent only by heuristic device naming.

## camera2 / camerax
- dev-cxnoid — emulate cost: CameraX getAvailableCameraInfos exposes no raw id string; recover the camera-id via Camera2Interop CameraInfo extraction (escape-hatch).
- dev-c2noname — fallback: cap.device.id.stable (native). camera2/CameraX surface no human display-name key; only the id string.
- dev-c2nodvendor — fallback: cap.device.id.stable (native). camera2 has no manufacturer/model/firmware vendor-identity keys.
- dev-c2notransport — fallback: cap.device.class.external.uvc (native). camera2 has no physical-transport/bus key; only LENS_FACING_EXTERNAL marks USB.
- dev-c2nocapcard — fallback: cap.device.class.external.uvc (native). no HDMI/SDI capture-card class in camera2; such sources appear (if at all) as EXTERNAL cameras.
- dev-c2nomic — fallback: none. camera2 has no CameraCharacteristics key linking a camera to its companion mic.
- dev-c2depthcap — emulate cost: camera2 has no dedicated depth-sensor device class; infer from DEPTH_OUTPUT capability + DEPTH16 format presence in getCameraCharacteristics (camera2ndk reachable).
- dev-cxnodepth — fallback: cap.device.class.depth_sensor on camera2ndk (emulate). CameraX surfaces no depth-capability characteristic; Camera2Interop would be needed but CameraX has no device-class hook for it.
- dev-c2mediatype — emulate cost: no hasMediaType-style declaration; derive media-type set from REQUEST_AVAILABLE_CAPABILITIES (DEPTH/etc.) + StreamConfigurationMap output formats (camera2ndk reachable; camerax via Camera2Interop).
- dev-cxinterop — emulate cost: deep camera2-only enumeration (full getCameraCharacteristics keys) on CameraX via Camera2Interop / Camera2CameraInfo.getCameraCharacteristic (escape-hatch — key injection, not session ownership).
- dev-cxhidesquery — fallback: cap.enum.feasibility.combination_tables on camerax (emulate via getAvailableConcurrentCameraInfos). CameraX hides isSessionConfigurationSupported/CameraDeviceSetup with no public hook; query_closed unreachable.
- dev-cxconcurinfo — emulate cost: CameraX getAvailableConcurrentCameraInfos returns combination lists (not the raw query); higher-level escape from the hidden CameraDeviceSetup surface.
- dev-c2availcb — emulate cost: no isConnected accessor; derive presence from registerAvailabilityCallback onCameraAvailable/Unavailable bookkeeping (camera2ndk + camerax reachable).
- dev-nocostbudget — fallback: cap.enum.concurrent_sets (native on camera2ndk). no hardwareCost/systemPressureCost ledger outside avf; concurrency is boolean-feasible, not cost-graded.

## libcamera
- dev-lcsysdev — emulate cost: no vendor/transport accessor; derive from properties::SystemDevices (dev_t) + PipelineHandler name (e.g. uvcvideo) by inspecting backing kernel devices.
- dev-lccapcard — fallback: cap.device.class.external.uvc (native). libcamera has no capture-card class; HDMI grabbers fronted by V4L2 appear via the v4l2 axis, not as a libcamera class.
- dev-lcnodepth — fallback: cap.device.class.external.uvc (native). libcamera is RGB/Bayer-oriented; no depth-sensor device class.
- dev-lcnomic — fallback: none. libcamera is video-only; no associated-audio concept.
- dev-lcmediatype — emulate cost: no media-type declaration accessor; infer from StreamConfiguration pixelFormat enumeration per generated role.
- dev-lcvalidate — emulate cost: CameraConfiguration::validate() returns Valid/Adjusted/Invalid — try-and-adjust on an open camera substitutes for an explicit co-operation query (this is the native try_adjust mechanism, used as the open-feasibility surface).
- dev-lcdisconnected — emulate cost: no boolean isConnected; track presence via Camera::disconnected + cameraAdded/Removed signal bookkeeping.

## mediafoundation / win32
- dev-mfsymlink — emulate cost: no structured vendor/model/firmware; parse VID/PID + vendor strings out of the VIDCAP_SYMBOLIC_LINK device-path string.
- dev-mfextcat — emulate cost: no dedicated external/UVC flag; distinguish via KSCATEGORY + VIDCAP_HW_SOURCE (hardware vs synthetic) and symbolic-link bus parsing.
- dev-mfnofacing — fallback: cap.device.class.external.uvc (native). MF/desktop has no front/back facing key (desktop cameras are unspecified facing).
- dev-mfprofiletables — emulate cost: query-without-open via MediaCaptureVideoProfile / KSCAMERAPROFILE declared combination tables read pre-activation (declared-table substitute for a true closed query).
- dev-mfgetnative — emulate cost: open feasibility by enumerating GetNativeMediaType per stream + SetCurrentMediaType acceptance (try on the materialized source).

## pipewire
- dev-pwprops — emulate cost: read the relevant fact from node props (node.name/description, object.path backend, media.class/role, device.id) on the registry global rather than a typed accessor; backend-dependent fidelity.
- dev-pwpropinfo — emulate cost: per-device static caps via SPA_PARAM_PropInfo + SPA_PARAM_EnumFormat on the bound node global without a streaming open.
- dev-pwenumformat — emulate cost: open feasibility / try-adjust via SPA_PARAM_EnumFormat choice ranges + param_changed fixation negotiation (backend bounds it).
- dev-pwnodegroup — emulate cost: camera↔mic association via device.id node grouping (nodes sharing the owning Device global), portal-restricted to camera nodes so audio pairing is partial.
- dev-pwregistry — emulate cost: presence via pw_registry global / global_remove bookkeeping (no typed connected-state accessor).

## shared-USB-parent association
- dev-usbparent — emulate cost: camera↔mic correlation via the shared USB parent in sysfs (v4l2) / USB device tree (uvc-direct); the UVC webcam's mic is a separate USB-Audio-Class device, not a video node — pairing is topology inference only.

## v4l2
- dev-v4l2physinfer — emulate cost: single-sensor physical class inferred from V4L2_CAP_VIDEO_CAPTURE + V4L2_INPUT_TYPE_CAMERA; no explicit physical/logical device-class flag.
- dev-v4l2tryfmt — emulate cost: VIDIOC_TRY_FMT (+ S_PARM) is the only concrete feasibility check and silently step-rounds — used as both open-query and try-adjust (note: it is also the native silent_clamp surface).
- dev-v4l2openprobe — emulate cost: no boolean connected accessor; presence is open()+QUERYCAP success / ENODEV, paired with the udev monitor.

## genicam
- dev-gennodemap — emulate cost: open feasibility via the live GenApi node map (EAccessMode + pIsAvailable/pMin/pMax + Invalidator recompute) — query the live node, not a packaged co-operation table.
- dev-gennotryadj — fallback: cap.enum.feasibility.query_open on genicam (emulate via node map). SFNC has no try-and-adjust-to-nearest primitive; legal values vanish from the node rather than snapping.

## uvc-direct
- dev-uvcvspin — emulate cost: media-type set declared by walking multiple VideoStreaming interfaces (RGB/IR/depth pins) via VS_PROBE/COMMIT format descriptors.
- dev-uvcprobe — emulate cost: open feasibility / try-adjust by VS_PROBE then re-read — the device returns the nearest negotiable resolution/interval/payload before COMMIT.

## blanket deny (consumer-OS lacks the device-class / accessor entirely)
- dev-vlogicalnone — fallback: cap.device.class.physical (native on this axis). logical/virtual multi-constituent device class exists only on avf (isVirtualDevice) + camera2 (LOGICAL_MULTI_CAMERA); elsewhere a fused cam is just a physical device.
- dev-noncontinuity — fallback: cap.device.class.external.uvc (native where present) else cap.enum.list. Continuity Camera is an Apple-only device class; no equivalent off-platform.
- dev-nodeskview — fallback: cap.enum.list (native). DeskView is an Apple-only virtual overhead camera class.
- dev-norotation — fallback: cap.device.facing where native else none. fixed mount/sensor rotation is a camera2/libcamera/v4l2 property; avf/mf/web have no mount-rotation key.
- dev-nosensorgeom — fallback: cap.enum.media_types where native else none. physical pixel-array geometry is a camera2/libcamera/genicam characteristic; absent elsewhere.
- dev-nopixpitch — fallback: none. physical pixel/unit-cell size is camera2/libcamera/genicam/avf only; v4l2/pipewire/mf/web lack it.
- dev-nocfa — fallback: none. CFA/Bayer layout is camera2/libcamera/genicam/v4l2 only; avf/pipewire/mf lack a CFA accessor.
- dev-nowhitelevel — fallback: none. sensor white-level/DR bounds are camera2/genicam only.
- dev-noaperturechar — fallback: none. reported lens-aperture characteristic is avf/camera2/genicam only; not a v4l2/libcamera/mf device characteristic (the iris control is separate).
- dev-nofocallen — fallback: cap.device.lens.aperture.fixed where native else none. native focal-length list is camera2/genicam/mf-KS/uvc only; avf/libcamera/v4l2/web lack it.
- dev-nominfocus — fallback: none. minimum/hyperfocal focus distance is an avf/camera2 device characteristic only.
- dev-nohwlevel — fallback: none. coarse hardware-tier is camera2 INFO_SUPPORTED_HARDWARE_LEVEL / genicam SFNC version only.
- dev-noqueryclosed — fallback: cap.enum.feasibility.query_open where native/emulate else none. query-feasibility-without-opening is camera2 (API35) / genicam node-map only.
- dev-notryadjust — fallback: cap.enum.feasibility.query_open where native/emulate else none. try-and-adjust-to-nearest is a libcamera validate() primitive; not present elsewhere.
- dev-nosilentclamp — fallback: cap.enum.feasibility.per_control_range (native everywhere). silent step-rounding trap is the v4l2 TRY_FMT / MF post-preview seam; honest-range query supersedes it elsewhere.
- dev-noconstraints — fallback: cap.enum.feasibility.per_control_range (native) / query_open. the constraint-satisfaction + OverconstrainedError model is web-only.
- dev-nocombotables — fallback: cap.enum.feasibility.query_open where native/emulate else none. vendor-declared guaranteed-compatible tuple tables are avf/camera2/mf only.
- dev-noconcurrent — fallback: cap.stream.multi.concurrent (single-camera multi-stream) else none. concurrent-device-set enumeration is avf/camera2 only; v4l2/libcamera/web have no synchronized multi-sensor session primitive.
- dev-androidrawfloor — fallback: cap.device.access.extension_unit unreachable; none. Android exposes UVC cameras as ordinary camera2 devices with no raw-USB / XU passthrough to apps.

## top
# 40-notes-top — topology & streams matrix notes

- top-camerax-graph — CameraX session graph via UseCase binding; Camera2 SessionConfiguration/OutputConfiguration underneath.
- top-camerax-concurrent — ConcurrentCamera / ConcurrentCameraConfig.Builder / SingleCameraConfig; front+back binding (isConcurrentCameraModeOn).
- top-camerax-multistream — multiple UseCase binding (Preview/ImageCapture/ImageAnalysis/VideoCapture) gives concurrent streams.
- top-camerax-usecase — UseCase classes ARE the per-stream role abstraction.
- top-camerax-streamsharing — StreamSharing multiplexes one camera stream across use cases beyond the hardware stream limit (no Camera2 equivalent).
- top-camerax-reconfig — CameraX rebinds use cases live; deferred surface via Camera2 underneath.
- top-camerax-interop — fallback: Camera2Interop escape-hatch; CameraX does not surface this primitive natively. cost: escape-hatch (Camera2Interop, bypasses CameraX safety).

- top-camera2-concurrentid-java — getConcurrentCameraIds / SCALER_MANDATORY_CONCURRENT_STREAM_COMBINATIONS is Java-only; NDK has NO equivalent. cost: JNI bridge for enumeration.
- top-camera2-physstream — physical-stream split via OutputConfiguration.setPhysicalCameraId on a logical multi-cam.
- top-camera2-physhotplug-java — registerExtendedAvailabilityCallback (onPhysicalCameraAvailable/Unavailable) is Java-side. cost: JNI bridge.
- top-camera2-acq-requestmodel — capture (single) vs setRepeatingRequest (continuous); request model, not an AcquisitionMode enum.
- top-camera2-noswitchover-introspect — fallback none: Camera2 does not expose virtual-device switchover zoom factors; OS auto-switches a logical cam silently.
- top-camera2-noswitchover-control — fallback none: no API to configure lens-switchover regime or lock during recording.
- top-camera2-nokind — fallback none: per-stream color/IR/depth source-kind not classified (physical id ≠ kind).
- top-camera2-nosyncprimitive — fallback none: no AVCaptureDataOutputSynchronizer analogue; per-result timestamps exist but no time-aligned bundle delivery primitive.
- top-camera2-noportclock — fallback none: no per-port capture clock object exposed.

- top-avf-multicam-nomacos — fallback cap.topology.multicam.concurrent: AVCaptureMultiCamSession + isMultiCamSupported + costs are API_UNAVAILABLE(macos); native on iOS/macCatalyst only. macOS ceiling is single-cam single-format.
- top-avf-switchover-nomacos — fallback none: virtualDeviceSwitchOverVideoZoomFactors is API_UNAVAILABLE(macos); iOS-only.
- top-avf-switchover-behavior-both — switchover behavior + fallback set (setPrimaryConstituentDeviceSwitchingBehavior:, activePrimaryConstituentDevice) is macos(12)+ios(15) BOTH (unlike zoomfactors which is iOS-only).
- top-avf-nosynctype — fallback none: AVF has no genlock-quality / sensor-sync-type readout for a virtual cam's constituents.
- top-avf-nophysicalhotplug — fallback none: device-level hot-plug exists, but no per-physical-sub-camera availability callback.
- top-avf-nostreamcount — fallback none: no reported max-simultaneous-stream-count introspection; you discover by canAddOutput trial.
- top-avf-nomultires — fallback none: no MultiResolutionImageReader analogue (one logical stream, multiple resolutions).
- top-avf-nosync-macos — fallback cap.stream.sync.timealigned: AVCaptureDataOutputSynchronizer is API_UNAVAILABLE(macos); native on iOS only (major macOS gap).
- top-avf-nodeferred — fallback none: no add-surface-to-live-session-post-creation primitive; reconfigure is begin/commit batch only.
- top-avf-nosessionparams — fallback none: no session-wide-params-held-constant declaration.
- top-avf-acq-coarse — still-vs-video selection is the output class choice, not a SingleFrame/MultiFrame(N)/Continuous enum; coarse mapping.
- top-avf-kind-coarse — AVCaptureInputPort.mediaType gives media-type, not the Color/IR/Depth source-kind granularity of MF MediaFrameSourceKind; coarse.

- top-genicam-perdevice — fallback cap.topology.multicam.concurrent: GenTL is per-device (Device->DataStream->Buffer); no multi-camera coordinated-session concept in one handle.
- top-genicam-source-as-constituent — SourceSelector/SourceCount enumerates multi-source imaging pipelines (RGB+IR) — mapped as logical constituents.
- top-genicam-source-notlogical — fallback none: SourceControl is multi-source, not Apple/Android logical-virtual-lens semantics (no virtual-device flag, ports, active-physical, or switchover).
- top-genicam-locked — fallback none: topology/format is locked after acquisition start; no live reconfigure (TLParamsLocked gates the param set).
- top-genicam-nodeferred — fallback none: no deferred-surface add to a live DataStream.
- top-genicam-nomultires — fallback none: no one-stream-multiple-resolution delivery primitive.
- top-genicam-nousecase — fallback none: streams are not role-tagged (no preview/record/still taxonomy).

- top-v4l2-nomulticam — fallback cap.topology.multicam.concurrent: V4L2 has no synchronized multi-sensor session; each sensor is an independent node/pipeline.
- top-v4l2-multinode — multi-node / media-controller pipeline gives concurrent streams and per-node S_FMT and multi-VS (per-node) on a media graph.
- top-v4l2-nological — fallback none: no logical/virtual-lens model; sensors are independent nodes.
- top-v4l2-nousecase — fallback none: no per-stream role/use-case tag.
- top-v4l2-nostreamcount — fallback none: no reported max-stream-count; bounded by node/buffer arbitration (single-open per node).
- top-v4l2-nofanout — fallback none: only one fd may stream/hold buffers; no one-stream-to-many-consumers fan-out (arbitration via priority/EBUSY).
- top-v4l2-nomultires — fallback none: no multi-resolution single-stream.
- top-v4l2-nokind — fallback none: no per-stream Color/IR/Depth source-kind classification.
- top-v4l2-nosync — fallback none: no time-aligned multi-output bundle delivery primitive.
- top-v4l2-noportclock — fallback none: no per-port capture-clock object.
- top-v4l2-reconfig-teardown — fallback none: changing format mid-stream requires STREAMOFF/teardown; S_FMT rejected while streaming.
- top-v4l2-nodeferred — fallback none: no deferred-surface add to a live session.
- top-v4l2-nosessionparams — fallback none: no session-wide-params-held-constant declaration.
- top-v4l2-noacqmode — fallback none: no SingleFrame/MultiFrame(N)/Continuous acquisition selector (streaming I/O is continuous QBUF/DQBUF).

- top-libcamera-multicam-uncoordinated — multiple Camera objects can run, but no coordinated/genlocked multi-cam session primitive. cost: app-coordinated, uncoordinated clocks.
- top-libcamera-nodeviceset — fallback none: no enumerate-concurrent-device-combos / per-stream multicam cap.
- top-libcamera-nological — fallback none: no logical/virtual-lens decomposition model.
- top-libcamera-nostreamcount — fallback none: no reported max-stream-count (CameraConfiguration accepts streams; validate() adjusts, no ceiling readout).
- top-libcamera-nofanout — fallback none: no one-stream-to-many-consumers fan-out primitive.
- top-libcamera-nomultivs — fallback none: no multi-VS/multi-pin source split; one Camera, multiple StreamConfigurations of the same source.
- top-libcamera-nomultires — fallback none: no multi-resolution single-stream.
- top-libcamera-nokind — fallback none: no per-stream Color/IR/Depth source-kind classification.
- top-libcamera-nosync — fallback none: no time-aligned multi-output bundle delivery primitive (Requests complete per-camera).
- top-libcamera-noportclock — fallback none: no per-port capture-clock object.
- top-libcamera-locked — fallback none: configuration is locked after start; reconfigure requires stop+configure (no live topology/format change).
- top-libcamera-nodeferred — fallback none: no deferred-surface add to a live session.
- top-libcamera-nosessionparams — fallback none: no session-wide-params-held-constant declaration.
- top-libcamera-noacqmode — fallback none: no SingleFrame/MultiFrame(N)/Continuous selector (queueRequest loop is the model).

- top-pw-multicam-backendbound — multiple pw_streams can run, but multi-cam concurrency is bounded by the backend (V4L2 single-open, libcamera per-camera), not coordinated by PipeWire. cost: backend-bound, uncoordinated.
- top-pw-mediarole — PW_KEY_MEDIA_ROLE / MEDIA_CATEGORY props are the per-stream role hint.
- top-pw-multivs-backend — multi-VS/multi-source split is whatever the active backend (V4L2/libcamera) exposes as separate nodes; PipeWire fronts them.
- top-pw-nodeviceset — fallback none: no enumerate-concurrent-device-combos / per-stream multicam cap (backend-bound).
- top-pw-nological — fallback none: no logical/virtual-lens decomposition model.
- top-pw-nostreamcount — fallback none: no reported max-stream-count (backend-bound).
- top-pw-nofanout — fallback none: no one-source-to-many-consumers fan-out primitive at the stream API (session-manager links 1:1).
- top-pw-nomultires — fallback none: no multi-resolution single-stream.
- top-pw-nokind — fallback none: no per-stream Color/IR/Depth source-kind classification.
- top-pw-nosync — fallback none: no time-aligned multi-output bundle delivery primitive.
- top-pw-noportclock — fallback none: no per-port capture-clock object exposed.
- top-pw-reconfig-renegotiate — fallback none: format change requires param renegotiation (param_changed + update_params), effectively a reconnect, not live topology swap.
- top-pw-nodeferred — fallback none: no deferred-surface add to a live session.
- top-pw-nosessionparams — fallback none: no session-wide-params-held-constant declaration.
- top-pw-noacqmode — fallback none: no SingleFrame/MultiFrame(N)/Continuous selector (process callback is continuous).

- top-mf-multicam-uncoordinated — multiple IMFMediaSource/SourceReaders can open distinct devices, but no coordinated/genlocked multi-cam session primitive. cost: app-coordinated, uncoordinated clocks.
- top-mf-nodeviceset — fallback none: no enumerate-concurrent-device-combos / multicam cost/per-stream cap (no AVF-style declaration).
- top-mf-nological — fallback none: no logical/virtual-lens decomposition (MediaFrameSourceGroup groups sources but no virtual-device/constituent/switchover model).
- top-mf-nostreamcount — fallback none: no reported max-simultaneous-stream-count introspection.
- top-mf-nofanout — fallback none: no one-stream-to-many-consumers fan-out primitive (each sink/reader pulls independently from the source).
- top-mf-nomultires — fallback none: no multi-resolution single-stream.
- top-mf-noportclock — fallback none: no per-port capture-clock object exposed.
- top-mf-nodeferred — fallback none: no deferred-surface add to a live session post-creation.
- top-mf-nosessionparams — fallback none: no session-wide-params-held-constant declaration.
- top-mf-noacqmode — fallback none: no SingleFrame/MultiFrame(N)/Continuous selector (ReadSample loop; VariablePhotoSequence is photo-specific, not stream acquisition mode).

- top-web-twocam — two getUserMedia calls (distinct deviceId:{exact}) for two cameras. cost: practically limited — many UAs/OSes refuse a second simultaneous camera; no atomic multi-cam open, no genlock.
- top-web-nodeviceset — fallback none: no enumerate-concurrent-device-combos / multicam cost/per-stream cap.
- top-web-nological — fallback none: no logical/physical lens model; facingMode is the only hint, constituent sensors not addressable.
- top-web-nousecase — fallback none: no per-track role/use-case tag.
- top-web-nostreamcount — fallback none: no reported max-stream-count.
- top-web-nomultivs — fallback none: no multi-VS/multi-pin source split; a track is one opaque source.
- top-web-nomultires — fallback none: no multi-resolution single-stream.
- top-web-nokind — fallback none: no per-track Color/IR/Depth source-kind classification.
- top-web-nosync — fallback none: no time-aligned multi-output bundle delivery primitive (separate getUserMedia calls do not guarantee a shared clock).
- top-web-noportclock — fallback none: no per-port capture-clock object.
- top-web-nodeferred — fallback none: no deferred-surface add to a live session.
- top-web-nosessionparams — fallback none: no session-wide-params-held-constant declaration.
- top-web-noacqmode — fallback none: no SingleFrame/MultiFrame(N)/Continuous selector (track delivers continuously).

- top-uvc-graph-implicit — UVC session graph is implicit (VS_PROBE/COMMIT negotiates one pin); no explicit input->connection->output wiring. cost: graph is firmware-fixed entity model, not app-wired.
- top-uvc-singledevice — fallback cap.topology.multicam.concurrent: one physical device; multicam/deviceset/cost/participation are out of scope for a single device.
- top-uvc-nological — fallback none: no logical/virtual-lens decomposition (entity model, not Apple/Android logical cam).
- top-uvc-multivs-firmware — multi-VideoStreaming-interface concurrency is firmware-dependent; the raw-UVC path exposes RGB+IR+depth pins. cost: firmware-dependent (not all devices implement multi-VS).
- top-uvc-multivs-rawmacos — fallback emulate via raw-UVC (IOKit) on macos: AVFoundation does not surface multi-VS; reach the extra VideoStreaming interfaces raw. cost: firmware-dependent + raw-USB gateway.
- top-ios-noraw — fallback cap.stream.source.multivs deny: iOS has no public raw-USB path (App Store sandbox) — the real floor; multi-VS unreachable.
- top-uvc-nousecase — fallback none: no per-stream role/use-case tag in the UVC entity model.
- top-uvc-nostreamcount — fallback none: no reported max-stream-count.
- top-uvc-nofanout — fallback none: no one-stream-to-many-consumers fan-out primitive.
- top-uvc-nomultires — fallback none: no multi-resolution single-stream.
- top-uvc-nokind — fallback none: source-kind (color/IR/depth) is implied by the VS interface, not a queryable per-stream classification field.
- top-uvc-nosync — fallback none: no time-aligned multi-output bundle delivery primitive (multiple VS pins are not delivered as a synchronized set).
- top-uvc-nodeferred — fallback none: no deferred-surface add to a live session.
- top-uvc-nosessionparams — fallback none: no session-wide-params-held-constant declaration.
- top-uvc-noacqmode — fallback none: no SingleFrame/MultiFrame(N)/Continuous selector at the streaming layer (still-pin VS_STILL_* is photo, not stream acquisition mode).

- top-no-costbudget — fallback cap.topology.multicam.cost.{hardware,systempressure}: only AVFoundation reports a multicam hardware/system-pressure cost budget (hardwareCost/systemPressureCost < 1.0); no other axis has a cost-dimension feasibility readout.
- top-no-transferctrl — fallback cap.stream.transfer.usercontrolled: user-paced transfer out of on-device memory (TransferControl: Start/Stop/Pause/Resume/queue depth) is a GenICam-only CEILING cap; no consumer OS exposes it.
- top-no-channeltransport — fallback cap.stream.channel.transport.config: per-transport-channel config (packet size, inter-packet delay, zones) is a GenICam GigE-Vision-only CEILING cap; no consumer OS exposes transport-channel tuning.

## ctl
# 40-notes-ctl — fine-grained control (`cap.control.*`) matrix notes

Reused pattern-notes keyed `ctl-*`; cost/fallback/`?` per key. Empty note = plain native.

## cross-cutting emulate mechanisms
- ctl-rawuvc — emulate via raw-UVC under-the-OS (IOKit/IOUSBHost on macOS); cost firmware-dependent + no high-level integration; macOS-only (iOS sandbox forbids raw USB).
- ctl-cxinterop — emulate via CameraX `Camera2Interop.Extender.setCaptureRequestOption` / `Camera2CameraControl.setCaptureRequestOptions`; cost escape-hatch, key injection only, no CameraX session integration.
- ctl-pwcustom — emulate via PipeWire `SPA_PROP_START_CUSTOM` backend-mapped custom prop (V4L2/libcamera backend); cost backend-limited, not portable, discoverable only via `SPA_PARAM_PropInfo`.
- ctl-3acompose — emulate the joint 3A op by composing per-A (AE/AF/AWB) setters/locks; cost not atomic across the three.

## deny markers (capability absent on this axis)
- ctl-avfmissing — fallback none; AVFoundation exposes no equivalent surface.
- ctl-iosnoraw — fallback none; iOS App Store sandbox has no raw-USB path (the real floor).
- ctl-c2missing — fallback none; not in the Camera2 NDK catalog.
- ctl-cxmissing — fallback none; outside CameraX basic CameraControl set AND not reachable as a Camera2Interop key for this cap.
- ctl-v4l2missing — fallback none; no V4L2 CID.
- ctl-lcmissing — fallback none; not in libcamera `controls` namespace.
- ctl-pwmissing — fallback none; no named SPA_PROP and no backend-custom mapping for this cap.
- ctl-mfmissing — fallback none; absent from MF/KS/WinRT control surface.
- ctl-webdeny — deny; web exposes only coarse constraint fields, full ISP/manual surface not addressable.
- ctl-uvcmissing — fallback none; not a UVC CT/PU/scanning/privacy selector.
- ctl-genmissing — fallback none; not an SFNC feature.

## plumbing / per-frame / feedback
- ctl-avfnoperframe — fallback none; AVFoundation controls are device-level, no per-frame request attach (per-shot only on photo settings).
- ctl-pwnoperframe — fallback none; `SPA_PARAM_Props` is a live write, not a per-frame attach.
- ctl-mfnoperframe — fallback none; MF control set is async/session-level, no per-frame request.
- ctl-uvcnoperframe — fallback none; UVC control transfer has no per-frame attach.
- ctl-v4l2noresult — fallback none; baseline V4L2 has no per-frame applied-value feedback (request-API metadata partial).
- ctl-pwnoresult — fallback none; no per-frame applied-value readback.
- ctl-uvcnoresult — fallback none; no per-frame applied-value readback.
- ctl-websettings — emulate via `getSettings()`; cost current-value readback, not per-frame.
- ctl-pwnolock — deny; PipeWire has no exclusive-config lock primitive.
- ctl-weblock — deny; web arbitrates via UA, no explicit hardware-config lock.
- ctl-lcnovendor — fallback none; libcamera has no vendor-passthrough escape.

## focus fallbacks (unit/representation mismatch)
- ctl-avfnormalized — fallback `cap.control.focus.manual-lens-position-normalized`; AVFoundation focus is normalized 0..1, not diopters/meters/abs-units.
- ctl-c2diopters — fallback `cap.control.focus.manual-distance-diopters`; Camera2 manual focus is diopters.
- ctl-lcdiopters — fallback `cap.control.focus.manual-distance-diopters`; libcamera `LensPosition` is dioptres.
- ctl-v4l2absunits — fallback `cap.control.focus.manual-position-absolute-units`; V4L2 focus is raw absolute units.
- ctl-mfabsunits — fallback `cap.control.focus.manual-position-absolute-units`; MF Focus is absolute units.
- ctl-uvcabsunits — fallback `cap.control.focus.manual-position-absolute-units`; UVC CT_FOCUS_ABSOLUTE is raw units.
- ctl-webmeters — fallback `cap.control.focus.manual-distance-meters`; web `focusDistance` is meters.
- ctl-genrelonly — fallback `cap.control.focus.manual-relative`; SFNC only offers `FocusStepper` (relative), no absolute.
- ctl-mfnotrigger — fallback `cap.control.focus.af-mode`; MF has no dedicated AF trigger, drive via focus-mode flags.
- ctl-uvcnotrigger — fallback `cap.control.focus.af-mode`; UVC toggles CT_FOCUS_AUTO, no dedicated one-shot trigger.

## exposure / iso / gain fallbacks
- ctl-avfaeonly — fallback `cap.control.exposure.ae-mode`; AVFoundation folds auto-ISO/auto-gain into the AE regime, no dedicated toggle.
- ctl-c2aeonly — fallback `cap.control.exposure.ae-mode`; Camera2 auto-gain is governed by AE mode.
- ctl-c2aeauto — emulate via `cap.control.exposure.ae-mode`; Camera2 auto-ISO implied by AE_MODE=ON.
- ctl-webaeonly — fallback `cap.control.exposure.ae-mode`; web auto-ISO/auto-gain governed by `exposureMode`.
- ctl-uvcaeonly — fallback `cap.control.exposure.ae-mode`; UVC AGC governed by CT_AE_MODE.
- ctl-gengainauto — fallback `cap.control.gain.analog-auto`; SFNC `GainAuto` is the auto-sensitivity path.
- ctl-lcgainauto — fallback `cap.control.gain.analog-auto`; libcamera `AnalogueGainMode` is the auto-sensitivity path.
- ctl-avfisoonly — fallback `cap.control.iso.manual`; AVFoundation exposes ISO, not an analog-gain split.
- ctl-webisoonly — fallback `cap.control.iso.manual`; web exposes `iso`, no analog/digital gain split.
- ctl-lcgainnotiso — fallback `cap.control.gain.analog-manual`; libcamera manual sensitivity is `AnalogueGain`, not ISO.
- ctl-uvcgainnotiso — fallback `cap.control.gain.analog-manual`; UVC manual sensitivity is PU gain, not ISO.
- ctl-gengainnotiso — fallback `cap.control.gain.analog-manual`; SFNC manual sensitivity is `Gain`, not ISO.
- ctl-v4l2nogainsplit — fallback `cap.control.gain.analog-manual`; V4L2 `GAIN` is generic, no analog/digital split.
- ctl-uvcdigmult — fallback `cap.control.gain.digital-multiplier`; UVC digital boost is the digital-multiplier control.
- ctl-c2fpsrange — emulate via `AE_TARGET_FPS_RANGE`; Camera2 trades FPS for exposure through the target-FPS range.
- ctl-c2facescene — emulate via `CONTROL_SCENE_MODE=FACE_PRIORITY`; Camera2 face-driven 3A rides the FACE_PRIORITY scene, not a dedicated toggle.
- ctl-c2reportonly — deny; Camera2 `SENSOR_BLACK_LEVEL` is a report, not a manual set.

## white balance fallbacks
- ctl-avfmodeonly — fallback `cap.control.wb.mode`; AVFoundation one-shot WB rides the mode, no dedicated single-shot.
- ctl-c2modeonly — fallback `cap.control.wb.mode`; Camera2 has no single-shot WB action.
- ctl-lcmodeonly — fallback `cap.control.wb.mode`; libcamera one-shot WB rides AWB mode.
- ctl-mfmodeonly — fallback `cap.control.wb.mode`; MF one-shot WB rides the mode.
- ctl-uvcmodeonly — fallback `cap.control.{exposure.ae-mode,wb.mode}`; UVC lock/one-shot is a mode toggle, not a dedicated lock/action.
- ctl-genmodeonly — fallback `cap.control.{exposure.ae-mode,wb.mode}`; SFNC lock is `Auto=Off`, no dedicated lock op.
- ctl-c2gainsonly — fallback `cap.control.wb.manual-gains-rgb`; Camera2 manual WB is RGB gains, no Kelvin.
- ctl-c2rgbonly — fallback `cap.control.wb.manual-gains-rgb`; Camera2 manual WB is RGB gains, no per-component.
- ctl-mfrgbonly — fallback `cap.control.wb.manual-gains-rgb`; MF `WHITEBALANCE_GAINS` is RGB.
- ctl-genratioonly — fallback `cap.control.wb.manual-gains-component`; SFNC WB is `BalanceRatio` component, no Kelvin.
- ctl-v4l2componentonly — fallback `cap.control.wb.manual-gains-component`; V4L2 manual WB is red/blue balance components.
- ctl-uvccomponentonly — fallback `cap.control.wb.manual-gains-component`; UVC manual WB is per-component.
- ctl-gencomponentonly — fallback `cap.control.wb.manual-gains-component`; SFNC `BalanceRatio` is component.
- ctl-webtemponly — fallback `cap.control.wb.manual-temperature-kelvin`; web manual WB is `colorTemperature` Kelvin only.
- ctl-webmodeonly — fallback `cap.control.wb.mode`; web WB lock rides `"manual"` whiteBalanceMode.
- ctl-lcaelock — emulate; libcamera freezes AE/AWB by disabling `AeEnable`/`AwbEnable` rather than a dedicated lock.
- ctl-mfmodelock — emulate; MF locks AE/WB via `EXTENDED_*MODE=manual`, not a dedicated lock flag.

## region fallbacks
- ctl-v4l2noregion — fallback none; V4L2 has no metering-ROI control.
- ctl-lcnoaeregion — fallback none; libcamera `AfWindows` is AF-only, no AE/AWB region.
- ctl-lcafonly — fallback none; libcamera region support is AF-only (`AfMetering`/`AfWindows`).
- ctl-avfsingleroi — fallback none; AVFoundation exposes a single point-of-interest, no max-count surface.

## zoom fallbacks
- ctl-avfratioonly — fallback `cap.control.zoom.ratio`; AVFoundation zoom is a float factor, no crop-rect.
- ctl-webratioonly — fallback `cap.control.zoom.ratio`; web zoom is a float factor, no crop-rect.
- ctl-v4l2croponly — fallback `cap.control.zoom.digital-crop-rect`; V4L2 zoom is crop selection, no ratio.
- ctl-lccroponly — fallback `cap.control.zoom.digital-crop-rect`; libcamera zoom is `ScalerCrop`, no ratio.
- ctl-gencroponly — fallback `cap.control.zoom.digital-crop-rect`; SFNC zoom is image-format crop, no ratio.
- ctl-mfsmoothzoom — emulate; MF `EXTENDED_ZOOM` is smooth digital zoom but exposes no app-set ramp rate.
- ctl-macunavail — deny; `videoZoomFactorUpscaleThreshold` is API_UNAVAILABLE(macos) (Format-only, iOS).

## flash / torch
- ctl-v4l2flashclass — emulate; V4L2 FLASH control class drives strobe, not still-flash-mode semantics.

## catalog leftovers
- ctl-v4l2noscanmode — fallback none; V4L2 has no standard scanning-mode CID (CT_SCANNING_MODE is not mapped by uvcvideo).

## `?` — unresolved
- ctl-macflashnohw — `?`; the AVFoundation flash API (`flashMode`/`isAutoRedEyeReductionEnabled`/`isFlashScene`) is present on macOS but largely a no-op without flash hardware — native-API-present vs deny-no-hardware unsettled (macOS-vs-iOS divergence).

## ptz
# 40-notes-ptz — mechanical controls (PTZ) matrix notes

## native paths
- ptz-v4l2native — V4L2 `PAN/TILT/ZOOM_ABSOLUTE/RELATIVE`, `_SPEED`, `_RESET` CIDs; UVC PTZ cams map directly. Cost: none beyond device support.
- ptz-v4l2privacy — `V4L2_CID_PRIVACY` boolean blocks acquisition where firmware exposes it. Cost: none.
- ptz-v4l2query — `VIDIOC_QUERYCTRL` (range/step/default) + `VIDIOC_G_CTRL` (current). Cost: none.
- ptz-mfnative — `IAMCameraControl` absolute `CameraControl_Pan/_Tilt/_Roll/_Zoom` (deg, -180..+180) + KS `_PAN/_TILT_RELATIVE`/`PANTILT_RELATIVE` speed. Cost: none.
- ptz-mfprivacy — KS `KSPROPERTY_CAMERACONTROL_PRIVACY` (UVC privacy shutter), reached via `IKsControl`. Cost: direct-KS, not on DShow `CameraControlProperty`.
- ptz-mfquery — `KSPROPERTY_TYPE_BASICSUPPORT`/`GetRange` for range, `Get` for current. Cost: none.
- ptz-mfautoflag — `CameraControlFlags{Auto}` hands PTZ to device auto-positioning. Cost: none. `[CEILING]` mf-only.
- ptz-uvcnative — UVC `CT_PANTILT_ABSOLUTE/_RELATIVE` (0x0d/0e), `CT_ROLL_ABSOLUTE/_RELATIVE` (0x0f/10), `CT_ZOOM_ABSOLUTE/_RELATIVE` (0x0b/0c), `CT_PRIVACY` (0x11), arc-second units. Cost: device firmware must implement the control unit.
- ptz-uvcquery — UVC `GET_MIN/MAX/RES/DEF/CUR` + `GET_INFO` cap-bitmap per control. Cost: none beyond device support.
- ptz-uvcspeedsubfield — UVC has no dedicated speed CID; continuous-speed motion is emulated via `CT_PANTILT/ZOOM_RELATIVE` speed sub-fields (driver-loop). Cost: relative-control sub-field driving, not a true speed control.
- ptz-webchromium — getUserMedia `pan`/`tilt`/`zoom` ConstrainDouble. PTZ-permission-gated (`video:{pan/tilt/zoom:true}` + `"pan-tilt-zoom"` permission) and Chromium-only (Firefox/Safari expose none). Cost: separate elevated permission; Chromium-only.
- ptz-webquery — `getCapabilities()`/`getSettings()` for pan/tilt/zoom range+current. Cost: gated by same PTZ permission; Chromium-only.
- ptz-genicamopticfocal — SFNC OpticControl `FocalLength [mm]` + `FocalLengthInitialize/Status/Stepper` = motorized optical zoom. Cost: optional OpticControl category + motorized-lens controller present.
- ptz-genicamopticstatus — SFNC `OpticController*Status`/`*Stepper` reads position/range per optic axis. Cost: OpticControl present.
- ptz-genicamopticlens — SFNC OpticControl `Shutter`/`Filter`/`Magnification` (+ each `*Initialize/Status/Stepper`). `[CEILING]` genicam-only motorized-optic surface. Cost: motorized optic controller present.
- ptz-genicamopticlifecycle — SFNC `OpticControllerSelector`/`Initialize`/`Disconnect`/`Abort`/`Status`. `[CEILING]` genicam-only. Cost: OpticControl present.

## emulate (raw-UVC under-the-OS)
- ptz-rawuvc — external-UVC-only: raw UVC Camera-Terminal controls via macOS IOKit / linux UVCIOC|libusb (libcamera/pipewire whose native path is v4l2). Cost: firmware-dependent, external-UVC PTZ camera only; no PTZ on built-in cameras. Fallback when absent: `cap.control.zoom.digital-crop-rect` for the zoom axes.
- ptz-rawuvcnoauto — raw-UVC reaches PTZ position controls but UVC has no auto-positioning flag (`CameraControlFlags{Auto}` is mf-only); auto.mode stays deny even on the raw path. Fallback: none.

## deny
- ptz-noiosrawusb — iOS App Store sandbox has no public raw-USB path; AVFoundation publishes no mechanical PTZ. The real floor. Fallback: none.
- ptz-androidnouvc — Camera2/CameraX have no mechanical-PTZ key and no UVC raw-control passthrough (DeviceAsWebcam is gadget-side, not host control). Fallback: `cap.control.zoom.digital-crop-rect` (digital only).
- ptz-uvcnoreset — UVC/MF/web have no pan/tilt RESET; only V4L2 exposes `PAN/TILT_RESET`. Fallback: re-issue absolute=default. Used for macos/linux-libcamera/pipewire/uvc reset cells.
- ptz-mfnoreset — MF/KS expose no pan/tilt reset selector. Fallback: absolute=default.
- ptz-mfnozoomrel — MF `CameraControl_Zoom` is absolute-only; no relative-zoom selector on DShow. Fallback: read current + write absolute.
- ptz-mfnozoomspeed — MF has no continuous zoom-speed control (only pan/tilt relative-speed). Fallback: none.
- ptz-uvcnoautoflag — UVC PTZ controls carry no auto-positioning flag. Fallback: none.
- ptz-v4l2noroll — V4L2 ext-ctrls-camera exposes no ROLL CID (pan/tilt/zoom only). Fallback: none.
- ptz-v4l2noauto — V4L2 has no PTZ auto-positioning control; auto.mode is mf-only. Covers v4l2/libcamera/pipewire. Fallback: none.
- ptz-webnorelative — getUserMedia pan/tilt/zoom are absolute ConstrainDouble; no relative-step API. Fallback: read current + set absolute.
- ptz-webnospeed — no continuous-speed PTZ in getUserMedia. Fallback: none.
- ptz-webnoreset — no pan/tilt reset in getUserMedia. Fallback: set absolute=default.
- ptz-webnoroll — getUserMedia exposes no roll constraint. Fallback: none.
- ptz-webnoprivacy — no hardware privacy-shutter control in getUserMedia (`MediaStreamTrack.muted` is a UA/OS track-mute, not a shutter). Fallback: none.
- ptz-webnoauto — no PTZ auto-positioning in getUserMedia. Fallback: none.
- ptz-genicamfixedmount — core SFNC has no PTZ/privacy; industrial cameras are fixed-mount, pan/tilt is an end-user mount not a device feature. Fallback: none.
- ptz-genicamabsonly — SFNC OpticControl drives `FocalLength` absolute via stepper; no relative/speed zoom feature. Fallback: read status + set absolute.
- ptz-genicamonly — the `[CEILING]` lens/auto caps (lens.shutter/filter/magnification, controller.lifecycle) are SFNC-OpticControl-only; no consumer-OS or UVC equivalent. Fallback: none.

## ? (unsure)
- ptz-mfrollrelative — `KSPROPERTY_CAMERACONTROL_ROLL_RELATIVE` existence in KS is `?` (inventory marks `_ROLL_RELATIVE`? — pan/tilt/zoom relative are documented, roll-relative is not confirmed). `?`

## cap
# capture-modes matrix notes — `cap.capture.*`

- cap-rawuvc — still via UVC stillpipe or video-frame grab
- cap-cxinterop — RAW_SENSOR via Camera2Interop ImageReader
- cap-iosonly — ProRAW iOS-centric
- cap-v4l2subdev — media-controller subdev binning [trap]
- cap-perframebracket — per-request EV sequence, no native bracket primitive
- cap-focusbracket — AVCapturePhotoBracketSettings focus-bracket unconfirmed
- cap-genlog — SFNC tonemap as log-curve unconfirmed
- cap-uvchevc — HEVC-over-UVC is vendor/XU not base UVC
- cap-deferred — deferral retired per MEL-ENGINE-I
- cap-trigsw — per-request capture submit is the closest software single-shot
- cap-acqctl — session/repeating-request start/stop is closest
- cap-fpsformat — high-fps via FrameDurationLimits if mode lists it
- cap-v4l2fps — high-fps via S_PARM timeperframe if format lists it
- cap-mfencoder — keyframe via MF UVC encoder property if exposed

## dep
# 40-notes-dep — depth / 3D / calibration / IR / seg / spatial

## native-witness
- dep-iosdepth — native: avf depth-producing hardware (TrueDepth/LiDAR) is iOS/iPadOS-only; `AVCaptureDepthDataOutput`/`AVDepthData` stream + per-format depth pairing live here.
- dep-iosir — native: avf TrueDepth IR stream is iOS-only.
- dep-iosmatte — native: `AVPortraitEffectsMatte`/`AVSemanticSegmentationMatte` production is iOS-only (`API_UNAVAILABLE(macos)`); macos = deny.
- dep-avfcalib — native both: `AVCameraCalibrationData` intrinsic/extrinsic matrices (class macos 10.13 + ios 11), but full delivery needs depth path.
- dep-avfperframe — native both: per-frame `kCMSampleBufferAttachmentKey_CameraIntrinsicMatrix` + `isCameraIntrinsicMatrixDeliveryEnabled` on a plain video connection (no depth hw needed; opt-in gated by stabilization=off / non-ultra-wide).
- dep-avfpixelsize — native both: `AVCaptureDeviceFormat`-adjacent `pixelSize` is a sensor-geometry property, not depth-gated.
- dep-avfdistlut — native both: `lensDistortionLookupTable`/`inverse`/`lensDistortionCenter` on `AVCameraCalibrationData` (class on both OSes).
- dep-avfspatial — native both: `isSpatialVideoCaptureSupported`/`Enabled` + `spatialCaptureDiscomfortReasons` are macos(15)+ios(18) at the movie-output pin.
- dep-c2depth — native: camera2ndk DEPTH_OUTPUT capability (`DEPTH16`, stream configs, exclusivity).
- dep-c2depthjpeg — native: camera2ndk `DYNAMIC_DEPTH`/`DEPTH_JPEG` still depth.
- dep-c2calib — native: `LENS_INTRINSIC_CALIBRATION` fx,fy,cx,cy,skew.
- dep-c2distortion — native: `LENS_DISTORTION` Brown-Conrady coeffs (supersedes deprecated `RADIAL_DISTORTION`).
- dep-c2pose — native: `LENS_POSE_TRANSLATION`/`_ROTATION`/`_REFERENCE`.
- dep-c2ir — native: camera2ndk MONOCHROME/`Y8` mono stream.
- dep-c2nircfa — native: `SENSOR_INFO_COLOR_FILTER_ARRANGEMENT_NIR`.
- dep-mfdepth — native: WinRT `DepthMediaFrame`/`MediaFrameSourceKind.Depth`.
- dep-mfreliable — native: `DepthFormat.MinReliableDepth`/`MaxReliableDepth` = accuracy range.
- dep-mfunit — native: `DepthScaleInMeters`.
- dep-mfcoordmap — native: `DepthCorrelatedCoordinateMapper`/`TryCreateCoordinateMapper` 2D↔3D.
- dep-mfintrinsics — native: `CameraIntrinsics.FocalLength`/`PrincipalPoint`.
- dep-mfdistortion — native: `CameraIntrinsics.RadialDistortion`/`TangentialDistortion`.
- dep-mfwarp — native: `UndistortPoint(s)`/`DistortPoint`/`ProjectOntoFrame`/`UndistortedProjectionTransform`.
- dep-mfmask — native: `BACKGROUNDSEGMENTATION_MASK` person/bg mask track output (covers portrait matte + mask.person).
- dep-mfir — native: WinRT `MediaFrameSourceKind.Infrared`/`InfraredMediaFrame`.
- dep-mfhello — native: Windows Hello IR under `KSCATEGORY_SENSOR_CAMERA` + `_FACEAUTH_MODE`/secure-mode.
- dep-v4l2depth — native: `Z16` 16-bit depth map.
- dep-v4l2conf — native: `CNF4` 4-bit confidence map.
- dep-v4l2d4xx — native: per-frame intrinsics via `V4L2_META_FMT_D4XX` metadata node (RealSense; CameraIntrinsics id 5).
- dep-v4l2ir — native: `GREY`/`Y10..Y16` mono/IR streams.
- dep-v4l2stereoir — native: `Y8I`/`Y12I` interleaved stereo-IR.
- dep-v4l2inzi — native: `INZI` IR(Y10)+depth(Z16) interleaved.
- dep-lcir — native: libcamera `R8`/`R10`/`R12`/`R16` mono.
- dep-lcnircfa — native: libcamera `ColorFilterArrangement==MONO`.
- dep-pwgray — native: pipewire `GRAY8`/`GRAY16` mono format (no IR semantic tag, just the grey format).
- dep-uvcirpin — native: uvc multi-VS IR streaming pin (RGB+IR module).
- dep-msxucalib — native: uvc `MSXU_CONTROL_CAMERA_INTRINSICS` 0x08 / `_EXTRINSICS` 0x07 (firmware-implemented MS-XU).
- dep-msxufaceauth — native: uvc `MSXU_CONTROL_FACE_AUTHENTICATION` 0x06 IR auth streaming.
- dep-genscan3d — native: genicam Scan3dControl (`Coord3D_*`, focal length, principal point, distance unit, output mode incl. DisparityC).
- dep-genconf — native: genicam `ComponentSelector=Confidence`/`Confidence*` formats.
- dep-geninvalid — native: genicam `Scan3dInvalidDataFlag`+`Value`/`AxisMin`/`Max`.
- dep-genunit — native: genicam `Scan3dDistanceUnit`/`CoordinateSystem`/`CoordinateScale`/`Offset`.
- dep-genir — native: genicam `Mono*`/`ComponentSelector=Infrared`.
- dep-gennircfa — native: genicam `PixelColorFilter=None`.
- dep-genchunk — native: genicam `ChunkScan3dFocalLength`/`PrincipalPoint*` per-frame.
- dep-genpixelsize — native: genicam `SensorPixelWidth`/`Height`.
- dep-genposeref — native: genicam `Scan3dCoordinateSystemReference`.
- dep-genbaseline — native: genicam `Scan3dBaseline` stereo baseline [m].
- dep-gendc — native: genicam GenDC multicomponent container (range+intensity+confidence in one) — the only axis where a `[CEILING]` raw cap is native.

## emulate
- dep-rawuvc — emulate; cost: firmware-must-implement MS-XU. Calibration intrinsics/extrinsics reachable via raw-UVC `MS_CAMERA_CONTROL_XU` on macos(IOKit)/win32(KsProperty)/linux(UVCIOC) where the high-level API omits it; deny ios/web.
- dep-cxinterop — emulate; cost: drop to `Camera2Interop` + raw `ImageReader`/characteristic read. CameraX has no first-class depth/calibration use-case; everything camera2-native is reachable but only by escaping CameraX.

## ceiling
- dep-ceilingdeny — deny; fallback `cap.depth.map.float`. `[CEILING]` raw-sensor caps have no consumer-OS surface; OS gives only the depth/AF result.
- dep-ceilinghal — `?`; embedded-Linux HAL reachability for `[CEILING]` raw ToF/structured/lidar/PDAF frames is unclear (v4l2 subdev / media-controller could in principle expose pre-depth sensor pads, unverified). Fallback `cap.depth.map.float`.

## macos-vs-ios depth divergence
- dep-macdepth — `?` (depth-family) or deny (still/delivery). macOS built-in/Continuity cameras vend no depth device types; `AVDepthData`/`AVCameraCalibrationData` *classes* compile but no depth-producing hw → `?` for stream-shape caps (could light up on a future depth-capable Continuity/external path), `deny` for hard hw-gated still/delivery caps.
- dep-macir — `?`: macOS has no TrueDepth IR device type, but a UVC/external IR module enumerated as a normal AVCaptureDevice could vend a grey stream; unverified.

## fallback / absence (deny)
- dep-depthfallback — deny; fallback `cap.depth.map.float`. Depth semantics facet (accuracy/quality grade) not modeled on this axis.
- dep-c2nodisparity — deny; fallback `cap.depth.map.float`. camera2 vends DEPTH16 (metric), not a disparity map.
- dep-c2noaccuracy — deny; fallback `cap.depth.map.float`. No absolute/relative accuracy flag on camera2 depth.
- dep-nopointcloud — deny; fallback `cap.depth.map.float`. Point-cloud only on camera2/genicam.
- dep-noconf — deny; fallback `cap.depth.map.float`. Per-pixel confidence only on v4l2(CNF4)/genicam.
- dep-geninvalidonly — deny; fallback `cap.depth.map.float`. Invalid-pixel sentinel is a genicam-only declaration.
- dep-nounit — deny; fallback `cap.depth.map.float`. Distance-unit/coord-system declaration only on mf/genicam.
- dep-nofilter — deny; fallback `cap.depth.map.float`. Built-in depth smoothing/hole-fill only on avf.
- dep-noconvert — deny; fallback `cap.depth.map.float`. depth↔disparity conversion only on avf.
- dep-norate — deny; fallback `cap.depth.map.float`. Independent depth-stream rate/format only on avf/camera2.
- dep-nostill — deny; fallback `cap.depth.map.float`. Depth-with-still only on avf/camera2.
- dep-noexclusive — deny; fallback `cap.depth.map.float`. depth/color exclusivity flag is a camera2-only declaration.
- dep-nocoordmap — deny; fallback `cap.depth.map.float`. 2D↔3D coordinate mapper only on mf.
- dep-nozoomco — deny; fallback `cap.depth.map.float`. zoom×depth co-feasibility list only on avf.
- dep-distlut — deny; fallback `cap.calib.distortion.lut`. Parametric distortion model: avf exposes a LUT, not Brown-Conrady coeffs.
- dep-distmodel — deny; fallback `cap.calib.distortion.model`. Distortion-LUT form only on avf; camera2/mf give parametric coeffs.
- dep-nodistortion — deny; fallback `cap.calib.distortion.model`. No distortion surface on this axis.
- dep-warphost — deny; fallback none (host-side compute). Built-in (un)distort/project ops only on mf; elsewhere apply intrinsics host-side `[down]`.
- dep-poseinstead — deny; fallback `cap.calib.pose.translation`+`cap.calib.pose.rotation`. camera2 splits extrinsics into pose translation+rotation, no single extrinsic matrix.
- dep-extrinsicinstead — deny; fallback `cap.calib.extrinsics.matrix`. Decomposed pose translation/rotation only on camera2; others (if any) carry a combined extrinsic matrix.
- dep-noreference — deny; fallback `cap.calib.extrinsics.matrix`. Pose-reference-frame enum only on camera2/genicam.
- dep-nopixelsize — deny; fallback none. Physical pixel pitch only on avf/genicam.
- dep-noperframe — deny; fallback `cap.calib.intrinsics.matrix`. Per-frame intrinsic attachment only on avf/v4l2-D4XX/genicam-chunk.
- dep-nodelivery — deny; fallback `cap.calib.intrinsics.matrix`. Opt-in calibration-with-still delivery only on avf (depth-gated).
- dep-nobaseline — deny; fallback none. Stereo baseline distance only on genicam.
- dep-noseg — deny; fallback none. No person/portrait/semantic segmentation on this axis.
- dep-nosemantic — deny; fallback `cap.seg.mask.person`. Per-class semantic matte only on avf; mf gives a single person mask.
- dep-noorient — deny; fallback none. Matte EXIF re-orient only on avf.
- dep-segmaskfallback — deny; fallback none. Live person-mask track output only on mf/web; avf gives stills mattes not a live mask track.
- dep-nostereoir — deny; fallback `cap.ir.stream`. Interleaved stereo-IR only on v4l2.
- dep-noirdepth — deny; fallback `cap.ir.stream`+`cap.depth.map.float`. IR+depth interleaved only on v4l2(INZI).
- dep-noauth — deny; fallback none. Secure IR face-auth only on mf(Hello)/uvc(MSXU).
- dep-nonircfa — deny; fallback `cap.ir.stream`. NIR/MONO CFA report only on camera2/libcamera/genicam.
- dep-nospatial — deny; fallback none. Stereo/spatial video only on avf (and capture-card dual-link 3D `[>pin]`).

## libcamera / pipewire absence
- dep-lc2d — deny; fallback `cap.depth.map.float`. libcamera is a 2D ISP stack: no depth/disparity/stereo/ToF/structured-light abstraction.
- dep-lcnocalib — deny; fallback none. libcamera exposes no runtime intrinsics/extrinsics/distortion; calibration lives in IPA tuning files.
- dep-pwtransport — deny; fallback none. pipewire is transport: no depth/stereo/intrinsics/distortion concept; IR/mono is only a grey pixel format.

## genicam non-effect
- dep-genonoeffect — deny; fallback none. Industrial GenICam cameras do no portrait/semantic/person segmentation by design (host-side downstream concern).

## uvc vendor-only
- dep-uvcdepthvendor — deny; fallback `cap.depth.map.float`. uvc per-device depth decode is vendor-specific where no MS-XU is implemented; native only for `uvc:`-listed controls.
- dep-uvcsegvendor — deny; fallback none. uvc beautify/segmentation is vendor-XU only.

## web
- dep-webnodepth — deny; fallback none. getUserMedia exposes no depth/disparity/point-cloud; depth lives in WebXR (separate domain).
- dep-webnocalib — deny; fallback none. No camera intrinsics/extrinsics/distortion on getUserMedia.
- dep-webnoir — deny; fallback none. No IR/NIR/mono stream on getUserMedia.
- dep-webnoseg — deny; fallback none. No portrait/semantic matte on getUserMedia; only the (experimental) person mask below.
- dep-webnospatial — deny; fallback none. No stereo/spatial video on getUserMedia.
- dep-webmaskexp — `?`; fallback none. `backgroundSegmentationMask` is Chromium-experimental (Firefox/Safari none); person/bg mask as track output, unstable API.

## eff
# 40 — notes: live effects (`eff-`)

- eff-readonly — OS-owned effect is user/system-controlled, no app setter; `.toggle`/`.config` deny, only `.state`/`.support_query` read (avf Portrait/Studio-Light/Background-Replacement)
- eff-rawuvc — emulate via raw-UVC under-the-OS access: MSXU `DIGITALWINDOW`/`DIGITALWINDOW_CONFIG` on DIGITALWINDOW-bearing webcams; firmware-dependent, GET_INFO/GET_LEN gated, no convergence-state surface
- eff-noeffect — axis contributes zero native live-effects by design (libcamera/pipewire/v4l2/genicam: no subject-aware FX; V4L2_CID_COLORFX is a fixed non-subject catalog); fallback none
- eff-webexp — native but Chromium-experimental (~Chrome 116+, Intel-authored); Firefox/Safari absent; genuine getUserMedia camera tracks only (not canvas/generator)
- eff-iosplist — app-toggleable on macOS+iOS BOTH, but iOS requires Info.plist `NSCameraReactionEffectsEnabled` opt-in
- eff-divergence — macOS-vs-iOS availability divergence unresolved (`isStudioLightSupported` flagged `?` in source); macOS native, iOS pending confirm → `?`

## frm
# frame-memory (frm) matrix — pattern notes

- frm-cpufallback — when a zero-copy surface kind or exotic delivery format is unavailable, the consumer falls back to `cap.frame.map.cpu` (native on all 9 consumer axes) and reads pixels off the CPU pointer; cost = one CPU read / lost zero-copy.
- frm-surfkind — zero-copy surface kinds are axis-bound and never cross: IOSurface (apple), AHardwareBuffer (android), dmabuf+modifiers+explicit_sync (linux), D3D11 (win32), WebGPU VideoFrame (web). On a foreign axis the kind denies; fallback chains to that axis's own native surface kind, ultimately `cap.frame.map.cpu`. cost = no cross-platform zero-copy handle; per-axis import path only.
- frm-cxinterop — CameraX exposes only the common formats (YUV_420_888, JPEG, RGBA_8888, PRIVATE) + `OUTPUT_IMAGE_FORMAT_*`; exotic delivery formats (16-bit RGB, P010, RAW, NV16/NV24, planar) are reachable only by dropping to Camera2Interop, so on the camerax column they deny with fallback to the common YUV/RGBA format. cost = lose CameraX ergonomics or convert.
- frm-uvcstack — raw-UVC owns only the negotiated payload bytes (YUYV/MJPEG/H264) carried by the stream; pooling, mapping, zero-copy, drop policy and format enumeration are the host-OS stack's job, not UVC's. So nearly all `cap.frame.*` deny on the uvc-direct axis with fallback = the host-OS axis (avf/camera2/v4l2/mf per platform). Native only for the three stream-carried formats.
- frm-ceiling — `cap.frame.format.3d.coord` / `.3d.confidence` have no consumer-OS delivery on any of the 9 axes; native only on genicam,sfnc (PFNC `Coord3D_*` / `Confidence*`). On every consumer axis they deny with fallback `cap.frame.format.gray_highbit` (high-bit mono is the nearest deliverable container). MEL-ENGINE-I keeps them in vocab.
- frm-vocabq — vocab §14d research-scheduled `?` cells: mf L8 (`gray8`), mf L16 (`gray_highbit`), mf RGB565 (`rgb.lowbit`), camera2 P210 (`yuv.semiplanar422_hdr`), and camera2/camerax/v4l2/libcamera `drop.reason` (signal-only — drop is signalled but the reason is not surfaced). Stay `?` until P7 resolves existence/classification.
- frm-webpool — web pooling and queue depth are UA-internal; `maxBufferSize` on `MediaStreamTrackProcessor` is the only knob and dropped frames carry no per-frame reason. `cap.frame.pool.queue_depth` → `?` on web (knob exists but semantics are UA-defined); `pool.allocate`/`pool.lifecycle_events` deny (UA owns the pool).
- frm-genicam-unlisted — frame-plumbing IDs whose §8b `normalizes` omits a `genicam:` entry (`pool.recycle` GenTL DSQueueBuffer, `deliver.next` DSGetBufferInfo) deny on the genicam column even though GenTL has the mechanism — classified strictly by what the vocab lists. fallback = host-OS axis / none.

## tim
# 40-notes-tim — timing matrix note fragments

Keys referenced from `40-matrix-tim.csv` column 5. `native` cells use a per-axis key tagging the backing API; `emulate`/`deny`/`?` carry cost / fallback / open-question.

## emulate — framework data-plane (cost-bearing)
- tim-avsync-emu — emulate everywhere; the OBS shared-A/V-clock machinery (per-source rebase, jitter buffer, ts-jump recovery, sync-offset). cost: latency + memory (frame buffering, `MAX_ASYNC_FRAMES`). NEVER deny, NEVER native.
- tim-avshared-emu — emulate; no OS shared-A/V capture session — app rebases video + separate audio onto one monotonic host clock. cost: rebase math + per-source `timing_adjust`.
- tim-crossmap-emu — emulate; clocks already same domain (e.g. QPC) or app linear-maps two capture clocks. cost: per-frame affine map.
- tim-timebase-emu — emulate; app-controlled timeline (rate/anchor) built over the source clock — framework data-plane like av-sync, not deny. cost: timebase abstraction.
- tim-cxinterop — emulate; CameraX hides the deep timing knob — reach it via `Camera2Interop` on the underlying camera2 device. cost: interop escape hatch; degrades to deny if camera2 backing unavailable.
- tim-wall-emu — emulate; source delivers monotonic only — pair to wall-clock by sampling realtime↔monotonic offset (a `ClockRecovery`-style model). cost: drift tracking.
- tim-seq-emu — emulate; no dedicated drop-revealing counter — derive sequence from per-request frame numbers / PTS gaps. cost: gap bookkeeping.
- tim-discont-emu — emulate; no explicit gap tick — synthesize from sequence/`presentedFrames` gaps or per-frame error flags. cost: gap detection.
- tim-imu-emu — emulate; frame ts and IMU samples share a derivable clock domain (MONOTONIC/graph↔IIO) — app correlates. cost: cross-domain align.

## emulate — raw-UVC under-the-OS
- tim-rawuvc — emulate (macos); parse the raw isoc UVC payload header off IOKit for PTS / SCR-SOF / `dwClockFrequency`. cost: raw-USB parse + clock-map reconstruction.

## native — frame.timestamp (all axes)
- tim-ts-avf — `CMSampleBufferGetPresentationTimeStamp` (host-time clock).
- tim-ts-c2 — `ACAMERA_SENSOR_TIMESTAMP` (ns, start-of-exposure).
- tim-ts-cx — `ImageProxy.getImageInfo().getTimestamp()` (basic frame ts only).
- tim-ts-v4l2 — `v4l2_buffer.timestamp`.
- tim-ts-lc — `SensorTimestamp` / `FrameMetadata::timestamp` (BOOTTIME).
- tim-ts-pw — `spa_meta_header.pts`.
- tim-ts-mf — `IMFSample::GetSampleTime` (QPC, 100ns).
- tim-ts-web — `VideoFrame.timestamp` (µs).
- tim-ts-uvc — per-frame payload PTS.
- tim-ts-genicam — `ChunkTimestamp`.

## native — clock-domain id
- tim-clkdom-avf — host-time clock domain.
- tim-clkdom-c2 — `SENSOR_INFO_TIMESTAMP_SOURCE` (`_UNKNOWN`/`_REALTIME`).
- tim-clkdom-v4l2 — `TIMESTAMP_MONOTONIC`/`_UNKNOWN`/`_COPY` flags.
- tim-clkdom-lc — CLOCK_BOOTTIME (documented).
- tim-clkdom-pw — graph clock.
- tim-clkdom-mf — QPC.

## native — clock-domain select / capture-point
- tim-clksel-c2 — `OutputConfiguration.setTimestampBase`.
- tim-cappt-c2 — `SENSOR_READOUT_TIMESTAMP` + `setReadoutTimestampEnabled`.
- tim-cappt-v4l2 — `TSTAMP_SRC_SOE`/`_EOF`.

## native — wallclock
- tim-wall-lc — `FrameWallClock` (REALTIME paired via `ClockRecovery`).
- tim-wall-mf — `MetadataTimeStamps.Presentation`.
- tim-wall-web — `VideoFrame`/rVFC wall-clock signal.

## native — sequence-id
- tim-seq-v4l2 — `v4l2_buffer.sequence`.
- tim-seq-lc — `FrameMetadata::sequence`.
- tim-seq-pw — `spa_meta_header.seq`.
- tim-seq-web — `presentedFrames`.
- tim-seq-uvc — FID toggle.
- tim-seq-genicam — `ChunkFrameID`.

## native — request-id
- tim-reqid-c2 — `getFrameNumber()` / `SYNC_FRAME_NUMBER`.
- tim-reqid-mf — `REQUESTED_FRAME_SETTING_ID`.

## native — rolling-shutter
- tim-skew-c2 — `SENSOR_ROLLING_SHUTTER_SKEW`.
- tim-skew-lc — `draft::SensorRollingShutterSkew`.
- tim-linetime-v4l2 — `IMAGE_SOURCE` vblank/hblank + `TSTAMP_SRC_SOE` + subdev frame interval.

## native — readout-rate / settings-latency
- tim-readoutrate-mf — `MF_CAPTURE_METADATA_SENSORFRAMERATE`.
- tim-latency-c2 — `SYNC_MAX_LATENCY` / `REQUEST_PIPELINE_DEPTH`.
- tim-latency-lc — `draft::MaxLatency` / `PipelineDepth`.

## native — events
- tim-expevent-genicam — `EventExposureEnd` / `EventFrameTrigger` / `AcquisitionStatus[ExposureActive]`.
- tim-syncevent-v4l2 — `V4L2_EVENT_VSYNC` / `_FRAME_SYNC` (`SUBSCRIBE_EVENT`/`DQEVENT`).
- tim-syncevent-genicam — async frame-start event surface (Event/AcquisitionStatus).

## native — device-clock
- tim-clockrate-uvc — `dwClockFrequency`.
- tim-clockrate-genicam — `DeviceClockSelector` + `DeviceClockFrequency`.
- tim-devpts-v4l2 — `V4L2_META_FMT_UVC` raw payload PTS.
- tim-devpts-mf — `MetadataTimeStamps.Device`.
- tim-devpts-uvc — payload PTS (device-clock units).
- tim-devpts-genicam — `ChunkTimestamp` (device counter per frame).
- tim-scrsof-v4l2 — `V4L2_META_FMT_UVC`/`_UVC_MSXU_1_5`/`META_CAPTURE` (`ts`+`sof`).
- tim-scrsof-mf — `MetadataId_UsbVideoHeader` (`KSSTREAM_UVC_METADATA`).
- tim-scrsof-uvc — SCR = STC + 1kHz SOF.
- tim-tscounter-genicam — `Timestamp` / `TimestampReset` / `TimestampLatch` + `Value`.

## native — av-clock
- tim-avshared-avf — `AVCaptureSession.synchronizationClock`.
- tim-avshared-mf — capture session QPC A/V common clock.
- tim-avshared-web — `getUserMedia({video,audio})` shared timeline.
- tim-avshared-pw — common graph clock spans audio + video nodes.
- tim-crossmap-avf — `CMSyncConvertTime` / `CMSyncGetRelativeRate` / `AVCaptureInputPort.clock`.
- tim-crossmap-pw — `pw_stream_get_time_n` / `pw_stream_get_nsec`.
- tim-crossmap-genicam — PTP `PtpOffsetFromMaster` cross-camera clock map.
- tim-timebase-avf — `CMTimebase` / `SetRate` / `CopyMasterClock`.

## native — discontinuity
- tim-discont-avf — `captureOutput:didDropSampleBuffer:` drop tick (no payload).
- tim-discont-mf — `MF_SOURCE_READERF_STREAMTICK`.
- tim-discont-pw — `SPA_META_HEADER_FLAG` `CORRUPTED`/`EMPTY`.

## native — imu-correlation
- tim-imu-avf — `CMMotionManager` → `CMClockMakeHostTimeFromSystemUnits` → `CMSyncConvertTime`.
- tim-imu-c2 — `TIMESTAMP_SOURCE_REALTIME` ↔ `SensorEvent`.
- tim-imu-lc — `SensorTimestamp` CLOCK_BOOTTIME ↔ IMU.

## native — timecode / genlock
- tim-smpte-avf — `AVMediaTypeTimecode` + `kCMTimeCodeFormatType_TimeCode32` (AVAssetWriter path).
- tim-smpte-v4l2 — `BUF_FLAG_TIMECODE` + `v4l2_timecode` (incl. SDI capture-card DV path).
- tim-syncpins-genicam — `DigitalIOControl` (`LineSource`) XVS/XHS pins.
- tim-ptp-genicam — `PtpControl` (`PtpEnable`/`PtpStatus`/`PtpOffsetFromMaster`/`PtpClockID`).

## native — frame-rate clamp (all axes)
- tim-fpsclamp-avf — `activeVideoMin/MaxFrameDuration` / `AVFrameRateRange`.
- tim-fpsclamp-c2 — `AE_TARGET_FPS_RANGE` / `SENSOR_FRAME_DURATION`.
- tim-fpsclamp-cx — `setTargetFrameRate` (camerax fps range).
- tim-fpsclamp-v4l2 — `VIDIOC_S_PARM` `timeperframe`.
- tim-fpsclamp-lc — `FrameDurationLimits`.
- tim-fpsclamp-pw — stream param framerate negotiation.
- tim-fpsclamp-mf — `MF_MT_FRAME_RATE` media-type select.
- tim-fpsclamp-web — `applyConstraints({frameRate})`.
- tim-fpsclamp-uvc — `dwFrameInterval` VS_PROBE/COMMIT.
- tim-fpsclamp-genicam — `AcquisitionFrameRate` / `AcquisitionLineRate`.

## deny — fallback / no-API
- tim-ceilingdeny — `[CEILING]` cap, no consumer-OS backing on this axis. fallback: `cap.timing.frame.timestamp`. native only on the listed provider plane.
- tim-web-noclockid — web exposes no explicit capture-clock domain id. fallback: `cap.timing.frame.timestamp`.
- tim-uvc-nodomainid — UVC delivers device-clock PTS, not a host monotonic/realtime domain id. fallback: `cap.timing.frame.timestamp`.
- tim-genicam-nodomainid — genicam has a device counter, not a host clock-domain id. fallback: `cap.timing.frame.timestamp`.
- tim-noclocksel — no API to choose the stamp clock domain. fallback: `cap.timing.frame.timestamp.clock-domain-id`.
- tim-nocapturepoint — no SOE/EOF capture-point select. fallback: `cap.timing.frame.timestamp`.
- tim-noreqid — no per-request frame-number correlation (streaming model). fallback: `cap.timing.frame.sequence-id`.
- tim-noskew — no rolling-shutter skew field. fallback: `cap.timing.rolling-shutter.line-time-derived` / none.
- tim-noskew-v4l2 — no direct skew field on v4l2. fallback: `cap.timing.rolling-shutter.line-time-derived`.
- tim-nolinetime — no line-time + SOE reconstruction surface. fallback: `cap.timing.rolling-shutter.skew` / none.
- tim-noreadoutrate — no measured sensor-readout-rate metadata. fallback: `cap.timing.frame-rate.clamp`.
- tim-nolatency — no settings-apply latency frame count. fallback: none.
- tim-nosyncevent — no async VSYNC/frame-start event. fallback: `cap.timing.frame.timestamp`.
- tim-noclockrate — no exposed device-clock frequency. fallback: none.
- tim-nodevpts — no device-clock PTS access. fallback: `cap.timing.frame.timestamp`.
- tim-noscrsof — no SCR/SOF device↔host map (no raw-UVC path / not USB-UVC). fallback: `cap.timing.frame.timestamp`.
- tim-nosmpte — no SMPTE timecode. fallback: none.
- tim-genicam-noaudio — genicam is video-only; no bundled audio session. fallback: none.
- tim-genicam-nowall — no per-frame paired wall-clock. fallback: `cap.timing.frame.timestamp`.
- tim-genicam-nosdiref — SDI house-reference is a capture-card concern, not genicam (genicam uses PTP). fallback: `cap.timing.genlock.ptp-clock-sync`.
- tim-uvc-nowall — UVC device clock only, no wall-clock. fallback: `cap.timing.frame.timestamp`.
- tim-imu-noweb — no per-frame IMU correlation clock (DeviceMotion is separate, unshared). fallback: none.
- tim-imu-nouvc — UVC device clock, no IMU. fallback: none.
- tim-imu-nogenicam — no IMU in genicam. fallback: none.

## ? — open
- tim-imu-mf-q — `?` IMU/gyro correlation on MF reachable only via OEM custom metadata blob (`MF_CAPTURE_METADATA` Any-Custom-GUID); no standard gyro/OIS sample attribute. resolve before freeze.
- tim-sdiref-v4l2-q — `?` SDI house-reference reachability via v4l2 `QUERY/G/S/ENUM_DV_TIMINGS` is partial (locks incoming signal timing, but no genuine tri-level/house-ref clock concept). resolve before freeze.

## met
# 40-notes-met — metadata matrix note fragments

## avfoundation (macos + ios)
- met-avfbag — native: `kCMSampleBufferAttachmentKey_*` keyed attachments on each `CMSampleBuffer`.
- met-avfexif — cost: per-frame value read from EXIF buffer attachment (`kCGImagePropertyExif*` via `CMGetAttachment`) on STILLS; NOT a per-video-frame result bag; for video falls to live device prop, no per-frame guarantee.
- met-avfexifstill — cost: EXIF/TIFF blob only on still capture (`AVCapturePhoto` EXIF), not per video frame.
- met-avfliveprop — cost: value sampled from live `AVCaptureDevice` props (`exposureDuration`/`ISO`/`lensAperture`/`deviceWhiteBalanceGains`/`exposureTargetOffset`/`videoZoomFactor`), polled — not a per-frame buffer attachment.
- met-avfadjustflag — cost: only the boolean `isAdjusting{Focus,WhiteBalance}` live prop, not a full convergence-state enum; coarse + polled, not per-frame.
- met-avfmetaobj — native: `AVMetadataFaceObject` (`.bounds`/`.faceID`) via `AVCaptureMetadataOutput`.
- met-avffacepose — native: `AVMetadataFaceObject.rollAngle`/`.yawAngle`.
- met-avfmrc — native: `AVMetadataMachineReadableCodeObject.stringValue`/`.corners`/`.descriptor`.
- met-avfbodyobj — native: `AVMetadataBodyObject`/`HumanFullBody`/`Cat/DogBody`/`Head` (iOS-centric; constants compile on macos 14).
- met-avfsalient — native: `AVMetadataSalientObject`.
- met-avfnorawblob — fallback: none — no raw driver metadata blob surfaced.
- met-avfnoselect — fallback: none — no per-chunk selectable metadata enable.
- met-avfnogainsplit — fallback: cap.meta.applied.iso — no analog/digital gain split, only combined ISO.
- met-avfnoexpcompresult — fallback: none — no per-frame EV-compensation echo.
- met-avfnowbtemp — fallback: cap.meta.applied.wbgains — no Kelvin WB echo; only per-channel gains live prop.
- met-avfnoccm — fallback: none — no per-frame color-correction matrix echo.
- met-avfnotonemap — fallback: none — no tonemap/gamma curve echo.
- met-avfnoblacklevel — fallback: none — no dynamic black-level echo.
- met-avfnoscenemode — fallback: none — no scene-mode echo.
- met-avfnoflashstate — fallback: none — no per-frame flash-fired state.
- met-avfnocropecho — fallback: none — no per-frame scaler-crop echo.
- met-avfnodigwin — fallback: none — no digital-window/auto-framing rect echo.
- met-avfnobinning — fallback: none — no binning/readout-mode echo.
- met-avfnogeometryecho — fallback: none — no geometry/pixfmt/ROI/flip echo.
- met-avfnoaestate — fallback: none — no AE convergence-state buffer attachment.
- met-avfnoregionsecho — fallback: none — no applied 3A metering-rectangle echo.
- met-avfnofacescore — fallback: none — face object carries no confidence score.
- met-avfnolandmarks — fallback: none — `AVMetadataFaceObject` has no landmark points.
- met-avfnofaceexpr — fallback: none — no blink/smile scoring.
- met-avfnoscenechange — fallback: none — no scene-change flag.
- met-avfnoflicker — fallback: none — no flicker/power-line-freq detection.
- met-avfnonighthint — fallback: none — no night/low-light scene indicator.
- met-avfnoillum — fallback: none — no scene-illuminance (lux) estimate.
- met-avfnofocusfom — fallback: none — no focus figure-of-merit.
- met-avfnostatsmap — fallback: none — no histogram/sharpness/lens-shading/hot-pixel/predicted-color map in capture (those are Core Image / Vision downstream).
- met-avfnoispblob — fallback: none — no vendor ISP statistics blob.
- met-avfnoispparams — fallback: none — no ISP config/params metadata.
- met-avfnoois — fallback: none — no OIS displacement sample stream.
- met-avfnosensortemp — fallback: none — no sensor die-temperature.
- met-avfnouvcheader — fallback: cap.meta.access.rawblob — UVC payload-header parsed internally, not surfaced.
- met-avfnochunk — fallback: none — no GenICam-style chunk I/O/frame-id metadata.
- met-avfnotransformecho — fallback: none — no per-frame transform/orientation echo (rotation is connection-level, not buffer meta).
- met-avfnodamage — fallback: none — no damage/changed-region rects.
- met-avfnoirillum — fallback: none — no IR active-illumination on/off flag.
- met-avfnosegmask — fallback: none — no camera-emitted segmentation mask.
- met-avfnobracketid — fallback: none — no bracket setting-ID correlation tag.
- met-avfnocustomblob — fallback: none — no OEM/vendor custom metadata blob path.

## camera2ndk + camerax
- met-cxhide — cost: CameraX hides `CaptureResult`; metadata bag reachable only via `Camera2Interop`/`Camera2CameraInfo` escape hatch.
- met-cxinterop — cost: CameraX exposes no first-class API; per-frame result reached via `Camera2Interop` `CaptureResult` callbacks (escape hatch), not CameraX surface.
- met-cam2autoframing — native: `CONTROL_AUTOFRAMING_STATE` result.
- met-cam2extfacedetect — native: `EXTENDED_FACEDETECTION` `_BLINK`/`_SMILE` (vendor-extension, availability-gated).
- met-cam2fulllandmarks — native: landmarks only in `STATISTICS_FACE_DETECT_MODE_FULL`.
- met-cam2histlimited — cost: `STATISTICS_HISTOGRAM_MODE` present but legacy/limited availability (`INFO_MAX_HISTOGRAM_COUNT`).
- met-cam2sharplimited — cost: `STATISTICS_SHARPNESS_MAP_MODE` present but legacy/limited availability.
- met-cam2jpegexif — native: `JPEG_*` result keys (EXIF on JPEG/stills).
- met-cam2nightext — native: `EXTENSION_NIGHT_MODE_INDICATOR`/`LOW_LIGHT_BOOST_STATE` (extension/availability-gated).
- met-cam2predcolordeprecated — ?: `STATISTICS_PREDICTED_COLOR_GAINS`/`_TRANSFORM` deprecated/`@hide` in modern API; existence uncertain.
- met-cam2fixedresult — fallback: none — CaptureResult key set is OS-fixed, no per-chunk selectable enable.
- met-cam2norawblob — fallback: cap.meta.access.bag — no raw unparsed driver blob; only parsed CaptureResult keys.
- met-cam2nowbtemp — fallback: cap.meta.applied.wbgains — no Kelvin WB result key; only `COLOR_CORRECTION_GAINS`.
- met-cam2noscenemoderesult — fallback: none — scene-mode is a request hint, no per-frame result echo.
- met-cam2noexptargetoffset — fallback: none — no metered-exposure-target-offset result key.
- met-cam2nofacepose — fallback: none — no per-face roll/yaw angles.
- met-cam2nobody — fallback: none — no body/pet/head detection.
- met-cam2nosalient — fallback: none — no salient-object box.
- met-cam2nocode — fallback: none — barcode is not camera metadata (`SCENE_MODE_BARCODE` is only a tuning hint); decode downstream.
- met-cam2noillum — fallback: none — no scene-illuminance (lux).
- met-cam2nofocusfom — fallback: none — no focus figure-of-merit.
- met-cam2noispblob — fallback: none — no vendor ISP statistics blob.
- met-cam2noispparams — fallback: none — no round-tripped ISP params metadata.
- met-cam2nosensortemp — fallback: none — no per-frame sensor die-temperature.
- met-cam2nogeometryecho — fallback: cap.meta.applied.cropregion — no full geometry/pixfmt/flip echo bundle.
- met-cam2nouvcheader — fallback: none — no UVC payload-header bits.
- met-cam2nochunk — fallback: none — no GenICam chunk I/O-line/counter/timer metadata.
- met-cam2nochunkframeid — fallback: cap.meta.access.bag — frame correlation via request, no chunk frame-id.
- met-cam2notransformecho — fallback: none — orientation is `SENSOR_ORIENTATION`/display convention, no per-frame transform-echo meta.
- met-cam2nodamage — fallback: none — no damage-region rects.
- met-cam2noirillum — fallback: none — no per-frame IR active-illumination flag.
- met-cam2nosegmask — fallback: none — no camera-emitted segmentation mask.
- met-cam2nobracketid — fallback: none — no bracket setting-ID correlation tag.
- met-cam2nocustomblob — fallback: none — no generic OEM custom metadata-blob channel.

## linux,v4l2
- met-v4l2metanode — native: `V4L2_BUF_TYPE_META_CAPTURE` on a separate metadata video node.
- met-v4l2uvcblob — native: `V4L2_META_FMT_UVC`/`_D4XX` raw payload-header/device metadata blob.
- met-v4l2metaneg — cost: per-format `CAP_META_CAPTURE` negotiation selects which meta format, not per-chunk individual enable.
- met-v4l2uvcheader — native: `V4L2_META_FMT_UVC`/`_UVC_MSXU_1_5` payload-header (FID/EOF/error/SOF) flags.
- met-v4l2hgo — native: `V4L2_META_FMT_VSP1_HGO`/`_HGT` (Renesas hardware histogram).
- met-v4l2ispstat — native: `RK_ISP1_STAT_3A`/`IPU3_3A`/`MALI_C55_STATS`/`C3ISP_STATS`/`RPI_BE`/`FE_STATS` ISP statistics blobs.
- met-v4l2ispparams — native: `RK_ISP1_PARAMS`/`EXT_PARAMS`/`IPU3_PARAMS`/`MALI_C55_PARAMS`/`C3ISP_PARAMS`/`RPI_BE_CFG`/`FE_CFG` (round-tripped ISP params).
- met-v4l2csilines — native: `V4L2_META_FMT_GENERIC_8`/`CSI2_10..24` embedded CSI-2 sensor metadata lines (embedded-Linux subdev).
- met-v4l2nostatesult — fallback: none — V4L2 emits sensor-line ISP stats, not per-frame applied-3A/settings echo (that lives in the ISP userspace, e.g. libcamera).
- met-v4l2nodetect — fallback: none — no face/scene/code detection at kernel level (only coarse `V4L2_EVENT_MOTION_DET`).
- met-v4l2nostatmap — fallback: cap.meta.stats.ispblob — no standalone sharpness/lens-shading/hot-pixel/predicted-color map (folded into vendor ISP blobs).
- met-v4l2nogeometryecho — fallback: none — no per-frame geometry/format echo metadata.
- met-v4l2noois — fallback: none — no OIS sample stream.
- met-v4l2nosensortemp — fallback: none — no per-frame sensor temperature in metadata.
- met-v4l2nochunk — fallback: none — no GenICam-style chunk I/O/counter metadata.
- met-v4l2notransformecho — fallback: none — `CAMERA_ORIENTATION`/HFLIP/VFLIP are controls, no per-frame transform-echo buffer meta.
- met-v4l2nodamage — fallback: none — no damage-region rects.
- met-v4l2noirillum — fallback: none — no per-frame IR active-illumination flag.
- met-v4l2nosegmask — fallback: none — no segmentation mask.
- met-v4l2noexif — fallback: none — no camera-stack EXIF blob.
- met-v4l2nobracketid — fallback: none — no bracket setting-ID tag (Request API correlates buffers, not a setting-id meta).
- met-v4l2nocustomblob — fallback: cap.meta.access.rawblob — vendor payloads ride the raw UVC/D4XX blob, no generic custom channel.

## linux,libcamera
- met-lcdraftface — cost: face metadata exists only as `controls::draft::FaceDetect*`, rarely implemented by pipelines.
- met-lcfixedmeta — fallback: none — `Request::metadata()` key set is pipeline-fixed, no per-chunk selectable enable.
- met-lcrpistats — native: `rpi::StatsOutputEnable`/`Bcm2835StatsOutput`/`PispStatsOutput` (RPi vendor ISP stats blob).
- met-lcdebugmeta — native: `DebugMetadataEnable`/`rpi::CnnOutputTensor` (vendor/debug custom blob channel).
- met-lcshadingavail — fallback: none — `draft::LensShadingMapMode` reports availability only, no map payload in stable API.
- met-lcnorawblob — fallback: cap.meta.access.bag — parsed ControlList only, no raw unparsed blob.
- met-lcnoisokey — fallback: cap.meta.applied.analogdigitalgain — no ISO key; gain is `AnalogueGain`+`DigitalGain`.
- met-lcnoaperturekey — fallback: none — no per-frame aperture metadata key.
- met-lcnotonemap — fallback: none — no tonemap-curve metadata.
- met-lcnoscenemode — fallback: none — no scene-mode echo.
- met-lcnoflashstate — fallback: none — no flash-state metadata.
- met-lcnozoomfactor — fallback: cap.meta.applied.cropregion — no zoom-factor; `ScalerCrop` echo only.
- met-lcnodigwin — fallback: none — no digital-window/auto-framing rect.
- met-lcnobinning — fallback: none — no binning/readout echo.
- met-lcnogeometryecho — fallback: cap.meta.applied.cropregion — no full geometry/format echo bundle.
- met-lcnoexptargetoffset — fallback: none — no metered-exposure-target-offset.
- met-lcnoregionsecho — fallback: none — no applied 3A metering-region echo.
- met-lcnofacepose — fallback: none — no face roll/yaw.
- met-lcnofaceexpr — fallback: none — no blink/smile.
- met-lcnobody — fallback: none — no body/pet detection.
- met-lcnosalient — fallback: none — no salient-object box.
- met-lcnocode — fallback: none — no barcode payload.
- met-lcnoscenechange — fallback: none — no scene-change flag.
- met-lcnonighthint — fallback: none — no night/low-light indicator.
- met-lcnohistogram — fallback: cap.meta.stats.ispblob — no standalone histogram (folded into vendor stats blob).
- met-lcnosharpmap — fallback: cap.meta.scene.focusfom — no sharpness map; only scalar `FocusFoM`.
- met-lcnohotpixel — fallback: none — no hot-pixel map.
- met-lcnopredcolor — fallback: none — no predicted-color gains/transform.
- met-lcnoispparams — fallback: cap.meta.stats.ispblob — emits stats, not round-tripped params.
- met-lcnoois — fallback: none — no OIS sample stream.
- met-lcnouvcheader — fallback: none — no UVC payload-header bits.
- met-lcnochunk — fallback: none — no GenICam chunk metadata.
- met-lcnotransformecho — fallback: none — orientation is config-level, no per-frame transform-echo meta.
- met-lcnodamage — fallback: none — no damage-region rects.
- met-lcnoirillum — fallback: none — no IR active-illumination flag.
- met-lcnosegmask — fallback: none — no segmentation mask.
- met-lcnoexif — fallback: none — no camera-stack EXIF blob.
- met-lcnobracketid — fallback: none — no bracket setting-ID tag.

## linux,pipewire
- met-pwparammeta — native(access): `SPA_PARAM_Meta`/`SPA_META_Header` requestable meta block; for selectable: negotiated meta-type list, coarse.
- met-pwcrop — native: `SPA_META_VideoCrop` (`spa_meta_region`).
- met-pwtransform — native: `SPA_META_VideoTransform` (rotation+flip echo).
- met-pwdamage — native: `SPA_META_VideoDamage` damage rects.
- met-pwparamtag — native: `SPA_PARAM_Tag` opaque tag metadata (custom blob channel).
- met-pwnometa — fallback: none — PipeWire carries no 3A/scene/stats/sensor camera metadata; only header/crop/transform/damage/tag.

## win32,mediafoundation
- met-mfnoaperturemeta — fallback: none — no aperture/f-number in `MF_CAPTURE_METADATA_*` set.
- met-mfnoccmmeta — fallback: none — no color-correction matrix metadata.
- met-mfnotonemap — fallback: none — no tonemap-curve metadata.
- met-mfnoblacklevel — fallback: none — no dynamic black-level metadata.
- met-mfnocropecho — fallback: cap.meta.applied.digitalwindow — no scaler-crop echo; `DIGITALWINDOW` is the framing rect.
- met-mfnobinning — fallback: none — no binning/readout echo.
- met-mfnogeometryecho — fallback: none — no geometry/pixfmt/flip echo bundle.
- met-mfnoaestate — fallback: none — no AE convergence-state (`FOCUSSTATE` is AF-only).
- met-mfnoawbstate — fallback: none — no AWB convergence-state.
- met-mfnoexptargetoffset — fallback: none — no metered-exposure-target-offset.
- met-mfnoregionsecho — fallback: none — no applied 3A metering-region echo.
- met-mfnofaceid — fallback: none — `FACEROIS` has no stable per-face tracking id.
- met-mfwsefacemeta — native: WSE `FACEMETADATA` landmark points.
- met-mffaceroichar — native: `FACEROICHARACTERIZATIONS` {Blink,Smile} scores.
- met-mfnofacepose — fallback: none — no face roll/yaw.
- met-mfnobody — fallback: none — no body/pet detection.
- met-mfnosalient — fallback: none — no salient-object box.
- met-mfnocode — fallback: none — no barcode payload metadata.
- met-mfnoscenechange — fallback: none — no scene-change flag.
- met-mfnoflicker — fallback: none — no flicker detection.
- met-mfnonighthint — fallback: none — no night/low-light indicator.
- met-mfnoillum — fallback: none — no scene-illuminance (lux).
- met-mfnofocusfom — fallback: cap.meta.applied.lensposition — no focus figure-of-merit; only `FOCUSSTATE`/`LENS_POSITION`.
- met-mfnosharpmap — fallback: none — no sharpness map.
- met-mfnoshadingmap — fallback: none — no lens-shading map.
- met-mfnohotpixel — fallback: none — no hot-pixel map.
- met-mfnopredcolor — fallback: none — no predicted-color gains/transform.
- met-mfnoispblob — fallback: cap.meta.access.rawblob — no parsed ISP stats; only raw `FRAME_RAWSTREAM` blob.
- met-mfnoispparams — fallback: none — no round-tripped ISP params.
- met-mfnoois — fallback: cap.meta.custom.blob — OIS would ride a custom-GUID blob, no first-class key.
- met-mfnosensortemp — fallback: none — no per-frame sensor temperature.
- met-mfsegmask — native: `BACKGROUNDSEGMENTATION_MASK`.
- met-mfcustomguid — native: `MF_CAPTURE_METADATA_<Custom GUID>` OEM blob (gyro/OIS/IMU).
- met-mfnouvcheader — fallback: cap.meta.access.rawblob — UVC header parsed by MFT0, surfaced via raw stream not as bits.
- met-mfnochunk — fallback: none — no GenICam chunk I/O/counter metadata.
- met-mfnotransformecho — fallback: none — `MF_MT_VIDEO_ROTATION` is media-type, no per-frame transform-echo meta.
- met-mfnodamage — fallback: none — no damage-region rects.

## web,getusermedia
- met-webnometa — fallback: none — web surfaces zero camera-internal metadata (no 3A/histogram/lens-shading/sensor-readback/scene/flicker); spec gap.
- met-webfacedetector — native: `FaceDetector` → `DetectedFace.boundingBox`/`.landmarks` (Shape Detection; non-universal, often flagged).
- met-webbarcodedetector — native: `BarcodeDetector` → `DetectedBarcode{rawValue,boundingBox,cornerPoints}` (Chromium+Safari 17; non-universal).
- met-webcroprect — native: `codedRect`/`visibleRect` crop rectangle.
- met-webvideoframexform — native: `VideoFrame.{rotation,flip}` transform/orientation echo.
- met-webnofaceid — fallback: none — `DetectedFace` has no stable tracking id.
- met-webnofacescore — fallback: none — `DetectedFace` has no confidence score.
- met-webnofacepose — fallback: none — no face roll/yaw (landmarks only, no pose).
- met-webnofaceexpr — fallback: none — no blink/smile scoring.
- met-webnobody — fallback: none — no body/pet detection.
- met-webnosalient — fallback: none — no salient-object box.
- met-webnodamage — fallback: none — no damage-region rects.
- met-webnosegmask — fallback: none — no camera-emitted segmentation mask.

## uvc-direct,uvc
- met-uvcmsxu — native: `MSXU_CONTROL_METADATA` 0x09 standardized per-frame metadata blob bag.
- met-uvcrawhdr — native: raw UVC payload-header bytes.
- met-uvcpayloadhdr — native: UVC 1.5 §2.4.3.3 payload-header status bits (FID/EOF/error/STI).
- met-uvcvendorxu — cost: applied/custom payloads only as far as a vendor XU adds them (e.g. MSXU table); not guaranteed.
- met-uvcfixedmeta — fallback: none — UVC metadata format is fixed; no per-chunk selectable enable beyond vendor XU.
- met-uvcnoapplied — fallback: none — UVC emits no per-frame applied-3A/settings echo beyond vendor XU.
- met-uvcnostate — fallback: none — no 3A convergence-state metadata.
- met-uvcnoregions — fallback: none — no 3A metering-region echo.
- met-uvcnodetect — fallback: none — no face/scene/code detection at UVC level.
- met-uvcnostats — fallback: none — no histogram/shading/OIS/sensor-temp statistics at UVC level.
- met-uvcnochunk — fallback: none — no GenICam-style chunk metadata.
- met-uvcnotransform — fallback: none — no per-frame transform/orientation echo.
- met-uvcnodamage — fallback: none — no damage-region rects.
- met-uvcnoirillum — fallback: none — no standardized IR active-illumination flag.
- met-uvcnosegmask — fallback: none — no segmentation mask.
- met-uvcnoexif — fallback: none — no camera-stack EXIF blob.
- met-uvcnobracketid — fallback: none — no bracket setting-ID tag.

## genicam,sfnc
- met-chunk — native: `ChunkExposureTime`/`ChunkGain` etc. per-frame chunk value (selectable, extensible).
- met-chunkblacklevel — native: `ChunkBlackLevel[Selector]`.
- met-chunkbinning — native: `ChunkBinning*`/`ChunkDecimation*`.
- met-chunkgeometry — native: `ChunkOffsetX/Y`/`Width/Height`/`PixelFormat`/`ReverseX/Y`/`LinePitch` geometry echo.
- met-chunkimage — native: `ChunkImage`/`ChunkXMLEnable` raw image-as-register + embedded XML.
- met-chunkio — native: `ChunkLineStatusAll`/`ChunkCounterValue`/`ChunkTimerValue`/`ChunkEncoderValue`.
- met-chunkframeid — native: `ChunkFrameID`/`ChunkTransferBlockID`.
- met-genicamevent — native: `EventExposureEnd`/`EventFrameTrigger`/`EventError` async event (with timestamp/frame-id).
- met-genicamsequencerid — cost: bracket correlation via `ChunkSequencerSetActive` / sequencer-set id, not a dedicated setting-id tag.
- met-genicamchunkcustom — cost: vendor-custom values via `ChunkSelector`/`ChunkEnable[]` extensible chunks, device-specific.
- met-genicamgain — fallback: cap.meta.applied.analogdigitalgain — `ChunkGain`, no ISO concept.
- met-genicamno3astate — fallback: none — no AE/AF/AWB convergence-state chunk (no on-device consumer 3A state model).
- met-genicamnoev — fallback: none — no EV/exposure-compensation chunk.
- met-genicamnoaperture — fallback: none — no aperture chunk (machine-vision lenses are external).
- met-genicamnolenspos — fallback: none — no lens-position chunk.
- met-genicamnoframedur — fallback: cap.meta.chunk.iostate — no frame-duration chunk; timing via `ChunkTimestamp`.
- met-genicamnowbtemp — fallback: none — no WB-Kelvin chunk.
- met-genicamnowbgains — fallback: none — no WB-gains chunk (color is vendor territory).
- met-genicamnoccm — fallback: none — no color-correction-matrix chunk.
- met-genicamnotonemap — fallback: none — no tonemap-curve chunk.
- met-genicamnoscenemode — fallback: none — no scene-mode chunk.
- met-genicamnoflash — fallback: cap.meta.chunk.iostate — strobe is via `LineStatus`, no flash-state chunk.
- met-genicamnozoom — fallback: none — no zoom-factor chunk.
- met-genicamnodigwin — fallback: cap.meta.applied.geometryecho — no digital-window; geometry via `ChunkOffset`/`Width`.
- met-genicamnoexptarget — fallback: none — no metered-exposure-target-offset chunk.
- met-genicamnoregions — fallback: none — no 3A metering-region echo (no consumer 3A regions model).
- met-genicamnodetect — fallback: none — no face/scene/code detection (vendor feature territory; not core SFNC).
- met-genicamnoscene — fallback: none — no scene change/flicker/night/illuminance/focusFoM chunk.
- met-genicamnohist — fallback: none — histogram/AOI-statistics not in core SFNC (vendor feature).
- met-genicamnostatmap — fallback: none — no sharpness/lens-shading/hot-pixel/predicted-color map in core SFNC.
- met-genicamnoispblob — fallback: cap.meta.access.rawblob — no standardized ISP stats blob (vendor chunk).
- met-genicamnoispparams — fallback: none — no round-tripped ISP params chunk.
- met-genicamnoois — fallback: none — no OIS sample stream.
- met-genicamnosensortemp — fallback: cap.meta.chunk.iostate — temperature is a DeviceControl feature, not a per-frame chunk.
- met-genicamnouvc — fallback: none — no UVC payload-header (different transport).
- met-genicamnotransform — fallback: cap.meta.applied.geometryecho — flip echoed via `ChunkReverseX/Y`, no rotation transform chunk.
- met-genicamnodamage — fallback: none — no damage-region rects.
- met-genicamnoirillum — fallback: cap.meta.chunk.iostate — illumination via `LightControl`/`LineStatus`, no per-frame IR flag chunk.
- met-genicamnosegmask — fallback: none — no segmentation mask.
- met-genicamnoexif — fallback: cap.meta.access.rawblob — no EXIF blob (chunk metadata instead).

## ceiling
- met-ceilingdeny — fallback: native genicam/embedded-Linux where listed, else none — `[CEILING]` cap: HAL-internal / industrial-only, denied to consumer OS stacks.

## egr
# 40-notes-egr — egress + test ingest matrix notes

## publish-native paths (per-axis OS publish)
- egr-avf-sysext — macOS CMIOExtension: `CMIOExtensionProvider/Device/Stream` registered via `OSSystemExtensionRequest`; consumers enumerate the vcam as an ordinary device. Cost: signed+notarized System Extension w/ user approval (`cap.egress.install.systemext`).
- egr-mf-vcam — `MFCreateVirtualCamera(MFVirtualCameraType_SoftwareCameraSource, …)`/`IMFVirtualCamera.Start` (Win11 22000+) or legacy DShow source filter. Cost: HKLM admin COM registration (`cap.egress.install.com_admin`); webcam-privacy gate → `E_ACCESSDENIED`.
- egr-v4l2loopback — `v4l2loopback` OUTPUT node: open O_RDWR, `S_FMT` type `VIDEO_OUTPUT`, write()/QBUF frames; consumers open the `/dev/videoN` as a camera. Cost: out-of-tree kernel module (`cap.egress.install.kernel_module`).
- egr-pw-node — `pw_stream` `PW_DIRECTION_OUTPUT` `media.class=Video/Source`+`media.role=Camera`; seen by PipeWire-aware consumers. Cost: userspace node (`cap.egress.install.userspace_node`); V4L2-only apps need the bridge (`egr-pw-v4l2bridge`).
- egr-avf-sendsample — SOURCE stream frame push via `CMIOExtensionStream.sendSampleBuffer:discontinuity:hostTimeInNanoseconds:`. Cost: sysext-resident extension process.
- egr-mf-imfsample — custom media source feeds `IMFSample` (`MFCreateSample`+`AddBuffer`, time+duration) to the Frame Server. Cost: COM custom-source contract; no Seek/Pause.
- egr-v4l2-output-write — producer `write()` one frame/call or streaming `QBUF`/`DQBUF`+`STREAMON` on the OUTPUT node. Cost: none beyond the loopback module.
- egr-pw-produce — on-demand produce: node fills buffers only when a consumer drives it. Cost: none.
- egr-avf-sinkpull — pull model: `StreamDirectionSink` + `consumeSampleBufferFromClient:completionHandler:`. Cost: sink-stream variant of the extension.
- egr-pw-trigger — consumer-driven pull via `pw_stream_trigger_process`/`is_driving`. Cost: none.
- egr-avf-clientevents — consumer attach/detach via `connectClient:`/`disconnectClient:`/`.streamingClients`. Cost: none.
- egr-mf-mediaevent — attach/detach via `IMFMediaEvent` registered on `Start`. Cost: none.
- egr-pw-state — `PW_STREAM_STATE_PAUSED`→`STREAMING` transition signals consumer presence (also in-use feedback). Cost: none.
- egr-avf-clientidentity — per-consumer `CMIOExtensionClient.clientID`/`.pid`/`.signingID` + `authorizedToStartStreamForClient:`. Cost: none.
- egr-avf-streamformat — per-consumer `CMIOExtensionStreamFormat` + `…ActiveFormatIndex`/`…FrameDuration`. Cost: none.
- egr-v4l2-sfmt — producer `S_FMT` defines the single visible format consumers negotiate against. Cost: one format, set by producer (no per-consumer divergence).
- egr-pw-spaformat — node format negotiated via SPA params with each consumer. Cost: none.
- egr-mf-mediatypes — per-stream MF media types + DShow `IAMStreamConfig`. Cost: none.
- egr-avf-propbag — app-defined controls via CMIOExtension property bags + `CMIOObjectGet/SetPropertyData`. Cost: none.
- egr-mf-ksproperty — controls on the vcam via `KSProperty`/`IKsControl`. Cost: none.
- egr-mf-propertypages — per-consumer config UI via DShow `ISpecifyPropertyPages`. Cost: DShow path only.
- egr-mf-sharemode — controlling-vs-sharing arbitration via `MF_DEVSOURCE_ATTRIBUTE_FRAMESERVER_SHARE_MODE` + `MF_DEVICESTREAM_FRAMESERVER_SHARED`. Cost: none.
- egr-avf-sinkflow — sink-stream flow control via `StreamSinkBuffersRequiredForStartup`/`SinkBufferQueueSize`/`SinkEndOfData`/`SinkBufferUnderrunCount`. Cost: none.
- egr-mf-wrapphysical — vcam wraps real cameras via `IMFVirtualCamera.AddDeviceSourceInfo` + `ASSOCIATED_CAMERA_SOURCES` (1 vcam per physical cam). Cost: none.
- egr-mf-invalidated — `Remove`→`MF_E_VIDEO_RECORDING_DEVICE_INVALIDATED` to in-use consumers; producer learns teardown. Cost: none.
- egr-avf-streamingclients — producer reads `.streamingClients` non-empty for in-use feedback. Cost: none.
- egr-avf-sysext-session — session lifetime: sysext active only while host app runs. Cost: none.
- egr-avf-sysext-system — system lifetime: installed sysext persists OS-wide across reboot/all-users. Cost: install persistence (`cap.egress.install.systemext`).
- egr-mf-lifetime-session — `MFVirtualCameraLifetime Session` dies on `Shutdown`. Cost: none.
- egr-mf-lifetime-system — `MFVirtualCameraLifetime System`+AllUsers persists across reboot. Cost: admin (AllUsers/System need elevation).
- egr-v4l2-producersession — producer-attach defines the active session; node goes idle when producer closes. Cost: node itself is module-lifetime, only the stream is session-scoped.
- egr-pw-nodesession — node bound to `pw_core` lifetime = producing session. Cost: none.
- egr-v4l2-bootmodule — system lifetime: `modules-load.d`/DKMS auto-loads v4l2loopback at boot → persistent node. Cost: boot-time module install.
- egr-v4l2-exclusivecaps — legacy-consumer compat: `exclusive_caps` flips node OUTPUT→CAPTURE so Chromium/WebRTC consumers accept it. Cost: none.
- egr-pw-v4l2bridge — legacy-consumer compat: `pw-v4l2` LD_PRELOAD shim or `v4l2loopback` bridge so V4L2-only apps (Discord/Slack/Zoom) see the PipeWire node. Cost: extra bridge layer; adoption on consumer side incomplete.
- egr-mf-dshowbridge — legacy-consumer compat: DShow source filter + fake `DevicePath` HKLM reg so picky consumers (Skype) accept the vcam; 32/64-bit arch must match consumer. Cost: regsvr32 + DevicePath gotcha.

## local-only fallback + test-src (framework in-process synthetic surface)
- egr-localonly — `cap.egress.publish.local_only_fallback`: framework vends frames to an in-process surface even where an OS publish path exists (macos/win32/linux-v4l2/linux-pipewire). Cost: in-process consumers only; not OS-wide. Fallback target for the publish caps' degrade.
- egr-localonly-ios — iOS has no public virtual-camera publish API; local-only is the only egress — in-process surface (e.g. `AVSampleBufferDisplayLayer`/app-internal pipe). Cost: in-process only. Fallback: none beyond local.
- egr-localonly-android — ordinary apps cannot register an OS-wide vcam; framework synthetic source consumed in-process. Cost: in-process only.
- egr-localonly-libcamera — libcamera is capture-only; publishing a shareable node needs a separate layer (PipeWire/v4l2loopback, different axes). Local fallback = in-process synthetic. Cost: in-process only; OS publish lives on linux+pipewire/linux+v4l2.
- egr-web-trackgen — `cap.egress.publish.local_only_fallback` native on web: `MediaStreamTrackGenerator`/`canvas.captureStream` build an in-page `MediaStreamTrack` from app frames. Cost: IN-PAGE only (same document); not OS-visible.
- egr-testsrc-emulate — framework in-process synthetic provider/feed where no dedicated test-source API exists (macos/ios/android/v4l2/libcamera). Cost: framework-implemented data plane, no OS test-source object.
- egr-testsrc-pw — `pw_stream` produce node is itself the in-process synthetic provider, fed + consumed in the same graph. Cost: none.
- egr-testsrc-mf — same custom-media-source machinery consumed in-process (Frame Server custom source). Cost: none.
- egr-testsrc-web — `MediaStreamTrackGenerator.writable<VideoFrame>`/`requestFrame` (+ `captureStream`) is the in-page synthetic source. Cost: Chromium-only for track-generator; in-page.
- egr-testsrc-genicam — GenTL Producer presenting a synthetic software/replay Device module. `[CEILING]` genicam-only. Cost: provider-plane, not a consumer axis.
- egr-testpattern-emulate — framework-drawn synthetic test pattern into the in-process source (no device test-pattern API on this axis). Cost: framework data plane.
- egr-testpattern-v4l2 — device/loopback test pattern (`vivid`/`TIMESTAMP_COPY` loopback crumb). Cost: none.
- egr-testpattern-libcamera — `draft::TestPatternMode` device test pattern. Cost: none.
- egr-testpattern-genicam — `TestPayloadFormatMode` device synthetic payload. `[CEILING]` genicam-only. Cost: provider-plane.
- egr-manualclock-avf — frame-on-demand via `customClockConfiguration` sink variant. Cost: sysext-resident.
- egr-manualclock-pw — manual timing via `pw_stream_trigger_process`. Cost: none.
- egr-manualclock-web — `captureStream(0)` + `requestFrame()` drives frames manually. Cost: in-page.
- egr-manualclock-emulate — framework drives synthetic-source timing (no manual-clock API on this axis). Cost: framework data plane.

## install ceiling (native only on the matching axis)
- egr-install-systemext — `cap.egress.install.systemext` native on macos: `OSSystemExtensionRequest` + sysext entitlements + `CMIOExtensionMachServiceName`, Developer-ID + notarization. Cost: signed+notarized sysext w/ user approval.
- egr-install-comadmin — `cap.egress.install.com_admin` native on win32: HKLM CLSID registration (admin) + regsvr32 for DShow. Cost: admin install.
- egr-install-kmod — `cap.egress.install.kernel_module` native on linux+v4l2: v4l2loopback modprobe/DKMS. Cost: out-of-tree kernel module (not mainline).
- egr-install-userspace — `cap.egress.install.userspace_node` native on linux+pipewire: pure `pw_stream` node, no driver install. Cost: none (lowest install ceiling).
- egr-install-privileged — `cap.egress.install.privileged_denied` native-as-a-fact on android: `VirtualDeviceManager.createVirtualCamera` is `@SystemApi`+CDM; DeviceAsWebcam is system-image-only. The fact is real; the publish caps it gates deny → local-only.
- egr-install-mismatch — install-ceiling cap on an axis whose publish path uses a different install mechanism. deny. Fallback: that axis's own install cap.
- egr-scope-virtualdevice — `cap.egress.scope.virtual_device_only` native-as-a-fact on android: `VirtualCamera` is visible only within the virtual device's context (`getDeviceId`/`POLICY_TYPE_CAMERA`), never to default-device apps. Cost: privileged path; scoped visibility.
- egr-scope-mismatch — scope-virtual-device cap on a non-android axis (publish is OS-wide, not virtual-device-scoped). deny. Fallback: none.

## deny — no publish/feature surface on this axis
- egr-noios — iOS+AVFoundation has NO public virtual-camera publish API (iPadOS only consumes external USB cams; ReplayKit is a different domain). Fallback: `cap.egress.publish.local_only_fallback`.
- egr-privileged — android publish (camera2ndk/camerax) is gated to system/privileged apps (`@SystemApi`+CDM, or system-image DeviceAsWebcam); ordinary apps cannot register an OS-wide vcam. Fallback: `cap.egress.publish.local_only_fallback`. The gate itself is the fact `cap.egress.install.privileged_denied`.
- egr-webinpage — web produced streams (`canvas.captureStream`/`MediaStreamTrackGenerator`) are IN-PAGE only; no API registers an OS-wide camera. Fallback: `cap.egress.publish.local_only_fallback`.
- egr-libcamera-nopublish — libcamera is capture-only; no publish/sink/loopback API in `namespace libcamera`. Fallback: `cap.egress.publish.local_only_fallback` (OS publish lives on linux+pipewire/linux+v4l2, different axes).
- egr-uvc-ingest — UVC-direct is an INGEST axis; being a UVC gadget (configfs gadget driver / DeviceAsWebcam) is system-image-level, not a generic app publish path. Fallback: none.
- egr-genicam-ingest — GenICam is an ingest stack; no app→OS "register as a camera" path. Fallback: none (test-pattern/provider live on the provider-plane `[CEILING]`).
- egr-testsrc-genicam-nofeed — `cap.testsrc.frame_feed` on genicam: the GenTL synthetic device emits a device-side `TestPayloadFormatMode` pattern, not an app-fed frame stream; no app→device frame-feed path. Fallback: none.
- egr-v4l2-nopull — v4l2loopback OUTPUT is push-only; no consumer-demand-driven pull model. Fallback: `cap.egress.publish.frame_push`.
- egr-mf-nopull — MF custom media source is push-driven (no consumer-pull contract). Fallback: `cap.egress.publish.frame_push`.
- egr-v4l2-noattachevent — v4l2loopback surfaces no consumer attach/detach event to the producer (open-count is not eventful). Fallback: none.
- egr-v4l2-noidentity — v4l2loopback gives the producer no per-consumer pid/identity. Fallback: none.
- egr-pw-noidentity — no per-consumer pid/signing-id surfaced to the producing PipeWire node. Fallback: none.
- egr-mf-noidentity — MF Frame Server does not surface per-consumer identity to the custom source. Fallback: none.
- egr-v4l2-nocontrolexpose — v4l2loopback synthesizes its own controls; no app-defined control exposure on the node. Fallback: none.
- egr-avf-noconfigdialog — CMIOExtension has no per-consumer config-dialog surface (mf-only via DShow). Fallback: none.
- egr-v4l2-noconfigdialog — no per-consumer config-UI surface on a v4l2loopback node. Fallback: none.
- egr-pw-noconfigdialog — no per-consumer config-UI surface on a PipeWire node. Fallback: none.
- egr-avf-nosharemode — CMIOExtension has no controlling-vs-sharing arbitration knob (mf-only). Fallback: none.
- egr-v4l2-nosharemode — no share-mode arbitration on a v4l2loopback node. Fallback: none.
- egr-pw-nosharemode — no controlling-vs-sharing arbitration on a PipeWire node. Fallback: none.
- egr-v4l2-noflowctl — no sink-stream flow-control/buffering knobs on v4l2loopback (avf-only). Fallback: none.
- egr-pw-noflowctl — no producer-side flow-control knobs exposed on a PipeWire node (avf-only). Fallback: none.
- egr-mf-noflowctl — no sink-stream flow-control knobs on the MF custom source (avf-only). Fallback: none.
- egr-avf-nowrapphysical — CMIOExtension cannot wrap/arbitrate a physical camera (mf-only). Fallback: none.
- egr-v4l2-nowrapphysical — v4l2loopback does not wrap/arbitrate a physical camera (mf-only). Fallback: none.
- egr-pw-nowrapphysical — a PipeWire publish node does not wrap a physical camera (mf-only). Fallback: none.
- egr-v4l2-noinusefeedback — no producer-side in-use/invalidate feedback on v4l2loopback. Fallback: none.
- egr-v4l2-nomicassoc — no published-mic/audio association on a v4l2loopback video node. Fallback: none.
- egr-pw-nomicassoc — no cam↔mic association primitive on a PipeWire publish node. Fallback: none.
- egr-avf-nolegacybridge — macOS CMIOExtension is itself the modern+legacy path; no separate legacy-consumer bridge. Fallback: none.
- egr-pw-nosystemlifetime — a `pw_stream` node is session-bound to `pw_core`; no reboot-persistent OS-wide node. Fallback: `cap.egress.lifetime.session`.

## ? (unsure — research-scheduled, see vocab §14d)
- egr-pw-controls-unknown — `cap.egress.controls.expose` on pipewire: no standardized portal/PipeWire mechanism to expose app-defined controls on a virtual camera node; would be ad-hoc `SPA_PARAM_PropInfo` custom props. `?`
- egr-avf-micassoc-unknown — `cap.egress.mic_association` on avf: CMIOExtension audio-stream pairing with the published cam is unconfirmed. `?`
- egr-mf-micassoc-unknown — `cap.egress.mic_association` on mf: vcam audio-companion association is unconfirmed. `?`

## os
# 40-notes-os — OS integration matrix notes

## consent — native paths
- os-avfconsent — AVFoundation `authorizationStatusForMediaType:`/`requestAccessForMediaType:completionHandler:`; `AVAuthorizationStatus` NotDetermined/Restricted/Denied/Authorized. macos+ios both. Cost: none.
- os-androidconsent — Android runtime `android.permission.CAMERA` (dangerous): `Activity.requestPermissions`; status via `checkSelfPermission`/`AppOpsManager.OPSTR_CAMERA`. Android-platform path — both camera2ndk and camerax route through it. Cost: none.
- os-portalconsent — xdg-desktop-portal `AccessCamera(handle_token)` → user prompt → grant → `OpenPipeWireRemote()→fd`. PipeWire is the SOLE Linux runtime-consent path (sandbox-crossing fd). Cost: portal+session-manager present; no grant ⇒ no fd ⇒ no nodes.
- os-mfconsent — WinRT `AppCapability.Create("Webcam").CheckAccess()`→`AppCapabilityAccessStatus`; any-app fallback `E_ACCESSDENIED` on denied open. Cost: packaged path for prompt; desktop apps get only the deny-on-open signal.
- os-webconsent — first `getUserMedia()` triggers the UA permission prompt; grant persists per-origin (UA policy). Cost: none.
- os-webpermquery — `navigator.permissions.query({name:"camera"})`→`PermissionStatus{state, change event}` covers both status_query and status_change_event. Cost: Chromium full; Firefox rejects `"camera"` query; Safari partial.
- os-mfpolicygate — Group Policy "Let Windows apps access the camera" (Force Allow/Deny) + HKLM ConsentStore Value. Cost: admin/registry; not app-callable.
- os-webpolicygate — `Permissions-Policy: camera=` HTTP header + `<iframe allow="camera">` gate the document/frame. Cost: none (header/attribute authoring).
- os-androidrationale — `shouldShowRequestPermissionRationale(String)`; Android-platform, both columns. Cost: none.
- os-staticpkg — static packaging declaration baked into the bundle/manifest at build time (Info.plist `NSCameraUsageDescription`, sandbox entitlement `com.apple.security.device.camera`, Android `<uses-permission CAMERA>`/`<uses-feature camera*>`/`FOREGROUND_SERVICE_CAMERA`+`foregroundServiceType`, MF manifest `DeviceCapability webcam`). Native on the axis that mandates it; the build system emits it. Cost: none beyond authoring the manifest.

## consent — deny
- os-noperm-v4l2 — v4l2 has NO permission/consent model of its own; access is POSIX `/dev/videoN` perms (group `video`/logind uaccess). Every runtime-consent cap denies here. Fallback: linux+pipewire (portal) is the consent axis.
- os-noperm-libcamera — libcamera has NO consent concept; raw `Camera::acquire()`. The portal/PipeWire layer sits above it. Fallback: linux+pipewire (portal).
- os-genicamnoconsent — industrial GenICam stack has no OS camera-consent model; the device grants/denies via the transport control channel, not a per-app prompt. Fallback: none (host-OS consent axis if the GenTL producer runs sandboxed).
- os-uvcnotstack — raw uvc-direct is not an OS-integration stack; consent/lifecycle/policy live in the host OS, not the USB transfer layer. Fallback: the host-OS axis (avf/v4l2/mf) owns consent for the same physical device.
- os-avfnostatusevent — AVFoundation exposes no authorization-status *change* notification (only one-shot status query/request). Fallback: re-query `authorizationStatusForMediaType:` on resume.
- os-androidnostatusevent — Android has no permission-status change broadcast; revocation surfaces only as the next open/op failing. Fallback: re-check `checkSelfPermission` on resume.
- os-portalnostatusevent — portal records permission per-app but emits no status-change signal to the client. Fallback: re-issue `AccessCamera`.
- os-mfnostatusevent — ConsentStore Value changes raise no app callback. Fallback: re-query `CheckAccess()`.
- os-norationale — only Android surfaces a pre-prompt rationale signal; avf/mf/web/pipewire have no equivalent. Fallback: none (show rationale unconditionally before requesting).
- os-webnodeeplink — getUserMedia has no API to open the browser/OS camera-settings page. Fallback: none.
- os-nousagestring — only Apple mandates a static usage string (`NSCameraUsageDescription`); other platforms declare consent differently. Fallback: none (declared via os-staticpkg on the mandating axis).
- os-nomanifestcap — manifest/appx capability declaration is an Android/MF concept; avf/web/pipewire don't gate via a manifest capability token. Fallback: none.
- os-nosandboxent — sandbox entitlement for camera is an Apple App-Sandbox concept; no equivalent token on other platforms. Fallback: none.
- os-nofeaturedecl — `<uses-feature android.hardware.camera*>` hardware-requirement declaration is Android-only (store filtering). Fallback: none.
- os-nofgservice — foreground-service-type declaration for backgrounded camera is Android-only. Fallback: none.
- os-nopolicygate — org/admin force-allow/deny policy gate exists only on Windows (Group Policy) and web (Permissions-Policy); other axes have no admin policy seam. Fallback: none.
- os-nousagelog — no OS-recorded recent-camera-usage log exposed on this axis. Fallback: none.

## consent — ?
- os-deeplink — open the OS camera-privacy settings page. avf: `UIApplicationOpenSettingsURLString`/macOS `x-apple.systempreferences` `?` exact symbol; android: `Settings.ACTION_APPLICATION_DETAILS_SETTINGS` `?`; mf: `ms-settings:privacy-webcam` via `Launcher.LaunchUriAsync` `?`; pipewire: portal has no settings deeplink — listed `?` pending which axes actually expose it (vocab marks `cap.os.consent.deeplink_settings` `?`). `?`
- os-usagelog — mf ConsentStore "Recent activity" 7-day usage log; readable via registry but no documented app API — `?` whether to expose. `?`

## session lifecycle — native paths
- os-avfsession — `AVCaptureSession startRunning`/`stopRunning`. Cost: none.
- os-avfrunstate — `AVCaptureSession.isRunning` (KVO) + `DidStart/StopRunningNotification`. Cost: none.
- os-androidsession — camera2 NDK `ACameraCaptureSession` create + `setRepeatingRequest`/`stopRepeating`/close to start/stop streaming. Cost: none.
- os-cxsession — CameraX `ProcessCameraProvider.bindToLifecycle(...)`/`unbindAll()`; lifecycle-aware (camera follows the bound `LifecycleOwner`). Cost: none.
- os-v4l2session — `VIDIOC_STREAMON`/`VIDIOC_STREAMOFF` on the queue. Cost: none.
- os-libcamerasession — `Camera::start()`/`stop()` + request queue. Cost: none.
- os-pwsession — `pw_stream_connect`/disconnect + `pw_stream_set_active`. Cost: none.
- os-mfsession — `MediaCapture.InitializeAsync` + source-reader/sink start; WinRT first-call on STA thread. Cost: STA-thread constraint on the WinRT path.
- os-websession — `getUserMedia()` resolves a live `MediaStream`; `track.stop()` stops. Cost: none.
- os-uvcsession — UVC `VS_COMMIT_CONTROL` (probe/commit) + start/stop streaming via the host transfer layer. Cost: none beyond the host USB stack.
- os-genicamsession — SFNC `AcquisitionStart`/`AcquisitionStop` commands. Cost: none.
- os-pwstreamstate — `pw_stream_get_state` / state-changed callback (`STREAMING`/`PAUSED`/`ERROR`). Cost: none.
- os-mfstreamstate — WinRT `CameraStreamStateChanged`/`MediaCapture` state; MF source events. Cost: none.
- os-webreadystate — `MediaStreamTrack.readyState` (`live`/`ended`) + active-track inspection. Cost: none.
- os-genicamacqstatus — SFNC `AcquisitionStatusSelector`+`AcquisitionStatus` reports whether acquisition is running. Cost: none.

## session lifecycle — emulate
- os-runstatetrack — no explicit running-state flag on the axis; the framework tracks its own start/stop boolean around `STREAMON`/`acquire`/repeating-request. Covers camera2ndk, camerax, v4l2, libcamera, uvc. Cost: trivial bookkeeping; truthful only to the framework's own calls, not external state.

## hotplug — native paths
- os-avfhotplug — AVFoundation device-discovery: `AVCaptureDeviceDiscoverySession` KVO on `.devices` / device-was-connected/disconnected notifications. macos+ios. Cost: none.
- os-androidhotplug — `CameraManager.AvailabilityCallback.onCameraAvailable`; Android-platform, both columns. Cost: none.
- os-androidremoval — `onCameraUnavailable` + in-session `StateCallback.onDisconnected`. Cost: none.
- os-libcamerahotplug — `CameraManager` `cameraAdded`/`cameraRemoved` signals. Cost: none.
- os-pwhotplug — `pw_registry` `global`/`global_remove` on the portal fd. Cost: none.
- os-mfhotplug — `DeviceWatcher`/`MFCreateDeviceSourceActivate` enumeration + WM_DEVICECHANGE; arrival+removal. Cost: none.
- os-webdevicechange — `navigator.mediaDevices.devicechange` event (arrival). Cost: labels/ids hidden until a grant exists.
- os-webtrackended — `MediaStreamTrack` `ended` event + `readyState` for removal of the in-use camera; also the only web preemption surrogate. Cost: signals only the in-use device's loss, not arbitrary removals.
- os-uvchotplug — USB attach/detach via the host (libusb hotplug / IOKit USB notification / udev). Cost: relies on the host USB stack, not a camera API.
- os-genicamhotplug — GenTL `EVENT_*`/interface update callback on device list change (GigE discovery / U3V enumerate). Cost: producer-dependent.

## hotplug — emulate
- os-udevhotplug — v4l2 has no in-API hotplug event; arrival/removal is inferred from udev (`/dev/videoN` add/remove) or polling the device list. Cost: needs a udev monitor or poll loop outside V4L2 proper.

## arbitration — native paths
- os-avfexclusive — AVFoundation opening a device input is exclusive by default; contention surfaces as `AVErrorDeviceInUseByAnotherApplication`. Cost: none.
- os-androidexclusive — camera2 `openCamera` is single-holder; eviction via priority. Android-platform, both columns. Cost: none.
- os-v4l2ebusy — `open()`→`EBUSY` when a node is exclusively held; also the contention-error signal. Cost: error-on-open only, no pre-query.
- os-libcameraacquire — `Camera::acquire()`/`release()` exclusive; contention fails loudly (also the contention-error signal). Cost: no multiplexing on raw libcamera.
- os-mfsharemode — `MF_DEVSOURCE_ATTRIBUTE_FRAMESERVER_SHARE_MODE` 0=controlling (one holder, full control) / 1=sharing (read-only multi-reader via Frame Server). Covers both exclusive and shared open. Cost: none.
- os-uvcexclusive — host USB interface claim is exclusive (single process holds the streaming interface). Cost: device/OS-version-dependent coexistence with the OS capture stack.
- os-genicamccp — GigE `GevCCP` (control-channel privilege: open/exclusive/control+switchover); the device itself grants/denies. Cost: GigE transport; switchover key for control handoff.
- os-pwmultiplex — PipeWire/WirePlumber multiplexes one camera across multiple readers (shared open). Cost: session-manager mediated; no explicit per-client exclusivity API.
- os-genicamreadonly — `DeviceAccessStatus` ReadOnly/OpenReadOnly lets a monitor open the device read-only alongside the controlling app. Cost: control privilege stays with the primary app.
- os-avfinuse — `AVCaptureDevice.isInUseByAnotherApplication`. macOS/macCatalyst only. Cost: macOS-only accessor.
- os-genicamaccessstatus — GenTL `DEVICE_ACCESS_STATUS` (ReadWrite/ReadOnly/NoAccess/Busy) for in-use query, contention signal, and preemption (status flips to Busy/NoAccess when another primary takes control). Cost: GenTL info command; exact spelling `?` (DEVICE_ACCESS_STATUS, not a guaranteed SFNC node).
- os-mfaccessdenied — `E_ACCESSDENIED`/`MF_E_*` on open when another controlling instance holds the device. Cost: error-on-open.
- os-avfcontend — `AVErrorDeviceInUseByAnotherApplication` (-11815) signals open/use failed because another client holds the device. macos+ios. Cost: error code, not a pre-query.
- os-webnotreadable — `getUserMedia` `NotReadableError` = hardware/OS busy (another app holds the camera); the only web arbitration signal. Cost: coarse — no in-use pre-query, no preemption.
- os-uvccontend — host returns an in-use/claim error when the streaming interface is already held. Cost: error-on-open only.
- os-androidcontend — `StateCallback.onError` `ERROR_CAMERA_IN_USE`(1)/`ERROR_MAX_CAMERAS_IN_USE`(2); NDK `onError`. Cost: none.
- os-v4l2priority — `VIDIOC_G/S_PRIORITY` (`UNSET`/`BACKGROUND`/`INTERACTIVE`/`RECORD`) cooperatively gates who may change format/controls. Cost: cooperative only, no preemption.
- os-genicamswitchover — `GevPrimaryApplicationSwitchoverKey` sets control-priority for primary-app switchover. Cost: GigE-only.
- os-avfpreempt — `VideoDeviceInUseByAnotherClient` interruption reason signals this client was evicted (iOS arbitration model surfaced on both via the interruption notification). Cost: arrives as an interruption, not a dedicated preemption callback.
- os-androidpreempt — `StateCallback.onDisconnected` fires when evicted by a higher-priority client; NDK `onDisconnected`. Cost: none.
- os-androidpriochange — `AvailabilityCallback.onCameraAccessPrioritiesChanged()` (API 34). Android-platform, both columns. Cost: API 34+.

## arbitration — emulate
- os-v4l2trialopen — no in-use pre-query; emulate by a non-blocking trial `open()` and reading `EBUSY` vs success, then closing. Cost: a probe open may itself perturb a cooperating client.
- os-libcameratrialacquire — emulate in-use query via a trial `acquire()`/`release()`. Cost: a probe acquire can race a real client.
- os-mftrialopen — emulate in-use query via a trial controlling-mode activate that fails with `E_ACCESSDENIED`. Cost: probe activation side-effects.
- os-uvctrialopen — emulate in-use query via a trial interface claim. Cost: probe claim side-effects.

## arbitration — deny
- os-avfnoshared — AVFoundation has no shared/read-only multi-reader open; device input is exclusive. Fallback: none (use a virtual-camera/CMIO extension to fan out).
- os-androidnoshared — camera2/CameraX expose no shared read-only open (concurrent is front+back, not co-readers of one sensor). Fallback: none.
- os-v4l2noshared — V4L2 has no shared-session concept; one exclusive holder. Fallback: linux+pipewire multiplex.
- os-libcameranoshared — raw libcamera does not multiplex. Fallback: go through PipeWire.
- os-webnoshared — getUserMedia gives no shared-open control; the UA decides device sharing. Fallback: none.
- os-uvcnoshared — a claimed UVC streaming interface is single-holder. Fallback: none.
- os-avfinuseiosna — `isInUseByAnotherApplication` is macOS/macCatalyst-only — unavailable on iOS. Fallback: infer from interruption reason `VideoDeviceInUseByAnotherClient`.
- os-androidnoinuse — no in-use pre-query before open; you learn at open via `ERROR_CAMERA_IN_USE`. Fallback: attempt open and read the error.
- os-pwnoinuse — portal/PipeWire surfaces no per-device in-use query (session-manager owns it). Fallback: none.
- os-webnoinuse — no device-in-use pre-query; only `NotReadableError` at open. Fallback: attempt getUserMedia and read the error.
- os-pwnoexclusive — PipeWire's model is multiplex/shared; no client-facing exclusive-lock primitive. Fallback: shared open is the only mode.
- os-pwnocontend — PipeWire has no explicit contention error; a busy single-open backend surfaces as a stream error from the backend, not a portal signal. Fallback: read the backend/stream error.
- os-portalnopolicygate — the portal records per-app consent but exposes no org/admin force-allow/deny policy gate to the client. Fallback: none (admin policy, if any, lives in the portal backend config, not app-callable).
- os-webnoexclusive — getUserMedia gives the app no exclusive-open control; the UA owns device arbitration, so an app cannot request exclusivity. Fallback: none (the UA may share or refuse via `NotReadableError`).
- os-nopriohint — no cooperative client-priority hint on this axis (only v4l2 S_PRIORITY and genicam switchover-key expose one). Fallback: none.
- os-androidautoprio — camera2 priority is OS-assigned by foreground/top state; no app-set priority hint. Fallback: none (priority follows process lifecycle).
- os-nopreempt — no preemption/eviction notification on this axis. Fallback: detect loss via session-stop/stream-error.
- os-nopriochange — no access-priority-ranking-changed event on this axis (Android-only). Fallback: none.

## interruption / background / mute lifecycle — native paths
- os-avfinterrupt — iOS `AVCaptureSessionWasInterruptedNotification` + `isInterrupted` (KVO); interruption_ended via `InterruptionEndedNotification`. Cost: none on iOS.
- os-avfreason — iOS `AVCaptureSessionInterruptionReasonKey`→`AVCaptureSessionInterruptionReason` (InBackground/AudioDeviceInUse/MultipleForegroundApps/SystemPressure/VideoDeviceInUseByAnotherClient). Cost: iOS-only userInfo key.
- os-mfinterrupt — WinRT `MediaCapture.Failed`/`CameraStreamStateChanged` signal interruption + recovery. Cost: coarse — no structured reason code.
- os-webmute — `MediaStreamTrack.muted` (readonly) + `mute`/`unmute` events: UA/OS-driven track mute is the web surrogate for interruption start/end and the OS privacy-toggle mute. Cost: a mute, not a session teardown; no reason code.
- os-avfbgblock — iOS `VideoDeviceNotAvailableInBackground` (camera blocked while backgrounded). Cost: iOS policy.
- os-androidbgblock — background camera open is blocked without `FOREGROUND_SERVICE_CAMERA`+`foregroundServiceType="camera"`. Android-platform, both columns. Cost: requires the FGS declaration.
- os-webvisibility — Page Visibility (`document.hidden`) — UAs may mute/throttle camera when the page is backgrounded. Cost: UA-dependent; throttle/mute, not a hard block.

## interruption / background / mute — deny
- os-nointerrupt — v4l2/libcamera/pipewire/uvc/genicam have no session-interruption notion (no OS lifecycle layer). Fallback: detect via stream error / frame stoppage.
- os-noreasoncode — axis signals interruption but carries no structured reason enum (mf/android/web). Fallback: none (reason unknowable).
- os-avfreasonmacosna — on macOS the interruption-reason userInfo keys are `API_UNAVAILABLE(macos)` — the whole reason enum is an iOS arbitration model. Fallback: none on macOS.
- os-nobgblock — no OS background-camera block policy on this axis (desktop/industrial run unattended). Fallback: none.
- os-macosnobgblock — macOS has no iOS-style background-camera block (apps keep the camera when not frontmost). Fallback: none (treat as always-permitted).
- os-noosmute — no OS-forced privacy-toggle stream-mute event on this axis (only the web `MediaStreamTrack.muted` surfaces one). Fallback: detect via frame stoppage / privacy-toggle query.
- os-avfnoosmute — AVFoundation has no OS-mute *event*; the iOS privacy state surfaces only as an interruption. Fallback: use interruption notifications.
- os-androidnoosmute — Android's SensorPrivacyManager toggle reports cameras unavailable (open fails / `onCameraUnavailable`) rather than emitting a live-stream mute event. Fallback: handle as unavailability/disconnect.
- os-nomultitask — multitask camera-access (camera while sharing screen with other fg apps) is an iPad-only concept; no equivalent elsewhere. Fallback: none.
- os-multitaskmacosna — `isMultitaskingCameraAccessEnabled` is an iPad concept, not applicable on macOS. Fallback: none.

## interruption — emulate
- os-androidinterrupt — camera2/CameraX have no first-class interrupt/resume notification; the framework synthesizes interruption from `onDisconnected`/`onError` + app lifecycle (background) and resume from re-open. Cost: composed signal, not an OS interruption object.

## interruption / lifecycle — ?
- os-avfinterruptmacos — on macOS the interruption notifications exist but the reason model is iOS-shaped and the macOS interruption semantics diverge (no background/multitasking interruptions); whether to surface a macOS interruption at all is unresolved. `?`
- os-multitask — iOS `AVCaptureSession.isMultitaskingCameraAccessEnabled` — flagged `?` in inventory + vocab (research-scheduled). `?`

## orientation — native paths
- os-androidsensororient — `ACAMERA_SENSOR_ORIENTATION` (0/90/180/270 cw vs natural). Android-platform, both columns. Cost: none.
- os-v4l2sensororient — read-only `V4L2_CID_CAMERA_ORIENTATION` + `V4L2_CID_CAMERA_SENSOR_ROTATION`. Cost: none.
- os-libcamerarotation — `properties::Rotation` static mount orientation. Cost: none.
- os-avfrotcoord — `AVCaptureDeviceRotationCoordinator` (`.videoRotationAngleForHorizonLevelPreview`/`Capture`, KVO) computes capture/preview rotation vs UI. macos(14)+ios(17). Cost: OS-version floor.
- os-androiddisplayrot — `Surface.ROTATION_0/_90/_180/_270` + sensor-orientation math for the display transform. Both columns. Cost: app computes the composite angle.
- os-mfrothelper — `CameraRotationHelper` / `MF_MT_VIDEO_ROTATION` for display-relative rotation. Cost: none.
- os-avfrotapply — `AVCaptureConnection.videoRotationAngle`/`isVideoRotationAngleSupported:` applies rotation to delivered frames. Cost: none.
- os-cxrotapply — CameraX `setTargetRotation`/`UseCase.setTargetRotation` applies output rotation declaratively. Cost: none.
- os-mfrotapply — `MF_MT_VIDEO_ROTATION` on media type / `IMFVideoProcessorControl::SetRotation`. Cost: none.
- os-libcameraorient — `CameraConfiguration::orientation` (EXIF-274 `Orientation` enum) applies rotation+mirror at configure time; covers output rotation and front mirror. Cost: set at configure, not per-frame live.
- os-pwvideotransform — `SPA_META_VideoTransform` per-buffer rotation/flip (if the backend sets it). Cost: backend-dependent; echo, not a control knob.
- os-avfmirror — `AVCaptureConnection.isVideoMirrored`/`automaticallyAdjustsVideoMirroring`. Cost: none.
- os-cxmirror — CameraX `MirrorMode.MIRROR_MODE_OFF/ON/ON_FRONT_ONLY` on the use case. Cost: none.
- os-v4l2flip — `V4L2_CID_HFLIP`/`VFLIP` manual mirror. Cost: manual; no front-cam auto convention.
- os-mfmirror — `IMFVideoProcessorControl::SetMirror` / WinRT `MediaCapture.SetPreviewMirroring`. Cost: none.
- os-genicamreverse — SFNC `ReverseX`/`ReverseY` sensor-readout mirror. Cost: readout-level flip, not a front-cam convention.
- os-avffacing — `AVCaptureDevice.position` (Front/Back/Unspecified) gives the coarse facing hint. Cost: none.
- os-androidfacing — `LENS_FACING_FRONT/BACK/EXTERNAL`; CameraX `CameraSelector.DEFAULT_FRONT/BACK`. Both columns. Cost: none.
- os-libcamerafacing — `properties::Location` (Front/Back/External). Cost: none.
- os-mffacing — WinRT `EnclosureLocation.Panel` (Front/Back) facing. Cost: enclosure metadata may be absent on external cams.
- os-webfacing — `facingMode` (`user`/`environment`) — the ONLY web orientation hint. Cost: coarse; no geometry, no sensor-mount/display-rotation API.

## orientation — emulate
- os-rotapplygpu — no device/driver rotation+mirror knob on the axis; apply the transform in the framework's GPU/CPU output stage. Covers v4l2 (where flip absent for arbitrary rotate), pipewire, web, uvc, genicam front_mirror, and the all-axes output-rotation echo per `cap.os.orientation.output_rotation_apply` overlap with §10. Cost: one extra sample/blit; orientation must ride in frame metadata for downstream consumers.
- os-androidrotapply — camera2 NDK has no live output-rotation knob (rotation is a Surface/consumer transform); emulate via the output Surface transform or GPU stage. Cost: consumer-side transform, not in-pipeline.
- os-androidmirroremulate — camera2 has NO mirror key (front mirroring is an app-applied display convention); emulate in the GPU/output stage. Cost: app-side flip; CameraX exposes `MirrorMode` natively (see os-cxmirror).

## orientation — deny
- os-avfnosensormount — AVFoundation exposes no fixed sensor-mount-orientation accessor (it folds mount into the rotation coordinator). Fallback: `cap.os.orientation.display_rotation` (os-avfrotcoord) already nets the applied angle.
- os-pwnosensormount — portal/PipeWire surfaces no static sensor-mount property. Fallback: none.
- os-mfnosensormount — MF has no fixed sensor-mount-orientation readout. Fallback: none.
- os-webnosensormount — web has no sensor-mount/rotation API. Fallback: `facingMode` only.
- os-uvcnosensormount — UVC has no sensor-mount-orientation descriptor. Fallback: none.
- os-genicamnosensormount — GenICam has no UI/natural-orientation mount concept (fixed-mount industrial). Fallback: none.
- os-nodisplayrot — no UI-display-rotation computation API on this axis (v4l2/libcamera/pipewire/uvc/genicam — no UI layer). Fallback: app supplies the UI orientation and uses os-rotapplygpu.
- os-webnodisplayrot — no camera-relative display-rotation API; `screen.orientation` is device, not camera. Fallback: none.
- os-nofacing — no front/back facing hint on this axis (v4l2 unless ORIENTATION CID, pipewire, uvc, genicam). Fallback: none (industrial/USB cams have no panel facing).

## privacy indicator / toggle — native paths
- os-androidsensorprivacy — `SensorPrivacyManager` (observe-only) reports the Android-12 in-use dot / toggle state; Android-platform, both columns. Cost: `@SystemApi`-shaped; apps observe, cannot flip.
- os-androidsensortoggle — `SensorPrivacyManager.supportsSensorToggle`/state for the HW/SW kill-switch. Both columns. Cost: observe-only.
- os-v4l2privacy — `V4L2_CID_PRIVACY` boolean (firmware privacy shutter/kill) blocks acquisition. Cost: only where firmware exposes it.
- os-uvcprivacyctrl — UVC `CT_PRIVACY` control (0x11) via the raw transfer layer. Cost: device firmware must implement it.
- os-mfforcesw — force a software privacy indicator where no HW LED exists: HKLM `NoPhysicalCameraLED=1`. Cost: registry/OEM, Windows-only; not app-callable at runtime.
- os-genicamindicatormode — SFNC `DeviceIndicatorMode` (Active/Inactive/ErrorStatus) configures the on-device status LED. `[CEILING]` — native genicam only. Cost: genicam-only.

## privacy indicator / toggle — deny
- os-noindicatorread — the system in-use privacy indicator (LED/green dot) is OS-managed and NOT readable from the app on this axis (avf/ios/v4l2/libcamera/pipewire/mf/web/uvc/genicam-consumer). Fallback: none — read the toggle state where available (os-*sensortoggle/privacy) instead.
- os-noforcesw — no force-software-indicator hook on this axis (Windows-only registry knob). Fallback: none.
- os-notogglequery — no OS kill-switch/privacy-toggle state query on this axis. Fallback: detect via acquisition failure / track mute.
- os-ledceiling — `cap.os.privacy.indicator_led_mode` is `[CEILING]`: no consumer-OS axis configures an on-device status LED. Denies on every consumer axis; native only on genicam (os-genicamindicatormode). Fallback: none on consumer axes.

## shutter sound — native paths
- os-androidmediaaction — `MediaActionSound` (`SHUTTER_CLICK`/`FOCUS_COMPLETE`/`START/STOP_VIDEO_RECORDING`), `load`/`play`. Android-platform, both columns. Cost: none.

## shutter sound — deny
- os-noshuttersound — no standard system shutter-sound API on this axis (macos/linux/pipewire/mf/web/uvc/genicam). Fallback: play a bundled sound asset via the app's audio path.
- os-iosshuttersystem — iOS plays the system shutter sound itself for `AVCapturePhotoOutput` capture (regionally mandatory, not an app-callable play API). Fallback: none — the system owns it.
- os-androidnoshutterdisable — camera2/CameraX have NO API to disable the shutter sound (legacy Camera1 `canDisableShutterSound`/`enableShutterSound`; regional enforcement below the API). Fallback: none.
- os-shutterregional — disabling the shutter sound is a regional legal mandate, not a queryable control (avf/mf: none). Fallback: none.

## capture-input / control-surface — native paths
- os-avfcaptureevent — iOS hardware-shutter / volume-button capture via `AVCaptureEventInteraction` (`AVCaptureEvent.phase`) / SwiftUI `onCameraCaptureEvent`. iOS 17.2+, `[>pin]`. Cost: iOS-only.
- os-uvcstatusinterrupt — UVC still-image hardware trigger via the Status-Interrupt endpoint (`[>pin]`). Cost: device must wire a still-trigger button to the interrupt endpoint.
- os-avfcontrolsurface — `AVCaptureControl`/`AVCaptureSlider`/`AVCaptureIndexPicker`/`AVCaptureToggle`/`AVCaptureSystem*Slider` + `addControl:` + `AVCaptureSessionControlsDelegate` bind a dedicated hardware camera-control surface. macos(15)+ios(18), `[>pin]`. Cost: OS-version floor; hardware control surface present.

## capture-input — deny
- os-shutterbtnmacosna — `AVCaptureEventInteraction` is AVKit/iOS-only; macOS has no hardware-shutter/volume-button capture hook. Fallback: none.
- os-androidnoshutterbtn — Android routes volume keys through normal key events, not a camera-capture-event API; no camera2/CameraX hardware-shutter binding. Fallback: handle `KeyEvent` VOLUME keys at the app level.
- os-noshutterbtn — no hardware shutter/volume-button capture event on this axis (linux/pipewire/mf/web/genicam). Fallback: none (genicam hardware trigger is `cap.capture.trigger.hardwareline`, a different cap).
- os-nocontrolsurface — no dedicated hardware camera-control-surface binding on this axis (Apple-only AVCaptureControl). Fallback: none.

## thermal — native paths
- os-avfsystempressure — iOS `AVCaptureDevice.systemPressureState`/`AVCaptureSystemPressureLevel` (Nominal..Shutdown); KVO drives state_event. iOS/Catalyst/tvOS. Cost: iOS family only.
- os-avfpressurefactors — iOS `AVCaptureSystemPressureFactors` (SystemTemperature/PeakPower/DepthModuleTemperature). Cost: iOS-only.
- os-avfpressureinterrupt — iOS `AVCaptureSessionInterruptionSystemPressureStateKey` carries the exact pressure state that forced shutdown. Cost: iOS-only.
- os-androidthermal — `PowerManager.getCurrentThermalStatus()` / NDK `AThermal_getCurrentThermalStatus` (NONE..SHUTDOWN). Android-platform, both columns. Cost: API 29/30+.
- os-androidthermallistener — `addThermalStatusListener` / NDK thermal callback. Both columns. Cost: API 29+.
- os-androidheadroom — `getThermalHeadroom(int)` / NDK `AThermal_getThermalHeadroom` (0..1 forecast). Both columns. Cost: API 30/31+.
- os-genicamtemperature — SFNC `DeviceTemperature` (+`DeviceTemperatureSelector`); state_query native, state_event emulated by polling. Cost: device must expose the temperature node.
- os-mfthrottle — `EXTENDED_FRAMERATE_THROTTLE` / `OPTIMIZATIONHINT` power levers (consumer-native slice of the power-throttle hint). Cost: indirect power lever, not a thermal callback.
- os-uvcthrottlexu — UVC `FRAMERATE_THROTTLE` XU (0x0E) for power/framerate throttle; the `[CEILING]` slice of the throttle hint. Cost: vendor XU; device must implement.

## thermal — emulate
- os-libcamerasensortemp — libcamera exposes only `controls::SensorTemperature` (read-only, no level enum / no callback); emulate state_query + a polled state_event from raw °C, with the framework deriving pressure levels. Cost: framework self-governs thresholds (MEL-ENGINE-VI/III); no OS throttle policy.
- (os-genicamtemperature also used emulate for state_event — polled `DeviceTemperature`, no push event.)

## thermal — deny
- os-thermalmacosna — the entire AVCapture system-pressure family is `API_UNAVAILABLE(macos)` (iOS/Catalyst/tvOS only). Covers macOS thermal state_query/state_event/pressure_factors/pressure_interruption_state. Fallback: none in AVFoundation; macOS thermal would come from IOKit `SMC`/`thermalState` outside the camera API.
- os-nothermal — no thermal-pressure or power-state hook on this axis (v4l2/pipewire/mf/web/uvc thermal-read). Fallback: none — degradation is the driver's silent business.
- os-noheadroom — no normalized thermal-headroom/forecast on this axis (Android-only). Fallback: none.
- os-iosnoheadroom — iOS exposes pressure *level* but no normalized headroom-forecast scalar (that is Android `getThermalHeadroom`). Fallback: derive coarse headroom from `AVCaptureSystemPressureLevel`.
- os-nopressurefactors — no breakdown of which factors drive thermal pressure on this axis (iOS-only `AVCaptureSystemPressureFactors`). Fallback: none.
- os-nopressureinterrupt — no readout of the exact pressure state that forced interruption/shutdown on this axis (iOS-only). Fallback: none.
- os-nothrottlehint — no power/framerate-throttle request hook on this axis (mf `EXTENDED_FRAMERATE_THROTTLE` and uvc `FRAMERATE_THROTTLE` XU are the only ones). Fallback: lower the requested frame rate / resolution via normal controls.


## baresensor
# baresensor — matrix notes (embedded+baresensor + set-B new-cap rows)
> note keys for `40-matrix-baresensor.csv`. `<key> — <cost | fallback cap-ID | ?>`. Plain native carries no key.
> floor = raw-Bayer sensor, no on-chip ISP. `[ISP-SoC lifts]` = an OV2640/OV5640-class SoC sensor with an on-chip ISP makes the cell native; the column stays classified for the floor.

## native-with-key (the cell IS native; key records WHY it is reachable)
- bs-manual — native by direct I²C/SCCB register write (the OS-HAL-gated knob is a raw register here).
- bs-sensorchar — native as a datasheet/chip-ID-register constant the framework carries (no runtime service needed).
- bs-regreadback — native: applied value confirmed by re-reading the sensor's own register (the framework wrote it).
- bs-vcdt — native via CSI-2 Virtual-Channel/Data-Type multiplexing on the one physical link (receiver demux, no OS pin object).
- bs-mono — native where the sensor is mono (IMX296LLR / OV9281 / AR0234-mono): single-channel, no CFA, no demosaic.
- bs-flip — native: HFLIP/VFLIP + mount-rotation are sensor registers (IMX219 flip even shifts Bayer phase).
- bs-firmwaresession — native: the firmware owns the sensor exclusively; start/stop is `video_stream_start/stop` directly, no OS session.
- bs-gpio — native: a GPIO button wired to an interrupt fires the single-shot capture (the "shutter button" is literally a GPIO).
- bs-pool — native: allocator-owned DMA target buffer + cache-coherency management by the MCU.
- bs-testpat — native: the sensor's own `V4L2_CID_TEST_PATTERN` generator (bring-up without optics).

## emulate
- bs-avsync-emu — emulate: framework data-plane logic (shared A/V time-base / rebase / jitter / ts-jump / offset); no sensor or OS primitive but build-able. Same as every axis (vocab §13 emulate-everywhere).

## deny (note = fallback cap-ID or `none`)
- bs-noisp — ISP/tone pipeline absent on the raw floor → fallback none. [ISP-SoC lifts: OV2640/OV5640 on-chip ISP makes contrast/saturation/sharpness/gamma/CCM/color-effect native.]
- bs-noae — auto-exposure (AE mode/lock/region/metering/precapture/state) absent → fallback cap.control.exposure.manual-time.
- bs-no3a — on-chip 3A orchestration / scene / capture-intent / lux absent on the raw floor → fallback the manual siblings (exposure.manual-time, gain.analog-manual). [ISP-SoC lifts on AEC/AGC/AWB-bearing sensors.]
- bs-noawb — auto white-balance + WB-gain registers absent on a raw sensor (host applies WB during demosaic) → fallback none. [ISP-SoC lifts: OV5640/OV2640 on-chip AWB.]
- bs-noaf — no autofocus / lens-position / hunting-state: a bare sensor has no focus actuator (VCM is a separate off-sensor IC) → fallback none. [ISP-SoC lifts: OV5640 AF firmware.]
- bs-noflash — flash mode/strength/preflash/torch/scene-detect/charge-state absent (no OS flash service) → fallback cap.control.flash.strobe-output (the native sensor STROBE pin).
- bs-nozoom — no ratio/ramp/bounds/method zoom path → fallback cap.control.zoom.digital-crop-rect (sensor readout window).
- bs-nomech — no motors on silicon: PTZ / aperture-iris / ND / mechanical-shutter / filter-wheel → fallback none.
- bs-nodepth — single bare 2D sensor produces no depth / disparity / point-cloud / calibration → fallback none (depth = a host-built stereo SYSTEM across two genlocked sensors).
- bs-noeffect — no OS/firmware effect engine (auto-frame / blur / replace / studio-light / eye-contact / reactions / seg-matte) → fallback none.
- bs-nodetect — no on-device detector (face / body / salient / barcode / scene hints) → fallback none (CV/ML is downstream).
- bs-nostats — no ISP 3A statistics map (histogram / lens-shading / hot-pixel / sharpness / OIS) on a bare sensor → fallback none. [ISP-SoC lifts: RkISP/IPU3/DCMIPP stats nodes.]
- bs-nocompphoto — no comp-photo pipeline (Night / HDR-fusion / Portrait / Bokeh / ZSL-ring) → fallback none.
- bs-nozsl — no buffered zero-shutter-lag ring / reprocess / responsive shutter → fallback cap.capture.still.
- bs-noseq — no hardware bracket/burst/sequencer object on these sensors → fallback none (app may re-write exposure between frames manually).
- bs-noenc — no on-board video/still encoder (H.264 / HEVC / MJPEG / EXIF blob) → fallback none.
- bs-nojpeg — no hardware JPEG on the raw floor → fallback none. [ISP-SoC-with-HW-JPEG lifts: OV2640/OV5640 emit a JPEG bitstream.]
- bs-noegress — no OS, hence no virtual-camera publish / consumer-attach / install path (the framework is a pure source) → fallback none.
- bs-noos — no OS surface at all (consent / lifecycle / arbitration / privacy-indicator / shutter-sound / wallclock / opaque-private / mic-association / raw_under_os) → fallback none.
- bs-nomulti — no multi-cam session manager / concurrent-set / cost-budget / logical-fused device / time-aligned sync (multi-sensor is a board reality via genlock pins, not an API) → fallback none.
- bs-noclass — device-class flags (virtual / external-uvc / capture-card / continuity / deskview / depth-sensor) absent on a single bare sensor → fallback cap.device.class.physical.
- bs-nofeas — pre-open feasibility query (query_closed/open/combination_tables/constraints) absent: identity is a chip-ID probe + static board wiring → fallback cap.enum.caps.per_device.
- bs-nozerocopy — OS-surface zero-copy handoff (IOSurface / AHardwareBuffer / D3D11 / WebGPU-VideoFrame) absent — no OS graphics stack → fallback cap.frame.map.cpu (+ cap.frame.zerocopy.import_external for app-owned DMA buffers).
- bs-nohotplug — no arrival/removal notification: the sensor is wired at a fixed bus address → fallback cap.device.connected_state (chip-ID probe).
- bs-nofanout — no stream-share/fan-out to multiple consumers → fallback none.
- bs-nousecase — no per-stream use-case role tagging → fallback none.
- bs-noreconfig — live topology/format reconfigure needs a stop/restart on a bare sensor → fallback none.
- bs-nouvc — UVC-specific surface (extension-unit / still-pipe / payload-header / SCR-SOF device-clock) absent — there is no USB UVC layer → fallback none.
- bs-noperframe — no per-request per-frame control-attach / request-id machinery → fallback cap.control.lock.hardware-config.
- bs-nochunk — no GenICam chunk / custom-blob per-frame I/O-state metadata → fallback cap.meta.access.rawblob.
- bs-noimu — frame ts not mappable into a guaranteed motion-sensor clock domain → fallback none.
- bs-notimecode — no SMPTE timecode (RP188/VITC/LTC) → fallback none.
- bs-nogenlock — no SDI/tri-level/house-reference genlock → fallback cap.timing.genlock.sync-pins (the native XVS/XHS pin path).
- bs-noptp — no IEEE-1588/PTP network clock or scheduled-action firing → fallback cap.timing.genlock.sync-pins.
- bs-noevent — no async exposure-end/frame-trigger EVENT object (the GenICam ceiling) → fallback cap.timing.frame-sync-event (VSYNC IRQ).
- bs-ceiling — `[CEILING]` cap with no consumer backing and not a bare-sensor primitive (machine-vision class, linescan scan-type, user-controlled transfer) → fallback none.

## `?` cells (genuine unknowns the inventory flagged — never a guess)
- bs-multiroi-q — ? multi-ROI (≥2 regions) readout: absent as a register surface in the cited drivers; CSI-2 v3.0 SROI is protocol-level, unexposed → ?
- bs-shuttermode-q — ? runtime global-vs-rolling shutter select: shutter type is FIXED per sensor, not a runtime knob; whether any cited part exposes a select is unverified → ?
- bs-hdrdol-q — ? in-sensor DOL/multi-slope HDR readout: IMX477 is DOL-capable but the open driver does not implement it (register sets NDA-gated); libcamera HdrMode hooks are unwired → ?
- bs-userset-q — ? persist whole-sensor config to on-device NVM: some sensors carry OTP/NVM but a UserSet-style save/load surface on these parts is unverified → ?
- bs-dmabuf-q — ? dmabuf export / DRM-modifiers / explicit-sync: reachable only if the SoC has a GPU + DRM stack (SoC-ISP-class targets); on a bare MCU receiver, unknown → ?
- bs-devcounter-q — ? free-running device timestamp counter (read/reset/latch): a GenICam-style counter on a bare sensor is part-dependent, unverified → ?
- bs-embedded-q — ? per-chunk selectable embedded-metadata enable: embedded-data-line reliability is flagged (IMX219 returns junk by default; IMX296/AR0234/OmniVision embedded lines unverified) → ?
- bs-temp-q — ? sensor die-temperature register: present on SOME Sony/onsemi parts, absent on others; no uniform surface → ?

## set-B note keys (the 3 new caps on the 11 existing OS-HAL axes)
- bs-newcap-deny — deny: the OS/transport owns sensor bring-up and the sensor itself; the new bare-metal cap is unreachable under the HAL → fallback none.
- bs-newcap-deny-rawos — deny: register R/W is not exposed; the closest surface is the under-OS raw path → fallback cap.device.access.raw_under_os.
- bs-newcap-deny-flash — deny: no sensor-strobe-pin assertion under this HAL → fallback cap.control.flash.still-mode (or none where absent).
- bs-v4l2-dbgreg — ? v4l2 `VIDIOC_DBG_S/G_REGISTER` exists but is CONFIG_VIDEO_ADV_DEBUG-gated + root + per-driver opt-in (debug ioctl, not production) → ?
- bs-v4l2-flashstrobe — ? v4l2 FLASH-LED class can strobe an illuminator but exposure-window-synced sensor STROBE-pin assertion is partial/driver-dependent → ?

## p6 (stress findings)
- met-synthbracketid — emulate: framework mints a synthetic bracket-ID via `cap.frame.cookie` + per-frame applied-EV echo (OS withholds `REQUESTED_FRAME_SETTING_ID`).
- top-fwk-tsalign — emulate: framework time-aligns outputs via native per-frame timestamps (no OS sync-primitive), cf. `cap.timing.av-sync.*`.
- p6-noios — deny: no virtual-camera / hidden-cam model on iOS; fallback none.
- p6-novcam — deny: no software-vcam ingest (Android VirtualDeviceManager is privileged/scoped); fallback none.
- p6-libcam-novcam — deny: libcamera enumerates real cameras only, not loopback/virtual; fallback via the v4l2/pipewire axis, else none.
- p6-uvc-nostack — deny: uvc-direct is a control-transport device-class, not an enumeration stack; fallback host-OS axis.
- p6-nostack — deny: not an OS enumeration stack; fallback none.
- p6-noos — deny: no OS (bare-metal); fallback none.
- p6-nohidden — deny: no app-level reveal of hidden cams; fallback none.
- p6-allvisible — native: all nodes already visible (nothing hidden to reveal).
- p6-web-uadecide — deny: UA decides device visibility, no app reveal control; fallback none.
- p6-fixedfmt — deny: source format negotiated fixed up front, no underneath-change event; fallback `cap.device.hotplug` or none.
- p6-pw-renego — emulate: PipeWire renegotiates format via param change (cost: stream restart).
- p6-mf-reconfig — emulate: detect via stream error/STREAMTICK + reconfigure (cost: latency).
- p6-gev-resize — emulate: GenICam size/format-change event (partial).
- p6-nohdrmeta — deny: no HDR static-metadata channel on this axis; fallback none (DeckLink-class capture cards carry it).
- p6-fwk-focusramp — emulate: framework steps `cap.control.focus.manual-*` toward target at a rate (cost: CPU loop).
- p6-fwk-expramp — emulate: framework steps `cap.control.exposure.manual-*` toward target at a rate (cost: CPU loop).
- p6-pw-noctl — deny: PipeWire portal exposes no manual focus/exposure control; fallback none.
- p6-bs-fixedfocus — deny: bare sensors are typically fixed-focus (no focus actuator); fallback none.
- p6-cx-nomulticam — deny: CameraX hides multicam control internals; fallback `cap.topology.multicam.control-independence`@camera2ndk.
- p6-nomulticam — deny: no multicam session manager on this axis; fallback none.
