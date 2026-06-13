# uvc-direct — inventory
> api gen: USB Video Class 1.5, reached UNDER the OS via per-platform gateways
> covers matrix columns: uvc-direct (UVC-class devices only; deny ios/web)
> consolidated from 20-beyond-os UVC research; the under-OS path to caps the high-level OS API denies

> The under-OS primitive is the class control transfer `(unitID, selector, request)` with the verb set
> `GET_CUR/SET_CUR/GET_MIN/GET_MAX/GET_RES/GET_DEF/GET_LEN/GET_INFO`. Three host gateways:
> Linux `UVCIOC_CTRL_QUERY` / libusb · Windows `IKsControl::KsProperty` (== `DeviceIoControl`) · macOS IOKit/IOUSBHost
> (match `AVCaptureDevice.uniqueID`→USB VID/PID). **iOS = no public raw-USB path (App Store sandbox) — the floor.**

## devices & enumeration
- `bUnitID` / entity model — Camera Terminal, Processing Unit, Extension Unit, VideoStreaming interface, each addressed by ID — source: USB-IF UVC 1.5 §2.3.1
- `UVCIOC_CTRL_QUERY` (`struct uvc_xu_control_query{unit,selector,query,size,*data}`) — Linux raw control access, no root needed — source: kernel.org uvcvideo driver docs
- `IKsControl::KsProperty` / `DeviceIoControl` — Windows raw control to the KS filter (CT/PU) or node (XU) — source: MS `PROPSETID_VIDCAP_*`
- IOKit / IOUSBHost raw control transfers — macOS path matched to `AVCaptureDevice.uniqueID`; refs VVUVCKit, uvc-util, uvccompat — source: github VVUVCKit/uvc-util
- `GET_INFO` capability bitmap — per-control: which of GET/SET valid, async, autoupdate; gates everything, hidden by every high-level API — source: USB-IF UVC 1.5 §4.1.2
- `GET_LEN` — must precede a read of unknown-size controls — source: USB-IF UVC 1.5 §4.1.2

## topology & streams
- `VS_PROBE_CONTROL` / `VS_COMMIT_CONTROL` — negotiate resolution, frame interval, `dwMaxPayloadTransferSize`, `dwClockFrequency`; pick exact format/fps and renegotiate dynamically — source: USB-IF UVC 1.5 §4.3.1.1
- multiple VideoStreaming interfaces from one physical device — RGB + IR + depth pins (e.g. face-auth modules) — source: MS face-auth UVC example
- `dwClockFrequency` — negotiated device-clock rate, needed for PTS/SCR→host mapping; not surfaced by high-level APIs — source: USB-IF UVC 1.5 §4.3.1.1

## fine-grained control
- Camera-Terminal selectors (§4.2.2.1): `CT_SCANNING_MODE` 0x01, `CT_AE_MODE` 0x02, `CT_AE_PRIORITY` 0x03, `CT_EXPOSURE_TIME_ABSOLUTE` 0x04, `CT_EXPOSURE_TIME_RELATIVE` 0x05, `CT_FOCUS_ABSOLUTE` 0x06, `CT_FOCUS_RELATIVE` 0x07, `CT_FOCUS_AUTO` 0x08, `CT_IRIS_ABSOLUTE` 0x09, `CT_IRIS_RELATIVE` 0x0a, `CT_FOCUS_SIMPLE` 0x12 (UVC 1.5), `CT_WINDOW` 0x13, `CT_REGION_OF_INTEREST` 0x14 (`bmAutoControls` selects which auto-algorithms track the ROI) — source: USB-IF UVC 1.5 §4.2.2.1 + App. A.9.4. **All ABSENT from AVFoundation; reachable raw.**
- Processing-Unit selectors (§4.2.2.3): brightness, contrast, contrast-auto (1.5), hue, hue-auto, saturation, sharpness, gamma, white-balance-temperature (+auto), white-balance-component R/B (+auto), backlight-compensation, gain, power-line-frequency, digital-multiplier, digital-multiplier-limit, analog-video-standard, analog-video-lock-status — source: USB-IF UVC 1.5 §4.2.2.3. **WB-by-component, digital-multiplier, power-line, gamma absent from AVFoundation.**

## mechanical controls (PTZ)
- `CT_PANTILT_ABSOLUTE` 0x0d / `CT_PANTILT_RELATIVE` 0x0e — pan & tilt coupled, arc-second units (±180×3600) — source: USB-IF UVC 1.5 §4.2.2.1; uvc-util
- `CT_ROLL_ABSOLUTE` 0x0f / `CT_ROLL_RELATIVE` 0x10 — source: USB-IF UVC 1.5 §4.2.2.1
- `CT_ZOOM_ABSOLUTE` 0x0b / `CT_ZOOM_RELATIVE` 0x0c — source: USB-IF UVC 1.5 §4.2.2.1
- `CT_PRIVACY` 0x11 — hardware privacy shutter — source: USB-IF UVC 1.5 §4.2.2.1
- **Headline: PTZ is reachable via raw UVC on macOS (IOKit) / win32 (KsProperty) / linux (UVCIOC) even though AVFoundation exposes none — `emulate(raw-UVC)`, not `deny`.**

## capture modes
- payloads: uncompressed (YUY2/NV12), MJPEG, **H.264 frame-based + UVC 1.5 H.264 video-class** (on-camera encode), MPEG-2 TS, MPEG-4 SL/VC1, DV; legacy UVC-1.1 H.264 XU — source: USB-IF Frame-Based Payload spec
- on-camera H.264 encoder controls: initial-bitrate, slice-mode, iframe-period, entropy/CABAC, SEI, num-reorder-frames, leaky-bucket; dynamic force-keyframe, LTR frames, runtime bitrate/QP/rate-control/level — Windows MF exposes natively (`usbvideo.sys`); Linux uvcvideo does NOT decode H.264 payloads; macOS raw isoc — source: MS `camera-encoder-h264-uvc-1-5`; GStreamer uvch264; Logitech H.264/UVC whitepaper. **Receiving the camera's bitstream = in scope; host transcode = downstream.**
- still-image: `VS_STILL_PROBE_CONTROL` / `VS_STILL_COMMIT_CONTROL` (still resolution ≠ video) + `VS_STILL_IMAGE_TRIGGER_CONTROL` — Method 1 (over video pipe), Method 2 (video pauses), Method 3 (dedicated bulk endpoint + hardware-button trigger via Status-Interrupt endpoint); STI bit tags still frames. Method 2 off-by-default on Windows (`EnableDependentStillPinCapture=1` INF); Linux mainline limited — source: USB-IF UVC 1.5 §2.4.2.4 / §4.3.1.2

## depth / 3D / calibration
- `MS_CAMERA_CONTROL_XU` GUID `{0F3F95DC-2632-4C4E-92C9-A04782F43BC8}` — published-GUID XU reachable as raw XU on macOS/Linux (Windows maps to `KSPROPERTY_CAMERACONTROL_EXTENDED_*`): `MSXU_CONTROL_CAMERA_EXTRINSICS` 0x07 + `_CAMERA_INTRINSICS` 0x08 (**lens calibration matrices**), `_FACE_AUTHENTICATION` 0x06 (IR streaming modes), `_IR_TORCH` 0x0A — source: MS `uvc-extensions-1-5` §2.2.2
- IR / depth streams via multiple VideoStreaming interfaces (one device exposes RGB + IR pins) — source: MS face-auth UVC example
- `?` per-device depth decode format is vendor-specific where no MS-XU implemented

## live effects
- `MSXU_CONTROL_DIGITALWINDOW` 0x0B + `_DIGITALWINDOW_CONFIG` 0x0C — digital PTZ / auto-framing substitute; `MSXU_CONTROL_VIDEO_HDR` 0x0D; `_FIELDOFVIEW2` 0x10 — source: MS `uvc-extensions-1-5` §2.2.2
- otherwise none standard — beautify/segmentation are vendor XU only

## frame memory
- isochronous vs bulk endpoints (Method 3 still + some encoded streams use bulk for error-free transfer) — source: USB-IF UVC 1.5 §2.4.2
- payload formats negotiated at Probe/Commit (see capture modes); raw frames map to the `image` CPU buffer; zero-copy GPU is the host stack's job (not UVC's) — source: USB-IF UVC 1.5 §4.3.1.1

## timing
- per-frame **PTS** (frame-capture time in device-clock units per `dwClockFrequency`) — payload header — source: USB-IF UVC 1.5 §2.4.3.3
- per-frame **SCR** = STC (device clock) + 11-bit 1 kHz SOF token counter sampled together — the surface to linearly map device clock → USB bus clock → host clock — source: USB-IF UVC 1.5 §2.4.3.3
- surfaced: Linux `V4L2_META_FMT_UVC` ('UVCH') metadata node + driver-added host `ts`+`sof`; `V4L2_META_FMT_UVC_MSXU_1_5` (2025); Windows `MetadataId_UsbVideoHeader` (`KSSTREAM_UVC_METADATA`, INF opt-in); macOS = raw-parse off the isoc endpoint — source: kernel pixfmt-meta-uvc; MS `uvc-extensions-1-5` §2.2.3

## metadata
- `MSXU_CONTROL_METADATA` 0x09 — standardized per-frame metadata blob — source: MS `uvc-extensions-1-5` §2.2.2
- UVC payload-header status/error bits (FID toggle, EOF, error, STI still) — source: USB-IF UVC 1.5 §2.4.3.3
- no face/scene/3A statistics at the UVC level beyond what an XU vendor adds — source: USB-IF UVC 1.5

## egress (virtual camera publish)
- UVC-direct is an INGEST axis (reaching a UVC device's capture surface). Being a UVC camera (USB gadget side) = the device/host-gadget role: Linux UVC gadget driver (`configfs`), Android DeviceAsWebcam — system-image-level, not a generic app publish path — source: kernel UVC gadget; Android DeviceAsWebcam (covered in camera2 inventory)

## OS integration
- gateway summary: Linux `UVCIOC_CTRL_QUERY` (raw, no root) / `UVCIOC_CTRL_MAP` (maps XU→V4L2 control, **requires root**); Windows `IKsControl` on `PROPSETID_VIDCAP_CAMERACONTROL` (filter, always available) / `PROPSETID_VIDCAP_VIDEOPROCAMP` / `PROPSETID_VIDCAP_EXTENSION_UNIT` (node, `KSP_NODE` + NodeId — node id not programmatically discoverable, found via KSStudio + hardcoded); macOS IOKit/IOUSBHost — source: kernel uvcvideo; MS Extension-Unit-Plug-In; VVUVCKit
- exclusivity (macOS): historically control transfers only when the device isn't open by another process; modern IOKit/IOUSBHost can often coexist with AVFoundation capture, device/OS-version dependent — source: VVUVCKit/uvc-util notes
- **iOS: effectively no public path** — external UVC only via recent USB-C/AVFoundation external-device support; no raw IOKit USB for App Store apps — source: Apple external-device docs

## obscure corners
- `UVCIOC_CTRL_MAP` (`struct uvc_xu_control_mapping`, carries XU `entity[16]` GUID) — register a new XU→V4L2 control mapping; root-only — source: kernel uvcvideo
- analog-video-standard / analog-video-lock-status PU controls — for analog-frontend UVC capture dongles — source: USB-IF UVC 1.5 §4.2.2.3
- full `MS_CAMERA_CONTROL_XU` selector table: PHOTOMODE/FOCUS/EXPOSURE/EVCOMPENSATION/WHITEBALANCE 0x01-04, FACE_AUTH 0x06, EXTRINSICS/INTRINSICS 0x07/08, METADATA 0x09, IR_TORCH 0x0A, DIGITALWINDOW 0x0B/0C, VIDEO_HDR 0x0D, FRAMERATE_THROTTLE 0x0E, FIELDOFVIEW2 0x0F/10 — source: MS `uvc-extensions-1-5` §2.2.2
- `?` `CT_REGION_OF_INTEREST` `bmAutoControls` bit layout — confirm against USB-IF UVC 1.5 App. A before pinning
- `?` H.265/HEVC over UVC — vendor/XU territory, NOT a base-UVC-1.5 standardized payload; do not treat as guaranteed
