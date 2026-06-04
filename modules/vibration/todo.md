# vibration — todo

## v1 simplifications (confessed — MEL-ENGINE-VIII)

- **Completion is always synthesized from duration.** The `Mel_Vib_Completion` bridge is wired
  (`core_notify`) but no provider calls it yet; finite patterns resolve on a reactor duration timer
  (`MEL_VIB_WARN_COMPLETION_SYNTHESIZED` always set). Native completion (CoreHaptics finished
  handler, evdev fd, web Gamepad promise) is unimplemented; `caps.completion_exact` is false on every
  provider.
- **Pause is resynth-only, boundary-quantized everywhere.** Both host providers leave the vtable
  `pause`/`resume` NULL, so the core aborts and re-submits the tail (events with `at >= elapsed`).
  Apple's native exact pause via `CHHapticAdvancedPatternPlayer` is not wired; `caps.pause_exact` is
  false, so every resume raises `MEL_VIB_WARN_PAUSE_QUANTIZED`.
- **Envelopes are dropped, not baked.** `intensity_env` / `sharpness_env` are zeroed at lowering and
  flagged `MEL_VIB_WARN_ENVELOPE_BAKED`; no provider samples them yet. Android forwards only base
  amplitude; Apple forwards only base intensity/sharpness.
- **Single actuator.** `caps.actuator_count` is reported but `actuator_mask` is ignored; multi-motor
  topology (dual-rumble L/R, triggers) and per-actuator roles are not modeled.
- **Loop on Apple is ignored.** Android honors `loop` via `createWaveform` repeat; CoreHaptics v1
  plays once regardless of `MEL_VIB_LOOP_FOREVER`.
- **`mel_vib_native` returns NULL on Android** (the JNI `Vibrator` local ref has no stable lifetime
  to hand out); Apple's raw `CHHapticEngine` escape is likewise not exposed yet.
- **Threading.** play/pause/resume/abort assume the caller is on the completion reactor's thread
  (the window input callbacks are). Cross-thread submission is not yet guarded (spec §9
  `SerializedPerDevice` not enforced).
- **Spec drift.** `spec.md §6.1` still types `mel_vib_pause`/`_resume` as `u8`; the implementation
  returns `Mel_Vib_Status` (`u32`). Reconcile the spec.

## Next

- Native completion (resolve early via `core_notify`) and `caps.completion_exact = true` where the
  platform gives a real signal.
- Apple native pause/resume/seek through `CHHapticAdvancedPatternPlayer`.
- Envelope sampling in the lowering for devices without native curves.
- Controller providers (XInput / evdev / web Gamepad) once an input module supplies discovery.
- Tests: a `mel_add_test` exercising lowering + a null provider (no platform haptics) for caps
  degradation and pause/resume tail-slicing.

## Force-feedback augmentation (confessed — MEL-ENGINE-VIII)

- **One effect slot per device on win32.** The DirectInput backend keeps a single
  `LPDIRECTINPUTEFFECT` per node and rebuilds it on upload/update; `ff_caps.max_effects = 1`. Many
  wheels permit several concurrent hardware effects (constant + periodic + spring). Multi-effect
  uploads are not yet held simultaneously on win32; evdev honors `EVIOCGEFFECTS` and reports the
  real count.
- **Win32 backend is host-unverifiable.** `vibration_dinput.c` needs the Windows SDK; the macOS
  cross toolchain (`zig cc`/bare clang) has no `windows.h`. Core, FFB core, and the Linux evdev
  backend cross-compile and the test passes on host, but the DirectInput path is reviewed by
  inspection only and must be confirmed on `win-pilot`.
- **Effect-status is engine-tracked, not hardware-polled.** `mel_vib_ff_status` reports the
  playing/paused state the core last commanded, not a live hardware completion. evdev's status fd
  and DirectInput's `IDirectInputEffect_GetEffectStatus` are not yet polled, so a self-terminating
  finite effect still reads `playing` until stopped/released. No native completion is wired (the
  pre-existing `MEL_VIB_WARN_COMPLETION_SYNTHESIZED` note still stands for the timeline path).
- **Direction is encoded but minimally lowered.** Cartesian/spherical/steering all collapse to a
  single polar angle (`atan2`) for both backends; per-axis cartesian forces on >1-axis hardware are
  flagged `AXES_REDUCED`/`DIRECTION_FLATTENED` rather than driven independently. The encoding is
  preserved end-to-end; only the lowering is single-axis.
- **Pause is provider-native or stop-fallback.** Where a provider exposes `ff_pause`/`ff_resume`
  the core forwards; otherwise pause stops the effect and resume restarts it from the top. evdev and
  DirectInput leave `ff_pause`/`ff_resume` NULL, so pause is stop-and-restart (not mid-effect
  resume). No `PauseQuantized` analog is raised for FFB yet.
- **evdev FF scratch bitset is a fixed stack array** (`ff_bits[]`, sized from the kernel `FF_MAX`
  protocol constant). It is a protocol bitmap query buffer, not a growable collection, so MEL-CODE-002
  does not bite; flagged for Gabbo's judgment.
- **`ff_set_autocenter_strength` on DirectInput is on/off only.** `DIPROP_AUTOCENTER` takes
  `ON`/`OFF`; the strength float is clamped and used only as a boolean. evdev's `FF_AUTOCENTER`
  honors the full 0..1 magnitude.
