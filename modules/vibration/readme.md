# vibration

The interface for tactile output across every vibration-capable device. The host handset is the
first provider; controllers, force-feedback nodes, XR controllers, and any module that registers a
`Mel_Vib_Provider_Desc` expose tactile output through the same surface.

Devices are opaque handles (`Mel_Vib_Device`). A pattern is a CoreHaptics-class event timeline
(`Mel_Vib_Pattern` of `Mel_Vib_Event`); `mel_vib_play` returns a `Mel_Vib_Playback` handle that can
be paused, resumed, and aborted. The core lowers one pattern onto each device's declared `caps`,
naming every fidelity loss in the returned status' warning bits.

## Force feedback (`vibration/ffb.h`)

Wheels and force-feedback joysticks need sustained, directional, physically-modelled forces, not a
fire-and-forget event timeline. `vibration/ffb.h` adds that surface on the same provider spine:

- Effect families (flag bitset, not an enum): rumble, constant, ramp, periodic, condition.
- Periodic waveforms: sine, square, triangle, sawtooth-up, sawtooth-down.
- Condition effects: spring, damper, inertia, friction (with coefficients, saturations, deadband,
  center).
- 3D direction encodings: polar, cartesian, spherical, steering-axis (`mel_vib_ff_dir_*`).
- Per-effect attack/fade envelope; master gain; autocenter toggle/strength.
- Effect lifecycle: `mel_vib_ff_upload` -> `Mel_Vib_FF_Slot` (generational), `start`/`stop`/`pause`/
  `resume`/`update`/`release`, with `mel_vib_ff_status` reporting playing/paused/loops-remaining.

The core queries `Mel_Vib_FF_Caps` from the provider and lowers each effect onto it, naming every
fidelity loss in the same `Mel_Vib_Status` warning bitset (`MEL_VIB_FF_WARN_*`): condition dropped,
waveform approximated, frequency clamped, envelope dropped, direction flattened, axes reduced, gain
quantized, autocenter absent. A device without a force-feedback provider answers honest-absent
(`mel_vib_ff_supported` false; `caps.present` false; `upload` fails loud) rather than faking it.

Host providers:
- Android — `Vibrator` / `VibratorManager` via JNI (the `platform` bridge). Ships the `VIBRATE`
  permission fragment. No force-feedback conditions; honest-absent for the FFB surface.
- Apple — CoreHaptics (`CHHapticEngine`), present only where `supportsHaptics` is true (real
  iPhone; absent on the simulator, iPad, and most Macs). No force-feedback conditions; honest-absent
  for the FFB surface.
- Linux — evdev force-feedback (`/dev/input/event*`): `FF_RUMBLE`/`FF_CONSTANT`/`FF_RAMP`/
  `FF_PERIODIC`/`FF_SPRING`/`FF_DAMPER`/`FF_INERTIA`/`FF_FRICTION`, native attack/fade envelopes,
  `FF_GAIN`/`FF_AUTOCENTER`.
- Win32 — DirectInput8 force-feedback: constant/ramp/periodic/condition effects, `DIPROP_FFGAIN`,
  `DIPROP_AUTOCENTER`.
- Web — no host vibrator; controller providers fill it through `mel_vib_provider_register`.

Spec: `spec.md`. Dependencies: `core`, `allocator`, `collection`, `string`, `reactor`, `time`,
`log`, `platform`.
