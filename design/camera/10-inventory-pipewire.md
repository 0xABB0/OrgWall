# pipewire — inventory
> api gen: xdg-desktop-portal Camera + PipeWire (libcamera/V4L2-backed)
> covers matrix columns: linux+pipewire

> This axis is a **transport + consent** layer, not a capture driver. The portal is the
> sole sandbox/Wayland-era consent gate; PipeWire is the buffer transport (incl. dmabuf
> zero-copy) and the node graph. Nearly all *device capability* (controls, formats,
> readout modes, 3A) is **DELEGATED to the V4L2 or libcamera backend** that feeds the
> PipeWire camera node — PipeWire forwards a thin, lossy subset. This axis's distinct
> value: (1) the consent flow, (2) dmabuf transport to GPU, (3) publishing a virtual
> camera node. Where breadth comes from the backend, this file points there and marks it.

## devices & enumeration
- `org.freedesktop.portal.Camera.IsCameraPresent` — read-only `b` property; true iff any camera is available to the app — source: [portal Camera docs](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Camera.html)
- `org.freedesktop.portal.Camera.version` — read-only `u` property; interface protocol version — source: [portal Camera docs](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Camera.html)
- `org.freedesktop.portal.Camera.AccessCamera(options a{sv}) → handle o` — request camera access; returns a Request object path; consent prompt fires before grant — source: [portal Camera docs](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Camera.html)
- `AccessCamera options.handle_token (s)` — last element of the Request `handle` object path; **the only documented option key** (no device-type / `types` filter exists on this portal — that key lives on ScreenCast, not Camera) — source: [portal Camera XML](https://github.com/flatpak/xdg-desktop-portal/blob/main/data/org.freedesktop.portal.Camera.xml)
- `org.freedesktop.portal.Camera.OpenPipeWireRemote(options a{sv}) → fd h` — returns an fd to a PipeWire remote pre-restricted to camera nodes; "only succeeds if the application already has permission" — source: [portal Camera docs](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Camera.html)
- `OpenPipeWireRemote options` — currently **no option keys supported** — source: [portal Camera XML](https://github.com/flatpak/xdg-desktop-portal/blob/main/data/org.freedesktop.portal.Camera.xml)
- `org.freedesktop.portal.Request::Response (u response, a{sv} results)` — async result of `AccessCamera`; `response` = 0 success / 1 cancelled / 2 error — source: [portal Request docs](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Request.html)
- `pw_context_connect_fd(context, fd, properties, user_data_size)` — wrap the portal fd into a `pw_core`; **the enumeration entry point** — the portal fd is a restricted remote that only exposes camera globals — source: [PipeWire tutorial 5](https://docs.pipewire.org/page_tutorial5.html)
- `pw_core_get_registry(core, PW_VERSION_REGISTRY, 0)` — obtain the registry proxy to enumerate globals on the (camera-restricted) remote — source: [PipeWire registry](https://docs.pipewire.org/group__pw__registry.html)
- `pw_registry_add_listener(registry, hook, events, data)` — attach a `pw_registry_events` listener — source: [PipeWire tutorial 2](https://docs.pipewire.org/page_tutorial2.html)
- `pw_registry_events.global(data, id, permissions, type, version, props a{ss})` — **hot-plug arrival**: fired for each global; filter `type == PW_TYPE_INTERFACE_Node` and inspect `props` for camera markers — source: [PipeWire registry](https://docs.pipewire.org/group__pw__registry.html)
- `pw_registry_events.global_remove(data, id)` — **hot-plug removal**: global with `id` is gone — source: [PipeWire registry](https://docs.pipewire.org/group__pw__registry.html)
- `media.class = "Video/Source"` (`PW_KEY_MEDIA_CLASS`) — node prop marking a camera/video source the session manager routes as a capture target — source: [pipewire-props man](https://docs.pipewire.org/page_man_pipewire-props_7.html)
- `media.role = "Camera"` (`PW_KEY_MEDIA_ROLE`) — node prop tagging the use-case as camera recording; distinguishes camera sources from screencast/other Video/Source nodes — source: [pipewire-props man](https://docs.pipewire.org/page_man_pipewire-props_7.html)
- `node.name` (`PW_KEY_NODE_NAME`) — unique node name, e.g. `v4l2_input.pci-0000_05_00.3-usb-0_3_1.0` — source: [pipewire-props man](https://docs.pipewire.org/page_man_pipewire-props_7.html)
- `node.description` (`PW_KEY_NODE_DESCRIPTION`) — human-readable name, e.g. "Integrated Camera" — source: [pipewire-props man](https://docs.pipewire.org/page_man_pipewire-props_7.html)
- `object.path` (`PW_KEY_OBJECT_PATH`) — backend object path, e.g. `v4l2:/dev/video0` — reveals which backend (v4l2 vs libcamera) fronts the node — source: [pipewire-props man](https://docs.pipewire.org/page_man_pipewire-props_7.html)
- `device.id` (`PW_KEY_DEVICE_ID`) — id of the owning Device global the node hangs off — source: [pipewire-props man](https://docs.pipewire.org/page_man_pipewire-props_7.html)
- `pw_registry_bind(registry, id, type, version, user_data_size)` — bind a node global to get a `pw_node` proxy for its events/params — source: [PipeWire registry](https://docs.pipewire.org/group__pw__registry.html)
- consent-gated enumeration — the portal fd restricts the registry so **only camera nodes are visible**; without `AccessCamera` grant, `OpenPipeWireRemote` fails and no camera globals appear (vs a raw `pw_context_connect()` which would expose the whole graph) — source: [portal Camera docs](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Camera.html)
- camera node **vs** portal fd — the fd is the *connection*; camera nodes are *globals* on it. You never get a raw `/dev/videoN` fd; the backend opens the device, PipeWire owns the buffers — source: [PipeWire is the new v4l2loopback](https://www.ideasonboard.com/news/pipewire-is-the-new-v4l2loopback/)

## topology & streams
- `pw_stream_new(core, name, props)` / `pw_stream_new_simple(loop, name, props, events, data)` — create a capture stream; camera streams set `PW_KEY_MEDIA_TYPE="Video"`, `PW_KEY_MEDIA_CATEGORY="Capture"`, `PW_KEY_MEDIA_ROLE="Camera"` — source: [PipeWire tutorial 5](https://docs.pipewire.org/page_tutorial5.html)
- `pw_stream_connect(stream, direction, target_id, flags, params, n_params)` — connect; camera ingest uses `PW_DIRECTION_INPUT`, `target_id = PW_ID_ANY` or a specific node id — source: [PipeWire tutorial 5](https://docs.pipewire.org/page_tutorial5.html)
- `PW_DIRECTION_INPUT` — stream consumes data (camera capture) — source: [PipeWire streams](https://docs.pipewire.org/page_streams.html)
- `PW_STREAM_FLAG_AUTOCONNECT` — let the session manager link the stream to a node automatically — source: [PipeWire streams](https://docs.pipewire.org/page_streams.html)
- `PW_STREAM_FLAG_MAP_BUFFERS` — PipeWire mmaps shm buffers for you (NOT used for dmabuf — those must be imported via the graphics API) — source: [PipeWire streams](https://docs.pipewire.org/page_streams.html) · [DMA-BUF sharing](https://docs.pipewire.org/page_dma_buf.html)
- `PW_STREAM_FLAG_RT_PROCESS` — run the `process` callback in the realtime data thread — source: [PipeWire streams](https://docs.pipewire.org/page_streams.html)
- `PW_KEY_TARGET_OBJECT` — stream prop naming the target node to connect to (the chosen camera node) — source: [PipeWire streams](https://docs.pipewire.org/page_streams.html)
- `SPA_PARAM_EnumFormat` — node param enumerating supported formats as `SPA_TYPE_OBJECT_Format` PODs (the backend's format list, surfaced as choices) — source: [SPA POD](https://docs.pipewire.org/page_spa_pod.html)
- `SPA_PARAM_Format` — the single negotiated/active format POD after fixation — source: [SPA param](https://docs.pipewire.org/group__spa__param.html)
- `pw_stream_update_params(stream, params, n_params)` — complete negotiation by replying to a `param_changed` with refined params (e.g. announce buffer reqs) — source: [PipeWire streams](https://docs.pipewire.org/page_streams.html)
- `pw_stream_events.param_changed(data, id, param)` — backend emitted a new param (e.g. `SPA_PARAM_Format` fixed) — source: [PipeWire streams](https://docs.pipewire.org/page_streams.html)
- `pw_stream_events.state_changed(data, old, state, error)` — lifecycle: `PW_STREAM_STATE_UNCONNECTED/CONNECTING/PAUSED/STREAMING/ERROR` — source: [PipeWire streams](https://docs.pipewire.org/page_streams.html)
- multiple streams — open several `pw_stream`s against several camera nodes; multi-stream/multi-cam concurrency is **bounded by the backend** (V4L2 single-open per `/dev/videoN`; libcamera per-camera) not by PipeWire — source: [PipeWire streams](https://docs.pipewire.org/page_streams.html) · `?` concurrency limits backend-specific
- backend surfacing — `object.path = v4l2:...` ⇒ V4L2 plugin; libcamera plugin fronts cameras the V4L2 stack can't (ISP-pipelined, Intel IPU6); **which backend is active is a daemon config choice**, and format/control breadth differs between them — source: [Integrating libcamera into PipeWire](https://www.collabora.com/news-and-blog/blog/2020/09/11/integrating-libcamera-into-pipewire/)

## fine-grained control
- `SPA_PARAM_Props` — read/write node param carrying live property values as `SPA_TYPE_OBJECT_Props` — the control-write channel — source: [SPA POD](https://docs.pipewire.org/page_spa_pod.html)
- `SPA_PARAM_PropInfo` — enumerates each prop's id/type/range/labels (so you can query supported range/step — avoids silent clamp) — source: [SPA param](https://docs.pipewire.org/group__spa__param.html)
- `SPA_PROP_brightness` — video brightness control (`SPA_PROP_START_Video` = 0x20000 region) — source: [spa/param/props.h source](https://docs.pipewire.org/props_8h_source.html)
- `SPA_PROP_contrast` — video contrast — source: [spa/param/props.h source](https://docs.pipewire.org/props_8h_source.html)
- `SPA_PROP_saturation` — video saturation — source: [spa/param/props.h source](https://docs.pipewire.org/props_8h_source.html)
- `SPA_PROP_hue` — video hue — source: [spa/param/props.h source](https://docs.pipewire.org/props_8h_source.html)
- `SPA_PROP_gamma` — video gamma — source: [spa/param/props.h source](https://docs.pipewire.org/props_8h_source.html)
- `SPA_PROP_exposure` — video exposure — source: [spa/param/props.h source](https://docs.pipewire.org/props_8h_source.html)
- `SPA_PROP_gain` — video gain — source: [spa/param/props.h source](https://docs.pipewire.org/props_8h_source.html)
- `SPA_PROP_sharpness` — video sharpness — source: [spa/param/props.h source](https://docs.pipewire.org/props_8h_source.html)
- **DELEGATION** — the *named* video props above are the **entire** PipeWire-native camera control set (8 controls). Everything richer — white balance Kelvin, focus distance, ISO, aperture, zoom, flicker/power-line-freq, PTZ, full ISP/3A — has **no named SPA_PROP**; the V4L2/libcamera backend exposes them as **device-specific controls** mapped into the `SPA_PROP_START_CUSTOM` (0x1000000) region, discoverable only via `SPA_PARAM_PropInfo` (`SPA_PROP_INFO_id/name/type/labels/description/container/params`). Control *breadth* is backend-limited and not portable — source: [spa/param/props.h source](https://docs.pipewire.org/props_8h_source.html)
- `SPA_PROP_INFO_id / _name / _type / _labels / _container / _params / _description` — per-prop metadata so an app can discover backend custom controls by name + range — source: [spa/param/props.h source](https://docs.pipewire.org/props_8h_source.html)
- choice encoding — props advertise allowed values via POD Choice: `SPA_CHOICE_Range` (default/min/max), `SPA_CHOICE_Step` (adds step), `SPA_CHOICE_Enum` (discrete set) — gives range/step without a silent clamp — source: [SPA POD](https://docs.pipewire.org/page_spa_pod.html)

## mechanical controls
- PTZ via SPA props — **`?` / backend-dependent**. No named `SPA_PROP_pan/tilt/zoom`. UVC PTZ controls would surface (if at all) as V4L2 controls in `SPA_PROP_START_CUSTOM` via the v4l2 plugin; not exposed as a first-class PipeWire concept and not guaranteed forwarded — source: [spa/param/props.h source](https://docs.pipewire.org/props_8h_source.html) · `?` no portal/PipeWire-native PTZ surface

## capture modes
- format negotiation only — modes = whatever the backend lists in `SPA_PARAM_EnumFormat`; no bracketing/ZSL/burst/RAW-pipeline concepts at this layer (deferred to V4L2/libcamera backend; backend itself is thin here) — source: [PipeWire tutorial 5](https://docs.pipewire.org/page_tutorial5.html)
- `SPA_FORMAT_mediaType` — `video` — source: [spa/param/format.h](https://docs.pipewire.org/param_2format_8h_source.html)
- `SPA_FORMAT_mediaSubtype` — selects raw vs encoded — source: [spa/param/format.h](https://docs.pipewire.org/param_2format_8h_source.html)
- `SPA_MEDIA_SUBTYPE_raw` — uncompressed video — source: [spa/param/format.h](https://docs.pipewire.org/param_2format_8h_source.html)
- `SPA_MEDIA_SUBTYPE_mjpg` — MJPEG (common UVC webcam mode; `video_format = SPA_VIDEO_FORMAT_ENCODED`) — source: [v4l2-utils.c](https://github.com/PipeWire/pipewire/blob/master/spa/plugins/v4l2/v4l2-utils.c)
- `SPA_MEDIA_SUBTYPE_h264` — H.264 from cameras that encode on-sensor — source: [v4l2-utils.c](https://github.com/PipeWire/pipewire/blob/master/spa/plugins/v4l2/v4l2-utils.c)
- `SPA_MEDIA_SUBTYPE_jpeg` (image range `START_Image`=0x30000), `_bayer`, `_dv`, `_mpegts`, `_h263`, `_mpeg1/2/4`, `_xvid`, `_vc1`, `_vp8`, `_vp9`, `_h265` — full subtype catalog (camera-relevant: raw/mjpg/h264/bayer) — source: [spa/param/format.h](https://docs.pipewire.org/param_2format_8h_source.html)
- `SPA_FORMAT_VIDEO_format` — the `SPA_VIDEO_FORMAT_*` enum value (for raw) — source: [SPA param](https://docs.pipewire.org/group__spa__param.html)
- `SPA_FORMAT_VIDEO_size` — `spa_rectangle` width×height — source: [SPA param](https://docs.pipewire.org/group__spa__param.html)
- `SPA_FORMAT_VIDEO_framerate` — `spa_fraction` num/den — source: [SPA param](https://docs.pipewire.org/group__spa__param.html)
- `SPA_FORMAT_VIDEO_maxFramerate` — max framerate fraction — source: [SPA param](https://docs.pipewire.org/group__spa__param.html)
- no bracketing / ZSL / still-during-video / RAW pipeline — **none at this layer**; not modeled by PipeWire or the portal — source: (none; absent from PipeWire format model)

## depth / 3D / calibration
- **none at portal/PipeWire layer** — no depth-map, stereo, intrinsics, or distortion concept. IR/mono is *only* a pixel format: if the backend lists a grey format (`SPA_VIDEO_FORMAT_GRAY8`, `SPA_VIDEO_FORMAT_GRAY16_LE/BE`) it appears as an ordinary `SPA_PARAM_EnumFormat` entry; no semantic tagging that it is IR/depth — source: [spa/param/video/raw.h](https://github.com/PipeWire/pipewire/blob/master/spa/include/spa/param/video/raw.h) · `?` calibration entirely backend/out-of-band

## live effects
- **none** — PipeWire is transport; no auto-framing / portrait / eye-contact. Any such effect is a separate processing node the app or another process inserts, not a camera capability — source: (none; out of PipeWire camera model)

## frame memory
- `pw_buffer` — wraps a `spa_buffer`; obtained from the stream queue — source: [PipeWire streams](https://docs.pipewire.org/page_streams.html)
- `pw_stream_dequeue_buffer(stream)` — get a filled buffer in `process` (capture); `NULL` when none ready — source: [PipeWire tutorial 5](https://docs.pipewire.org/page_tutorial5.html)
- `pw_stream_queue_buffer(stream, buffer)` — recycle the buffer back to the pool (back-pressure: hold = stall producer) — source: [PipeWire tutorial 5](https://docs.pipewire.org/page_tutorial5.html)
- `pw_stream_events.add_buffer / remove_buffer(data, buffer)` — pool grows/shrinks; the moment to import/release dmabuf fds — source: [PipeWire streams](https://docs.pipewire.org/page_streams.html)
- `spa_buffer.datas[i].data` — CPU pointer (for `SPA_DATA_MemPtr`/mmapped) — source: [PipeWire tutorial 5](https://docs.pipewire.org/page_tutorial5.html)
- `spa_buffer.datas[i].chunk` (`spa_chunk`) — `offset`, `size`, `stride`, `flags` of the valid region — source: [SPA buffers](https://docs.pipewire.org/group__spa__buffer.html)
- `SPA_DATA_MemPtr` — data is a plain pointer (the `data` field set) — source: [SPA buffers](https://docs.pipewire.org/group__spa__buffer.html)
- `SPA_DATA_MemFd` — memfd; mmap to reach memory — source: [SPA buffers](https://docs.pipewire.org/group__spa__buffer.html)
- `SPA_DATA_DmaBuf` — fd to dmabuf; **zero-copy GPU import path** (import via EGL/gbm/Vulkan/VA-API, do NOT mmap — tiling/compression) — source: [DMA-BUF sharing](https://docs.pipewire.org/page_dma_buf.html)
- `SPA_DATA_MemId` — memory referenced by id, obtained out-of-band — source: [SPA buffers](https://docs.pipewire.org/group__spa__buffer.html)
- `SPA_PARAM_Buffers` — negotiate the pool: count, size, layout — source: [SPA param](https://docs.pipewire.org/group__spa__param.html)
- `SPA_PARAM_BUFFERS_buffers` — number of buffers in the pool — source: [SPA param](https://docs.pipewire.org/group__spa__param.html)
- `SPA_PARAM_BUFFERS_blocks` — `spa_data` blocks per buffer (= plane count for the negotiated format/modifier) — source: [DMA-BUF sharing](https://docs.pipewire.org/page_dma_buf.html)
- `SPA_PARAM_BUFFERS_size` — bytes per buffer block — source: [SPA param](https://docs.pipewire.org/group__spa__param.html)
- `SPA_PARAM_BUFFERS_stride` — row stride — source: [SPA param](https://docs.pipewire.org/group__spa__param.html)
- `SPA_PARAM_BUFFERS_align` — required alignment — source: [SPA param](https://docs.pipewire.org/group__spa__param.html)
- `SPA_PARAM_BUFFERS_dataType` — bitmask of acceptable data types: `1<<SPA_DATA_DmaBuf` / `1<<SPA_DATA_MemFd` / `1<<SPA_DATA_MemPtr` — the dmabuf-vs-shm switch — source: [DMA-BUF sharing](https://docs.pipewire.org/page_dma_buf.html)
- `SPA_FORMAT_VIDEO_modifier` — DRM format modifier; present ⇒ dmabuf negotiation. Carry it in a **second** `SPA_PARAM_EnumFormat` flagged `SPA_POD_PROP_FLAG_MANDATORY | SPA_POD_PROP_FLAG_DONT_FIXATE` (dmabuf), with a modifier-less EnumFormat as shm fallback; **producer fixates** the format-modifier pair — source: [DMA-BUF sharing](https://docs.pipewire.org/page_dma_buf.html)
- `SPA_VIDEO_FORMAT_*` — `RGB`, `RGBA`, `BGRA`, `YUY2`, `UYVY`, `I420`, `NV12`, `GRAY8`, `GRAY16_LE/BE`, … and `SPA_VIDEO_FORMAT_ENCODED` for mjpg/h264 — source: [spa/param/video/raw.h](https://github.com/PipeWire/pipewire/blob/master/spa/include/spa/param/video/raw.h)
- `spa_video_info_raw{ format, modifier, size (spa_rectangle), framerate (spa_fraction), max_framerate, flags, color_range, color_matrix, transfer_function, color_primaries }` — parsed via `spa_format_video_raw_parse` — source: [spa_video_info_raw](https://docs.pipewire.org/structspa__video__info__raw.html)
- v4l2 dmabuf special case — v4l2 exports planar dmabufs in main memory **without modifiers**; both ends set `1<<SPA_DATA_DmaBuf` but announce no modifier — source: [DMA-BUF sharing](https://docs.pipewire.org/page_dma_buf.html)
- back-pressure — drops/stalls are implicit in queue/dequeue timing; **no explicit frame-drop-reason metadata** at this layer — source: [PipeWire streams](https://docs.pipewire.org/page_streams.html) · `?` drop reasons not surfaced

## timing
- `SPA_META_Header` (`spa_meta_header`) — per-buffer timing/sequence metadata — source: [SPA buffers](https://docs.pipewire.org/group__spa__buffer.html)
- `spa_meta_header.pts (int64)` — presentation timestamp, nanoseconds — source: [spa_meta_header](https://pipewire.pages.freedesktop.org/pipewire/structspa__meta__header.html)
- `spa_meta_header.dts_offset (int64)` — decode timestamp as a delta from pts — source: [spa_meta_header](https://pipewire.pages.freedesktop.org/pipewire/structspa__meta__header.html)
- `spa_meta_header.seq (uint64)` — sequence number, increments at a media-specific rate (frame counter) — source: [spa_meta_header](https://pipewire.pages.freedesktop.org/pipewire/structspa__meta__header.html)
- `spa_meta_header.offset (uint32)` — offset in the current cycle — source: [spa_meta_header](https://pipewire.pages.freedesktop.org/pipewire/structspa__meta__header.html)
- `spa_meta_header.flags (uint32)` — header flags (e.g. corrupted) — source: [spa_meta_header](https://pipewire.pages.freedesktop.org/pipewire/structspa__meta__header.html)
- `pw_stream_get_time_n(stream, time, size)` / `pw_stream_get_nsec(stream)` — query stream clock for sync — source: [PipeWire streams](https://docs.pipewire.org/page_streams.html)
- clock domain — pts is on the PipeWire graph clock; **no SMPTE timecode, no IMU/motion correlation** at this layer (backend/none) — source: [spa_meta_header](https://pipewire.pages.freedesktop.org/pipewire/structspa__meta__header.html) · `?` clock-domain↔wall-clock mapping is graph-dependent

## metadata
- `SPA_META_Header` — header timing/seq (see timing) — source: [SPA buffers](https://docs.pipewire.org/group__spa__buffer.html)
- `SPA_META_VideoCrop` (`spa_meta_region`) — active crop/region rectangle on the buffer — source: [SPA buffers](https://docs.pipewire.org/group__spa__buffer.html)
- `SPA_META_VideoDamage` — array of `spa_meta_region` damage rects (invalid entry terminates) — source: [SPA buffers](https://docs.pipewire.org/group__spa__buffer.html)
- `SPA_META_VideoTransform` (`spa_meta_videotransform`, type=8) — **orientation/rotation+flip** applied to the buffer; value ∈ `spa_meta_videotransform_value` — source: [spa_meta_videotransform](https://pipewire.pages.freedesktop.org/pipewire/structspa__meta__videotransform.html)
- `SPA_META_TRANSFORMATION_None/_90/_180/_270/_Flipped/_Flipped90/_Flipped180/_Flipped270` — the 8 transform values (mirrors wl_output transform; libcamera maps its `Transform` into these) — source: [PipeWire issue #716](https://gitlab.freedesktop.org/pipewire/pipewire/-/issues/716)
- `SPA_META_Cursor` (`spa_meta_cursor`) — cursor info (screencast-oriented; not camera) — source: [SPA buffers](https://docs.pipewire.org/group__spa__buffer.html)
- `SPA_META_Bitmap` — bitmap info (cursor bitmap) — source: [SPA buffers](https://docs.pipewire.org/group__spa__buffer.html)
- `SPA_META_Control` — a timed set of events associated with the buffer — source: [SPA buffers](https://docs.pipewire.org/group__spa__buffer.html)
- `SPA_META_Busy` — don't write to buffer while count > 0 — source: [SPA buffers](https://docs.pipewire.org/group__spa__buffer.html)
- `SPA_PARAM_Meta` / `SPA_PARAM_META_type` / `SPA_PARAM_META_size` — request a meta block on buffers (e.g. ask for `SPA_META_Header`) during negotiation — source: [SPA param](https://docs.pipewire.org/group__spa__param.html)
- no face/scene/3A-result/histogram metadata — **none**; PipeWire carries none of camera's higher-level metadata (backend/none) — source: (none; absent from SPA meta set)

## egress (virtual camera publish)
- publish a node — connect a `pw_stream` with `PW_DIRECTION_OUTPUT` carrying `media.class=Video/Source` + `media.role=Camera`; it appears in the graph as a camera other PipeWire-aware apps can capture — source: [PipeWire streams](https://docs.pipewire.org/page_streams.html) · [PipeWire is the new v4l2loopback](https://www.ideasonboard.com/news/pipewire-is-the-new-v4l2loopback/)
- `PW_DIRECTION_OUTPUT` — a stream that *produces* data; used to implement a Source/virtual camera — source: [PipeWire streams](https://docs.pipewire.org/page_streams.html)
- reference impl — GStreamer `pipewiresink mode=provide stream-properties="properties,media.class=Video/Source,media.role=Camera"` is the canonical one-liner; OBS ships a native PipeWire virtual camera using exactly this node shape — source: [PipeWire is the new v4l2loopback](https://www.ideasonboard.com/news/pipewire-is-the-new-v4l2loopback/) · [OBS PipeWire Virtual Camera PR #5377](https://github.com/obsproject/obs-studio/pull/5377)
- streaming-state semantics — the publish node sits in `PW_STREAM_STATE_PAUSED` (ready, no buffers) until a consumer connects, then `PW_STREAM_STATE_STREAMING` — gives consumer attach/detach feedback (MEL-ENGINE-III: produce buffers only on demand) — source: [OBS PipeWire Virtual Camera PR #5377](https://github.com/obsproject/obs-studio/pull/5377)
- **reality / in flux** — a pure PipeWire node is seen ONLY by apps that consume PipeWire camera sources (Firefox `media.webrtc.camera.allow-pipewire`; Chrome in development; portal-aware Flatpaks; qpwgraph/Helvum). **V4L2-only apps (Discord, Slack, Zoom-native) do NOT see it** — source: [PipeWire virtual camera discussion](https://github.com/obsproject/obs-studio/discussions/7998)
- relationship to v4l2loopback — for legacy V4L2-only consumers you still bridge to a `v4l2loopback` device, or use PipeWire's `pw-v4l2` `LD_PRELOAD` shim to expose PipeWire sources to V4L2 clients. PipeWire is positioned to *replace* v4l2loopback in userspace but adoption on the consumer side is incomplete — source: [PipeWire is the new v4l2loopback](https://www.ideasonboard.com/news/pipewire-is-the-new-v4l2loopback/)
- consumer-side controls on a published cam — `?` no portal/PipeWire-native mechanism to expose app-defined controls on a virtual camera node (would be ad-hoc `SPA_PARAM_PropInfo` custom props) — source: `?` not standardized

## OS integration
- **the portal IS the consent path** — flow: `AccessCamera(handle_token)` → user permission prompt (via the portal backend UI) → `Request::Response(0)` grant → `OpenPipeWireRemote() → fd` → `pw_context_connect_fd(fd)` → enumerate/stream. No grant ⇒ no fd ⇒ no nodes — source: [portal Camera docs](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Camera.html)
- why it exists vs raw V4L2 — sandboxed apps (**Flatpak/Snap**) and Wayland-session apps have **no direct `/dev/videoN` access**; the portal is the brokered, user-consented gate, and PipeWire is the transport that crosses the sandbox boundary via the returned fd — source: [PipeWire camera handling is now happening](https://blogs.gnome.org/uraeus/2024/03/15/pipewire-camera-handling-is-now-happening/)
- Flatpak/Snap integration — the portal D-Bus service is reachable from inside the sandbox; the camera permission is recorded per-app and **persisted** by the permission store (subsequent `AccessCamera` calls may resolve without re-prompting) — source: [portal Camera docs](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Camera.html) · `?` exact persistence policy is backend-implementation-defined
- session/handle lifecycle — `AccessCamera` returns a `Request` object (cancel via `Request.Close`); the granted permission outlives the request. The PipeWire fd lifetime is the app's `pw_core` connection (Camera portal has **no Session object**, unlike ScreenCast) — source: [portal Camera XML](https://github.com/flatpak/xdg-desktop-portal/blob/main/data/org.freedesktop.portal.Camera.xml)
- hot-plug — surfaced through `pw_registry` `global`/`global_remove` on the portal fd (see devices) — source: [PipeWire registry](https://docs.pipewire.org/group__pw__registry.html)
- **no orientation / privacy-LED / thermal of its own** — orientation comes only as `SPA_META_VideoTransform` per-buffer (if the backend sets it); no in-use LED or thermal signal at this layer (backend/none) — source: [spa_meta_videotransform](https://pipewire.pages.freedesktop.org/pipewire/structspa__meta__videotransform.html)
- arbitration — multiple apps capturing one camera is mediated by **WirePlumber/session-manager + the backend's single-open limits**, not by the portal; no explicit preemption API surfaced — source: [PipeWire is the new v4l2loopback](https://www.ideasonboard.com/news/pipewire-is-the-new-v4l2loopback/) · `?` arbitration semantics backend/session-manager-dependent

## obscure corners
- `SPA_PARAM_IO` (`SPA_PARAM_META_IO`) — shared IO area (e.g. `SPA_IO_Position`, `SPA_IO_RateMatch`) for driving/clocking a stream — rarely touched for camera capture — source: [SPA param](https://docs.pipewire.org/group__spa__param.html)
- `SPA_PARAM_Latency` / `SPA_PARAM_ProcessLatency` — report stream latency; mostly an audio concern, defined for all streams — source: [SPA param](https://docs.pipewire.org/group__spa__param.html)
- `SPA_PARAM_Tag` (`SPA_TYPE_OBJECT_ParamTag`) — opaque tag metadata propagated through the graph — source: [SPA param](https://docs.pipewire.org/group__spa__param.html)
- `SPA_PARAM_EnumProfile/_Profile`, `SPA_PARAM_EnumPortConfig/_PortConfig`, `SPA_PARAM_EnumRoute/_Route` — Device-level profile/route params (more relevant to ALSA/audio devices; a camera Device may expose minimal versions) — source: [SPA param](https://docs.pipewire.org/group__spa__param.html)
- `SPA_DATA_SyncObj` — fd to a syncobj; pairs with `spa_meta_sync_timeline` for explicit GPU sync fences (emerging; explicit-sync dmabuf path) — source: [SPA buffers](https://docs.pipewire.org/group__spa__buffer.html)
- `SPA_CHUNK_FLAG_CORRUPTED` / `SPA_CHUNK_FLAG_EMPTY` — per-chunk flags marking corrupted or neutral (black) frame data — the closest thing to a drop/corruption signal — source: [SPA buffers](https://docs.pipewire.org/group__spa__buffer.html)
- `pw_stream_trigger_process` / `pw_stream_is_driving` — drive the graph cycle manually (pull model); unusual for a camera consumer — source: [PipeWire streams](https://docs.pipewire.org/page_streams.html)
- `pw-v4l2` LD_PRELOAD shim — exposes PipeWire sources to legacy V4L2 client apps without v4l2loopback — source: [PipeWire is the new v4l2loopback](https://www.ideasonboard.com/news/pipewire-is-the-new-v4l2loopback/)
- libportal `xdp_portal_access_camera` / `xdp_portal_open_pipewire_remote_for_camera` — C convenience wrappers over the raw D-Bus Camera methods — source: [libportal](https://libportal.org/) · `?` exact symbol names unverified against current libportal headers
