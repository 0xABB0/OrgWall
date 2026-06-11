# Notification — umbrella design

OS user notifications for Melody: local (immediate + scheduled), fully interactive
(action buttons, text reply, attachments, progress), plus the complete push pipeline
(client device registration and server-side senders). Sibling of `tray`/`messagebox`
in idiom: provider vtable backends, slotmap handles, u32 severity+bitset statuses,
event channel with pull and push faces, allocator-threaded init.

## Module split

- `modules/notification` — client surface. Permission, channels, post/schedule/update/
  cancel, interactive content, events, push registration (device token + incoming
  remote payloads). Spec: `modules/notification/spec.md`.
- `modules/push` — server-side senders: APNs, FCM HTTP v1, WebPush, WNS. Runs in any
  Melody process (typically a server target). Spec: `design/push-send.md`.

Granular specs, ordered by prerequisite depth (implement no-prereq first):

| spec | prereqs |
|---|---|
| `modules/notification/spec.md` (core + local backends) | none |
| `design/notification-win32-toast.md` | none (COM ABI only) |
| `design/notification-push-client.md` | boot delegate/launch hooks |
| `design/http2-client.md` | none |
| `design/push-send.md` | http2-client (APNs), HKDF vendor (WebPush), RS256 (FCM/WNS) |

## Failure modes iterated

- **No provider / unsupported platform** — honest-absent: `mel_notif_supported()` false,
  operations return `ERR_NO_PROVIDER`. Never a crash (MEL-ENGINE-VII/VIII).
- **Permission denied / undetermined** — authorization is explicit and async
  (`Mel_Future`, camera idiom). Posting while denied returns `ERR_NOT_AUTHORIZED`;
  the module never auto-prompts (MEL-CODE-007: no silent defaults).
- **Unbundled macOS executable** — `UNUserNotificationCenter` aborts outside an `.app`.
  The apple backend reports unsupported when `bundleIdentifier` is nil; CLI runs degrade
  honestly instead of crashing.
- **Backend feature gaps** — caps bitset per provider; content using an unsupported
  feature is delivered without it and the call returns `WARNED` with a per-feature
  warn bit (messagebox idiom). Nothing is silently dropped.
- **Scheduling on platforms without an OS scheduler** (linux, wasm) — in-process
  fallback timer; fires only while the process lives. Returns
  `WARN_SCHEDULE_VOLATILE` so the caller knows persistence was not obtained.
- **App launched by a notification tap** — launch payload must be retrievable after
  init; boot currently discards launch options on ios/android. Covered in
  `design/notification-push-client.md` (boot hooks); until then the activation event
  is delivered only for taps while the process lives.
- **Event strings lifetime** (reply text, push payloads) — events own no hidden
  storage: strings are duplicated into the module allocator; polled events are
  released by the consumer via `mel_notif_event_free`, push-face events are released
  by the module after the callback returns. Any event dropped on ring overflow is
  freed at the drop site — no leak path.
- **Android channels** — API 26+ refuses notifications without a channel. Explicit
  `mel_notif_channel_register`; content with an empty channel id falls back to a
  module-registered default channel and warns (`WARN_DEFAULT_CHANNEL`) — visible,
  logged, never silent.
- **Push token rotation** — token delivery is an event (`PUSH_TOKEN`), not a one-shot
  return; rotation re-fires it. Apps must treat tokens as replaceable.
- **Sender-side auth expiry** — APNs JWTs expire (~1h), FCM OAuth tokens expire;
  senders cache and refresh under a validity margin, never per-send re-sign storms.
- **Sender transport** — APNs is HTTP/2-only: requires `design/http2-client.md`.
  FCM/WNS/WebPush endpoints accept HTTP/1.1 over TLS (mongoose client).

## Phases

1. `modules/notification`: core (frontend, providers, events, permission, scheduling)
   + backends: apple (macos/ios), linux (DBus `org.freedesktop.Notifications`),
   wasm (Notification API), android (JNI `Notification.Builder`); win32 honest-absent
   until phase 2. Tests against a fake provider.
2. win32 toast backend (WinRT COM ABI, AUMID registration).
3. Push client: boot launch/delegate hooks, APNs registration, FCM receiver
   (gradle/google-services), web service worker.
4. `modules/push` senders: http2 client, JWT/base64url, HKDF, RS256 (GMP), then
   APNs/FCM/WebPush/WNS.
