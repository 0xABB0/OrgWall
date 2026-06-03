# Vibration — Spec

The single interface for tactile output across every vibration-capable device. The host handset
is the first provider, not the definition: game controllers, force-feedback nodes, XR
controllers, and any module that registers a provider expose tactile output through this same
surface. Not a haptic designer, not an audio engine.

Composes with `design/xr.md`: an XR `haptic_output` action is one provider behind this surface;
its amplitude/frequency/duration trigger is the one-event pattern of §4.5.

## Model

- **Command model** — a CoreHaptics-class event timeline (§4), lowered honestly onto simpler
  motors (§7, §8).
- **Providers** — a runtime vtable registry (§5); the host provider is registered at init, others
  register their own devices; hot-plug invalidates handles.
- **Completion** — reactor-driven, reusing the completion-pump shape of `design/gpu-rhi.md §3.3`
  (§6). The module owns no thread; all timing rides the consumer's reactor (MEL-ENGINE-III).

## 1. Objects

- `Mel_Vib_Device` — value handle over `Mel_SlotMap_Handle` (the `display` idiom). Generation
  guards turn use-after-unplug into `mel_vib_alive(d) == false`. `MEL_VIB_DEVICE_NULL` is zero.
- `Mel_Vib_Playback` — handle to a running pattern; pausable, resumable, abortable (§6); the
  identity a completion carries back. Lifecycle: playing ⇄ paused, terminating in completed |
  aborted | device-lost.
- `Mel_Vib_Descriptor` — name, owning provider, actuator topology (§4.1), `Mel_Vib_Caps`.
  Immutable for the device's lifetime; cached.
- `Mel_Vib_Caps` — capability database (§2).

```c
void mel_vib_init(const Mel_Alloc* alloc, Mel_Reactor* reactor);
void mel_vib_shutdown(void);
u32  mel_vib_refresh(void);            // re-probe providers; returns device count
u32  mel_vib_count(void);
u32  mel_vib_list(Mel_Vib_Device* out, u32 cap);
Mel_Vib_Describe_Result mel_vib_describe(Mel_Vib_Device d);
bool mel_vib_alive(Mel_Vib_Device d);
bool mel_vib_equal(Mel_Vib_Device a, Mel_Vib_Device b);
```

One process-global registry (devices are process-global, the `display` precedent). The init
reactor is the default completion pump; a per-play override (§6.1) resumes completion elsewhere.

## 2. Capabilities

Queried from the descriptor, immutable. A portable consumer branches on these before authoring:

- `amplitude` — continuous 0..1, or absent (binary on/off). Governs whether an intensity curve
  survives or is quantized.
- `sharpness` — whether sharpness/frequency is expressed, and the honored band (`min_hz`,
  `max_hz`).
- `envelopes` — whether intensity/sharpness curves are honored natively or must be pre-baked.
- `continuous` — whether sustained events exist, or only transients.
- `primitives` — `u64` mask over the open primitive-ID space (§4.4).
- `actuators` — count and per-actuator role; a dynamic list (§4.1).
- `limits` — `max_events`, `max_duration_s`, `max_envelope_points`. Exceeding truncates with a
  warning, never silently.
- `completion` — reportable completion fidelity (§6.2): native-waitable, thread-callback, or
  synthesized-from-duration.
- `pause` — whether playback can pause/resume, and whether pause is exact (hardware- or
  sample-accurate) or boundary-quantized (lands on the next event boundary, §6.7). Two bools, not
  a tier enum.

## 3. Status

The engine-wide `{ value, status }` convention of `gpu-rhi.md §3.2`. Handle validity is the
usability signal; status is diagnostic. Status is a `u8` severity (`Ok | Warned | Error`) plus a
warning bitset — **not an enum** (MEL-CODE-001: a status code is not a protocol). Branch-free
`mel_vib_failed` / `mel_vib_warned`. The human-readable cause goes to `mel_log_error("vibration",
…)` at the failure site.

Warning bits — the degradation channel:

- `AmplitudeQuantized` — intensity curve reduced to on/off.
- `SharpnessDropped` — sharpness/frequency unrepresentable, ignored.
- `EnvelopeBaked` — curves pre-sampled into discrete events.
- `PatternTruncated` — pattern exceeded a device limit and was cut.
- `CompletionSynthesized` — completion fired from modeled duration, not a hardware signal.
- `PauseQuantized` — a resume landed on an event boundary because the device could not pause
  mid-primitive (§6.7).

## 4. Command model — timeline

### 4.1 Actuators

A device exposes one or more named actuators a pattern event may address. A phone has one; a
dual-rumble pad has low-frequency, high-frequency, and trigger actuators. The set is a dynamic
list in the descriptor (MEL-CODE-002); an event carries an `actuator_mask` (0 ⇒ all).

### 4.2 Event

```c
typedef struct {
    f32 at;            // start, seconds from pattern origin
    f32 duration;      // 0 => transient impulse; > 0 => continuous
    f32 intensity;     // 0..1
    f32 sharpness;     // 0..1; maps to/from frequency_hz per device
    Mel_Vib_Envelope intensity_env;
    Mel_Vib_Envelope sharpness_env;
    u32 actuator_mask; // 0 => all
} Mel_Vib_Event;
```

Transient versus continuous is duration-driven (`duration == 0` ⇒ transient), not an enum — the
field already had to exist.

### 4.3 Envelope

A parameter curve is a dynamic array of `{ t, value }` breakpoints (allocator-fed) over an
event's duration; empty ⇒ constant at the event's base. Forwarded where honored natively
(CoreHaptics `CHHapticParameterCurve`, Android frequency-envelope builders, evdev `FF_PERIODIC`
attack/fade); otherwise sampled into discrete events bounded by `max_events`, raising
`EnvelopeBaked`.

### 4.4 Primitives

Hardware-synthesized primitives (Android `Composition` click/tick/thud/…, the iOS feedback
presets) are an **open numeric ID space** with documented well-known constants in a registry
extensible by providers — not an enum, the set is deliberately open (MEL-ENGINE-IV). A device
declares its synthesizable subset in `caps.primitives`. A primitive event is an ordinary event
carrying a primitive ID and scale; absent on a device, it falls back to a timeline approximation.

### 4.5 Pattern

```c
typedef struct {
    Mel_Vib_Event* events;   // dynamic, sorted by .at
    u32            count;
    u32            loop;      // 0 => once; N => repeats; MEL_VIB_LOOP_FOREVER until stop
} Mel_Vib_Pattern;
```

A one-event pattern is `xr.md`'s amplitude/frequency/duration pulse — intensity is amplitude,
sharpness is the device's frequency mapping, duration is duration — so XR haptics and this surface
share one vocabulary (MEL-ENGINE-IX). `mel_vib_pulse(amplitude, frequency_hz, duration_s)`
constructs it.

### 4.6 Semantic layer

`impact_light | impact_medium | impact_heavy | selection | notify_{success,warning,error}` are
documented `Mel_Vib_Pattern` constants — named patterns, not an enum — playable directly or
copied and modified. On iOS they lower to the OS feedback generators; elsewhere to their primitive
or timeline definition. Conventions, overridable.

## 5. Provider registry

### 5.1 Vtable

```c
typedef struct {
    bool (*enumerate)(void* user, Mel_Vib_Raw* out, u32 cap, u32* produced);
    bool (*open)(void* user, u64 stable_id, Mel_Vib_Descriptor* out);
    void (*close)(void* user, u64 stable_id);
    u8   (*submit)(void* user, u64 stable_id, const Mel_Vib_Lowered* pattern,
                   Mel_Vib_Completion completion);   // returns severity
    void (*abort)(void* user, u64 stable_id, u64 playback_token);
    void (*pause)(void* user, u64 stable_id, u64 playback_token);   // NULL => core synthesizes
    void (*resume)(void* user, u64 stable_id, u64 playback_token);  // NULL => core synthesizes
    void* (*native)(void* user, u64 stable_id);       // §5.4 escape
} Mel_Vib_Provider_Vtable;

Mel_Vib_Provider mel_vib_provider_register(Mel_Vib_Provider_Desc desc,
                                           const Mel_Vib_Provider_Vtable* vt, void* user);
void             mel_vib_provider_unregister(Mel_Vib_Provider p);
```

The host provider (Android `Vibrator`/`VibratorManager`, iOS/macOS CoreHaptics, web
`navigator.vibrate`) is registered by the module at `mel_vib_init`. Controller, HID, and XR
modules register their own. The primitive does not change shape per consumer.

### 5.2 Hot-plug

Providers publish add/remove by posting onto the module reactor; `mel_vib_refresh` re-probes
`enumerate`. A vanished device retires: handle generation invalidates, in-flight playbacks resolve
`device_lost` (§6.5), `mel_vib_alive` reports false. A per-provider `stable_id` recognizes a
re-plugged device.

### 5.3 Lowering lives in the core

The provider receives `Mel_Vib_Lowered`, never `Mel_Vib_Pattern`: the core has already applied
`caps` (§7), so each backend only marshals what its hardware honors, and the degradation policy
sits in one place (MEL-ENGINE-IX). `submit`/`abort` are resolved once at open and called through
the vtable directly — no per-call lookup (MEL-ENGINE-III). `pause`/`resume` are optional: NULL ⇒
the core synthesizes them by `abort`ing and re-`submit`ting the timeline tail from the elapsed
offset (§6.7).

### 5.4 Raw submission

`mel_vib_native(d)` returns the provider's native object (a `CHHapticEngine*`, the JNI `Vibrator`
ref, an evdev fd, an `XInput` user index) for a consumer that bypasses the timeline. Past the
hatch the consumer owns correctness; releasing the native object outside the registry is undefined
and debug-asserted.

## 6. Playback and completion

### 6.1 Surface

```c
typedef struct {
    Mel_Reactor*        reactor;      // override; default is the init reactor
    Mel_Vib_On_Complete on_complete;  // optional
    void*               user;
} Mel_Vib_Play_Opt;

Mel_Vib_Play_Result mel_vib_play(Mel_Vib_Device d, const Mel_Vib_Pattern* p, Mel_Vib_Play_Opt opt);
u8                  mel_vib_pause(Mel_Vib_Playback pb);   // severity; Error if caps.pause unset
u8                  mel_vib_resume(Mel_Vib_Playback pb);  // severity
void                mel_vib_abort(Mel_Vib_Playback pb);
void                mel_vib_abort_all(Mel_Vib_Device d);
bool                mel_vib_playing(Mel_Vib_Playback pb);
bool                mel_vib_paused(Mel_Vib_Playback pb);
```

`mel_vib_play` returns `{ Mel_Vib_Playback, status }`; an invalid playback handle is the
did-not-start signal. Reactor-core like `gpu-rhi.md §3.3`: one completion pump per device
multiplexes in-flight playbacks onto one reactor source; the job system is not a prerequisite.
`abort` always succeeds and resolves the playback `Aborted` (§6.5); `pause`/`resume` return a
severity (`Error`, logged, when `caps.pause` is unset — the consumer branches on caps first).
State is exposed as predicates, not an enum.

### 6.2 Three completion fidelities, one contract

`caps.completion` declares which; all resolve the same `Mel_Vib_On_Complete`:

- **Native-waitable** — evdev status fd, the web Gamepad `playEffect` promise, an HID input
  report. Registered via `mel_reactor_source_add_poll`.
- **Thread-callback** — CoreHaptics' stopped/finished handler on a vendor thread, bridged to the
  consumer reactor with `mel_reactor_post`.
- **Synthesized-from-duration** — the Android `Vibrator` API and `navigator.vibrate` report no
  completion; a `mel_reactor_timer_new` armed for the pattern's known duration resolves it,
  flagging `CompletionSynthesized`.

### 6.3 Preemption

One actuator is one resource. Default policy is replace-on-actuator: a new pattern on an actuator
aborts the holder, which resolves `Aborted`. Devices supporting parallel players (CoreHaptics
advanced players, disjoint `actuator_mask` on multi-motor pads) mix instead; `caps` reports which.
Per-play overridable (`replace | mix | reject-if-busy`); never silently mixes on hardware that
cannot.

### 6.4 Ergonomics

Coroutine suspension via `coroutine`; a plain continuation callback; `mel_vib_play_sync`
that pumps the reactor until completion — off-reactor threads only, debug-asserts on the reactor's
own thread (re-entrant pumping deadlocks).

### 6.5 Resolution

Every completion resolves with a reason — completed, aborted, preempted, device-lost — as
severity-plus-bitset, not an enum. A lost device resolves `device_lost`/`Error`; an aborted
playback resolves `Ok` with an `Aborted` bit. Pause does **not** resolve: the handle stays alive,
`mel_vib_paused` reports true, and no completion fires until the pattern finishes or is aborted.

### 6.6 Backpressure

As `gpu-rhi.md §3.3`: redundant resolves for an already-pending playback coalesce; a reactor that
has stopped pumping debug-asserts at a hard ceiling. No unique completion is dropped.

### 6.7 Pause and resume

Three realizations, declared per device by `caps.pause` (§2), mirroring the completion-fidelity
axis:

- **Native** — the provider implements `pause`/`resume` against a hardware player (CoreHaptics
  `CHHapticAdvancedPatternPlayer`: pause, resume, and seek). Hardware-accurate.
- **Engine-driven** — devices the core clocks set-point by set-point (XInput, Windows.Gaming.Input):
  pause halts the driving reactor tick and records the offset; resume continues from it. Exact, and
  free — the engine already owns the clock.
- **Resynth-from-offset** — devices with no native pause (Android `Vibrator`, `navigator.vibrate`,
  evdev, web Gamepad): on pause the core aborts the in-flight effect and records the elapsed offset;
  on resume it re-`submit`s the timeline tail sliced at that offset. Exact for waveform/amplitude
  timelines; an opaque atomic primitive straddling the pause point cannot be split, so pause snaps
  to the next event boundary and raises `PauseQuantized` (§3).

The core retains the pattern for the playback's lifetime to slice the tail. Synthesized-completion
timers (§6.2) freeze on pause — `mel_reactor_source_set_ready_time(MEL_REACTOR_READY_TIME_NEVER)` —
and re-arm for the remaining duration on resume; native and thread-callback completions stop with
the player and need no engine bookkeeping. Pausing an already-paused playback, or resuming a
playing one, is a no-op that debug-asserts (MEL-ENGINE-VIII).

## 7. Lowering

The core, not the provider, turns a `Mel_Vib_Pattern` into a `Mel_Vib_Lowered` by applying
`caps`:

- intensity curve without `amplitude` ⇒ duty-cycled on/off pulses; warn `AmplitudeQuantized`.
- sharpness without `sharpness` ⇒ dropped; warn `SharpnessDropped`.
- envelope without native curves ⇒ sampled to discrete events ≤ `max_events`; warn
  `EnvelopeBaked`.
- continuous event on a transient-only device ⇒ dense transient train; warn if lossy.
- pattern beyond `max_events`/`max_duration_s` ⇒ truncated; warn `PatternTruncated`.
- absent primitive ⇒ timeline approximation.

Every loss is named in the warning bitset and the log; no fidelity is faked (MEL-ENGINE-VIII).

## 8. Platform lowering

**iOS / macOS — CoreHaptics.** Host provider is a `CHHapticEngine`. Transient/continuous map
directly; intensity → `hapticIntensity`, sharpness → `hapticSharpness`, envelopes →
`CHHapticParameterCurve`; raw `AHAP` via §5.4. Presence gated on
`capabilitiesForHardware().supportsHaptics` — absent on iPad and on macOS except the Force-Touch
trackpad and attached Game Controllers, so the host provider registers zero devices there and
tactile output arrives only from a controller provider. The engine restarts on its reset/stopped
handler (CoreHaptics idle-shuts-down). Pause/resume (and seek) are native via
`CHHapticAdvancedPatternPlayer` (`makeAdvancedPlayer`). Devices without CoreHaptics fall back to
the OS feedback generators driving only the §4.6 semantic layer. Game-controller haptics
(`GCController.haptics` per `GCHapticsLocality`) are a separate provider over the same machinery.

**Android — `Vibrator` / `VibratorManager`.** Host provider fetches the service through the
`platform` JNI bridge (the `power` precedent). `VibratorManager` enumerates multiple vibrators;
older devices have one. Intensity → `VibrationEffect.createOneShot`/`createWaveform` amplitudes,
gated on `hasAmplitudeControl`; primitives → `VibrationEffect.Composition`; presets →
`createPredefined`. Sharpness/frequency and true envelopes exist only where the platform exposes
frequency-envelope builders; below that sharpness is dropped or approximated. The API reports no
completion ⇒ synthesized (§6.2). Requires `<uses-permission android:name="android.permission.VIBRATE"/>`
in the manifest, injected by packaging (§10); a missing permission surfaces as a logged
`SecurityException` and an `Error` status. No native pause: the core resynthesizes pause/resume
from the elapsed offset (§6.7) — exact across `createWaveform`, boundary-quantized across
`Composition`/predefined primitives.

**Win32 — no host vibrator.** The host provider registers zero devices. Tactile output arrives
through controller providers over XInput (`XInputSetState`, two motor speeds, no frequency, no
completion ⇒ synthesized) or Windows.Gaming.Input (`Gamepad.Vibration`: two motors and two
triggers, 0..1). Both are engine-clocked, so pause/resume is exact and free (§6.7).

**Linux — evdev force-feedback.** No host vibrator generally; force-feedback nodes appear as
`/dev/input/event*`. `FF_RUMBLE` carries strong/weak magnitudes; `FF_PERIODIC` carries waveform
plus attack/fade (a native envelope target); the fd is a native-waitable completion source. No
native pause; the core resynthesizes pause/resume from the elapsed offset (§6.7).

**Web — `navigator.vibrate` and the Gamepad API.** Host provider is `navigator.vibrate`, an
on/off millisecond list — no amplitude, no frequency, fire-and-forget, ignored by most desktop
browsers and iOS Safari. The core lowers to the on/off envelope, synthesizes completion, flags the
degradation, and does not assert the platform honored it. Controllers expose
`gamepad.vibrationActuator.playEffect("dual-rumble", { duration, strongMagnitude, weakMagnitude })`
— a separate provider with genuine amplitude and a real promise completion. Neither path has
native pause; the core resynthesizes pause/resume from the elapsed offset (§6.7), at on/off
granularity for `navigator.vibrate`.

## 9. Concurrency

- `describe`/`list`/`count` — `Concurrent` (caps immutable).
- `play` — `SerializedPerDevice`: the actuator is one resource and preemption (§6.3) is per device.
- `stop` — `SerializedPerObject` on the playback handle.
- Completion delivery — on the consumer's reactor, thread, and class.

No registry-internal thread; the only timing machinery is reactor sources and timers the consumer
owns.

## 10. Coding-guideline compliance

- **No fixed arrays** (MEL-CODE-002): actuators, events, breakpoints, device lists are dynamic and
  allocator-fed or caller-sized with explicit `cap`/`produced`; no `MEL_VIB_MAX_*`.
- **No enums** (MEL-CODE-001): transient/continuous is duration-driven (§4.2); primitives are an
  open ID registry (§4.4); the semantic layer is named patterns (§4.6); capabilities are a struct
  plus a `u64` mask (§2); the `pause` capability is two bools, not a tier enum (§2); playback state
  is predicates (§6.1); status and resolution reasons are severity plus bitset (§3, §6.5).
- **Allocators** (MEL-CODE-003): every allocating call takes a `Mel_Alloc*`.
- **Manifest**: the Android `VIBRATE` permission is carried by the packaging manifest path
  (`mel_manifest` / the android gradle template), or the runtime fails loud (§8).

## 11. Dependencies

`core`, `allocator`, `collection.slotmap`, `reactor`, `platform` (Android JNI / win32 globals),
`log`, `math` (envelope sampling, §7). No `gpu`, no `job`, no `provider` registry module: a
vibration provider is in-tree code registering a vtable, not a dlopened SDK, so the heavier
`provider.md` machinery would be unwarranted cost.

## 12. Build

Mirrors `power/build.c`: library `vibration`, public `include/`, common `src/vibration.c`,
per-platform host-provider sources gated by `WHEN(.platforms = MEL_ON(...))`, Apple frameworks
linked `MEL_PUBLIC` (`CoreHaptics`, `Foundation`; `UIKit` on iOS; `GameController` for the
controller provider), `mel_depends` on the §11 modules.

## 13. Failure modes

- Device vanishes mid-pattern ⇒ playbacks resolve `device_lost`/`Error`, handle invalidates.
- Reactor not pumped ⇒ completion never fires; bounded, debug-asserts at the §6.6 ceiling.
- `mel_vib_play_sync` on the reactor thread ⇒ debug-assert (re-entrant pumping deadlocks).
- Android `VIBRATE` permission absent ⇒ logged `SecurityException`, `Error`, no silent dead motor.
- CoreHaptics engine auto-stopped/reset ⇒ provider restarts before submit; failed restart `Error`.
- Intensity on a binary motor / frequency on a frequency-less device / pattern beyond limits ⇒
  lowered with the matching warning (§7).
- `navigator.vibrate` ignored ⇒ `CompletionSynthesized` already signals modeled timing.
- Provider unregistered while handles are held ⇒ handles invalidate, playbacks resolve.
- Two consumers contend one actuator ⇒ serialized; preemption per §6.3; displaced resolves
  `Aborted`.
- `loop = forever` ⇒ runs until `mel_vib_abort`; the single in-flight record is reused, no leak.
- Pause on a device whose `caps.pause` is unset ⇒ `mel_vib_pause` returns `Error` (logged);
  playback continues.
- Pause mid-opaque-primitive on a resynth device ⇒ resume snaps to the next event boundary,
  `PauseQuantized` (§6.7).
- Double pause, or resume while playing ⇒ no-op, debug-assert.
- Resume after the device was lost ⇒ `Error`; the playback already resolved `device_lost`.

## 14. Module sub-specs

- **`vibration-core`** *(no prerequisite)* — handles, registry, `Mel_Vib_Caps`, pattern/event/
  envelope, the lowering (§7), the timeline tail-slice for resynth pause (§6.7), the provider
  vtable (§5), a null provider and a host stub so the surface compiles, lowers, and unit-tests on
  the host without platform haptics.
- **`vibration-reactor`** — the completion pump, the three completion fidelities (§6.2), the
  pause/resume realizations (§6.7: native, engine-driven, resynth), `play_sync`, coroutine/callback
  ergonomics. Depends on core.
- **`vibration-android`** — `Vibrator`/`VibratorManager` host provider + manifest permission.
- **`vibration-apple`** — CoreHaptics host provider, OS-feedback fallback, `GameController`
  provider.
- **`vibration-web`** — `navigator.vibrate` host provider, Gamepad-API controller provider.
- **`vibration-evdev` / `vibration-xinput`** — Linux/Windows controller providers, registered
  through §5 once a controller/input module supplies device discovery.

Per MEL-SPEC-002, this spec moves to `modules/vibration/spec.md` once the module exists.
