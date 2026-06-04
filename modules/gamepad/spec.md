# gamepad — spec

## Scope

Two layers over one device handle:
- Raw joystick — arbitrary axes/buttons/hats(POV)/balls, GUID-stable identity, VID/PID/serial/firmware,
  player index, connection (wired/wireless) + battery, output (dual-motor + trigger rumble, RGB/player
  LED, manufacturer effect packets), rich telemetry (touchpad fingers, gyro/accel, Steam Input handle).
- Standardized gamepad — SDL_GameControllerDB-format mappings (platform-filtered) over the raw layer:
  binding introspection, normalized read, regional face labels.

## Identity (`Mel_Guid`, `<guid/guid.h>`)

128-bit, SDL GUID byte layout (bus + VID/PID/version little-endian, driver signature/data tail).
Round-trips to/from the 32-hex-char string. `mel_guid_vidpid` recovers VID/PID/version. Hash via
`hash` (xxh3). Standalone `guid` library so identity binds without the gamepad surface.

## Spine (`Mel_Joystick`)

Generational value handle over `Mel_SlotMap_Handle`; `MEL_JOYSTICK_NULL`, `_equal`, `_alive`. Identity
survives reconfiguration; generation rolls on unplug. Registry-owned. `mel_joystick_refresh` re-enumerates
every active provider, diffs against the live set (keyed by provider + provider-stable id), and fires
`Mel_Joystick_Event` (`added`/`removed`/`changed` + `MEL_JOYSTICK_FIELD_*`). Pull: `mel_joystick_poll_events`.
Push: `mel_joystick_subscribe`/`_unsubscribe` (slotmap-handle subscription on a `Mel_Executor`).

Providers register via `mel_joystick_provider_register(desc)`. Each platform implements
`mel_joystick__register_host_providers`. The built-in virtual provider synthesizes joysticks for replay,
emulation, and XR-controller bridging.

## Status

`Mel_Joystick_Status` — u32 bitset. Severity mask 0x3 (`OK`/`WARNED`/`ERROR`) + capability/loss flags
(`INVALID_HANDLE`, `DEVICE_LOST`, `UNSUPPORTED`, `RUMBLE_QUANTIZED`, `TRIGGER_RUMBLE_OFF`,
`LED_UNSUPPORTED`, `EFFECT_REJECTED`, `NO_PROVIDER`). No error strings. Loud in debug, honest-absent in
release.

## Protocol enums (the only approved enums)

- `Mel_Scancode` — USB HID usage pages 0x07 + 0x0C, frozen. Reflection enum.
- `Mel_Gamepad_Button` / `Mel_Gamepad_Axis` — standardized layout (south/east/west/north, dpad,
  shoulders, sticks, triggers, paddles, touchpad). Reflection enums.

Keycode stays a u32; joystick "type" is open-world via caps/descriptor, never an enum.

## Mappings (`Mel_Gamepad_Db`)

Load SDL_GameControllerDB lines from string/line with platform filtering (`platform:` token vs the db's
filter; default host platform). Binding introspection (`mel_gamepad_button_binding`,
`mel_gamepad_axis_binding`) returns the `bN`/`aN`/`hN.M` target. `mel_gamepad_read` polls the raw device
and applies the active db, yielding a normalized `Mel_Gamepad_Frame`. Regional labels resolve A/B vs
Cross/Circle/Square/Triangle vs Nintendo from the mapped controller family. A bundled
`gamecontrollerdb.txt` ships embedded (`mel_gamepad_db_load_bundled`).

## Backends

- macos/ios — GameController.framework (`GCController`); IOKit linked for the raw-HID escape.
- linux — evdev + force-feedback (`EVIOCSFF`/`FF_RUMBLE`).
- android — `InputDevice` `SOURCE_GAMEPAD`/`SOURCE_JOYSTICK` over JNI; `Vibrator` rumble.
- win32 — XInput (state, dual-motor rumble, battery). GameInput/RawInput tracked in todo.
- wasm — Gamepad API + `gamepad.vibrationActuator`.

Each exposes the platform's honest potential; a capability that cannot exist is reported absent in caps,
never synthesized.
