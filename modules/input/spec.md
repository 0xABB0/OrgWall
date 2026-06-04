# input — spec

## Spine (canonical)

`Mel_Input_Device` wraps a `Mel_SlotMap_Handle`: generational value handle, `MEL_INPUT_DEVICE_NULL`
sentinel, `_equal`/`_alive`. Identity survives reconfiguration; generation rolls on unplug. The
registry owns handles. `mel_input_refresh()` re-enumerates every provider, diffs against the prior
registry by `(provider, stable_id)`, and fires `ADDED`/`REMOVED`/`CHANGED` with a `changed_fields`
bitmask (`MEL_INPUT_FIELD_*`, `1u<<n`). Pull: `mel_input_poll_events`. Push: `mel_input_subscribe`
onto a `Mel_Executor*`. Both faces read one `event`-module channel.

## Device class is open-world

There is no device-class enum (MEL-CODE-001). A device advertises capability bits
(`MEL_INPUT_CAP_*`) in `Mel_Input_Device_Descriptor`. A single physical device may carry several
classes; foreign/virtual devices register through `provider.h` exactly like a built-in backend.

## Approved protocol enums

`Mel_Scancode` is the USB HID usage page 0x07 + 0x0C table, frozen; a reflection enum. `Mel_Keycode`
is a `u32` Unicode-or-extended value, NOT an enum. Pointer/touch/pen phases and event kinds are plain
(non-reflection) discriminators in the house event-record idiom.

## Status

`Mel_Input_Status` is a `u32` bitset: severity mask `0x3` (OK/WARNED/ERROR) plus capability/loss
flags from `1u<<2` (invalid handle, unsupported, degraded, no provider, area ignored, capture
emulated, warp/confine unavailable, cursor quantized, IME synthesized). No error strings. Debug
asserts loud; release degrades honestly (MEL-ENGINE-VIII).

## Stream

Backends translate native events and push them through a `Mel_Input_Sink` (`mel_input_sink_*`),
which fires onto the stream channel. Push backends (macos/ios/android/wasm/win32) feed via the
platform bridge; poll backends (linux evdev) service the kernel fd in `mel_input_pump`.
`mel_input_poll` drains the typed `Mel_Input_Event` union.

## Providers

`mel_input_provider_register` takes a callback table (enumerate, pump, key/mouse/touch/pen state,
cursor lifecycle, text/IME, native). The spine selects the first provider exposing each capability;
absence yields an honest status, never a silent default (MEL-CODE-007).
