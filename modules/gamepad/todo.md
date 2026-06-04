# gamepad — todo

- win32: prefer Windows.Gaming.Input / GameInput (touchpad, gyro, trigger rumble, RGB LED on DualSense)
  and add a RawInput/HID fallback for non-XInput sticks; XInput alone caps at dual-motor rumble + battery
  and four devices. `mel_joystick_win32_rawinput_handle` is wired to NULL until the HID path lands.
- macos/ios: drive CHHapticEngine through `GCDeviceHaptics` for real rumble waveforms (today `rumble`
  validates support and returns OK without playing); surface DualShock/DualSense touchpad + the
  IOHIDDevice raw escape (`mel_joystick_macos_iohid_device` returns NULL).
- linux: udev hot-plug monitor so `refresh` need not re-scan `/dev/input`; sysfs battery
  (`power_supply`) for `Mel_Joystick_Power`; ball (`REL_*`) reporting; LED via `/sys/class/leds`.
- android: read live axis/button state (today `poll` returns an empty frame; values arrive through
  the activity's `MotionEvent`/`KeyEvent`, not pollable from InputDevice alone) and trigger-aware
  `VibratorManager` for dual-rumble on Android 12+.
- wasm: the W3C extended gamepad (touch/pose) for XR controllers when the browser exposes it.
- Shared spine unification with the `input` module at merge time (see readme).
- Bundled `gamecontrollerdb.txt` carries a small seed set; wire a periodic sync to upstream
  SDL_GameControllerDB or load the full db as a runtime asset.
