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
