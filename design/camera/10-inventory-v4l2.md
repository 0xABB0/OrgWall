# v4l2 — inventory
> api gen: V4L2 kernel uAPI (videodev2.h) + UVC driver; v4l2loopback for publish
> covers matrix columns: linux+v4l2

## devices & enumeration
- `/dev/video0..N` — character device node per V4L2 video function (capture, output, meta, …); a UVC webcam often exposes several (capture node + UVC metadata node) — source: kernel.org Linux Media, https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/devices.html
- `VIDIOC_QUERYCAP` — query driver/card/bus_info + `capabilities` and per-node `device_caps` (struct v4l2_capability) — source: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/vidioc-querycap.html
- `V4L2_CAP_VIDEO_CAPTURE` / `V4L2_CAP_VIDEO_CAPTURE_MPLANE` — single-/multi-planar capture capability — source: videodev2.h, querycap doc
- `V4L2_CAP_VIDEO_OUTPUT` / `V4L2_CAP_VIDEO_OUTPUT_MPLANE` — output (used by v4l2loopback publish) — source: querycap doc
- `V4L2_CAP_VIDEO_OVERLAY` / `V4L2_CAP_VIDEO_OUTPUT_OVERLAY` — framebuffer overlay caps — source: videodev2.h
- `V4L2_CAP_VIDEO_M2M` / `V4L2_CAP_VIDEO_M2M_MPLANE` — mem-to-mem (codec) device — source: videodev2.h
- `V4L2_CAP_META_CAPTURE` / `V4L2_CAP_META_OUTPUT` — metadata node caps — source: videodev2.h
- `V4L2_CAP_STREAMING` — supports streaming I/O (QBUF/DQBUF) — source: querycap doc
- `V4L2_CAP_READWRITE` — supports read()/write() I/O — source: querycap doc
- `V4L2_CAP_EXT_PIX_FORMAT` — extended pixel-format fields supported — source: videodev2.h
- `V4L2_CAP_DEVICE_CAPS` — `device_caps` field is valid (per-node vs whole-device) — source: querycap doc
- `V4L2_CAP_IO_MC` — node's formats configured only via media controller (subdev-driven ISP) — source: querycap doc
- `V4L2_CAP_TIMEPERFRAME` — frame period settable via S_PARM — source: g-parm doc
- `V4L2_CAP_TOUCH` / `V4L2_CAP_ASYNCIO` / `V4L2_CAP_EDID` — touch sensor / async I/O / EDID node — source: videodev2.h
- `V4L2_CAP_TUNER` / `V4L2_CAP_MODULATOR` / `V4L2_CAP_RADIO` / `V4L2_CAP_AUDIO` / `V4L2_CAP_SDR_CAPTURE` / `V4L2_CAP_SDR_OUTPUT` / `V4L2_CAP_HW_FREQ_SEEK` / `V4L2_CAP_RDS_CAPTURE` — TV/radio/SDR caps (not camera; present in the same flag space) — source: videodev2.h
- `VIDIOC_ENUMINPUT` — enumerate video inputs on a device (struct v4l2_input: name, type, status, std); `V4L2_INPUT_TYPE_CAMERA` — source: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/vidioc-enuminput.html
- `VIDIOC_G_INPUT` / `VIDIOC_S_INPUT` — get/set the selected input index (capture-card multi-input) — source: user-func doc
- `VIDIOC_ENUMOUTPUT` / `VIDIOC_G_OUTPUT` / `VIDIOC_S_OUTPUT` — enumerate/select outputs (output/loopback side) — source: user-func doc
- sysfs `/sys/class/video4linux/videoN/{name,index,dev}` — device attributes for enumeration — source: kernel.org media device docs
- udev hot-plug — `udev` rules / `libudev` monitor on subsystem `video4linux` deliver add/remove uevents; ACLs via `uaccess` tag — source: udev / kernel media docs
- libv4l (`libv4l1`/`libv4l2`/`libv4lconvert`) — userspace shim emulating formats/controls absent in hardware (e.g. converting Bayer/MJPEG to RGB/YUYV) — source: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/libv4l-introduction.html
- UVC descriptors — USB Video Class enumerates Terminals/Units (Camera Terminal, Processing Unit, Extension Units) into V4L2 controls + formats via the `uvcvideo` driver — source: https://docs.kernel.org/userspace-api/media/drivers/uvcvideo.html ; USB UVC 1.5 spec
- capture cards / HDMI grabbers — appear as V4L2 capture nodes; DV-timings ioctls handle digital-video signal detection (below) — source: kernel media docs
- device ↔ mic seam — a UVC webcam's built-in microphone is a **separate ALSA/USB-audio device**, NOT a V4L2 node; V4L2 has no mic identity. Association is via shared USB parent in sysfs only — note seam (audio belongs to audioin/ALSA) — source: UVC driver doc (audio is USB Audio Class, not uvcvideo)
- `/dev/media0..N` + Media Controller — ISP/sensor pipelines expose a media graph; see media-controller area below — source: https://www.kernel.org/doc/html/latest/userspace-api/media/mediactl/media-controller.html
- feature-combination — there is **no atomic "is this format+size+interval supported" query**. Enumeration is staged: `VIDIOC_ENUM_FMT` → `VIDIOC_ENUM_FRAMESIZES` (per pixelformat) → `VIDIOC_ENUM_FRAMEINTERVALS` (per format+size). The only feasibility check of a concrete combination is `VIDIOC_TRY_FMT` (and S_PARM for the interval), which may silently adjust fields — note this is a silent-clamp seam — source: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/vidioc-enum-framesizes.html

## topology & streams
- `VIDIOC_G_FMT` / `VIDIOC_S_FMT` / `VIDIOC_TRY_FMT` — get/set/try the active format (struct v4l2_format → fmt.pix for single-planar) — source: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/vidioc-g-fmt.html
- `V4L2_BUF_TYPE_VIDEO_CAPTURE` / `V4L2_BUF_TYPE_VIDEO_OUTPUT` — single-planar capture/output buffer type — source: videodev2.h
- `V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE` / `V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE` — multiplanar (fmt.pix_mp, per-plane bytesperline/sizeimage) — source: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/pixfmt-v4l2-mplane.html
- `V4L2_BUF_TYPE_VIDEO_OVERLAY` / `V4L2_BUF_TYPE_VBI_CAPTURE` / `V4L2_BUF_TYPE_VBI_OUTPUT` / `V4L2_BUF_TYPE_SLICED_VBI_CAPTURE` / `V4L2_BUF_TYPE_SLICED_VBI_OUTPUT` / `V4L2_BUF_TYPE_SDR_CAPTURE` / `V4L2_BUF_TYPE_SDR_OUTPUT` / `V4L2_BUF_TYPE_META_CAPTURE` / `V4L2_BUF_TYPE_META_OUTPUT` — full buffer-type enum — source: videodev2.h
- multiple opens — a node may be opened by several fds; only one may stream / hold buffers; arbitration via priority (G/S_PRIORITY) and EBUSY — source: kernel media open/close docs
- `VIDIOC_G_SELECTION` / `VIDIOC_S_SELECTION` — crop/compose rectangles + targets (`V4L2_SEL_TGT_CROP`, `V4L2_SEL_TGT_COMPOSE`, `_DEFAULT`, `_BOUNDS`) — source: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/vidioc-g-selection.html
- `VIDIOC_CROPCAP` / `VIDIOC_G_CROP` / `VIDIOC_S_CROP` — legacy crop + pixel-aspect (superseded by SELECTION) — source: user-func doc
- media-controller pipeline — entities (sensor/ISP/CSI-receiver subdevs + video nodes), pads, links describe the capture graph; see media-controller area — source: media-controller doc
- `VIDIOC_SUBDEV_G_FMT` / `VIDIOC_SUBDEV_S_FMT` — per-pad media-bus format on a `/dev/v4l-subdevN` (sensor/ISP) — source: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/vidioc-subdev-g-fmt.html
- `VIDIOC_SUBDEV_G_SELECTION` / `VIDIOC_SUBDEV_S_SELECTION` — subdev crop/compose per pad — source: subdev docs
- `VIDIOC_SUBDEV_G_CROP` / `VIDIOC_SUBDEV_S_CROP` — legacy subdev crop — source: subdev docs
- `VIDIOC_SUBDEV_G_ROUTING` / `VIDIOC_SUBDEV_S_ROUTING` — internal pad routing (streams within a subdev) — source: subdev docs
- `VIDIOC_SUBDEV_G_CLIENT_CAP` / `VIDIOC_SUBDEV_S_CLIENT_CAP` — negotiate subdev API client capabilities (e.g. streams) — source: subdev docs
- `VIDIOC_SUBDEV_QUERYCAP` — query subdev node caps — source: subdev docs
- **no native concurrent-multicamera concept** — V4L2 has no notion of synchronized multi-sensor sessions; each sensor is an independent node/pipeline. Multi-cam genlock is a hardware/media-graph concern, not a V4L2 session primitive — note — source: kernel media docs (absence)

## fine-grained control
- `VIDIOC_QUERYCTRL` — query a control's type/name/min/max/step/default/flags (struct v4l2_queryctrl, s32 ranges) — source: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/vidioc-queryctrl.html
- `VIDIOC_QUERY_EXT_CTRL` — extended query: s64/u64 ranges, `elem_size`, `elems`, `nr_of_dims`, `dims[]` (arrays/compound) — source: queryctrl doc
- `VIDIOC_QUERYMENU` — enumerate menu/integer-menu item names/values — source: queryctrl doc
- `VIDIOC_G_CTRL` / `VIDIOC_S_CTRL` — get/set one s32 control — source: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/vidioc-g-ctrl.html
- `VIDIOC_G_EXT_CTRLS` / `VIDIOC_S_EXT_CTRLS` / `VIDIOC_TRY_EXT_CTRLS` — get/set/try a batch (struct v4l2_ext_controls, atomic across a class) — source: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/vidioc-g-ext-ctrls.html
- control types: `V4L2_CTRL_TYPE_INTEGER` `V4L2_CTRL_TYPE_BOOLEAN` `V4L2_CTRL_TYPE_MENU` `V4L2_CTRL_TYPE_INTEGER_MENU` `V4L2_CTRL_TYPE_BITMASK` `V4L2_CTRL_TYPE_BUTTON` `V4L2_CTRL_TYPE_INTEGER64` `V4L2_CTRL_TYPE_STRING` `V4L2_CTRL_TYPE_U8` `V4L2_CTRL_TYPE_U16` `V4L2_CTRL_TYPE_U32` `V4L2_CTRL_TYPE_AREA` `V4L2_CTRL_TYPE_RECT` `V4L2_CTRL_TYPE_CTRL_CLASS` — source: queryctrl doc
- control flags: `V4L2_CTRL_FLAG_DISABLED` `_GRABBED` `_READ_ONLY` `_UPDATE` `_INACTIVE` `_SLIDER` `_WRITE_ONLY` `_VOLATILE` `_HAS_PAYLOAD` `_EXECUTE_ON_WRITE` `_MODIFY_LAYOUT` `_DYNAMIC_ARRAY` `_HAS_WHICH_MIN_MAX` — source: queryctrl doc
- `V4L2_CID_BASE` / `V4L2_CID_USER_BASE` — first user-class control id (== BRIGHTNESS) — source: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/control.html
- `V4L2_CID_USER_CLASS` — user control-class descriptor — source: control doc
- `V4L2_CID_BRIGHTNESS` — picture brightness / black level — source: control doc
- `V4L2_CID_CONTRAST` — picture contrast / luma gain — source: control doc
- `V4L2_CID_SATURATION` — color saturation / chroma gain — source: control doc
- `V4L2_CID_HUE` — hue / color balance — source: control doc
- `V4L2_CID_HUE_AUTO` — automatic hue control toggle — source: control doc
- `V4L2_CID_GAMMA` — gamma adjust — source: control doc
- `V4L2_CID_BLACK_LEVEL` — deprecated alias of brightness — source: control doc
- `V4L2_CID_WHITENESS` — grey-scale whiteness (deprecated) — source: control doc
- `V4L2_CID_GAIN` — gain — source: control doc
- `V4L2_CID_AUTOGAIN` — automatic gain/exposure toggle — source: control doc
- `V4L2_CID_AUTO_WHITE_BALANCE` — automatic white balance toggle — source: control doc
- `V4L2_CID_DO_WHITE_BALANCE` — one-shot WB action (button) — source: control doc
- `V4L2_CID_WHITE_BALANCE_TEMPERATURE` — WB color temperature in Kelvin — source: control doc
- `V4L2_CID_RED_BALANCE` / `V4L2_CID_BLUE_BALANCE` — per-channel chroma balance — source: control doc
- `V4L2_CID_EXPOSURE` — exposure (generic user-class) — source: control doc
- `V4L2_CID_SHARPNESS` — sharpness filter — source: control doc
- `V4L2_CID_BACKLIGHT_COMPENSATION` — backlight compensation — source: control doc
- `V4L2_CID_POWER_LINE_FREQUENCY` — anti-flicker mains-frequency filter (menu: `V4L2_CID_POWER_LINE_FREQUENCY_DISABLED` `_50HZ` `_60HZ` `_AUTO`) — source: control doc
- `V4L2_CID_HFLIP` / `V4L2_CID_VFLIP` — horizontal/vertical mirror — source: control doc
- `V4L2_CID_ROTATE` — rotate image by angle — source: control doc
- `V4L2_CID_COLOR_KILLER` — force black & white — source: control doc
- `V4L2_CID_COLORFX` — color effect (menu: NONE/BW/SEPIA/NEGATIVE/EMBOSS/SKETCH/SKY_BLUE/GRASS_GREEN/SKIN_WHITEN/VIVID/AQUA/ART_FREEZE/SILHOUETTE/SOLARIZATION/ANTIQUE/SET_CBCR/SET_RGB) — source: control doc
- `V4L2_CID_COLORFX_RGB` / `V4L2_CID_COLORFX_CBCR` — coefficients for SET_RGB / SET_CBCR effect — source: control doc
- `V4L2_CID_CHROMA_AGC` / `V4L2_CID_CHROMA_GAIN` — chroma auto-gain / chroma gain (TV) — source: control doc
- `V4L2_CID_AUTOBRIGHTNESS` — auto brightness toggle — source: control doc
- `V4L2_CID_BAND_STOP_FILTER` — band-stop (anti-flicker) filter strength — source: ext-ctrls-camera doc
- `V4L2_CID_BG_COLOR` — output device background color — source: control doc
- `V4L2_CID_ILLUMINATORS_1` / `V4L2_CID_ILLUMINATORS_2` — on-board illuminator (torch/LED) toggles — source: control doc
- `V4L2_CID_ALPHA_COMPONENT` — alpha component value — source: control doc
- `V4L2_CID_MIN_BUFFERS_FOR_CAPTURE` / `V4L2_CID_MIN_BUFFERS_FOR_OUTPUT` — read-only min buffer-count hints — source: control doc
- `V4L2_CID_LASTP1` / `V4L2_CID_PRIVATE_BASE` — end of predefined ids / driver-private base — source: control doc
- `V4L2_CID_CAMERA_CLASS` / `V4L2_CID_CAMERA_CLASS_BASE` — camera control-class descriptor/base — source: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/ext-ctrls-camera.html
- `V4L2_CID_EXPOSURE_AUTO` — auto-exposure mode (menu: `V4L2_EXPOSURE_AUTO` `_MANUAL` `_SHUTTER_PRIORITY` `_APERTURE_PRIORITY`) — source: ext-ctrls-camera doc
- `V4L2_CID_EXPOSURE_ABSOLUTE` — exposure time in 100 µs units — source: ext-ctrls-camera doc
- `V4L2_CID_EXPOSURE_AUTO_PRIORITY` — allow dynamic frame rate under auto-exposure — source: ext-ctrls-camera doc
- `V4L2_CID_EXPOSURE_BIAS` / `V4L2_CID_AUTO_EXPOSURE_BIAS` — auto-exposure compensation in EV — source: ext-ctrls-camera doc
- `V4L2_CID_EXPOSURE_METERING` — metering mode (menu: `V4L2_EXPOSURE_METERING_AVERAGE` `_CENTER_WEIGHTED` `_SPOT` `_MATRIX`) — source: ext-ctrls-camera doc
- `V4L2_CID_FOCUS_ABSOLUTE` — absolute focal-point position — source: ext-ctrls-camera doc
- `V4L2_CID_FOCUS_RELATIVE` — relative focus move (write-only) — source: ext-ctrls-camera doc
- `V4L2_CID_FOCUS_AUTO` — continuous autofocus toggle — source: ext-ctrls-camera doc
- `V4L2_CID_AUTO_FOCUS_START` / `V4L2_CID_AUTO_FOCUS_STOP` — single-AF start/stop (button) — source: ext-ctrls-camera doc
- `V4L2_CID_AUTO_FOCUS_STATUS` — AF status bitmask (`V4L2_AUTO_FOCUS_STATUS_IDLE` `_BUSY` `_REACHED` `_FAILED`) — source: ext-ctrls-camera doc
- `V4L2_CID_AUTO_FOCUS_RANGE` — AF distance range (menu: `V4L2_AUTO_FOCUS_RANGE_AUTO` `_NORMAL` `_MACRO` `_INFINITY`) — source: ext-ctrls-camera doc
- `V4L2_CID_IRIS_ABSOLUTE` — absolute aperture/iris — source: ext-ctrls-camera doc
- `V4L2_CID_IRIS_RELATIVE` — relative iris move (write-only) — source: ext-ctrls-camera doc
- `V4L2_CID_PRIVACY` — privacy: block image acquisition (boolean) — source: ext-ctrls-camera doc
- `V4L2_CID_WIDE_DYNAMIC_RANGE` — WDR/HDR-ish toggle — source: ext-ctrls-camera doc
- `V4L2_CID_IMAGE_STABILIZATION` — image stabilization toggle — source: ext-ctrls-camera doc
- `V4L2_CID_AUTO_N_PRESET_WHITE_BALANCE` — WB mode (menu: `V4L2_WHITE_BALANCE_MANUAL` `_AUTO` `_INCANDESCENT` `_FLUORESCENT` `_FLUORESCENT_H` `_HORIZON` `_DAYLIGHT` `_FLASH` `_CLOUDY` `_SHADE`) — source: ext-ctrls-camera doc
- `V4L2_CID_ISO_SENSITIVITY` — ISO equivalent (integer-menu) — source: ext-ctrls-camera doc
- `V4L2_CID_ISO_SENSITIVITY_AUTO` — auto-ISO toggle (menu: `V4L2_CID_ISO_SENSITIVITY_MANUAL` `_AUTO`) — source: ext-ctrls-camera doc
- `V4L2_CID_SCENE_MODE` — scene preset (menu: `V4L2_SCENE_MODE_NONE` `_BACKLIGHT` `_BEACH_SNOW` `_CANDLELIGHT` `_DAWN_DUSK` `_FALL_COLORS` `_FIREWORKS` `_LANDSCAPE` `_NIGHT` `_PARTY_INDOOR` `_PORTRAIT` `_SPORTS` `_SUNSET` `_TEXT`) — source: ext-ctrls-camera doc
- `V4L2_CID_3A_LOCK` — lock AE/AWB/AF (bitmask: `V4L2_LOCK_EXPOSURE` `_WHITE_BALANCE` `_FOCUS`) — source: ext-ctrls-camera doc
- `V4L2_CID_CAMERA_ORIENTATION` — read-only mount orientation (`V4L2_CAMERA_ORIENTATION_FRONT` `_BACK` `_EXTERNAL`) — source: ext-ctrls-camera doc
- `V4L2_CID_CAMERA_SENSOR_ROTATION` — read-only sensor rotation correction (degrees CCW) — source: ext-ctrls-camera doc
- `V4L2_CID_HDR_SENSOR_MODE` — sensor HDR mode (dual-exposure merge) — source: ext-ctrls-camera doc
- control classes beyond user/camera also exist: `V4L2_CID_IMAGE_SOURCE_CLASS_*` (analogue/digital gain, vblank/hblank, test pattern), `V4L2_CID_IMAGE_PROC_CLASS_*` (pixel-rate, link-freq), `V4L2_CID_FLASH_CLASS_*` (LED/flash strobe — torch), `V4L2_CID_CODEC_CLASS_*`, `V4L2_CID_FM_TX/RX_CLASS_*`, `V4L2_CID_JPEG_CLASS_*`, `V4L2_CID_DV_CLASS_*` — source: kernel.org control-class reference index
- ranges/steps — every control's min/max/step/default obtained via QUERYCTRL/QUERY_EXT_CTRL; driver may still clamp on S_CTRL (errors loudly only for out-of-range outside [min,max] on some drivers — silent rounding to step is common) — note — source: queryctrl doc

## mechanical controls
- `V4L2_CID_PAN_ABSOLUTE` — absolute horizontal pan position (arc-seconds) — source: ext-ctrls-camera doc
- `V4L2_CID_PAN_RELATIVE` — relative pan move (write-only) — source: ext-ctrls-camera doc
- `V4L2_CID_PAN_RESET` — reset pan to default (button) — source: ext-ctrls-camera doc
- `V4L2_CID_PAN_SPEED` — continuous pan at given speed — source: ext-ctrls-camera doc
- `V4L2_CID_TILT_ABSOLUTE` — absolute vertical tilt position (arc-seconds) — source: ext-ctrls-camera doc
- `V4L2_CID_TILT_RELATIVE` — relative tilt move (write-only) — source: ext-ctrls-camera doc
- `V4L2_CID_TILT_RESET` — reset tilt to default (button) — source: ext-ctrls-camera doc
- `V4L2_CID_TILT_SPEED` — continuous tilt at given speed — source: ext-ctrls-camera doc
- `V4L2_CID_ZOOM_ABSOLUTE` — absolute optical zoom (focal length) — source: ext-ctrls-camera doc
- `V4L2_CID_ZOOM_RELATIVE` — relative zoom step (write-only) — source: ext-ctrls-camera doc
- `V4L2_CID_ZOOM_CONTINUOUS` — drive zoom lens at speed until stopped — source: ext-ctrls-camera doc
- note — this is V4L2's strong, fully-specified PTZ surface (UVC PTZ cameras map directly); these are optical/mechanical, distinct from digital crop/zoom via SELECTION — source: ext-ctrls-camera doc

## capture modes
- `V4L2_PIX_FMT_MJPEG` ('MJPG') — Motion-JPEG (the dominant UVC compressed mode for high-res/fps) — source: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/pixfmt-reserved.html
- `V4L2_PIX_FMT_JPEG` ('JPEG') — JPEG stream — source: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/pixfmt-compressed.html
- `V4L2_PIX_FMT_H264` ('H264') — H.264 Access Unit per buffer (UVC H.264 webcams) — source: pixfmt-compressed doc
- `V4L2_PIX_FMT_H264_NO_SC` ('AVC1') — H.264 without start codes — source: pixfmt-compressed doc
- `V4L2_PIX_FMT_H264_MVC` ('M264') — H.264 MVC stream — source: pixfmt-compressed doc
- `V4L2_PIX_FMT_HEVC` ('HEVC') — H.265 Access Unit per buffer — source: pixfmt-compressed doc
- `V4L2_PIX_FMT_VP8` ('VP80') / `V4L2_PIX_FMT_VP9` ('VP90') — VP8/VP9 compressed frame per buffer — source: pixfmt-compressed doc
- (stateless codec formats `*_SLICE`/`*_FRAME` exist for M2M decoders — not camera capture; listed in frame-memory note) — source: pixfmt-compressed doc
- `VIDIOC_G_PARM` / `VIDIOC_S_PARM` — streaming params: `timeperframe` (v4l2_fract → frame rate), `capturemode`, `readbuffers`, `extendedmode` (struct v4l2_captureparm) — source: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/vidioc-g-parm.html
- `V4L2_MODE_HIGHQUALITY` — high-quality still capture mode bit in `capturemode` (higher res / frame-combine / noise reduction) — source: g-parm doc
- sensor RAW Bayer (still/RAW path) — `V4L2_PIX_FMT_SRGGB8/10/12/14/16` and CFA siblings (see frame-memory) — source: pixfmt-bayer doc
- UVC still-image capture — UVC defines still-capture Methods 1/2/3; the Linux `uvcvideo` driver historically does **not** expose a separate still trigger to userspace (stills are taken from the video stream); no V4L2 still-trigger ioctl — note — source: UVC 1.5 spec; uvcvideo driver doc
- **no bracketing / ZSL / reprocessing** at V4L2 level — no API to queue per-frame exposure/focus sequences or a zero-shutter-lag ring; bracketing must be driven by per-frame S_EXT_CTRLS timed against DQBUF (no native primitive) — note — source: kernel media docs (absence)

## depth / 3D / calibration
- `V4L2_PIX_FMT_GREY` ('GREY') — 8-bit greyscale (IR/mono streams) — source: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/pixfmt-yuv-luma.html
- `V4L2_PIX_FMT_Y4` / `V4L2_PIX_FMT_Y6` — 4-/6-bit greyscale — source: pixfmt-yuv-luma doc
- `V4L2_PIX_FMT_Y10` ('Y10 ') — 10-bit greyscale in 16-bit words (LSB-aligned) — source: pixfmt-yuv-luma doc
- `V4L2_PIX_FMT_Y12` ('Y12 ') — 12-bit greyscale in 16-bit words — source: pixfmt-yuv-luma doc
- `V4L2_PIX_FMT_Y14` ('Y14 ') — 14-bit greyscale in 16-bit words — source: pixfmt-yuv-luma doc
- `V4L2_PIX_FMT_Y16` ('Y16 ') — 16-bit greyscale, little-endian (depth/IR; actual precision may be lower) — source: pixfmt-yuv-luma doc
- `V4L2_PIX_FMT_Y16_BE` ('Y16 '|(1<<31)) — 16-bit greyscale, big-endian — source: pixfmt-yuv-luma doc
- `V4L2_PIX_FMT_Y10BPACK` ('Y10B') — 10-bit greyscale, bit-packed — source: pixfmt-yuv-luma doc
- `V4L2_PIX_FMT_Y10P` ('Y10P') — 10-bit greyscale, MIPI-packed — source: pixfmt-yuv-luma doc
- `V4L2_PIX_FMT_Y12P` / `V4L2_PIX_FMT_Y14P` — 12-/14-bit MIPI-packed greyscale — source: pixfmt-yuv-luma doc
- `V4L2_PIX_FMT_Y8I` ('Y8I ') — interleaved 8-bit greyscale (stereo IR pair, e.g. RealSense) — source: pixfmt-yuv-luma doc
- `V4L2_PIX_FMT_Y12I` ('Y12I') — interleaved 12-bit greyscale (two sources bit-packed in 24-bit word) — source: pixfmt-yuv-luma doc
- `V4L2_PIX_FMT_Z16` ('Z16 ') — 16-bit depth map — source: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/depth-formats.html
- `V4L2_PIX_FMT_CNF4` ('CNF4') — 4-bit confidence map (depth) — source: depth-formats doc
- `V4L2_PIX_FMT_INZI` ('INZI') — planar IR (Y10) + depth (Z16) interleaved (RealSense) — source: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/pixfmt-inzi.html
- RealSense-style metadata — depth/IR per-frame metadata (intrinsics/extrinsics/capture-stats) carried via the UVC metadata node as `V4L2_META_FMT_D4XX` (MS-UVC extension blocks: CaptureStats id 3, CameraExtrinsics id 4, CameraIntrinsics id 5) — source: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/metafmt-d4xx.html
- **no calibration API** — V4L2 has no standard intrinsics/distortion/extrinsics query; calibration only arrives as vendor metadata (D4XX) or out-of-band — note — source: kernel media docs (absence)

## live effects
- **none at V4L2 level** — auto-framing / Center-Stage / background-blur / eye-contact / studio-light are not V4L2 concepts; where present they are vendor firmware behind UVC Extension Units (opaque) or done in userspace (libcamera/PipeWire). `V4L2_CID_COLORFX` is only a fixed color-effect catalog, not subject-aware effects — state — source: kernel media docs (absence); uvcvideo XU doc

## frame memory
- `VIDIOC_REQBUFS` — allocate a buffer pool (count, type, memory); returns `capabilities` bitfield — source: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/vidioc-reqbufs.html
- `VIDIOC_CREATE_BUFS` — allocate additional buffers of a (possibly different) format; `max_num_buffers` in v4l2_create_buffers — source: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/vidioc-create-bufs.html
- `VIDIOC_REMOVE_BUFS` — free a sub-range of buffers (newer kernels) — source: user-func doc
- `VIDIOC_PREPARE_BUF` — pre-validate/pin a buffer before queueing — source: user-func doc
- `VIDIOC_QUERYBUF` — query a buffer's state/offset/length for mmap — source: user-func doc
- `VIDIOC_QBUF` / `VIDIOC_DQBUF` — enqueue/dequeue a buffer (struct v4l2_buffer) — source: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/vidioc-qbuf.html
- `VIDIOC_STREAMON` / `VIDIOC_STREAMOFF` — start/stop streaming for a buffer type — source: user-func doc
- `VIDIOC_EXPBUF` — export an MMAP buffer as a **DMABUF fd** (struct v4l2_exportbuffer: type/index/plane/flags/fd) → zero-copy to GPU/DRM — source: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/vidioc-expbuf.html
- memory models: `V4L2_MEMORY_MMAP` (kernel-allocated, mmap), `V4L2_MEMORY_USERPTR` (app-allocated), `V4L2_MEMORY_DMABUF` (import external dmabuf — zero-copy GPU→V4L2), `V4L2_MEMORY_OVERLAY` — source: videodev2.h
- `V4L2_MEMORY_FLAG_NON_COHERENT` — request non-coherent (cached) buffer memory (with cache-hint flags) — source: reqbufs doc
- reqbufs caps: `V4L2_BUF_CAP_SUPPORTS_MMAP` `_USERPTR` `_DMABUF` `_REQUESTS` `_ORPHANED_BUFS` `_M2M_HOLD_CAPTURE_BUF` `_MMAP_CACHE_HINTS` `_MAX_NUM_BUFFERS` `_REMOVE_BUFS` — source: reqbufs doc
- multiplanar — `fmt.pix_mp` carries `num_planes`, per-plane `bytesperline`/`sizeimage`; buffer carries `struct v4l2_plane[]` — source: pixfmt-v4l2-mplane doc
- packed YUV 4:2:2: `V4L2_PIX_FMT_YUYV` `V4L2_PIX_FMT_UYVY` `V4L2_PIX_FMT_YVYU` `V4L2_PIX_FMT_VYUY` — source: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/pixfmt-packed-yuv.html
- packed YUV 4:2:2 high-depth: `V4L2_PIX_FMT_Y210` `V4L2_PIX_FMT_Y212` `V4L2_PIX_FMT_Y216` (10/12/16-bit) — source: pixfmt-packed-yuv doc
- semi-planar: `V4L2_PIX_FMT_NV12` `V4L2_PIX_FMT_NV21` (4:2:0), `V4L2_PIX_FMT_NV16` `V4L2_PIX_FMT_NV61` (4:2:2), `V4L2_PIX_FMT_NV24` `V4L2_PIX_FMT_NV42` (4:4:4) — source: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/pixfmt-yuv-planar.html
- semi-planar non-contiguous: `V4L2_PIX_FMT_NV12M` `V4L2_PIX_FMT_NV21M` — source: pixfmt-yuv-planar doc
- tiled: `V4L2_PIX_FMT_NV12MT` (64x32 Z-order), `V4L2_PIX_FMT_NV12_4L4`, `V4L2_PIX_FMT_NV12_16L16` — source: pixfmt-yuv-planar doc
- 10-bit semi-planar: `V4L2_PIX_FMT_NV15` (packed), `V4L2_PIX_FMT_P010` (16-bit container), `V4L2_PIX_FMT_P012` (12-bit), `V4L2_PIX_FMT_P210` (4:2:2) — source: pixfmt-yuv-planar doc
- planar: `V4L2_PIX_FMT_YUV420` `V4L2_PIX_FMT_YVU420` (4:2:0), `V4L2_PIX_FMT_YUV422P` (4:2:2), `V4L2_PIX_FMT_YUV411P` (4:1:1), `V4L2_PIX_FMT_YUV410` `V4L2_PIX_FMT_YVU410` (4:1:0), `V4L2_PIX_FMT_YUV444M` (4:4:4) — source: pixfmt-yuv-planar doc
- planar non-contiguous: `V4L2_PIX_FMT_YUV420M` `V4L2_PIX_FMT_YVU420M` `V4L2_PIX_FMT_YUV422M` — source: pixfmt-yuv-planar doc
- RGB packed: `V4L2_PIX_FMT_RGB332` `V4L2_PIX_FMT_RGB565` `V4L2_PIX_FMT_RGB565X` `V4L2_PIX_FMT_RGB555` `V4L2_PIX_FMT_RGB444` `V4L2_PIX_FMT_ARGB444` `V4L2_PIX_FMT_XRGB444` `V4L2_PIX_FMT_RGB24` `V4L2_PIX_FMT_BGR24` — source: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/pixfmt-rgb.html
- RGB 32-bit: `V4L2_PIX_FMT_RGB32` `V4L2_PIX_FMT_BGR32` `V4L2_PIX_FMT_ARGB32` `V4L2_PIX_FMT_XRGB32` `V4L2_PIX_FMT_ABGR32` `V4L2_PIX_FMT_XBGR32` `V4L2_PIX_FMT_RGBA32` `V4L2_PIX_FMT_RGBX32` `V4L2_PIX_FMT_BGRA32` `V4L2_PIX_FMT_BGRX32` — source: pixfmt-rgb doc
- RGB high-depth: `V4L2_PIX_FMT_RGB48` `V4L2_PIX_FMT_BGR48` `V4L2_PIX_FMT_ARGB2101010` `V4L2_PIX_FMT_RGBA1010102` — source: pixfmt-rgb doc
- Bayer 8-bit: `V4L2_PIX_FMT_SBGGR8` `V4L2_PIX_FMT_SGBRG8` `V4L2_PIX_FMT_SGRBG8` `V4L2_PIX_FMT_SRGGB8` — source: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/pixfmt-bayer.html
- Bayer 10-bit: `V4L2_PIX_FMT_SBGGR10` `V4L2_PIX_FMT_SGBRG10` `V4L2_PIX_FMT_SGRBG10` `V4L2_PIX_FMT_SRGGB10` + packed `V4L2_PIX_FMT_SBGGR10P` + compressed `V4L2_PIX_FMT_SBGGR10ALAW8` `V4L2_PIX_FMT_SBGGR10DPCM8` — source: pixfmt-bayer doc
- Bayer 12-bit: `V4L2_PIX_FMT_SBGGR12` `V4L2_PIX_FMT_SGBRG12` `V4L2_PIX_FMT_SGRBG12` `V4L2_PIX_FMT_SRGGB12` + packed `V4L2_PIX_FMT_SBGGR12P` — source: pixfmt-bayer doc
- Bayer 14-bit: `V4L2_PIX_FMT_SBGGR14` `V4L2_PIX_FMT_SGBRG14` `V4L2_PIX_FMT_SGRBG14` `V4L2_PIX_FMT_SRGGB14` + packed `V4L2_PIX_FMT_SBGGR14P` — source: pixfmt-bayer doc
- Bayer 16-bit: `V4L2_PIX_FMT_SBGGR16` `V4L2_PIX_FMT_SGBRG16` `V4L2_PIX_FMT_SGRBG16` `V4L2_PIX_FMT_SRGGB16` — source: pixfmt-bayer doc
- per-frame plane layout — `bytesperline` (stride) and `sizeimage` carried in v4l2_pix_format / per v4l2_plane — source: pixfmt docs
- queue depth — chosen by REQBUFS/CREATE_BUFS count; `V4L2_CID_MIN_BUFFERS_FOR_CAPTURE` is the driver's floor hint — source: control + reqbufs docs
- frame-drop reporting — `V4L2_BUF_FLAG_ERROR` on DQBUF marks a corrupt/dropped buffer (payload may still be returned); `sequence` gaps reveal missed frames — source: vidioc-qbuf doc

## timing
- `struct v4l2_buffer.timestamp` — per-buffer capture timestamp (timeval) — source: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/buffer.html
- `struct v4l2_buffer.sequence` — monotonically increasing frame counter; gaps = dropped frames — source: buffer doc
- timestamp source/type flags: `V4L2_BUF_FLAG_TIMESTAMP_MASK` `V4L2_BUF_FLAG_TIMESTAMP_UNKNOWN` `V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC` (CLOCK_MONOTONIC) `V4L2_BUF_FLAG_TIMESTAMP_COPY` (copied from OUTPUT buffer — loopback/M2M) — source: buffer doc
- timestamp capture point: `V4L2_BUF_FLAG_TSTAMP_SRC_MASK` `V4L2_BUF_FLAG_TSTAMP_SRC_EOF` (end-of-frame) `V4L2_BUF_FLAG_TSTAMP_SRC_SOE` (start-of-exposure) — source: buffer doc
- frame-type flags: `V4L2_BUF_FLAG_KEYFRAME` `V4L2_BUF_FLAG_PFRAME` `V4L2_BUF_FLAG_BFRAME` — source: buffer doc
- `V4L2_BUF_FLAG_TIMECODE` + `struct v4l2_timecode` — SMPTE-style timecode attached to a buffer (`V4L2_TC_TYPE_*`, `V4L2_TC_FLAG_*`) — source: buffer doc
- state flags: `V4L2_BUF_FLAG_MAPPED` `_QUEUED` `_DONE` `_PREPARED` `_LAST` `_ERROR` `_REQUEST_FD` `_IN_REQUEST` `_NO_CACHE_INVALIDATE` `_NO_CACHE_CLEAN` `_M2M_HOLD_CAPTURE_BUF` — source: buffer doc
- `VIDIOC_SUBSCRIBE_EVENT` / `VIDIOC_UNSUBSCRIBE_EVENT` / `VIDIOC_DQEVENT` — subscribe/dequeue async events — source: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/vidioc-dqevent.html
- events: `V4L2_EVENT_VSYNC` (vertical sync) `V4L2_EVENT_EOS` (end of stream) `V4L2_EVENT_CTRL` (control value/flags/range changed) `V4L2_EVENT_FRAME_SYNC` (frame reception started) `V4L2_EVENT_SOURCE_CHANGE` (source param changed) `V4L2_EVENT_MOTION_DET` (motion-detect region state) `V4L2_EVENT_ALL` `V4L2_EVENT_PRIVATE_START` — source: dqevent doc
- ctrl-change flags: `V4L2_EVENT_CTRL_CH_VALUE` `_FLAGS` `_RANGE` `_DIMENSIONS`; source-change flag: `V4L2_EVENT_SRC_CH_RESOLUTION` — source: dqevent doc
- UVC per-frame metadata — `V4L2_META_FMT_UVC` node yields struct uvc_meta_buf: `ts` (host CLOCK_MONOTONIC ns), `sof` (USB frame number), `length`, `flags`, `buf[]` (raw UVC payload header carrying **PTS** presentation timestamp + **SCR** source-clock = STC + SOF token) — enables device-clock↔host-clock correlation and rolling-shutter/PTS reconstruction — source: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/metafmt-uvc.html
- `V4L2_BUF_TYPE_META_CAPTURE` — the buffer type the UVC metadata node streams — source: metafmt-uvc doc
- rolling shutter — exposed only indirectly: sensor subdev exposure/line-time controls + SOE timestamps; no dedicated skew field — note — source: kernel media docs
- DV-timings (capture cards): `VIDIOC_QUERY_DV_TIMINGS` `VIDIOC_G_DV_TIMINGS` `VIDIOC_S_DV_TIMINGS` `VIDIOC_ENUM_DV_TIMINGS` `VIDIOC_DV_TIMINGS_CAP` — detect/lock incoming HDMI/SDI signal timing — source: user-func doc

## metadata
- `V4L2_BUF_TYPE_META_CAPTURE` / `V4L2_BUF_TYPE_META_OUTPUT` — metadata stream buffer types (separate video node) — source: videodev2.h
- `V4L2_META_FMT_UVC` ('UVCH') — standard UVC payload-header metadata (PTS/SCR/SOF) — source: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/meta-formats.html
- `V4L2_META_FMT_UVC_MSXU_1_5` ('UVCM') — UVC Microsoft Extension Unit 1.5 metadata — source: meta-formats doc
- `V4L2_META_FMT_D4XX` ('D4XX') — Intel RealSense D4xx per-frame metadata (over UVC) — source: metafmt-d4xx doc
- ISP statistics/params metadata (sensor pipelines): `V4L2_META_FMT_RK_ISP1_PARAMS` ('rk1p') / `V4L2_META_FMT_RK_ISP1_STAT_3A` ('rk1s') / `V4L2_META_FMT_RK_ISP1_EXT_PARAMS` ('rk1e') — Rockchip ISP1; `V4L2_META_FMT_IPU3_PARAMS` ('ip3p') / `V4L2_META_FMT_IPU3_3A` ('ip3s') — Intel IPU3 3A; `V4L2_META_FMT_VSP1_HGO` ('VSPH') / `V4L2_META_FMT_VSP1_HGT` ('VSPT') — Renesas histogram; `V4L2_META_FMT_MALI_C55_STATS`/`_PARAMS`; `V4L2_META_FMT_C3ISP_STATS`/`_PARAMS`; `V4L2_META_FMT_RPI_BE_CFG`/`_FE_CFG`/`_FE_STATS` — source: meta-formats doc
- generic CSI-2 metadata lines: `V4L2_META_FMT_GENERIC_8` ('MET8') `V4L2_META_FMT_GENERIC_CSI2_10` `_12` `_14` `_16` `_20` `_24` — source: meta-formats doc
- `V4L2_META_FMT_VIVID` ('VIVD') — test-driver metadata — source: meta-formats doc
- **no face/scene detection at kernel level** — V4L2 emits sensor-line statistics for an ISP, not face rects / scene classification / barcode payloads; only `V4L2_EVENT_MOTION_DET` (coarse motion-region event) exists. Higher-level detection is userspace (libcamera/CV) — note — source: kernel media docs (absence)

## egress (virtual camera PUBLISH)
- `v4l2loopback` — out-of-tree kernel module; `modprobe v4l2loopback` creates `/dev/videoN` nodes that other apps open as ordinary cameras; an app **writes** frames in — source: https://github.com/v4l2loopback/v4l2loopback
- producer path — open O_RDWR, `VIDIOC_S_FMT` with `type = V4L2_BUF_TYPE_VIDEO_OUTPUT` (pixelformat/width/height/bytesperline/sizeimage), then either `write()` one frame per call **or** streaming `VIDIOC_REQBUFS`+`VIDIOC_QBUF`/`VIDIOC_DQBUF`+`VIDIOC_STREAMON` with `V4L2_MEMORY_MMAP` — source: V4L2 output API; v4l2loopback examples
- `exclusive_caps` module option — node reports only OUTPUT caps until a producer attaches, then flips to CAPTURE-only (required by Chromium/WebRTC consumers) — source: v4l2loopback docs / ArchWiki
- controls/format on the published node are those v4l2loopback synthesizes; the producer's `S_FMT` defines what consumers see; no PTZ/3A controls unless the loopback exposes them — source: v4l2loopback docs
- general V4L2 output devices — `V4L2_CAP_VIDEO_OUTPUT`, `VIDIOC_ENUMOUTPUT`, `V4L2_BUF_TYPE_VIDEO_OUTPUT(_MPLANE)` are the standard output surface v4l2loopback rides on — source: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/dev-output.html
- note — requires the v4l2loopback module **installed** (DKMS/out-of-tree); not in mainline. The alternative consent-correct publish path (PipeWire virtual node) is a different axis (linux+pipewire), not V4L2 — source: v4l2loopback README

## OS integration
- **no built-in permission/consent model** — access is POSIX file permissions on `/dev/videoN` (group `video`, or per-session ACL via systemd-logind `uaccess` udev tag); no per-app prompt — note: this absence is precisely why sandboxed apps use the xdg-desktop-portal/PipeWire path (separate axis) — source: udev/logind docs; kernel media docs (absence)
- arbitration — only `open()` returning `EBUSY` when a node is exclusively held; `VIDIOC_G_PRIORITY` / `VIDIOC_S_PRIORITY` with `V4L2_PRIORITY_UNSET` `_BACKGROUND` `_INTERACTIVE` `_RECORD` `_DEFAULT` cooperatively gate who may change format/controls — no preemption, no shared-session concept — source: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/vidioc-g-priority.html
- orientation — read-only `V4L2_CID_CAMERA_ORIENTATION` + `V4L2_CID_CAMERA_SENSOR_ROTATION` report mount, but **no runtime UI-orientation / mirroring standard**; HFLIP/VFLIP are manual — source: ext-ctrls-camera doc
- privacy — `V4L2_CID_PRIVACY` (boolean, where firmware exposes it) blocks acquisition; **no standard privacy-LED state readout** for UVC (the in-use LED is hardware-driven by the camera, not reported via V4L2) — note — source: ext-ctrls-camera doc; UVC driver doc
- **no thermal / power-aware hook** — V4L2 has no thermal-throttle or power-state callback; degradation is the driver's silent business — note — source: kernel media docs (absence)
- `VIDIOC_LOG_STATUS` — ask driver to dump its state to the kernel log (diagnostic) — source: user-func doc

## obscure corners
- `UVCIOC_CTRL_MAP` — driver-private ioctl: map a UVC Extension-Unit control (by GUID `entity[16]` + `selector` + bit offset/size) onto a new V4L2 control id so it enumerates normally (struct uvc_xu_control_mapping; `enum uvc_control_data_type`: `UVC_CTRL_DATA_TYPE_RAW` `_SIGNED` `_UNSIGNED` `_BOOLEAN` `_ENUM` `_BITMASK` — note: this is one of V4L2's only legitimate enum surfaces) — source: https://docs.kernel.org/userspace-api/media/drivers/uvcvideo.html
- `UVCIOC_CTRL_QUERY` — driver-private ioctl: raw GET/SET to a UVC XU control by unit+selector (struct uvc_xu_control_query) — vendor controls, firmware upload, LED control — source: uvcvideo doc
- `V4L2_CID_BAND_STOP_FILTER` — rarely-implemented anti-banding band-stop filter — source: ext-ctrls-camera doc
- `V4L2_CID_WIDE_DYNAMIC_RANGE` / `V4L2_CID_HDR_SENSOR_MODE` — sparsely-implemented HDR toggles — source: ext-ctrls-camera doc
- `V4L2_CID_PAN_SPEED` / `V4L2_CID_TILT_SPEED` / `V4L2_CID_ZOOM_CONTINUOUS` — continuous-motion PTZ, only on motorized conference cams — source: ext-ctrls-camera doc
- `V4L2_PIX_FMT_SBGGR10ALAW8` / `V4L2_PIX_FMT_SBGGR10DPCM8` — compressed/companded 10-bit Bayer (obscure sensor links) — source: pixfmt-bayer doc
- `V4L2_PIX_FMT_NV12MT` / `V4L2_PIX_FMT_NV12_4L4` / `V4L2_PIX_FMT_NV12_16L16` — vendor tiled YUV layouts (GPU/codec import) — source: pixfmt-yuv-planar doc
- `V4L2_PIX_FMT_HEVC` / `V4L2_PIX_FMT_VP8` / `V4L2_PIX_FMT_VP9` — UVC-coded streams seldom seen from webcams vs MJPEG/H264 — source: pixfmt-compressed doc
- `V4L2_BUF_FLAG_TIMESTAMP_COPY` — only meaningful on M2M/loopback (timestamp copied from the OUTPUT buffer) — source: buffer doc
- `V4L2_EVENT_MOTION_DET` — coarse hardware motion-detection region event (few drivers) — source: dqevent doc
- `VIDIOC_DBG_G_REGISTER` / `VIDIOC_DBG_S_REGISTER` / `VIDIOC_DBG_G_CHIP_INFO` — raw sensor-register debug poke (CONFIG_VIDEO_ADV_DEBUG only) — source: user-func doc
- `VIDIOC_S_HW_FREQ_SEEK` / `VIDIOC_ENUM_FREQ_BANDS` / tuner ioctls — radio/TV-only, present in the same ioctl namespace — source: user-func doc
- `VIDIOC_G_EDID` / `VIDIOC_S_EDID` — read/write EDID on capture-card HDMI inputs — source: user-func doc
- `VIDIOC_SUBDEV_ENUM_MBUS_CODE` / `VIDIOC_SUBDEV_ENUM_FRAME_SIZE` / `VIDIOC_SUBDEV_ENUM_FRAME_INTERVAL` — enumerate media-bus formats/sizes/intervals on a sensor subdev pad — source: subdev docs
- `VIDIOC_SUBDEV_G_FRAME_INTERVAL` / `VIDIOC_SUBDEV_S_FRAME_INTERVAL` — per-subdev frame interval — source: subdev docs
- `MEDIA_IOC_DEVICE_INFO` / `MEDIA_IOC_ENUM_ENTITIES` / `MEDIA_IOC_ENUM_LINKS` / `MEDIA_IOC_SETUP_LINK` / `MEDIA_IOC_G_TOPOLOGY` — media-controller graph: entities/pads/links, enable/disable links to route an ISP/sensor pipeline — source: https://www.kernel.org/doc/html/latest/userspace-api/media/mediactl/media-funcs.html
- `MEDIA_IOC_REQUEST_ALLOC` + `V4L2_BUF_FLAG_REQUEST_FD` — the **Request API**: batch per-frame control+buffer state and apply atomically on a specific frame (the closest V4L2 has to per-frame bracketing; mainly used by stateless codecs/ISPs, rarely by capture) — source: https://www.kernel.org/doc/html/latest/userspace-api/media/mediactl/request-api.html
- `V4L2_PIX_FMT_Y16_BE` — big-endian 16-bit greyscale, FourCC has bit 31 set (`'Y16 ' | (1<<31)`) — obscure encoding trick — source: pixfmt-yuv-luma doc
- `V4L2_CID_ILLUMINATORS_1` / `_2` — board illuminator/torch toggles repurposed by some IR cameras — source: control doc
- `VIDIOC_ENUMAUDIO` / `VIDIOC_G_AUDIO` / `VIDIOC_S_AUDIO` — V4L2's *own* audio-input selection (analog TV-card audio mux), **not** a capture mic and unrelated to ALSA/UVC audio — easy false-friend — source: user-func doc
