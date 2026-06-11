# 2026-06-11 — geolocation module

## Work done

New `modules/geolocation`: device positioning as a service — one-shot fixes, continuous
watches, heading, geofencing, OS geocoding — over one active per-platform provider. Scope,
Android dual-backend (Fused preferred, LocationManager fallback), all-six-platforms and the
name were Gabbo's calls.

- **Design** followed the camera.md idiom (the repo's current design direction): open-data
  classifications as const singletons (`mel_geo_auth`/`mel_geo_scope`/`mel_geo_result`),
  caller-owned intrusive nodes, caller-owned/embedded futures, one home vat, provider registry
  of static nodes. Specs written in `design/`, iterated against failure modes (denial
  mid-stream, global service toggle, timeout/cancel races, stale-fix masquerade, OS region
  caps, boundary flap, executor lag, provider death, backgrounding, missing
  plist/manifest declarations), then moved to `modules/geolocation/spec.md` per MEL-SPEC-002
  once implemented.
- **Core** (`src/geolocation.c`): demand-multiplexed single OS stream (union of watch +
  software-region demands), per-watch interval/distance filters with latest-wins seqlock
  delivery to each node's executor, one mpsc-free marshalling hop (seqlock slots + armed pump
  task) from any backend thread to the home vat, core-uniform request timeouts via one vat
  deadline source, software geofence evaluation with symmetric hysteresis, geocode nodes with
  caller allocators, claim-CAS resolution races (backend/timeout/cancel — first wins).
- **Backends**: apple (CoreLocation: auth incl. plist-key detection, dual managers for
  stream/one-shot, heading on iOS, native CLCircularRegion monitoring, CLGeocoder);
  android (JNI + `MelodyGeo`/`MelodyGeoFused`/`MelodyGeoFenceReceiver` Java companions, GMS
  runtime detection orders fused before framework, permissions via the platform module's
  future chain incl. background scope, GMS geofencing, Geocoder on a lazy companion thread);
  win32 (Windows.Devices.Geolocation through the raw COM ABI in C: Geolocator stream +
  one-shots, RequestAccessAsync, GeofenceMonitor native regions); linux (GeoClue2 over
  dlopen'd libdbus-1 riding the home vat as an fd source); wasm (navigator.geolocation via
  EM_JS, Permissions API, prompt-on-use authorize).
- **Build framework extension**: `mel_android_dependency(t, coord)` emits
  `dependencies { implementation(...) }` in generated gradle library projects — needed for
  play-services-location, usable by any module.
- **Tests**: `geolocation-core` — 18 tests over the mock provider (activation, delivery,
  coalescing, filters, outage/recovery, demand union, request resolve/cancel/max-age/timeout,
  software region hysteresis, heading, geocode fill/cancel, authorize, provider loss,
  great-circle sanity). All pass.
- **Validation**: tests green on macOS; lib compiles for macos, ios, android (C side), wasm,
  linux (zig cross). Neighbor suites (vat-core, future-core, sensor-core) still green.

- **Demo app** `apps/geo-tour` (boot-hosted console tour): prints caps + current
  authorization, authorizes (in-use scope), then on grant: last-known, one-shot request
  (bounding the run via the module's own timeout), a watch with interval filter, heading
  where supported, a 75 m geofence armed on the first fix (reporting native vs software
  monitoring), reverse geocode of the fix, plus a forward geocode that also runs on the
  denied path. Exits by releasing the root vat once all stages settle; a 10 s "grace"
  one-shot bounds the watch on stationary machines. Verified live on macOS: granted path
  delivered real fixes, armed an OS-monitored region, and resolved both geocodes; denied
  path (unbundled binary, no plist key) exits 0 through geocoding-only. Builds for wasm.

## Kludges

- **win32 backend never compiled.** Host cross-build for win32 is broken repo-wide (plain
  clang, no Windows SDK), and remote validation needs either this branch merged to main (then
  the documented pull+build flow) or permission to check the branch out on win-pilot. The
  WinRT C ABI names are written against the SDK MIDL headers from memory — expect a round of
  compile fixes.
- **win32 altitude reference unchecked**: the altitude valid bit is set whenever Windows
  reports one, without QI-ing for `AltitudeReferenceSystem`; may be surface-referenced,
  deviating from the "ellipsoid where the OS says so" field contract.
- **linux libdbus ABI mirrored locally** (`geolocation_dbus.h`): opaque handles are safe, but
  `DBusMessageIter`/`DBusError` are caller-allocated structs mirrored as oversized blobs
  (128 B / name+message+pad). Safe by construction only if libdbus never grows them beyond
  that; never run against a live GeoClue2.
- **Android Java compiles only at app packaging.** `nob build geolocation android` validates
  the C/JNI side only; the companions, the AAR dependency and the geofence receiver are
  unexercised until an app consumes the module.
- **Android `authorization()` cannot distinguish denied from never-asked** (maps both to
  not_determined); a real denied only surfaces from an authorize round-trip.
- **Android framework `streamUpdate` is stop+start** — a brief update gap on demand changes.
- **Apple backend asserts the home vat is the main thread** instead of dispatching to it —
  honest and loud, but apps with a non-main home vat must wrap their own marshalling.
- **Geocoder companion thread (android)**: blocking `Geocoder` runs on one lazy
  `HandlerThread` — a deliberate, spec-documented thread (MEL-ENGINE-III tension accepted).
- **Geocode allocators must be thread-safe** (backends fill places on OS callback threads) —
  stated in readme, enforceable nowhere.
- **`mel_geo_shutdown` memsets the registry**, dropping any third-party providers registered
  before init across an init/shutdown/init cycle; host providers re-register so only
  external registrants are affected.
- **Region software seeding never fires an initial event** (spec'd, but a surprise if the app
  adds a region while inside and waits for an enter).
- **geo-tour's grace path is reasoned, not observed**: the live granted run happened before
  the grace bound was added, and re-verifying requires answering a fresh TCC prompt
  (rebuilt binary → re-prompt). The denied path is verified end-to-end with the final code.
- **geo-tour found a lifetime gotcha worth knowing**: `mel_geo_init` opens a vat source, so
  a retained-then-released root vat still does not end its run until `mel_geo_shutdown` —
  apps must shut the module down from inside the run, not from `mel_app_on_exit`.
- **Full `./nob test` sweep aborted** — the background run produced no output in 20 minutes
  (likely ninja contention with the foreground builds); replaced with targeted suites
  (geolocation-core, vat-core, future-core, sensor-core), all green. The full-suite pass on a
  quiet checkout is still owed.

## CLAUDE.md suggestions (recommendations only)

- platforms.md documents `mel_cflags`/`mel_link`/etc. but none of the packaging surface
  (`mel_android_manifest/java/namespace/dependency`, `mel_apple_plist`); a packaging
  authoring section would have saved a code-spelunk.
- The win32 host cross-build being unavailable (no SDK headers, despite zig being a listed
  prerequisite) is worth stating in CLAUDE.md's win32 section so agents don't rediscover it.

## Suggestions

- Merge → main, then `git pull` + `nob build geolocation` on win-pilot for the win32 round.
- `apps/geo-tour` is the living smoke test; the android packaging gap stays open until a
  boot entry for android exists (boot today: macos + wasm) — packaging geo-tour for android
  then will exercise `mel_android_dependency` and the Java companions end-to-end.
- The camera.md migration would benefit from the marshalling pattern built here (seqlock
  slots + armed pump task); they're siblings.
- CLGeocoder is deprecated in the macOS/iOS 26 SDKs — MapKit migration tracked in
  `modules/geolocation/todo.md`.
