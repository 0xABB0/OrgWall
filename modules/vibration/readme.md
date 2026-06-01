# vibration

The interface for tactile output across every vibration-capable device. The host handset is the
first provider; controllers, force-feedback nodes, XR controllers, and any module that registers a
`Mel_Vib_Provider_Desc` expose tactile output through the same surface.

Devices are opaque handles (`Mel_Vib_Device`). A pattern is a CoreHaptics-class event timeline
(`Mel_Vib_Pattern` of `Mel_Vib_Event`); `mel_vib_play` returns a `Mel_Vib_Playback` handle that can
be paused, resumed, and aborted. The core lowers one pattern onto each device's declared `caps`,
naming every fidelity loss in the returned status' warning bits.

Host providers:
- Android — `Vibrator` / `VibratorManager` via JNI (the `platform` bridge). Ships the `VIBRATE`
  permission fragment.
- Apple — CoreHaptics (`CHHapticEngine`), present only where `supportsHaptics` is true (real
  iPhone; absent on the simulator, iPad, and most Macs).
- Linux / Win32 / Web — no host vibrator; the host provider registers nothing. Controller providers
  fill these through `mel_vib_provider_register`.

Spec: `spec.md`. Dependencies: `core`, `allocator`, `collection`, `string`, `reactor`, `time`,
`log`, `platform`.
