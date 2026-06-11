# http

An HTTP/1.1 client over `net` streams: the request half of the web story (`server` answers; this
asks). Native on the vat substrate — request serialization, incremental response parsing
(content-length, chunked with trailers, close-delimited), keep-alive connection pooling with a
per-host cap and FIFO admission, redirect following (opt-in count, method rewrite per RFC), per-
phase timeouts, cancellation, body collect or stream-to-sink. URL parsing and percent coding live
in `<http/url.h>`. Design rationale and failure-mode sweep in `spec.md`.

Vocabulary matches `server`: methods are `str8`, status codes are plain `i32`, headers are
name/value pairs. A 4xx/5xx response is a successful transfer — `status_code` carries it; error
severity is reserved for transport and protocol failures.

Owed (MEL-ENGINE-I, honestly stubbed today): https (`UNAVAILABLE | TLS_FAILED` until the tls
stream lands), streaming request bodies (`body_stream`), relative-path redirect resolution,
content decompression (hook reserved for the `compress` module), the wasm `fetch` lowering.

The wire layer (`src/wire.c`) is pure and incremental — no IO, fully vector-testable. Protocol
state constants there fall under MEL-CODE-001's protocol exception.

Depends on `net` (and through it the async substrate), `io`, `string`, `collection`, `future`,
`executor`, `vat`, `time`, `log`.
