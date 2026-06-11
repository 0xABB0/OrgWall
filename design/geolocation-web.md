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
