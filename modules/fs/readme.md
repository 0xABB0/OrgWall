# fs

Async-first filesystem. Every metadata or bulk operation returns a `Mel_Future*` lowered onto a
worker-thread pool bound to a `Mel_Vat` — the doctrine's executor-offload discipline for objects
readiness cannot represent. The completion is an intrusive `Mel_Task` embedded in the op record,
posted back to the vat (`mel_vat_post` rings the doorbell); the loop turn resolves the future and
runs the continuation on the caller's `deliver` executor (default `mel_vat_executor`). In-flight
ops retain the vat (`mel_vat_retain`/`release`), so `mel_vat_run` stays live until completions
drain. Synchronous folder/cwd helpers are thin shims that do not touch the pool.

## Why it exists

The vat/port substrate is readiness-based (poll IN/OUT) — it serves sockets and pipes, not
blocking filesystem syscalls (`stat`, `mkdir`, `readdir`, `rename`, `copy`). `fs` carries its own
worker pool that runs those blocking calls off the loop and hands results back through
`mel_vat_post`, so a single-threaded vat never blocks on disk.

## Surface

- `fs/fs.h` — `Mel_Fs` object, `Mel_Fs_Status` bitset, `Mel_Fs_Kind` bits, `Mel_Fs_Stat`,
  generation-checked `Mel_Fs_Op` cancel handle, and the async ops: `stat`, `exists`, `mkdir`
  (recursive), `remove` (recursive), `rename`, `copy` (atomic), `read_file`, `write_file` (atomic).
- `fs/dir.h` — async `enumerate` (optional streaming batch callback) and `glob` (`*`/`?`, optional
  case-insensitive and recursive); `mel_fs_glob_match` exposed for in-memory matching.
- `fs/paths.h` — synchronous standard locations (`base`, `pref`, `home`, `desktop`, `documents`,
  `downloads`, `music`, `pictures`, `videos`, `templates`, `saved games`, `screenshots`, `cache`,
  `temp`), `cwd`, `chdir`, and `mel_fs_pref_identity` to seed the pref/app-data leaf.

All paths are `str8`; every allocation flows through a caller-supplied `const Mel_Alloc*`. Failure is
a status bitset (severity + cause flags + `os_error`), never a string; debug builds assert
loop-thread affinity.

## Backends

- **macos / ios** — POSIX `stat`/`openat`-family blocking ops; `NSSearchPathForDirectoriesInDomains`
  + `NSHomeDirectory` + bundle resource path for standard locations (ARC, `-framework Foundation`).
- **linux** — POSIX ops + XDG base-dir spec (`XDG_*_DIR`, `XDG_DATA_HOME`, `/proc/self/exe` for base).
- **android** — POSIX ops on the app sandbox; JNI `Context.getFilesDir`/`getCacheDir`/
  `getExternalFilesDir` for standard locations (depends on `platform`).
- **win32** — wide-char `CreateFileW`/`GetFileAttributesExW`/`FindFirstFileW`, `MoveFileExW`/
  `CopyFileW`, `SHGetKnownFolderPath` (`-lshell32 -lole32`).
- **wasm** — emscripten MEMFS/OPFS via the POSIX ops; browser-absent standard folders report
  `MEL_FS_UNAVAILABLE` honestly.

## Dependencies

`core`, `allocator`, `collection`, `string`, `executor`, `future`, `vat`, `thread`, `log`,
`platform` (android JNI bridge).
