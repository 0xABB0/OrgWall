# camera — specification

OS camera capture: enumerate capture devices (with hotplug), authorize, open and
configure a device, stream frames, and deliver each frame as a zero-copy
`Mel_Image` to consumers (barcode decode chief among them). This is the contract
and the map of built versus sequenced. The module is, structurally, the display
module (enumeration + hotplug-via-`Mel_Event`) crossed with the vibration module
(provider-plugin backends + device open/close), built on the future/event
substrate. Capture is produced here; pixels are owned by `image` (MEL-ENGINE-IX).

## Boundary types

```
typedef struct { Mel_SlotMap_Handle h; } Mel_Camera;          // device handle
#define MEL_CAMERA_NULL ((Mel_Camera){ 0 })

typedef struct mel_camera_facing mel_camera_facing;           // open descriptor, const singletons
extern const mel_camera_facing mel_camera_front;
extern const mel_camera_facing mel_camera_back;
extern const mel_camera_facing mel_camera_external;
extern const mel_camera_facing mel_camera_unknown;

typedef struct {
    const mel_image_format* format;   // pixel format the device delivers (e.g. nv12)
    i32 width, height;
    f32 fps_min, fps_max;
} Mel_Camera_Mode;

typedef struct {
    str8                    name;
    const mel_camera_facing* facing;
    Mel_Array(Mel_Camera_Mode) modes;   // dynamic, no fixed cap (MEL-CODE-002)
    const Mel_Alloc*        alloc;
} Mel_Camera_Descriptor;

typedef struct {
    const mel_image_format* format;
    i32 width, height;
    f32 fps;
} Mel_Camera_Config;

typedef struct {
    Mel_Image        image;       // BORROWED zero-copy wrap of the OS buffer; valid ONLY during the frame callback
    u64              timestamp_ns;
    u64              sequence;
    Mel_Image_Orient orient;      // sensor->upright transform the consumer applies if it wants upright pixels
} Mel_Camera_Frame;
```

`mel_camera_facing` is open data (the `mel_image_format` / galois pattern), not an
enum: facing is a classification that grows (continuity, virtual, …), not a
protocol (MEL-CODE-001). `Mel_Camera_Mode` lists are dynamic arrays.

## Frame lifetime — the load-bearing contract

`Mel_Camera_Frame.image` borrows the OS capture buffer and is valid **only for
the duration of the push callback**. A consumer that needs the frame afterward
must `mel_image_convert`/copy it into its own `Mel_Image` inside the callback.
Decode is the happy path: take `mel_image_gray_borrow(&frame.image)` and decode
synchronously in-callback — zero copy, zero allocation (MEL-ENGINE-III/VI). Pull
subscription is therefore unsafe for frames (the borrow dangles); frames are
delivered push-only. This is asserted and documented, never silently mishandled
(MEL-ENGINE-VIII).

## Lifecycle, enumeration, hotplug (display-shaped)

```
void mel_camera_init(const Mel_Alloc* alloc, Mel_Reactor* reactor);
void mel_camera_shutdown(void);

u32  mel_camera_refresh(void);
u32  mel_camera_count(void);
u32  mel_camera_list(Mel_Camera* out, u32 cap);

Mel_Camera_Describe_Result mel_camera_describe(Mel_Camera c, const Mel_Alloc* a);
bool mel_camera_alive(Mel_Camera c);
bool mel_camera_equal(Mel_Camera a, Mel_Camera b);

typedef struct { Mel_Camera camera; const mel_camera_facing* facing; bool added; bool removed; bool changed; } Mel_Camera_Event;
typedef struct { Mel_SlotMap_Handle handle; } Mel_Camera_Hotplug_Sub;
typedef struct { Mel_SlotMap_Handle handle; } Mel_Camera_Frame_Sub;
typedef void (*Mel_Camera_Event_Callback)(const Mel_Camera_Event* ev, void* user);

Mel_Camera_Hotplug_Sub mel_camera_subscribe(Mel_Executor* exec, Mel_Camera_Event_Callback cb, void* user);
void                   mel_camera_unsubscribe(Mel_Camera_Hotplug_Sub sub);
```

Hotplug and frame subscriptions are distinct types so the compiler rejects
feeding one to the other's unsubscribe.

Hotplug rides a `Mel_Event` internally (as display does). The add/remove/change
distinction is carried as booleans on the event payload, not an enum.

## Authorization (future-shaped, never silent)

```
typedef struct mel_camera_auth mel_camera_auth;   // open: granted / denied / not_determined / restricted
extern const mel_camera_auth mel_camera_auth_granted;
extern const mel_camera_auth mel_camera_auth_denied;
extern const mel_camera_auth mel_camera_auth_not_determined;
extern const mel_camera_auth mel_camera_auth_restricted;

const mel_camera_auth* mel_camera_authorization(void);              // sync current status
Mel_Future*            mel_camera_authorize(const Mel_Alloc* a);    // request -> future<auth>
```

Permission is a first-class async step (macOS/iOS/Android prompt), surfaced as a
future resolving to an auth status — never a silent capture failure
(MEL-ENGINE-VIII). The future's value is a `const mel_camera_auth*` static
singleton (read via `mel_camera_future_auth`); never freed by the caller.
Producers resolve inline; route the continuation off-thread with
`mel_future_then`. One destructor, `mel_camera_future_free`, frees every camera
future.

## Open / stream (future + event substrate)

```
Mel_Future* mel_camera_open(Mel_Camera c, Mel_Camera_Config cfg, const Mel_Alloc* a);  // -> future settles when configured
Mel_Future* mel_camera_start(Mel_Camera c, const Mel_Alloc* a);                        // begin streaming
Mel_Future* mel_camera_stop(Mel_Camera c, const Mel_Alloc* a);
void        mel_camera_close(Mel_Camera c);

Mel_Event*           mel_camera_frames(Mel_Camera c);   // Mel_Camera_Frame stream; subscribe PUSH only (see frame lifetime)
Mel_Camera_Frame_Sub mel_camera_frame_subscribe(Mel_Camera c, Mel_Camera_Frame_Callback cb, void* user);  // inline, capture-thread delivery
```

`mel_camera_frame_subscribe` takes no executor: the borrow lives only for the
callback, so frames are always delivered inline on the capture thread; a
deferring executor would dangle the wrapped OS planes.

The frame event is created with `mel_event_policy_latest` and ring depth 1 —
under backpressure the freshest frame wins and stale frames drop, which is the
correct camera semantics and bounds memory (MEL-ENGINE-III); a deeper ring would
only retain already-expired borrows. Stream teardown adopts the open/start
futures into a `Mel_Future_Scope` so close cancels cleanly.

## Provider plugin (vibration-shaped)

```
typedef struct {
    void (*on_frame)(void* token, const Mel_Camera_Frame* frame);   // backend -> core, on the capture callback
    void (*on_event)(void* token, Mel_Camera_Event ev);             // hotplug
    void* token;
} Mel_Camera_Sink;

typedef struct {
    const char* name;
    void*       user;
    u32  (*enumerate)(void* user, Mel_Camera_Raw* out, u32 cap);
    bool (*open)(void* user, u64 stable_id, Mel_Camera_Config cfg, Mel_Camera_Sink sink);
    void (*close)(void* user, u64 stable_id);
    Mel_Camera_Status (*start)(void* user, u64 stable_id);
    Mel_Camera_Status (*stop)(void* user, u64 stable_id);
    const mel_camera_auth* (*authorization)(void* user);
    void (*authorize)(void* user, Mel_Camera_Sink sink);            // async; resolves via a sink/notify
    void* (*native)(void* user, u64 stable_id);
} Mel_Camera_Provider_Desc;

Mel_Camera_Provider mel_camera_provider_register(const Mel_Camera_Provider_Desc* desc);
void                mel_camera_provider_unregister(Mel_Camera_Provider p);
void                mel_camera__register_host_providers(void);
```

Backends register a provider; the core owns handles, the frame `Mel_Event`, and
the reactor wiring. A backend hands each captured buffer to the core as a
borrowed `Mel_Image` (via `mel_image_wrap` over the OS planes — NV12/420f Y plane
is luminance, zero copy) plus timestamp and sensor orientation.

## Platforms

macOS/iOS AVFoundation first (`apple/src/*.m`, `-framework AVFoundation
-framework CoreMedia -framework CoreVideo -framework Foundation`). Linux V4L2,
Android Camera2/NDK, Win32 Media Foundation, wasm getUserMedia are sequenced;
unsupported platforms get a `host_none` stub (`mel_camera__register_host_providers`
no-op), so the module always builds (MEL-ENGINE-I/VII).

## Dependencies

`core`, `allocator`, `collection`, `image`, `event`, `future`, `reactor`,
`executor`, `string`, `log`. No `gpu`, no `paint`.

## Contract

Validate before acting; loud failure on misuse (closed device, unsupported
config) — never a silent no-frames. The AVFoundation backend honours
`Mel_Camera_Config` (format/width/height/fps select an `AVCaptureDeviceFormat`
under `lockForConfiguration`); an unsatisfiable config fails the open future
rather than silently yielding the default preset. Owned state frees on shutdown;
frames are borrowed and never freed by the consumer. The real AVFoundation
backend links and compiles on macOS; headless capture cannot be run-verified, so
module logic is proven against a mock provider emitting synthetic NV12 frames.

## Status

Sequenced: enumeration/hotplug, authorization, open/start/stop, frame event, the
provider substrate, the macOS AVFoundation backend, and a mock-provider test
harness. Other platform backends are designed-for, not yet built (deferral is not
refusal, MEL-ENGINE-I).
