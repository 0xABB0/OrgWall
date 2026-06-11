# push — server-side notification senders

`modules/push`: send push notifications to devices from any Melody process. One
frontend, one sender vtable per service: APNs, FCM HTTP v1, WebPush (RFC 8030/8291/8292),
WNS. Async: every send returns a `Mel_Future*` resolving to `{ http_status, status,
str8 response }`.

## API sketch

- `mel_push_init(const Mel_Alloc*, Mel_Executor*)`.
- Credentials are explicit objects, loaded once, refreshed internally:
  - `Mel_Push_Apns mel_push_apns_create(.team_id, .key_id, .p8_path | .p8_pem, .topic, .sandbox)`
  - `Mel_Push_Fcm  mel_push_fcm_create(.service_account_json_path)`
  - `Mel_Push_Webpush mel_push_webpush_create(.vapid_private, .vapid_subject)` (+ keygen helper)
  - `Mel_Push_Wns  mel_push_wns_create(.package_sid, .client_secret)`
- `Mel_Future* mel_push_send(<cred>, str8 device_token_or_endpoint, str8 payload_json, Mel_Push_Opt)`
  — opt carries priority, ttl, collapse id, topic override.
- Statuses: u32 severity + bitset (`ERR_AUTH`, `ERR_TOKEN_GONE` — device unregistered,
  caller must drop the token — `ERR_TRANSPORT`, `ERR_PAYLOAD_TOO_LARGE`, `WARN_RETRYABLE`).

## Transport

- FCM / WNS / WebPush endpoints: HTTP/1.1 over TLS via mongoose client (modules/server
  grows a thin async client face, or push embeds its own `mg_mgr`).
- APNs (`api.push.apple.com:443`): HTTP/2 only → prerequisite `design/http2-client.md`.

## Crypto inventory

Present in mongoose: TLS, SHA-256/384, HMAC-SHA256, P-256 ECDSA (µECC, deterministic
RFC 6979), ECDH, AES-128-GCM, standard base64.

To build inside `modules/push`:
- base64url (wrap mongoose base64; `-_`, no padding).
- JWT assembly (header.payload.signature, ES256 + RS256).
- HKDF-SHA256 (RFC 5869) — for WebPush RFC 8291 key derivation.
- RS256 sign — FCM/WNS OAuth2 service-account assertions are RSA-SHA256. Implement
  PKCS#1 v1.5 signing over `third-party/gmp` (modular exponentiation with CRT); parse
  PKCS#8 RSA keys from the service-account JSON. No new third-party dependency.

## Per-service flow

- **APNs** — ES256 JWT (`iss`=team, `kid`=key id), cached ≤55 min; POST
  `/3/device/<token>`, headers `apns-topic`, `apns-priority`, `apns-expiration`,
  `apns-collapse-id`. 410 → `ERR_TOKEN_GONE`.
- **FCM v1** — RS256 JWT assertion → OAuth2 token (`oauth2.googleapis.com/token`),
  cached to expiry margin; POST `/v1/projects/<id>/messages:send`. UNREGISTERED →
  `ERR_TOKEN_GONE`.
- **WebPush** — parse subscription JSON (endpoint, `p256dh`, `auth`); ECDH + HKDF →
  AES-128-GCM `aes128gcm` content coding (RFC 8188); VAPID ES256 JWT (`aud`=endpoint
  origin) in `Authorization: vapid`. 404/410 → `ERR_TOKEN_GONE`.
- **WNS** — OAuth2 client-credentials → token; POST to channel URI with
  `X-WNS-Type: wns/toast|raw`. 410 → `ERR_TOKEN_GONE`.

## Failure modes

- Key parse failure at `*_create` time, loud, never at first send.
- Clock skew → JWT `iat` backdated 60 s.
- Rate limiting (429/`TooManyRequests`) → `WARN_RETRYABLE` + `Retry-After` surfaced;
  no internal retry loops (MEL-ENGINE-III: no hidden work).
- Payload caps validated client-side (APNs 4 KiB, FCM 4 KiB, WebPush 4 KiB after
  encryption overhead, WNS 5 KiB) → `ERR_PAYLOAD_TOO_LARGE` before any network spend.
- TLS verification on by default; `skip_verification` only via explicit opt.

## Tests

Vectors: RFC 8291 appendix (WebPush encryption), RFC 7515 ES256/RS256 JWS examples,
HKDF RFC 5869 vectors. Transport tested against a local mongoose server fixture.
