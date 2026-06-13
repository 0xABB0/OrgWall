# avfoundation — inventory
> api gen: AVFoundation / AVCaptureSession, macOS 14 + iOS 17 (+ CoreMediaIO CMIOExtension for publish)
> covers matrix columns: macos+avfoundation, ios+avfoundation

Notation: symbols VERBATIM (Obj-C primary; Swift in parens where it differs). Availability + DELTAS pinned from the on-disk SDK headers (`MacOSX26.0.sdk` AVFoundation/CoreMediaIO/AVKit headers, `API_AVAILABLE`/`API_UNAVAILABLE` macros) — authoritative over the JS-rendered web pages. `[>pin]` = exists in SDK but introduced AFTER the macOS 14 / iOS 17 pin (recorded because the union defines the ceiling). `?` = could not pin exact symbol/behaviour; what to check noted. "iOS-only" / "macOS-only" track `API_UNAVAILABLE`. Web sources are the Apple Developer doc pages for each symbol cluster (same slug as the header symbol); header file:line given where load-bearing.

## devices & enumeration
- `AVCaptureDevice` — a physical/virtual capture device (camera or mic) — source: AVCaptureDevice.h, https://developer.apple.com/documentation/avfoundation/avcapturedevice
- `AVCaptureDeviceInput` — `AVCaptureInput` wrapping a device for a session — source: AVCaptureInput.h
- `AVCaptureDeviceInput initWithDevice:error:` — throwing init wrapping a device — source: AVCaptureInput.h
- `AVCaptureDeviceInput.ports(for:sourceDeviceType:sourceDevicePosition:)` (`portsWithMediaType:sourceDeviceType:sourceDevicePosition:`) — extract a virtual device's constituent `AVCaptureInputPort`s for manual multi-cam wiring — source: AVCaptureInput.h
- `AVCaptureDeviceInput.unifiedAutoExposureDefaultsEnabled` — unify AE defaults across constituents — source: AVCaptureInput.h
- `AVCaptureDeviceInput.videoMinFrameDurationOverride` (`CMTime`) — override device min frame duration on the input — source: AVCaptureInput.h
- `AVCaptureDeviceDiscoverySession` — query devices by type+media+position; `.devices` is KVO-observable for hot-plug — source: AVCaptureDeviceDiscoverySession.h
- `AVCaptureDeviceDiscoverySession discoverySessionWithDeviceTypes:mediaType:position:` — build a discovery session — source: same
- `AVCaptureDeviceDiscoverySession.devices` — matching devices (KVO `[.initial,.new]` for arrive/remove) — source: same
- `AVCaptureDeviceDiscoverySession.supportedMultiCamDeviceSets` — `NSArray<NSSet<AVCaptureDevice*>*>`: device combos usable simultaneously in multi-cam — source: same
- `AVCaptureDevice defaultDeviceWithMediaType:` — default device for a media type — source: AVCaptureDevice.h
- `AVCaptureDevice defaultDeviceWithDeviceType:mediaType:position:` — default device matching type+media+position — source: AVCaptureDevice.h
- `AVCaptureDevice devicesWithMediaType:` / `+devices` — DEPRECATED all-devices enumeration (use discovery session) — source: AVCaptureDevice.h

### device characteristics
- `AVCaptureDevice.localizedName` — human-readable name — source: AVCaptureDevice.h
- `AVCaptureDevice.uniqueID` — persistent unique identifier — source: AVCaptureDevice.h
- `AVCaptureDevice.modelID` — model identifier string — source: AVCaptureDevice.h
- `AVCaptureDevice.manufacturer` — manufacturer string — source: AVCaptureDevice.h
- `AVCaptureDevice.deviceType` — the `AVCaptureDeviceType` — source: AVCaptureDevice.h
- `AVCaptureDevice.position` — `AVCaptureDevicePosition` (`Front`/`Back`/`Unspecified`) — source: AVCaptureDevice.h
- `AVCaptureDevice hasMediaType:` — Bool: supports a given `AVMediaType` — source: AVCaptureDevice.h
- `AVCaptureDevice.transportType` — connection transport (USB/Thunderbolt/etc.); macOS-relevant — source: AVCaptureDevice.h
- `AVCaptureDevice.lensAperture` — current lens f-number (read-only) — source: AVCaptureDevice.h
- `AVCaptureDevice.deviceWhiteBalanceGains` — current per-channel WB gains — source: AVCaptureDevice.h
- `AVCaptureDevice.activeColorSpace` (`AVCaptureColorSpace`) — active capture color space — source: AVCaptureDevice.h
- `AVCaptureDevice.formats` — `[AVCaptureDeviceFormat]` supported formats — source: AVCaptureDevice.h
- `AVCaptureDevice.activeFormat` — current `AVCaptureDeviceFormat` — source: AVCaptureDevice.h
- `AVCaptureDevice.isConnected` — currently connected — source: AVCaptureDevice.h
- `AVCaptureDevice.isContinuityCamera` (`continuityCamera`) — device is a Continuity Camera — macos(13)+ios(16), BOTH platforms — source: AVCaptureDevice.h:2547
- `AVCaptureDevice.companionDeskViewCamera` — the Desk View camera paired with a device — macos(13)+ios(16) — source: AVCaptureDevice.h

### device types (AVCaptureDeviceType) — DELTAS pinned
- `AVCaptureDeviceTypeBuiltInWideAngleCamera` — wide-angle — ios(10)+macos(10.15), BOTH — source: AVCaptureDevice.h
- `AVCaptureDeviceTypeBuiltInUltraWideCamera` — ultra-wide — **iOS-only** — source: AVCaptureDevice.h
- `AVCaptureDeviceTypeBuiltInTelephotoCamera` — telephoto — **iOS-only** in practice — source: AVCaptureDevice.h
- `AVCaptureDeviceTypeBuiltInDualCamera` — virtual: wide+tele — **iOS-only** — source: AVCaptureDevice.h
- `AVCaptureDeviceTypeBuiltInDualWideCamera` — virtual: ultrawide+wide — **iOS-only** — source: AVCaptureDevice.h
- `AVCaptureDeviceTypeBuiltInTripleCamera` — virtual: ultrawide+wide+tele — **iOS-only** — source: AVCaptureDevice.h
- `AVCaptureDeviceTypeBuiltInTrueDepthCamera` — IR+YUV depth front cam — ios(11.1), **API_UNAVAILABLE(macos)** — source: AVCaptureDevice.h:568
- `AVCaptureDeviceTypeBuiltInLiDARDepthCamera` — LiDAR+YUV depth cam — ios(15.4), **API_UNAVAILABLE(macos)** — source: AVCaptureDevice.h:574
- `AVCaptureDeviceTypeContinuityCamera` — iPhone-as-Mac-camera — macos(14)+ios(17), BOTH (new in this pin) — source: AVCaptureDevice.h:587
- `AVCaptureDeviceTypeDeskViewCamera` — virtual overhead desk camera — macos(13), **API_UNAVAILABLE(ios)** (macOS-only) — source: AVCaptureDevice.h:593
- `AVCaptureDeviceTypeExternal` — external/UVC device (iPad+Mac) — macos(14)+ios(17), BOTH (replaces ExternalUnknown) — source: AVCaptureDevice.h:484
- `AVCaptureDeviceTypeMicrophone` — mic device type — macos(14)+ios(17) (replaces BuiltInMicrophone) — source: AVCaptureDevice.h:490
- `AVCaptureDeviceTypeExternalUnknown` — DEPRECATED→External, macos(10.15,14.0), macOS-only — source: AVCaptureDevice.h:599
- `AVCaptureDeviceTypeBuiltInMicrophone` — DEPRECATED→Microphone, macos/ios(…,14.0/17.0) — source: AVCaptureDevice.h:611
- `AVCaptureDeviceTypeBuiltInDuoCamera` — DEPRECATED→BuiltInDualCamera — source: AVCaptureDevice.h

### per-device capability query / feature-combination feasibility
- `AVCaptureDeviceFormat` (`AVCaptureDevice.Format`) — one capture format (dims, FPS ranges, ISO, HDR, depth, etc.) — source: AVCaptureDevice.h
- `AVCaptureDeviceFormat.videoSupportedFrameRateRanges` — `[AVFrameRateRange]` — source: AVCaptureDevice.h
- `AVCaptureDeviceFormat.supportedColorSpaces` — `[AVCaptureColorSpace]` — source: AVCaptureDevice.h
- `AVCaptureDeviceFormat.supportedMaxPhotoDimensions` — `[CMVideoDimensions]` max still dims — source: AVCaptureDevice.h
- `AVCaptureDeviceFormat.supportedDepthDataFormats` — `[AVCaptureDeviceFormat]` depth formats co-usable with this video format — source: AVCaptureDevice.h (gates depth-capability)
- `AVCaptureDeviceFormat.supportedVideoZoomFactorsForDepthDataDelivery` — zoom factors valid while depth is delivered (combination feasibility) — source: AVCaptureDevice.h
- `AVCaptureDevice.isMultiCamSupported` (`multiCamSupported`, instance) — device participates in multi-cam — ios(13)+macCatalyst(14)+tvos(17)+visionos(2.1), **API_UNAVAILABLE(macos)** — source: AVCaptureDevice.h:3542
- `AVCaptureDeviceFormat.isMultiCamSupported` (`multiCamSupported`) — this format runs sustainably in multi-cam — **API_UNAVAILABLE(macos)** — source: AVCaptureSession.h:841 (doc), AVCaptureDevice.h:3535
- `AVCaptureDevice.isVirtualDevice` — device composed of ≥2 physical constituents — source: AVCaptureDevice.h
- `AVCaptureDevice.constituentDevices` — `[AVCaptureDevice]` physical sub-devices — source: AVCaptureDevice.h
- DELTA: feature-combination feasibility on macOS is impoverished — no multi-cam, no `AVCaptureDataOutputSynchronizer`, no depth-format pairing; the macOS ceiling is single-cam single-format.

### hot-plug
- `AVCaptureDeviceWasConnectedNotification` (`AVCaptureDevice.wasConnectedNotification`) — device became available — source: AVCaptureDevice.h
- `AVCaptureDeviceWasDisconnectedNotification` (`AVCaptureDevice.wasDisconnectedNotification`) — device removed — source: AVCaptureDevice.h
- KVO on `AVCaptureDeviceDiscoverySession.devices` — recommended hot-plug path (esp. iOS audio route quirks) — source: AVCaptureDeviceDiscoverySession.h
- `AVCaptureDevice.subjectAreaDidChangeNotification` (`AVCaptureDeviceSubjectAreaDidChangeNotification`) — subject-area change (3A re-trigger hint) — source: AVCaptureDevice.h

### device↔mic association
- No explicit "this camera's mic" property exists on `AVCaptureDevice`. Association is implicit: a UVC/Continuity device exposes its audio as a separate `AVCaptureDevice` of type `Microphone`/`External`; you correlate by `uniqueID`/`modelID`/`manufacturer` or `transportType`, or add both as inputs. `?` confirm whether any `companion`-style audio accessor exists beyond `companionDeskViewCamera` (none found in headers).

## topology & streams
- `AVCaptureSession` — capture graph (inputs→connections→outputs) — source: AVCaptureSession.h
- `AVCaptureMultiCamSession` — `AVCaptureSession` subclass running multiple cameras at once — ios(13)+macCatalyst(14)+tvos(17), **API_UNAVAILABLE(macos)** (no native-macOS multi-cam) — source: AVCaptureSession.h
- `AVCaptureMultiCamSession.isMultiCamSupported` (class) — system supports multi-cam — source: AVCaptureSession.h:841
- `AVCaptureMultiCamSession.hardwareCost` (`Float`) — % of hardware budget used by current config — source: AVCaptureSession.h
- `AVCaptureMultiCamSession.systemPressureCost` (`Float`) — system-pressure cost of current config — source: AVCaptureSession.h
- `AVCaptureSession beginConfiguration` / `commitConfiguration` — atomic live reconfiguration batch — source: AVCaptureSession.h
- `AVCaptureSession canAddInput:` / `addInput:` / `removeInput:` — source: AVCaptureSession.h
- `AVCaptureSession addInputWithNoConnections:` — add input without auto-forming connections (manual topology) — source: AVCaptureSession.h
- `AVCaptureSession canAddOutput:` / `addOutput:` / `removeOutput:` — source: AVCaptureSession.h
- `AVCaptureSession addOutputWithNoConnections:` — add output without auto-connections — source: AVCaptureSession.h
- `AVCaptureSession addConnection:` / `canAddConnection:` / `removeConnection:` — manual `AVCaptureConnection` wiring — source: AVCaptureSession.h
- `AVCaptureSession.inputs` / `.outputs` / `.connections` — current topology — source: AVCaptureSession.h
- `AVCaptureSession.sessionPreset` / `canSetSessionPreset:` — source: AVCaptureSession.h
- `AVCaptureSessionPreset*` — `Photo`/`High`/`Medium`/`Low`/`InputPriority`/`Qvga320x240`/`Vga640x480`/`Cif352x288`/`qHD960x540`/`Hd1280x720`/`Hd1920x1080`/`Hd4K3840x2160`/`iFrame960x540`/`iFrame1280x720` — source: AVCaptureSession.h
- `AVCaptureSession.automaticallyConfiguresCaptureDeviceForWideColor` — auto wide-gamut — source: AVCaptureSession.h
- `AVCaptureConnection` — link between input ports and an output (or preview layer) — source: AVCaptureSession.h
- `AVCaptureConnection connectionWithInputPorts:output:` — manual connection — source: AVCaptureSession.h
- `AVCaptureConnection connectionWithInputPort:videoPreviewLayer:` — port→preview connection — source: AVCaptureSession.h
- `AVCaptureConnection.isEnabled` / `.isActive` — source: AVCaptureSession.h
- `AVCaptureConnection.audioChannels` — `[AVCaptureAudioChannel]` (level metering) — source: AVCaptureSession.h
- `AVCaptureInputPort` (`AVCaptureInput.Port`) — a single media stream off an input — source: AVCaptureInput.h
- `AVCaptureInputPort.mediaType` / `.formatDescription` / `.isEnabled` — source: AVCaptureInput.h
- `AVCaptureInputPort.sourceDeviceType` — constituent source's device type (split a virtual device's ports) — source: AVCaptureInput.h
- `AVCaptureInputPort.sourceDevicePosition` — constituent source's position — source: AVCaptureInput.h
- `AVCaptureInputPort.clock` (`CMClock`) — port capture clock for cross-output sync — source: AVCaptureInput.h

### logical/virtual device constituents & switchover
- `AVCaptureDevice.virtualDeviceSwitchOverVideoZoomFactors` — `[NSNumber]` zoom factors at which a virtual device switches constituent — ios(13)+macCatalyst(14)+tvos(17), **API_UNAVAILABLE(macos)** — source: AVCaptureDevice.h:785
- `AVCaptureDevice.dualCameraSwitchOverVideoZoomFactor` — DEPRECATED→virtualDeviceSwitchOver…, ios(11,13) — source: AVCaptureDevice.h:1979
- `AVCaptureDevice.primaryConstituentDevice` — physical device currently primary — source: AVCaptureDevice.h
- `AVCaptureDevice.activePrimaryConstituentDevice` — active primary constituent — macos(12)+ios(15), BOTH — source: AVCaptureDevice.h:849
- `AVCaptureDevice.supportedFallbackPrimaryConstituentDevices` — devices selectable as fallback for a focus/light-limited long-focal primary — macos(12)+ios(15) — source: AVCaptureDevice.h
- `AVCaptureDevice.fallbackPrimaryConstituentDevices` — configured fallback set — source: AVCaptureDevice.h
- `AVCaptureDevice setPrimaryConstituentDeviceSwitchingBehavior:restrictedSwitchingBehaviorConditions:` — set switchover behavior + restriction conditions — macos(12)+ios(15) — source: AVCaptureDevice.h
- `AVCaptureDevice.primaryConstituentDeviceSwitchingBehavior` / `.activePrimaryConstituentDeviceSwitchingBehavior` — source: AVCaptureDevice.h
- `AVCaptureDevice.activePrimaryConstituentDeviceRestrictedSwitchingBehaviorConditions` — active restriction conditions — source: AVCaptureDevice.h
- `AVCapturePrimaryConstituentDeviceSwitchingBehavior` (enum `Unsupported`/`Auto`/`Restricted`/`Locked`) — PROTOCOL-style closed switchover modes — source: AVCaptureDevice.h
- `AVCapturePrimaryConstituentDeviceRestrictedSwitchingBehaviorConditions` (OptionSet: `…VideoZoomChanged`/`…FocusModeChanged`/`…ExposureModeChanged`) — source: AVCaptureDevice.h
- `AVCaptureMovieFileOutput.isPrimaryConstituentDeviceSwitchingBehaviorForRecordingEnabled` — lock switchover during recording — source: AVCaptureFileOutput.h
- NOTE: `constituentDeviceSwitchOverVideoZoomFactors` (named in some references) does NOT exist — only `virtualDeviceSwitchOverVideoZoomFactors`.

### synchronized multi-output
- `AVCaptureDataOutputSynchronizer` — time-aligns multiple data outputs (video+depth+metadata) — ios(11)+macCatalyst(14)+tvos(17), **API_UNAVAILABLE(macos)** (major macOS gap) — source: AVCaptureDataOutputSynchronizer.h
- `AVCaptureDataOutputSynchronizer initWithDataOutputs:` / `.dataOutputs` / `setDelegate:queue:` — source: same
- `AVCaptureDataOutputSynchronizerDelegate dataOutputSynchronizer:didOutputSynchronizedDataCollection:` — delivery — source: same
- `AVCaptureSynchronizedDataCollection` — keyed bundle for one instant; `synchronizedDataForCaptureOutput:`, subscript, `count` — source: same
- `AVCaptureSynchronizedData` — abstract base; `.timestamp` (`CMTime`) — source: same
- `AVCaptureSynchronizedSampleBufferData` — `.sampleBuffer`, `.sampleBufferWasDropped`, `.droppedReason` — source: same
- `AVCaptureSynchronizedDepthData` — `.depthData`, `.depthDataWasDropped`, `.droppedReason` — source: same
- `AVCaptureSynchronizedMetadataObjectData` — `.metadataObjects` — source: same

## fine-grained control
- `AVCaptureDevice lockForConfiguration:` / `unlockForConfiguration` — exclusive hardware-config access (required before any setter) — source: AVCaptureDevice.h
- `AVCaptureDevice.isSubjectAreaChangeMonitoringEnabled` — monitor subject area for 3A re-trigger — source: AVCaptureDevice.h

### focus
- `AVCaptureDevice.focusMode` (`AVCaptureFocusMode`: `Locked`/`AutoFocus`/`ContinuousAutoFocus`) — source: AVCaptureDevice.h
- `AVCaptureDevice isFocusModeSupported:` — source: AVCaptureDevice.h
- `AVCaptureDevice.focusPointOfInterest` / `.isFocusPointOfInterestSupported` — source: AVCaptureDevice.h
- `AVCaptureDevice.isSmoothAutoFocusEnabled` / `.isSmoothAutoFocusSupported` — source: AVCaptureDevice.h
- `AVCaptureDevice.autoFocusRangeRestriction` (`AVCaptureAutoFocusRangeRestriction`: `None`/`Near`/`Far`) / `.isAutoFocusRangeRestrictionSupported` — source: AVCaptureDevice.h
- `AVCaptureDevice.lensPosition` (0…1) — source: AVCaptureDevice.h
- `AVCaptureDevice setFocusModeLockedWithLensPosition:completionHandler:` — lock lens at a position — source: AVCaptureDevice.h
- `AVCaptureLensPositionCurrent` — sentinel "leave lens where it is" — source: AVCaptureDevice.h
- `AVCaptureDevice.isLockingFocusWithCustomLensPositionSupported` — source: AVCaptureDevice.h
- `AVCaptureDevice.minimumFocusDistance` (mm) — source: AVCaptureDevice.h
- `AVCaptureDevice.isAdjustingFocus` — currently hunting — source: AVCaptureDevice.h
- `AVCaptureDevice.isFaceDrivenAutoFocusEnabled` / `.automaticallyAdjustsFaceDrivenAutoFocusEnabled` — source: AVCaptureDevice.h
- `AVCaptureDevice.focusRectOfInterest` / `.isFocusRectOfInterestSupported` / `.minFocusRectOfInterestSize` / `defaultRectForFocusPointOfInterest` `?` — rect-based AF (obscure; confirm exact names on Format/Device pages) — source: AVCaptureDevice.h

### exposure
- `AVCaptureDevice.exposureMode` (`AVCaptureExposureMode`: `Locked`/`AutoExpose`/`ContinuousAutoExposure`/`Custom`) — source: AVCaptureDevice.h
- `AVCaptureDevice isExposureModeSupported:` — source: AVCaptureDevice.h
- `AVCaptureDevice.exposurePointOfInterest` / `.isExposurePointOfInterestSupported` — source: AVCaptureDevice.h
- `AVCaptureDevice setExposureModeCustomWithDuration:ISO:completionHandler:` — custom shutter+ISO — source: AVCaptureDevice.h
- `AVCaptureDevice.exposureDuration` (`CMTime`) / `.ISO` (`Float`) — current values — source: AVCaptureDevice.h
- `AVCaptureExposureDurationCurrent` / `AVCaptureISOCurrent` — sentinels for custom setter — source: AVCaptureDevice.h
- `AVCaptureDevice.activeMaxExposureDuration` — clamp on AE max shutter — source: AVCaptureDevice.h
- `AVCaptureDevice.exposureTargetBias` / `setExposureTargetBias:completionHandler:` / `.minExposureTargetBias` / `.maxExposureTargetBias` (EV) — source: AVCaptureDevice.h
- `AVCaptureExposureTargetBiasCurrent` — sentinel — source: AVCaptureDevice.h
- `AVCaptureDevice.exposureTargetOffset` — metered offset from target (EV) — source: AVCaptureDevice.h
- `AVCaptureDevice.isAdjustingExposure` — source: AVCaptureDevice.h
- `AVCaptureDevice.isFaceDrivenAutoExposureEnabled` / `.automaticallyAdjustsFaceDrivenAutoExposureEnabled` — source: AVCaptureDevice.h
- `AVCaptureDevice.exposureRectOfInterest` / `.isExposureRectOfInterestSupported` / `.minExposureRectOfInterestSize` `?` — rect-based AE (obscure; confirm) — source: AVCaptureDevice.h
- ISO/duration ranges on format: `AVCaptureDeviceFormat.minISO`/`.maxISO`/`.minExposureDuration`/`.maxExposureDuration` — source: AVCaptureDevice.h

### white balance
- `AVCaptureDevice.whiteBalanceMode` (`AVCaptureWhiteBalanceMode`: `Locked`/`AutoWhiteBalance`/`ContinuousAutoWhiteBalance`) — source: AVCaptureDevice.h
- `AVCaptureDevice isWhiteBalanceModeSupported:` / `.isAdjustingWhiteBalance` — source: AVCaptureDevice.h
- `AVCaptureDevice.deviceWhiteBalanceGains` (`AVCaptureWhiteBalanceGains`) — current RGB gains — source: AVCaptureDevice.h
- `AVCaptureDevice.grayWorldDeviceWhiteBalanceGains` — gray-world neutral gains — source: AVCaptureDevice.h
- `AVCaptureDevice.maxWhiteBalanceGain` — per-channel ceiling — source: AVCaptureDevice.h
- `AVCaptureDevice setWhiteBalanceModeLockedWithDeviceWhiteBalanceGains:completionHandler:` — lock to gains — source: AVCaptureDevice.h
- `AVCaptureDevice.isLockingWhiteBalanceWithCustomDeviceGainsSupported` — source: AVCaptureDevice.h
- `AVCaptureDevice chromaticityValuesForDeviceWhiteBalanceGains:` (`AVCaptureWhiteBalanceChromaticityValues`, CIE xy) — source: AVCaptureDevice.h
- `AVCaptureDevice temperatureAndTintValuesForDeviceWhiteBalanceGains:` (`AVCaptureWhiteBalanceTemperatureAndTintValues`) — source: AVCaptureDevice.h
- `AVCaptureDevice deviceWhiteBalanceGainsForChromaticityValues:` / `…ForTemperatureAndTintValues:` — inverse conversions — source: AVCaptureDevice.h
- `AVCaptureWhiteBalanceGains` (struct `redGain`/`greenGain`/`blueGain`) — source: AVCaptureDevice.h
- `setWhiteBalanceModeLockedWithTemperatureAndTintValues:completionHandler:` `?` — lock by temp/tint directly — `[>pin]` iOS 18 (pre-18: convert temp/tint→gains) — source: AVCaptureDevice.h

### zoom
- `AVCaptureDevice.videoZoomFactor` — centered crop/enlarge factor — source: AVCaptureDevice.h
- `AVCaptureDevice.minAvailableVideoZoomFactor` / `.maxAvailableVideoZoomFactor` — current-config bounds — source: AVCaptureDevice.h
- `AVCaptureDevice rampToVideoZoomFactor:withRate:` — smooth zoom transition — source: AVCaptureDevice.h
- `AVCaptureDevice cancelVideoZoomRamp` / `.isRampingVideoZoom` (`rampingVideoZoom`) — source: AVCaptureDevice.h
- `AVCaptureDeviceFormat.videoMaxZoomFactor` — format ceiling — source: AVCaptureDevice.h
- `AVCaptureDeviceFormat.videoZoomFactorUpscaleThreshold` — factor beyond which digital upscaling kicks in — **API_UNAVAILABLE(macos)** (Format-only, NOT on device) — source: AVCaptureDevice.h:3269
- `AVCaptureDevice.displayVideoZoomFactorMultiplier` — UI display multiplier — `[>pin]` iOS 18 — source: AVCaptureDevice.h
- `AVCaptureDeviceFormat.secondaryNativeResolutionZoomFactors` — additional native (non-upscaled) zoom stops — source: AVCaptureDevice.h

### torch / flash
- `AVCaptureDevice.torchMode` (`AVCaptureTorchMode`: `Off`/`On`/`Auto`) / `isTorchModeSupported:` — source: AVCaptureDevice.h
- `AVCaptureDevice.hasTorch` / `.isTorchAvailable` / `.isTorchActive` / `.torchLevel` — source: AVCaptureDevice.h
- `AVCaptureDevice setTorchModeOnWithLevel:error:` — torch at a level (0…1 or sentinel) — source: AVCaptureDevice.h:1056
- `AVCaptureMaxAvailableTorchLevel` (Swift `AVCaptureDevice.maxAvailableTorchLevel`) — sentinel for max level — macos(10.15)+ios(6) — source: AVCaptureDevice.h:979. NOTE: `torchActiveLevel` does NOT exist.
- `AVCaptureDevice.hasFlash` / `.isFlashAvailable` — source: AVCaptureDevice.h
- `AVCaptureDevice.flashMode` / `isFlashModeSupported:` / `.isFlashActive` — DEPRECATED (flash now per-shot on `AVCapturePhotoSettings.flashMode`; live support via `AVCapturePhotoOutput.supportedFlashModes`) — source: AVCaptureDevice.h
- `AVCaptureFlashMode` (`Off`/`On`/`Auto`) — largely no-op on macOS (no flash hardware) — source: AVCaptureDevice.h

### HDR / low-light / tone / distortion
- `AVCaptureDevice.isVideoHDREnabled` / `.automaticallyAdjustsVideoHDREnabled` — per-frame EDR video — source: AVCaptureDevice.h
- `AVCaptureDeviceFormat.isVideoHDRSupported` — source: AVCaptureDevice.h
- `AVCaptureDevice.isGlobalToneMappingEnabled` / `AVCaptureDeviceFormat.isGlobalToneMappingSupported` — source: AVCaptureDevice.h
- `AVCaptureDevice.isLowLightBoostSupported` / `.isLowLightBoostEnabled` / `.automaticallyEnablesLowLightBoostWhenAvailable` — source: AVCaptureDevice.h
- `AVCaptureDevice.isGeometricDistortionCorrectionEnabled` / `.isGeometricDistortionCorrectionSupported` — source: AVCaptureDevice.h
- `AVCaptureDeviceFormat.geometricDistortionCorrectedVideoFieldOfView` — corrected FOV — source: AVCaptureDevice.h
- `AVCaptureColorSpace` enum: `sRGB`/`P3_D65`/`HLG_BT2020`/`AppleLog` — source: AVCaptureDevice.h:2176
  - DELTA: `AVCaptureColorSpace_AppleLog` is ios(17)+macCatalyst(17)+tvos(17), **API_UNAVAILABLE(macos)** — Apple Log capture is iOS-only — source: AVCaptureDevice.h:2176

### frame-rate ranges & active durations
- `AVCaptureDevice.activeVideoMinFrameDuration` / `.activeVideoMaxFrameDuration` (`CMTime`) — FPS throttle (set both equal for fixed/high FPS); also the system-pressure mitigation lever — source: AVCaptureDevice.h
- `AVCaptureDeviceFormat.videoSupportedFrameRateRanges` — `[AVFrameRateRange]` — source: AVCaptureDevice.h
- `AVFrameRateRange` — `.minFrameRate`/`.maxFrameRate`/`.minFrameDuration`/`.maxFrameDuration` — source: AVCaptureDevice.h
- `AVCaptureDevice.isAutoVideoFrameRateEnabled` / `AVCaptureDeviceFormat.isAutoVideoFrameRateSupported` — `[>pin]` iOS 18 — source: AVCaptureDevice.h

## mechanical controls
- none on this surface — AVFoundation exposes NO mechanical pan/tilt (PTZ). Only digital/computational equivalents exist: `videoZoomFactor`/`rampToVideoZoomFactor:withRate:` (digital crop-zoom), Center Stage (software pan/tilt/zoom crop), and `AVCaptureDeviceRotationCoordinator` (orientation *sensing*, not actuation). No UVC-PTZ, `pan`, or `tilt` control is published. Verified against the full `AVCaptureDevice.h` surface. `?` mechanical PTZ on UVC conference cams would need a private/IOKit USB-video-class path outside AVFoundation.

## capture modes
- `AVCapturePhotoOutput` — still-image output — source: AVCapturePhotoOutput.h
- `AVCapturePhotoOutput capturePhotoWithSettings:delegate:` — fire a still — source: same
- `AVCapturePhotoSettings` — per-shot config — source: AVCapturePhotoSettings.h
- `AVCapturePhotoOutput.maxPhotoDimensions` (`CMVideoDimensions`) + `AVCapturePhotoSettings.maxPhotoDimensions` — high-res still dims (replaces deprecated `isHighResolutionCaptureEnabled`/`isHighResolutionPhotoEnabled`) — source: AVCapturePhotoOutput.h
- `AVCapturePhotoQualityPrioritization` (`Speed`/`Balanced`/`Quality`) + `.maxPhotoQualityPrioritization` (output) + `.photoQualityPrioritization` (settings) — source: AVCapturePhotoOutput.h
- `AVCaptureDeviceFormat.isHighPhotoQualitySupported` / `.isHighestPhotoQualitySupported` — source: AVCaptureDevice.h

### RAW + ProRAW
- `AVCapturePhotoOutput.availableRawPhotoPixelFormatTypes` (`[OSType]`) — source: AVCapturePhotoOutput.h
- `AVCapturePhotoOutput supportedRawPhotoPixelFormatTypesForFileType:` — source: same
- `AVCapturePhotoOutput isAppleProRAWSupported` / `isAppleProRAWEnabled` — ProRAW (iOS-only) — source: same
- `AVCapturePhotoOutput isAppleProRAWPixelFormat:` / `isBayerRAWPixelFormat:` — classify an OSType — source: same
- `AVCapturePhotoOutput.availableRawPhotoCodecTypes` / `supportedRawPhotoCodecTypesForRawPhotoPixelFormatType:fileType:` — source: same
- `AVCapturePhotoSettings.rawPhotoPixelFormatType` / `.rawFileType` + RAW inits (`photoSettingsWithRawPixelFormatType:…`) — source: AVCapturePhotoSettings.h
- Bayer RAW CV formats: `kCVPixelFormatType_14Bayer_GRBG`/`_RGGB`/`_BGGR`/`_GBRG` — 14-bit Bayer in 16-bit LE — source: CoreVideo CVPixelBuffer.h, https://developer.apple.com/documentation/corevideo/pixel-format-identifiers

### codecs / file types / HEIC-HEVC
- `AVCapturePhotoOutput.availablePhotoCodecTypes` / `supportedPhotoCodecTypesForFileType:` — source: AVCapturePhotoOutput.h
- `AVCapturePhotoOutput.availablePhotoFileTypes` / `.availableRawPhotoFileTypes` — heic/jpeg/dng — source: same
- `AVCapturePhotoOutput.availablePhotoPixelFormatTypes` / `supportedPhotoPixelFormatTypesForFileType:` — source: same
- `AVVideoCodecType` — `HEVC`/`HEVCWithAlpha`/`H264`/`JPEG`/`JPEGXL`/`ProRes422`/`ProRes422HQ`/`ProRes422LT`/`ProRes422Proxy`/`ProRes4444`/`AppleProRes4444XQ`/`ProRESRAW`/`ProRESRAWHQ` — source: AVVideoSettings.h; `AVFileTypeHEIC` is the file type, distinct from the HEVC codec — source: AVMediaFormat.h

### zero-shutter-lag / responsive / deferred
- `AVCapturePhotoOutput.isZeroShutterLagSupported` / `.isZeroShutterLagEnabled` — source: AVCapturePhotoOutput.h
- `AVCapturePhotoOutput.isResponsiveCaptureSupported` / `.isResponsiveCaptureEnabled` — overlapped capture — source: same
- `AVCapturePhotoOutput.isFastCapturePrioritizationSupported` / `.isFastCapturePrioritizationEnabled` — source: same
- `AVCapturePhotoOutput.captureReadiness` (`AVCapturePhotoOutputCaptureReadiness`) — shutter-button readiness — source: same
- `AVCapturePhotoOutput.isAutoDeferredPhotoDeliverySupported` / `.isAutoDeferredPhotoDeliveryEnabled` — ios(17), **API_UNAVAILABLE(macos)** — source: AVCapturePhotoOutput.h:382
- `AVCaptureDeferredPhotoProxy` — lightweight proxy resolved later via `PHImageManager`/`PHAsset` — source: AVCapturePhotoOutput.h:2291
- `captureOutput:didFinishCapturingDeferredPhotoProxy:error:` (`photoOutput(_:didFinishCapturingDeferredPhotoProxy:error:)`) — proxy delivery callback (NOT a property) — ios(17) — source: AVCapturePhotoOutput.h:1071
- `AVCapturePhotoOutput.deferredPhotoProxyDimensions` — resolved proxy dims — source: AVCapturePhotoOutput.h:1903

### bracketed / constituent / per-shot
- `AVCapturePhotoBracketSettings` — multi-image bracket; `.bracketedSettings`, `.isLensStabilizationEnabled` — source: AVCapturePhotoBracketSettings.h
- `AVCaptureBracketedStillImageSettings` (abstract) — source: AVCaptureStillImageOutput.h
- `AVCaptureAutoExposureBracketedStillImageSettings autoExposureSettingsWithExposureTargetBias:` — source: same
- `AVCaptureManualExposureBracketedStillImageSettings manualExposureSettingsWithExposureDuration:ISO:` — source: same
- `AVCapturePhotoOutput.maxBracketedCapturePhotoCount` / `.isLensStabilizationDuringBracketedCaptureSupported` — source: AVCapturePhotoOutput.h
- `AVCaptureLensStabilizationStatus` (`Off`/`Active`/`OutOfRange`/`Unavailable`) — OIS state during bracket — source: AVCaptureDevice.h
- `AVCapturePhotoOutput.isVirtualDeviceConstituentPhotoDeliverySupported` / `.isVirtualDeviceConstituentPhotoDeliveryEnabled` + `AVCapturePhotoSettings.virtualDeviceConstituentPhotoDeliveryEnabledDevices` — per-lens stills from a virtual device — source: AVCapturePhotoOutput.h
- `AVCapturePhotoOutput.isVirtualDeviceFusionSupported` / `AVCapturePhotoSettings.isAutoVirtualDeviceFusionEnabled` — source: same (older `isConstituentPhotoDeliveryEnabled`/`isDualCameraDualPhotoDeliveryEnabled`/`isAutoDualCameraFusionEnabled` are DEPRECATED)
- `AVCapturePhotoSettings.flashMode` / `.isAutoRedEyeReductionEnabled` / `.previewPhotoFormat` / `.embeddedThumbnailPhotoFormat` / `.rawEmbeddedThumbnailPhotoFormat` — source: AVCapturePhotoSettings.h
- `AVCapturePhotoOutput.supportedFlashModes` / `.isAutoRedEyeReductionSupported` / `.isFlashScene` / `.photoSettingsForSceneMonitoring` — source: AVCapturePhotoOutput.h

### movie / data file output
- `AVCaptureMovieFileOutput` — QuickTime A/V file (encoding belongs to media domain, but the output exists) — source: AVCaptureFileOutput.h
- `AVCaptureMovieFileOutput startRecordingToOutputFileURL:recordingDelegate:` / `stopRecording` / `.availableVideoCodecTypes` / `setOutputSettings:forConnection:` / `.metadata` — source: same
- `AVCaptureFileOutput` (abstract base) / `AVCaptureAudioDataOutput` — source: same

### high frame rate / spatial / cinematic / ProRes-log
- High-FPS / slow-mo: no dedicated symbol — select an `activeFormat` with high-`maxFrameRate` range and set `activeVideoMinFrameDuration == activeVideoMaxFrameDuration`.
- `AVCaptureDeviceFormat.isSpatialVideoCaptureSupported` (device-format gate exists at the pin) — source: AVCaptureDevice.h:3555
- `AVCaptureMovieFileOutput.isSpatialVideoCaptureSupported` / `.isSpatialVideoCaptureEnabled` — **`[>pin]` macos(15)+ios(18)** (requires Cinematic stabilization) — source: AVCaptureFileOutput.h:577,592
- `AVCaptureDevice.spatialCaptureDiscomfortReasons` / `AVSpatialCaptureDiscomfortReason` — scene suitability — source: AVCaptureDevice.h
- `AVCaptureDeviceInput.isCinematicVideoCaptureSupported` / `.isCinematicVideoCaptureEnabled` (on INPUT, not device) + `setCinematicVideoTrackingFocus…`/`CinematicVideoFocusMode` — **`[>pin]` macos(26)+ios(26)** — source: AVCaptureInput.h:430,439
- ProRes/log: codec selection (`AVVideoCodecType.ProRes*`) + `activeColorSpace = AppleLog`; no separate "log mode" property. `AVCaptureDeviceFormat.isVideoBinned` — format produces binned video — source: AVCaptureDevice.h

## depth/3D/calibration
- `AVCaptureDepthDataOutput` — streams `AVDepthData` — source: AVCaptureDepthDataOutput.h
- `AVCaptureDepthDataOutput.isFilteringEnabled` — temporal/spatial smoothing + hole-fill — source: same
- `AVCaptureDepthDataOutput.alwaysDiscardsLateDepthData` — back-pressure — source: same
- `AVCaptureDepthDataOutput setDelegate:callbackQueue:` — source: same
- `AVCaptureDepthDataOutputDelegate depthDataOutput:didOutputDepthData:timestamp:connection:` — depth delivery — source: same
- `AVCaptureDepthDataOutputDelegate depthDataOutput:didDropDepthData:timestamp:connection:reason:` — depth drop — source: same
- `AVCaptureOutputDataDroppedReason` — `LateData`/`OutOfBuffers`/`Discontinuity` — source: AVCaptureOutput.h
- `AVCaptureDevice.activeDepthDataFormat` — chosen depth format — ios(11)+macCatalyst(14)+tvos(17), **API_UNAVAILABLE(macos)** — source: AVCaptureDevice.h:2212
- `AVCaptureDevice.activeDepthDataMinFrameDuration` — depth FPS — source: AVCaptureDevice.h
- `AVDepthData` — a depth/disparity map + metadata — available macos+ios+tvos (the class), though depth-producing hardware is iOS-only — source: AVDepthData.h
- `AVDepthData.depthDataType` (`OSType`) / `.depthDataMap` (`CVPixelBuffer`) — source: same
- `AVDepthData.depthDataAccuracy` (`AVDepthDataAccuracy` `Relative`/`Absolute`) — source: same
- `AVDepthData.isDepthDataFiltered` / `.depthDataQuality` (`AVDepthDataQuality` `Low`/`High`) — source: same
- `AVDepthData.availableDepthDataTypes` / `depthDataByConvertingToDepthDataType:` (`converting(toDepthDataType:)`) — source: same
- `AVDepthData.cameraCalibrationData` (`AVCameraCalibrationData?`) — source: same
- `AVDepthData depthDataByApplyingExifOrientation:` (`applyingExifOrientation`) — source: same
- Depth/disparity CV formats: `kCVPixelFormatType_DepthFloat16`(`'hdep'`)/`_DepthFloat32`(`'fdep'`)/`_DisparityFloat16`(`'hdis'`)/`_DisparityFloat32`(`'fdis'`) — source: CoreVideo CVPixelBuffer.h
- `AVCapturePhotoOutput.isDepthDataDeliverySupported` / `.isDepthDataDeliveryEnabled` — depth in stills (iOS-only hardware) — source: AVCapturePhotoOutput.h
- `AVCapturePhotoSettings.isDepthDataDeliveryEnabled` / `.embedsDepthDataInPhoto` / `.isDepthDataFiltered` — source: AVCapturePhotoSettings.h
- `AVCapturePhoto.depthData` — depth on the delivered photo — source: AVCapturePhoto.h
- `AVCapturePhotoOutput.isCameraCalibrationDataDeliverySupported` / `AVCapturePhotoSettings.isCameraCalibrationDataDeliveryEnabled` — needs constituent-photo delivery ON + distortion-correction OFF — source: AVCapturePhotoOutput.h
- `AVCameraCalibrationData` — intrinsics/extrinsics/distortion; class on macos(10.13)+ios(11), BOTH — source: AVCameraCalibrationData.h
  - `.intrinsicMatrix` (`matrix_float3x3`) — source: AVCameraCalibrationData.h:51
  - `.intrinsicMatrixReferenceDimensions` (`CGSize`) — source: :61
  - `.extrinsicMatrix` (`matrix_float4x3`) — source: :76
  - `.pixelSize` (`float`, mm) — source: :83
  - `.lensDistortionLookupTable` (`NSData` of `Float`) / `.inverseLensDistortionLookupTable` (`NSData`) — source: :95,:107
  - `.lensDistortionCenter` (`CGPoint`) — source: :119

### portrait / semantic mattes (iOS-only)
- `AVPortraitEffectsMatte` — grayscale foreground matte (`kCVPixelFormatType_OneComponent8`) — ios(12), **API_UNAVAILABLE(macos)** — source: AVPortraitEffectsMatte.h
- `AVPortraitEffectsMatte.mattingImage` (`CVPixelBuffer`) / `.pixelFormatType` / `applyingExifOrientation:` — source: same
- `AVCapturePhotoOutput.isPortraitEffectsMatteDeliverySupported` / `.isPortraitEffectsMatteDeliveryEnabled` + `AVCapturePhotoSettings.embedsPortraitEffectsMatteInPhoto` + `AVCapturePhoto.portraitEffectsMatte` — source: AVCapturePhotoOutput.h
- `AVSemanticSegmentationMatte` — per-class soft matte — ios(13), **API_UNAVAILABLE(macos)** (the matte *types* are also defined for macos 10.15+ as constants but production needs iOS hardware) — source: AVSemanticSegmentationMatte.h
- `AVSemanticSegmentationMatteType` constants: `Skin`/`Hair`/`Teeth` (macos 10.15 / ios 13) and `Glasses` (macos 11 / **ios 14.1** — later) — source: AVSemanticSegmentationMatte.h:29,35,41,47
- `AVSemanticSegmentationMatte.matteType` / `.mattingImage` / `.pixelFormatType` / `applyingExifOrientation:` — source: same
- `AVCapturePhotoOutput.availableSemanticSegmentationMatteTypes` / `.enabledSemanticSegmentationMatteTypes` + `AVCapturePhotoSettings.embedsSemanticSegmentationMattesInPhoto` + `AVCapturePhoto semanticSegmentationMatteForType:` — source: AVCapturePhotoOutput.h
- DELTA: ALL depth/LiDAR/TrueDepth/portrait-matte/semantic-matte *production* is iOS/iPadOS; macOS built-in/Continuity cameras vend no depth device types. `AVDepthData`/`AVCameraCalibrationData` *classes* compile on macOS.

## live effects
- `AVCaptureDevice.centerStageControlMode` (class, `AVCaptureCenterStageControlMode` `User`/`App`/`Cooperative`) — auto-framing control regime — source: AVCaptureDevice.h
- `AVCaptureDevice.centerStageEnabled` (class, `isCenterStageEnabled`) — on/off — source: AVCaptureDevice.h
- `AVCaptureDevice.isCenterStageActive` (`centerStageActive`, instance read-only) — currently framing — source: AVCaptureDevice.h
- `AVCaptureDeviceFormat.isCenterStageSupported` / `.videoMinZoomFactorForCenterStage` / `.videoMaxZoomFactorForCenterStage` — source: AVCaptureDevice.h
  - DELTA: Center Stage works on macOS (Continuity Camera) AND iOS — macos(12.3)+ios(14.5), BOTH.
- Reactions — macos(14)+ios(17), BOTH (macOS always-on; iOS needs Info.plist `NSCameraReactionEffectsEnabled` opt-in):
  - `AVCaptureReactionType` (NS_TYPED_ENUM) constants: `AVCaptureReactionTypeThumbsUp`/`ThumbsDown`/`Balloons`/`Heart`/`Fireworks`/`Rain`/`Confetti`/`Lasers` — all macos(14)+ios(17) — source: AVCaptureReactions.h:29-78
  - `AVCaptureReactionSystemImageNameForType(AVCaptureReactionType)` (`AVCaptureReactionType.systemImageName`) — SF Symbol for UI — source: AVCaptureReactions.h:85
  - `AVCaptureDevice.reactionEffectsEnabled` (class) / `.reactionEffectGesturesEnabled` (class) — source: AVCaptureDevice.h:2443,2455
  - `AVCaptureDevice.canPerformReactionEffects` — resources available — source: AVCaptureDevice.h:2473
  - `AVCaptureDevice.availableReactionTypes` (`Set<AVCaptureReactionType>`) — source: AVCaptureDevice.h:2476
  - `AVCaptureDevice performEffectForReaction:` — fire a reaction in code — source: AVCaptureDevice.h
  - `AVCaptureDevice.reactionEffectsInProgress` (`[AVCaptureReactionEffectState]`) — source: AVCaptureDevice.h
  - `AVCaptureReactionEffectState` (class) `.reactionType` / `.startTime` / `.endTime` (`CMTime`) `?` — confirm start/end exact names — source: AVCaptureReactions.h:106
  - `AVCaptureDeviceFormat.reactionEffectsSupported` / `.videoFrameRateRangeForReactionEffectsInProgress` — source: AVCaptureDevice.h
- System video effects (READ/observe + show-system-UI only; NOT app-toggleable):
  - `AVCaptureDevice.isPortraitEffectActive` (read-only) — background-blur portrait — source: AVCaptureDevice.h
  - `AVCaptureDevice.isStudioLightActive` (read-only) — source: AVCaptureDevice.h
  - `AVCaptureDevice.isBackgroundReplacementActive` (read-only) — source: AVCaptureDevice.h
  - `AVCaptureDeviceFormat.isPortraitEffectSupported` / `.isStudioLightSupported` `?` / `.isBackgroundReplacementSupported` — source: AVCaptureDevice.h
  - `AVCaptureDevice.isBackgroundReplacementEnabled` (class, app feature opt-in) — source: AVCaptureDevice.h
  - `AVCaptureDevice showSystemUserInterface:` (class) — present Control-Center video/mic UI — source: AVCaptureDevice.h
  - `AVCaptureSystemUserInterface` (`VideoEffects`/`MicrophoneModes`) — source: AVCaptureDevice.h
  - NOTE: there is no `isPortraitEffectEnabled`/`isStudioLightEnabled` SETTER — Portrait/Studio Light/Background Replacement are user/system-controlled; only Center Stage + Reactions have app-control surfaces.
- subject-tracking / automatic framing == Center Stage; there is no separate ROI-tracking output beyond it + the salient-object metadata (see metadata).

## frame memory
- `AVCaptureVideoDataOutput` — uncompressed/compressed video sample buffers — source: AVCaptureVideoDataOutput.h
- `AVCaptureVideoDataOutput setSampleBufferDelegate:queue:` (serial queue) — source: same
- `AVCaptureVideoDataOutput.videoSettings` (key `kCVPixelBufferPixelFormatTypeKey`) — source: same
- `AVCaptureVideoDataOutput.availableVideoCVPixelFormatTypes` / `.availableVideoCodecTypes` — source: same
- `AVCaptureVideoDataOutput recommendedVideoSettingsForVideoCodecType:assetWriterOutputFileType:` / `recommendedVideoSettingsForAssetWriter:` — source: same
- `AVCaptureVideoDataOutput.alwaysDiscardsLateVideoFrames` — back-pressure (true ⇒ depth-1 freshest-frame) — source: same
- `AVCaptureVideoDataOutputSampleBufferDelegate captureOutput:didOutputSampleBuffer:fromConnection:` — frame — source: same
- `AVCaptureVideoDataOutputSampleBufferDelegate captureOutput:didDropSampleBuffer:fromConnection:` — drop (buffer has timing+format+reason, no image) — source: same
- `CMSampleBufferGetImageBuffer` — `CMSampleBuffer`→`CVImageBuffer` — source: CoreMedia CMSampleBuffer.h
- `CVPixelBufferGetIOSurface` — backing `IOSurface` for zero-copy — source: CoreVideo CVPixelBuffer.h
- `CVPixelBufferPool` / `CVPixelBufferPoolCreate` — recycled buffer pools — source: CoreVideo CVPixelBufferPool.h
- `kCVPixelBufferIOSurfacePropertiesKey` — required attr for IOSurface-backed buffers — source: CoreVideo CVPixelBuffer.h
- `CVMetalTextureCache` / `CVMetalTextureCacheCreate` / `CVMetalTextureCacheCreateTextureFromImage` / `CVMetalTextureGetTexture` — zero-copy `CVPixelBuffer`→`MTLTexture` — source: CoreVideo CVMetalTextureCache.h
- pixel formats: `kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange`(`'420v'`)/`_420YpCbCr8BiPlanarFullRange`(`'420f'`)/`_420YpCbCr10BiPlanarVideoRange`(`'x420'`,10-bit HDR)/`_32BGRA`/`_OneComponent8`(`'L008'`)/`_OneComponent16Half`/`_OneComponent32Float` + the `_14Bayer_*` family — source: CoreVideo CVPixelBuffer.h, https://developer.apple.com/documentation/corevideo/pixel-format-identifiers
- drop diagnostics (buffer-level, non-propagating → read via `CMGetAttachment`/`CMSampleBufferGetAttachment`, NOT the per-sample array):
  - `kCMSampleBufferAttachmentKey_DroppedFrameReason` + `…DroppedFrameReasonInfo` — source: CoreMedia CMSampleBuffer.h
  - values: `kCMSampleBufferDroppedFrameReason_FrameWasLate` / `_OutOfBuffers` / `_Discontinuity` — source: same

## timing
- `CMSampleBufferGetPresentationTimeStamp` / `…GetDecodeTimeStamp` / `…GetDuration` — per-buffer timing — source: CoreMedia CMSampleBuffer.h
- `CMSampleBufferGetOutputPresentationTimeStamp` / `…GetOutputDecodeTimeStamp` / `…GetOutputDuration` — post-edit timing — source: same
- `CMSampleBufferGetSampleTimingInfoArray` (`CMSampleTimingInfo`: `duration`/`presentationTimeStamp`/`decodeTimeStamp`) — source: same
- `AVCaptureSession.synchronizationClock` (`CMClock?`) — the clock to map all outputs onto — ios(15.4)+macos(12.3), BOTH — source: AVCaptureSession.h
- `AVCaptureSession.masterClock` — DEPRECATED→synchronizationClock — source: AVCaptureSession.h
- `CMClock` / `CMClockGetHostTimeClock` / `CMClockGetTime` — reference clock — source: CoreMedia CMSync.h
- `CMClockMakeHostTimeFromSystemUnits` / `CMClockConvertHostTimeToSystemUnits` — host-units (`mach_absolute_time`) ↔ `CMTime` — source: same
- `CMTimebase` / `CMTimebaseSetRate` / `CMTimebaseCopyMasterClock` — app-controlled timeline — source: same
- `CMSyncGetRelativeRate` / `CMSyncGetRelativeRateAndAnchorTime` / `CMSyncConvertTime` — map a time across two clocks (device clock → session clock) — source: same
- `CMClockOrTimebase` (`CMClockOrTimebaseRef`) — union accepted by sync fns — source: CoreMedia CMSync.h
- camera intrinsics per frame:
  - `kCMSampleBufferAttachmentKey_CameraIntrinsicMatrix` — `matrix_float3x3` as `CFData` on each video buffer — source: CoreMedia CMSampleBuffer.h
  - `AVCaptureConnection.isCameraIntrinsicMatrixDeliveryEnabled` / `.isCameraIntrinsicMatrixDeliverySupported` — opt-in (false if stabilization≠off or ultra-wide; iOS-centric) — source: AVCaptureSession.h
- IMU correlation: no AVFoundation fusion API. Manual — `CMMotionManager` (CoreMotion) sample `CMLogItem.timestamp` (boot-relative `mach_absolute_time`) → convert via `CMClockMakeHostTimeFromSystemUnits` + `CMSyncConvertTime` to `synchronizationClock` to align with buffer PTS. `?` AVFoundation exposes no public per-frame gyro/OIS sample attachment nor rolling-shutter readout-time API.
- SMPTE timecode (via AVAssetWriter path, NOT directly off `AVCaptureMovieFileOutput`):
  - `AVMediaTypeTimecode` (`AVMediaType.timecode`) — timecode track media type — source: AVMediaFormat.h, TN2310
  - `kCMTimeCodeFormatType_TimeCode32` (`tmcd`) / `CMTimeCodeFormatDescriptionCreate` / `kCMTimeCodeFlag_DropFrame` — source: CoreMedia CMTimeCode.h, TN2310 (note: the constant family is `kCMTimeCodeFlag_*`, not `kCMFormatDescriptionTimeCodeFlag`)
  - `AVAssetWriterInput addTrackAssociationWithTrackOfInput:type:` — link timecode↔video track — source: TN2310
  - `AVCaptureTimecode` — (largely legacy) timecode value type — source: AVCaptureTimecode (AVFoundation)
  - `AVAssetWriterInputMetadataAdaptor` + `AVTimedMetadataGroup` — timed-metadata track alternative (`.mov` only) — source: AVFoundation

## metadata
- `AVCaptureMetadataOutput` — vends detected metadata objects — source: AVCaptureMetadataOutput.h
- `AVCaptureMetadataOutput setMetadataObjectsDelegate:queue:` / `.metadataObjectsDelegate` / `.metadataObjectsCallbackQueue` — source: same
- `AVCaptureMetadataOutput.availableMetadataObjectTypes` (detectable) / `.metadataObjectTypes` (enabled subset) — source: same
- `AVCaptureMetadataOutput.rectOfInterest` — normalized scan region — source: same
- `AVCaptureMetadataOutputObjectsDelegate captureOutput:didOutputMetadataObjects:fromConnection:` — delivery — source: same
- `AVMetadataObject` (abstract) — `.type`/`.bounds`/`.time`/`.duration` — source: AVMetadataObject.h
- body/face/salient object types (DELTA: detection is iOS-centric, though `AVMetadataObjectTypeHumanFullBody`/`SalientObject` constants compile on macos 14/10.15):
  - `AVMetadataObjectTypeFace` — source: AVMetadataObject.h
  - `AVMetadataObjectTypeHumanBody` / `AVMetadataObjectTypeHumanFullBody` (macos(14)+ios(17), this pin) — source: AVMetadataObject.h:180
  - `AVMetadataObjectTypeCatBody` / `AVMetadataObjectTypeDogBody` — source: AVMetadataObject.h
  - `AVMetadataObjectTypeSalientObject` (macos 10.15 / ios 13) — source: AVMetadataObject.h:292
- metadata object subclasses:
  - `AVMetadataFaceObject` — `.faceID` / `.hasRollAngle` / `.rollAngle` / `.hasYawAngle` / `.yawAngle` — source: AVMetadataObject.h:346
  - `AVMetadataMachineReadableCodeObject` — `.stringValue` / `.corners` / `.descriptor` (`CIBarcodeDescriptor`) — source: AVMetadataObject.h:605
  - `AVMetadataBodyObject` (`.bodyID`) → subclasses `AVMetadataHumanBodyObject`, `AVMetadataHumanFullBodyObject`, `AVMetadataCatBodyObject`, `AVMetadataDogBodyObject` — source: AVMetadataObject.h:128,166,193,235,277
  - `AVMetadataCatHeadObject` / `AVMetadataDogHeadObject` — head (vs body) detections — source: AVMetadataObject.h:208,250
  - `AVMetadataSalientObject` — salient object (NOT `…ObjectObject`) — source: AVMetadataObject.h:305
- machine-readable codes (`AVMetadataObjectType*`): `Aztec`/`Code39`/`Code39Mod43`/`Code93`/`Code128`/`DataMatrix`/`EAN8`/`EAN13`/`Interleaved2of5`/`ITF14`/`PDF417`/`QR`/`UPCE`/`Codabar`/`GS1DataBar`/`GS1DataBarExpanded`/`GS1DataBarLimited`/`MicroQR`/`MicroPDF417` — source: AVMetadataObject.h, https://developer.apple.com/documentation/avfoundation/avmetadatamachinereadablecodeobject/machine-readable_object_types
- `AVCaptureVideoPreviewLayer transformedMetadataObjectForMetadataObject:` — metadata coords→layer space — source: AVCaptureVideoPreviewLayer.h
- per-frame 3A/sensor state: there is NO public AE/AF/AWB-lock-state buffer attachment. Per-frame sensor values come via EXIF on the buffer (`kCGImagePropertyExifDictionary` / `…ExifExposureTime` / `…ExifISOSpeedRatings` / `…ExifApertureValue` / `…ExifFNumber`, ImageIO, read with `CMGetAttachment`) or by reading live device props (`exposureDuration`/`ISO`/`lensAperture`/`deviceWhiteBalanceGains`/`exposureTargetOffset` + `isAdjusting{Focus,Exposure,WhiteBalance}`). `?` no histogram/statistics-map output is published in capture (those live in Core Image / Vision downstream).

## egress (virtual camera publish)
DELTA: macOS-ONLY. The entire CoreMediaIO CMIOExtension stack is macOS (macos 12.3+); iOS/iPadOS has NO public virtual-camera publish API (confirmed below).
- `CMIOExtensionProvider` — top-level extension provider — source: CoreMediaIO CMIOExtensionProvider.h:177
- `CMIOExtensionProviderSource` (protocol) — your provider impl: `connectClient:error:` / `disconnectClient:` / `availableProperties` / `providerPropertiesForProperties:error:` / `setProviderProperties:error:` — source: CMIOExtensionProvider.h:106
- `CMIOExtensionDevice` — the virtual device; `addStream:error:` / `.source` — source: CMIOExtensionDevice.h:159
- `CMIOExtensionDeviceSource` (protocol) — device impl + `availableProperties` — source: CMIOExtensionDevice.h:110
- `CMIOExtensionDeviceProperties` — device property bag — source: CMIOExtensionProperties.h
- `CMIOExtensionStream` — a stream on the device; `.streamingClients` / `.direction` / `.source` — source: CMIOExtensionStream.h:280
  - `streamWithLocalizedName:streamID:direction:clockType:source:` (+ customClockConfiguration variant) — source: CMIOExtensionStream.h:304,323
  - `sendSampleBuffer:discontinuity:hostTimeInNanoseconds:` — vend a frame to consumers (SOURCE stream) — source: CMIOExtensionStream.h:445
  - `consumeSampleBufferFromClient:completionHandler:` — pull a frame from a client (SINK stream) — source: CMIOExtensionStream.h:458
- `CMIOExtensionStreamSource` (protocol) — stream impl: `startStreamAndReturnError:` / `stopStreamAndReturnError:` / `availableProperties` / `authorizedToStartStreamForClient:` — source: CMIOExtensionStream.h:189,259
- `CMIOExtensionStreamDirection` — `CMIOExtensionStreamDirectionSource`(0) / `CMIOExtensionStreamDirectionSink`(1). NOTE: there is NO `CMIOExtensionStreamSink` class — "sink" is this direction value plus the sink property keys below — source: CMIOExtensionStream.h:23
- `CMIOExtensionStreamFormat` — supported format (`CMFormatDescription` + min/max `CMTime` frame durations) — source: CMIOExtensionProperties.h:449
- `CMIOExtensionScheduledOutput` — scheduled-output record (`initWithSequenceNumber:hostTimeInNanoseconds:`, `.sequenceNumber`) — source: CMIOExtensionProperties.h:525
- `CMIOExtensionClient` — connected consumer (`.clientID` / `.pid` / `.signingID`) — source: CMIOExtensionProperties.h:573
- `CMIOExtensionProperty` (`NSString*` key type) / `CMIOExtensionPropertyState` — source: CMIOExtensionProperties.h
- built-in stream property keys: `CMIOExtensionPropertyStreamActiveFormatIndex` / `…StreamFrameDuration` / `…StreamSinkBuffersRequiredForStartup` / `…StreamSinkBufferQueueSize` / `…StreamSinkEndOfData` / `…StreamSinkBufferUnderrunCount` — source: CMIOExtensionProperties.h
- consumer-side C API (CoreMediaIO): `CMIOObjectPropertyAddress` / `CMIOObjectHasProperty` / `CMIOObjectGetPropertyData` / `CMIOObjectSetPropertyData` (CMIOHardwareObject.h) — source: CoreMediaIO framework
- System Extension packaging:
  - `OSSystemExtensionRequest activationRequestForExtensionWithIdentifier:queue:` / `deactivationRequest…` — source: SystemExtensions OSSystemExtensionRequest.h
  - `OSSystemExtensionManager.sharedManager submitRequest:` — source: SystemExtensions
  - `OSSystemExtensionRequestDelegate` — `request:didFinishWithResult:` / `request:didFailWithError:` / `request:actionForReplacingExtension:withExtension:` / `requestNeedsUserApproval:` `?` confirm exact selectors — source: SystemExtensions
  - `com.apple.developer.system-extension.install` (entitlement on host app) / `com.apple.developer.system-extension.redistributable` — source: BundleResources Entitlements
  - `NSSystemExtensionUsageDescription` (Info.plist) / `CMIOExtensionMachServiceName` (Info.plist) — source: BundleResources, WWDC22 10022
  - packaging facts: extension at `<app>.app/Contents/Library/SystemExtensions/`; host app from `/Applications`; Developer ID + notarization for distribution
- legacy: CoreMediaIO DAL plug-in (`CMIOHardwarePlugIn`, `/Library/CoreMediaIO/Plug-Ins/DAL`) — DEPRECATED macOS 12.3, disabled after Ventura; use CMIOExtension — source: CoreMediaIO
- iOS/iPadOS: NO public virtual-camera publish API. iPadOS 17 only *consumes* external USB cameras (`AVCaptureDeviceTypeExternal`); it does not publish. Closest screen-egress is ReplayKit broadcast (different domain) — source: WWDC23 10106

## OS integration
- permission: `AVCaptureDevice authorizationStatusForMediaType:` / `requestAccessForMediaType:completionHandler:` / `AVAuthorizationStatus` (`NotDetermined`/`Restricted`/`Denied`/`Authorized`) / `AVMediaTypeVideo`,`AVMediaTypeAudio` — source: AVCaptureDevice.h
- Info.plist: `NSCameraUsageDescription` (crash if missing) / `NSMicrophoneUsageDescription` — source: BundleResources
- App Sandbox entitlements (macOS): `com.apple.security.device.camera` / `com.apple.security.device.audio-input` — source: BundleResources Entitlements
- session lifecycle: `AVCaptureSession startRunning` (blocking) / `stopRunning` / `.isRunning` (KVO) / `.isInterrupted` (KVO) — source: AVCaptureSession.h
- notifications: `AVCaptureSessionDidStartRunningNotification` / `…DidStopRunningNotification` / `AVCaptureSessionWasInterruptedNotification` / `AVCaptureSessionInterruptionEndedNotification` / `AVCaptureSessionRuntimeErrorNotification` — source: AVCaptureSession.h
- `AVCaptureSessionErrorKey` (→`NSError`) / `AVCaptureSessionInterruptionReasonKey` / `AVCaptureSessionInterruptionSystemPressureStateKey` — DELTA: the interruption-reason userInfo keys are **API_UNAVAILABLE(macos)** — source: AVCaptureSession.h:96
- `AVCaptureSessionInterruptionReason` — `VideoDeviceNotAvailableInBackground`(1)/`AudioDeviceInUseByAnotherClient`(2)/`VideoDeviceInUseByAnotherClient`(3)/`VideoDeviceNotAvailableWithMultipleForegroundApps`(4)/`VideoDeviceNotAvailableDueToSystemPressure`(5, ios 11.1) — the enum is an iOS arbitration model (`…ReasonKey` unavailable on macOS) — source: AVCaptureSession.h:72
- `AVCaptureDevice.isInUseByAnotherApplication` (`inUseByAnotherApplication`) — DELTA: macCatalyst(14)-ONLY (`API_UNAVAILABLE(macos, ios, tvos)`) — source: AVCaptureDevice.h:262
- `AVCaptureSession.isMultitaskingCameraAccessEnabled` `?` — iPad multitasking camera access — source: AVCaptureSession.h
- `AVErrorDeviceInUseByAnotherApplication` (-11815) — error code — source: AVError.h:48
- rotation (this pin):
  - `AVCaptureDeviceRotationCoordinator` (`AVCaptureDevice.RotationCoordinator`) — horizon-level angle source — macos(14)+ios(17), BOTH — source: AVCaptureDevice.h:2897
  - `…RotationCoordinator initWithDevice:previewLayer:` / `.videoRotationAngleForHorizonLevelPreview` / `.videoRotationAngleForHorizonLevelCapture` (KVO, main queue) / `.previewLayer` (weak) — source: AVCaptureDevice.h
  - `AVCaptureConnection.videoRotationAngle` / `isVideoRotationAngleSupported:` — source: AVCaptureSession.h
  - `AVCaptureConnection.videoOrientation` (`AVCaptureVideoOrientation`) — DEPRECATED→videoRotationAngle — source: AVCaptureSession.h
- mirroring: `AVCaptureConnection.isVideoMirrored` / `.automaticallyAdjustsVideoMirroring` / `.isVideoMirroringSupported` — source: AVCaptureSession.h
- system pressure — DELTA: entire family **API_UNAVAILABLE(macos)** (iOS/Catalyst/tvOS):
  - `AVCaptureDevice.systemPressureState` (KVO) — source: AVCaptureDevice.h
  - `AVCaptureSystemPressureState` — `.level` / `.factors` — source: AVCaptureSystemPressure.h
  - `AVCaptureSystemPressureLevel` constants: `Nominal`/`Fair`/`Serious`/`Critical`/`Shutdown` (ios 11.1) — source: AVCaptureSystemPressure.h:27-51
  - `AVCaptureSystemPressureFactors` (OptionSet): `None`(0)/`SystemTemperature`(1<<0)/`PeakPower`(1<<1)/`DepthModuleTemperature`(1<<2) — source: AVCaptureSystemPressure.h:71-74
- hardware Camera Control — `[>pin]` macos(15)+ios(18) (recorded; beyond pin):
  - `AVCaptureControl` (abstract) — source: AVCaptureControl.h
  - `AVCaptureSlider` / `AVCaptureIndexPicker` / `AVCaptureToggle` / `AVCaptureSystemZoomSlider` / `AVCaptureSystemExposureBiasSlider` — source: AVCaptureSlider.h / AVCaptureIndexPicker.h / AVCaptureSystemZoomSlider.h / AVCaptureSystemExposureBiasSlider.h
  - `AVCaptureSession.supportsControls` / `addControl:` / `canAddControl:` / `removeControl:` / `.controls` / `.maxControlsCount` / `setControlsDelegate:queue:` — source: AVCaptureSession.h:380,466
  - `AVCaptureSessionControlsDelegate` — `sessionControlsDidBecomeActive:` / `…WillEnterFullscreenAppearance:` / `…WillExitFullscreenAppearance:` / `…DidBecomeInactive:` — source: AVCaptureSession.h
- hardware shutter / volume-button capture (AVKit) — `[>pin]` ios(17.2):
  - `AVCaptureEventInteraction` (`UIInteraction`) — `initWithEventHandler:` / `initWithPrimaryEventHandler:secondaryEventHandler:` / `.enabled` — source: AVKit AVCaptureEventInteraction.h
  - `AVCaptureEvent` — `.phase` (`AVCaptureEventPhase` `Began`/`Cancelled`/`Ended`) — source: AVKit
  - SwiftUI `onCameraCaptureEvent(...)` — `[>pin]` — source: SwiftUI
- privacy indicator: system-managed green/orange dot; NO public API; not controllable — source: platform behavior

## obscure corners
- `AVCaptureDeviceInput.videoMinFrameDurationOverride` — per-input FPS override distinct from device active durations — source: AVCaptureInput.h
- `AVCaptureInputPort.clock` — per-port `CMClock` (each constituent of a virtual/multi-cam input has its own clock for sync) — source: AVCaptureInput.h
- `AVCaptureLensPositionCurrent` / `AVCaptureExposureDurationCurrent` / `AVCaptureISOCurrent` / `AVCaptureExposureTargetBiasCurrent` / `AVCaptureWhiteBalanceGainsCurrent` `?` — "leave as-is" sentinels for custom-lock setters — source: AVCaptureDevice.h
- `AVCaptureDevice.activeMaxExposureDuration` — caps the AE algorithm's longest shutter independent of mode — source: AVCaptureDevice.h
- `AVCaptureDeviceFormat.secondaryNativeResolutionZoomFactors` — extra non-upscaled "native" zoom stops between lenses — source: AVCaptureDevice.h
- `AVCaptureDeviceFormat.supportedVideoZoomFactorsForDepthDataDelivery` — zoom×depth co-feasibility list (combination feasibility, not a per-control range) — source: AVCaptureDevice.h
- `AVCaptureDevice.spatialCaptureDiscomfortReasons` / `AVSpatialCaptureDiscomfortReason` — scene unsuitability for comfortable stereo viewing — source: AVCaptureDevice.h
- `AVMetadataCatHeadObject` / `AVMetadataDogHeadObject` — pet *head* detection, separate from pet *body* — source: AVMetadataObject.h:208,250
- `AVCaptureReactionSystemImageNameForType` — maps a reaction to an SF Symbol for UI parity with the system overlay — source: AVCaptureReactions.h:85
- `CMIOExtensionStreamSinkBufferUnderrunCount` / `…SinkBuffersRequiredForStartup` — sink-stream flow-control properties for pull-based publish — source: CMIOExtensionProperties.h
- `AVCaptureConnection.audioChannels` (`AVCaptureAudioChannel.averagePowerLevel`/`peakHoldLevel`) — per-channel audio metering rides on the capture connection (timing seam to audioin) — source: AVCaptureSession.h
- `AVCaptureMovieFileOutput.isPrimaryConstituentDeviceSwitchingBehaviorForRecordingEnabled` — lock lens-switchover specifically during recording — source: AVCaptureFileOutput.h
- `AVCaptureSessionInterruptionSystemPressureStateKey` — interruption notification carries the exact pressure state that forced shutdown — source: AVCaptureSession.h
- `kCMSampleBufferAttachmentKey_DroppedFrameReasonInfo` — secondary detail beyond the primary drop reason — source: CoreMedia CMSampleBuffer.h
- `[>pin]` items recorded for the ceiling: spatial-video movie output (macos 15/ios 18), Cinematic Video on input + tracking-focus (26), Camera Control sliders/pickers + `AVCaptureSessionControlsDelegate` (macos 15/ios 18), `AVCaptureEventInteraction` (ios 17.2), `displayVideoZoomFactorMultiplier`/`isAutoVideoFrameRateEnabled`/temp-tint WB lock (ios 18), `AVCaptureColorSpace_AppleLog2` (26).
- residual `?` (verify before coding): `setWhiteBalanceModeLockedWithTemperatureAndTintValues:` exact selector + version; rect-of-interest AF/AE exact symbol names; `AVCaptureReactionEffectState` start/end property names; `OSSystemExtensionRequestDelegate` exact selectors; `AVCaptureSession.isMultitaskingCameraAccessEnabled`; histogram/statistics-map output (believed absent in capture).
