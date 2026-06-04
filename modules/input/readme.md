# input

The canonical device spine plus keyboard, mouse, touch and pen.

## Why it exists

`input` defines the device-spine contract every input-class module (gamepad, sensor, hid)
generalizes: a generational `Mel_Input_Device` handle, a registry with hot-plug diffing
(`refresh` -> ADDED/REMOVED/CHANGED + `changed_fields`), a pull face (`poll_events`) and a push
face (`subscribe` onto an executor), and foreign-source registration (`provider.h`). Per-device
streams (key, text, mouse, touch, pen) flow into one event channel; `pump` services poll-driven
backends and `poll` drains typed events.

## Public surface

- `input/input.h` — spine: handle, registry, capability descriptor, status bitset, init/refresh/
  list/describe/poll/subscribe, native accessor.
- `input/events.h` — device hot-plug events and the typed input-stream union (`Mel_Input_Event`).
- `input/scancode.h` — `Mel_Scancode` (USB HID usage page 0x07/0x0C, a frozen reflection enum) and
  `Mel_Keycode` (a u32 Unicode-or-extended value, not an enum).
- `input/keyboard.h` — per-key state, modifier mask, scancode<->keycode, names, text input + IME,
  text-input area, on-screen keyboard, input-type hints.
- `input/mouse.h` — multi-device state, relative mode, capture, warp, confinement, wheel, cursors
  (system shapes, custom RGBA + hotspot, animated frames, hi-DPI alternates, acceleration transform).
- `input/touch.h` — multi-finger normalized coords + per-finger pressure, direct/indirect.
- `input/pen.h` — pressure, X/Y tilt, distance/hover, barrel rotation, tangential pressure, slider,
  eraser-tip, up to five buttons, proximity.
- `input/provider.h` — provider registration for foreign sources and the backend callback table.
- `input/<platform>/<platform>.h` — per-platform native bridges (event feed, native handle).

## Backends

- macos — NSEvent/NSResponder, CGEvent, NSCursor, system + custom cursors, text-input scaffold.
- ios — UIKit UITouch (touch + Apple Pencil), UIPress/UIKey, UITextInput OSK.
- linux — evdev (`/dev/input/event*`) enumeration + key/rel reading; XKB/Wayland IME degrade honestly
  when no compositor seat is supplied.
- android — JNI KeyEvent/MotionEvent feed (Java shim `MelodyInput`), InputMethodManager OSK.
- win32 — RawInput relative deltas, WM_* translation, WM_POINTER pen + touch, IMM32 IME, cursors.
- wasm — emscripten HTML5 DOM KeyboardEvent/MouseEvent/WheelEvent/TouchEvent, pointer-lock capture.

## Dependencies

core, allocator, collection (slotmap, array), event, executor, log, string, reflect, platform.
