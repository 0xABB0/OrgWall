# Melody Thermal — `thermal`

OS-level thermal telemetry — a coarse pressure tier and an accurate per-domain
temperature — surfaced as a standalone top-level module. Extracted from the
former `sensor` module (whose `power` half is now the sibling `power` module).

This module is bound by the Ten Commandments of the Engine. Where a decision
turns on one, the commandment is cited by tag (`MEL-ENGINE-N`).

---

## 1. Module identity

`thermal` is its own top-level module at `modules/thermal/`. It is **not** a child
of any `platform` module, and there is no runtime platform object: the platform is
a build axis resolved at compile time (one executable, one platform), so the
active OS lowering is selected by the build's source-directory gating, not
dispatched through a runtime handle.

`thermal` reports a condition the app observes — OS-published thermal state — not
a hardware device the app commands.

---

## 2. Inherited principles

- **Emulate-to-equivalent absent faking.** Where the OS publishes no signal,
  `thermal` reports `unknown` / `none`, never a fabricated `nominal`. The API
  shape stays uniform; the carried value is honest (MEL-ENGINE-VIII).
- **Pull, not push.** Every signal is read on demand through a synchronous
  accessor; the engine spends zero cycles when the app does not ask (MEL-ENGINE-III).
  Push-style change notification is deferred — see §6.
- **Mechanism, not policy.** `thermal` reports the tier the OS reports. The
  decision — drop render resolution, switch to `Capped(30)`, suspend non-critical
  compute — belongs to the app's content model (MEL-ENGINE-V).
- **Honest gating.** Where no OS surface exists, the read reports `unknown` and
  the cap reports absent; the engine does not synthesize a value from indirect
  signals (e.g. inferring thermal pressure from frame-time variance).

---

## 3. Read API

The module is stateless and global: every accessor queries the OS surface for the
active build platform and returns the current value. There is no instance to
construct, no reactor to bind, and no registration. A call costs nothing until the
app makes it.

    Mel_Thermal_Pressure    mel_thermal_current(void);
    Mel_Thermal_Temperature mel_thermal_temperature(Mel_Thermal_Temp_Domain domain);
    Mel_Thermal_Caps        mel_thermal_caps(void);

**Capability inspection.** `mel_thermal_caps()` returns `{ present, temperature }`:
whether the running build's platform lowering publishes a thermal tier, and the
best temperature fidelity the `primary` domain achieves on this build. A consumer
that needs to branch on availability rather than absorb `unknown` reads this once
at startup. A runtime `unknown` read on a present surface (e.g. the OS has not yet
sampled) is still honest.

---

## 4. Tier

    Mel_Thermal_Pressure ∈ { unknown, nominal, fair, serious, critical }

`unknown` is the honest absence; on platforms with no thermal-pressure surface
(vanilla desktop Linux without trip-point zones, browser builds without Compute
Pressure, headless servers) the consumer sees `unknown`. `nominal | fair | serious
| critical` align with `NSProcessInfoThermalState` and Android's
`PowerManager.THERMAL_STATUS_*` (`LIGHT` → `fair`, `MODERATE` → `serious`,
`SEVERE | CRITICAL | EMERGENCY | SHUTDOWN` → `critical`; coalesced). The coalesce
loses Android's extra resolution intentionally: a portable consumer cannot
meaningfully discriminate distinctions no other platform exposes (MEL-ENGINE-IV —
the underlying Android tier stays reachable through a platform-specific interop
path where a consumer specifically needs it).

## 5. Temperature

The tier is the portable coarse signal; **temperature** is the accurate one,
exposed directly rather than hidden behind an escape hatch (MEL-ENGINE-II — a
number the hardware measures is power the user should reach).

    Mel_Thermal_Temp_Domain   ∈ { primary, cpu, gpu, ambient }
    Mel_Thermal_Temp_Fidelity ∈ { none, derived, measured }

    typedef struct { f32 celsius; Mel_Thermal_Temp_Fidelity fidelity; } Mel_Thermal_Temperature;

`primary` is the platform's most representative figure (the SoC/CPU package);
`primary` aliases `cpu` where no distinct package sensor exists.

**Gentle degradation (MEL-ENGINE-VII).** The fidelity is the honest signal of how
much to trust `celsius`:

- `measured` — a direct hardware-sensor reading (the mean of the platform's die
  sensors for that domain). `celsius` is trustworthy.
- `derived` — a coarse or approximate figure (an ambient estimate, or the hottest
  available zone standing in for a domain with no dedicated sensor). Indicative.
- `none` — the platform exposes no usable sensor for that domain. `celsius` is `0`
  and must be ignored; the consumer falls back to the tier.

The engine never fabricates a temperature: a platform with no sensor returns
`none`, not a plausible-looking number (MEL-ENGINE-VIII).

---

## 6. Deferred: change notification

Push-style notification — a callback that fires when the tier transitions — is
**deferred**, not refused (MEL-ENGINE-I). The current module is pull-only: the
consumer samples at a cadence it owns (e.g. the frame-pacing tick). When
notification lands it will **not** be reactor-coupled: `thermal` has no reactor
dependency, and the delivery mechanism is a plain callback contract whose
threading and coalescing semantics are specified at that time. The per-platform
notification surfaces are catalogued in §7 so the work is resumable.

---

## 7. OS lowerings

Selected by source-directory gating (`src/<platform>/` and the platform family
chain), so each platform compiles exactly one definition of each accessor.

### 7.1 Pressure tier

- **iOS / iPadOS / macOS** — read: `NSProcessInfo.thermalState`. Notification
  (deferred): `NSProcessInfoThermalStateDidChangeNotification`. Reliable on Apple
  Silicon; on Intel Macs the value transitions less aggressively.
- **Android** — read: `PowerManager.getCurrentThermalStatus()` (API 29+).
  Notification (deferred): `PowerManager.OnThermalStatusChangedListener`. Below API
  29 the read returns `unknown`.
- **Windows** — no first-class equivalent. `IOCTL_THERMAL_QUERY_INFO` / WMI
  `MSAcpi_ThermalZoneTemperature` only under an opt-in flag (the WMI poll has
  nontrivial cost); without opt-in, `present = false`. The engine **does not**
  infer pressure from CPU-frequency throttling (MEL-ENGINE-VIII).
- **Linux** — `/sys/class/thermal/thermal_zone*/temp` paired with the device's
  `trip_point_*_temp` thresholds to derive a tier. No kernel-ABI notification
  surface exists (the deferred path would low-frequency poll). `present = true`
  only when a zone with a usable trip-point set is discovered.
- **Web** — the Compute Pressure API (`PressureObserver`, Chrome 125+) for the
  `"thermals"` source where granted; observer-shaped (no synchronous read), so it
  is part of the deferred §6 work. Until then web reports `unknown`.

### 7.2 Temperature

- **macOS** — `measured`, via the Apple SMC (`AppleSMC` IOKit service,
  `IOConnectCallStructMethod`). No entitlement or root. Keys are enumerated once
  (the `#KEY` count, then `READ_INDEX`) and classified by family: `Tp*`/`Te*`
  (CPU P-/E-cores) → `cpu`/`primary`, `Tg*` (GPU) → `gpu`, `TA*`/`Ta*` →
  `ambient` (reported `derived`). Each domain is the mean of its plausibly-ranged
  (`0 < t < 150 °C`) `flt ` sensors; `sp78` is decoded too for Intel Macs. This
  families-and-mean approach is chip-agnostic across M-series rather than relying
  on fragile per-core key tables. The connection and key set are memoized.
- **iOS / iPadOS** — `none`. The SMC is not reachable from the iOS sandbox; no
  public per-domain temperature API exists. Falls back to the tier.
- **Linux** — `measured` / `derived`, via `/sys/class/thermal/thermal_zone*/{type,temp}`
  (millidegrees). A `cpu`/`pkg`/`soc`/`coretemp`-typed zone answers `cpu`/`primary`
  (`measured`); a `gpu`-typed zone answers `gpu`; with no typed match, the hottest
  zone answers `primary` as `derived`. Ambient is `none`.
- **Windows** — `none` by default (WMI opt-in, as the tier).
- **Android** — `derived`, via the thermal-zone read (`/sys/class/thermal`), the
  only die-temperature path a non-privileged app has. The official
  `HardwarePropertiesManager.getDeviceTemperatures` is `measured` but restricted to
  device/profile-owner apps, so it is not the default path.
- **Web** — `none`. Browsers expose no temperature API.

`mel_thermal_caps().temperature` reports the best fidelity the `primary` domain
achieves on this build, for a one-shot startup branch.

---

## 8. Downstream consumers

`thermal` has no upstream module dependency beyond `core` (types/compiler) and
`platform` (the Android JNI bootstrap). Downstream consumers:

- **`gpu`** — re-exports the tier through `caps.power.thermal_pressure` as a
  read-only view; the OS lowering lives here. The GPU re-export is documentary,
  not authoritative.
- **`frame.pacing`** — reads `thermal` once per registered render source and
  threads `thermal_pressure` into the `Frame_Info` passed to the render callback;
  it carries the value, takes no action (mechanism only).
- **`xr`** — XR runtimes publish their own thermal-budget surface; the XR module
  uses both — the XR-runtime budget for headset-internal state, `thermal` for the
  host-side complement — and does not silently fold them (MEL-ENGINE-VIII).
- **`media.video`** — encoder rate-control consults the tier to pick a sustainable
  preset (mechanism only).

---

## 9. Sensor enumeration

Per-domain `mel_thermal_temperature` is an aggregate. **Enumeration** exposes the
individual physical sensors behind it (each `/sys` thermal zone, each classified
SMC die key), each pulled on demand through a callback. It is **additive** — the
aggregate accessor of §5 is unchanged.

    typedef struct { Mel_Degrees value; Mel_Thermal_Temp_Fidelity fidelity; } Mel_Thermal_Reading;

    typedef struct Mel_Thermal_Sensor Mel_Thermal_Sensor;
    typedef Mel_Thermal_Reading (*Mel_Thermal_Sensor_Get)(Mel_Thermal_Sensor *self, void *user);

    struct Mel_Thermal_Sensor {
        const char             *name;    // SMC fourcc, or /sys zone type
        Mel_Thermal_Temp_Domain domain;  // cpu | gpu | ambient | primary
        Mel_Thermal_Sensor_Get  get;     // pull a fresh reading
        u64                     handle;  // opaque backend datum
    };

    typedef struct { Mel_Thermal_Sensor *items; usize count; } Mel_Thermal_Sensor_List;

    Mel_Thermal_Sensor_List mel_thermal_sensor_enumerate(const Mel_Alloc *alloc);
    void                    mel_thermal_sensor_list_free(Mel_Thermal_Sensor_List *, const Mel_Alloc *);
    Mel_Thermal_Reading     mel_thermal_sensor_read(Mel_Thermal_Sensor *self, void *user);  // convenience

`Mel_Degrees` is the value type of the standalone `temperature` units module
(mirroring `frequency`), kelvin-canonical, exact between Celsius / Fahrenheit
/ Kelvin. A `none` reading carries `0 K` — the absolute-zero sentinel
(`mel_degrees_is_absolute_zero`), never a plausible-looking lie (MEL-ENGINE-VIII).

**Memory (MEL-CODE-002/003).** The list is one contiguous allocation from the
caller's allocator — `items[]` followed by the name bytes — so `*_list_free` is a
single `mel_dealloc` (platform-agnostic, `src/sensor.c`, `ALWAYS`). **Pull, not
push** (MEL-ENGINE-III): `get` re-reads its source each call; `self->handle` is the
backend's datum (macOS: address of the process-lifetime static SMC entry;
Linux/Android: the `thermal_zone<N>` index), `user` is the caller's pass-through.

`primary` is a virtual aggregate, not enumerated as a physical sensor.

### 9.1 Lowerings

- **macOS** — one sensor per classified `flt ` SMC die key (from the memoized
  `mel_smc_init` groups). `get` reads that single key; cpu/gpu `measured`, ambient
  `derived`. Observed on M3 Pro: ~50 Tp* (cpu), ~18 Tg* (gpu), Ta*/TA* ambient.
- **Linux / Android** — one sensor per `/sys/class/thermal/thermal_zone*`, by
  scanning until the first absent `type`; classified by zone type; each a real
  kernel sensor → `measured`. Shared `mel_sysfs_sensor_enumerate`.
- **iOS / Windows / Web** — empty list `{NULL, 0}` (no SMC in the iOS sandbox; WMI
  opt-in absent; no synchronous browser surface).

### 9.2 Cost carried in (MEL-ENGINE-VIII)

The `Mel_Real` backing of `Mel_Degrees` makes `thermal` depend on `mpfr`/`gmp`.
**All six first-class targets build clean** (macos/ios/android/win32/linux/wasm).
Bringing wasm up required a framework fix: the autotools cross-configure now passes
`AR=emar RANLIB=emranlib` for wasm (`modules/build/thirdparty.c`) — the host
`ranlib` cannot index emscripten objects (`malformed uleb128`). macОС runs the
feature on real SMC hardware; `temperature-example.wasm` / `thermal-sensors.wasm`
run under node.

The push-style change notification of §6 remains deferred and is orthogonal to
enumeration.
