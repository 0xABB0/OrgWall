# notification

OS user notifications: immediate and scheduled local notifications with full
interactive content — action buttons, text reply, attachments, progress, badge —
plus the permission flow and activation/dismissal events. The push pipeline builds
on this module: client registration is specced in
`design/notification-push-client.md`, server-side senders (APNs/FCM/WebPush/WNS) in
`design/push-send.md`.

Handles are generational value handles over `Mel_SlotMap_Handle` (the `tray` idiom);
status is a u32 severity + warn/error bitset with `mel_notif_failed`/`_warned`
predicates; backends register a `Mel_Notif_Provider_Desc` vtable and the first
`supported()` one wins. Capability bits (`mel_notif_caps()`) drive loud degrade:
every feature a backend cannot honor comes back as a warn bit, never a silent drop.

Authorization follows the `camera` idiom: `mel_notif_authorize()` returns a
`Mel_Future*` resolving to `granted | provisional | denied | not_determined`; the
module never prompts on its own.

Events (presented, activated, action, replied, dismissed, auth-changed, push-token,
push-received) flow through an `event` channel with a pull face
(`mel_notif_poll_events`) and a push face (`mel_notif_subscribe`), mirroring `display`.
Kinds are flags and compose (`ACTIVATED|ACTION|REPLIED` for a text reply).

Backends:

- macOS / iOS — `UNUserNotificationCenter` (shared source). Honest-absent for
  unbundled executables; persistent scheduling; actions/reply via categories.
- linux — DBus `org.freedesktop.Notifications` (libdbus, the `dialog` idiom).
  Actions, update, rgba images; volatile in-process scheduling serviced by
  `mel_notif_linux_pump()`.
- wasm — `Notification` API via `EM_JS`. Volatile scheduling; actions need a service
  worker (future push-client phase) and are warned away for now.
- android — `Notification.Builder` through a Java helper + broadcast receiver over
  JNI; channels, actions, `RemoteInput` reply, progress, `POST_NOTIFICATIONS`
  runtime permission via the `platform` permission bridge.
- win32 — honest-absent until the WinRT toast backend
  (`design/notification-win32-toast.md`).

Spec: `spec.md`. Dependencies: `core`, `allocator`, `collection`, `string`, `event`,
`executor`, `future`, `log`; `platform` on android.
