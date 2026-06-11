# hid

Raw HID transport: the floor beneath gamepad and any device that speaks HID. Enumerate by
VID/PID, open by path, exchange input/output/feature reports, read the raw report descriptor,
recover manufacturer/product/serial strings, classify the bus (USB/Bluetooth/I2C/SPI), and track
arrivals/departures with a monotonic device-change counter. Allocator-driven throughout; async
reads ride the `port` proactor where the OS exposes a pollable fd, and a deadline-0 vat source
pumping bounded reads everywhere else.

## Why it exists

Gamepad, sensor, and bespoke peripheral support all need the same primitive: bytes in and out of an
HID device, plus an honest account of what the device is. `hid` is that primitive — a transport, not
a parser. It mints the device handles; consumers (gamepad above all) parse the report descriptor and
map reports to a higher abstraction. It is a producer, depended upon, never depending on its
consumers (MEL-ENGINE-IX).

## Dependencies

`core`, `allocator`, `collection` (slotmap + dynamic arrays + container_of), `event` (the dual
pull/push delivery channel), `executor`, `future` + `port` + `vat` (the async read substrate),
`log`, `platform` (Android JNI env).

## Public surface

`<hid/hid.h>` — enumeration, query, open/close, synchronous + async report I/O, status bitset.
`<hid/events.h>` — `Mel_Hid_Event` (added/removed/changed + `MEL_HID_FIELD_*` changed-field mask),
pull (`mel_hid_poll_events`) and push (`mel_hid_subscribe`/`_unsubscribe`).
`<hid/provider.h>` — provider registration (`mel_hid_provider_register`): the seam each platform
backend fills, and the same shape a foreign source (virtual device, network HID bridge) registers.
`<hid/<platform>/<platform>.h>` — per-platform native accessors (IOHIDDeviceRef, hidraw fd, Win32
HANDLE, Android fd, WebHID device id).

- `Mel_Hid_Device` — typed value handle over `Mel_SlotMap_Handle`. Identity survives reconfiguration;
  generation rolls on unplug. Compared by `mel_hid_equal`, sentinel `MEL_HID_DEVICE_NULL`.
- `Mel_Hid_Descriptor` — by-value snapshot from `mel_hid_describe`. The string and path members are
  fixed-capacity inline buffers (`MEL_HID_STRING_CAP`) so the snapshot is self-contained, exactly as
  `Mel_Display_Descriptor` carries its `name[]`.
- `Mel_Hid_Status` — `u32` severity-masked bitset (OK/WARNED/ERROR + loss/capability flags), no error
  strings (MEL-ENGINE-VIII). `Mel_Hid_Io_Result` pairs a byte count with that status.
- `Mel_Hid_Bus` and `Mel_Hid_Event_Kind` are open `u32` namespaces, not C enums (MEL-CODE-001): a new
  bus or a new event kind extends the namespace without a closed-set rewrite. This module mints **no**
  new enums.

## Architecture

`src/hid.c` is the portable registry/diff/dispatch core: a slotmap of devices keyed by a
backend-supplied stable id, diffed on `mel_hid_refresh()` (handles preserved for survivors,
generations rolled for the departed), with per-field change detection in `src/events.c` behind the
`src/events_internal.h` seam. Delivery rides an `event` channel (latest loss policy) the registry
owns; pull drains a registry-owned subscription, push is a consumer-executor subscription. All I/O is
dispatched through a provider vtable (`<hid/provider.h>`); the open-channel cookie the provider hands
back at `open` is threaded into every I/O call so the backend never re-resolves the device.

Async reads: `mel_hid_read_async` returns a future the core owns carrying a `Mel_Hid_Io_Result`. When
the channel exposes a pollable fd and a `Mel_Port` is supplied, it lowers onto the port proactor and a
continuation translates the `Mel_Port_Result`; otherwise a deadline-0 vat source on the port's vat pumps one bounded
blocking read per drain. With neither substrate it returns NULL loudly (MEL-CODE-007 — no silent fallback).

## Backends

- macOS (`macos/src/`) — IOHIDManager / IOKit. Full enumeration, open, output/feature reports, raw
  report descriptor (`kIOHIDReportDescriptorKey`), strings, transport classification. No pollable fd
  exists on this path, so async rides the polling vat source (honest).
- iOS (`ios/src/`) — honest absence. iOS does not expose the public IOKit HID interface to
  third-party apps, so raw HID enumeration is not a capability the platform grants (MEL-ENGINE-VII):
  the iOS backend registers no provider and `mel_hid_count()` is 0. HID-class input on iOS arrives
  through GameController.framework / ExternalAccessory for permitted accessory classes; a future
  bridge over those would register here.
- Linux (`linux/src/`) — hidraw + libudev. fd-bearing: async rides the port proactor. Feature reports
  and report descriptor via `HIDIOC*` ioctls.
- Windows (`win32/src/`) — hid.dll + SetupAPI; opened `FILE_FLAG_OVERLAPPED`. Bluetooth devices are
  classified by interface path. Report-descriptor read is unsupported by the API (honest absence).
- Android (`android/src/`) — JNI `UsbManager`; the dup'd `UsbDeviceConnection` fd is fd-bearing and
  rides the port proactor. Java helper in `android/java/`. Bluetooth HID routes through the Java
  profile and surfaces no fd. Feature reports unsupported on this path (honest absence).
- wasm (`wasm/src/`) — WebHID via Emscripten `EM_JS`. Honest unavailable where the browser lacks
  `navigator.hid`. Synchronous blocking reads cannot exist on the main thread, so blocking read
  returns WOULD_BLOCK; the supported route is the vat-pumped async read draining the JS ring.

## Verification

`test/hid_test.c` — platform-agnostic contract tests driven through a fake provider (registered via
the public `mel_hid_provider_register`, with host providers suppressed by an internal test seam so the
run is hardware-free and fork-safe): dead/null handles, enumerate add/remove/change diff on both pull
and push faces, the device-change counter, open -> write -> read (poll WOULD_BLOCK then a delivered
report) -> feature -> report descriptor (with PARTIAL truncation) -> close -> closed-handle loud
failure. Run: `./nob test hid-core`.
