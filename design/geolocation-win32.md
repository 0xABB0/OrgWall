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
