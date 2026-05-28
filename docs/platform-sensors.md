# Melody Platform Sensors — `platform.sensors`

OS-level environmental telemetry — thermal pressure, power source, low-power mode — surfaced as a subscription-driven sensor bus on the parent `platform` module, with submodules `platform.sensors.thermal` and `platform.sensors.power`. The GPU module (and frame pacing, XR, asset IO, media) consume these signals; they do not redefine them.

This module is bound by the Ten Commandments of the Engine. Where a decision turns on one, the commandment is cited by tag (`MEL-ENGINE-N`).

---

## 1. Module identity

Parent module: `platform` (already present at `modules/platform/`). Submodules, following the established `time` / `time.frequency` layout (header at `modules/time/include/time.frequency/frequency.h`):

- `platform.sensors.thermal` — `modules/platform/include/platform.sensors.thermal/`
- `platform.sensors.power`   — `modules/platform/include/platform.sensors.power/`

The `platform.sensors` namespace is a deliberate carve-out for OS-published environmental telemetry that is not a hardware device the app commands, but a condition the app observes. Future siblings — `platform.sensors.ambient_light`, `.dock_state`, `.lid`, `.fan`, `.charge_rate`, `.proximity` — are design space, not specified here. Each future submodule lands on its own merit and is not implicitly admitted by the parent's existence (MEL-ENGINE-I: embrace the hard problem when the domain demandeth, not pre-emptively).

---

## 2. Inherited principles

- **P1 — emulate-to-equivalent absent faking.** Where the OS publishes no signal, `platform.sensors` reports the tier as `unknown` / `none`, never a fabricated `nominal` / `off`. The API shape stays uniform; the carried value is honest (MEL-ENGINE-VIII).
- **Subscription, not polling.** Every signal is delivered via a callback registered against the parent `platform` instance; the engine spends zero cycles when no consumer is registered, and zero cycles between transitions even when one is (MEL-ENGINE-III: no stolen cycles).
- **Mechanism, not policy.** `platform.sensors` reports the tier the OS reports. The decision — drop render resolution, switch to `Capped(30)`, suspend non-critical compute, dim UI, defer asset prefetch — belongs to the app's content model. The engine refuses to impose a quality ladder on the user's product (MEL-ENGINE-V).
- **Orthogonal composition.** Thermal pressure, power source, and low-power mode are three independent signals carried as three independent events. A laptop on AC may sit in user-initiated Low Power Mode; a phone at 90% battery may sit in Low Power Mode the user toggled to conserve for later; thermal pressure may rise on a plugged-in desktop under sustained compute. The downstream consumer joins the three as it wishes (MEL-ENGINE-IX).
- **Honest gating.** Where no OS surface exists, the cap reports `unknown` and the callback never fires; the engine does not synthesize transitions from indirect signals (e.g. inferring thermal pressure from frame-time variance).

---

## 3. Subscription API

`platform.sensors` exposes one registration entry point per signal on the parent `platform` instance. Registration is concurrent (any thread may register), unregistration is serialized per-callback. Callbacks fire on the platform reactor (the parent's reactor at construction time) and are dispatched via the standard `mel_reactor_post` cross-thread bridge when the OS notification arrives off-reactor.

    Mel_Platform_Sensors_Subscription mel_platform_sensors_thermal_subscribe(
        Mel_Platform* platform,
        void (*on_change)(Mel_Platform_Thermal_Pressure tier, void* user),
        void* user);

    Mel_Platform_Sensors_Subscription mel_platform_sensors_power_source_subscribe(
        Mel_Platform* platform,
        void (*on_change)(Mel_Platform_Power_Source source, void* user),
        void* user);

    Mel_Platform_Sensors_Subscription mel_platform_sensors_low_power_subscribe(
        Mel_Platform* platform,
        void (*on_change)(Mel_Platform_Low_Power_Mode mode, void* user),
        void* user);

    void mel_platform_sensors_unsubscribe(Mel_Platform_Sensors_Subscription);

The subscription handle is a value type wrapping `Mel_SlotMap_Handle` (same idiom as GPU resource handles), so use-after-unsubscribe is a loud `alive()` failure rather than a dangling crash (MEL-ENGINE-VIII).

**Initial-state delivery.** Every subscription receives a synchronous initial callback at registration time with the current tier — the consumer never has to bracket "register, then poll current state." `unknown` is a valid initial value where the OS has not yet reported, and the callback fires again when the first real value arrives.

**Current-state read.** Pure-read accessors — `mel_platform_sensors_thermal_current(platform)`, `mel_platform_sensors_power_source_current(platform)`, `mel_platform_sensors_low_power_current(platform)` — return the last observed tier without registering. Useful for one-shot decisions (asset prefetch at launch, codec selection) that do not need ongoing notification.

**Capability inspection.** `mel_platform_sensors_caps(platform) → { thermal_present, power_source_present, low_power_present }` reports, per signal, whether the running OS publishes it. A consumer that needs to branch on availability rather than absorb `unknown` reads this once at startup.

---

## 4. `platform.sensors.thermal`

### 4.1 Tier enum

    Mel_Platform_Thermal_Pressure ∈ { unknown, nominal, fair, serious, critical }

`unknown` is the honest absence; on platforms with no thermal-pressure surface (vanilla desktop Linux without a vendor daemon, browser WebGPU on most builds, headless servers without IPMI exposure) the consumer sees `unknown` from registration until either a vendor path is wired up or the consumer accepts the absence. `nominal | fair | serious | critical` align with `NSProcessInfoThermalState` (`Nominal | Fair | Serious | Critical`) and Android's `PowerManager.THERMAL_STATUS_*` (`NONE | LIGHT | MODERATE | SEVERE | CRITICAL | EMERGENCY | SHUTDOWN`; coalesced — `LIGHT` → `fair`, `MODERATE` → `serious`, `SEVERE | CRITICAL | EMERGENCY | SHUTDOWN` → `critical`). The coalesce loses Android's extra resolution intentionally: a portable consumer cannot meaningfully discriminate between Android's `SEVERE` and `EMERGENCY` because no other platform exposes the distinction (MEL-ENGINE-IV: constrain conventions, never capabilities — the underlying Android tier remains reachable through `platform/<backend>/` interop where the consumer specifically needs it).

### 4.2 Event payload

The `on_change` callback receives the new tier; no rate-of-change, no temperature reading. Thermometers are not portable; tiers are. A consumer that needs absolute temperature reaches through the `platform/<backend>/` escape hatch (MEL-ENGINE-II: hide complexity, not power).

### 4.3 Where the signal is gated

A consumer that needs to know whether `nominal` means "the OS observed nominal thermal pressure" or "the OS publishes nothing and the engine reported a default" reads `mel_platform_sensors_caps(platform).thermal_present` ∵ `unknown` is reported as the initial state on no-signal platforms — `nominal` is never fabricated.

---

## 5. `platform.sensors.power`

Two independent signals carried in one submodule because both originate from the OS power subsystem and frequently fire together.

### 5.1 Power source

    Mel_Platform_Power_Source ∈ { unknown, ac, battery }

`unknown` covers headless servers, browser builds where the user denied the Battery Status permission, and desktops with no battery (`ac` is *not* synthesized for a desktop without battery hardware — the platform reports what the OS reports; the absence of a battery is itself reported through `mel_platform_sensors_caps`).

### 5.2 Low-power mode

    Mel_Platform_Low_Power_Mode ∈ { unknown, off, on }

The OS-level battery-saver / Low Power Mode toggle. Distinct from power source ∵ the user can request a battery-saver profile on AC (quiet, cool) and can be on battery without battery-saver engaged. The two events are dispatched separately and the consumer joins them as needed.

### 5.3 Charge level

Deliberately absent. A percentage reading is policy-relevant only when the app already knows the discharge rate, and exposing percentage tempts the consumer into ad-hoc thresholding ("under 20%, reduce quality") that the OS-published low-power flag already encodes more honestly. If a future product surface needs charge level (in-game battery indicator overlay, dev-tools telemetry, kiosk power dashboard), it lands as a separate `.charge_level` submodule with explicit consent and OS-permission semantics — design space, not specified here.

---

## 6. OS lowerings

Per submodule, per platform. Where multiple platform APIs apply, the engine prefers the notification-driven path; the polling path is the fallback only when no notification is available and a low-frequency wake-on-tick (≥ 5 s) is acceptable.

### 6.1 `platform.sensors.thermal`

- **iOS / iPadOS** — `NSProcessInfo.thermalState` plus `NSProcessInfoThermalStateDidChangeNotification` on `NSNotificationCenter`. Notification-driven; no polling.
- **macOS** — `NSProcessInfo.thermalState` + `NSProcessInfoThermalStateDidChangeNotification`. Available since macOS 10.10.3, reliable on Apple Silicon. On Intel Macs the value transitions less aggressively but the notification still fires.
- **Android** — `PowerManager.getCurrentThermalStatus()` for the initial read, `PowerManager.OnThermalStatusChangedListener` (API 29+) for the notification path. Below API 29, the Thermal HAL is consulted only where the device exposes it; otherwise the signal gates to `thermal_present = false` and the cap reports unknown.
- **Windows** — no first-class equivalent. The engine inspects `IOCTL_THERMAL_QUERY_INFO` / WMI `MSAcpi_ThermalZoneTemperature` only when an opt-in flag is set at platform construction (the WMI poll has nontrivial cost); without opt-in, `thermal_present = false`. The engine **does not** infer thermal pressure from CPU frequency throttling (MEL-ENGINE-VIII: do not synthesize what you do not measure).
- **Linux** — `/sys/class/thermal/thermal_zone*/temp` paired with the device's `trip_point_*_temp` thresholds to derive a tier. Polled at low frequency (default 2 s) via a reactor timer; no notification surface exists in the kernel ABI as of writing. Reported as `thermal_present = true` only when at least one zone with a usable trip-point set is discovered; otherwise `unknown`.
- **Web** — the Compute Pressure API (`PressureObserver`, Chrome 125+) for `"thermals"` source where granted by the browser permission model; otherwise `thermal_present = false`. The browser may rate-limit or coalesce transitions, and the API is partially deployed across browsers as of 2026-05 — the engine reports the granted source honestly rather than synthesizing on the experimental fallback (MEL-ENGINE-VIII).

### 6.2 `platform.sensors.power` — power source

- **iOS / iPadOS / macOS** — `IOPSCopyPowerSourcesInfo` + `IOPSNotificationCreateRunLoopSource`. Notification-driven.
- **Android** — `BatteryManager` + the `ACTION_POWER_CONNECTED` / `ACTION_POWER_DISCONNECTED` broadcast receivers.
- **Windows** — `GetSystemPowerStatus` for the initial read, `RegisterPowerSettingNotification(GUID_ACDC_POWER_SOURCE)` for the notification.
- **Linux** — `org.freedesktop.UPower` D-Bus interface (`UPower` daemon), specifically the `OnBattery` property and the `Changed` signal. Where `upowerd` is absent, fall back to `/sys/class/power_supply/AC*/online`; if neither is present, `power_source_present = false`.
- **Web** — `navigator.getBattery()` and the `chargingchange` event on the resolved `BatteryManager`. Note that Safari does not implement the Battery Status API and Firefox restricts it to privileged contexts; the cap reports the granted state per browser.

### 6.3 `platform.sensors.power` — low-power mode

- **iOS / iPadOS / macOS** — `NSProcessInfo.isLowPowerModeEnabled` + `NSProcessInfoPowerStateDidChangeNotification`.
- **Android** — `PowerManager.isPowerSaveMode()` + the `ACTION_POWER_SAVE_MODE_CHANGED` broadcast.
- **Windows** — `SYSTEM_POWER_STATUS.SystemStatusFlag` bit `1` (Battery Saver active, Windows 10 1709+). Polled at moderate frequency (default 5 s) ∵ no first-class notification surface for Battery Saver exists in Win32; `PowerRegisterSuspendResumeNotification` covers suspend, not battery-saver toggles. Where the Windows Runtime is available, `EnergySaverStatus` provides an event; the engine prefers that lowering when the WinRT runtime is in-process.
- **Linux** — `org.freedesktop.UPower.PowerProfiles` (`power-profiles-daemon`) `ActiveProfile` property + `PropertiesChanged` signal — `power-saver` maps to `on`, `balanced` / `performance` map to `off`. Where the daemon is absent (older distributions, headless), `low_power_present = false`.
- **Web** — no first-class API. Some browsers expose `navigator.connection.saveData` for data-saver, which is **not** semantically equivalent to OS low-power mode; the engine does not conflate them. Reported as `low_power_present = false` until a standardized surface exists.

---

## 7. Downstream consumers

`platform.sensors` has no upstream module dependency beyond the parent `platform` reactor and the standard slotmap / status / future primitives shared across the engine. Downstream consumers:

- **`gpu`** — re-exports the three signals through `caps.power.*` as a read-only view of `platform.sensors` state. `caps.power.thermal_pressure`, `caps.power.power_source`, `caps.power.low_power_mode` are no longer independently lowered by the GPU module; they read from `platform.sensors` at query time and the GPU `Mel_Gpu_Instance` re-emits the three events upward to GPU-registered callbacks for source-aligned ergonomics (an app already on the GPU module need not also hold a `platform` handle just to receive the events). The GPU re-export is documentary, not authoritative — the OS lowering lives here. The GPU `adapter_removed` event stays GPU-owned ∵ it is about a GPU adapter being unplugged, not an environmental tier (MEL-ENGINE-IX: parts compose, but each part owns its own concern).
- **`frame.pacing`** — the most direct consumer. The pacing layer (§7.5 in gpu-rhi.md) reads `platform.sensors` once per registered render source and threads `thermal_pressure`, `power_source`, `low_power_mode` into the `Frame_Info` passed to the user's render callback. The pacing layer itself takes no action on the values; it carries them so the app's quality system has them in the same context that already holds previous-GPU-time and predicted-next-vsync.
- **`xr`** — XR runtimes (visionOS, Quest, SteamVR) publish their own thermal-budget surface (`xrPerfSettings*`, `XR_FB_performance_metrics`); the XR module **uses both**: the XR-runtime budget is authoritative for headset-internal thermal state, and `platform.sensors.thermal` is the host-side complement. The XR app receives both and decides; the engine does not silently fold them together (MEL-ENGINE-VIII).
- **`io.asset`** — asset prefetch consults `platform.sensors.power.low_power_mode` and the power-source signal at queue-fill time to throttle background streaming on battery + low-power; mechanism only — the streaming budget belongs to the app's content model.
- **`media.video`** — encoder rate-control consults thermal and low-power to pick a sustainable bitrate / preset; again mechanism only.

The downstream list is exhaustive at spec time; further consumers (render-graph adaptive quality, render-reconstruction upscaler selection) wire up symmetrically when they land.

---

## 8. Honest gating summary

A consumer that wants one place to learn what the running platform can publish:

- `mel_platform_sensors_caps(platform).thermal_present` — false on Windows without WMI opt-in, false on desktop Linux without thermal-zone discovery, false on browser builds without Compute Pressure permission, false on headless server builds.
- `mel_platform_sensors_caps(platform).power_source_present` — false on headless server builds without battery hardware, false on Safari, false where the user denied Battery Status.
- `mel_platform_sensors_caps(platform).low_power_present` — false on every web build (no standardized surface), false on Linux without `power-profiles-daemon`, false on Windows pre-1709.

In every "false" case, subscription still succeeds, the initial callback fires with the corresponding `unknown` tier, and the callback never fires again until the OS surface either appears (hot-plug of a battery on a desktop, daemon start on Linux) or remains silent for the platform's lifetime. The engine never fakes `nominal` to satisfy a curious consumer (MEL-ENGINE-VIII: fail with honor — and silence with honor).
