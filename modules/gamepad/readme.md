# gamepad

Joystick and standardized-gamepad input across every controller-capable platform: raw axis/button/
hat(POV)/ball telemetry, GUID-stable identity, VID/PID/serial/firmware, player index, wired/wireless
and battery state; dual-motor and trigger rumble, RGB/player LED, manufacturer effect packets; PS4/PS5
touchpad fingers, integrated gyro/accel passthrough, and a Steam Input handle escape; plus synthesized
virtual joysticks for replay, emulation, and XR-controller bridging.

## Why it exists

Controllers are foreign input devices the way controllers, virtual devices, and providers are foreign
to the input spine. The raw joystick is the substrate; the standardized gamepad is a mapping layer atop
it (SDL_GameControllerDB-format), never a separate device tree. One handle (`Mel_Joystick`) identifies a
device; the gamepad layer reads through that same handle.

## Self-contained spine (merge-time note)

Per the pilot buildability rule, this module carries a *minimal* device spine consistent with the
ratified DEVICE-SPINE CONTRACT (registry-owned generational handles, hot-plug diff, pull
`poll_events` + push `subscribe`, provider registration). The canonical spine is defined by the
`input` module. At merge time, `Mel_Joystick`'s registry/diff/event core in `src/joystick.c` and the
provider-registration surface (`gamepad/provider.h`) unify with the input spine: the slotmap-handle
registry, the `Mel_*_Event { kind, dev, changed_fields }` record, and the `_poll_events`/`_subscribe`
faces are the shared parts; only the descriptor (`Mel_Joystick_Descriptor`), the protocol enums
(`Mel_Scancode`, `Mel_Gamepad_Button`, `Mel_Gamepad_Axis`), and the output routing remain
gamepad-specific.

## Public surface

- `<guid/guid.h>` — `Mel_Guid` (128-bit), SDL-format GUID parse/format, VID/PID/version extraction,
  hash (over `hash`). A standalone library target (`guid`) so other modules can bind device identity.
- `<gamepad/joystick.h>` — raw joystick: `Mel_Joystick` handle, `Mel_Joystick_Descriptor`,
  `Mel_Joystick_State` (axes/buttons/hats/balls + touches + gyro/accel), lifecycle, describe, poll,
  rumble/LED/player-index/effect output, Steam Input + native escapes. `Mel_Joystick_Status` is a
  severity-masked bitset (no error strings).
- `<gamepad/events.h>` — `Mel_Joystick_Event` (`added`/`removed`/`changed` + `MEL_JOYSTICK_FIELD_*`
  bitmask), pull (`mel_joystick_poll_events`) and push (`mel_joystick_subscribe`) faces.
- `<gamepad/protocol.h>` — the three approved protocol enums (reflection-backed): `Mel_Scancode`
  (USB HID usage pages 0x07 + 0x0C), `Mel_Gamepad_Button`, `Mel_Gamepad_Axis`.
- `<gamepad/gamepad.h>` — standardized layer: `Mel_Gamepad_Db` (load SDL_GameControllerDB from
  string/line with platform filtering, bundled db embedded), binding introspection, normalized
  `mel_gamepad_read`, regional face labels (A/B vs Cross/Circle vs Nintendo).
- `<gamepad/provider.h>` — provider registration (`mel_joystick_provider_register`) and virtual
  device synthesis (`mel_joystick_virtual_*`).
- `<gamepad/<target>/<target>.h>` — per-platform native accessors (`GCController*`, evdev fd/path,
  XInput index, Android device id, wasm gamepad index).

## Backends

- macos / ios — GameController.framework (`GCController` / `GCExtendedGamepad`); IOKit linked for the
  raw-HID escape. Battery, motion (gyro/accel), player LED where the device exposes them.
- linux — evdev (`/dev/input/event*`) with force-feedback `EVIOCSFF`/`FF_RUMBLE` ioctls for rumble.
- android — `InputDevice` `SOURCE_GAMEPAD`/`SOURCE_JOYSTICK` over JNI; rumble via the device `Vibrator`.
- win32 — XInput (state, dual-motor rumble, battery). Windows.Gaming.Input/GameInput and RawInput/HID
  for the full native potential are tracked in `todo.md`.
- wasm — the browser Gamepad API (`navigator.getGamepads`) with `gamepad.vibrationActuator` rumble.

## Bundled asset

`assets/gamecontrollerdb.txt` (SDL_GameControllerDB format) ships embedded via
`src/gamecontrollerdb.gen.h`; `mel_gamepad_db_load_bundled` parses it. Extend at runtime with
`mel_gamepad_db_load_string`.

Dependencies: `core`, `allocator`, `collection`, `string`, `event`, `executor`, `log`, `reflect`,
`guid`, `hash`, `platform` (android JNI bridge).
