# Notification — push client registration

Client half of the push pipeline: obtain a device token, surface incoming remote
payloads, expose the notification that launched the app. Extends `modules/notification`
(no new module).

## API (in `notification/push.h`)

- `Mel_Notif_Status mel_notif_push_register(void)` — asks the OS for a push endpoint.
  Token arrives as a `MEL_NOTIF_EVENT_PUSH_TOKEN` event carrying the raw token bytes
  (apple: APNs device token; android: FCM registration token; wasm: PushSubscription
  JSON). Re-fires on rotation.
- `Mel_Notif_Status mel_notif_push_unregister(void)`.
- Incoming remote payloads fire `MEL_NOTIF_EVENT_PUSH_RECEIVED` with the payload bytes
  (JSON as sent). Foreground delivery only in this phase; background/silent push is a
  per-platform capability documented in caps.
- `mel_notif_launch_payload(void)` → `str8` — payload of the notification that launched
  the process, empty if none.

## Boot hooks (prerequisite)

Boot owns the app delegate / activity and currently discards launch context.
Add to `modules/boot`:

- apple: a hook registry on `MelBootAppDelegate` —
  `application:didRegisterForRemoteNotificationsWithDeviceToken:`,
  `didFailToRegisterForRemoteNotificationsWithError:`,
  `didReceiveRemoteNotification:fetchCompletionHandler:`, and launch-options capture
  (`UIApplicationLaunchOptionsRemoteNotificationKey`, local equivalent). Modules
  subscribe via `mel_boot_apple_hooks_register(const Mel_Boot_Apple_Hooks*)`; multiple
  subscribers chained in registration order.
- android: capture `getIntent().getExtras()` at `nativeStart`, expose via boot; add a
  JNI entry the Java side calls on `onNewIntent`.
- macos: same delegate hook idiom on the NSApplicationDelegate.

## Per-platform registration

- **apple** — `registerForRemoteNotifications` (requires authorization granted first);
  entitlement `aps-environment` via plist/entitlement fragment in build packaging.
- **android (FCM)** — requires firebase-messaging gradle dependency +
  `google-services.json`. Build: `mel_android_gradle_dep()` (new build-system surface)
  and a manifest fragment declaring a `FirebaseMessagingService` subclass
  (`MelodyPushService`) that forwards token + messages over JNI. App supplies
  `google-services.json` path via manifest key `FCM_CONFIG`.
- **wasm** — service worker registration (the wasm runtime gains an optional SW asset),
  `pushManager.subscribe` with the app's VAPID public key (manifest key
  `WEBPUSH_VAPID_PUBLIC`). Subscription JSON is the token. Push received while the page
  is closed is shown by the SW directly; while open it is forwarded to the module.
- **win32 (WNS)** — `PushNotificationChannelManager` requires packaged identity (MSIX).
  Honest-absent for unpackaged builds; revisit with packaging support.
- **linux** — no OS push service; honest-absent.

## Failure modes

- Registration without authorization → `ERR_NOT_AUTHORIZED` (apple rejects silently
  otherwise — fail loud instead).
- Missing FCM config / VAPID key → `ERR_BACKEND_FAIL` at register time with a logged
  cause, never a crash at post time.
- Token rotation while app closed (FCM) → service caches to storage; module re-fires
  `PUSH_TOKEN` on next init.
