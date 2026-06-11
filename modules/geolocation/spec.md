# geolocation — design

Device positioning as a service: one-shot fixes, continuous watches, heading,
region monitoring (geofencing), and OS geocoding, multiplexed over per-platform
providers (CoreLocation, FusedLocation/LocationManager, Windows.Devices.
Geolocation, GeoClue2, navigator.geolocation). Module `modules/geolocation`,
prefix `mel_geo_` / `Mel_Geo_`.

Granular specs follow below as sections (core, apple, android, win32, linux,
web). Core has no prerequisite; each backend depends only on core (android
additionally on the `mel_android_dependency` build extension, specified in
the android section).

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

---

# geolocation core — spec

Provider-independent half of `modules/geolocation` (see `geolocation.md` for
the surface). No prerequisite; ships with the mock provider and the
`geolocation-core` test target.

## Layout

    modules/geolocation/
        build.c  readme.md
        include/geolocation/geolocation.h     // public surface
        include/geolocation/provider.h        // backend contract
        src/geolocation.c                     // registry, marshalling, demand, regions, timeouts
        src/<platform>/...                    // backends (own specs)
        test/mock_provider.h|c                // registerable scripted provider
        test/geolocation_test.c

## State

One static core struct: home vat, active provider node, intrusive lists
(watches, heading watches, regions, pending requests, pending geocodes),
current demand, marshalling state, timeout source. All mutation owner-confined
(`mel_vat_is_owner` asserted on every public entry).

## Marshalling (backend thread → home vat)

Sink calls may arrive on any thread. The core keeps:

- a latest-wins fix slot + heading slot + stream-result slot (seqlock-style:
  version counter, write, version counter — readers retry on torn reads);
- per-region and per-request/geocode delivery rides each node's embedded
  `Mel_Task` directly (the backend already holds the node pointer);
- one embedded pump `Mel_Task` posted to `mel_vat_executor(home)` whenever a
  slot is written (atomic armed flag dedupes).

The pump task (owner thread) snapshots the slots, walks the watch list,
applies per-watch interval/distance filters, copies fix+result into each
watch's pending slot, posts each watch's task to its executor. Same for
heading. Region software evaluation runs here too. Zero allocation; everything
lives in core statics and caller nodes.

Per-node task: reads its pending slot, invokes `cb`. Latest-wins by
construction (slot overwritten, task re-armed at most once).

Unlink discipline: `watch_stop`/`region_remove`/`request_cancel` (owner
thread) unlink the node and disarm its task; a task that fires after disarm
sees the armed flag cleared and returns. A node is reusable after stop returns.

## Demand union

`demand.accuracy_m = min` over (watches, pending requests, software-region
demand); `min_interval_ns = min`; `min_distance_m = min`. Recomputed on every
add/remove; transitions drive `stream_start` / `stream_update` /
`stream_stop`. Software-region demand: `accuracy = clamp(smallest_radius / 2,
10 m, 1000 m)`, `interval = clamp(time-to-nearest-boundary at last speed, 1 s,
60 s)` recomputed per fix; no fix yet → 1 s until the first one lands.

## Requests

`mel_geo_request` validates, initialises the embedded future, links the node,
includes it in demand, and forwards to the provider's `request` op. Resolution
paths (first wins, by future CAS): backend `on_request`; core timeout; user
cancel. On any resolve the core unlinks, recomputes demand, and calls the
backend's `request_cancel` if the backend had it in flight. `max_age_ns > 0`:
core tries `last_known` first and resolves inline when fresh enough.

## Timeout source

One `Mel_Vat_Source` on the home vat: `deadline()` = min pending-request
deadline (else `MEL_VAT_NEVER`), `drain()` resolves expired requests. Deadlines
are absolute monotonic, stamped at `mel_geo_request`.

## Software region evaluation

For providers with `region_add == NULL`. Inside-state per region, symmetric
hysteresis:

    band    := min(max(hacc, 0.1 * radius), radius / 2)
    inside  := distance <= radius - band/2   (flip only from outside)
    outside := distance >= radius + band/2   (flip only from inside)

First fix after `region_add` seeds inside-state without firing; subsequent
flips fire per `notify_enter`/`notify_exit`. Distance is great-circle
(haversine, f64).

## Mock provider (test/)

Static node + script handles: `mock_geo_install()`, `mock_geo_push_fix(fix)`,
`mock_geo_push_stream_result(r)`, `mock_geo_set_auth(auth)`,
`mock_geo_resolve_request(fix, r)`, `mock_geo_set_caps(caps)`, counters for
stream_start/update/stop and last demand. Push entry points marshal exactly
like a real backend (callable from a test thread).

## Tests (geolocation-core)

- registry: install, init activates, caps pass through, shutdown detaches
- watch: start → demand starts stream; push fix → delivered on inline exec;
  interval + distance filters; latest-wins (push 3, pump once, see last)
- watch outage: denied result delivered with invalid fix, watch survives,
  ok resumes
- request: resolve via mock; cancel resolves cancelled; max_age served from
  last_known; timeout via stepping the vat until deadline source fires
- demand: two watches → min union; stop one → update; stop all → stream_stop
- regions (software): seed, enter, exit, hysteresis no-flap at boundary,
  notify flags respected, remove recomputes demand
- geocode: forward/reverse via mock fill, free releases via node alloc,
  unsupported when ops NULL
- validation: zero accuracy / NULL exec / double start assert-fail paths
  (death tests if the harness has them, else release-mode error returns)

## build.c

Library `geolocation`: `src/geolocation.c` ALWAYS, per-platform sources gated
by `WHEN(.platforms = ...)`, frameworks/libs per backend specs. Test target
`geolocation-core`: core + mock + runner; depends `test`, `geolocation`, and
the transitive set (`core allocator collection string future executor vat log
thread`).

---

# geolocation apple backend — spec

`src/apple/geolocation_apple.m`, shared macOS + iOS, gated
`WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS))`. Links `CoreLocation` +
`Foundation`. Prerequisite: core spec.

## Shape

One `CLLocationManager` + one delegate object (static, lazily created on
attach). Delegate callbacks arrive on the main runloop; every callback
forwards through the sink (core marshals). One `CLGeocoder` for geocoding.

## Mapping

- **available** — always true.
- **caps** — fixes yes; heading `CLLocationManager.headingAvailable` (iOS;
  macOS false); regions_native
  `+isMonitoringAvailableForClass:[CLCircularRegion class]`; geocoding yes;
  background iOS yes (scope always + app capability), macOS false.
- **authorization** — `authorizationStatus` →
  `authorizedAlways/authorizedWhenInUse/denied/restricted/notDetermined`
  singletons.
- **authorize(scope)** — `requestWhenInUseAuthorization` /
  `requestAlwaysAuthorization`; resolve from
  `locationManagerDidChangeAuthorization`. Already determined → resolve
  inline. Missing plist key (`NSLocationWhenInUseUsageDescription` /
  `NSLocationAlwaysAndWhenInUseUsageDescription`) makes the OS no-op: detect
  via `[[NSBundle mainBundle] objectForInfoDictionaryKey:]`, log the key name,
  resolve denied.
- **last_known** — `manager.location`, converted; nil → unavailable.
- **request** — `requestLocation` when no stream is running; with a live
  stream the core's stream fixes satisfy it (the backend resolves from the
  next delegate fix meeting `accuracy_m`).
- **stream** — `desiredAccuracy` from demand accuracy (nearest
  `kCLLocationAccuracy*` tier), `distanceFilter` from `min_distance_m` (0 →
  `kCLDistanceFilterNone`); `startUpdatingLocation`. Interval throttling is
  core-side (CoreLocation has no interval knob).
- **heading** — `startUpdatingHeading` (iOS); `didUpdateHeading` →
  `Mel_Geo_Heading` (`magneticHeading`, `trueHeading` valid when >= 0,
  `headingAccuracy`).
- **regions** — `CLCircularRegion`, identifier = node pointer formatted;
  `startMonitoringForRegion`. The OS cap (20) fails asynchronously:
  `region_add` returns ok, `monitoringDidFailForRegion` arrives as a region
  event carrying `mel_geo_exhausted`. `didDetermineState` seeds inside-state.
- **geocode** — `geocodeAddressString` / `reverseGeocodeLocation`;
  `CLPlacemark` → `Mel_Geo_Place` strings copied via the node's allocator on
  the callback thread (allocator must be thread-safe for geocode nodes;
  documented in the header).
- **fix conversion** — `coordinate`, `ellipsoidalAltitude` (falls back to
  `altitude` with the same valid bit; both are "as the OS says"),
  `horizontalAccuracy < 0` → position invalid, `speed/speedAccuracy`,
  `course/courseAccuracy`, `timestamp` → utc ms; monotonic stamp taken at
  conversion.

## Errors

`didFailWithError`: `kCLErrorDenied` → stream_result denied;
`kCLErrorLocationUnknown` → transient, swallowed (CoreLocation keeps trying);
others → unavailable.

## App requirements (readme)

iOS/macOS apps must ship the usage-description plist keys via
`mel_apple_plist`; iOS background fixes additionally need the location
background mode. The module logs the exact missing key.

---

# geolocation android backend — spec

Two providers registered fused-first (the runtime fallback): GMS
FusedLocationProvider when Play services are present, framework
`android.location.LocationManager` otherwise. `src/android/geolocation_android.c`
(JNI for both) + `src/android/java/orgwall/melody/geolocation/MelodyGeo.java` +
`MelodyGeoFused.java`. Prerequisites: core spec; `mel_android_dependency`
build extension (below).

## Build extension — mel_android_dependency

`modules/build` grows `mel_android_dependency(Mel_Target*, const char* coord)`
(a `Mel_StrVec` on the target). `gen_android_library` emits
`dependencies { implementation("<coord>") }` per entry; the template's
`settings.gradle.kts` already has `google()`. The geolocation lib declares
`com.google.android.gms:play-services-location:21.3.0`.

## Selection

Fused node `available()`:
`GoogleApiAvailabilityLight.isGooglePlayServicesAvailable() == SUCCESS`
(via `MelodyGeoFused.available()`); framework node `available()` always true.
Registration order fused → framework makes init pick fused on GMS devices.

## Java companions

`MelodyGeo.java` (framework): wraps `LocationManager` — `getCurrentLocation`
(API 30+; `requestSingleUpdate` below), `requestLocationUpdates` on the main
looper, `getLastKnownLocation`, `Geocoder` (listener API 33+, blocking call on
a companion-owned single thread below — the one sanctioned thread, documented),
no native regions (software evaluation). Heading: none (framework location has
no heading service) → unsupported.

`MelodyGeoFused.java` (GMS): `FusedLocationProviderClient`
(`getCurrentLocation`, `requestLocationUpdates` with `LocationRequest`
priority mapped from accuracy: <=10 m HIGH_ACCURACY, <=100 m BALANCED, else
LOW_POWER), `GeofencingClient` (native regions; `addGeofences` failure →
exhausted; transitions via `PendingIntent` to a manifest-declared
`BroadcastReceiver`), `Geocoder` shared with the framework path.

Native callbacks surface through `native` methods on the companions; JNI side
converts `Location` → `Mel_Geo_Fix` (`getElapsedRealtimeNanos` → monotonic,
`getTime` → utc, `hasAltitude/hasSpeed/hasBearing/hasAccuracy` → valid bits;
altitude is WGS84-ellipsoid per android contract) and feeds the sink.

## Permissions

Module manifest (`mel_android_manifest`): `ACCESS_COARSE_LOCATION`,
`ACCESS_FINE_LOCATION`; the receiver for geofence transitions.
`authorize(scope)`: `mel_platform_android_request_permission` for FINE
(in_use); scope always additionally requests `ACCESS_BACKGROUND_LOCATION`
(API 29+). `authorization()`: `checkSelfPermission` mapped to the auth
singletons (background granted → granted_always).

## Caps

fused: fixes, regions_native, geocoding (`Geocoder.isPresent`), background
(with background permission); framework: fixes, geocoding, no native regions;
heading false on both.

---

# geolocation win32 backend — spec

`src/win32/geolocation_win32.c`: `Windows.Devices.Geolocation` WinRT consumed
through the raw COM ABI from C (`RoInitialize`, `RoGetActivationFactory`,
`windows.devices.geolocation.h` MIDL interfaces). Links
`-lruntimeobject -lole32`. Prerequisite: core spec.

## Mapping

- **available** — `RoGetActivationFactory(Windows.Devices.Geolocation.Geolocator)`
  succeeds.
- **caps** — fixes yes; heading no (unsupported — compass lives in
  Windows.Devices.Sensors, i.e. the sensor module's turf); regions_native via
  `GeofenceMonitor`; geocoding no (`MapLocationFinder` needs a map-service
  token — unsupported honestly, network provider later); background no.
- **authorization** — `Geolocator.RequestAccessAsync` status +
  `LocationStatus`: `Allowed` → granted_in_use (win32 has one grant level),
  `Denied` → denied, `Unspecified` → not_determined.
- **authorize** — `RequestAccessAsync`; completion delegate resolves the
  future (COM threadpool thread; sink marshals).
- **last_known / request** — `GetGeopositionAsync` (the timeout/maxage
  overload for `max_age_ns`); completion delegate → `on_request`.
- **stream** — `Geolocator` instance with `DesiredAccuracyInMeters` from
  demand, `ReportInterval` from `min_interval_ns`, `MovementThreshold` from
  `min_distance_m`; `PositionChanged` + `StatusChanged` events.
  `StatusChanged`: `Disabled` → denied, `NoData/NotAvailable` → unavailable,
  `Ready` → ok.
- **regions** — `GeofenceMonitor.Current`: `Geofence` with
  `BasicGeoposition` circle, id = formatted node pointer;
  `GeofenceStateChanged` reads the report queue, maps Entered/Exited.
  Creation failure → exhausted from `region_add`.
- **fix conversion** — `BasicGeoposition` lat/lon/alt (altitude reference per
  `AltitudeReferenceSystem`; ellipsoid only when it says so, else altitude
  invalid), accuracy/`VerticalAccuracy`, speed/heading from `Geocoordinate`,
  `Timestamp` → utc; monotonic stamped at conversion (QPC).

## Threading

All WinRT event handlers and async completions fire on COM threadpool threads;
every path goes through the sink. Handler objects are static C structs with
hand-rolled vtables (`IUnknown` + the typed delegate); refcounts are real
(module-lifetime statics pin at 1, release no-ops until detach).

---

# geolocation linux backend — spec

`src/linux/geolocation_linux.c`: GeoClue2 (`org.freedesktop.GeoClue2`) over
the system D-Bus. Transport: **libdbus-1 via `dlopen`** (`libdbus-1.so.3`,
SDL precedent) — a raw-socket D-Bus client is a module of its own and out of
scope. `geolocation_dbus.h` declares the minimal ABI surface locally
(opaque handles; `DBusMessageIter`/`DBusError` caller-allocated and
oversized beyond the real ABI footprint), so the TU cross-compiles without
dbus headers and `available()` degrades honestly to false when the library
is absent at runtime. Links `-ldl` only. Prerequisite: core spec.

## Mapping

- **available** — system bus connects and `org.freedesktop.GeoClue2.Manager`
  is activatable (`Introspect`/`Ping` on the well-known name).
- **caps** — fixes yes; heading no; regions software; geocoding no;
  background no.
- **authorization** — GeoClue authorization is agent/portal mediated and only
  observable by trying: not_determined until a client successfully starts
  (granted_in_use) or fails with AccessDenied (denied).
- **authorize** — creates+starts a throwaway client to force the portal
  prompt; resolves from the outcome.
- **client** — `Manager.GetClient` → set `DesktopId` (process name),
  `DistanceThreshold` from demand, `RequestedAccuracyLevel` from accuracy
  (<=10 m EXACT(8), <=1000 m NEIGHBORHOOD(5), <=10 km CITY(4), else
  COUNTRY(1)); `Client.Start`; `LocationUpdated` signal → fetch the Location
  object's properties (Latitude, Longitude, Accuracy, Altitude, Speed,
  Heading, Timestamp) → fix.
- **last_known** — the client's current `Location` property, if any.
- **request** — stream piggyback: ensure the stream runs, resolve on the
  first fix meeting accuracy; no native one-shot exists.

## Vat integration

The D-Bus connection fd rides the home vat as a `Mel_Vat_Source`
(`MEL_VAT_WAKE_IN`, `dbus_connection_get_unix_fd`); `drain` =
`dbus_connection_read_write(conn, 0)` + dispatch loop while messages pend.
No marshalling needed — sink calls already happen on the home vat.

Blocking calls (`GetClient`, property fetch) use bounded-timeout
`dbus_connection_send_with_reply` + pending-call notify, never
`_and_block`, so the vat is never parked inside libdbus. Known soft spot:
libdbus timeouts are not surfaced as a vat deadline, so a pending-call
expiry against a hung geoclue is only noticed on the next fd wakeup; core
request timeouts are unaffected (core-owned deadline source).

---

# geolocation web backend — spec

`src/wasm/geolocation_wasm.c`: `navigator.geolocation` via `EM_JS` (sensor
module precedent: JS writes into exported alloc/commit shims, C feeds the
sink). Browser callbacks run on the main thread — the guest vat — so sink
calls are already home. Prerequisite: core spec.

## Mapping

- **available** — `typeof navigator.geolocation !== 'undefined'` (secure
  contexts only; http → unavailable).
- **caps** — fixes yes; heading no (devicemotion compass is sensor turf);
  regions software; geocoding no; background no.
- **authorization** — `navigator.permissions.query({name:'geolocation'})`
  where present: granted → granted_in_use, denied → denied, prompt →
  not_determined; Permissions API absent → not_determined.
- **authorize** — the web prompts on use, not on ask: a zero-stakes
  `getCurrentPosition` with long timeout forces the prompt; resolve from
  success (granted_in_use) / `PERMISSION_DENIED` (denied).
- **request** — `getCurrentPosition` with `enableHighAccuracy =
  (accuracy_m <= 100)`, `maximumAge = max_age_ns`, `timeout` left to the
  core's uniform deadline (JS timeout set to infinity).
- **stream** — `watchPosition`, same option mapping; `min_interval_ns` /
  `min_distance_m` filtering is core-side.
- **errors** — `PERMISSION_DENIED` → denied, `POSITION_UNAVAILABLE` →
  unavailable, `TIMEOUT` cannot occur (infinite JS timeout).
- **fix conversion** — `coords` lat/lon/accuracy/altitude(+accuracy)/
  speed/heading (nulls → valid bits off; altitude is WGS84 ellipsoid per
  spec), `timestamp` → utc; monotonic from `emscripten_get_now` at
  conversion.

JS→C delivery copies the ten doubles into a static staging struct via an
exported commit function (no heap traffic per fix).
