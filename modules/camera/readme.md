# camera

OS camera capture: enumerate capture devices (with hotplug), authorize, open and
configure a device, stream frames, and deliver each frame as a zero-copy
`Mel_Image` to consumers (barcode decode chief among them).

Structurally this is `display` (enumeration + hotplug via `Mel_Event`) crossed
with `vibration` (provider-plugin backends + device open/close), built on the
future/event substrate. Pixels are owned by `image` (MEL-ENGINE-IX); capture is
produced here.

## Frame lifetime — the load-bearing contract

`Mel_Camera_Frame.image` borrows the OS capture buffer and is valid **only for
the duration of the push callback**. Decode in-callback off
`mel_image_gray_borrow(&frame.image)` — zero copy, zero allocation. A consumer
that needs the frame afterward must `mel_image_convert`/copy it inside the
callback. Frames are push-only; `mel_camera_frame_pull` fails loudly (the borrow
would dangle).

`mel_camera_frame_subscribe` always delivers on the capture thread, inline
(`mel_executor_inline`) — it takes no executor. The borrow's lifetime is the
callback alone, so a deferring executor would dangle the wrapped OS planes; the
contract is enforced structurally, never silently. `Mel_Camera_Frame_Sub` and
`Mel_Camera_Hotplug_Sub` are distinct types so a frame sub cannot be fed to
`mel_camera_unsubscribe` (nor a hotplug sub to `mel_camera_frame_unsubscribe`).

The per-device frame event uses `mel_event_policy_latest` with ring depth 1:
under backpressure the freshest frame wins and stale frames drop — correct camera
semantics, bounded memory (depth >1 would only retain already-dead borrows).

## Open descriptors (not enums)

`mel_camera_facing` and `mel_camera_auth` are open data carrying behaviour, with
const singletons (`mel_camera_front`, `mel_camera_auth_granted`, …) — facing and
auth are classifications that grow, not protocols (MEL-CODE-001).

## Futures

Every async producer (`mel_camera_authorize`, `mel_camera_open`,
`mel_camera_start`, `mel_camera_stop`) returns `Mel_Future*` and resolves it
inline on the caller's thread; place a continuation with `mel_future_then(f,
cont, exec)` to run off-thread. There is one destructor, `mel_camera_future_free`,
for every future the module returns. The authorize future resolves with a
`const mel_camera_auth*` value (read via `mel_camera_future_auth`); that pointer
is a static singleton and must **never** be freed by the caller —
`mel_camera_future_free` releases only the future's own bookkeeping.

## Provider plugin

Backends register a `Mel_Camera_Provider_Desc`; the core owns handles, the frame
`Mel_Event`, and the reactor wiring. A backend hands each captured buffer to the
core sink as a borrowed `Mel_Image` (via `mel_image_wrap` over the OS planes —
NV12 Y plane is luminance, zero copy) plus timestamp and sensor orientation.

The AVFoundation backend honours `Mel_Camera_Config`: it selects an
`AVCaptureDeviceFormat` matching `cfg.format` (mapped to the CoreVideo fourcc),
`cfg.width`/`cfg.height` and applies `cfg.fps` via the active frame durations,
returning `false` (open future fails `UNSUPPORTED`) when no device format
satisfies the request — never a silent fallback. The pixel format is resolved
once at open time and cached on the session, not re-dispatched per frame.
`Mel_Camera_Frame.orient` carries `flip_x = true` for front-facing devices
(mirrored sensor); `quarter_turns` is 0 on desktop AVFoundation.

## Platforms

macOS/iOS AVFoundation (`src/apple/camera_avf.m`). Linux V4L2, Android
Camera2/NDK, Win32 Media Foundation, wasm getUserMedia are sequenced;
unsupported platforms get a `host_none` stub so the module always builds
(MEL-ENGINE-I/VII). The AVFoundation backend links and compiles on macOS;
headless capture cannot be run-verified, so module logic is proven against a
mock provider emitting synthetic NV12 frames (`test/camera_test.c`).

## Dependencies

`core`, `allocator`, `collection`, `image`, `event`, `future`, `reactor`,
`executor`, `string`, `log`. No `gpu`, no `paint`.

## Test

`./nob test camera-core` — mock provider, deterministic, no hardware.
