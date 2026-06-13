# genicam — inventory
> api gen: GenICam SFNC 2.7 + GenTL + USB3 Vision + GigE Vision; PFNC pixel formats
> covers matrix columns: genicam

> The industrial machine-vision ceiling. A device is a self-describing register map: it ships a GenICam XML
> description that names every feature per SFNC; GenApi loads it into a **node map** and the camera CANNOT expose
> more than SFNC defines without vendor-XML extension. GenTL is the C transport ABI under it (System→Interface→
> Device→DataStream→Buffer). Transport bindings: GigE Vision (UDP/GVCP+GVSP), USB3 Vision, CoaXPress, Camera Link.
> Feature names below are VERBATIM SFNC 2.7 spelling. Square-bracket `[Selector]` suffixes = the feature is indexed
> by a selector (the SFNC selector/value idiom — set selector then read/write the value). Access codes: R=required,
> O=optional, M=mandatory; interface types IInteger/IFloat/IEnumeration/IBoolean/ICommand/IString/IRegister/ICategory.
> Primary source throughout: EMVA GenICam SFNC v2.7.1 (2022-2-1), https://www.emva.org/wp-content/uploads/GenICam_SFNC_v2_7.pdf

## devices & enumeration
> The GenTL Producer (vendor .cti shared lib) is loaded by a GenTL Consumer (the app). Enumeration walks the module
> tree; each module exposes a register map via GCReadPort/GCWritePort. SFNC DeviceControl gives the device-identity
> + link/throughput surface. GigE adds GVCP discovery + IP config; USB3 Vision adds USB descriptor discovery.
- `GenTL System module / TL_HANDLE` — root, enumerates interfaces; opened via TLOpen after GCInitLib — source: EMVA GenTL 1.6 §4, https://www.emva.org/wp-content/uploads/GenICam_GenTL_1_6.pdf
- `GenTL Interface module / IF_HANDLE` — a frame grabber / NIC; enumerates devices via IFOpenDevice — source: EMVA GenTL 1.6 §4
- `GenTL Device module / DEV_HANDLE` — producer's proxy for one remote device; DevGetPort returns the remote-device PORT_HANDLE (two ports: local + remote) — source: EMVA GenTL 1.6 §4
- `GCInitLib / GCCloseLib / TLOpenInterface / IFOpenDevice / DevOpenDataStream` — the open chain; reverse-order close — source: EMVA GenTL 1.6 §6 (function reference)
- `DeviceControl / ICategory` — category for device information and control — source: SFNC 2.7 §3.1
- `DeviceType / IEnumeration` — device type (Transmitter/Receiver/Transceiver/Peripheral/...) — source: SFNC 2.7 §3.1
- `DeviceScanType / IEnumeration` — Areascan / Linescan sensor scan type — source: SFNC 2.7 §3.1
- `DeviceVendorName / IString` — manufacturer of the device — source: SFNC 2.7 §3.1
- `DeviceModelName / IString` — model of the device — source: SFNC 2.7 §3.1
- `DeviceFamilyName / IString` — product family identifier — source: SFNC 2.7 §3.1
- `DeviceManufacturerInfo / IString` — manufacturer info string — source: SFNC 2.7 §3.1
- `DeviceVersion / IString` — device version — source: SFNC 2.7 §3.1
- `DeviceFirmwareVersion / IString` — firmware version in the device — source: SFNC 2.7 §3.1
- `DeviceSerialNumber / IString` — device serial number — source: SFNC 2.7 §3.1
- `DeviceID / IString` — DEPRECATED (use DeviceSerialNumber) — source: SFNC 2.7 §3.1
- `DeviceUserID / IString` — user-programmable device identifier — source: SFNC 2.7 §3.1
- `DeviceSFNCVersionMajor / Minor / SubMinor / IInteger` — SFNC version used to author the device XML — source: SFNC 2.7 §3.1
- `DeviceManifestEntrySelector` + `DeviceManifestXMLMajorVersion/MinorVersion/SubMinorVersion`, `DeviceManifestSchemaMajor/MinorVersion`, `DeviceManifestPrimaryURL/SecondaryURL` — selects + reports the device's GenICam XML manifest entries (where the description file lives) — source: SFNC 2.7 §3.1
- `DeviceTLType / IEnumeration` — transport-layer type (GigEVision/USB3Vision/CameraLink/CoaXPress/CameraLinkHS/Custom) — source: SFNC 2.7 §3.1
- `DeviceTLVersionMajor / Minor / SubMinor / IInteger` — transport-layer version — source: SFNC 2.7 §3.1
- `DeviceGenCPVersionMajor / Minor / IInteger` — GenCP (Generic Control Protocol) version supported — source: SFNC 2.7 §3.1
- `DeviceMaxThroughput / IInteger [Bps]` — max bandwidth streamable out of device — source: SFNC 2.7 §3.1
- `DeviceConnectionSelector` + `DeviceConnectionSpeed [Bps]` + `DeviceConnectionStatus` — per physical connection speed/status — source: SFNC 2.7 §3.1
- `DeviceLinkSelector` + `DeviceLinkSpeed [Bps]` + `DeviceLinkThroughputLimitMode` + `DeviceLinkThroughputLimit [Bps]` + `DeviceLinkConnectionCount` — bandwidth/link config: cap the device's egress rate per link — source: SFNC 2.7 §3.1
- `DeviceLinkHeartbeatMode / DeviceLinkHeartbeatTimeout [us] / DeviceLinkCommandTimeout [us]` — control-link heartbeat + command timeout — source: SFNC 2.7 §3.1
- `DeviceStreamChannelCount` + `DeviceStreamChannelSelector` + `DeviceStreamChannelType` + `DeviceStreamChannelLink` + `DeviceStreamChannelEndianness` + `DeviceStreamChannelPacketSize [B]` — per stream channel config (the transport-layer stream channels) — source: SFNC 2.7 §3.1
- `DeviceEventChannelCount / IInteger` — number of event channels supported — source: SFNC 2.7 §3.1
- `DeviceCharacterSet / IEnumeration` — character set of device strings — source: SFNC 2.7 §3.1
- `DeviceReset / ICommand` — resets device to power-up state — source: SFNC 2.7 §3.1
- `DeviceIndicatorMode / IEnumeration` — LED/indicator behavior (Active/Inactive/ErrorStatus) — source: SFNC 2.7 §3.1
- `DeviceFeaturePersistenceStart / DeviceFeaturePersistenceEnd / ICommand` — bracket a streamable-feature save/restore — source: SFNC 2.7 §3.1
- `DeviceRegistersStreamingStart / DeviceRegistersStreamingEnd / DeviceRegistersCheck / DeviceRegistersValid / DeviceRegistersEndianness` — bulk register streaming + consistency validation — source: SFNC 2.7 §3.1
- `DeviceTemperatureSelector` + `DeviceTemperature [C]` — temperature at a selectable location (Sensor/Mainboard/...) — source: SFNC 2.7 §3.1
- `DeviceClockSelector` + `DeviceClockFrequency [Hz]` — read a selectable internal clock's frequency — source: SFNC 2.7 §3.1
- `DeviceSerialPortSelector` + `DeviceSerialPortBaudRate` — on-device serial-port pass-through control — source: SFNC 2.7 §3.1
- `DeviceTapGeometry / IEnumeration` — multi-tap output geometry (Geometry_1X_1Y, _1X2_1Y, ...) — source: SFNC 2.7 §25 (Transport Layer Control)
- GigE-Vision discovery + GVCP control: device responds to DISCOVERY_CMD broadcast; `GevDiscoveryAckDelay [ms]`, `GevGVCPExtendedStatusCodesSelector` + `GevGVCPExtendedStatusCodes`, `GevGVCPPendingAck`, `GevPrimaryApplicationSwitchoverKey`, `GevCCP` (control channel privilege), `GevPrimaryApplicationSocket / GevPrimaryApplicationIPAddress` — GVCP is the UDP control protocol; exclusive control arbitration lives here — source: SFNC 2.7 §25 (GigE Vision) + AIA/EMVA GigE Vision 2.x spec
- GigE persistent IP / LLA / DHCP: `GevCurrentIPConfigurationLLA`, `GevCurrentIPConfigurationDHCP`, `GevCurrentIPConfigurationPersistentIP`, `GevCurrentIPAddress/SubnetMask/DefaultGateway`, `GevPersistentIPAddress/SubnetMask/DefaultGateway`, `GevIPConfigurationStatus` — the three GigE IP schemes — source: SFNC 2.7 §25 (GigE Vision)
- GigE physical link: `GevPhysicalLinkConfiguration` / `GevCurrentPhysicalLinkConfiguration` (Single/MultipleLinks/StaticLAG/DynamicLAG), `GevActiveLinkCount`, `GevSupportedOptionSelector` + `GevSupportedOption`, `GevMACAddress`, `GevPAUSEFrameReception/Transmission` — link aggregation + flow control — source: SFNC 2.7 §25 (GigE Vision)
- USB3 Vision device discovery — USB descriptor enumeration + U3V device class; no IP config (USB-addressed) — source: AIA/EMVA USB3 Vision 1.x spec, https://www.automate.org/a3-content/vision-standards-usb3-vision
- **feature-combination feasibility** = the GenApi node map. Each node carries `EAccessMode` (NI=NotImplemented / NA=NotAvailable / RO / WO / RW); `IsImplemented/IsAvailable/IsReadable/IsWritable` derive from it. Integer/Float nodes expose `GetMin/GetMax/GetIncrement` (set-value verified against them; increment must divide evenly). Enumeration nodes expose per-entry `IEnumEntry` access modes so illegal values vanish from the entry list. Dependencies propagate via the **Invalidator** list — changing one feature (e.g. `PixelFormat`) recomputes availability/min/max of dependents (e.g. `Width`, `BinningHorizontal`). The XML encodes this with `pIsAvailable / pIsImplemented / pIsLocked / pInvalidator / pMin / pMax`. This is how "what combinations are legal" is answered: query the live node, don't guess — source: EMVA GenApi 3.x reference (INode.h EAccessMode); https://github.com/roboception/rc_genicam_api INode.h

## topology & streams
> GenTL DataStream module is the acquisition engine + buffer pool; one device can expose multiple stream channels
> (multi-source / multi-stream). SFNC AcquisitionControl drives start/stop and frame counting.
- `GenTL DataStream module / DS_HANDLE` — acquisition engine + internal buffer pool; opened via DevOpenDataStream; one Device may have several — source: EMVA GenTL 1.6 §4
- `AcquisitionControl / ICategory` — acquisition + trigger control features — source: SFNC 2.7 §5.5.1
- `AcquisitionMode / IEnumeration` — values **SingleFrame / MultiFrame / Continuous** — source: SFNC 2.7 §5.5.1
- `AcquisitionStart / ICommand` — starts acquisition — source: SFNC 2.7 §5.5.1
- `AcquisitionStop / ICommand` — stops acquisition at end of current frame — source: SFNC 2.7 §5.5.1
- `AcquisitionStopMode / IEnumeration` — how AcquisitionStop interacts with in-flight frames — source: SFNC 2.7 §5.5.1
- `AcquisitionAbort / ICommand` — aborts acquisition immediately — source: SFNC 2.7 §5.5.1
- `AcquisitionArm / ICommand` — arms the device before AcquisitionStart — source: SFNC 2.7 §5.5.1
- `AcquisitionFrameCount / IInteger` — frames to acquire in MultiFrame mode — source: SFNC 2.7 §5.5.1
- `AcquisitionBurstFrameCount / IInteger` — frames per FrameBurstStart trigger — source: SFNC 2.7 §5.5.1
- `SourceControl / ICategory` — `SourceCount`, `SourceSelector`, `SourceIDValue` — multi-source devices (multiple imaging pipelines per device, e.g. RGB+IR) — source: SFNC 2.7 §19
- `TransferControl / ICategory` — `TransferSelector`, `TransferControlMode` (Automatic/UserControlled/Basic), `TransferOperationMode`, `TransferBlockCount`, `TransferBurstCount`, `TransferQueueMaxBlockCount/CurrentBlockCount`, `TransferQueueMode`, `TransferStart/Stop/Abort/Pause/Resume`, `TransferTriggerSelector/Mode/Source/Activation`, `TransferStatusSelector` + `TransferStatus`, `TransferComponentSelector` + `TransferStreamChannel` — explicit user-paced streaming out of on-device memory (the buffered/triggered-transfer model) — source: SFNC 2.7 §20
- GigE stream channels: `GevStreamChannelSelector`, `GevSCPHostPort`, `GevSCDA` (destination addr), `GevSCPSPacketSize`, `GevSCPD` (inter-packet delay), `GevSCSP`, `GevSCZoneCount/DirectionAll/ConfigurationLock`, `GevSCCFGPacketResendDestination/AllInTransmission/UnconditionalStreaming/ExtendedChunkData`, `GevSCPSFireTestPacket/DoNotFragment` — GVSP stream-channel transport config incl. multi-zone — source: SFNC 2.7 §25 (GigE Vision)

## fine-grained control
> SFNC AnalogControl is the gain/black-level/white-balance/gamma surface; ranges come from each node's
> GetMin/GetMax/GetIncrement at runtime (no fixed list — selector-driven and device-specific).
- `AnalogControl / ICategory` — analog video signal conditioning — source: SFNC 2.7 §6.1
- `GainSelector / IEnumeration` — selects which gain (All/Red/Green/Blue/Tap1/AnalogAll/DigitalAll/...) — source: SFNC 2.7 §6.1
- `Gain[GainSelector] / IFloat` — selected gain as absolute physical value (range via node Min/Max) — source: SFNC 2.7 §6.1
- `GainAuto[GainSelector] / IEnumeration` — AGC mode (Off/Once/Continuous) — source: SFNC 2.7 §6.1
- `GainAutoBalance / IEnumeration` — auto gain balancing across color channels/taps — source: SFNC 2.7 §6.1
- `BlackLevelSelector` + `BlackLevel[BlackLevelSelector] / IFloat` — analog black level, absolute value — source: SFNC 2.7 §6.1
- `BlackLevelAuto[BlackLevelSelector] / IEnumeration` + `BlackLevelAutoBalance` — auto black-level + balancing — source: SFNC 2.7 §6.1
- `WhiteClipSelector` + `WhiteClip[WhiteClipSelector] / IFloat` — clipping ceiling of the video signal — source: SFNC 2.7 §6.1
- `BalanceRatioSelector` + `BalanceRatio[BalanceRatioSelector] / IFloat` — white-balance ratio of selected color to reference — source: SFNC 2.7 §6.1
- `BalanceWhiteAuto / IEnumeration` — auto white balance (Off/Once/Continuous) — source: SFNC 2.7 §6.1
- `Gamma / IFloat` — gamma correction of pixel intensity — source: SFNC 2.7 §6.1
- `ExposureMode / IEnumeration` — Off / Timed / TriggerWidth / TriggerControlled — source: SFNC 2.7 §5.7
- `ExposureTimeMode / IEnumeration` — Common / Individual exposure-time config — source: SFNC 2.7 §5.7
- `ExposureTimeSelector` + `ExposureTime[ExposureTimeSelector] / IFloat [us]` — exposure time when Timed and ExposureAuto Off — source: SFNC 2.7 §5.7
- `ExposureAuto / IEnumeration` — auto exposure (Off/Once/Continuous) when ExposureMode is Timed — source: SFNC 2.7 §5.7
- `LUTControl / ICategory` — `LUTSelector`, `LUTEnable[LUTSelector]`, `LUTIndex[LUTSelector]`, `LUTValue[LUTSelector][LUTIndex]`, `LUTValueAll[LUTSelector]` (whole table in one register access) — per-channel look-up table — source: SFNC 2.7 §7.1
- `ColorTransformationControl / ICategory` — `ColorTransformationSelector` (RGBtoRGB/RGBtoYUV/...), `ColorTransformationEnable`, `ColorTransformationValueSelector` (Gain00..Gain22 + Offset0..2 = the 3×3 matrix + offset), `ColorTransformationValue / IFloat` — programmable color-correction matrix — source: SFNC 2.7 §8.1
- auto-algorithm regions (AAOI) — modeled via `RegionSelector` + `RegionDestination` directing a region's data to an auto-control engine (the SFNC region/destination idiom; SFNC has no dedicated `AAOI*` feature names — it is the region+destination mechanism) — source: SFNC 2.7 §4.1 (Region model)
- `DeviceLinkThroughputLimit[DeviceLinkSelector] / IInteger [Bps]` + `DeviceLinkThroughputLimitMode` — cap data rate (back-pressure on the sensor pipeline) — source: SFNC 2.7 §3.1
- `MultiSlopeMode / MultiSlopeKneePointCount / MultiSlopeKneePointSelector / MultiSlopeExposureLimit[%] / MultiSlopeSaturationThreshold[%] / MultiSlopeIntensityLimit[%] / MultiSlopeExposureGradient` — piecewise multi-slope (HDR-ish) exposure response — source: SFNC 2.7 §5.8

## mechanical controls
> Core SFNC has NO PTZ (pan/tilt/zoom) — industrial cameras are fixed-mount. Lens/optic motion lives in the
> optional OpticControl category (motorized lens controllers).
- PTZ — **none in core SFNC** by design (fixed-mount industrial cameras); pan/tilt is an end-user mechanical mount, not a device feature — source: SFNC 2.7 (absence; cf. UVC PTZ which is a separate webcam class)
- `OpticControl / ICategory` — motorized-lens / optic-controller surface (the vendor-lens path): `OpticControllerSelector`, `OpticControllerInitialize/Disconnect/Abort/Status`, `OpticControllerVendorName/FamilyName/ModelName/SerialNumber/Version/FirmwareVersion/Temperature` — source: SFNC 2.7 §21
- `Aperture[OpticControllerSelector] / IFloat` (f-number) + `ApertureInitialize/Status/Stepper`, `NumericalAperture` — iris/aperture control — source: SFNC 2.7 §21
- `FocusInitialize/Status/Stepper`, `FocusAutoMode`, `FocusAuto`, `FocalPower [dpt]`, `ObjectSensorDistance [mm]` — focus + autofocus on a motorized lens — source: SFNC 2.7 §21
- `FocalLength [mm]` + `FocalLengthInitialize/Status/Stepper` — zoom/focal-length control — source: SFNC 2.7 §21
- `Shutter`, `Filter`, `Stabilization`, `Magnification` (+ each `*Initialize/Status/Stepper`) — mechanical shutter, filter wheel, image-stabilization, magnification on the optic controller — source: SFNC 2.7 §21

## capture modes
> SFNC TriggerControl is the rich hardware-trigger surface consumer OS APIs lack entirely: selectable trigger
> types, hardware line/software/counter sources, edge/level activation, delay/divider/overlap.
- `TriggerSelector / IEnumeration` — type of trigger configured: **AcquisitionStart / AcquisitionEnd / AcquisitionActive / FrameStart / FrameEnd / FrameActive / FrameBurstStart / FrameBurstEnd / FrameBurstActive / LineStart / ExposureStart / ExposureEnd / ExposureActive / MultiSlopeExposureLimit1** — source: SFNC 2.7 §5.6
- `TriggerMode[TriggerSelector] / IEnumeration` — On/Off per selected trigger — source: SFNC 2.7 §5.6
- `TriggerSoftware[TriggerSelector] / ICommand` — generates an internal (software) trigger — source: SFNC 2.7 §5.6
- `TriggerSource[TriggerSelector] / IEnumeration` — Software / Line0..LineN / Counter0End / Timer0End / Encoder0 / UserOutput0 / Action0 / LinkTrigger0 / ... (the physical line or internal signal) — source: SFNC 2.7 §5.6
- `TriggerActivation[TriggerSelector] / IEnumeration` — **RisingEdge / FallingEdge / AnyEdge / LevelHigh / LevelLow** — source: SFNC 2.7 §5.6
- `TriggerOverlap[TriggerSelector] / IEnumeration` — Off / ReadOut / PreviousFrame / PreviousLine (overlap exposure with prior readout) — source: SFNC 2.7 §5.6
- `TriggerDelay[TriggerSelector] / IFloat [us]` — delay after trigger reception before activation — source: SFNC 2.7 §5.6
- `TriggerDivider[TriggerSelector] / IInteger` — divide incoming trigger pulses by N — source: SFNC 2.7 §5.6
- `TriggerMultiplier[TriggerSelector] / IInteger` — multiply incoming trigger pulses by N — source: SFNC 2.7 §5.6
- sensor `ExposureMode` = TriggerWidth — exposure duration set by the trigger pulse width (not a timer) — source: SFNC 2.7 §5.7
- `SensorShutterMode / IEnumeration` — **Global / Rolling / GlobalReset** — source: SFNC 2.7 §4.1
- bracketing / per-frame setting tables — via `SequencerControl` (see OS integration); HDR via `MultiSlopeMode` (see fine-grained control). SFNC has no single `HDR` feature name — HDR is multi-slope exposure or sequencer-driven bracketing — source: SFNC 2.7 §5.8, §17
- `SoftwareSignalControl / ICategory` — `SoftwareSignalSelector`, `SoftwareSignalPulse` — host-generated pulse usable as a software trigger source — source: SFNC 2.7 §13.1

## depth / 3D / calibration
> SFNC Scan3dControl is the calibrated-3D surface (distance unit, coordinate system, focal length, baseline,
> principal point, per-axis scale/offset/transform). GenDC carries multi-component (range+intensity+confidence).
- `Scan3dControl / ICategory` — 3D camera control category — source: SFNC 2.7 §3D Scan (§ "Scan 3D Control")
- `Scan3dExtractionSelector` + `Scan3dExtractionSource` + `Scan3dExtractionMethod` — selects/sources/method of the 3D-extraction module — source: SFNC 2.7 (Scan 3D Control)
- `Scan3dDistanceUnit / IEnumeration` — **Millimeter / Inch / Pixel** etc. for calibrated distance — source: SFNC 2.7 (Scan 3D Control)
- `Scan3dCoordinateSystem / IEnumeration` — **Cartesian / Spherical / Cylindrical** — source: SFNC 2.7 (Scan 3D Control)
- `Scan3dOutputMode / IEnumeration` — **UncalibratedC / CalibratedABC_Grid / CalibratedAC / CalibratedAC_Linescan / RectifiedC / RectifiedC_Linescan / DisparityC / DisparityC_Linescan** — source: SFNC 2.7 (Scan 3D Control)
- `Scan3dCoordinateSystemReference / IEnumeration` — Anchor / Transformed reference location — source: SFNC 2.7 (Scan 3D Control)
- `Scan3dCoordinateSelector / IEnumeration` — **CoordinateA / CoordinateB / CoordinateC** (X/Y/Z in Cartesian) — source: SFNC 2.7 (Scan 3D Control)
- `Scan3dCoordinateScale[...][Scan3dCoordinateSelector] / IFloat` + `Scan3dCoordinateOffset` — pixel→world transform scale/offset per axis — source: SFNC 2.7 (Scan 3D Control)
- `Scan3dInvalidDataFlag` + `Scan3dInvalidDataValue` — sentinel for non-valid (no-return) pixels — source: SFNC 2.7 (Scan 3D Control)
- `Scan3dAxisMin / Scan3dAxisMax` — valid transmitted coordinate range per axis — source: SFNC 2.7 (Scan 3D Control)
- `Scan3dCoordinateTransformSelector` + `Scan3dTransformValue` — rotation/translation transform matrix values — source: SFNC 2.7 (Scan 3D Control)
- `Scan3dCoordinateReferenceSelector` + `Scan3dCoordinateReferenceValue` — pose of anchor/transformed system vs reference — source: SFNC 2.7 (Scan 3D Control)
- `Scan3dFocalLength[RegionSelector] / IFloat [Pixel]` — camera focal length in pixels — source: SFNC 2.7 (Scan 3D Control)
- `Scan3dBaseline / IFloat [m]` — stereo-camera physical baseline distance — source: SFNC 2.7 (Scan 3D Control)
- `Scan3dPrincipalPointU / Scan3dPrincipalPointV [Pixel]` — principal point (optical center) relative to region origin — source: SFNC 2.7 (Scan 3D Control)
- GenICam **GenDC** (Generic Data Container) — multi-component container carrying range + intensity + confidence + metadata in one block; negotiated via `GenDCStreamingMode/Status`, `GenDCDescriptor`, `GenDCFlowMappingTable` (Transport Layer Control) — source: SFNC 2.7 §25 + EMVA GenDC 1.1 spec, https://www.emva.org/standards-technology/genicam/genicam-downloads/
- stereo / ToF support — modeled via Scan3d* + GenDC multi-component + PFNC `Coord3D_*` / `Confidence*` pixel formats (see frame memory) — source: SFNC 2.7 + PFNC 2.4

## live effects
- none — industrial cameras do NOT do beautify / background-blur / portrait segmentation. The pipeline is raw-sensor + analog/LUT/color-matrix conditioning only. Any "effect" is a host-side downstream concern, outside the device's SFNC surface — source: SFNC 2.7 (absence by design; cf. AVFoundation/MediaFoundation effect APIs)

## frame memory
> SFNC ImageFormatControl is the geometry + pixel-format + multi-ROI surface. PFNC (Pixel Format Naming
> Convention) is the separate registry of pixel-format names/values. GenDC + GenTL Buffer module move the bytes.
- `ImageFormatControl / ICategory` — image format of transmitted image — source: SFNC 2.7 §4.1
- `SensorWidth / SensorHeight / IInteger` — effective sensor dimensions in pixels — source: SFNC 2.7 §4.1
- `SensorPixelWidth / SensorPixelHeight / IFloat [um]` — physical pixel pitch — source: SFNC 2.7 §4.1
- `SensorName / IString`, `SensorTaps / IEnumeration`, `SensorDigitizationTaps` — sensor identity + tap geometry — source: SFNC 2.7 §4.1
- `WidthMax / HeightMax / IInteger` — max image dimensions — source: SFNC 2.7 §4.1
- `Width[RegionSelector] / Height[RegionSelector] / IInteger` — ROI dimensions — source: SFNC 2.7 §4.1
- `OffsetX[RegionSelector] / OffsetY[RegionSelector] / IInteger` — ROI origin offset — source: SFNC 2.7 §4.1
- `LinePitchEnable / LinePitch[RegionSelector] [B]` — bytes between consecutive lines — source: SFNC 2.7 §4.1
- `RegionSelector / IEnumeration` — selects ROI (Region0/Region1/... or All) — the **MULTI-ROI** entry point — source: SFNC 2.7 §4.1
- `RegionMode[RegionSelector] / IEnumeration` — On/Off per region (multiple regions active at once = multi-ROI) — source: SFNC 2.7 §4.1
- `RegionDestination[RegionSelector] / IEnumeration` — where the region's data goes (Stream0 / a processing/auto engine) — source: SFNC 2.7 §4.1
- `RegionIDValue[RegionSelector] / IInteger` — unique region identifier — source: SFNC 2.7 §4.1
- `ComponentSelector / IEnumeration` — selects a component to enable (Intensity / Range / Confidence / Disparity / Infrared / ...) — multi-component (3D+intensity) selection — source: SFNC 2.7 §4.1
- `ComponentEnable[RegionSelector][ComponentSelector] / IBoolean` + `ComponentIDValue` — activate per-component streaming — source: SFNC 2.7 §4.1
- `GroupSelector / GroupIDValue` — component-group selection — source: SFNC 2.7 §4.1
- `ImageComponentSelector / ImageComponentEnable` — DEPRECATED (use ComponentSelector/ComponentEnable) — source: SFNC 2.7 §4.1
- `BinningSelector` + `BinningHorizontal[BinningSelector] / BinningVertical[BinningSelector] / IInteger` + `BinningHorizontalMode / BinningVerticalMode` (Sum/Average) — combine photosites — source: SFNC 2.7 §4.1
- `DecimationHorizontal / DecimationVertical / IInteger` + `DecimationHorizontalMode / DecimationVerticalMode` (Discard/Average) — sub-sampling — source: SFNC 2.7 §4.1
- `ReverseX / ReverseY / IBoolean` — horizontal/vertical image flip — source: SFNC 2.7 §4.1
- `PixelFormat[ComponentSelector] / IEnumeration` — pixel format (PFNC value); `PixelFormatInfoSelector` + `PixelFormatInfoID` map name→32-bit stream identifier — source: SFNC 2.7 §4.1 + PFNC 2.4
- `PixelSize[ComponentSelector] / PixelColorFilter[ComponentSelector] / PixelDynamicRangeMin/Max` — bit depth, Bayer filter (BayerRG/GB/GR/BG/None), digitization range — source: SFNC 2.7 §4.1
- **PFNC pixel-format names** (verbatim, separate registry): Mono1p/Mono2p/Mono4p/`Mono8`/Mono8s/`Mono10`/Mono10Packed/Mono10p/`Mono12`/Mono12Packed/Mono12p/Mono14/`Mono16`; `BayerRG8`/BayerGR8/BayerGB8/BayerBG8 (+10/10p/12/12p/16 variants); `RGB8`/RGB8_Planar/RGB10/RGB12/RGB16/BGR8/RGBa8/BGRa8; `YUV422_8`/`YCbCr422_8`/YCbCr601_422_8/YCbCr709_422_8/YCbCr2020_422_8 (+CbYCrY orderings, +10/12 depths)/YCbCr8/YUV8_UYV; 3D: `Coord3D_ABC32f`/Coord3D_ABC32f_Planar/Coord3D_AC32f/Coord3D_C16/Coord3D_C32f; `Confidence1`/Confidence1p/Confidence8/Confidence16/Confidence32f — source: EMVA PFNC v2.4 (2021-6-16), https://www.emva.org/wp-content/uploads/GenICam_PFNC_2_4.pdf + GenICam Pixel Format Names and Values, https://www.emva.org/wp-content/uploads/GenICamPixelFormatValues.pdf
- `TestPatternGeneratorSelector` + `TestPattern[TestPatternGeneratorSelector] / IEnumeration` — on-sensor test patterns (GreyHorizontalRamp/VerticalRamp/ColorBar/FrameCounter/...) — source: SFNC 2.7 §4.1
- `Deinterlacing / IEnumeration` — device-side de-interlacing — source: SFNC 2.7 §4.1
- `ImageCompressionMode / ImageCompressionRateOption / ImageCompressionQuality / ImageCompressionBitrate [Mbps] / ImageCompressionJPEGFormatOption` — on-device JPEG/compression — source: SFNC 2.7 §4.1
- `GenTL Buffer module / BUFFER_HANDLE` — target memory for acquisition; user- or producer-allocated; announced via DSAllocAndAnnounceBuffer into the input pool; the ONE module with no port register map — source: EMVA GenTL 1.6 §4
- `PayloadSize / IInteger [B]` — bytes per buffer/chunk on the stream channel (Transport Layer Control) — source: SFNC 2.7 §25
- adaptive noise — no standard SFNC feature (vendor territory) — source: SFNC 2.7 (absence)

## timing
> SFNC has a full hardware-timing surface: device timestamp counter, PTP/IEEE-1588 network clock sync, counters
> and timers driven by signal events. This is the cross-camera synchronization substrate.
- `Timestamp / IInteger [ns]` — current device timestamp counter (Device Control) — source: SFNC 2.7 §3.1
- `TimestampReset / ICommand` — reset the timestamp counter — source: SFNC 2.7 §3.1
- `TimestampLatch / ICommand` + `TimestampLatchValue / IInteger [ns]` — latch then read the counter atomically — source: SFNC 2.7 §3.1
- `ChunkTimestamp / IInteger` (+ `ChunkTimestampLatchValue [ns]`) — per-frame timestamp embedded in payload (Chunk Data) — source: SFNC 2.7 §Chunk Data Control
- **PTP / IEEE-1588** (`PtpControl` category): `PtpEnable / IBoolean`, `PtpClockAccuracy / IEnumeration`, `PtpDataSetLatch / ICommand`, `PtpStatus / IEnumeration` (Disabled/Listening/Master/Passive/Uncalibrated/Slave), `PtpServoStatus`, `PtpOffsetFromMaster [ns]`, `PtpClockID`, `PtpParentClockID`, `PtpGrandmasterClockID`, `PtpMeanPropagationDelay [ns]` — network clock discipline so multiple cameras share a clock — source: SFNC 2.7 §25 (Precision Time Protocol)
- `CounterAndTimerControl / ICategory` — `CounterSelector`, `CounterEventSource` + `CounterEventActivation`, `CounterResetSource` + `CounterResetActivation`, `CounterReset`, `CounterValue`, `CounterValueAtReset`, `CounterDuration`, `CounterStatus`, `CounterTriggerSource` + `CounterTriggerActivation` — programmable counters incrementing on signal events — source: SFNC 2.7 §10
- timer features: `TimerSelector`, `TimerDuration [us]`, `TimerDelay [us]`, `TimerReset`, `TimerValue`, `TimerStatus`, `TimerTriggerSource` + `TimerTriggerActivation`, `TimerTriggerArmDelay` — programmable one-shot/retriggerable timers — source: SFNC 2.7 §10
- exposure-start/end timing — `EventExposureEnd` async event with `EventExposureEndTimestamp` (see metadata) — source: SFNC 2.7 §15
- `AcquisitionFrameRate / IFloat [Hz]` + `AcquisitionFrameRateEnable` — frame-rate control — source: SFNC 2.7 §5.5.1
- `AcquisitionLineRate / IFloat [Hz]` + `AcquisitionLineRateEnable` — line-rate control (linescan cameras) — source: SFNC 2.7 §5.5.1
- `AcquisitionStatusSelector` + `AcquisitionStatus / IBoolean` — read internal acquisition signals (FrameTriggerWait / FrameActive / ExposureActive / AcquisitionActive / ...); the surface that exposes ReadOut/exposure phase — source: SFNC 2.7 §5.5.1
- ReadOutTime — SFNC does not define a standalone `ReadOutTime` feature; readout phase is observed via `AcquisitionStatus[ExposureActive]` / TriggerOverlap=ReadOut semantics — source: SFNC 2.7 §5.5.1 (no such feature name; do not invent)
- `DeviceClockSelector` + `DeviceClockFrequency [Hz]` — the clock backing timestamps/timers — source: SFNC 2.7 §3.1

## metadata
> Chunk Data = camera-emitted per-frame metadata appended to the payload, selectable + extensible. Event Control =
> async notifications on signal edges / exposure phases. This is far richer than any consumer-OS frame metadata.
- `ChunkDataControl / ICategory` — chunk data control — source: SFNC 2.7 §Chunk Data Control
- `ChunkModeActive / IBoolean` — include chunk data in the payload — source: SFNC 2.7 §Chunk Data Control
- `ChunkXMLEnable / IBoolean` — embed the GenICam XML needed to parse chunks — source: SFNC 2.7 §Chunk Data Control
- `ChunkSelector / IEnumeration` + `ChunkEnable[ChunkSelector] / IBoolean` — per-chunk enable (selectable/extensible) — source: SFNC 2.7 §Chunk Data Control
- per-frame chunk values: `ChunkExposureTime[ChunkExposureTimeSelector] [us]`, `ChunkGain[ChunkGainSelector]`, `ChunkBlackLevel[ChunkBlackLevelSelector]`, `ChunkTimestamp` / `ChunkTimestampLatchValue`, `ChunkFrameID`, `ChunkLineStatusAll` (all I/O line states at FrameStart), `ChunkCounterValue[ChunkCounterSelector]`, `ChunkTimerValue[ChunkTimerSelector]`, `ChunkEncoderValue/Status`, `ChunkExposureTimeSelector` — source: SFNC 2.7 §Chunk Data Control
- chunk geometry/format echo: `ChunkOffsetX/Y`, `ChunkWidth/Height`, `ChunkPixelFormat`, `ChunkPixelDynamicRangeMin/Max`, `ChunkBinningHorizontal/Vertical`, `ChunkDecimationHorizontal/Vertical`, `ChunkReverseX/Y`, `ChunkLinePitch`, `ChunkImage` (full image as register) — source: SFNC 2.7 §Chunk Data Control
- chunk routing: `ChunkRegionSelector/ID/IDValue`, `ChunkComponentSelector/ID/IDValue`, `ChunkGroupSelector/ID/IDValue`, `ChunkSourceSelector/ID/IDValue`, `ChunkScanLineSelector`, `ChunkSequencerSetActive`, `ChunkTransferBlockID/StreamID/QueueCurrentBlockCount`, `ChunkStreamChannelID` — source: SFNC 2.7 §Chunk Data Control
- chunk 3D echo: `ChunkScan3dDistanceUnit/OutputMode/CoordinateSystem/CoordinateScale/Offset/InvalidDataFlag/Value/AxisMin/Max/TransformValue/FocalLength/Baseline/PrincipalPointU/V` — full Scan3d calibration carried per-frame — source: SFNC 2.7 §Chunk Data Control
- `EventControl / ICategory` — `EventSelector`, `EventNotification[EventSelector]` (Off/On/GigEVisionEvent) — source: SFNC 2.7 §15
- async event data categories: `EventFrameTrigger` (+Timestamp/FrameID), `EventExposureEnd` (+Timestamp/FrameID), `EventError` (+Timestamp/FrameID/Code), `EventTest` (+Timestamp) — selectable events fire on exposure-end / frame-trigger / errors / line edges / counter-end / timer-end (event id set is device-extensible) — source: SFNC 2.7 §15
- histogram / AOI statistics — **not in core SFNC** (vendor feature territory; no standard `Histogram*` feature name) — source: SFNC 2.7 (absence; do not invent)

## egress (virtual camera publish)
- machine-vision cameras do NOT publish virtual cameras — GenICam is an ingest stack (consume a remote device). Note: GenTL CAN model a software/virtual device — a GenTL Producer may present a synthetic Device module (e.g. a file/replay producer), and `TestPayloadFormatMode` makes a real device emit a synthetic test payload; but there is no app→OS "register as a camera" path here — source: EMVA GenTL 1.6 §4 + SFNC 2.7 §23 (Test Control)

## OS integration
> No OS camera-permission model — industrial access is exclusive device control over the transport's control
> channel (GVCP for GigE, control endpoint for U3V). Persistence, GPIO, network-synchronized triggering, on-device
> file upload, and hardware setting-tables (sequencer) all live here.
- access model — exclusive control via the transport control channel; GigE arbitrates with `GevCCP` (control channel privilege: open/exclusive/control+switchover) + `GevPrimaryApplicationSwitchoverKey` (monitor vs control access, primary-app switchover). No per-app OS permission prompt — the device itself grants/denies — source: SFNC 2.7 §25 (GigE Vision) + AIA/EMVA GigE Vision spec
- `DeviceAccessStatus` — device access state (ReadWrite / ReadOnly / NoAccess / OpenReadWrite / OpenReadOnly / Busy) surfaced at the GenTL Interface/Device-info level (DEVICE_ACCESS_STATUS), used to know if a device is already controlled by another process — source: EMVA GenTL 1.6 (DEVICE_ACCESS_STATUS enum) — `?` exact spelling is GenTL info-command, not an SFNC node
- `UserSetControl / ICategory` — `UserSetSelector` (Default/UserSet0/UserSet1/...), `UserSetLoad`, `UserSetSave`, `UserSetDefault`, `UserSetDescription`, `UserSetFeatureSelector` + `UserSetFeatureEnable` — on-device config persistence (save/load whole camera state to non-volatile memory) — source: SFNC 2.7 §16
- `ActionControl / ICategory` — `ActionUnconditionalMode`, `ActionDeviceKey` (W-O), `ActionQueueSize`, `ActionSelector`, `ActionGroupMask[ActionSelector]`, `ActionGroupKey[ActionSelector]` — Action Commands: broadcast a single network message that fires a synchronized action (e.g. trigger) on ALL matching cameras at once — source: SFNC 2.7 §14
- GigE **Scheduled Action Commands** — an action command tagged with a future PTP timestamp so every PTP-synced camera triggers at the *same wall-clock instant* (network-wide synchronized capture); `ActionQueueSize` reports the scheduled queue depth — source: AIA/EMVA GigE Vision 2.x spec (Scheduled Action Commands) + SFNC 2.7 §14
- `DigitalIOControl / ICategory` — GPIO: `LineSelector`, `LineMode[LineSelector]` (Input/Output), `LineInverter`, `LineStatus` + `LineStatusAll` (bitfield), `LineSource[LineSelector]` (route an internal signal — ExposureActive/Timer0Active/UserOutput0/... — onto an output pin), `LineFormat` (electrical: TTL/LVDS/OptoCoupled/...), `UserOutputSelector` + `UserOutputValue` + `UserOutputValueAll` + `UserOutputValueAllMask` — hardware I/O lines (strobe out, trigger in) — source: SFNC 2.7 §9
- `FileAccessControl / ICategory` — `FileSelector` (UserSet/LUTLuminance/DeviceFirmware/...), `FileOperationSelector` (Open/Close/Read/Write/Delete), `FileOperationExecute`, `FileOpenMode` (Read/Write/ReadWrite), `FileAccessBuffer`, `FileAccessOffset/Length`, `FileOperationStatus/Result`, `FileSize` — generic file transfer: firmware / LUT / config upload-download — source: SFNC 2.7 §18
- `SequencerControl / ICategory` — `SequencerMode`, `SequencerConfigurationMode`, `SequencerFeatureSelector` + `SequencerFeatureEnable`, `SequencerSetSelector`, `SequencerSetSave`, `SequencerSetLoad`, `SequencerSetActive`, `SequencerSetStart`, `SequencerSetNext[SequencerSetSelector][SequencerPathSelector]`, `SequencerPathSelector`, `SequencerTriggerSource` + `SequencerTriggerActivation` — hardware per-frame setting tables: each "set" is a snapshot of features (exposure/gain/ROI) and the camera advances set→set per frame on a trigger/event (bracketing in hardware) — source: SFNC 2.7 §17
- `EncoderControl / ICategory` — `EncoderSelector`, `EncoderSourceA/B`, `EncoderMode` (FourPhase/HighResolution), `EncoderDivider`, `EncoderOutputMode`, `EncoderStatus`, `EncoderTimeout`, `EncoderResetSource/Activation/Reset`, `EncoderValue/AtReset`, `EncoderResolution [mm]` — quadrature encoder input (conveyor/web position → line trigger), the linescan-on-a-conveyor surface — source: SFNC 2.7 §11
- `LogicBlockControl / ICategory` — `LogicBlockSelector`, `LogicBlockFunction`, `LogicBlockInputNumber/Selector/Source/Inverter`, `LogicBlockLUTSelector/Index/Value/ValueAll` — on-device combinational logic (build trigger gating from I/O lines without host involvement) — source: SFNC 2.7 §12
- `LightControl / ICategory` — `LightControllerSelector`, `LightControllerSource`, `LightCurrentRating [Amp]`, `LightVoltageRating [Volt]`, `LightBrightness [%]`, `LightConnectionStatus`, `LightCurrentMeasured/VoltageMeasured` — integrated/strobe illumination controller — source: SFNC 2.7 §20
- `TransportLayerControl / ICategory` — `TLParamsLocked` (M; locks critical features during acquisition), `TLParamsLockedSelector` + `TLParamsLockedState`, `PayloadSize` — the TL gate that freezes geometry/format mid-stream — source: SFNC 2.7 §25
- `GenICam Control` (node-map roots) — `Root` (ICategory; tree root), `Device` (IPort; default device port), `ValueArrayCandidates` — the GenApi entry points into the feature tree — source: SFNC 2.7 §24

## obscure corners
- `DeviceManifestEntrySelector` + `DeviceManifestPrimaryURL/SecondaryURL` — the device advertises where to fetch its own GenICam XML (local register address, or an http/file URL) — source: SFNC 2.7 §3.1
- `DeviceSerialPortSelector` + `DeviceSerialPortBaudRate` — tunnel an RS-232 serial port through the camera (control downstream lighting/PLC over the camera's link) — source: SFNC 2.7 §3.1
- `GevSCCFGUnconditionalStreaming` — keep streaming even if the control channel drops or ICMP unreachable arrives — source: SFNC 2.7 §25 (GigE Vision)
- `GevSCZoneCount` / `GevSCZoneDirectionAll` / `GevSCZoneConfigurationLock` — multi-zone GVSP (sensor split into independently-directed transmission zones) — source: SFNC 2.7 §25 (GigE Vision)
- `CoaXPress` link-sharing: `CxpLinkSharingEnable`, `CxpLinkSharingSubDeviceSelector`, `CxpLinkSharingHorizontalStripeCount/VerticalStripeCount/HorizontalOverlap/VerticalOverlap/DuplicateStripe` — split one sensor across multiple CXP sub-devices with overlap — source: SFNC 2.7 §25 (CoaXPress)
- `CxpPoCxpAuto / TurnOff / TripReset / Status` — Power-over-CoaXPress management (power the camera over the coax) — source: SFNC 2.7 §25 (CoaXPress)
- `ClConfiguration` / `ClTimeSlotsCount` — Camera Link tap configuration + time-multiplexing — source: SFNC 2.7 §25 (Camera Link)
- `NetworkStatistics` / `oMACControlFunctionEntity` / `aPAUSEMACCtrlFramesTransmitted/Received` — Ethernet PAUSE-frame counters (debug GigE flow control) — source: SFNC 2.7 §25 (Network Statistics)
- `TestPendingAck` / `TestEventGenerate` / `TestPayloadFormatMode` — protocol conformance / pending-ack / synthetic-payload test hooks — source: SFNC 2.7 §23 (Test Control)
- `GevGVCPExtendedStatusCodesSelector` + `GevGVCPExtendedStatusCodes` — opt into richer GVCP error codes per GigE version — source: SFNC 2.7 §25 (GigE Vision)
- `ChunkScanLineSelector` + `ChunkEncoderValue[ChunkEncoderSelector][ChunkScanLineSelector]` — per-scan-line encoder value (linescan: encoder position embedded per line) — source: SFNC 2.7 §Chunk Data Control
- `MultiSlopeKneePointSelector` family — multi-slope (dual/triple-slope) sensor HDR knee-point configuration — source: SFNC 2.7 §5.8
- `?` `DeviceAccessStatus` exact node spelling — it is a GenTL DEVICE_ACCESS_STATUS info command (ReadWrite/ReadOnly/NoAccess/Busy), not a guaranteed SFNC IEnumeration node; confirm against GenTL 1.6 before pinning
- `?` `ReadOutTime` — NOT a standard SFNC feature name (readout is inferred via AcquisitionStatus); do not treat as a real node
- `?` precise PFNC hex values + full Bayer 12/16 + full YCbCr depth matrix — authoritative list is the separate GenICamPixelFormatValues.pdf; cross-check before hard-coding values
