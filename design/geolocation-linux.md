# geolocation linux backend — spec

`src/linux/geolocation_linux.c`: GeoClue2 (`org.freedesktop.GeoClue2`) over
the system D-Bus. Transport: **libdbus-1** (system library, ubiquitous,
C ABI) — a raw-socket D-Bus client is a module of its own and out of scope;
the dependency is the same species as `-lsensorsapi` on win32. Links
`-ldbus-1`; include path via `pkg-config dbus-1` flags in build.c.
Prerequisite: core spec.

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
