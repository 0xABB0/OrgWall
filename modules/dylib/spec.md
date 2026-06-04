# dylib — spec

## Scope
Synchronous loading, symbol resolution, and unloading of native shared objects. Not a plugin
manager, not a registry: a thin, honest wrapper over each platform's loader. No enumeration, no
async — load/resolve/unload are blocking syscalls and lowering them onto a future would steal a
thread per op (MEL-ENGINE-III).

## Surface
- `Mel_Dylib` — opaque, owned by the allocator passed to `mel_dylib_open`.
- `mel_dylib_open_opt(opt)` / `mel_dylib_open(...)` — `opt { path, name, flags, alloc }`. Exactly one
  of `path`/`name`; both or neither is `ERROR | NOT_FOUND`. `name` is decorated per platform. Returns
  `Mel_Dylib_Open_Result { value, status }`.
- `mel_dylib_symbol(lib, sym)` — `Mel_Dylib_Symbol { addr, status }`; `NO_SYMBOL` on absence,
  separate from the address so a falsey address is not mistaken for failure.
- `mel_dylib_close(lib)` — unloads; tolerates NULL.
- `mel_dylib_available()`, `mel_dylib_path(lib)`, `mel_dylib_native(lib)`.

## Status
`Mel_Dylib_Status` = u32. Severity mask `0x3` (`OK`/`WARNED`/`ERROR`). Flag bits: `NOT_FOUND`,
`NO_SYMBOL`, `PERMISSION`, `BAD_IMAGE`, `INIT_FAILED`, `UNAVAILABLE`, `BAD_HANDLE`, `OUT_OF_MEMORY`.
Static-inline predicates. No error strings; the OS error code and message go to the log.

## Flags (no enums)
`BIND_NOW`/`BIND_LAZY`, `GLOBAL`/`LOCAL`, `NOLOAD`, `NODELETE`, `DEEPBIND`. Default (unset) =
`RTLD_NOW | RTLD_LOCAL`.

## Lifetime
Symbols and the handle are invalidated by `mel_dylib_close`. The caller owns the ordering contract:
no call through a symbol after its image is unloaded.

## Backends
- posix (macOS/iOS/Linux/Android): `dlopen`/`dlsym`/`dlclose`, `dlerror` for absence vs NULL value.
- win32: `LoadLibraryExW`/`GetProcAddress`/`FreeLibrary`, UTF-8→UTF-16, `GetLastError` classified;
  `NOLOAD` via `GetModuleHandleExW`.
- wasm: emscripten `dlopen` under `MEL_DYLIB_WASM_DYNAMIC` (MAIN_MODULE build); else honest-absent.

## Failure (MEL-ENGINE-VIII)
Every failed open/resolve returns a failing status, logs the path/symbol and OS error, and never
returns a partial handle. Missing path → `NOT_FOUND`; missing symbol → `NO_SYMBOL`; no backend →
`UNAVAILABLE`.
