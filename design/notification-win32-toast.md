# Notification — win32 toast backend

Win32 backend for `modules/notification` via WinRT toast APIs consumed through the raw
COM ABI from C (no C++/WRL, no WinRT projection): `RoGetActivationFactory` +
`IToastNotificationManagerStatics` + `IXmlDocument`.

## Identity (AUMID)

Toasts require an AppUserModelID:
- Process AUMID set via `SetCurrentProcessExplicitAppUserModelID` at backend init,
  value from manifest key `AUMID` (fallback `BUNDLE_ID`).
- Unpackaged apps additionally need a Start-Menu shortcut carrying the AUMID, or the
  registry route (`HKCU\Software\Classes\AppUserModelId\<aumid>` with `DisplayName`).
  Backend writes the registry entry on init (idempotent, removed never — documented).
  Without identity, toasts are rejected → backend reports unsupported with a logged
  cause, never silent.

## Mapping

- Content → toast XML (`<toast><visual><binding template="ToastGeneric">`): title,
  body, subtitle (`hint-style` attribution), hero image / inline image from file path
  (rgba buffers written to a temp png via modules/image), progress bar
  (`<progress>` bound values, update via `Data` + tag), actions (`<action>` with
  `arguments`=action id), text reply (`<input type="text">` + action with
  `hint-inputId`).
- Activation: in-process via `IToastActivatedEventArgs` (activated/dismissed/failed
  handlers per toast). Activation after process exit requires a COM activator CLSID
  registration — out of scope until packaging exists; documented cap gap.
- Schedule: `IScheduledToastNotification` (+ repeat via reposting); persists in Action
  Center → caps SCHEDULE_PERSISTS set.
- Channels → no analogue; group/tag map to toast Group/Tag for update/cancel.

## Failure modes

- WinRT unavailable (Server Core, old Win) → `RoGetActivationFactory` fails →
  unsupported, honest-absent.
- Focus assist / notifications disabled per-app → toast accepted but suppressed; OS
  gives no signal — documented, not detectable.
- Temp png write failure for rgba images → image dropped, `WARN_IMAGE_DROPPED`.
