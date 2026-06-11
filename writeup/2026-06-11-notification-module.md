# Notification module — phase 1

## Work done

- **Design specs** (`design/`): `notification.md` umbrella (scope: OS user
  notifications, full push pipeline, scheduling, full interactivity; failure-mode
  iteration; phased plan), plus granular specs ordered by prerequisite:
  `notification-push-client.md` (device tokens, boot delegate/launch hooks, FCM,
  web service worker), `push-send.md` (APNs/FCM/WebPush/WNS senders; crypto
  inventory: mongoose has TLS/ES256/ECDH/AES-GCM/SHA-256; missing HKDF ~300 lines,
  RS256 buildable over `third-party/gmp`), `http2-client.md` (APNs is HTTP/2-only;
  minimal client spec), `notification-win32-toast.md` (WinRT COM ABI + AUMID).
- **`modules/notification`** implemented (phase 1 of the umbrella):
  - Core frontend in the tray/messagebox idiom: slotmap handles, provider vtable
    registry with force-override for tests, u32 severity+bitset statuses, caps-driven
    loud degrade, deep-copied content, event channel (pull + push faces), camera-idiom
    `Mel_Future` authorization, channel registry (android semantics), schedule
    triggers with volatile/clamp warns.
  - Backends: apple (UNUserNotificationCenter, macos+ios shared source, honest-absent
    when unbundled, action categories, text reply, dismiss via CustomDismissAction),
    linux (libdbus `org.freedesktop.Notifications`, ActionInvoked/NotificationClosed,
    `replaces_id` update, image-data hint, in-process scheduling via
    `mel_notif_linux_pump`), web (EM_JS Notification API, canvas data-URL rgba icons,
    setTimeout scheduling), android (Java helper + broadcast receiver, channels,
    RemoteInput reply, progress, POST_NOTIFICATIONS via the platform permission
    bridge, request code 0x4D4E), win32 honest-absent stub.
  - 21 tests against a fake provider; all pass on macos host.
- **Verification**: `nob test notification-core` green (21/21); module library
  compiles for ios, wasm, android (C side); android Java compiles against
  android.jar 36 (javac, deprecation note only). linux cross-compile from darwin
  fails on missing `dbus/dbus.h` — pre-existing condition, `dialog` fails
  identically on this host; builds on a real linux box with libdbus headers.

## Kludges

Confessions, sanctioned or not (MEL-ENGINE-VIII):

- **Event string lifetime is window-bounded, not owned.** Transient event strings
  (action id, reply, push payload) live in a module-owned blob FIFO sized to the
  event ring (128). An event held across >128 newer events has dangling strings.
  Documented in spec; the alternative (consumer-facing free) risked double-frees
  with multiple subscribers. Push-face subscribers on slow executors can in theory
  see a recycled blob under burst.
- **Apple `authorization()` is a cached async snapshot** — sync queries don't exist
  in UNUserNotificationCenter; first call after init may report `not_determined`
  until the async refresh lands.
- **Apple post returns OK before the OS accepts the request** —
  `addNotificationRequest` is async; failures are loud in the log but not in the
  returned status.
- **Apple backend replaces an existing center delegate** (warned in log). Clean fix
  is a boot-owned delegate hook registry (specced in push-client).
- **Android scheduling is Handler-based and volatile** — no AlarmManager, no
  persistence across process death; honest `WARN_SCHEDULE_VOLATILE`, but the OS could
  do better (specced as future work; needs exact-alarm permission + boot receiver).
- **Android `silent`/`sound_path` degrade** — sound is channel-owned on API 26+;
  `sound_path` warns `SOUND_DROPPED`, `silent` requires a silent channel and is
  otherwise ignored (documented in spec, no warn bit of its own).
- **Android cold-start activation loss** — receiver fires the launch intent but the
  native event is dropped if the process wasn't up (`nativeReady` guard); needs the
  boot launch-payload hooks from the push-client spec.
- **Default channel on android** (`melody.default`, label "Notifications") when
  content has no channel — warned + logged, but it is still a default; sanctioned by
  the platform requiring *some* channel.
- **linux `default` action key is reserved** for tap activation; a user action with
  id `default` would alias it.
- **Deprecation pragmas** around `UNNotificationPresentationOptionAlert` for the
  iOS 13 deployment target; android Java uses one deprecated API
  (BigPictureStyle-era; same vintage as messagebox helper).
- **Java side is javac-verified only** — a full gradle apk build of an app that
  depends on `notification` has not been run this session.

## CLAUDE.md suggestions (recommendations only)

- Document that linux dbus-dependent modules (dialog, now notification) cannot
  cross-compile from the darwin host (missing sysroot headers) and where they are
  expected to build, so agents stop rediscovering it.

## Suggestions

- Implement phases in order: win32 toast backend → boot delegate/launch hooks +
  push client → `modules/http2` → `modules/push` senders. The specs are ready.
- A `hello-notification` app (or a showcase smoke entry + `N` key command) would
  give the apple backend a bundled-runtime verification path; unbundled runs are
  honest-absent so `nob run` alone can't exercise it.
- `modules/event` could offer a drop callback that surfaces the evicted item; the
  blob FIFO exists only because drops are invisible to the firing module.
