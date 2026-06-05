# camera — design

OS camera capture: enumerate capture devices across providers, authorize, open
and configure a device, stream frames to consumers as a zero-copy borrowed
`Mel_Image`. The decode path (barcode chief among them) is the load-bearing
consumer.

## Invariants

1. **The module allocates nothing.** No call to any `Mel_Alloc` in the module's
   own code. All storage is caller-owned (passed in) or static (const
   singletons, backend-static provider nodes). The only memory in play is the
   OS's own — `AVCaptureSession`, Camera2 objects, V4L2 `mmap`'d DMA buffers —
   which the OS allocates because the OS must, and which *are* the zero-copy
   frame source.
2. **No wasted cycles.** Steady-state frame delivery is one indirect call per
   subscriber over a borrowed buffer: no copy, no allocation, no per-frame
   bookkeeping beyond a sequence increment.
3. **Hug the OS.** Each backend delivers frames by the OS's own mechanism on the
   OS's own thread; the core imposes no thread of its own and spawns none.
4. **Open data, not enums** (MEL-CODE-001). Facing, authorization, and result
   are classifications that grow — modelled as opaque structs with const
   singletons, never enums.
5. **No fixed-cap collections** (MEL-CODE-002). Variable-length lists (devices,
   modes, subscribers) are *visited or intrusively linked*, never returned as a
   sized array and never stored behind a `[MAX]`.
6. **Loud failure** (MEL-ENGINE-VIII). Misuse (open on a dead camera,
   unsatisfiable config, pull on a borrowed frame) fails immediately and
   audibly, never as silent no-frames.

## Identity & classifications

A camera *is* its OS identity. No slotmap, no generational slot, no indirection.

```
typedef const struct Mel_Camera_Provider_Node* Mel_Camera_Provider;  // registry node ptr (stable, static)

typedef struct {
    Mel_Camera_Provider provider;     // which provider owns it
    u64                 stable_id;     // provider-stable OS id
} Mel_Camera;

#define MEL_CAMERA_NULL ((Mel_Camera){ 0 })

bool mel_camera_equal(Mel_Camera a, Mel_Camera b);   // provider == && stable_id ==
bool mel_camera_alive(Mel_Camera c);                 // provider registered && device still enumerable
```

Liveness is "still in the live set", computed against the provider — not a slot
generation.

```
typedef struct mel_camera_facing mel_camera_facing;     // front/back/external/unknown/virtual/continuity/…
extern const mel_camera_facing mel_camera_front, mel_camera_back, mel_camera_external, mel_camera_unknown;
const char* mel_camera_facing_name(const mel_camera_facing*);

typedef struct mel_camera_auth mel_camera_auth;         // granted/denied/not_determined/restricted
extern const mel_camera_auth mel_camera_auth_granted, mel_camera_auth_denied,
                             mel_camera_auth_not_determined, mel_camera_auth_restricted;
const char* mel_camera_auth_name(const mel_camera_auth*);
bool        mel_camera_auth_is_granted(const mel_camera_auth*);

typedef struct mel_camera_result mel_camera_result;     // ok/denied/no_device/busy/unsupported/lost/cancelled
extern const mel_camera_result mel_camera_ok, mel_camera_denied, mel_camera_no_device,
                              mel_camera_busy, mel_camera_unsupported, mel_camera_lost, mel_camera_cancelled;
const char* mel_camera_result_name(const mel_camera_result*);
bool        mel_camera_result_ok(const mel_camera_result*);
```

One classification idiom across the module: facing, auth, result are all open
data with const singletons. `mel_camera_result*` is what start/stop return and
what the open future carries — no parallel bitset, no `container_of` recovery.

## Provider registry (intrusive, static nodes)

The registry is intentional: OS providers, plus custom drivers, virtual cameras,
and adapters-over-video. Providers are an intrusive list of backend-owned static
nodes; registration links a node, it allocates nothing.

```
typedef struct Mel_Camera_Provider_Node Mel_Camera_Provider_Node;
struct Mel_Camera_Provider_Node {
    Mel_Camera_Provider_Desc  desc;     // function table (below)
    Mel_Camera_Provider_Node* next;     // core-managed link; zero-init by caller
};

void mel_camera_provider_register(Mel_Camera_Provider_Node* node);    // node->desc filled by caller
void mel_camera_provider_unregister(Mel_Camera_Provider_Node* node);

void mel_camera__register_host_providers(void);   // each platform registers its one static node; stub elsewhere
```

## Enumeration (visitor, across providers)

No count, no index, no cap buffer. The set is a union across providers that
register/unregister independently with hotplug underneath — an index would tear.
The visitor walks providers and each streams its own devices.

```
typedef struct {
    str8                     name;      // BORROWED; valid only for this callback
    const mel_camera_facing* facing;
} Mel_Camera_Descriptor;

typedef bool (*Mel_Camera_Visitor)(Mel_Camera cam, const Mel_Camera_Descriptor* desc, void* user);  // false → stop

void mel_camera_each(Mel_Camera_Visitor visit, void* user);                  // all devices, all providers
bool mel_camera_describe(Mel_Camera cam, Mel_Camera_Visitor visit, void* user);  // one device; false if gone
```

Modes are themselves visited, never returned as an array (no allocation, no
`[MAX_MODES]`):

```
typedef struct {
    const mel_image_format* format;     // pixel format the device delivers (e.g. nv12)
    i32 width, height;
    f32 fps_min, fps_max;
} Mel_Camera_Mode;

typedef bool (*Mel_Camera_Mode_Visitor)(const Mel_Camera_Mode* mode, void* user);  // false → stop
void mel_camera_modes_each(Mel_Camera cam, Mel_Camera_Mode_Visitor visit, void* user);
```

A backend's `enumerate` opens each node, fills a stack `Mel_Camera_Descriptor`,
calls the visitor, and releases it — no list ever materialises. `modes_each`
streams `VIDIOC_ENUM_*` / `AVCaptureDeviceFormat` results one at a time through
the visitor on the stack.

## Hotplug (intrusive subscriber, inline)

```
typedef struct {
    Mel_Camera               camera;
    const mel_camera_facing* facing;
    bool added, removed, changed;
} Mel_Camera_Hotplug;

typedef void (*Mel_Camera_Hotplug_Fn)(const Mel_Camera_Hotplug* ev, void* user);

typedef struct Mel_Camera_Hotplug_Sub Mel_Camera_Hotplug_Sub;
struct Mel_Camera_Hotplug_Sub {                 // caller-owned node
    Mel_Camera_Hotplug_Fn   cb;
    void*                   user;
    Mel_Camera_Hotplug_Sub* next;               // core-managed link
};

void mel_camera_hotplug_subscribe(Mel_Camera_Hotplug_Sub* sub);    // caller fills cb/user
void mel_camera_hotplug_unsubscribe(Mel_Camera_Hotplug_Sub* sub);
```

Backend hotplug notification → core walks the intrusive sub list inline → each
`cb`. Zero allocation. Delivery thread is the provider's notification thread (see
Threading).

## Authorization (future, caller-owned storage)

Permission is the genuinely-async OS step. The future lives in caller memory; the
module initialises and resolves it, allocating nothing.

```
const mel_camera_auth* mel_camera_authorization(void);                 // sync current status
bool mel_camera_authorize(Mel_Future* future);                          // caller owns *future; resolves to const mel_camera_auth*
const mel_camera_auth* mel_camera_future_auth(const Mel_Future* f);     // singleton; never freed
```

The future's value is a `const mel_camera_auth*` static singleton. The caller
owns the `Mel_Future` storage and tears it down with `mel_future` directly — the
module adds no destructor because it allocated nothing.

## Open / stream / frames

The open *stream* is caller-owned storage, the only way to support a dynamic
count of simultaneously-open cameras with neither allocation nor a `[MAX]`. The
caller embeds `Mel_Camera_Stream` (stack or in its own struct) and the module
initialises it in place.

```
typedef struct {
    const mel_image_format* format;
    i32 width, height;
    f32 fps;
} Mel_Camera_Config;

typedef struct {
    Mel_Image        image;          // BORROWED OS buffer; valid ONLY during the callback
    u64              timestamp_ns;
    u64              sequence;
    Mel_Image_Orient orient;         // sensor→upright transform the consumer applies if it wants upright pixels
} Mel_Camera_Frame;

typedef void (*Mel_Camera_Frame_Fn)(const Mel_Camera_Frame* frame, void* user);

typedef struct Mel_Camera_Frame_Sub Mel_Camera_Frame_Sub;
struct Mel_Camera_Frame_Sub {                  // caller-owned node
    Mel_Camera_Frame_Fn   cb;
    void*                 user;
    Mel_Camera_Frame_Sub* next;                // core-managed link
};

typedef struct Mel_Camera_Stream Mel_Camera_Stream;   // caller-owned storage; fixed layout (see Failure modes)

bool                     mel_camera_open(Mel_Camera cam, Mel_Camera_Config cfg, Mel_Camera_Stream* stream, Mel_Future* future);
const mel_camera_result* mel_camera_start(Mel_Camera_Stream* stream);   // begin delivery
const mel_camera_result* mel_camera_stop(Mel_Camera_Stream* stream);
void                     mel_camera_close(Mel_Camera_Stream* stream);   // release OS objects, unlink subs

void mel_camera_stream_subscribe(Mel_Camera_Stream* stream, Mel_Camera_Frame_Sub* sub);    // before or after start
void mel_camera_stream_unsubscribe(Mel_Camera_Stream* stream, Mel_Camera_Frame_Sub* sub);

void* mel_camera_native(Mel_Camera_Stream* stream);
```

`open` is the async one (future): native backends resolve it inline once the
session is configured; the web backend resolves it when `getUserMedia` settles.
The open future carries `const mel_camera_result*`. `start`/`stop` are
synchronous — no backend defers them — so they return a result directly rather
than wearing a future costume.

Frame delivery: OS hands a locked buffer → backend wraps it as a borrowed
`Mel_Image` (NV12 Y plane is luminance, zero copy) → core walks the stream's
intrusive sub list inline → each `cb` runs while the buffer is locked → core
releases the buffer on return. `frame.image` is valid only for the callback; a
consumer that needs it afterward copies/converts inside the callback. There is
no pull API — the borrow would dangle, so it does not exist (rather than fail at
runtime).

## Threading & reactor integration

Frames are delivered on the OS's own delivery thread for that backend. The core
neither spawns a thread nor forces one. The reactor *owns* the backends whose
delivery is fd/handle readiness; it does not try to unify the push backends
(that would require a copy across a thread hop, breaking zero-copy).

- **Pollable backends → reactor source** (à la `modules/port`): the backend
  embeds a `Mel_Reactor_Source` in its session (`mel_reactor_source_init`,
  external storage — no allocation), adds a `Mel_Reactor_Poll` over the readiness
  handle, and dispatches frames from the reactor's `dispatch`. No private thread.
  - Linux V4L2: the capture fd (`O_NONBLOCK`), `MEL_REACTOR_POLL_IN`; dispatch =
    `VIDIOC_DQBUF` → deliver → `VIDIOC_QBUF`.
  - Win32 Media Foundation: async mode (`IMFSourceReaderCallback`), the callback
    signals an event HANDLE registered as the poll handle; dispatch reads the
    queued sample. (Replaces the synchronous `ReadSample` thread.)
- **OS-push backends → deliver on the OS thread**, which is hugging the OS:
  - Apple AVFoundation: `AVCaptureVideoDataOutput` delegate on its GCD serial
    queue.
  - Android Camera2: `AImageReader` `onImageAvailable` on the reader's thread.
- **Browser → main thread**: `requestVideoFrameCallback` (per actual decoded
  frame), not `requestAnimationFrame` (per display refresh). The canvas readback
  is inherent to the browser and is the one platform where the frame is not a
  raw OS plane.

Hotplug notifications are rare and delivered inline on the provider's
notification thread.

## Backend contract (provider desc)

```
typedef struct {
    void (*on_frame)(Mel_Camera_Stream* stream, const Mel_Camera_Frame* frame);  // backend → core, buffer locked
    void (*on_hotplug)(Mel_Camera_Provider self, Mel_Camera_Hotplug ev);
    void (*on_auth)(Mel_Future* future, const mel_camera_auth* auth);
} Mel_Camera_Sink;

typedef bool (*Mel_Camera_Raw_Visitor)(void* core_ctx, u64 stable_id, const Mel_Camera_Descriptor* desc);

typedef struct {
    const char* name;
    void*       user;

    void                   (*enumerate)(void* user, Mel_Camera_Raw_Visitor visit, void* core_ctx);
    void                   (*modes)(void* user, u64 stable_id, Mel_Camera_Mode_Visitor visit, void* mv_user);

    const mel_camera_auth* (*authorization)(void* user);
    void                   (*authorize)(void* user, Mel_Future* future, Mel_Camera_Sink sink);

    const mel_camera_result* (*open)(void* user, u64 stable_id, Mel_Camera_Config cfg, Mel_Camera_Stream* stream, Mel_Camera_Sink sink);
    const mel_camera_result* (*start)(void* user, Mel_Camera_Stream* stream);
    const mel_camera_result* (*stop)(void* user, Mel_Camera_Stream* stream);
    void                     (*close)(void* user, Mel_Camera_Stream* stream);

    void*                    (*native)(void* user, Mel_Camera_Stream* stream);
} Mel_Camera_Provider_Desc;
```

The core owns the registry, the sub lists, and the sequence counter; the backend
owns the OS objects, anchored in the caller-provided `Mel_Camera_Stream`.

## Failure modes / open decisions

1. **`Mel_Camera_Stream` storage layout.** It must hold core state (sub-list
   head, sequence, owning `Mel_Camera`, result) *and* the active backend's
   session state, in a fixed caller-owned footprint with no allocation. Backend
   session state varies (AVF: an object ptr; V4L2: fd + reactor source + a table
   of `mmap`'d buffers; MF: reader ptr + event + reactor source). Candidate: a
   header struct plus a backend scratch region sized to the max over compiled
   backends (one backend per platform, so the max is that platform's). The V4L2
   buffer *table* is the sole variable-length piece.
2. **Bounded capture buffers vs MEL-CODE-002.** The count of in-flight DMA
   capture buffers is a tuning constant, not a growable collection — V4L2
   `REQBUFS`, Camera2 `AImageReader` max images, MF sample queue all take a fixed
   small N at open. This is the one place a compile-time constant N is
   structurally inherent. Decision needed: treat N as a protocol-ish constant
   (allowed), or make the buffer table itself caller-provided storage.
3. **Borrow lifetime across reactor dispatch.** On pollable backends the buffer
   must stay `DQBUF`'d for the whole sub-list walk and be `QBUF`'d on return;
   the dispatch must not re-enter. Single-consumer-per-frame is the contract; the
   walk is synchronous and bounded.
4. **Cross-thread subscribe/unsubscribe.** Frames deliver on the OS/reactor
   thread; subscription mutates the intrusive list. Either confine
   subscribe/unsubscribe to the same thread (documented) or guard the list. Lean:
   confine, asserted — no lock in the hot path.
5. **Hotplug ↔ open-stream coherence.** A `removed` event for a camera with a
   live stream must drive that stream to `lost` (the open future / next frame
   surfaces it), never a silent stall.

## Dependencies

`core`, `allocator` (types only — no allocation), `image`, `future`, `reactor`,
`string`, `log`. Dropped versus the prior shape: `collection` (no slotmap, no
dynamic arrays) and `event` (no ring; intrusive inline delivery). `executor`
only if a backend needs to marshal — otherwise dropped.
