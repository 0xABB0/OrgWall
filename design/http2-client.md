# http2-client — minimal HTTP/2 client

Prerequisite of APNs sending (`design/push-send.md`); APNs has no HTTP/1.1 endpoint.
Scope: a client-only, single-connection HTTP/2 implementation sufficient for
request/response APIs — not a general h2 stack, not a server.

## Scope

- Connection preface, SETTINGS exchange, single TLS connection (ALPN `h2`) over the
  mongoose TLS transport (or raw socket + mongoose TLS state).
- Frames: SETTINGS, HEADERS, CONTINUATION (rx), DATA, RST_STREAM, GOAWAY, PING,
  WINDOW_UPDATE. No PUSH_PROMISE (rejected via SETTINGS_ENABLE_PUSH=0), no priorities.
- HPACK: full decode (static + dynamic table); encode may use never-indexed literals
  only — correct and simple, at a compression cost that is irrelevant for push volumes.
- Concurrent streams: yes (APNs rewards multiplexing); flow control respected on both
  levels; connection kept alive with PING, transparent reconnect on GOAWAY.
- API: `Mel_H2* mel_h2_connect(alloc, host, port, opt)`;
  `Mel_Future* mel_h2_request(h2, method, path, headers, body)` resolving to
  `{ u32 status, headers, str8 body }`.

## Failure modes

- GOAWAY mid-flight → in-flight streams above last-stream-id resolve `WARN_RETRYABLE`;
  reconnect lazily on next request.
- Server SETTINGS lowering MAX_CONCURRENT_STREAMS → queue, never error.
- HPACK decode error / protocol error → connection error, all futures fail
  `ERR_TRANSPORT`, loud log (MEL-ENGINE-VIII).
- Flow-control exhaustion → backpressure via queueing, no deadlock (always drain rx).

## Tests

HPACK vectors from RFC 7541 appendix C; frame round-trip unit tests; live test gated
behind a local h2 fixture (nghttpd or a Go test server) — not in default `nob test`.

Placement: `modules/http2` (client-only), dependency of `modules/push` alone until a
broader HTTP story exists.
