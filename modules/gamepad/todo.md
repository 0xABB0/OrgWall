# gamepad — todo

- win32: prefer Windows.Gaming.Input / GameInput (touchpad, gyro, trigger rumble, RGB LED on DualSense)
  and add a RawInput/HID fallback for non-XInput sticks; XInput alone caps at dual-motor rumble + battery
  and four devices. `mel_joystick_win32_rawinput_handle` is wired to NULL until the HID path lands.
- macos/ios: drive CHHapticEngine through `GCDeviceHaptics` for real rumble waveforms; until then
  `rumble` returns `ERROR|UNSUPPORTED` and the descriptor does not advertise dual-motor/trigger rumble
  (honest, no fabricated OK). Surface DualShock/DualSense touchpad + the IOHIDDevice raw escape
  (`mel_joystick_macos_iohid_device` returns NULL).
- linux: udev hot-plug monitor so `refresh` need not re-scan `/dev/input`; sysfs battery
  (`power_supply`) for `Mel_Joystick_Power`; ball (`REL_*`) reporting; LED via `/sys/class/leds`.
- android: read live axis/button state. Values arrive through the activity's `MotionEvent`/`KeyEvent`,
  not pollable from InputDevice alone, so `poll` honestly returns false (warns once) until a JNI event
  bridge lands; rumble works via `Vibrator`. Add trigger-aware `VibratorManager` for dual-rumble on
  Android 12+.
- wasm: the W3C extended gamepad (touch/pose) for XR controllers when the browser exposes it.
- Shared spine unification with the `input` module at merge time (see readme).
- Bundled `gamecontrollerdb.txt` carries a small seed set; wire a periodic sync to upstream
  SDL_GameControllerDB or load the full db as a runtime asset.

## Performance (should-fix, non-blocking)

- linux: `enumerate` tears down and re-opens every `/dev/input/event*` fd on each refresh
  (`pads_close_all` then re-`open_device`). A udev hot-plug monitor would let refresh diff without
  re-scanning; until then the cost is O(devices) syscalls per refresh.
- gamepad mapping: `mapping_for` is a linear scan of the loaded db per `mel_gamepad_read`/binding
  query (O(mappings) per lookup). Index mappings by GUID (hash map) for O(1) match when the db grows.

## Stable identity gaps

- ios: GameController exposes no public durable per-device id (no IOKit registry, no VID/PID/serial),
  so the stable id falls back to the GCController pointer — address reuse across reconnect can alias.
  Revisit if a public durable source appears (e.g. `GCDevice` identity on a future OS).
- macos: HID-to-GCController correlation in `match_hid` is positional (claims the next unclaimed HID
  joystick record). Two identical controllers attached simultaneously may swap VID/PID/serial/registry
  id between them. Correlate on serial when present, then on a private IOHID-GCController join if Apple
  exposes one.
