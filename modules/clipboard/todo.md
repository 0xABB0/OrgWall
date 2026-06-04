# clipboard — todo

## Verification
- The future/event rewrite is core-only: backends touch the OS exactly as before and call the
  unchanged `backend.h` job accessors + `mel_clip_job_resolve` (which now resolves the embedded
  `Mel_Future`). apple is run-verified on macOS (`clipboard-core` green). win32 / android / web are
  compile-shaped only here.
- win32 backend is unverified: no Windows SDK headers in the current build environment
  (`windows.h` not found — the existing `power`/`window` win32 sources fail identically here).
  Compile and test on a host with the SDK.
- android and web backends compile (NDK + emscripten toolchains) but are not run-verified on a
  device / in a browser. The web token round-trip (`mel_clip_job_token` →
  `mel_clip__job_from_token` → `mel_clip_job_resolve`) now resolves a future; exercise
  read/write/clear against the real OS clipboard.

## Features pending
- **Win32 HTML** — route `text/html` through the `CF_HTML` byte-offset wrapper (StartHTML/EndHTML/
  StartFragment/EndFragment header) on write and strip it on read. Currently dropped with
  `RepresentationDropped`.
- **Android sequence / watch** — register an `OnPrimaryClipChangedListener` on a Looper thread and
  synthesize a sequence counter; bridge the listener callback to the reactor. Currently `sequence`
  is 0 and `watch` is unsupported.
- **Web rich + query** — implement `navigator.clipboard.read()`/`write([ClipboardItem])` for
  `text/html` and `image/png`, and enumerate types for `query`. Currently text-only; non-text write
  reps dropped, `query` logs unsupported.
- **Linux X11 rich targets** — advertise/serve `text/html`, `image/png`, `text/uri-list` in `TARGETS`
  and on read (parse the offered targets) rather than UTF-8 text only. Currently non-text reps drop
  with `RepresentationDropped`.
- **Linux X11 INCR** — large payloads over the `INCR` protocol; currently a single bounded property.
- **Wayland selection serving** — implement `wl_data_device_manager`/`wl_data_source`/`wl_data_offer`
  with pipe fd transfer for cross-client copy/paste and the zwp_primary_selection protocol. Currently
  the Wayland path is a same-connection selection cache (honest: serves only this process).
- **Linux run-verification** — X11/Wayland sources are `-fsyntax-only`/`-Wall -Wextra` clean on the
  macOS host but unbuilt/unrun against a real X server or compositor; exercise on Linux.

## Possible refinements
- `Stale` detection: capture `sequence()` at request and compare at serve to set the bit.
- Multi-item read (currently the read result is a single item with multiple representations).
- A `hello-clipboard` demo app exercising the GUI + clipboard round-trip.
