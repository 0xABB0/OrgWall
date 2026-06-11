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
