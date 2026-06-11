# Melody HTTP Client — `http`

An HTTP/1.1 client over `net` streams. The missing half of the web story: `server` answers requests; nothing in the tree can make one. Native on the substrate — not a second mongoose wrapper, because `mg_mgr_poll` is its own loop and does not compose with vat/port, and because an http client riding `net` is the composition that justifies `net` (MEL-ENGINE-IX).

Vocabulary deliberately matches `server`: methods are `str8` (no method enum, MEL-CODE-001), status codes are plain `i32`, headers are name/value `str8` pairs.

This document is bound by the Ten Commandments; decisions cite tags.

---

## 1. Scope

- **In**: HTTP/1.1 over TCP — request serialization, response parse (status line, headers, `Content-Length` and chunked bodies), keep-alive connection pool, redirects (opt-in), per-phase timeouts, cancellation, streamed request/response bodies, URL parse.
- **Out, deferred not refused** (MEL-ENGINE-I): HTTPS (blocked on `net-tls`; the surface carries `https://` from day one and resolves `UNAVAILABLE` honestly until the TLS stream lands), HTTP/2 and /3 (the request/response surface is version-silent; h2 is a connection-layer swap later), content decompression (hook reserved for the in-flight `compress` module), cookies jar, caching, proxies, websocket client.
- **wasm**: the same surface lowers to `fetch` — the browser is the HTTP stack there (MEL-ENGINE-VII). Owed; `UNAVAILABLE` stub first.

## 2. Surface

One `Mel_Http` context: `Mel_Http_Opt{ .net, .alloc, .max_conns_per_host, .pool_idle_timeout_ns }` — borrows a `Mel_Net*`, never creates one in shadow (MEL-ENGINE-III).

    Mel_Future* mel_http_fetch_opt(Mel_Http*, Mel_Http_Request req, Mel_Http_Fetch_Opt opt);

`Mel_Http_Request`:
- `str8 method` — any token; helpers `MEL_HTTP_GET`-style string macros only if Gabbo blesses them, otherwise plain `S8("GET")`.
- `str8 url` — parsed internally; or pre-parsed `Mel_Http_Url`.
- `Mel_Http_Header* headers; usize header_count` — caller array; client adds `Host`, `Content-Length`/`Transfer-Encoding`, `Connection` only when absent (caller override always wins, MEL-ENGINE-IV).
- Body: `str8 body` **or** `Mel_Stream* body_stream` (+ optional `i64 body_len`; unknown length streams chunked). Exactly one; both set is a debug assert (MEL-ENGINE-VIII).

`Mel_Http_Fetch_Opt`:
- `i64 connect_timeout_ns, response_timeout_ns, total_timeout_ns` — zero = none, each logs once when zero (MEL-CODE-007 discipline, as net connect).
- `u32 max_redirects` — 0 = don't follow (the non-silent default: redirects are returned, not chased).
- `Mel_Stream* sink` — when set, the body streams into it and the future resolves after the final byte with headers + status only; when null, the body collects into an allocator-owned buffer on the response.
- `Mel_Executor* deliver; Mel_Http_Op* out_op;`

Future resolves to `Mel_Http_Response*`: `i32 status_code`, `str8 reason`, headers (allocator-owned, iterable + `mel_http_response_header(resp, name)` case-insensitive lookup), `str8 body` (collect mode), `Mel_Http_Status status` + `os/net` error carry. Released through the future value hook or `mel_http_response_destroy`.

`Mel_Http_Status` is the house u32 bitset: `CANCELLED`, `TIMED_OUT`, `RESOLVE_FAILED`, `CONNECT_FAILED`, `TLS_FAILED`, `MALFORMED` (unparseable response), `TOO_MANY_REDIRECTS`, `BODY_INCOMPLETE` (peer closed mid-body), `UNAVAILABLE`. An HTTP error status code (4xx/5xx) is **not** an error severity — the transfer succeeded; the caller reads `status_code` (no conflation, MEL-ENGINE-VIII honesty cuts both ways).

## 3. URL

`Mel_Http_Url` value type + `mel_http_url_parse(str8, const Mel_Alloc*, Mel_Http_Url*)`: scheme, host (incl. `[v6]`), explicit-or-scheme-default port, path, query, fragment; percent-encoding helpers. Lives in `http` until a second consumer promotes it (`string`/`uri` is the obvious future home; not pre-emptively generalized).

## 4. Connection pool

Keyed by scheme+host+port. At most `max_conns_per_host` live; a fetch over the cap queues (FIFO) rather than dialing unbounded (MEL-ENGINE-VI). Idle conns carry a deadline source and close at `pool_idle_timeout_ns`; a pooled conn that turns out dead at reuse (stale EOF on first write/read) retries once on a fresh dial — the single sanctioned retry, internal and logged at debug level. `Connection: close` honored both ways. Pool teardown cancels queued fetches `CANCELLED`.

## 5. Protocol engine

A connection runs a small explicit state machine (await-coro on the net stream): send head → send body (chunked encoder when streaming unknown length) → read+parse status line and headers (incremental, bounded: header block over a configurable cap — default 64 KiB, in the opt struct, logged — fails `MALFORMED`) → body by `Content-Length` | chunked decoder (incl. trailers, discarded v1) | read-to-EOF (HTTP/1.0 close-delimited) → conn returns to pool or closes.

`HEAD` and `204`/`304` have no body regardless of headers; `100-continue` responses are skipped transparently; response interim headers fold per RFC 9112. Pipelining is not offered (real-world poison; keep-alive + pool covers the win).

## 6. Failure modes iterated

- **Peer closes mid-body** — `BODY_INCOMPLETE | ERROR`, partial bytes delivered (collect mode: buffer so far on the response; sink mode: already written), count reported.
- **Malformed chunk size / header overflow / non-numeric status** — `MALFORMED`, conn closed, never pooled.
- **Redirect loops / downgrade** — `max_redirects` cap → `TOO_MANY_REDIRECTS`; https→http redirect is refused by default (explicit `allow_insecure_redirect` opt), method/body rewrite per 301/302/303/307/308 semantics.
- **Cancellation** — at any phase: op handle cancels the underlying net op/coro, conn closes (a half-spoken HTTP/1.1 conn is unreusable), future resolves `CANCELLED`.
- **Timeouts** — connect rides net's; response timeout arms at request-sent, covers to-last-body-byte; total covers the whole fetch incl. redirects. Each fires `TIMED_OUT` + phase bit.
- **Slow-loris responses** — response timeout is the defense; no per-read inactivity timer in v1 (deferred, surface-compatible).
- **Sink write fails** — fetch fails with the sink's IO status carried; conn drains-or-closes by remaining-body-size heuristic (small remainder drains to keep the conn; large closes — the cap is the header-cap constant, logged).
- **Body stream shorter/longer than declared `body_len`** — debug assert + `MALFORMED` fail; never silently pad or truncate (MEL-ENGINE-VIII).
- **Pool reuse race with server-side close** — §4 single retry.
- **Huge collect-mode body** — `max_body_bytes` opt (0 = unbounded, logged once); over-cap fails with `BODY_INCOMPLETE`-distinct `BODY_TOO_LARGE` bit.

## 7. Granular split

1. `http-url` — URL parse + percent coding. No prerequisites (string only).
2. `http-wire` — request serializer, response parser, chunked codec as pure incremental functions over buffers (no IO; fully vector-testable). No prerequisites.
3. `http-fetch` — engine over net streams + pool. Needs 1, 2, `net-tcp`.
4. `http-tls` — https enablement. Blocked on `net-tls`.
5. `http-wasm` — fetch lowering. Independent of 3's internals.

Tests: wire-level vectors (parser/serializer/chunked against RFC examples and malformed inputs), loopback integration against `modules/server` (GET/POST, chunked both ways, keep-alive reuse observed, redirect chain, timeout and cancel paths).
