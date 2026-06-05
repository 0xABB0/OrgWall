# guid

`Mel_Guid` — 128-bit device identity in SDL_GameControllerDB byte layout.

## Why it exists

Device identity must bind without dragging in the surface that produced it. `guid` is a standalone
library so any module — gamepad, hid, input — can carry stable identity and hash it into a registry
without depending on the input spine.

## Public surface

- `<guid/guid.h>` — `Mel_Guid` (16 bytes), equality/zero, xxh3 hash (over `hash`), 32-hex-char
  string round-trip (`mel_guid_to_string` / `mel_guid_from_string`), construction from raw bytes,
  HIDAPI fields, or VID/PID/version, and VID/PID/version extraction (`mel_guid_vidpid`).

## Byte layout

Bus + VID/PID/version little-endian, driver signature/data in the tail two bytes — the SDL GUID
format, so GUIDs interoperate with SDL_GameControllerDB mapping strings.

Dependencies: `core`, `hash` (xxh3), `string` (`str8` parse face).
