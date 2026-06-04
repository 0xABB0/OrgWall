# dylib

Loading native shared objects at runtime, resolving their C symbols, and unloading them, across
every platform that affords dynamic linking. One contract spans the three host loaders.

A library is an opaque, allocator-owned handle (`Mel_Dylib*`). `mel_dylib_open` resolves a target —
either an explicit `path`, or a bare `name` the core decorates per platform (`lib<name>.so`,
`lib<name>.dylib`, `<name>.dll`) — and returns a `Mel_Dylib_Open_Result { value, status }`. Exactly
one of `path`/`name` must be set; both or neither is a loud error. `mel_dylib_symbol` resolves a C
symbol and returns `Mel_Dylib_Symbol { addr, status }`; the status distinguishes a genuinely absent
symbol (`MEL_DYLIB_NO_SYMBOL`) from a resolved address, so a symbol whose address is legitimately
falsey is never mistaken for failure. `mel_dylib_close` unloads.

```c
Mel_Dylib_Open_Result r = mel_dylib_open(.name = "foo", .alloc = alloc);
if (mel_dylib_status_ok(r.status)) {
    Mel_Dylib_Symbol s = mel_dylib_symbol(r.value, "foo_entry");
    if (mel_dylib_status_ok(s.status))
        ((void (*)(void))s.addr)();
    mel_dylib_close(r.value);
}
```

After `mel_dylib_close`, every pointer the library yielded — resolved symbols and the handle
itself — is invalid; dereferencing one is undefined. The caller owns ordering: it must not call
through a symbol after unloading the image that holds it.

Binding and scope are flag bits, not enums, defaulting to the contracted eager-and-local
(`RTLD_NOW | RTLD_LOCAL`): `MEL_DYLIB_BIND_NOW` / `MEL_DYLIB_BIND_LAZY`, `MEL_DYLIB_GLOBAL` /
`MEL_DYLIB_LOCAL`, plus `MEL_DYLIB_NOLOAD`, `MEL_DYLIB_NODELETE`, `MEL_DYLIB_DEEPBIND` where the
platform honors them. Status is a severity (`OK`/`WARNED`/`ERROR`) plus a result bitset
(`NOT_FOUND`, `NO_SYMBOL`, `PERMISSION`, `BAD_IMAGE`, `INIT_FAILED`, `UNAVAILABLE`, `BAD_HANDLE`,
`OUT_OF_MEMORY`) — no error strings; the platform error code and message land in the log.

Backends (one compiles per platform):
- macOS / iOS / Linux / Android — `dlopen` / `dlsym` / `dlclose` (`RTLD_NOW | RTLD_LOCAL` default;
  the full RTLD flag space exposed where the libc defines it). Absence vs NULL-valued symbol is told
  apart through `dlerror`.
- Win32 — `LoadLibraryExW` / `GetProcAddress` / `FreeLibrary`, the UTF-8 path widened to UTF-16;
  `GetLastError` classified into the status. `MEL_DYLIB_NOLOAD` maps to `GetModuleHandleExW`.
- Web (wasm/emscripten) — `dlopen` via emscripten, available only in a `MAIN_MODULE` build; gate it
  by defining `MEL_DYLIB_WASM_DYNAMIC` on the consuming target. Without it the backend is
  honest-absent: `mel_dylib_available()` is false and every open reports `MEL_DYLIB_UNAVAILABLE`.

Spec: `spec.md`. Dependencies: `core`, `allocator`, `log`.
