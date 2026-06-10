# camera — design

OS camera capture: enumerate capture devices across providers, authorize, open
and configure a device, stream frames to consumers as a zero-copy borrowed
`Mel_Image`. The decode path (barcode chief among them) is the load-bearing
consumer. This replaces the shipped `modules/camera` surface; the migration
ledger at the end maps old symbols to new.

## Invariants

1. **The module allocates nothing — with one named exception.** No call to any
   `Mel_Alloc` in the module's own code. Storage is caller-owned (passed in) or
   static (const singletons, backend-static provider nodes). The OS's own memory
   (`AVCaptureSession`, Camera2 objects, V4L2 `mmap`'d DMA buffers) is the
   zero-copy frame source and is the OS's to allocate. The exception: pollable
   backends host their readiness in vat sources, and `mel_vat_source_open`
   allocates the source node from the vat's allocator — memory the vat's owner
   chose at `mel_vat_open`, accounted to the vat, freed at
   `mel_vat_source_close`.
2. **No wasted cycles.** Steady-state frame delivery is one indirect call per
   subscriber over a borrowed buffer: no copy, no allocation, no per-frame
   bookkeeping beyond a sequence increment.
3. **Hug the OS.** Each backend delivers frames by the OS's own mechanism on the
   OS's own thread; the core imposes no thread of its own and spawns none.
4. **Open data, not enums** (MEL-CODE-001). Facing, authorization, and result
   are classifications that grow — modelled as opaque structs with const
   singletons, never enums and never status bitmasks.
5. **No fixed-cap collections** (MEL-CODE-002). Variable-length lists (devices,
   modes, subscribers) are *visited or intrusively linked*, never returned as a
   sized array and never stored behind a `[MAX]`.
6. **Loud failure** (MEL-ENGINE-VIII). Misuse (open on a dead camera,
   unsatisfiable config, wrong-thread mutation) fails immediately and audibly,
   never as silent no-frames.
7. **No silent defaults** (MEL-CODE-007). Config carries every knob explicitly —
   format, extent, fps, buffer count; a zero field fails the open. Hotplug
   subscriptions name their executor; there is no implicit delivery target.

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
what futures carry as value — it is the one truth; the future's
`Mel_Future_Status` severity merely mirrors `mel_camera_result_ok` so generic
future plumbing composes.

## Init — the home vat

```
void mel_camera_init(Mel_Vat* vat);    // registers host providers; vat hosts pollable backends
void mel_camera_shutdown(void);
```

The module has one home vat, given at init (the port precedent:
`mel_port_create(.vat = v)`):

- Backends whose delivery and hotplug are fd/handle readiness (V4L2, Media
  Foundation) open their vat sources on it.
- It pins the owner thread: enumeration, open/close, subscribe/unsubscribe are
  confined to the vat owner, asserted with `mel_vat_is_owner`
  (MEL-ENGINE-VIII) — no lock on the frame path.

OS-push backends (AVFoundation, Camera2) never touch the vat's waiter; they
still honor the confinement contract. `mel_camera_init` calls
`mel_camera__register_host_providers` — apps never call it; with boot owning
`main`, `mel_app_setup(Mel_Vat* root)` calls `mel_camera_init(root)` (or a
dedicated io vat).

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

Modes are themselves visited, never returned as an array (no allocation, no `[MAX_MODES]`):

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

## Hotplug (intrusive subscriber, executor-delivered)

Hotplug is rare and off the hot path; consumers should not hand-marshal it. A
subscription names its executor — `mel_executor_inline` is the explicit way to
ask for delivery on the provider's notification thread (MEL-CODE-007: named,
never implied).

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
    Mel_Executor*           exec;               // required; asserted non-NULL
    Mel_Camera_Hotplug_Sub* next;               // core-managed link
    Mel_Task                task;               // core-managed posting state
    Mel_Camera_Hotplug      pending;            // core-managed; latest-wins coalescing per sub
};

void mel_camera_hotplug_subscribe(Mel_Camera_Hotplug_Sub* sub);    // caller fills cb/user/exec
void mel_camera_hotplug_unsubscribe(Mel_Camera_Hotplug_Sub* sub);
```

Backend notification → core walks the intrusive sub list → each sub's event is
posted as its embedded `Mel_Task` to `sub->exec`. Zero allocation: the task and
payload live in the caller's node.

## Authorization (future, caller-owned storage)

Permission is the genuinely-async OS step. `Mel_Future` is a public struct
initialised in place (`mel_future_init`); the future lives in caller memory, the
module resolves it, allocating nothing and adding no destructor.

```
const mel_camera_auth* mel_camera_authorization(void);                  // sync current status
void                   mel_camera_authorize(Mel_Future* future);        // caller init'd; resolves to const mel_camera_auth*
const mel_camera_auth* mel_camera_future_auth(const Mel_Future* f);     // singleton; never freed
```

**Consumption discipline.** A camera future is consumed only from a
`mel_future_then` continuation (deliver on `mel_vat_executor(vat)`), or awaited
from a fiber/coro via `mel_await_future`. Reading `mel_camera_future_auth` on a
possibly-pending future is the eliminated bug class: a pending authorization
misread as denial. In debug, value accessors assert `mel_future_resolved`.

On platforms where authorization is already determined, the backend resolves the
future inline before `authorize` returns — the continuation still runs; callers
never branch on "was it inline".

## Open / stream / frames

Stream storage is caller-provided, the only way to support a dynamic count of
simultaneously-open cameras with neither module allocation nor a `[MAX]`.
Footprint is queried per device + config, because a backend's session state is
variable (the V4L2 `mmap` buffer table scales with `cfg.buffers`); the caller
allocates from its own allocator (MEL-CODE-003) or embeds a worst-case buffer it
owns.

```
typedef struct {
    const mel_image_format* format;
    i32 width, height;
    f32 fps;
    u32 buffers;        // in-flight capture buffers (V4L2 REQBUFS / AImageReader maxImages / MF queue); explicit, no default
} Mel_Camera_Config;

typedef struct {
    Mel_Image        image;          // BORROWED OS buffer; valid ONLY during the callback
    u64              timestamp_ns;
    u64              sequence;
    Mel_Image_Orient orient;         // sensor→upright transform the consumer applies if it wants upright pixels
} Mel_Camera_Frame;

typedef void (*Mel_Camera_Frame_Fn)(const Mel_Camera_Frame* frame, void* user);

typedef struct Mel_Camera_Frame_Sub Mel_Camera_Frame_Sub;
struct Mel_Camera_Frame_Sub {                  // caller-owned node; NO executor — delivery is inline by contract
    Mel_Camera_Frame_Fn   cb;
    void*                 user;
    Mel_Camera_Frame_Sub* next;                // core-managed link
};

typedef struct Mel_Camera_Stream Mel_Camera_Stream;   // header view over caller-provided storage

usize              mel_camera_stream_footprint(Mel_Camera cam, const Mel_Camera_Config* cfg);  // 0 if cam dead/cfg invalid
Mel_Camera_Stream* mel_camera_open(Mel_Camera cam, const Mel_Camera_Config* cfg,
                                   void* storage, Mel_Future* future);   // storage ≥ footprint, caller-owned
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
than wearing a future costume. The same consumption discipline applies to the
open future.

Frame delivery: OS hands a locked buffer → backend wraps it as a borrowed
`Mel_Image` (NV12 Y plane is luminance, zero copy) → core walks the stream's
intrusive sub list inline → each `cb` runs while the buffer is locked → core
releases the buffer on return. `frame.image` is valid only for the callback; a
consumer that needs it afterward copies/converts inside the callback. There is
no pull API — the borrow would dangle, so it does not exist (rather than fail at
runtime). The frame sub carries no executor for the same reason: a deferring
executor would dangle the borrow, so the field does not exist.

**Borrow across drain.** On pollable backends the buffer stays dequeued
(`VIDIOC_DQBUF`) for the whole sub-list walk and is requeued on return; the walk
is synchronous, bounded, and never re-enters the drain.

**Hotplug ↔ stream coherence.** A `removed` event for a camera with a live
stream drives that stream to `lost`: a pending open future resolves
`mel_camera_lost`, a started stream's next `start`/`stop` returns it, and the
hotplug event fires — never a silent stall.

## Threading & vat integration

Frames are delivered on the OS's own delivery thread for that backend. The core
neither spawns a thread nor forces one. The home vat hosts the backends whose
delivery is fd/handle readiness; it does not try to unify the push backends
(that would require a copy across a thread hop, breaking zero-copy).

- **Pollable backends → vat source** (à la `modules/port`): the backend opens a
  `Mel_Vat_Source` on the home vat (`mel_vat_source_open`, vtbl =
  wakeables/deadline/drain/cancel) and delivers frames from `drain`.
  - Linux V4L2: the capture fd (`O_NONBLOCK`) as a wakeable with
    `MEL_VAT_WAKE_IN`; drain = `VIDIOC_DQBUF` → deliver → `VIDIOC_QBUF`. The
    udev monitor fd is a second, provider-level source feeding hotplug.
    **Gated on the epoll/io_uring waiter** — until it ships the backend reports
    `mel_camera_unsupported` honestly rather than spawning a private thread
    (MEL-ENGINE-VII: honest alternative, not a broken shadow).
  - Win32 Media Foundation: a native completion source on the IOCP waiter —
    `IMFSourceReaderCallback` completions land as completion packets, drain
    reads the queued sample. No event-handle shim, no synchronous `ReadSample`
    thread. **Gated on the IOCP waiter** (the same gate `port` win32 sits
    behind); same honest `unsupported` until then.
  - The cocoa ui waiter refuses fd wakeables: a fd-backed camera source cannot
    ride the macOS root vat. Moot for AVFoundation (OS-push), recorded so
    nobody routes a virtual/V4L2-style provider onto it.
- **OS-push backends → deliver on the OS thread**, which is hugging the OS:
  - Apple AVFoundation: `AVCaptureVideoDataOutput` delegate on its GCD serial
    queue.
  - Android Camera2: `AImageReader` `onImageAvailable` on the reader's thread.
    The backend keeps its JNI + `MelodyCamera.java` companion: authorization is
    an Activity permission request round-trip — the future resolves from the
    JNI callback, never synchronously.
- **Browser → guest vat**: the browser loop is the vat (`mel_vat_waiter_guest`
  + embedder). Frames via `requestVideoFrameCallback` (per actual decoded
  frame), not `requestAnimationFrame` (per display refresh). The canvas
  readback is inherent to the browser and is the one platform where the frame
  is not a raw OS plane.

Hotplug notifications are posted per-sub to each subscription's executor (see
Hotplug).

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

    void (*attach)(void* user, Mel_Vat* vat);    // home vat handed once at init; provider opens its sources here
    void (*detach)(void* user);                  // close provider-level sources

    void                   (*enumerate)(void* user, Mel_Camera_Raw_Visitor visit, void* core_ctx);
    void                   (*modes)(void* user, u64 stable_id, Mel_Camera_Mode_Visitor visit, void* mv_user);

    const mel_camera_auth* (*authorization)(void* user);
    void                   (*authorize)(void* user, Mel_Future* future, Mel_Camera_Sink sink);

    usize                    (*footprint)(void* user, u64 stable_id, const Mel_Camera_Config* cfg);
    const mel_camera_result* (*open)(void* user, u64 stable_id, const Mel_Camera_Config* cfg, Mel_Camera_Stream* stream, Mel_Camera_Sink sink);
    const mel_camera_result* (*start)(void* user, Mel_Camera_Stream* stream);
    const mel_camera_result* (*stop)(void* user, Mel_Camera_Stream* stream);
    void                     (*close)(void* user, Mel_Camera_Stream* stream);

    void*                    (*native)(void* user, Mel_Camera_Stream* stream);
} Mel_Camera_Provider_Desc;
```

The core owns the registry, the sub lists, and the sequence counter; the backend
owns the OS objects, anchored in the caller-provided stream storage past the
core header. `footprint` is the provider's session size for that device+config —
the core adds its header and returns the sum; a third-party provider states its
own size, nothing is capped at a compile-time max over known backends.

## Migration ledger (shipped module → this design)

The shipped `modules/camera` (slotmap handles, `Mel_Event` frames, heap
futures, `Mel_Camera_Status` bitmask) migrates by coordinated rename — two app
consumers (`camera-scanner`, `barcode-reader`), no shim.

| shipped | this design |
|---|---|
| `Mel_Camera { Mel_SlotMap_Handle }` | `Mel_Camera { provider, stable_id }` |
| `Mel_Camera_Status` bitmask | `const mel_camera_result*` singletons |
| `mel_camera_init(alloc, Mel_Executor*)` | `mel_camera_init(Mel_Vat*)` |
| `mel_camera_refresh/count/list` | `mel_camera_each` visitor |
| `mel_camera_describe(c, alloc)` + `describe_free` | `mel_camera_describe(c, visitor, user)`, borrowed descriptor |
| `Mel_Camera_Modes` dynamic array on descriptor | `mel_camera_modes_each` visitor |
| `mel_camera_subscribe(exec, cb, user)` slotmap sub | `mel_camera_hotplug_subscribe(Mel_Camera_Hotplug_Sub*)` intrusive node (keeps the executor) |
| `mel_camera_authorize(alloc) → Mel_Future*` | `mel_camera_authorize(Mel_Future*)` caller storage |
| `mel_camera_open/start/stop(c, alloc) → Mel_Future*` | `mel_camera_open(c, cfg, storage, Mel_Future*)`; sync `start`/`stop` results |
| `mel_camera_future_status/free` | gone — caller-owned future, result singleton as value |
| `mel_camera_frames(c) → Mel_Event*` ring | gone — intrusive `Mel_Camera_Frame_Sub`, inline by contract |
| `mel_camera_frame_subscribe(c, cb, user)` slotmap sub | `mel_camera_stream_subscribe(stream, Mel_Camera_Frame_Sub*)` |
| `mel_camera_frame_pull` (fails loudly) | does not exist |

Per-backend: `camera_avf.m` (OS-push; mechanical port), `camera_camera2.c` +
JNI/Java (OS-push; auth via Activity round-trip), `camera_v4l2.c` (rewrite as
vat source; gated on epoll waiter), `camera_mf.c` (rewrite as IOCP completion
source; gated on IOCP waiter), `camera_web.c` (guest vat). The mock provider and
`camera-core` test migrate with the contract.

## Dependencies

`core`, `allocator` (types only — module-owned allocation is nil), `image`,
`future`, `executor` (waist types), `vat`, `string`, `log`. Dropped versus the
shipped module: `collection` (no slotmap, no dynamic arrays) and `event` (no
ring; intrusive inline delivery).

## Open decisions (gabbo)

1. **Per-stream vat override.** One home vat suffices today; a `vat` field on
   `Mel_Camera_Config` would let an app pin a busy capture stream to a
   dedicated io vat. Lean: defer until a consumer exists.
2. **Hotplug coalescing.** The per-sub embedded task coalesces to latest-wins
   when the executor lags. Acceptable for hotplug (add/remove of the same
   device collapses); if per-event fidelity is ever needed the sub grows a
   caller-sized queue. Lean: latest-wins, documented.
