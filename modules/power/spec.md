# Melody Power — `power`

OS-level power telemetry — power source, the energy/performance profile, and the
battery's state of charge — surfaced as a standalone top-level module. Extracted
from the former `sensor` module (whose thermal half is now the sibling `thermal`
module) and since augmented with the profile and battery reads.

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
  `power` reports the honest absence (`unknown`, or `present = false`), never a
  fabricated `ac`, profile bias, or charge level. The API shape stays uniform;
  the carried value is honest (MEL-ENGINE-VIII).
- **Pull, not push.** Every signal is read on demand through a synchronous
  accessor; the engine spends zero cycles when the app does not ask
  (MEL-ENGINE-III). Push-style change notification is deferred — see §5.
- **Mechanism, not policy.** `power` reports what the OS reports. The decision —
  throttle background streaming, dim UI, defer asset prefetch — belongs to the
  app's content model (MEL-ENGINE-V).
- **Orthogonal composition.** Source, profile and battery are independent reads.
  The downstream consumer joins them as it wishes (MEL-ENGINE-IX).

---

## 3. Read API

The module is stateless and global: every accessor queries the OS surface for the
active build platform and returns the current value. There is no instance to
construct, no reactor to bind, and no registration. A call costs nothing until the
app makes it.

    Mel_Power_Source         mel_power_source_current(void);
    Mel_Power_Profile        mel_power_profile_current(void);
    bool                     mel_power_profile_name(char* buf, usize cap);
    Mel_Power_Low_Power_Mode mel_power_low_power_current(void);
    Mel_Power_Battery        mel_power_battery_current(void);
    Mel_Power_Caps           mel_power_caps(void);

**Capability inspection.** `mel_power_caps()` returns
`{ power_source_present, profile_present, battery_present }`, reporting per signal
whether the running build's platform lowering publishes it. A consumer that needs
to branch on availability rather than absorb an absent read consults this once at
startup. A runtime absent read on a present surface is still honest.

---

## 4. Signals

### 4.1 Power source

    Mel_Power_Source ∈ { unknown, ac, battery }

`unknown` covers headless servers, browser builds where the user denied the
Battery Status permission, and platforms where no lowering is wired. The platform
reports what the OS reports; the absence of a battery is surfaced through
`mel_power_caps`.

### 4.2 Profile

    typedef struct { f32 bias; bool present; } Mel_Power_Profile;

The OS energy/performance scheme, expressed as an **ordinal scalar** rather than a
closed enum — the closed-set abstraction is the wrong one for a vendor-defined,
open category (MEL-CODE-001). `bias ∈ [-1, +1]`: `-1` maximum power-saving, `0`
balanced, `+1` maximum performance. `present = false` where the platform publishes
no profile surface; the consumer ignores `bias` then.

`bias` is the **portable coarse** signal. The verbatim OS token — `"performance"`,
`"balanced-performance"`, `"Low Power"` — is the **faithful escape** beneath it
(MEL-ENGINE-II), reached through `mel_power_profile_name(buf, cap)`, which fills the
caller's buffer and returns `false` where no profile exists or `cap` is too small.
The buffer is caller-owned, so no allocator is threaded and no fixed-size array is
embedded in the API (MEL-CODE-002 / MEL-CODE-003); truncation is a hard `false`,
never a silent cut (MEL-ENGINE-VIII).

**Honest gradation (MEL-ENGINE-VII).** Only Linux publishes a genuinely graded
profile (`/sys/firmware/acpi/platform_profile`). macOS, iOS, Windows and Android
expose only the binary battery-saver bit, so on those platforms `bias ∈ {-1, 0}`:
the engine maps the saver to `-1` and its absence to `0` (balanced), and never
claims `+1` it cannot observe. The richer per-OS surfaces are catalogued in §6 as
the deferred path; the `f32` axis is already shaped to carry them.

### 4.3 Low-power mode (derived)

    Mel_Power_Low_Power_Mode ∈ { unknown, off, on }

The OS battery-saver toggle, **derived** from the profile rather than read as a
separate primitive: `on ⇔ bias < 0`, `unknown ⇔ !present`. The single derivation
lives in `src/power.c` and composes over every platform's profile read
(MEL-ENGINE-IX). It is retained because "is the user asking to conserve?" is a
legitimate binary query a consumer should not have to re-derive (MEL-ENGINE-V).

### 4.4 Battery

    typedef struct {
        bool present;
        bool charging;
        f32  level;            /* state of charge ∈ [0,1] */
        f32  seconds_to_empty; /* < 0 ⇒ unavailable */
        f32  seconds_to_full;  /* < 0 ⇒ unavailable */
    } Mel_Power_Battery;

The battery's state of charge and, where the OS publishes them, its runtime
estimates. `present = false` means no battery hardware or no surface — the
consumer ignores the remaining fields. A negative `seconds_*` is the honest
absence of that estimate (an OS that gives a level but not a runtime, or is still
calibrating). The engine never fabricates a level or a time (MEL-ENGINE-VIII).

This **overrides** the prior stance — charge level was deliberately absent (the
former §4.3) on the argument that the low-power flag encodes the same policy more
honestly. The level is now exposed at Gabbo's direction: an in-app battery
indicator, dev-tools telemetry, and kiosk power dashboards are real product
surfaces the engine should not refuse (MEL-ENGINE-I). The consumer still owns the
policy (MEL-ENGINE-V); the engine only reports the number.

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
chain), so each platform compiles exactly one definition of each accessor. The
low-power derivation in `src/power.c` is the one source common to every platform.

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
  richer/notification path for the deferred work.
- **Web** — `navigator.getBattery()` (the resolved `BatteryManager`). Promise-shaped,
  so part of the deferred §5 work; until then web reports `unknown`.

### 6.2 Profile

- **iOS / iPadOS / macOS** — `NSProcessInfo.isLowPowerModeEnabled` (guarded by
  `respondsToSelector:`, macOS 12+) → `bias = -1` when on, `0` otherwise; name
  `"Low Power"` / `"Automatic"`. **Deferred richer surface:** macOS High Power
  Mode (Apple-silicon laptops) has no public read; when one exists it raises the
  ceiling to `+1`. Notification (deferred):
  `NSProcessInfoPowerStateDidChangeNotification`.
- **Linux** — `/sys/firmware/acpi/platform_profile`, the only genuinely graded
  source: `low-power | quiet | cool → -1`, `balanced → 0`,
  `balanced-performance → 0.5`, `performance → 1`; the raw token is the name.
  `profile_present = false` where the file is absent. **Deferred:** the
  `net.hadess.PowerProfiles` / `org.freedesktop.UPower.PowerProfiles` D-Bus
  interface is the richer + notification path.
- **Windows** — `GetSystemPowerStatus.SystemStatusFlag` bit `1` (Battery Saver,
  Windows 10 1709+) → `bias = -1` / `0`; name `"Battery saver"` / `"Balanced"`.
  **Deferred richer surface:** `PowerGetEffectiveOverlayScheme` (the Power-mode
  slider: best efficiency / balanced / best performance) maps the full `[-1,+1]`
  range; deferred to avoid the `powrprof` link + header-availability risk against
  the cross toolchain, consistent with the "fallback now, richer later" discipline.
- **Android** — `PowerManager.isPowerSaveMode()` → `bias = -1` / `0`; name
  `"Power saving"` / `"Normal"`. Notification (deferred):
  `ACTION_POWER_SAVE_MODE_CHANGED` broadcast.
- **Web** — no first-class API; `navigator.connection.saveData` is data-saver,
  **not** OS profile, and is not conflated. `profile_present = false`.

### 6.3 Battery

- **macOS** — IOKit IOPS: `IOPSCopyPowerSourcesList` →
  `IOPSGetPowerSourceDescription`, the `kIOPSInternalBatteryType` source.
  `kIOPSCurrentCapacityKey / kIOPSMaxCapacityKey` → `level`; `kIOPSIsChargingKey`
  → `charging`; `kIOPSTimeToEmptyKey / kIOPSTimeToFullChargeKey` (minutes, `-1`
  unknown) → seconds. Time is `-1` while full or calibrating.
- **iOS / iPadOS** — `UIDevice.batteryLevel` (`-1` unknown) → `level`;
  `batteryState == Charging` → `charging`. No public runtime estimate, so
  `seconds_* = -1`.
- **Linux** — the first `Battery`-typed `/sys/class/power_supply/*`:
  `capacity` (percent) → `level`; `status == Charging` → `charging`; the
  `energy_now / power_now` pair (else `charge_now / current_now`) with
  `energy_full / charge_full` → `seconds_to_empty` while discharging,
  `seconds_to_full` while charging.
- **Windows** — `GetSystemPowerStatus`: `BatteryFlag` bit `128` (no battery) gates
  `present`; `BatteryLifePercent` (`≤ 100`) → `level`; `BatteryFlag` bit `8`
  (charging) → `charging`; `BatteryLifeTime` (seconds, `-1` unknown) →
  `seconds_to_empty`. No time-to-full surface, so `seconds_to_full = -1`.
- **Android** — the `ACTION_BATTERY_CHANGED` intent: `level / scale` → `level`;
  `status == BATTERY_STATUS_CHARGING (2)` → `charging`. **Deferred:**
  `BatteryManager.computeChargeTimeRemaining()` (API 28+) is the time-to-full path;
  `seconds_* = -1` until wired.
- **Web** — no synchronous surface; `present = false` until the deferred §5 work.

---

## 7. Downstream consumers

`power` has no upstream module dependency beyond `core` (types/compiler) and
`platform` (the Android JNI bootstrap). Downstream consumers (aspirational, not
yet wired in code):

- **`gpu`** — re-exports source / profile through its caps view as a read-only
  documentary mirror; the OS lowering lives here.
- **`frame.pacing`** — reads `power` once per registered render source and threads
  source / profile / battery into the `Frame_Info` passed to the render callback;
  it carries the values, takes no action (mechanism only).
- **`io.asset`** — asset prefetch consults profile + source at queue-fill time to
  throttle background streaming on battery + saver (mechanism only).
- **`media.video`** — encoder rate-control consults the profile to pick a
  sustainable bitrate / preset (mechanism only).

---

## 8. Open refinement

- **Battery-absence on Apple desktops.** macOS reports `power_source_present = true`
  and `ac`; `battery_present` now distinguishes a battery-less Mac from a laptop.
  A providing-source read of `ac` with `battery_present = false` is the honest
  desktop signal; confirm no consumer conflates the two.
- **Android caps probing.** `mel_power_caps` reports all-present whenever a JNI env
  exists; tighten by probing `isPowerSaveMode` / the battery intent once where the
  API level matters.
- **Profile bias anchors.** The Linux `balanced-performance → 0.5` anchor is a
  judgement call; revisit if a consumer needs a finer or differently-weighted axis
  (the `f32` carries it without an API change).
