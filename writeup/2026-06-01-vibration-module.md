# Vibration module

## Work done

New `modules/vibration/` — the single interface for tactile output, with the host handset as its
first provider and a runtime registry so controllers / XR / HID modules register their own devices
through the same surface (`mel_vib_provider_register`). Design settled with Gabbo across four forks:
CoreHaptics-class event timeline, runtime provider registry, reactor-integrated completion,
spec-first. A fifth fork (status as enum) was resolved by his amended MEL-CODE-001 — a status code
is not a protocol, so status is a `u8` severity + warning bitset, no enum anywhere in the surface.

- **Spec** authored in `design/vibration.md`, iterated against every failure mode, split into
  sub-specs, then moved to `modules/vibration/spec.md` per MEL-SPEC-002 (and rewritten terse / no
  temporal framing per the MEL-SPEC rules Gabbo added mid-session).
- **Core** (`src/vibration.c`): slotmap-handle devices and playbacks, dynamic-array registry (no
  fixed arrays), `Mel_Vib_Caps`, the pattern/event/envelope types, the lowering (applies caps →
  names every fidelity loss in the warning bits), the provider vtable, and the reactor-driven
  playback state machine (play / pause / resume / abort, synthesized-completion timer, resynth
  pause via timeline tail-slice).
- **Android provider** (`src/android/vibration_android.c`): `Vibrator` via the `platform` JNI
  bridge — `createWaveform` (amplitude or on/off) / `cancel`, caps probed from `hasVibrator` /
  `hasAmplitudeControl`. Ships the `VIBRATE` permission fragment.
- **Apple provider** (`src/apple/vibration_apple.m`): CoreHaptics `CHHapticEngine` —
  transient/continuous events with intensity/sharpness, gated on `supportsHaptics` (registers zero
  devices on the simulator / iPad / Mac, honestly).
- **`vibration_host_none.c`** for linux/win32/wasm (no host vibrator; registers nothing).
- **Example** `apps/hello-vibration/` — a gui app with Play / Pause / Resume / Abort buttons.

**Verification.** Compiles on macOS, iOS-simulator, Android. Runs correctly in three runtimes:
macOS and iOS-simulator report `0 devices` (no Taptic engine — honest) and fail loudly on every
op (no silent no-op); the **Android emulator** enumerates `Android Vibrator (amp=1 sharp=0
pause=1)` and the full lifecycle works — `Play` returns `0x49` (`WARNED | SHARPNESS_DROPPED |
COMPLETION_SYNTHESIZED`, exactly matching the emulator's caps), `Pause` returns `0x81` (`WARNED |
PAUSE_QUANTIZED`), resume re-submits the tail, the reactor timer fires completion, and the JNI
`vibrate` logs no failure.

**Framework bugs fixed in passing** (pre-existing, surfaced by being the first to run a gui+reactor
app on a device):
- `reactor/build.c` + `gui/build.c`: added `-landroid` — both use `ALooper_*` / `ANativeWindow_*`
  but linked neither, so any on-device gui app failed to `dlopen`.
- `allocator/src/tracking.c`: guarded the `execinfo`/`backtrace` path against `__ANDROID__`
  (bionic defines `__linux__` but has no `backtrace` < API 33). My one-liner was superseded by
  Gabbo's proper API-33 guard.
- Diagnosed (Gabbo fixed): gui's `backend.bridge.o` held only `MelGui_*` JNI entry points and no
  internally-referenced C symbol, so the linker never pulled that archive member into
  `libmelody.so` → `MelGui.nativeRegister` unresolved. Fixed with `mel_whole_archive` for the gui
  android link.

## Kludges (MEL-ENGINE-VIII — confess all)

Module v1 simplifications, all recorded in `modules/vibration/todo.md`:
- **Completion is always synthesized** from a reactor duration timer; the `Mel_Vib_Completion`
  native-completion bridge is wired (`core_notify`) but no provider calls it. `caps.completion_exact`
  is false everywhere; `Play` always raises `COMPLETION_SYNTHESIZED`.
- **Pause is resynth-only, boundary-quantized everywhere.** Both providers leave vtable
  `pause`/`resume` NULL, so the core aborts and re-submits the tail. Apple's native exact pause via
  `CHHapticAdvancedPatternPlayer` is not wired; every resume raises `PAUSE_QUANTIZED`.
- **Envelopes are dropped, not baked** — zeroed at lowering and flagged `ENVELOPE_BAKED`; no
  provider samples them.
- **Single actuator** — `actuator_count` reported, `actuator_mask` ignored.
- **Apple ignores `loop`**; **`mel_vib_native` returns NULL** on both providers.
- **Threading**: play/pause/resume/abort assume the caller is on the completion reactor's thread
  (the gui callbacks are); spec §9 `SerializedPerDevice` is not enforced.
- **Physical buzz unverified.** macOS/iOS-sim have no haptic hardware; the Android emulator accepts
  `vibrate()` but has no motor; the physical Pixel 4a was not connected at test time. The JNI path
  executes cleanly, but a *felt* buzz on hardware is not yet observed.
- **Spec drift**: `spec.md §6.1` still types `mel_vib_pause`/`_resume` as `u8`; the code returns
  `Mel_Vib_Status` (`u32`).
- **Out-of-module edits** to `reactor`/`gui`/`allocator` build/source were necessary to run on a
  device, not part of the vibration module proper.

## CLAUDE.md suggestions (recommendations only — not applied)

- The module-folder convention was loosened mid-session ("no hard-constraint on layout"); the
  vibration module uses `include/<m>/` + `src/<axis>/`, matching the live idiom.

## Suggestions

- **A build-time guard for JNI-only translation units.** A TU that exports only `Java_*` symbols
  and defines no internally-referenced symbol is silently dropped from the shared object unless the
  archive is whole-archived. A discovery-time check ("an androidnative TU with only JNI exports
  must be covered by `mel_whole_archive`") would have caught `backend.bridge.o` before a device run.
- **A logcat sink for the android gui runtime.** Melody's `mel_log_*` output does not reach
  logcat on android; on-device debugging relied on reading the gui status label via `uiautomator`.
  A logcat sink wired at gui android start would make device logs visible.
- **Native completion next** (resolve early via `core_notify`, set `completion_exact`), then Apple
  native pause/resume/seek, then envelope sampling — the `todo.md` resume order.
- A `mel_add_test` exercising the lowering + a null provider (caps degradation, pause tail-slice)
  would lock the warning-bit contract without platform haptics.
