# net

Client-and-server socket networking on the vat/port substrate: async TCP connect/listen/accept,
UDP datagrams, name resolution, and an address value-type. Established TCP connections vend a
`Mel_Stream` (read/write lowered onto `port`), so anything that consumes a stream composes with a
socket. The full design rationale and failure-mode sweep lives in `spec.md`.

One `Mel_Net` binds a vat and an allocator. Connect/accept/UDP ops are per-op vat sources (the
`port` pattern); resolve runs blocking `getaddrinfo` on a small owned worker pool (the `fs`
pattern). Status is the house u32 bitset; raw OS error rides alongside.

Backends: `src/posix/` covers macOS, iOS, Linux, Android. win32 (Winsock2 over the same readiness
shape) and the wasm story (no raw sockets in browsers; http/websocket lower to `fetch`/`WebSocket`
at their own layers) are owed; both stub `UNAVAILABLE` today via `src/none/`.

The module's public namespace is `<net/...>`, which shadows the platform's `<net/if.h>` family for
names the module defines; the posix backend reaches the system `<net/if.h>` because the module
defines no header of that name. Never add an `include/net/if.h`.

Depends on `core`, `allocator`, `collection`, `string`, `executor`, `future`, `vat`, `port`, `io`,
`thread`, `time`, `log`.
