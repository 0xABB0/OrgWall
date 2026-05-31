# thermal — todo

Execution checklist and resume point. `spec.md` is authoritative for design.

## Done

- Extracted from the former `sensor` module: tier + temperature + caps, renamed
  to the `Mel_Thermal_*` / `mel_thermal_*` surface. Per-platform `src/` layout,
  discovered by the build.
- Read API: `mel_thermal_current`, `mel_thermal_temperature`, `mel_thermal_caps`.
- macOS lowering — built, **run, and observed** (Apple M3 Pro / macOS 26.2): tier
  `NSProcessInfo.thermalState`; SMC per-domain temperature (`AppleSMC`, no
  entitlement), CPU/GPU `measured`, ambient `derived`, key set memoized after a
  one-time enumeration.
- All platforms cross-compile clean (macos, ios, linux, android, win32, wasm).
  The iOS `temperature.c` is now correctly gated (the former `sensor` build
  globbed only `src/ios/*.m`, dropping it).

## Remaining

- **Run-verify on real hosts.** Windows, Linux, iOS, Android are compile/structure-
  verified only. Linux zone typing + trip-point tier mapping; iOS tier on device;
  Android thermal-status across API levels (the call needs API 29+; below that it
  throws and we report `unknown`).
- **macOS temperature accuracy.** Validate family means against `powermetrics`
  across load and on non-M3 chips (M1/M2/M4, Intel `sp78`). Ambient (`TA*`/`Ta*`)
  is best-effort `derived`; confirm or drop.
- **Android privileged temperature.** `HardwarePropertiesManager.getDeviceTemperatures`
  is `measured` but device/profile-owner only; offer it where the app qualifies,
  falling back to the `/sys` `derived` path.
- **Windows thermal.** Tier + temperature need the WMI `MSAcpi_ThermalZoneTemperature`
  / `IOCTL_THERMAL_QUERY_INFO` opt-in (nontrivial poll cost); absent by default.
- **Web.** No synchronous surface exists; Compute Pressure (`PressureObserver`) is
  async and arrives with the deferred event work.

## Augmentation (from `todo.org`)

- `Mel_Thermal_Sensor` struct enumerated through `mel_thermal_sensor_enumerate`,
  each with a `get(Mel_Thermal_Sensor *self, void *user) → Mel_Thermal_Reading`
  callback returning `{ Mel_Degrees, Mel_Fidelity }`.
- A temperature units module mirroring `time.frequency`, converting between the
  three temperature scales.

## Deferred: change notification

Push-style transition callbacks (`spec.md` §6). Not reactor-coupled. The
per-platform notification surfaces are catalogued in `spec.md` §7.

## Refinements

- Android `caps.present` is true whenever a JNI env exists, but
  `getCurrentThermalStatus` needs API 29+; on older levels the read returns
  `unknown` while caps says present. Tighten by probing the method once.
- SMC read cost: a `cpu` temperature read does one `READ_BYTES` syscall per
  classified sensor (~60 on M3 Pro). Fine at telemetry cadence, not per-frame. A
  min-interval cache would need a clock → a `time`-module dependency; weigh
  against keeping `thermal` core-only.
- The SMC connection (`io_connect_t`) is opened lazily on first temperature read
  and held for process lifetime. No `mel_thermal_shutdown` exists; add one if a
  consumer needs deterministic teardown.

## Known debt carried in from `sensor`

- Public enums (MEL-CODE-001) and the fixed `MEL_SMC_MAX_SENSORS` array
  (MEL-CODE-002) were preserved verbatim by the extraction. Revisit under
  Gabbo's direction when augmenting.
