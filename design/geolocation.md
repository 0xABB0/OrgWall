# geolocation — design

Device positioning as a service: one-shot fixes, continuous watches, heading,
region monitoring (geofencing), and OS geocoding, multiplexed over per-platform
providers (CoreLocation, FusedLocation/LocationManager, Windows.Devices.
Geolocation, GeoClue2, navigator.geolocation). Module `modules/geolocation`,
prefix `mel_geo_` / `Mel_Geo_`.

Granular specs: `geolocation-core.md`, `geolocation-apple.md`,
`geolocation-android.md`, `geolocation-win32.md`, `geolocation-linux.md`,
`geolocation-web.md`. Core has no prerequisite; each backend depends only on
core (android additionally on the `mel_android_dependency` build extension,
specified in `geolocation-android.md`).

## Invariants

1. **The module allocates nothing — with the vat-source exception.** Requests,
   watches, regions, geocodes are caller-owned intrusive nodes. Geocode result
   strings come from the allocator the caller put in the node (MEL-CODE-003).
   The core's timeout source is opened on the home vat via
   `mel_vat_source_open`, which allocates from the vat's allocator — memory the
   vat's owner chose, accounted to the vat.
2. **One OS stream, N consumers.** The provider runs at most one position
   stream, at the union of demands (tightest accuracy, shortest interval,
   smallest distance filter) across watches and software-evaluated regions.
   One-shot requests are the provider's affair (every platform has a native
   one-shot; backends may satisfy them from a live stream). Per-node
   filtering happens in the core. Demand recomputes on every node
   add/remove; no consumer → stream stopped (MEL-ENGINE-III: no cycles the
   user did not ask for).
3. **Hug the OS.** Backends deliver by the OS's own mechanism on the OS's own
   thread; the core spawns no thread. Cross-thread delivery is one task hop to
   the home vat, then per-node tasks to each node's executor.
4. **Open data, not enums** (MEL-CODE-001). Authorization, scope, and result
   are opaque structs with const singletons. Fix field presence is a `valid`
   bitmask (data presence, not classification).
5. **No fixed-cap collections** (MEL-CODE-002). All node sets are intrusive
   lists. OS region caps (CoreLocation 20, GMS 100) surface as a loud
   `mel_geo_exhausted` on `region_add`, never a silent drop.
6. **Loud failure** (MEL-ENGINE-VIII). Zero accuracy, zero timeout, NULL
   executor, geocode without allocator, API call off the owner thread, watch
   started twice — all assert in debug, error loudly in release.
7. **No silent defaults** (MEL-CODE-007). Accuracy, timeout, and delivery
   executor are explicit on every node. A cached fix is returned only through
   `mel_geo_last_known` or an explicit `max_age_ns`; a one-shot never silently
   serves stale data.
8. **Honest degradation** (MEL-ENGINE-VII). Where a platform lacks native
   heading or geocoding, the call reports `mel_geo_unsupported`. Where it lacks
   native geofencing, the core evaluates regions in software from the provider
   stream — a real, documented-cost alternative, not a broken shadow.

## Classifications

```
typedef struct mel_geo_auth mel_geo_auth;
extern const mel_geo_auth mel_geo_auth_granted_always, mel_geo_auth_granted_in_use,
                          mel_geo_auth_denied, mel_geo_auth_restricted,
                          mel_geo_auth_not_determined;
const char* mel_geo_auth_name(const mel_geo_auth*);
bool        mel_geo_auth_is_granted(const mel_geo_auth*);   // either grant level

typedef struct mel_geo_scope mel_geo_scope;                 // requested grant level
extern const mel_geo_scope mel_geo_scope_in_use, mel_geo_scope_always;

typedef struct mel_geo_result mel_geo_result;
extern const mel_geo_result mel_geo_ok, mel_geo_denied, mel_geo_unavailable,
                            mel_geo_timeout, mel_geo_cancelled, mel_geo_unsupported,
                            mel_geo_lost, mel_geo_exhausted;
const char* mel_geo_result_name(const mel_geo_result*);
bool        mel_geo_result_ok(const mel_geo_result*);
```

`mel_geo_result*` is the one truth carried by futures and callbacks; future
severity mirrors it so generic future plumbing composes.

## Data

```
#define MEL_GEO_VALID_POSITION   (1u << 0)    // latitude_deg, longitude_deg
#define MEL_GEO_VALID_ALTITUDE   (1u << 1)
#define MEL_GEO_VALID_HACC       (1u << 2)
#define MEL_GEO_VALID_VACC       (1u << 3)
#define MEL_GEO_VALID_SPEED      (1u << 4)
#define MEL_GEO_VALID_SPEED_ACC  (1u << 5)
#define MEL_GEO_VALID_COURSE     (1u << 6)
#define MEL_GEO_VALID_COURSE_ACC (1u << 7)
#define MEL_GEO_VALID_UTC        (1u << 8)
#define MEL_GEO_VALID_MONOTONIC  (1u << 9)

typedef struct {
    f64 latitude_deg, longitude_deg;        // WGS84
    f64 altitude_m;                          // above WGS84 ellipsoid where the OS says so
    f64 horizontal_accuracy_m, vertical_accuracy_m;   // 68% confidence radius
    f64 speed_mps, speed_accuracy_mps;
    f64 course_deg, course_accuracy_deg;     // clockwise from true north
    u64 utc_unix_ms;
    u64 monotonic_ns;
    u32 valid;
} Mel_Geo_Fix;

typedef struct {
    f64 magnetic_deg, true_deg, accuracy_deg;
    u64 monotonic_ns;
    u32 valid;                               // MEL_GEO_VALID_HEADING_* bits
} Mel_Geo_Heading;

typedef struct {
    bool fixes;
    bool heading;
    bool regions_native;     // false → software evaluation from the stream
    bool geocoding;
    bool background;         // fixes continue while app is backgrounded (scope always)
} Mel_Geo_Caps;
```

## Init — the home vat

```
void mel_geo_init(Mel_Vat* vat);     // registers host providers, picks the active one
void mel_geo_shutdown(void);
Mel_Geo_Caps mel_geo_caps(void);
```

All API calls are confined to the vat owner thread, asserted with
`mel_vat_is_owner`. Backends deliver from OS threads; the core hops once to the
home vat (embedded task, latest-wins slots), then posts per-node tasks to each
node's executor. `mel_executor_inline` on a node is the explicit way to run on
the home vat turn.

## Provider registry & active provider

Static intrusive nodes, camera-design idiom:

```
struct Mel_Geo_Provider_Node { Mel_Geo_Provider_Desc desc; Mel_Geo_Provider_Node* next; };
void mel_geo_provider_register(Mel_Geo_Provider_Node*);
void mel_geo_provider_unregister(Mel_Geo_Provider_Node*);
void mel_geo__register_host_providers(void);
```

Location is a service, not a device set: exactly one provider is *active*. At
init the core walks the registry in registration order and activates the first
whose `available()` returns true. Host providers register most-capable first
(android: fused before framework — the runtime fallback). Registration after
init re-evaluates only if nothing is active; a provider dying mid-run reports
`mel_geo_lost` to every live node, it is not silently swapped.

## Authorization

```
const mel_geo_auth* mel_geo_authorization(void);
void                mel_geo_authorize(const mel_geo_scope* scope, Mel_Future* future);
const mel_geo_auth* mel_geo_future_auth(const Mel_Future* f);   // singleton; never freed
```

Caller-initialised future (`mel_future_init`), resolved by the backend —
inline when already determined; the continuation runs either way. Consumption
discipline: `mel_future_then` or fiber await; debug asserts
`mel_future_resolved` in `mel_geo_future_auth`. Requests/watches on
undetermined authorization implicitly trigger the OS prompt where the platform
does so itself (web, geoclue); they never prompt on platforms where prompting
is a separate call — there the node fails `mel_geo_denied` and the app calls
`mel_geo_authorize` explicitly.

## Last known & one-shot request

```
const mel_geo_result* mel_geo_last_known(Mel_Geo_Fix* out);    // sync; never prompts, never powers radios

struct Mel_Geo_Request {
    f64        accuracy_m;       // required > 0; backend maps to nearest platform tier
    i64        timeout_ns;       // required > 0, or MEL_GEO_NEVER
    i64        max_age_ns;       // accept a cached fix at most this old; 0 = fresh only
    Mel_Future future;           // core-initialised; resolves to const mel_geo_result*
    Mel_Geo_Fix fix;             // core-filled before resolve
    /* core-managed: link, deadline */
};
void mel_geo_request(Mel_Geo_Request* req);
void mel_geo_request_cancel(Mel_Geo_Request* req);     // resolves cancelled if pending
```

Timeouts are core-owned and uniform: one deadline vat source on the home vat,
deadline = min over pending requests; backends never implement timers. A
timed-out request resolves `mel_geo_timeout` with whatever partial fix arrived
(`valid` says how much), and the backend's in-flight one-shot is cancelled.

## Watch (continuous fixes)

```
typedef void (*Mel_Geo_Fix_Fn)(const Mel_Geo_Fix* fix, const mel_geo_result* result, void* user);

struct Mel_Geo_Watch {
    f64           accuracy_m;        // required > 0
    i64           min_interval_ns;   // 0 = every provider update
    f64           min_distance_m;    // 0 = no distance filter
    Mel_Geo_Fix_Fn cb;
    void*          user;
    Mel_Executor*  exec;             // required
    /* core-managed: next, task, pending fix+result, last delivered fix/time */
};
void mel_geo_watch_start(Mel_Geo_Watch*);
void mel_geo_watch_stop(Mel_Geo_Watch*);
```

The provider streams once at the demand union; the core filters per watch
(interval, great-circle distance) and posts to each watch's executor with
latest-wins coalescing — a fix is state, not an event; a lagging executor sees
the newest fix, never a backlog. Stream-level transitions (denied, lost,
unavailable, recovered ok) are delivered through the same callback with the
result singleton and an invalid fix; the watch stays armed across outages and
resumes when the OS recovers — stopping is the caller's decision.

## Heading

```
typedef void (*Mel_Geo_Heading_Fn)(const Mel_Geo_Heading* h, const mel_geo_result* result, void* user);
struct Mel_Geo_Heading_Watch { Mel_Geo_Heading_Fn cb; void* user; Mel_Executor* exec; /* core-managed */ };
void mel_geo_heading_start(Mel_Geo_Heading_Watch*);    // unsupported where the provider has no heading
void mel_geo_heading_stop(Mel_Geo_Heading_Watch*);
```

Heading here is the *location service's* heading (course-corrected compass);
raw magnetometer fusion belongs to `modules/sensor`.

## Regions (geofencing)

```
typedef struct {
    Mel_Geo_Region*       region;
    bool                  entered, exited;
    const mel_geo_result* result;
} Mel_Geo_Region_Event;
typedef void (*Mel_Geo_Region_Fn)(const Mel_Geo_Region_Event* ev, void* user);

struct Mel_Geo_Region {
    f64  latitude_deg, longitude_deg, radius_m;     // required; radius > 0
    bool notify_enter, notify_exit;                 // at least one required
    Mel_Geo_Region_Fn cb;
    void*             user;
    Mel_Executor*     exec;                         // required
    /* core-managed: next, task, pending, inside-state, native id */
};
const mel_geo_result* mel_geo_region_add(Mel_Geo_Region*);   // exhausted on OS cap
void                  mel_geo_region_remove(Mel_Geo_Region*);
```

Native where the provider implements region ops (CoreLocation, GMS
geofencing); elsewhere the core evaluates in software: while ≥1 region exists
it adds a region-demand to the stream union (accuracy scaled to the smallest
radius, interval scaled to distance-to-nearest-boundary) and flips inside-state
with hysteresis — a transition fires only when the fix center crosses the
boundary by more than `max(horizontal_accuracy_m, 0.1 * radius_m)`. The power
cost is the stream the regions demanded — visible, user-asked
(MEL-ENGINE-III). Software evaluation has no region cap and no background
delivery; `caps().regions_native` and `caps().background` say which world the
app is in.

## Geocoding

```
typedef struct {
    str8 name, thoroughfare, locality, admin_area, postal_code, country, country_code;
    f64  latitude_deg, longitude_deg;
    u32  valid;
} Mel_Geo_Place;

struct Mel_Geo_Geocode {
    str8 query;                              // forward: required non-empty
    f64  latitude_deg, longitude_deg;        // reverse: required
    u32  max_results;                        // required > 0
    const Mel_Alloc* alloc;                  // required; places + strings
    Mel_Future       future;                 // core-initialised; resolves to const mel_geo_result*
    Mel_Geo_Places   places;                 // Mel_Array(Mel_Geo_Place), filled before resolve
    /* core-managed: link */
};
void mel_geo_geocode_forward(Mel_Geo_Geocode*);
void mel_geo_geocode_reverse(Mel_Geo_Geocode*);
void mel_geo_geocode_cancel(Mel_Geo_Geocode*);
void mel_geo_geocode_free(Mel_Geo_Geocode*);     // releases places via node alloc
```

Geocoding is the OS's service (CLGeocoder, android.location.Geocoder);
platforms without one report `mel_geo_unsupported` — a network geocoder is a
future provider, not this module's business to ship secretly.

## Backend contract

```
typedef struct { f64 accuracy_m; i64 min_interval_ns; f64 min_distance_m; } Mel_Geo_Demand;

typedef struct {
    void (*on_fix)(const Mel_Geo_Fix* fix);                       // stream fix, any thread
    void (*on_stream_result)(const mel_geo_result* r);            // denied/lost/unavailable/ok transitions
    void (*on_heading)(const Mel_Geo_Heading* h);
    void (*on_region)(Mel_Geo_Region* region, bool entered);      // native region transition
    void (*on_auth)(Mel_Future* future, const mel_geo_auth* auth);
    void (*on_request)(Mel_Geo_Request* req, const Mel_Geo_Fix* fix, const mel_geo_result* r);
    void (*on_geocode)(Mel_Geo_Geocode* g, const mel_geo_result* r);   // places filled by backend
} Mel_Geo_Provider_Sink;

typedef struct {
    const char* name;
    void*       user;

    bool (*available)(void* user);
    void (*attach)(void* user, Mel_Vat* vat, const Mel_Geo_Provider_Sink* sink);
    void (*detach)(void* user);
    Mel_Geo_Caps (*caps)(void* user);

    const mel_geo_auth* (*authorization)(void* user);
    void (*authorize)(void* user, const mel_geo_scope* scope, Mel_Future* future);

    const mel_geo_result* (*last_known)(void* user, Mel_Geo_Fix* out);
    void (*request)(void* user, Mel_Geo_Request* req);            // resolve via sink.on_request
    void (*request_cancel)(void* user, Mel_Geo_Request* req);

    const mel_geo_result* (*stream_start)(void* user, const Mel_Geo_Demand* d);
    void (*stream_update)(void* user, const Mel_Geo_Demand* d);
    void (*stream_stop)(void* user);

    const mel_geo_result* (*heading_start)(void* user);           // NULL = unsupported
    void (*heading_stop)(void* user);

    const mel_geo_result* (*region_add)(void* user, Mel_Geo_Region* r);   // NULL = software evaluation
    void (*region_remove)(void* user, Mel_Geo_Region* r);

    void (*geocode_forward)(void* user, Mel_Geo_Geocode* g);      // NULL = unsupported
    void (*geocode_reverse)(void* user, Mel_Geo_Geocode* g);
} Mel_Geo_Provider_Desc;
```

Sink calls are legal from any thread; the core marshals (one mpsc-fed task hop
to the home vat) before touching node lists. Node pointers handed to a backend
(`request`, `region_add`, `geocode_*`) stay valid until the matching
cancel/remove/resolve — the core guarantees it, backends never free them.

## Failure modes iterated

- **Denied / revoked mid-stream** → `on_stream_result(denied)`; watches receive
  it and stay armed; pending requests resolve denied; native regions report
  denied per-region. Re-grant → `on_stream_result(ok)` and the stream resumes.
- **Location services off globally** → `unavailable`, same recovery contract.
- **Timeout** → core deadline source; partial fix delivered with `timeout`.
- **Cancel** races resolve: future's write-once CAS decides; both sides accept
  losing.
- **Stale fix masquerading as live** → impossible by construction
  (`last_known` and `max_age_ns` are the only cached paths).
- **OS region cap** → `mel_geo_exhausted` from `region_add`, loud.
- **Region flap at the boundary** → hysteresis band above.
- **Executor lag** → latest-wins per node; fixes/headings are state; region
  pending coalesces enter+exit into the latest transition (documented).
- **Provider death (GMS process, geoclue daemon)** → `lost` to every node; no
  silent provider swap mid-run.
- **Backgrounding** → platform-documented per backend; `caps().background`
  plus `mel_geo_scope_always` name the contract; foreground-only platforms
  pause and resume with `on_stream_result`.
- **App forgot the platform permission declaration** (plist key, manifest
  entry) → backend detects the OS's instant-deny and logs the missing key by
  name before reporting denied (MEL-ENGINE-V).
- **Double start / remove of a never-added region / zero accuracy** → debug
  assert, release loud error log + no-op.

## Threading summary

| platform | delivery thread | marshalling |
|---|---|---|
| macOS/iOS | CLLocationManagerDelegate on main runloop | sink → home-vat hop |
| android | Looper callbacks via JNI on a binder/looper thread | sink → home-vat hop |
| win32 | WinRT event on COM threadpool | sink → home-vat hop |
| linux | D-Bus fd as vat source on the home vat | already home |
| web | browser loop (guest vat) | already home |

## Dependencies

`core`, `allocator` (types), `collection` (mpsc, array), `string`, `future`,
`executor`, `vat`, `log`; `platform` on android only. Math is two great-circle
helpers implemented locally.

## Open decisions (gabbo)

1. **Network geocoder provider** (Nominatim/Photon) for linux/web/win32 once an
   http module exists — the provider seam is already shaped for it.
2. **Per-watch provider override** (e.g. a replay provider for testing while
   the OS provider runs) — deferred until a consumer exists; registry supports
   it structurally.
3. **Software region evaluation while backgrounded** is out: it would require
   the module to keep the app alive, which is the app's call, not the
   engine's.
