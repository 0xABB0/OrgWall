# notification — spec

OS user notifications: post now, schedule for later, update, cancel; interactive
content (action buttons, text reply, attachments, progress, badge); permission flow;
activation/dismissal events. Push registration is specced in
`design/notification-push-client.md`; server-side senders in `design/push-send.md`.

## Model

- `Mel_Notif` — generational value handle over `Mel_SlotMap_Handle` (`_NULL`,
  `mel_notif_alive`, `mel_notif_equal`). One handle per posted or scheduled
  notification; the slot owns a deep copy of the content (caller storage is free
  immediately after the call).
- Status is `u32`: severity in bits 0..1 (`OK | WARNED | ERROR`), warn flags in bits
  2..15, error flags in bits 16+. Predicates `mel_notif_failed` / `mel_notif_warned`.
- Providers register a `Mel_Notif_Provider_Desc` vtable; first `supported()` provider
  wins; `mel_notif__force_provider` overrides (tests). No provider → honest-absent:
  `mel_notif_supported()` false, operations return `ERR_NO_PROVIDER`.

## Capabilities

`mel_notif_caps()` returns the active provider's `Mel_Notif_Caps` bitset:
`ACTIONS, REPLY, ICON, ATTACHMENT, PROGRESS, BADGE, SOUND, SCHEDULE,
SCHEDULE_PERSISTS, REPEAT, UPDATE, CHANNELS, PUSH, AUTH`.

The frontend compares content against caps and degrades loudly: each dropped feature
sets its warn bit (`WARN_ACTIONS_DROPPED`, `WARN_REPLY_DROPPED`, `WARN_IMAGE_DROPPED`,
`WARN_PROGRESS_DROPPED`, `WARN_BADGE_DROPPED`, `WARN_SOUND_DROPPED`) and the call
returns `WARNED`. Nothing is dropped silently.

## Content

`Mel_Notif_Content`: `title`, `subtitle`, `body`, `channel`, `group` (thread id),
`icon` / `attachment` (`Mel_Notif_Image`: rgba buffer or path), `actions` +
`action_count` (`Mel_Notif_Action`: id, label, flags `FOREGROUND | DESTRUCTIVE |
TEXT_INPUT`, input placeholder), `progress` (present/indeterminate/value),
`sound_path`, `silent`, `has_badge` + `badge`, `payload` (opaque bytes, returned on
activation).

## Operations

- `mel_notif_post(content)` → `{ value, status }` — deliver now.
- `mel_notif_schedule(content, trigger)` — `Mel_Notif_Trigger { at_unix_ms,
  interval_ms }`; both zero is `ERR_INVALID_ARG`; interval-only fires first after one
  interval. No `CAP_SCHEDULE` → `ERR_UNSUPPORTED`. `CAP_SCHEDULE` without
  `SCHEDULE_PERSISTS` → `WARN_SCHEDULE_VOLATILE` (fires only while the process
  lives). `interval_ms` without `CAP_REPEAT` → `WARN_REPEAT_CLAMPED` (fires once).
- `mel_notif_update(n, content)` — replace content in place (progress updates).
  Providers without `update` are reposted with the same token + `WARN_UPDATE_REPOSTED`.
- `mel_notif_cancel(n)` — unschedule pending and remove delivered; frees the handle.
- `mel_notif_cancel_all()`.

## Channels

`mel_notif_channel_register(Mel_Notif_Channel_Opt { id, label, description, high,
silent })`. Only meaningful on `CAP_CHANNELS` backends (android): there, posting with
an unregistered channel id is `ERR_INVALID_ARG`; an empty channel id falls back to a
module-registered `melody.default` channel with `WARN_DEFAULT_CHANNEL` (logged). On
backends without the cap the channel field is an ignored hint.

## Authorization

Camera idiom: `mel_notif_authorization()` (sync, may be a cached snapshot on apple),
`mel_notif_authorize(alloc)` → `Mel_Future*`, `mel_notif_future_auth(f)` → one of
`mel_notif_auth_granted | provisional | denied | not_determined`. Providers without
`CAP_AUTH` are always granted. The module never prompts on its own; posting while not
granted fails with `ERR_NOT_AUTHORIZED` on backends that can tell.

## Events

Channel of `Mel_Notif_Event` (ring 128, latest-wins, overflow logged), pull
(`mel_notif_poll_events`) + push (`mel_notif_subscribe(exec, cb, user)`), tray idiom.
Kinds are flags and compose: `PRESENTED`, `ACTIVATED`, `ACTION`, `REPLIED`,
`DISMISSED`, `AUTH_CHANGED`, `PUSH_TOKEN`, `PUSH_RECEIVED`. A button press arrives as
`ACTIVATED|ACTION`; a text reply as `ACTIVATED|ACTION|REPLIED` with `reply` set.
`notif` may be dead (OS activation for a notification this process never posted);
`payload` still carries the content payload when recoverable.

String lifetime: `action_id` / `reply` / `payload` point into module-owned storage
that lives at least as long as the event's slot in the ring (the last 128 events).
Consumers that hold events across more than a ring's worth of traffic must copy.

## Backends

- **apple** (macos/ios, one source) — `UNUserNotificationCenter`. Unsupported when
  unbundled (`bundleIdentifier` nil). Categories synthesized per distinct action set;
  `CustomDismissAction` gives dismissal events. Text reply via
  `UNTextInputNotificationAction`. Schedule persists
  (`UNTimeIntervalNotificationTrigger`); repeat min 60 s (clamped + warned); absolute
  time + repeat cannot combine (repeat wins, logged). Icon and progress are not a
  platform concept (warned); attachment by path only. The backend takes the center
  delegate (warned if one was set); `authorization()` is an async-refreshed cache.
- **linux** — `org.freedesktop.Notifications` over libdbus (linked, dialog idiom).
  Actions map to action keys (`default` reserved for tap); `ActionInvoked` /
  `NotificationClosed` drive events. Update via `replaces_id`; rgba attachments via
  `image-data` hint; icon path as `app_icon`; sound via `sound-file` hint. No
  permission concept. Scheduling is in-process and serviced by
  `mel_notif_linux_pump()` (also pumped on every provider call) — volatile by caps.
  Reply unsupported (warned).
- **web** (wasm) — `Notification` API via `EM_JS`. Permission maps 1:1; sync query.
  rgba icon rendered through a canvas data-URL; attachment path/URL as `image`.
  Actions require a service worker (phase: push-client) — dropped + warned.
  Schedule/repeat via `setTimeout`/`setInterval` (volatile). Update via same-tag
  replacement. Click/close/show round-trip through exported callbacks.
- **android** — `Notification.Builder` via the `MelodyNotification` Java helper +
  `MelodyNotificationReceiver` broadcast receiver (JNI `RegisterNatives`). Channels
  required by the platform (`CAP_CHANNELS`); actions and `RemoteInput` reply
  supported; progress, badge number, large icon (rgba or path), big picture. Runtime
  permission `POST_NOTIFICATIONS` (API 33+) via the platform permission bridge
  (request code `0x4D4E`); `areNotificationsEnabled` is the source of truth.
  Per-notification sound is channel-owned on API 26+ (`sound_path` warned, `silent`
  needs a silent channel). Scheduling via main-looper handler (volatile; AlarmManager
  persistence is future work). Tap/dismiss events arrive only while the process
  lives; cold-start payload delivery needs the boot launch hooks
  (`design/notification-push-client.md`).
- **win32** — honest-absent; toast backend specced in
  `design/notification-win32-toast.md`.

## Threading

Frontend state is not thread-safe; call from the loop thread. Backends hop OS
callbacks to the main queue/looper before dispatching into the module.
