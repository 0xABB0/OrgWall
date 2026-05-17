# Stacktrace Symbolication

## Problem Statement

The current stacktrace module ties capture and symbolication together inside a single per-platform implementation. On Win32 it uses `dbghelp` (PDB, function name + filename + line). On macOS and Android the per-platform code can capture addresses and resolve function names via `dladdr`, but has no path to filename/line information without shelling out or vendoring a DWARF reader. Source-line info is part of the public `Mel_Stackframe` struct and the `MEL_STACKTRACE_HAS_SOURCE_INFO` toggle, but the toggle is a lie on two of the three platforms — the fields exist and are silently zero.

The available approaches each have a real, non-overlapping tradeoff:

- **Platform-native** (`dbghelp` / `dladdr`) — no extra dependencies, no extra runtime artifacts to ship. On Windows you get full source info because PDBs are next to the binary by default; on macOS/Android you get function names only.
- **libbacktrace** — full source info in-process on all targets, single algorithm, no subprocess overhead. Requires DWARF debug info to be present at runtime: dSYM bundles on macOS, unstripped `.so` on Android, clang-with-DWARF on Win32. Cannot resolve frames in system libraries that ship PDB-only.
- **Subprocess** (`llvm-symbolizer`, `atos`, `addr2line`) — full source info, no in-process dependency to vendor, works against whatever the host toolchain provides. Slow (fork per resolution batch), unsuitable for crash handlers, depends on tool availability on the user's machine.
- **Offline** — capture is cheap and works in any context including signal handlers and crash dumps; symbolication happens on a developer machine that has the debug info. Standard pattern for production crash reporting (Crashlytics, Sentry, Breakpad). Requires emitting and storing a serialized capture, plus an external tool to translate it.

A real engine wants all four. Debug builds default to the richest in-process option. Release builds capture cheaply and translate offline. Crash handlers use capture-only. Developer tooling shells out to `llvm-symbolizer` for one-off introspection without rebuilding. None of these should be mutually exclusive — multiple symbolicators can be linked in simultaneously and chosen per call.

## Solution

Split the module into two orthogonal phases — **capture** and **symbolicate** — with a pluggable symbolicator backend.

Capture is always cheap: per-platform code records instruction pointers and a snapshot of the loaded-module table. The module table entry per loaded image carries `(load_address, size, path, identity)` where `identity` is the build-id (ELF), UUID (Mach-O), or `(timestamp, image_size)` (PE). Capture never allocates beyond the frame array, never blocks on I/O, and is safe to call from signal handlers and crash contexts.

Symbolicate is a separate, explicit call that takes a captured trace and fills in function names, filenames, lines, and columns using a selected backend. Four backends ship:

- **platform** — the current `dbghelp` / `dladdr` path. Always available, no extra deps.
- **libbacktrace** — vendored at `third-party/libbacktrace/`, reads DWARF in-process, replaces `dladdr` for translation while keeping the per-platform capture untouched.
- **subprocess** — spawns `llvm-symbolizer` (preferred, single tool for all platforms) with `atos` and `addr2line` as platform-specific fallbacks. Batches frames into one invocation per call.
- **offline** — no-op at runtime beyond capture; serializes the trace plus its module table to a text format that an external tool (`tools/melsym/`) resolves against a debug-symbol store.

Backend selection is build-time (which backends compile in) and runtime (which compiled-in backend is used per call). The current `mel_stacktrace_capture` becomes a convenience wrapper that captures and immediately symbolicates with the default backend, preserving the existing API.

## Implementation Decisions

The new public API:

```c
bool mel_stacktrace_capture_raw(Mel_Stacktrace* st, usize skip, size keep, Mel_Alloc* alloc);
bool mel_stacktrace_symbolicate(Mel_Stacktrace* st, Mel_Stacktrace_Symbolicator sym);
bool mel_stacktrace_serialize(const Mel_Stacktrace* st, Mel_Writer* sink);
bool mel_stacktrace_capture(Mel_Stacktrace* st, usize skip, size keep, Mel_Alloc* alloc);
```

`Mel_Stacktrace` grows a `modules` field (a small array of `Mel_Module_Info` referenced by frame index) and each `Mel_Stackframe` grows `module_index` and `module_offset` so a captured-only frame is still self-describing for offline resolution.

Symbolicator selection at build time goes through `stacktrace.cfg.h`:

- `MEL_STACKTRACE_SYMBOLICATORS` — bitmask of `MEL_STACKTRACE_SYM_PLATFORM | _LIBBACKTRACE | _SUBPROCESS | _OFFLINE`. Defaults: `PLATFORM | OFFLINE` for release, `PLATFORM | LIBBACKTRACE | SUBPROCESS | OFFLINE` for debug.
- `MEL_STACKTRACE_DEFAULT_SYMBOLICATOR` — which one `mel_stacktrace_capture` uses when called without explicit backend. Defaults: `LIBBACKTRACE` for debug, `PLATFORM` for release, `OFFLINE` if neither is compiled in.

Runtime override via `mel_stacktrace_set_default_symbolicator(Mel_Stacktrace_Symbolicator)`. A symbolicator that was not compiled in returns an error at call time rather than silently falling back; falling back masks misconfiguration.

Per-platform capture stays where it is — `modules/debug/src/win32/`, `modules/debug/src/macos/`, `modules/debug/src/android/`, `modules/debug/src/linux/` — but gains module-table snapshotting:

- **Linux/Android** — `dl_iterate_phdr` for load addresses and paths, parse `PT_NOTE` for the `NT_GNU_BUILD_ID` note.
- **macOS** — `_dyld_image_count` / `_dyld_get_image_header` / `_dyld_get_image_vmaddr_slide`, read `LC_UUID` from the Mach-O header.
- **Win32** — `EnumProcessModules` plus `GetModuleInformation`, read `IMAGE_NT_HEADERS.FileHeader.TimeDateStamp` and `OptionalHeader.SizeOfImage`.

Module snapshotting runs once at first capture and is refreshed on demand (a counter incremented by a small `dlopen`/`LoadLibrary` interposer; if the counter advanced since last snapshot, re-snapshot). Snapshot lives in module-static storage, not per-trace, so capture remains allocation-free for the table.

Backend implementations live in `modules/debug/src/sym/`:

- `sym_platform.c` — extracted from the current per-platform translate functions; on Win32 wraps `SymFromAddr` + `SymGetLineFromAddr64`, on POSIX wraps `dladdr`.
- `sym_libbacktrace.c` — single file, calls `backtrace_pcinfo(state, pc, cb, err_cb, data)` per frame, fills frame fields from the callback. One `backtrace_state*` per loaded module (cached), keyed by module path.
- `sym_subprocess.c` — spawns `llvm-symbolizer --obj=<path>` once per module touched by the trace, writes hex addresses to stdin, parses the `function\nfile:line:col\n` response format. Falls back to `atos -o <path> -l <load_addr>` on macOS and `addr2line -e <path> -f -C` on Linux/Android if `llvm-symbolizer` is not on `PATH`.
- `sym_offline.c` — symbolicator that always returns success without populating anything beyond what capture already filled in; `mel_stacktrace_serialize` is the consumer of offline-mode traces.

The serialization format is text, single-line records, designed to be diff-friendly and trivially parseable:

```
melstack 1
mod 0 path=/data/app/.../libmelody.so base=0x7f8a load=0x80000000 size=0x412000 buildid=a3c1...
mod 1 path=/system/lib64/libc.so base=0x7f8b load=0x7c000000 size=0x180000 buildid=...
frame 0 mod=0 off=0x12340 ip=0x80012340
frame 1 mod=0 off=0x4b210
frame 2 mod=1 off=0x21008
```

Binary serialization is a second concern; text is canonical and required, binary is opt-in via `mel_stacktrace_serialize_binary` once a need shows up.

libbacktrace vendoring: `third-party/libbacktrace/upstream/` mirrors upstream layout; `third-party/libbacktrace/config/<platform>/{config.h,backtrace-supported.h}` are hand-rolled to replace autoconf. The per-platform file set the wrapper compiles:

- common: `atomic.c dwarf.c fileline.c sort.c state.c posix.c mmapio.c mmap.c read.c simple.c`
- Linux/Android: add `elf.c`
- macOS: add `macho.c`
- Win32: add `pecoff.c`

`nob.c` gets a `libbacktrace` third-party target gated by the `MEL_STACKTRACE_SYMBOLICATORS` bitmask the user configures; if libbacktrace is not selected, the sources are not compiled and the third-party target is skipped.

The offline tool `melsym` lives at `tools/melsym/`. It accepts a serialized trace on stdin (or a file), takes one or more `--symbol-dir <path>` arguments naming directories to search for matching debug info (matched by build-id / UUID / `(timestamp, size)`), and prints the resolved trace. It is built like any other app, against the same `melody` library, so it can reuse the libbacktrace and DWARF code.

The `MEL_STACKTRACE_HAS_FUNCTION_NAMES` and `MEL_STACKTRACE_HAS_SOURCE_INFO` cfg toggles stay, but become advisory hints to symbolicators rather than promises. If `HAS_SOURCE_INFO` is on and the active symbolicator cannot deliver source info (e.g., `platform` backend on macOS), filename/line stay zero and `mel_stacktrace_symbolicate` still returns true — silent unavailability is the existing contract, and changing that is out of scope for this PRD.

## Testing Decisions

The most valuable test is end-to-end: from each test executable, capture a stacktrace at a known line, symbolicate with each compiled-in backend, and assert the resolved frames match the expected function names and source lines. The test runs across all four backends per platform that supports them. The platform backend always runs; libbacktrace and subprocess run if their build dependencies are present in CI; offline always runs and uses the in-tree `melsym` tool to translate.

Module-table snapshotting is tested in isolation: load a known shared library at runtime via `dlopen`/`LoadLibrary`, capture a frame inside it, verify the module table contains the expected entry with a matching build-id / UUID / timestamp.

Serialization round-trips through `mel_stacktrace_serialize` + `melsym` parse: serialize a capture, parse it back, assert the in-memory representation matches.

Out-of-process resolution is tested by the `melsym` tool's own test suite: feed it golden serialized traces plus a fixture symbol store, assert resolved output matches expected.

Modules under test: `modules/debug/src/sym/*.c` (each backend in isolation), `modules/debug/src/<platform>/stacktrace.c` (capture + module snapshot), `tools/melsym/` (parse + resolve).

## Out of Scope

- C++ demangling beyond what each backend provides natively (libbacktrace demangles, `llvm-symbolizer` demangles, `atos` demangles, `dladdr` does not). A separate `mel_demangle` shim is not part of this PRD.
- Symbol-server protocols (Microsoft Symbol Server, Breakpad's HTTP API). `melsym` reads from local directories; remote fetching is a future concern.
- Inlined-frame expansion. libbacktrace reports inlined frames; the API will surface them, but a richer model (e.g., a frame tree) is not part of this PRD — inlined frames appear as additional sequential frames marked with an `inlined_into` index.
- Crash-dump capture (minidump-equivalent, signal-handler-safe full state dump). Capture is signal-safe for stack traces; full process snapshot is a separate module.
- Symbol upload pipelines and CI integration for stashing dSYMs / debug `.so` files in a symbol store.

## Further Notes

The split into capture-and-symbolicate is the load-bearing decision. Once those are separate, every other choice — which backends to compile, which to default to, whether to ship debug info at runtime, whether to symbolicate eagerly or on the developer's machine — becomes an independent configuration knob rather than a fork in the codebase.

The reason all four backends coexist rather than collapsing into "always libbacktrace": libbacktrace cannot resolve frames in modules whose debug info is not present at runtime. On Windows, system DLLs ship PDB-only and libbacktrace's PE/COFF reader expects DWARF — `dbghelp` is strictly better for those frames. On Android production builds, debug info is stripped and not shipped — only the offline backend works there. The platform backend covers the "no symbols available, give me at least function names from the dynamic symbol table" case. Each backend is the right answer for a real scenario; collapsing them loses one of those scenarios.

The offline format is intentionally text rather than a binary protocol like Breakpad's minidump. Text is diffable in PRs, greppable in logs, and trivially constructed by other tools (a panic handler in a different language could write the same format). The binary form is reserved for cases where serialization size dominates — crash uploads from constrained devices — and is opt-in.

The runtime selection of symbolicator (rather than only build-time) exists because a single binary may want different backends for different traces: fast platform-native for telemetry traces, libbacktrace for an interactive debug command, offline serialization for a crash report. Forcing a single global choice per build would prevent that.
