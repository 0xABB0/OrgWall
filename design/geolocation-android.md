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
