# net — owed

- win32 backend: Winsock2 over the same readiness shape (WSAStartup owned by the context; ConnectEx/IOCP is a later lowering). Stubbed `UNAVAILABLE` via src/none today.
- wasm: no raw sockets in browsers by design; the web story is http/websocket lowering to fetch/WebSocket at their layers. The none stub is the permanent honest answer here, not debt.
- tls: stream-over-stream layer per spec §9; blocked on the in-flight crypto module's surface for the software backend; Apple/Schannel backends are platform work.
- happy-eyeballs dual-stack connect (RFC 8305); today connect takes one address and resolve consumers pick items[0].
- platform async resolvers (getaddrinfo_a, DnsQueryEx) as a worker-pool replacement behind the same future surface.
- accept/udp ops are single-in-flight per object (kqueue ident+filter collision between per-op sources); concurrent submissions assert in debug and fail MEL_NET_BUSY in release. Lifting this means a per-object source multiplexing waiters.
- the conn stream's port-per-connection mirrors process_pipe; if a third consumer appears, promote the fd-stream-over-port shape into io.
- multicast, unix domain sockets, socket buffer-size options.
