# Melody Power — `power`

OS-level power telemetry — power source and low-power mode — surfaced as a
standalone top-level module. Extracted from the former `sensor` module (whose
thermal half is now the sibling `thermal` module).

This module is bound by the Ten Commandments of the Engine. Where a decision
turns on one, the commandment is cited by tag (`MEL-ENGINE-N`).

---

## 1. Module identity

`power` is its own top-level module at `modules/power/`. It is **not** a child of
any `platform` module, and there is no runtime platform object: the platform is a
build axis resolved at compile time (one executable, one platform), so the active
OS lowering is selected by the build's source-directory gating, not dispatched
through a runtime handle.

`power` reports a condition the app observes — OS-published power state — not a
hardware device the app commands.

---

## 2. Inherited principles

- **Emulate-to-equivalent absent faking.** Where the OS publishes no signal,
  `power` reports `unknown`, never a fabricated `ac` / `off`. The API shape stays
  uniform; the carried value is honest (MEL-ENGINE-VIII).
- **Pull, not push.** Every signal is read on demand through a synchronous
  accessor; the engine spends zero cycles when the app does not ask
  (MEL-ENGINE-III). Push-style change notification is deferred — see §5.
- **Mechanism, not policy.** `power` reports what the OS reports. The decision —
  throttle background streaming, dim UI, defer asset prefetch — belongs to the
  app's content model (MEL-ENGINE-V).
- **Orthogonal composition.** Power source and low-power mode are two independent
  reads. The downstream consumer joins them as it wishes (MEL-ENGINE-IX).

---

## 3. Read API

The module is stateless and global: every accessor queries the OS surface for the
active build platform and returns the current value. There is no instance to
construct, no reactor to bind, and no registration. A call costs nothing until the
app makes it.

    Mel_Power_Source         mel_power_source_current(void);
    Mel_Power_Low_Power_Mode mel_power_low_power_current(void);
    Mel_Power_Caps           mel_power_caps(void);

**Capability inspection.** `mel_power_caps()` returns
`{ power_source_present, low_power_present }`, reporting per signal whether the
running build's platform lowering publishes it. A consumer that needs to branch on
availability rather than absorb `unknown` reads this once at startup. The absence
of a battery is reported through `power_source_present`; a runtime `unknown` read
on a present surface is still honest.

---

## 4. Signals

### 4.1 Power source

    Mel_Power_Source ∈ { unknown, ac, battery }

`unknown` covers headless servers, browser builds where the user denied the
Battery Status permission, and platforms where no lowering is wired. The platform
reports what the OS reports; the absence of a battery is surfaced through
`mel_power_caps`.

### 4.2 Low-power mode

    Mel_Power_Low_Power_Mode ∈ { unknown, off, on }

The OS-level battery-saver / Low Power Mode toggle. Distinct from power source ∵
the user can request a battery-saver profile on AC (quiet, cool) and can be on
battery without battery-saver engaged. The two reads are independent and the
consumer joins them as needed.

### 4.3 Charge level

Deliberately absent. A percentage reading is policy-relevant only when the app
already knows the discharge rate, and exposing percentage tempts the consumer into
ad-hoc thresholding ("under 20 %, reduce quality") that the OS-published
low-power flag already encodes more honestly. If a future product surface needs
charge level (in-game battery indicator, dev-tools telemetry, kiosk power
dashboard), it lands as a separate submodule with explicit consent and
OS-permission semantics — design space, not specified here.

---

## 5. Deferred: change notification

Push-style notification — a callback that fires on a transition — is **deferred**,
not refused (MEL-ENGINE-I). The current module is pull-only. When notification
lands it will **not** be reactor-coupled: `power` has no reactor dependency, and
the delivery mechanism is a plain callback contract whose threading and coalescing
semantics are specified at that time. The per-platform notification surfaces are
catalogued in §6 so the work is resumable.

---

## 6. OS lowerings

Selected by source-directory gating (`src/<platform>/` and the platform family
chain), so each platform compiles exactly one definition of each accessor.

### 6.1 Power source

- **iOS / iPadOS / macOS** — macOS read: `IOPSCopyPowerSourcesInfo` +
  `IOPSGetProvidingPowerSourceType`. iOS read: `UIDevice.batteryState`
  (charging/full → `ac`, unplugged → `battery`). Notification (deferred):
  `IOPSNotificationCreateRunLoopSource`.
- **Android** — read: the sticky `ACTION_BATTERY_CHANGED` intent
  (`registerReceiver(null, …)`), `plugged` extra `> 0` → `ac`, `== 0` →
  `battery`. Notification (deferred): `ACTION_POWER_CONNECTED` /
  `ACTION_POWER_DISCONNECTED` receivers.
- **Windows** — read: `GetSystemPowerStatus.ACLineStatus`. Notification (deferred):
  `RegisterPowerSettingNotification(GUID_ACDC_POWER_SOURCE)`.
- **Linux** — `/sys/class/power_supply/*/{type,online}` (a `Mains`/`USB` zone with
  `online == 1` → `ac`; else a `Battery` zone → `battery`). The
  `org.freedesktop.UPower` D-Bus `OnBattery` property + `Changed` signal is the
  richer/notification path for the deferred work. `power_source_present = false`
  where no power-supply entries exist.
- **Web** — `navigator.getBattery()` (the resolved `BatteryManager`). Safari does
  not implement the Battery Status API and Firefox restricts it; the API is
  promise-shaped, so it is part of the deferred §5 work. Until then web reports
  `unknown`.

### 6.2 Low-power mode

- **iOS / iPadOS / macOS** — read: `NSProcessInfo.isLowPowerModeEnabled` (guarded
  by `respondsToSelector:` — macOS 12+). Notification (deferred):
  `NSProcessInfoPowerStateDidChangeNotification`.
- **Android** — read: `PowerManager.isPowerSaveMode()`. Notification (deferred):
  `ACTION_POWER_SAVE_MODE_CHANGED` broadcast.
- **Windows** — `SYSTEM_POWER_STATUS.SystemStatusFlag` bit `1` (Battery Saver,
  Windows 10 1709+). No first-class Win32 notification; where the Windows Runtime
  is in-process, `EnergySaverStatus` provides an event (deferred path).
- **Linux** — read: `/sys/firmware/acpi/platform_profile` (`low-power` / `quiet` →
  `on`; `balanced` / `performance` → `off`). `low_power_present = false` where the
  file is absent. The D-Bus `org.freedesktop.UPower.PowerProfiles` path is the
  notification surface for the deferred work.
- **Web** — no first-class API. `navigator.connection.saveData` is data-saver,
  **not** OS low-power mode; the engine does not conflate them. Reported as
  `low_power_present = false` until a standardized surface exists.

---

## 7. Downstream consumers

`power` has no upstream module dependency beyond `core` (types/compiler) and
`platform` (the Android JNI bootstrap). Downstream consumers:

- **`gpu`** — re-exports the two signals through `caps.power.power_source` /
  `caps.power.low_power_mode` as a read-only view; the OS lowering lives here. The
  re-export is documentary, not authoritative.
- **`frame.pacing`** — reads `power` once per registered render source and threads
  `power_source` / `low_power_mode` into the `Frame_Info` passed to the render
  callback; it carries the values, takes no action (mechanism only).
- **`io.asset`** — asset prefetch consults low-power and the power-source signal at
  queue-fill time to throttle background streaming on battery + low-power
  (mechanism only).
- **`media.video`** — encoder rate-control consults low-power to pick a sustainable
  bitrate / preset (mechanism only).

---

## 8. Open refinement

Battery-absence reporting: a battery-less desktop's absence should be surfaced
through `caps`, distinct from `unknown`. macOS currently reports
`power_source_present = true` and the providing-source read returns `ac`. Decide
whether caps should reflect battery hardware presence. (Carried in from the former
`sensor` module's `todo.md`.)
