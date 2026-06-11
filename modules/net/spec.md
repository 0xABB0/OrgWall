# Melody Net — `net`

Client-and-server socket networking: TCP, UDP, and async name resolution, lowered onto the vat/port substrate. Established byte connections are `Mel_Stream`s — everything that consumes a stream (http, tls, archive readers, custom protocols) composes with a socket for free (MEL-ENGINE-IX).

`design/io-asset.md` §1 named `io.network` as design space; this module is that space landing on its own merit, as a flat sibling of `fs`/`process`/`port` (the substrate family is flat, not nested under `io`).

This document is bound by the Ten Commandments; decisions cite tags.

---

## 1. Scope

- **In**: TCP connect / listen / accept, UDP bind / send_to / recv_from, async DNS resolve, address value-type, socket options (nodelay, keepalive, reuse, buffer sizes), connect timeout, cancellation, IPv4 + IPv6.
- **Out, owned elsewhere**: TLS (§9 — a stream-over-stream layer, separate granular spec), HTTP (`design/http.md`), the existing `server` module (mongoose stays; it serves the embedded-server use case and is not rebased in this wave).
- **Out, deferred not refused** (MEL-ENGINE-I): happy-eyeballs dual-stack connect, multicast, raw sockets, Unix domain sockets, platform async resolvers (`getaddrinfo_a`, `DnsQueryEx`). Each is admissible later without surface breakage; none is "never".

## 2. Position in the substrate

- One `Mel_Net` context binds a vat and an allocator (`Mel_Net_Opt{ .vat, .alloc, .resolver_workers }`), mirroring `Mel_Port_Opt` / `Mel_Fs_Opt`. No global state; an app that never creates a `Mel_Net` pays nothing (MEL-ENGINE-III).
- Established-connection read/write lowers **through `port`** — port is fd-agnostic; a connected socket fd is just an fd. Net does not duplicate port's readiness machinery for data transfer.
- Connect, accept, and UDP datagram ops are **net-owned vat sources**: port speaks only read/write without addresses, while connect needs a writability edge, accept returns a new fd, and UDP carries an address per datagram. Same per-op source pattern port itself uses internally.
- Thread affinity: all net calls on the vat owner thread, asserted in debug (MEL-ENGINE-VIII); cross-thread entry is `mel_vat_post`, as everywhere in the substrate.

## 3. Status

`Mel_Net_Status` is the house u32 bitset — severity in the low 2 bits, condition bits above; raw OS error carried separately in the result (MEL-CODE-001: no enums).

Condition bits: `CANCELLED`, `TIMED_OUT`, `REFUSED`, `UNREACHABLE`, `RESET`, `IN_USE` (bind/listen address in use), `BAD_ADDRESS` (parse/format), `RESOLVE_FAILED`, `TRUNCATED` (UDP datagram larger than buffer), `UNAVAILABLE` (platform stub), `CLOSED` (op on a closed object).

## 4. Address

`Mel_Net_Address` is a value type: 16 address bytes, `bool v6`, `u32 scope_id`, `u16 port`. The 16-byte field is protocol-fixed (IPv6 width), not a `MEL_MAX_*` kludge — the same exception that admits protocol enums (MEL-CODE-001/002 rationale).

- `mel_net_address_parse(str8, u16 port, Mel_Net_Address* out) -> Mel_Net_Status` — numeric literals only (`"127.0.0.1"`, `"::1"`, `"fe80::1%en0"`); names go through resolve. No silent fallback from parse to resolve (MEL-CODE-007).
- `mel_net_address_format(addr, const Mel_Alloc*) -> str8` — allocator-returning, `string` module convention.
- Equality, loopback/any/v4-mapped classifiers, well-known constructors `mel_net_address_v4(a,b,c,d, port)` / `mel_net_address_v6_loopback(port)`.

## 5. Resolve

    Mel_Future* mel_net_resolve_opt(Mel_Net*, str8 host, Mel_Net_Resolve_Opt);
    // opt: { u16 port; bool v4_only, v6_only; Mel_Executor* deliver; Mel_Net_Op* out_op; }

Resolves to a `Mel_Net_Resolve_Result{ Mel_Net_Address* items; usize count; status; os_error }`, allocator-owned, freed through the future's value hook. v1 lowering: blocking `getaddrinfo` on a small net-owned worker pool (`fs` worker pattern; `.resolver_workers`, no silent default — `mel_net_create` without an explicit count uses 1 and logs it once). Platform async resolvers are a backend swap later; the surface is already a future.

Cancellation marks the op cancelled and detaches — the worker call itself is not interruptible (`getaddrinfo` has no portable cancel); the future resolves cancelled immediately, the worker result is discarded. The pool is the documented cost of pending resolves at teardown: `mel_net_destroy` joins workers after cancelling pending ops (MEL-ENGINE-III: the thread is visible, requested, and bounded).

## 6. TCP

    Mel_Future* mel_net_tcp_connect_opt(Mel_Net*, Mel_Net_Tcp_Connect_Opt);
    // opt: { Mel_Net_Address address; i64 timeout_ns; bool nodelay; Mel_Executor* deliver; Mel_Net_Op* out_op; }

Future resolves to `Mel_Net_Conn*`. Lowering: non-blocking `socket` + `connect`; `EINPROGRESS` arms a writability wakeable on a per-op source; on the edge, `SO_ERROR` decides resolve-vs-fail. `timeout_ns` is mandatory in the opt struct's mind: zero means no timeout, and the call logs a one-time warning when zero — a hung connect with no deadline is the classic silent default (MEL-CODE-007 tension resolved in favor of explicitness without forbidding the choice).

    Mel_Net_Listener* mel_net_tcp_listen_opt(Mel_Net*, Mel_Net_Tcp_Listen_Opt);
    // opt: { Mel_Net_Address address; u32 backlog; bool reuse_addr; }
    Mel_Future* mel_net_listener_accept(Mel_Net_Listener*, Mel_Net_Accept_Opt);

Listen is synchronous (bind/listen fail immediately or not at all); accept is a future per accepted connection, readability-armed. `mel_net_listener_address` reports the bound address (port 0 → kernel-assigned, read back).

`Mel_Net_Conn`:
- `mel_net_conn_stream(conn) -> Mel_Stream*` — caps `{ .readable, .writable, .async }`, not seekable, not sized; read/write lower to `mel_port_read`/`mel_port_write` on the conn fd (the `process_pipe` shape); `flush` resolves immediately (no userspace buffer — what you write is what the kernel got).
- `mel_net_conn_local_address` / `mel_net_conn_peer_address`.
- `mel_net_conn_shutdown(conn, bool read, bool write)` — half-close is a real protocol tool, surfaced (MEL-ENGINE-II).
- `mel_net_conn_destroy` — cancels in-flight port ops on the fd, closes.
- Peer close during read is `MEL_IO_EOF` through the stream, not an error; `ECONNRESET` is `RESET` with error severity.

## 7. UDP

    Mel_Net_Udp* mel_net_udp_open_opt(Mel_Net*, Mel_Net_Udp_Opt);   // { address (bind, optional any), reuse_addr }
    Mel_Future* mel_net_udp_send_opt(Mel_Net_Udp*, Mel_Net_Udp_Send_Opt);   // { address, buffer, len, deliver, out_op }
    Mel_Future* mel_net_udp_recv_opt(Mel_Net_Udp*, Mel_Net_Udp_Recv_Opt);   // { buffer, len, deliver, out_op }

Recv resolves to `Mel_Net_Udp_Result{ bytes, Mel_Net_Address from, status, os_error }`. A datagram larger than the buffer delivers the truncated bytes with `TRUNCATED | WARNED` — loud, not fatal, caller decides (MEL-ENGINE-VIII). UDP is not a stream and does not pretend to be one; no `Mel_Stream` over datagrams.

## 8. Backends

- `src/posix/` — macOS, iOS, Linux, Android share one POSIX backend (sockets are the one POSIX corner that is actually portable); per-OS deltas (`SO_NOSIGPIPE` vs `MSG_NOSIGNAL`, `accept4` presence) are compile-time branches inside it, not separate trees, until a real divergence forces a split.
- `src/win32/` — Winsock2 (`WSAStartup` owned by the net context, non-blocking + the same readiness shape; IOCP/`ConnectEx` is a later lowering behind the same surface). Owed, stubbed `UNAVAILABLE` first.
- `src/wasm/` — `UNAVAILABLE` honestly: browsers have no raw sockets. The web story is http/websocket at their own layers over `fetch`/`WebSocket` (MEL-ENGINE-VII: honest alternative, not a broken shadow).
- build.c gates by `WHEN(.platforms = ...)` exactly as `port/build.c` does.

## 9. TLS (design space, split spec)

TLS is a **stream transform**: `Mel_Stream*` in, `Mel_Stream*` out — so it secures any byte stream, not just sockets (MEL-ENGINE-IX). Backend vtable per platform: Apple (Network.framework's standalone TLS or SecureTransport callbacks), Schannel, and a software backend (pending the in-flight `crypto` module). Client cert validation on by default; `skip_verification` is an explicit opt and logs loudly every handshake (MEL-CODE-007, MEL-ENGINE-VIII). Not implemented in this wave; granular spec follows once `crypto`'s surface exists.

## 10. Failure modes iterated

- **Connect refused / unreachable / timeout** — distinct condition bits; timer rides the per-op source deadline; on timeout the fd closes and the future resolves `TIMED_OUT | ERROR`.
- **Cancellation mid-connect** — `mel_net_cancel(net, op)` mirrors `mel_port_cancel`: retract wakeable, close fd, resolve cancelled. Generation-checked op handles make stale cancels harmless.
- **Vat teardown with in-flight ops** — net ops hold `mel_vat_retain` for their span (the coro/fs discipline); `mel_net_destroy` cancels all pending ops then releases; `Mel_Future_Scope` composes on top for callers.
- **SIGPIPE** — `SO_NOSIGPIPE`/`MSG_NOSIGNAL` at socket creation; data-path writes inherit port's existing SIGPIPE discipline.
- **EINTR** — backend loops on it; never surfaces.
- **fd exhaustion** — `EMFILE` on socket/accept maps to error severity with `os_error` carried; accept additionally backs off one turn before re-arming so the loop does not spin hot (MEL-ENGINE-VI).
- **Partial writes** — port already drains whole buffers across readiness turns; `PARTIAL` surfaces only on cancellation mid-drain.
- **Double destroy / op on closed object** — `CLOSED` status + debug assert (MEL-ENGINE-VIII).
- **Listener backlog overflow** — kernel's problem by design; accept keeps draining, pending accept futures are the app's admission control.
- **UDP truncation** — §7, `TRUNCATED | WARNED`.
- **Resolve of numeric literal** — resolve recognizes literals and short-circuits without touching the pool (cheap, deterministic), but parse never resolves (one-way composition, no ambiguity).
- **Dual-stack** — listen on `::` with `IPV6_V6ONLY` **explicitly set** from the opt (`bool v6_only`), never inherited from the OS default, which differs per platform (MEL-CODE-007).

## 11. Granular split

1. `net-core` — status, address, context, build.c, posix skeleton. No prerequisites.
2. `net-resolve` — worker-pool resolve. Needs core.
3. `net-tcp` — connect/listen/accept/conn-stream. Needs core; uses port.
4. `net-udp` — datagram ops. Needs core.
5. `net-tls` — split spec, blocked on `crypto` surface.
6. `net-win32` — Winsock backend. Needs 1–4 shapes frozen.

Tests per slice: address vectors (pure), loopback TCP echo through streams, UDP loopback round-trip, resolve `localhost`, cancellation and timeout paths.
