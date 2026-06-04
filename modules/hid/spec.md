# hid — specification

## Scope

Raw HID transport. Bytes and identity, not parsing. Enumerate, open, exchange reports, read the
report descriptor, classify the bus, observe hot-plug. The report-descriptor parser belongs to the
consumer (gamepad), never here.

## Device spine

- Handle: `Mel_Hid_Device { Mel_SlotMap_Handle h }`, generational. `MEL_HID_DEVICE_NULL`,
  `mel_hid_equal`, `mel_hid_alive`. Identity persists across re-enumeration; generation rolls on
  departure.
- Registry-owned. `mel_hid_refresh()` re-enumerates every active provider, diffs against the live set,
  fires `ADDED`/`REMOVED`/`CHANGED`. `mel_hid_device_change_count()` bumps once per refresh that moved
  topology — a cheap poll guard.
- Dual delivery: pull `mel_hid_poll_events`; push `mel_hid_subscribe`/`_unsubscribe` on a
  consumer-supplied executor.
- Foreign sources register through `mel_hid_provider_register` (`<hid/provider.h>`), identical to the
  native backend path.

## Status

`Mel_Hid_Status` is a `u32`: severity in the low two bits (OK/WARNED/ERROR), then loss/capability
flags (`TIMED_OUT`, `WOULD_BLOCK`, `DEVICE_LOST`, `PARTIAL`, `ACCESS_DENIED`, `UNSUPPORTED`,
`NOT_OPEN`, `INVALID_HANDLE`, `CANCELLED`, `NO_BACKEND`). No error strings. `Mel_Hid_Io_Result`
pairs bytes with status; a truncated read warns `PARTIAL` and reports the full length.

## I/O

- `open`/`close`/`is_open` — `open` is idempotent per handle; I/O before `open` fails `NOT_OPEN`.
- `write` — output report, host -> device. Byte 0 is the report id for numbered reports, 0 otherwise.
- `read` — input report. `timeout_ms`: `MEL_HID_TIMEOUT_BLOCK` (block), `MEL_HID_TIMEOUT_POLL`
  (immediate, `WOULD_BLOCK` when empty), or a positive bound (`TIMED_OUT` on expiry).
- `get_feature`/`send_feature` — bidirectional control reports.
- `get_report_descriptor` — raw HID report-descriptor bytes; full length even when `cap` truncates.
- `get_string` — string descriptor by 1-based USB string index.
- `read_async` — future carrying `Mel_Hid_Io_Result`; port proactor on an fd channel, reactor source
  otherwise, NULL when no substrate (loud).

## Enums

None minted. `Mel_Hid_Bus` and `Mel_Hid_Event_Kind` are open `u32` namespaces (MEL-CODE-001). The
approved protocol enums (`Mel_Scancode`, `Mel_Gamepad_*`) belong to the input/gamepad modules, not
here.

## Honesty

A field a backend cannot report stays zero/empty, never fabricated (MEL-ENGINE-VIII). A capability a
platform lacks is reported `UNSUPPORTED`, not faked: report-descriptor read on Windows and feature
reports on the Android USB path are honest absences. WebHID is honest-unavailable when the browser
lacks `navigator.hid`. Use-after-removal is loud-not-fatal: a log line plus a status flag.

## Merge-time unification

This module self-contains its device spine (registry/diff/event channel) per the pilot buildability
rule. When the canonical `input` module lands, the spine here folds into it: `Mel_Hid_Device`,
the refresh/diff, the pull/push event surface, and the provider-registration seam are the same
shapes input defines. Only the I/O surface (reports, descriptor, strings, async read) and the bus
classification are hid-specific and stay.
