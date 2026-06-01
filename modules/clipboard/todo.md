# clipboard — todo

## Verification
- win32 backend is unverified: no Windows SDK headers in the current build environment
  (`windows.h` not found — the existing `power`/`window` win32 sources fail identically here).
  Compile and test on a host with the SDK.
- android and web backends compile (NDK + emscripten toolchains) but are not run-verified on a
  device / in a browser. Exercise read/write/clear against the real OS clipboard.

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
- **Linux** — X11/Wayland selection ownership and `SelectionRequest` serving, coupled to the
  `window` event loop. Currently a stub reporting `NoClipboard`.

## Possible refinements
- `Stale` detection: capture `sequence()` at request and compare at serve to set the bit.
- Multi-item read (currently the read result is a single item with multiple representations).
- A `hello-clipboard` demo app exercising the GUI + clipboard round-trip.
