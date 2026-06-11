# http — owed

- https: blocked on net's tls stream layer; today fetch fails `UNAVAILABLE | TLS_FAILED` with a log. The surface (urls, redirect downgrade policy) already speaks https.
- streaming request bodies (`body_stream` + chunked encoder); today asserts in debug and fails `UNAVAILABLE`.
- relative-path redirect resolution (RFC 3986 merge); absolute and absolute-path Locations work, relative fails `MALFORMED` with a log.
- content decompression hook for the in-flight compress module (Accept-Encoding / Content-Encoding).
- wasm lowering to browser fetch.
- per-read inactivity timeout (slow-loris defense beyond the whole-response timeout).
- `Content-Length: 0` is not emitted for body-less non-GET requests; callers needing it set the header explicitly.
- trailers are parsed and discarded; not surfaced on the result.
- cookies, caching, proxies: out of v1 scope by spec.
- string module's builder is a stub; wire serialization grows a byte array instead. Swap when builder lands.
