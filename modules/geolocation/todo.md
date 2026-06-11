# geolocation — todo

- win32: compile-validate on the windows box (WinRT C ABI names written against the SDK MIDL
  headers, never compiled; host cross-build lacks windows headers repo-wide).
- win32: check `AltitudeReferenceSystem` (QI Geopoint→IGeoshape) before claiming the altitude
  valid bit; currently set whenever Windows reports an altitude.
- linux: run against a live GeoClue2 (built via zig cross only); verify the oversized
  `DBusMessageIter`/`DBusError` ABI mirrors against a real libdbus.
- android: Java companions compile only at app packaging; package an app that depends on
  geolocation to exercise `mel_android_dependency` (play-services-location AAR) and the
  Geocoder/geofence paths end-to-end.
- android: `streamUpdate` is stop+start (brief gap); fused path could rebuild the request
  without unregistering.
- apple: CLGeocoder is deprecated in the macOS 26 / iOS 26 SDKs in favor of MapKit
  (MKReverseGeocodingRequest); migrate when a MapKit dependency is acceptable.
- network geocoder provider (Nominatim/Photon) for linux/win32/web once an http module exists.
- example app under apps/ showing watch + region + geocode.
- background-scope plumbing: iOS `allowsBackgroundLocationUpdates` + plist background mode,
  android manifest `ACCESS_BACKGROUND_LOCATION` doc (apps must declare it themselves; the
  module manifest deliberately does not, to avoid store-review fallout for apps that never
  use background location).
