# Melody Sensor — `sensor`

OS-level environmental telemetry — thermal pressure, power source, low-power mode — surfaced as a standalone top-level module with submodules `sensor.thermal` and `sensor.power`. The GPU module (and frame pacing, XR, asset IO, media) consume these signals; they do not redefine them.

This module is bound by the Ten Commandments of the Engine. Where a decision turns on one, the commandment is cited by tag (`MEL-ENGINE-N`).

---

## 1. Module identity

`sensor` is its own top-level module at `modules/sensor/`. It is **not** a child of any `platform` module, and there is no runtime platform object: the platform is a build axis resolved at compile time (one executable, one platform), so the active OS lowering is selected by the build's source-directory gating, not dispatched through a runtime handle.

Submodules, following the established `time` / `time.frequency` layout (header at `modules/time/include/time.frequency/frequency.h`):

- `sensor.thermal` — `modules/sensor/include/sensor.thermal/`
- `sensor.power`   — `modules/sensor/include/sensor.power/`

The parent-level `sensor` namespace (`modules/sensor/include/sensor/`) carries the cross-signal capability surface (`caps.h`) and the umbrella header (`sensor.h`).

The `sensor` namespace is a deliberate carve-out for OS-published environmental telemetry that is not a hardware device the app commands, but a condition the app observes. Future siblings — `sensor.ambient_light`, `.dock_state`, `.lid`, `.fan`, `.charge_rate`, `.proximity` — are design space, not specified here. Each future submodule lands on its own merit and is not implicitly admitted by the parent's existence (MEL-ENGINE-I: embrace the hard problem when the domain demandeth, not pre-emptively).

---

## 2. Inherited principles

- **P1 — emulate-to-equivalent absent faking.** Where the OS publishes no signal, `sensor` reports the tier as `unknown` / `none`, never a fabricated `nominal` / `off`. The API shape stays uniform; the carried value is honest (MEL-ENGINE-VIII).
- **Pull, not push.** Every signal is read on demand through a synchronous accessor; the engine spends zero cycles when the app does not ask (MEL-ENGINE-III: no stolen cycles). Push-style change notification is deferred — see §6.
- **Mechanism, not policy.** `sensor` reports the tier the OS reports. The decision — drop render resolution, switch to `Capped(30)`, suspend non-critical compute, dim UI, defer asset prefetch — belongs to the app's content model. The engine refuses to impose a quality ladder on the user's product (MEL-ENGINE-V).
- **Orthogonal composition.** Thermal pressure, power source, and low-power mode are three independent signals carried as three independent reads. A laptop on AC may sit in user-initiated Low Power Mode; a phone at 90% battery may sit in Low Power Mode the user toggled to conserve for later; thermal pressure may rise on a plugged-in desktop under sustained compute. The downstream consumer joins the three as it wishes (MEL-ENGINE-IX).
- **Honest gating.** Where no OS surface exists, the read reports `unknown` and the cap reports absent; the engine does not synthesize a value from indirect signals (e.g. inferring thermal pressure from frame-time variance).

---

## 3. Read API

The module is stateless and global: every accessor queries the OS surface for the active build platform and returns the current tier. There is no instance to construct, no reactor to bind, and no registration. A call costs nothing until the app makes it.

    Mel_Sensor_Thermal_Pressure mel_sensor_thermal_current(void);
    Mel_Sensor_Temperature      mel_sensor_thermal_temperature(Mel_Sensor_Temp_Domain domain);
    Mel_Sensor_Power_Source     mel_sensor_power_source_current(void);
    Mel_Sensor_Low_Power_Mode   mel_sensor_low_power_current(void);

    Mel_Sensor_Caps             mel_sensor_caps(void);

**Honest absence.** `unknown` is a valid return where the OS has not reported or publishes nothing on this platform. The accessor never fabricates `nominal` / `ac` / `off` to satisfy a curious caller (MEL-ENGINE-VIII).

**Capability inspection.** `mel_sensor_caps()` returns `{ thermal_present, power_source_present, low_power_present }`, reporting per signal whether the running build's platform lowering publishes it. A consumer that needs to branch on availability rather than absorb `unknown` reads this once at startup. The bools report platform-surface availability; a runtime `unknown` read on a present surface (e.g. the OS has not yet sampled) is still honest.

---

## 4. `sensor.thermal`

### 4.1 Tier enum

    Mel_Sensor_Thermal_Pressure ∈ { unknown, nominal, fair, serious, critical }

`unknown` is the honest absence; on platforms with no thermal-pressure surface (vanilla desktop Linux without a vendor daemon, browser WebGPU on most builds, headless servers without IPMI exposure) the consumer sees `unknown` until a vendor path is wired up or the consumer accepts the absence. `nominal | fair | serious | critical` align with `NSProcessInfoThermalState` (`Nominal | Fair | Serious | Critical`) and Android's `PowerManager.THERMAL_STATUS_*` (`NONE | LIGHT | MODERATE | SEVERE | CRITICAL | EMERGENCY | SHUTDOWN`; coalesced — `LIGHT` → `fair`, `MODERATE` → `serious`, `SEVERE | CRITICAL | EMERGENCY | SHUTDOWN` → `critical`). The coalesce loses Android's extra resolution intentionally: a portable consumer cannot meaningfully discriminate between Android's `SEVERE` and `EMERGENCY` because no other platform exposes the distinction (MEL-ENGINE-IV: constrain conventions, never capabilities — the underlying Android tier remains reachable through a platform-specific interop path where the consumer specifically needs it).

### 4.2 Temperature

The tier (§4.1) is the portable coarse signal; **temperature** is the accurate one, exposed directly rather than hidden behind an escape hatch (MEL-ENGINE-II: hide complexity, not power — a number the hardware measures is power the user should reach).

    Mel_Sensor_Temp_Domain ∈ { primary, cpu, gpu, ambient }
    Mel_Sensor_Temp_Fidelity ∈ { none, derived, measured }

    typedef struct { f32 celsius; Mel_Sensor_Temp_Fidelity fidelity; } Mel_Sensor_Temperature;

    Mel_Sensor_Temperature mel_sensor_thermal_temperature(Mel_Sensor_Temp_Domain domain);

`primary` is the platform's most representative figure (the SoC/CPU package). `cpu` / `gpu` / `ambient` are the idiomatic domains a rich platform exposes; `primary` aliases `cpu` where no distinct package sensor exists.

**Gentle degradation (MEL-ENGINE-VII: age forward, degrade gracefully).** The fidelity is the honest signal of how much to trust `celsius`:

- `measured` — a direct hardware-sensor reading (the mean of the platform's die sensors for that domain). `celsius` is trustworthy.
- `derived` — a coarse or approximate figure (an ambient estimate, or the hottest available zone standing in for a domain with no dedicated sensor). `celsius` is indicative, not precise.
- `none` — the platform exposes no usable sensor for that domain. `celsius` is `0` and must be ignored; the consumer falls back to the tier (§4.1).

The engine never fabricates a temperature: a platform with no sensor returns `none`, not a plausible-looking number (MEL-ENGINE-VIII). A consumer reads `fidelity` once, decides whether the platform is accurate enough for its purpose, and degrades to the tier where it is not.

`mel_sensor_caps().temperature` reports the best fidelity the `primary` domain achieves on this build, for a one-shot startup branch.

### 4.3 Where the signal is gated

A consumer that needs to know whether `nominal` means "the OS observed nominal thermal pressure" or "the OS publishes nothing and the engine reported a default" reads `mel_sensor_caps().thermal_present` ∵ `unknown` is what a no-signal platform returns — `nominal` is never fabricated.

---

## 5. `sensor.power`

Two independent signals carried in one submodule because both originate from the OS power subsystem.

### 5.1 Power source

    Mel_Sensor_Power_Source ∈ { unknown, ac, battery }

`unknown` covers headless servers, browser builds where the user denied the Battery Status permission, and platforms where no lowering is wired. The platform reports what the OS reports; the absence of a battery is itself reported through `mel_sensor_caps`.

### 5.2 Low-power mode

    Mel_Sensor_Low_Power_Mode ∈ { unknown, off, on }

The OS-level battery-saver / Low Power Mode toggle. Distinct from power source ∵ the user can request a battery-saver profile on AC (quiet, cool) and can be on battery without battery-saver engaged. The two reads are independent and the consumer joins them as needed.

### 5.3 Charge level

Deliberately absent. A percentage reading is policy-relevant only when the app already knows the discharge rate, and exposing percentage tempts the consumer into ad-hoc thresholding ("under 20%, reduce quality") that the OS-published low-power flag already encodes more honestly. If a future product surface needs charge level (in-game battery indicator overlay, dev-tools telemetry, kiosk power dashboard), it lands as a separate `sensor.charge_level` submodule with explicit consent and OS-permission semantics — design space, not specified here.

---

## 6. Deferred: change notification

Push-style notification — a callback that fires when a tier transitions — is **deferred**, not refused (MEL-ENGINE-I: defer out of strategy is wisdom). The current module is pull-only: the consumer samples at a cadence it owns (e.g. the frame-pacing tick already reads platform state once per registered render source — §8).

When notification lands it will **not** be reactor-coupled: the sensor module has no reactor dependency, and the eventual delivery mechanism is a plain callback contract whose threading and coalescing semantics are specified at that time. The reactor-dispatched subscription bus described in earlier drafts of this spec is withdrawn. The OS notification surfaces that such a path would ride are catalogued per platform in §7 so the work is resumable.

---

## 7. OS lowerings

Per submodule, per platform. Each lowering's **initial-read** path is what the current pull-only API uses; the **notification** surface is recorded for the deferred §6 work. Implementations are selected by source-directory gating (`src/<platform>/` and the platform family chain), so each platform compiles exactly one definition of each accessor.

### 7.1 `sensor.thermal` — pressure tier

- **iOS / iPadOS** — read: `NSProcessInfo.thermalState`. Notification (deferred): `NSProcessInfoThermalStateDidChangeNotification` on `NSNotificationCenter`.
- **macOS** — read: `NSProcessInfo.thermalState`. Notification (deferred): `NSProcessInfoThermalStateDidChangeNotification`. Available since macOS 10.10.3, reliable on Apple Silicon; on Intel Macs the value transitions less aggressively.
- **Android** — read: `PowerManager.getCurrentThermalStatus()`. Notification (deferred): `PowerManager.OnThermalStatusChangedListener` (API 29+). Below API 29 the Thermal HAL is consulted only where the device exposes it; otherwise `thermal_present = false`.
- **Windows** — no first-class equivalent. The engine inspects `IOCTL_THERMAL_QUERY_INFO` / WMI `MSAcpi_ThermalZoneTemperature` only when an opt-in flag is set at build/startup (the WMI poll has nontrivial cost); without opt-in, `thermal_present = false`. The engine **does not** infer thermal pressure from CPU frequency throttling (MEL-ENGINE-VIII: do not synthesize what you do not measure).
- **Linux** — `/sys/class/thermal/thermal_zone*/temp` paired with the device's `trip_point_*_temp` thresholds to derive a tier. Read-only; no kernel-ABI notification surface exists as of writing (the deferred path would low-frequency poll). Reported as `thermal_present = true` only when at least one zone with a usable trip-point set is discovered; otherwise `unknown`.
- **Web** — the Compute Pressure API (`PressureObserver`, Chrome 125+) for `"thermals"` source where granted by the browser permission model; otherwise `thermal_present = false`. The API is partially deployed across browsers as of 2026-05 and is observer-shaped (no synchronous current-value read), so it is part of the deferred §6 work; until then web reports `unknown`.

### 7.1b `sensor.thermal` — temperature

- **macOS** — `measured`, via the Apple SMC (`AppleSMC` IOKit service, `IOConnectCallStructMethod`). No entitlement or root required. Keys are enumerated once (the `#KEY` count, then `READ_INDEX`) and classified by family: `Tp*`/`Te*` (CPU P-/E-cores) → `cpu` and `primary`, `Tg*` (GPU) → `gpu`, `TA*`/`Ta*` → `ambient` (reported `derived`). Each domain is the mean of its plausibly-ranged (`0 < t < 150 °C`) `flt ` sensors; `sp78` is decoded too for Intel Macs. This families-and-mean approach is chip-agnostic across M-series rather than depending on fragile per-core key tables. The connection and the discovered key set are memoized so a read does not re-enumerate.
- **iOS / iPadOS** — `none`. The SMC is not reachable from the iOS sandbox; no public per-domain temperature API exists. Falls back to the tier.
- **Linux** — `measured` / `derived`, via `/sys/class/thermal/thermal_zone*/{type,temp}` (millidegrees). A zone whose `type` matches `cpu`/`pkg`/`soc`/`coretemp` answers `cpu`/`primary` (`measured`); a `gpu`-typed zone answers `gpu`; with no typed match, the hottest zone answers `primary` as `derived`. Ambient is `none`.
- **Windows** — `none` by default. Accurate die temperature requires the WMI `MSAcpi_ThermalZoneTemperature` opt-in (nontrivial poll cost, and frequently only motherboard-zone accuracy); wired behind the same opt-in as the thermal tier.
- **Android** — `derived`, via the thermal-zone read (`/sys/class/thermal`), the only die-temperature path a non-privileged app has. The official `HardwarePropertiesManager.getDeviceTemperatures(DEVICE_TEMPERATURE_CPU/GPU/...)` (API 24+) is `measured` but restricted to device/profile-owner apps, so it is not the default path.
- **Web** — `none`. Browsers expose no temperature API (the Compute Pressure API yields a pressure state, not degrees).

### 7.2 `sensor.power` — power source

- **iOS / iPadOS / macOS** — read: `IOPSCopyPowerSourcesInfo` + `IOPSGetProvidingPowerSourceType`. Notification (deferred): `IOPSNotificationCreateRunLoopSource`.
- **Android** — read: the sticky `ACTION_BATTERY_CHANGED` intent (`registerReceiver(null, …)`), `plugged` extra `> 0` → `ac`, `== 0` → `battery`. Notification (deferred): `ACTION_POWER_CONNECTED` / `ACTION_POWER_DISCONNECTED` broadcast receivers.
- **Windows** — read: `GetSystemPowerStatus`. Notification (deferred): `RegisterPowerSettingNotification(GUID_ACDC_POWER_SOURCE)`.
- **Linux** — `org.freedesktop.UPower` D-Bus interface (`OnBattery` property; `Changed` signal for the deferred path). Where `upowerd` is absent, fall back to `/sys/class/power_supply/AC*/online`; if neither is present, `power_source_present = false`.
- **Web** — `navigator.getBattery()` (the resolved `BatteryManager`; `chargingchange` for the deferred path). Safari does not implement the Battery Status API and Firefox restricts it; the cap reports the granted state per browser. The API is promise-shaped, so it is part of the deferred §6 work; until then web reports `unknown`.

### 7.3 `sensor.power` — low-power mode

- **iOS / iPadOS / macOS** — read: `NSProcessInfo.isLowPowerModeEnabled` (guarded by `respondsToSelector:` — macOS 12+). Notification (deferred): `NSProcessInfoPowerStateDidChangeNotification`.
- **Android** — read: `PowerManager.isPowerSaveMode()`. Notification (deferred): `ACTION_POWER_SAVE_MODE_CHANGED` broadcast.
- **Windows** — `SYSTEM_POWER_STATUS.SystemStatusFlag` bit `1` (Battery Saver active, Windows 10 1709+). No first-class notification surface in Win32; where the Windows Runtime is in-process, `EnergySaverStatus` provides an event (deferred path).
- **Linux** — read: `/sys/firmware/acpi/platform_profile` (the kernel platform-profile interface, which `power-profiles-daemon` also drives) — `low-power` / `quiet` map to `on`, `balanced` / `performance` map to `off`. Where the file is absent, `low_power_present = false`. The D-Bus `org.freedesktop.UPower.PowerProfiles` path is the notification surface for the deferred work.
- **Web** — no first-class API. `navigator.connection.saveData` is data-saver, **not** OS low-power mode; the engine does not conflate them. Reported as `low_power_present = false` until a standardized surface exists.

---

## 8. Downstream consumers

`sensor` has no upstream module dependency beyond `core` (types/compiler) shared across the engine. Downstream consumers:

- **`gpu`** — re-exports the three signals through `caps.power.*` as a read-only view of `sensor` state. `caps.power.thermal_pressure`, `caps.power.power_source`, `caps.power.low_power_mode` are no longer independently lowered by the GPU module; they read from `sensor` at query time. The GPU re-export is documentary, not authoritative — the OS lowering lives here. The GPU `adapter_removed` event stays GPU-owned ∵ it is about a GPU adapter being unplugged, not an environmental tier (MEL-ENGINE-IX: parts compose, but each part owns its own concern).
- **`frame.pacing`** — the most direct consumer. The pacing layer (§7.5 in `gpu-rhi.md`) reads `sensor` once per registered render source and threads `thermal_pressure`, `power_source`, `low_power_mode` into the `Frame_Info` passed to the user's render callback. The pacing layer itself takes no action on the values; it carries them so the app's quality system has them in the same context that already holds previous-GPU-time and predicted-next-vsync.
- **`xr`** — XR runtimes (visionOS, Quest, SteamVR) publish their own thermal-budget surface (`xrPerfSettings*`, `XR_FB_performance_metrics`); the XR module **uses both**: the XR-runtime budget is authoritative for headset-internal thermal state, and `sensor.thermal` is the host-side complement. The XR app receives both and decides; the engine does not silently fold them together (MEL-ENGINE-VIII).
- **`io.asset`** — asset prefetch consults `sensor.power` low-power mode and the power-source signal at queue-fill time to throttle background streaming on battery + low-power; mechanism only — the streaming budget belongs to the app's content model.
- **`media.video`** — encoder rate-control consults thermal and low-power to pick a sustainable bitrate / preset; again mechanism only.

The downstream list is exhaustive at spec time; further consumers (render-graph adaptive quality, render-reconstruction upscaler selection) wire up symmetrically when they land.

---

## 9. Honest gating summary

A consumer that wants one place to learn what the running platform can publish:

- `mel_sensor_caps().thermal_present` — false on Windows without WMI opt-in, false on desktop Linux without thermal-zone discovery, false on browser builds without Compute Pressure, false on headless server builds.
- `mel_sensor_caps().power_source_present` — false on headless server builds without battery hardware, false on Safari, false where the user denied Battery Status.
- `mel_sensor_caps().low_power_present` — false on every web build (no standardized surface), false on Linux without `power-profiles-daemon`, false on Windows pre-1709.
- `mel_sensor_caps().temperature` — `measured` on macOS (SMC) and Linux with a typed CPU zone; `derived` on Linux with only untyped zones; `none` on iOS, Web, Windows without the WMI opt-in, and Android without the privileged HAL path.

In every "false" case the corresponding accessor returns the `unknown` tier; the engine never fakes `nominal` to satisfy a curious consumer (MEL-ENGINE-VIII: fail with honor — and silence with honor).

---

## 10. Implementation status

The build auto-discovers `modules/sensor/`; sources are gated by `src/<platform>/`. Current state — full detail and the resume checklist live in `todo.md`:

Every platform has a real lowering — none is a stub. Verification differs by what this session's host could exercise:

- **macOS** — built, **run, and observed** (Apple M3 Pro, macOS 26.2): thermal tier (`NSProcessInfo.thermalState`), low-power (`NSProcessInfo.isLowPowerModeEnabled`), power source (IOKit `IOPSCopyPowerSourcesInfo`), `measured` per-domain temperature via the SMC (CPU ≈ 45 °C, GPU ≈ 37 °C, ambient `derived`). Needs the IOKit framework link (`modules/build.c`).
- **Windows** — power source (`GetSystemPowerStatus.ACLineStatus`) and low-power (`SystemStatusFlag` bit 0). **Compiles against the Windows headers** (mingw, this host); not run. Thermal tier/temperature stay absent (WMI opt-in, §7).
- **Linux** — thermal tier (thermal-zone trip points), temperature (`/sys/class/thermal`), power source (`/sys/class/power_supply`), low-power (`/sys/firmware/acpi/platform_profile`). **Clean under `-Wall -Wextra`**; not run (no Linux host this session).
- **iOS** — thermal tier + low-power from the shared Apple sources; power source via `UIDevice.batteryState`. **Compiles against the iphoneos SDK**; not run. Temperature `none` (no SMC in the sandbox).
- **Android** — thermal tier (`PowerManager.getCurrentThermalStatus`), low-power (`PowerManager.isPowerSaveMode`), power source (`BATTERY_CHANGED` sticky-intent `plugged` extra), all via JNI through `ActivityThread.currentApplication()`; temperature via thermal-zone read (`derived`, since `HardwarePropertiesManager` is privileged). **JNI TU structurally checked against `jni.h`**; not run on a device.
- **Web** — no *synchronous* power/thermal/temperature API exists in browsers (Compute Pressure and `getBattery()` are observer/promise-shaped). Honestly reports `unknown` / `none` until the deferred event work (§6) lands; that is a platform constraint, not a stub.
