# web — inventory
> api gen: getUserMedia + MediaStreamTrack + ImageCapture + WebCodecs + Insertable Streams (Chromium/Firefox/Safari deltas noted)
> covers matrix columns: web+getusermedia

## devices & enumeration
- `navigator.mediaDevices.enumerateDevices()` — Promise<MediaDeviceInfo[]>; lists current input/output devices — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/MediaDevices/enumerateDevices
- `MediaDeviceInfo.deviceId` — origin-unique opaque id (reset per origin/clears on cookie clear) — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/MediaDeviceInfo
- `MediaDeviceInfo.groupId` — shared by devices on the same physical hardware (camera + its built-in mic share a groupId) — this is the only device↔mic association web exposes — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/MediaDeviceInfo/groupId
- `MediaDeviceInfo.kind` — `"videoinput"` | `"audioinput"` | `"audiooutput"` (string, not exposed as enum to JS; values fixed by spec) — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/MediaDeviceInfo/kind
- `MediaDeviceInfo.label` — human-readable name; **empty string until camera permission is granted / an active stream exists** (anti-fingerprinting) — source: MDN enumerateDevices
- `MediaDeviceInfo.toJSON()` — serialization helper — source: MDN MediaDeviceInfo
- `InputDeviceInfo` — subclass of MediaDeviceInfo returned for input devices; adds `getCapabilities()` (device-level capabilities without opening a track) — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/InputDeviceInfo
- `navigator.mediaDevices.getSupportedConstraints()` — MediaTrackSupportedConstraints: dict of boolean flags, one per constraint the UA recognizes (feature-detection for constraint NAMES, not per-device ranges) — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/MediaDevices/getSupportedConstraints
- `MediaTrackSupportedConstraints.{width,height,aspectRatio,frameRate,facingMode,resizeMode,deviceId,groupId}` + image-capture set `{whiteBalanceMode,exposureMode,focusMode,exposureCompensation,exposureTime,colorTemperature,iso,brightness,contrast,saturation,sharpness,focusDistance,pointsOfInterest,zoom,torch,pan,tilt}` — booleans — source: MDN getSupportedConstraints
- `navigator.mediaDevices.ondevicechange` / `addEventListener("devicechange", …)` — fires on hot-plug (connect/disconnect); event carries no payload, app must re-enumerate — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/MediaDevices/devicechange_event
- `navigator.mediaDevices.selectAudioOutput({deviceId?})` — prompts user to pick an audio OUTPUT device (Audio Output Devices API; relevant only for the device↔groupId picture, not a camera capability) — Firefox + Chromium; requires transient activation + `speaker-selection` permission — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/MediaDevices/selectAudioOutput
- **no raw external / UVC / capture-card distinction** — web exposes no transport/bus info; a USB webcam, built-in cam, Continuity Camera and an HDMI capture card are all just `kind:"videoinput"` with opaque deviceId/groupId. No vendor/product id, no "external" flag — source: spec gap (Media Capture and Streams has no such field) https://www.w3.org/TR/mediacapture-streams/
- **feasibility model (constraints)** — there is no `isSessionConfigurationSupported`-style tuple query on web. Feasibility is resolved by the UA when it satisfies a `MediaTrackConstraints` set: each constraint field is `ideal` (advisory, UA optimizes toward it) / `exact` (mandatory) / `min` / `max` (mandatory range). The UA's **constraint-satisfaction algorithm** picks one device + settings tuple satisfying all mandatory constraints, maximizing fitness against `ideal`; if no settings satisfy the mandatory set it rejects with `OverconstrainedError` (carries `.constraint` = the offending field name). `track.getCapabilities()` reports the achievable ranges, `track.getSettings()` the chosen point — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/MediaTrackConstraints + https://developer.mozilla.org/en-US/docs/Web/API/OverconstrainedError

## topology & streams
- `MediaStream` — container of 0..N `MediaStreamTrack`s; `.id`, `.active` (true if ≥1 unended track), `addtrack`/`removetrack` events — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/MediaStream
- `MediaStream.getTracks() / getVideoTracks() / getAudioTracks() / getTrackById(id)` — source: MDN MediaStream
- `MediaStream.addTrack(t) / removeTrack(t) / clone()` — clone() deep-clones all tracks (new track ids) — source: MDN MediaStream
- `new MediaStream() / new MediaStream(stream) / new MediaStream(tracks[])` — assemble a stream from arbitrary tracks (compose multi-source) — source: MDN MediaStream
- `getUserMedia({video, audio})` — **one call requesting video+audio yields tracks on a shared capture session ⇒ shared timeline / common time-base ⇒ A/V sync.** Multiple separate getUserMedia calls do NOT guarantee a shared clock — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/MediaDevices/getUserMedia
- **multi-camera** — concurrently opening two `videoinput` devices via two getUserMedia calls (distinct `deviceId:{exact}`) is allowed in spec but **practically limited**: many UAs/OSes refuse a second simultaneous camera, and there is no atomic multi-cam open. No genlock / hardware-synced multi-cam — source: spec gap, Media Capture and Streams https://www.w3.org/TR/mediacapture-streams/
- `MediaStreamTrack.clone()` — independent track handle onto the same source (separate enabled/constraints) — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/MediaStreamTrack/clone
- **no logical/physical lens model** — `facingMode` is the only lens hint; web cannot address a multi-lens logical camera's constituent physical sensors or query switch-over zoom factors. A phone's wide/ultrawide/tele appear (if at all) as separate opaque deviceIds or as a single `zoom`-capable track; the UA decides — source: spec gap https://www.w3.org/TR/mediacapture-streams/

## fine-grained control
> All three of MediaTrackConstraints (request) / MediaTrackCapabilities (`getCapabilities()`, ranges) / MediaTrackSettings (`getSettings()`, current values) carry the SAME field set. Ranges report as `{min,max,step}` (numeric) or value-arrays (enumerated). **Most image-capture fields below are Chromium-only; Firefox & Safari implement essentially only the core video set.**
- `width` — ConstrainULong; capability `{min,max}` — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/MediaTrackConstraints
- `height` — ConstrainULong — source: MDN MediaTrackConstraints
- `aspectRatio` — ConstrainDouble (width/height) — source: MDN MediaTrackConstraints
- `frameRate` — ConstrainDouble (fps) — source: MDN MediaTrackConstraints
- `facingMode` — ConstrainDOMString; values `"user"` | `"environment"` | `"left"` | `"right"` — only orientation/lens hint on web — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/MediaTrackSettings/facingMode
- `resizeMode` — ConstrainDOMString; `"none"` (native sensor res) | `"crop-and-scale"` (UA may crop/downscale from a higher native res) — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/MediaTrackSettings/resizeMode
- `deviceId` — ConstrainDOMString; pin a specific device with `{exact}` — source: MDN MediaTrackConstraints
- `groupId` — ConstrainDOMString — source: MDN MediaTrackConstraints
- `track.applyConstraints(constraints)` → Promise — re-negotiate a live track without teardown; rejects `OverconstrainedError` (with `.constraint`) on infeasible mandatory set — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/MediaStreamTrack/applyConstraints
- `track.getCapabilities()` → MediaTrackCapabilities — per-control achievable ranges/value-sets for THIS opened track (the honest range query; no silent clamp) — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/MediaStreamTrack/getCapabilities
- `track.getConstraints()` → last-applied MediaTrackConstraints — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/MediaStreamTrack/getConstraints
- `track.getSettings()` → MediaTrackSettings — actual current value of every constrainable property — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/MediaStreamTrack/getSettings
### image-capture / MediaStreamTrack extensions (MediaStream Image Capture spec — **Chromium-mostly**)
- `whiteBalanceMode` — ConstrainDOMString; MeteringMode `"none"|"manual"|"single-shot"|"continuous"` — source: W3C MediaStream Image Capture https://w3c.github.io/mediacapture-image/
- `exposureMode` — ConstrainDOMString; MeteringMode values — source: W3C Image Capture
- `focusMode` — ConstrainDOMString; MeteringMode values — source: W3C Image Capture
- `pointsOfInterest` — sequence of `{x,y}` Point2D (normalized 0..1), 3A metering ROI — source: W3C Image Capture
- `exposureCompensation` — ConstrainDouble (EV, typically ±3 f-stop; range from getCapabilities) — source: W3C Image Capture
- `exposureTime` — ConstrainDouble (units of 100µs per spec) — source: W3C Image Capture
- `colorTemperature` — ConstrainDouble (Kelvin) — source: W3C Image Capture
- `iso` — ConstrainDouble (sensitivity) — source: W3C Image Capture
- `brightness` — ConstrainDouble — source: W3C Image Capture
- `contrast` — ConstrainDouble — source: W3C Image Capture
- `saturation` — ConstrainDouble — source: W3C Image Capture
- `sharpness` — ConstrainDouble — source: W3C Image Capture
- `focusDistance` — ConstrainDouble (meters; lens position) — source: W3C Image Capture
- `zoom` — ConstrainDouble (digital/optical zoom factor; range from getCapabilities) — source: W3C Image Capture
- `torch` — ConstrainBoolean (continuous lamp on/off; NOT a still flash) — source: W3C Image Capture
- **range/step honesty** — every numeric control above reports `{min,max,step}` via `getCapabilities()`; an unsupported control is simply absent from the capabilities dict (no silent clamp, but also no guarantee `applyConstraints` lands exactly on the requested value) — source: W3C Image Capture
- **no per-channel color gains, no CCM, no lens-shading/hot-pixel, no tonemap/gamma curve, no edge-enhancement, no noise-reduction strength, no aperture, no analog/digital gain split** — web exposes only the coarse fields above; the full ISP surface is not addressable — source: spec gap https://w3c.github.io/mediacapture-image/

## mechanical controls
- `pan` — ConstrainDouble (degrees; mechanical PTZ pan) — source: W3C Image Capture https://w3c.github.io/mediacapture-image/ §pan
- `tilt` — ConstrainDouble (degrees; mechanical tilt) — source: W3C Image Capture §tilt
- (`zoom` doubles as optical/mechanical zoom on PTZ cams — listed above) — source: W3C Image Capture
- **`pan`/`tilt`/`zoom` are gated behind a SEPARATE permission** — getUserMedia must request `video:{pan:true}` / `{tilt:true}` / `{zoom:true}` AND the page must hold the `"pan-tilt-zoom"` permission; otherwise these are stripped from capabilities/settings even on a PTZ-capable camera — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/MediaDevices/getUserMedia#pan_tilt_and_zoom
- `navigator.permissions.query({name:"camera", panTiltZoom:true})` — query PTZ-elevated camera permission; state granted/denied/prompt — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/Permissions/query (Chromium; Firefox does not support querying `camera`)
- **PTZ is Chromium-only**; Firefox/Safari expose no pan/tilt — source: MDN browser-compat on PTZ constraints

## capture modes
- `new ImageCapture(videoTrack)` — wraps a live video MediaStreamTrack for still capture — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/ImageCapture/ImageCapture
- `ImageCapture.track` — readonly source track — source: W3C Image Capture
- `ImageCapture.takePhoto(photoSettings?)` → Promise<Blob> — single still (UA-encoded, typically JPEG) — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/ImageCapture/takePhoto
- `PhotoSettings.{imageWidth, imageHeight, fillLightMode, redEyeReduction}` — takePhoto args — source: W3C Image Capture
- `PhotoSettings.fillLightMode` — FillLightMode `"auto"|"off"|"flash"` (this is the still-capture FLASH control, distinct from the continuous `torch`) — source: W3C Image Capture
- `PhotoSettings.redEyeReduction` — boolean — source: W3C Image Capture
- `ImageCapture.getPhotoCapabilities()` → Promise<PhotoCapabilities> — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/ImageCapture/getPhotoCapabilities
- `PhotoCapabilities.{imageWidth, imageHeight}` — MediaSettingsRange `{min,max,step}` — source: W3C Image Capture
- `PhotoCapabilities.fillLightMode` — sequence<FillLightMode> — source: W3C Image Capture
- `PhotoCapabilities.redEyeReduction` — RedEyeReduction `"never"|"always"|"controllable"` — source: W3C Image Capture
- `MediaSettingsRange.{max, min, step}` — source: W3C Image Capture
- `ImageCapture.getPhotoSettings()` → Promise<PhotoSettings> — current still config — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/ImageCapture/getPhotoSettings
- `ImageCapture.grabFrame()` → Promise<ImageBitmap> — snapshot of the live preview (cheaper than takePhoto, preview-res, no reconfigure) — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/ImageCapture/grabFrame
- **ImageCapture browser reality** — Chromium: full. Firefox: only `takePhoto`/`grabFrame` historically behind `dom.imagecapture.enabled`, `getPhotoCapabilities`/`getPhotoSettings` absent/limited. **Safari: not implemented at all** — apps fall back to drawing the `<video>` onto a canvas + `canvas.toBlob()` — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/ImageCapture#browser_compatibility
- **no RAW / ProRAW, no DNG, no HEIC selection, no exposure/focus bracketing, no zero-shutter-lag, no still-during-video guarantee, no HDR/Night/Portrait vendor modes, no 10-bit/HLG/log video, no high-speed slow-mo, no sensor readout/binning control** — none exposed on web; UA picks encoding (JPEG) and there is no burst/bracketing primitive — source: spec gap https://w3c.github.io/mediacapture-image/
- `MediaRecorder` — encodes a MediaStream to a container (webm/mp4) — **encode/mux is out of camera scope** (downstream media domain), noted as existing — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/MediaRecorder

## depth / 3D / calibration
- **none standard** — web exposes no depth stream, no disparity/point-cloud, no camera intrinsics/extrinsics, no lens-distortion, no IR/NIR/mono stream, no stereo/spatial-video from getUserMedia. Depth/world-tracking lives in **WebXR (a separate domain)**, not the camera API — source: spec gap; WebXR Depth Sensing https://www.w3.org/TR/webxr-depth-sensing/
- `FaceDetector` landmarks (eye/nose/mouth points only — see metadata) are the closest thing to face geometry; **no 3D mesh, no depth** — source: WICG Shape Detection https://wicg.github.io/shape-detection-api/

## live effects
- `backgroundBlur` — ConstrainBoolean (constraint) / `sequence<boolean>` (capability) / boolean (setting); lets the page observe & (if controllable) toggle PLATFORM background blur. Capability array semantics: absent/`[true]`/`[false]` ⇒ not controllable, `[false,true]` ⇒ controllable via `applyConstraints` — **Chromium experimental (~Chrome 116+, Intel-authored); Firefox/Safari no** — source: W3C Media Capture Extensions https://w3c.github.io/mediacapture-extensions/ + Chrome blog https://developer.chrome.com/blog/background-blur
- `faceFraming` — ConstrainBoolean; observe/control platform auto-framing (Center-Stage-like) — **Chromium experimental; non-universal** — source: W3C Media Capture Extensions https://w3c.github.io/mediacapture-extensions/
- `eyeGazeCorrection` — ConstrainDOMString; EyeGazeCorrectionMode `"off"|"normal"|"stare"` (AI eye-contact synthesis; `"stare"`=teleprompter) — **Chromium experimental** — source: W3C Media Capture Extensions + riju/eyeGazeCorrection explainer https://github.com/riju/eyeGazeCorrection
- `backgroundSegmentationMask` — ConstrainBoolean / `sequence<boolean>`; exposes platform person-segmentation MASK as track output (for app-side blur/replace) rather than a baked effect — **Chromium experimental** — source: W3C Media Capture Extensions https://w3c.github.io/mediacapture-extensions/
- `"configurationchange"` event on MediaStreamTrack — fires when the platform/user toggles one of these (or other settings) via an OS affordance, so the app can re-read `getSettings()` — source: W3C Media Capture Extensions
- **security note** — these platform effects only attach to genuine camera (getUserMedia) tracks; a `CanvasCaptureMediaStreamTrack` or generator track does NOT support them (one-way device→platform, no app bits into native ML) — source: backgroundBlur explainer https://github.com/riju/backgroundBlur
- otherwise **no app-controllable studio-light / reactions / subject-tracking ROI** beyond `pointsOfInterest` metering — source: spec gap

## frame memory
- `VideoFrame` (WebCodecs) — GPU- or CPU-resident decoded frame handle; transferable; usable in Workers — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/VideoFrame
- `VideoFrame.format` — VideoPixelFormat; full enum: `"I420"`, `"I420P10"`, `"I420P12"`, `"I420A"`, `"I420AP10"`, `"I420AP12"`, `"I422"`, `"I422P10"`, `"I422P12"`, `"I422A"`, `"I422AP10"`, `"I422AP12"`, `"I444"`, `"I444P10"`, `"I444P12"`, `"I444A"`, `"I444AP10"`, `"I444AP12"`, `"NV12"`, `"RGBA"`, `"RGBX"`, `"BGRA"`, `"BGRX"`, `"I010"` — source: WebCodecs spec https://www.w3.org/TR/webcodecs/#pixel-format (MDN documents the common subset I420/I420A/I422/I444/NV12/RGBA/RGBX/BGRA/BGRX)
- `VideoFrame.{codedWidth, codedHeight}` — full coded dims incl. padding (pre-aspect) — source: MDN VideoFrame
- `VideoFrame.codedRect` / `VideoFrame.visibleRect` — DOMRectReadOnly; visibleRect = the meaningful (cropped) region within coded — source: MDN VideoFrame
- `VideoFrame.{displayWidth, displayHeight}` — post-aspect-ratio display dims — source: MDN VideoFrame
- `VideoFrame.colorSpace` → VideoColorSpace `{primaries, transfer, matrix, fullRange}` — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/VideoColorSpace
- `VideoFrame.{rotation, flip}` — orientation metadata (0/90/180/270, horizontal mirror) — source: MDN VideoFrame
- `VideoFrame.allocationSize(options?)` — bytes needed to `copyTo` (options: `rect`, `layout`) — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/VideoFrame/allocationSize
- `VideoFrame.copyTo(destination, options?)` → Promise<PlaneLayout[]> — CPU readback into ArrayBuffer/TypedArray; options `{rect, layout, format, colorSpace}`; returns per-plane `PlaneLayout{offset, stride}` — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/VideoFrame/copyTo
- `VideoFrame.clone()` — new handle, same underlying media — source: MDN VideoFrame
- `VideoFrame.close()` — **mandatory** release of the (pooled/limited) backing buffer; leaking VideoFrames stalls the pipeline — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/VideoFrame/close
- `VideoFrame.metadata()` → VideoFrameMetadata (sparse; UA/source-defined extra fields) — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/VideoFrame/metadata
- `new VideoFrame(canvasImageSource, init)` / `new VideoFrame(bufferData, init)` — construct from `<video>`/canvas/ImageBitmap/OffscreenCanvas/another VideoFrame, OR from raw pixel buffer with explicit `{format, codedWidth, codedHeight, timestamp, ...}` — source: MDN VideoFrame
- `MediaStreamTrackProcessor({track, maxBufferSize?})` — **Insertable Streams**; `.readable` = ReadableStream<VideoFrame> pulled from a live camera track (Worker-only) — **Chromium only (Firefox/Safari no)** — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/MediaStreamTrackProcessor + W3C MediaStreamTrack Insertable Media Processing https://w3c.github.io/mediacapture-transform/
- back-pressure — `maxBufferSize` bounds queued frames; the `ReadableStream` applies standard pull back-pressure; slow consumers cause frame drops at the source — source: MDN MediaStreamTrackProcessor + W3C mediacapture-transform
- **zero-copy GPU import** — `GPUDevice.importExternalTexture({source: videoFrame|HTMLVideoElement, colorSpace})` → GPUExternalTexture, sampled directly in WGSL (VideoFrame source persists until `.close()`) — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/GPUDevice/importExternalTexture
- WebGL import — `gl.texImage2D` / `texSubImage2D` accept `HTMLVideoElement` AND `VideoFrame` as pixel source (copy, not zero-copy) — source: MDN texImage2D / WebGL VideoFrame support
- `createImageBitmap(source, [sx,sy,sw,sh], options?)` — accepts `VideoFrame`, `HTMLVideoElement`, `ImageData`, `Blob`, `OffscreenCanvas`, etc.; Worker-available; returns ImageBitmap (own `.close()`) — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/Window/createImageBitmap
- `OffscreenCanvas` (+ `transferToImageBitmap()`) — off-main-thread canvas render target for frame processing in a Worker — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/OffscreenCanvas
- **no explicit buffer pool / queue-depth control / frame-drop-reason API** — pooling is internal to the UA; `maxBufferSize` is the only knob, and dropped frames are not individually reported with reasons — source: spec gap https://w3c.github.io/mediacapture-transform/

## timing
- `VideoFrame.timestamp` — presentation timestamp in **microseconds** (integer) — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/VideoFrame/timestamp
- `VideoFrame.duration` — microseconds (integer, nullable) — source: MDN VideoFrame
- frames from `MediaStreamTrackProcessor` carry these timestamps (the per-frame camera time on web) — source: W3C mediacapture-transform https://w3c.github.io/mediacapture-transform/
- `HTMLVideoElement.requestVideoFrameCallback(cb)` — `cb(now, metadata)` fires per displayed frame; **Safari yes, Chromium yes, Firefox lagging** — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/HTMLVideoElement/requestVideoFrameCallback
- `VideoFrameCallbackMetadata.presentationTime` — DOMHighResTimeStamp the UA submitted the frame for composition — source: MDN
- `VideoFrameCallbackMetadata.expectedDisplayTime` — DOMHighResTimeStamp the frame is expected on screen — source: MDN
- `VideoFrameCallbackMetadata.{width, height}` — visible decoded dims — source: MDN
- `VideoFrameCallbackMetadata.mediaTime` — media presentation timestamp (seconds) — source: MDN
- `VideoFrameCallbackMetadata.presentedFrames` — running count of composited frames (detect drops) — source: MDN
- `VideoFrameCallbackMetadata.processingDuration` — seconds, decode-submit → ready — source: MDN
- `VideoFrameCallbackMetadata.captureTime` — DOMHighResTimeStamp the frame was captured (**WebRTC-sourced tracks**) — source: MDN
- `VideoFrameCallbackMetadata.receiveTime` — DOMHighResTimeStamp encoded frame received (WebRTC) — source: MDN
- `VideoFrameCallbackMetadata.rtpTimestamp` — RTP media timestamp (WebRTC) — source: MDN
- `HTMLVideoElement.cancelVideoFrameCallback(handle)` — source: MDN
- **no SMPTE timecode, no IMU/gyro/OIS per-frame metadata, no rolling-shutter-skew, no exposure-applied-frame#, no genlock, no explicit capture-clock domain id** — `captureTime` (WebRTC only) is the nearest sensor-time signal; getUserMedia camera frames give only the microsecond `VideoFrame.timestamp` — source: spec gap

## metadata
- `BarcodeDetector` — `new BarcodeDetector({formats})`; `detect(imageBitmapSource)` → DetectedBarcode[] `{rawValue, format, boundingBox(DOMRectReadOnly), cornerPoints(Point2D[])}`; static `BarcodeDetector.getSupportedFormats()` — source: WICG Shape Detection https://wicg.github.io/shape-detection-api/
- BarcodeFormat values: `"aztec"`, `"code_128"`, `"code_39"`, `"code_93"`, `"codabar"`, `"data_matrix"`, `"ean_13"`, `"ean_8"`, `"itf"`, `"pdf417"`, `"qr_code"`, `"upc_a"`, `"upc_e"`, `"unknown"` — source: WICG Shape Detection
- `FaceDetector` — `new FaceDetector({maxDetectedFaces, fastMode})`; `detect(image)` → DetectedFace[] `{boundingBox, landmarks}`; `Landmark{type:"eye"|"nose"|"mouth", locations:Point2D[]}` — **landmarks only, no 3A/pose/depth** — source: WICG Shape Detection
- `TextDetector` — `detect(image)` → DetectedText[] `{rawValue, boundingBox, cornerPoints}` — **explicitly deemed not stable enough to standardize; Chromium-experimental/removed** — source: WICG Shape Detection (notes TextDetector unstable)
- **Shape Detection browser reality** — `BarcodeDetector`: Chromium (Android/Chrome OS/macOS via platform; Linux/Windows often missing) + Safari 17+ ; `FaceDetector`/`TextDetector`: behind flags / removed; Firefox: none. Highly non-universal — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/Barcode_Detection_API#browser_compatibility
- `MediaStreamTrack.stats` / `track.getFrameStats()` — proposed track-level frame stats (deliveredFrames, discardedFrames, totalFrames) — **experimental, Chromium** — source: W3C Media Capture Extensions https://w3c.github.io/mediacapture-extensions/#mediastreamtrack-statistics
- **no camera-internal 3A state / metering result / histogram / lens-shading map / scene or flicker detection / per-frame sensor settings readback** — web surfaces zero ISP/3A telemetry; only the high-level detector results above — source: spec gap

## egress (virtual camera PUBLISH)
- `HTMLCanvasElement.captureStream(frameRate?)` → MediaStream with one `CanvasCaptureMediaStreamTrack` of canvas contents (frameRate 0 ⇒ manual) — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/HTMLCanvasElement/captureStream
- `CanvasCaptureMediaStreamTrack.requestFrame()` — push one frame when frameRate=0 — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/CanvasCaptureMediaStreamTrack/requestFrame
- `HTMLMediaElement.captureStream()` → MediaStream mirroring `<video>`/`<audio>` playback — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/HTMLMediaElement/captureStream
- `MediaStreamTrackGenerator({kind:"video"})` — **Insertable Streams**; `.writable` = WritableStream<VideoFrame>; the object **IS a MediaStreamTrack** you can put in a MediaStream — build a track from app-produced frames — **Chromium only**; being superseded by `VideoTrackGenerator` (Worker-based) — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/MediaStreamTrackGenerator + W3C mediacapture-transform https://w3c.github.io/mediacapture-transform/
- **web CANNOT publish an OS-wide virtual camera.** Every produced stream/track above is **IN-PAGE ONLY** — consumable solely within the same document (assign to a `<video>`, feed `RTCPeerConnection`, hand to `MediaRecorder`, pass to a Worker). There is no API to register a system-visible camera device other OS apps enumerate. The matrix `web+getusermedia` publish path = **none (canvas.captureStream in-page only)**, degrading honestly to local-only (MEL-ENGINE-VII) — source: charter axis pin + spec gap (no virtual-camera registration in any web spec)
- (WebRTC `RTCPeerConnection.addTrack` peer-send is **transport, out of scope** — it ships frames to a remote peer, not to the local OS) — source: charter scope note

## OS integration
- `navigator.permissions.query({name:"camera"})` → PermissionStatus `{state:"granted"|"denied"|"prompt", onchange/"change" event}` — **Chromium yes; Firefox does NOT support querying `"camera"` (rejects); Safari partial** — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/Permissions/query
- `navigator.permissions.query({name:"camera", panTiltZoom:true})` — the elevated PTZ permission descriptor (see mechanical controls) — source: MDN Permissions/query
- `getUserMedia()` consent — first call triggers the **UA permission prompt**; grant may persist per-origin (UA policy); subsequent calls resolve without prompt while granted — source: MDN getUserMedia
- getUserMedia rejections expose OS/consent state: `NotAllowedError` (denied / policy), `NotFoundError` (no device), `NotReadableError` (hardware/OS busy — i.e. **arbitration loss**, another app holds the camera), `OverconstrainedError`, `SecurityError`, `TypeError` (insecure context) — source: MDN getUserMedia
- **secure-context (HTTPS) requirement** — `navigator.mediaDevices` is `undefined` on insecure origins (http, except `localhost`/`file:`) — source: MDN getUserMedia
- `Permissions-Policy: camera=…` HTTP header + `<iframe allow="camera">` — gate camera access for the document / embedded frames — source: MDN https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Headers/Permissions-Policy/camera
- `MediaStreamTrack.muted` (readonly) + `mute`/`unmute` events — **the UA/OS can mute a track** (privacy toggle, tab hidden, OS camera switch); this is the web surrogate for interruption/background lifecycle — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/MediaStreamTrack/muted
- `MediaStreamTrack.enabled` (read/write) — app-side gate (black/silence) without releasing the device — source: MDN MediaStreamTrack
- `MediaStreamTrack.readyState` + `ended` event — device removed / stopped (hot-plug removal of the in-use camera ends its track) — source: MDN MediaStreamTrack
- Page Visibility / `document.hidden` — UAs may mute or throttle camera when the page is backgrounded — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/Page_Visibility_API
- **privacy indicator** — browser/OS shows an in-use camera indicator (LED / tab badge); **not readable or controllable from JS** — source: spec gap (privacy-by-design; no API)
- `DocumentPictureInPicture.requestWindow({width,height,disallowReturnToOpener,preferInitialWindowPlacement})` — Document Picture-in-Picture: float arbitrary DOM (e.g. a camera `<video>` + controls) in an always-on-top window; requires transient activation; `enter` event, `pagehide` on close — **Chromium; Firefox/Safari no** — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/DocumentPictureInPicture/requestWindow
- **`facingMode` is the ONLY orientation hint** — no camera sensor-rotation / mount-orientation / mirroring API. `screen.orientation` (Screen Orientation API) is the DEVICE orientation, a separate domain, not camera-relative — source: spec gap; MDN https://developer.mozilla.org/en-US/docs/Web/API/Screen/orientation
- **no thermal / power-aware camera-degradation API, no shutter-sound control, no hardware-shutter-button hook, no arbitration/preemption control** — none exposed; `NotReadableError` is the only arbitration signal — source: spec gap

## obscure corners
- `InputDeviceInfo.getCapabilities()` — device-level capabilities WITHOUT opening a track (rarely used; Chromium) — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/InputDeviceInfo/getCapabilities
- `MediaStreamTrack.contentHint` — `""|"motion"|"detail"|"text"` (video) advisory to downstream encoders/processing — source: MDN https://developer.mozilla.org/en-US/docs/Web/API/MediaStreamTrack/contentHint
- `VideoTrackGenerator` (Worker-scope) — the **newer replacement** for `MediaStreamTrackGenerator`: `{track, writable, muted}`; lives off-main-thread — Chromium-experimental — source: W3C mediacapture-transform https://w3c.github.io/mediacapture-transform/#track-generator
- `MediaStreamTrackAudioSourceNode` / WebAudio bridge — for the bundled audio track of an A/V getUserMedia (audio path, listed for completeness) — source: MDN Web Audio
- `applyConstraints` `advanced:[…]` array — ordered list of optional constraint sets the UA tries in sequence (the only way to express "prefer A, else B" priority) — source: MDN MediaTrackConstraints#advanced
- `VideoColorSpace` enums — primaries `bt709|bt470bg|smpte170m|bt2020|smpte432`, transfer `bt709|smpte170m|iec61966-2-1|linear|pq|hlg`, matrix `rgb|bt709|bt470bg|smpte170m|bt2020-ncl`, `fullRange` boolean — source: WebCodecs spec https://www.w3.org/TR/webcodecs/#videocolorspace
- `?` `restrictOwnAudio` / `suppressLocalAudioPlayback` / `displaySurface` / `logicalSurface` — these are **getDisplayMedia (screen-capture) constraints**, surfaced by MediaTrackConstraints but NOT camera; included only to flag they are not camera fields — source: MDN MediaTrackConstraints
- `?` `screenPixelRatio` / `cursor` — screen-capture-only settings, likewise not camera — source: MDN MediaTrackSettings
- `?` Chrome `camera-effects` / VideoFrameMetadata segmentation-mask plumbing — proposal-stage, fields unstable; not yet a fixed API name — source: https://github.com/markafoltz/camera-effects
