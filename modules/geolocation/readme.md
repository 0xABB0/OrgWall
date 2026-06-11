# geolocation

Device positioning as a service: one-shot fixes, continuous watches, heading, region monitoring
(geofencing) and OS geocoding, multiplexed over one active per-platform provider (CoreLocation,
FusedLocation/LocationManager, Windows.Devices.Geolocation, GeoClue2, navigator.geolocation).
Open-data classifications (`mel_geo_auth`/`mel_geo_scope`/`mel_geo_result` const singletons),
caller-owned intrusive nodes, caller-owned futures, one home vat. Design: `design/geolocation*.md`.

## Why it exists

Location is a service every platform exposes differently — different permission models, accuracy
knobs, delivery threads and geofencing caps. This module unifies them behind one demand-multiplexed
stream (the provider runs at most one OS stream at the union of consumer demands), with honest
degradation: software region evaluation where the OS has no geofencing, `mel_geo_unsupported` where
a capability does not exist (MEL-ENGINE-VII, VIII).

## Public surface

- `<geolocation/geolocation.h>` — init/shutdown on a home vat, `mel_geo_caps`, authorization
  (`mel_geo_authorize` resolving a caller-owned future), `mel_geo_last_known`, one-shot
  `Mel_Geo_Request` (core-uniform timeouts, `max_age_ns` for explicit cached serving),
  `Mel_Geo_Watch` (interval/distance filters, latest-wins delivery to the node's executor),
  `Mel_Geo_Heading_Watch`, `Mel_Geo_Region` (native or software-evaluated with hysteresis),
  `Mel_Geo_Geocode` (places allocated from the node's allocator), `mel_geo_distance_m`.
- `<geolocation/provider.h>` — provider contract: static registry nodes, demand stream, sink
  callbacks legal from any thread (the core marshals to the home vat).

All public calls are confined to the home vat's owner thread. Geocode allocators must be
thread-safe (backends fill places on OS callback threads).

## App requirements

- **iOS/macOS** — ship `NSLocationWhenInUseUsageDescription` (and
  `NSLocationAlwaysAndWhenInUseUsageDescription` for `mel_geo_scope_always`) via
  `mel_apple_plist`; iOS background fixes need the `location` background mode. The backend logs
  the exact missing key before reporting denied.
- **Android** — the module manifest declares `ACCESS_COARSE_LOCATION`/`ACCESS_FINE_LOCATION`;
  `mel_geo_scope_always` requests `ACCESS_BACKGROUND_LOCATION` (API 29+). Fused (Google Play
  services) is preferred at runtime, framework `LocationManager` is the fallback.
- **web** — secure context required; the OS prompts on first use.

## Dependencies

`core`, `allocator`, `collection`, `string`, `future`, `executor`, `vat`, `time`, `log`, `debug`;
`platform` on android. Platform: CoreLocation/Foundation (apple), play-services-location via
`mel_android_dependency` (android), runtimeobject/ole32 (win32), dbus-1 (linux, GeoClue2).
