# VFS — Implementation Status

This document tracks what's been built, what each file contains, test coverage,
and what's left. For the VFS design contract, see `vfs.md`.

---

## Files

### `melody/string.path.h` / `melody/string.path.c`

General-purpose path manipulation extracted from VFS. All functions are buffer-based (no allocation).

```c
str8 mel_path_normalize(str8 path, u8* buf, usize buf_cap);
str8 mel_path_join(str8 base, str8 relative, u8* buf, usize buf_cap);
str8 mel_path_parent(str8 path);
str8 mel_path_filename(str8 path);
str8 mel_path_extension(str8 path);
bool mel_path_is_absolute(str8 path);
```

Tests: `tests/test_string_path.c` (tag: `string`)

### `melody/async.io.*`

Generic I/O executor module. VFS registers a handler and delegates all threading, queuing, ticket management, fence/link semantics to this layer.

- `melody/async.io.fwd.h` — forward declarations (`Mel_Io`, `Mel_Io_Sqe`, `Mel_Io_Cqe`)
- `melody/async.io.cfg.h` — configuration macros
- `melody/async.io.h` — full API
- `melody/async.io.c` — implementation (worker threads, SQ/CQ rings, condvar sync)
- `tests/test_async_io.c` (tag: `async`)

Key API:
```c
typedef void (*Mel_Io_Execute_Fn)(void* ctx, const Mel_Io_Sqe* sqe, Mel_Io_Cqe* cqe);

bool  mel_io_init(Mel_Io* io, const Mel_Io_Desc* desc);
void  mel_io_shutdown(Mel_Io* io);
u16   mel_io_register_handler(Mel_Io* io, Mel_Io_Execute_Fn fn, void* ctx);
u64   mel_io_next_ticket(Mel_Io* io);
i32   mel_io_submit(Mel_Io* io, const Mel_Io_Sqe* sqes, i32 count);
i32   mel_io_poll(Mel_Io* io, Mel_Io_Cqe* out, i32 max_count);
bool  mel_io_wait(Mel_Io* io, i32 min_count, u32 timeout_ms);
bool  mel_io_poll_ticket(Mel_Io* io, u64 ticket, Mel_Io_Cqe* out);
bool  mel_io_wait_ticket(Mel_Io* io, u64 ticket, u32 timeout_ms, Mel_Io_Cqe* out);
```

`worker_count = 0` in `Mel_Io_Desc` → synchronous mode (dispatch on caller's thread inside `mel_io_submit`).

### `melody/vfs.fwd.h`

Forward declarations only. Any `.h` that needs VFS types without pulling in the full API includes this.

Includes `collection.slotmap.fwd.h` for `Mel_SlotMap_Handle`.

Forward-declared types:
- `Mel_Vfs`
- `Mel_Vfs_File`, `Mel_Vfs_Dir`, `Mel_Vfs_Map`, `Mel_Vfs_Watch` — each wraps `Mel_SlotMap_Handle`, with `_INVALID` sentinels using `MEL_SLOTMAP_HANDLE_NULL`
- `Mel_Vfs_Stat`
- `Mel_Vfs_Error`
- `Mel_IoVec`
- `Mel_Vfs_Dir_Entry`
- `Mel_Vfs_Sqe`, `Mel_Vfs_Cqe`
- `Mel_Vfs_Backend`
- `Mel_Vfs_Mount`

### `melody/vfs.cfg.h`

Configuration macros with ifdef/default pattern per MEL-STYLE-003.

- `MEL_VFS_MAX_PATH` (default: 4096)
- `MEL_VFS_MOUNT_TABLE_INITIAL` (default: 16)
- `MEL_VFS_RING_BUFFER_SIZE` (default: 64K — currently unused, kept for future ring-buffer path)
- `MEL_VFS_VALIDATE_HANDLES` (default: 1 in debug, 0 in release)

### `melody/vfs.async.h`

Level 1 core async API. This is the primary contract.

Includes: `vfs.cfg.h`, `vfs.fwd.h`, `async.io.fwd.h`, `string.str8.fwd.h`, `allocator.fwd.h`

Defines all constants:
- `MEL_VFS_OP_*` (OPEN, CLOSE, READ, WRITE, READV, WRITEV, MAP, UNMAP, STAT, DIR_OPEN, DIR_NEXT, DIR_NEXT_BATCH, DIR_CLOSE, WATCH_OPEN, WATCH_NEXT, WATCH_CLOSE, SYNC, CANCEL, RENAME, DELETE, MKDIR)
- `MEL_VFS_STATUS_*` (OK, PENDING, NOT_FOUND, EOF, PERMISSION, UNSUPPORTED, CANCELLED, IO_ERROR, INVALID_ARGUMENT, BUFFER_TOO_SMALL, TIMEOUT, ALREADY_EXISTS)
- `MEL_VFS_ERRCAT_*` (NONE, GENERIC, OS, BACKEND)
- `MEL_VFS_OPEN_*` (READ, WRITE, CREATE, TRUNCATE, DIRECT)
- `MEL_VFS_WATCH_*` (ADDED, MODIFIED, REMOVED, RENAMED)
- `MEL_VFS_PRIORITY_*` (LOW=0, NORMAL=128, HIGH=255)
- `MEL_VFS_QOS_*` (LATENCY_CRITICAL, STREAMING, BULK, COUNT)
- `MEL_VFS_SQE_F_*` (FENCE, LINK_NEXT, DONT_BLOCK)
- `MEL_VFS_MAP_*` (READ, WRITE)

Full struct definitions:
- `Mel_Vfs_Sqe` — ticket, op, flags, priority, qos_class, deadline_ns, user_data, tagged union
- `Mel_Vfs_Cqe` — ticket, op, status, result, error, user_data, tagged union
- `Mel_Vfs_Desc` — `{ const Mel_Alloc* allocator; Mel_Io* io; }`

Note: handle types, `Mel_IoVec`, `Mel_Vfs_Stat`, `Mel_Vfs_Error`, `Mel_Vfs_Dir_Entry` are defined in `vfs.fwd.h`.

Functions:
- `mel_vfs_init(Mel_Vfs*, const Mel_Vfs_Desc*)` → bool
- `mel_vfs_shutdown(Mel_Vfs*)`
- `mel_vfs_next_ticket(Mel_Vfs*)` → u64 (delegates to `mel_io_next_ticket`)
- `mel_vfs_submit(Mel_Vfs*, const Mel_Vfs_Sqe*, i32 count)` → i32 (heap-allocates per-op, converts to Mel_Io_Sqe, delegates to mel_io_submit)
- `mel_vfs_poll(Mel_Vfs*, Mel_Vfs_Cqe*, i32 max_count)` → i32
- `mel_vfs_wait(Mel_Vfs*, i32 min_count, u32 timeout_ms)` → bool
- `mel_vfs_poll_ticket(Mel_Vfs*, u64 ticket, Mel_Vfs_Cqe*)` → bool
- `mel_vfs_wait_ticket(Mel_Vfs*, u64 ticket, u32 timeout_ms, Mel_Vfs_Cqe*)` → bool
- `mel_vfs_map_ptr(Mel_Vfs*, Mel_Vfs_Map, usize* out_size)` → void*

### `melody/vfs.async.inl`

Inline helpers for hot-path validation and convenience. Delegates to `mel_slotmap_handle_valid()`.

- `mel_vfs_file_valid(Mel_Vfs_File)` → bool
- `mel_vfs_dir_valid(Mel_Vfs_Dir)` → bool
- `mel_vfs_map_valid(Mel_Vfs_Map)` → bool
- `mel_vfs_watch_valid(Mel_Vfs_Watch)` → bool

### `melody/vfs.async.c`

Core VFS implementation. The VFS is a thin handler registered with `Mel_Io`.

- **Init:** creates SDL_Mutex `state_lock`, initializes 4 slotmaps (file/dir/map/watch), registers handler with Mel_Io
- **Submit:** heap-allocates a `Mel_Vfs__Op` per SQE, copies paths and iov arrays into per-op owned memory (RENAME packs both paths into one buffer `[src\0dst\0]`), converts to `Mel_Io_Sqe` with `op_data = op`, delegates to `mel_io_submit`. Frees rejected ops.
- **Handler callback (`mel__vfs_io_handler`):** called by Mel_Io workers; uses three-phase execution:
  - Phase 0 (no lock): path normalization + escape check for path-bearing ops
  - Phase 1 (lock): `mel__vfs_resolve` — slotmap lookups, mount resolution, permission checks, populates `Mel_Vfs__Op_Ctx`
  - Phase 2 (no lock): `mel__vfs_dispatch` — backend I/O using resolved context
  - Phase 3 (lock, conditional): `mel__vfs_commit` — slotmap inserts + mount refcount updates (only if `needs_commit && status == OK`)
- **CQE conversion (`mel__vfs_convert_cqe`):** extracts VFS CQE from the op's result, then frees the op and its owned allocations
- **Poll/wait:** delegate to `mel_io_poll`/`mel_io_wait`/`mel_io_poll_ticket`/`mel_io_wait_ticket`, converting each `Mel_Io_Cqe` back to `Mel_Vfs_Cqe`
- **Mount resolution (`mel__vfs_find_mount`):** scans mount array with priority desc → prefix_len desc → insertion_order asc tie-break
- **Path normalization:** delegates to `mel_path_normalize()` from `string.path.h`
- **Handle lifecycle:** OPEN inserts into slotmap + increments mount refcount; CLOSE removes from slotmap + decrements refcount; if mount is retired and refcount hits 0, mount prefix memory is freed immediately
- **Watch sessions:** WATCH_OPEN/WATCH_NEXT/WATCH_CLOSE delegate to backend; NULL backend watch ptrs → UNSUPPORTED
- **Cancellation:** `MEL_VFS_OP_CANCEL` delegates to `mel_io_cancel` (best-effort cancellation)
- **FENCE/LINK_NEXT:** handled by Mel_Io layer, not VFS
- **DONT_BLOCK:** defined but not yet implemented
- **try_submit_native:** only checked for data I/O ops (READ, WRITE, READV, WRITEV, SYNC). Metadata ops (STAT, OPEN, RENAME, DELETE, MKDIR, DIR_*, WATCH_*) always use synchronous backend dispatch — these are typically fast kernel calls where native async submission (io_uring, IOCP) adds overhead without meaningful benefit
- **map_ptr:** locks state_lock, looks up map slotmap, returns pointer + size

### `melody/vfs.h`

Level 2 ergonomic API + mount lifecycle + VFS internal struct definition.

Includes: `vfs.async.h`, `vfs.backend.h`, `async.io.h`, `allocator.h`, `collection.array.h`, `collection.slotmap.h`, `string.str8.fwd.h`

Internal data types (visible, not opaque):
- `Mel_Vfs__File_Data` — `{ Mel_Vfs_Backend* backend; Mel_Vfs_Native_Handle native_handle; u32 mount_generation; u32 mount_index; }`
- `Mel_Vfs__Dir_Data` — `{ Mel_Vfs_Backend* backend; Mel_Vfs_Native_Handle native_handle; }`
- `Mel_Vfs__Map_Data` — `{ void* ptr; usize size; Mel_Vfs_Backend* backend; Mel_Vfs_Native_Handle file_native_handle; }`
- `Mel_Vfs__Watch_Data` — `{ Mel_Vfs_Backend* backend; Mel_Vfs_Native_Handle native_handle; }`
- `Mel_Vfs__Op` — `{ Mel_Vfs_Sqe sqe; Mel_Vfs_Cqe cqe; u8* owned_path_data; Mel_IoVec* owned_iov; u8 inline_path[256]; Mel_IoVec inline_iov[8]; }` — heap-allocated per submission, freed on CQE extraction. Short paths (< 256 bytes) and small iov arrays (≤ 8 entries) avoid heap allocation by using the inline buffers.

Types:
- `Mel_Vfs_Mount` — `{ str8 prefix; Mel_Vfs_Backend* backend; u8 priority; bool writable; u32 refcount; bool retired; u32 insertion_order; }`
- `struct Mel_Vfs` — `{ const Mel_Alloc* alloc; Mel_Io* io; u16 handler_id; SDL_Mutex* state_lock; Mel_SlotMap file_slots/dir_slots/map_slots/watch_slots; Mel_Array(Mel_Vfs_Mount) mounts; u32 mount_generation; u32 mount_insertion_counter; }`
- `Mel_Vfs_Mount_Opt` — `{ Mel_Vfs* vfs; u8 priority; bool writable; }` (`vfs == NULL` uses module-static instance)
- `Mel_Vfs_Enum_Cb` — `bool (*)(str8 virtual_path, const Mel_Vfs_Stat* stat, void* user)`
- `Mel_Vfs_Enum_Opt` — `{ bool recursive; bool include_dirs; }`

Functions:
- `mel_vfs_instance(void)` → `Mel_Vfs*` (module-static VFS instance)
- `mel_vfs_mount_opt(str8 prefix, Mel_Vfs_Backend* backend, Mel_Vfs_Mount_Opt)` — convenience mount API using module statics by default
- `mel_vfs_mount(prefix, backend, ...)` (macro with _opt pattern)
- `mel_vfs_unmount(str8 prefix)` — convenience unmount on module-static VFS
- `mel_vfs_mount_ex(Mel_Vfs*, str8 prefix, Mel_Vfs_Backend*, u8 priority, bool writable)` — explicit instance API
- `mel_vfs_unmount_ex(Mel_Vfs*, str8 prefix)` — explicit instance API
- `mel_vfs_read_file_alloc(Mel_Vfs*, str8 path, usize* out_size, const Mel_Alloc*)` → u8*
- `mel_vfs_read_text_alloc(Mel_Vfs*, str8 path, const Mel_Alloc*)` → str8
- `mel_vfs_write_file(Mel_Vfs*, str8 path, const u8* data, usize size)` → bool
- `mel_vfs_write_text(Mel_Vfs*, str8 path, str8 text)` → bool
- `mel_vfs_exists(Mel_Vfs*, str8 path)` → bool
- `mel_vfs_stat_sync(Mel_Vfs*, str8 path, Mel_Vfs_Stat*)` → bool
- `mel_vfs_sync_file(Mel_Vfs*, Mel_Vfs_File)` → bool
- `mel_vfs_rename(Mel_Vfs*, str8 src, str8 dst)` → bool
- `mel_vfs_delete(Mel_Vfs*, str8 path)` → bool
- `mel_vfs_mkdir(Mel_Vfs*, str8 path)` → bool
- `mel_vfs_enumerate_opt(Mel_Vfs*, str8 path, Mel_Vfs_Enum_Cb, void*, Mel_Vfs_Enum_Opt)` → bool
- `mel_vfs_enumerate(vfs, path, cb, user, ...)` (macro with _opt pattern)

### `melody/vfs.c`

Level 2 implementation + mount lifecycle.

- Mount registry: `Mel_Array(Mel_Vfs_Mount)`, unsorted (mount resolution scans all non-retired entries)
- Mount: normalizes prefix via `mel_path_normalize`, dupes into allocator, pushes to array, increments generation
- Unmount: marks mount as retired, increments generation; if refcount == 0, frees prefix immediately
- Mount generation counter: u32, incremented on mount/unmount (under state_lock)
- Blocking wrappers: `mel__vfs_blocking_op` — submit SQE, call `mel_vfs_wait_ticket` for owned ticket
- Enumerate: `mel__vfs_enumerate_dir` recursively walks via DIR_OPEN → DIR_NEXT loop → DIR_CLOSE, building full virtual paths with `mel_path_join`, supports `opt.recursive` and `opt.include_dirs`, early exit on callback returning false

### `melody/vfs.backend.h`

Backend vtable definition. Internal header but fully designed — this is the pluggability contract.

```c
typedef usize Mel_Vfs_Native_Handle;

typedef struct Mel_Vfs_Backend {
    i32  (*open)(Mel_Vfs_Backend*, str8 path, u32 open_flags, Mel_Vfs_Native_Handle* out_native_handle);
    void (*close)(Mel_Vfs_Backend*, Mel_Vfs_Native_Handle native_handle);
    i64  (*read)(Mel_Vfs_Backend*, Mel_Vfs_Native_Handle native_handle, u64 offset, void* dst, usize size);
    i64  (*write)(Mel_Vfs_Backend*, Mel_Vfs_Native_Handle native_handle, u64 offset, const void* src, usize size);
    i64  (*readv)(Mel_Vfs_Backend*, Mel_Vfs_Native_Handle native_handle, u64 offset, Mel_IoVec* iov, usize iov_cnt);
    i64  (*writev)(Mel_Vfs_Backend*, Mel_Vfs_Native_Handle native_handle, u64 offset, const Mel_IoVec* iov, usize iov_cnt);
    i32  (*stat)(Mel_Vfs_Backend*, str8 path, Mel_Vfs_Stat* out);
    i32  (*dir_open)(Mel_Vfs_Backend*, str8 path, Mel_Vfs_Native_Handle* out_native_handle);
    i32  (*dir_next)(Mel_Vfs_Backend*, Mel_Vfs_Native_Handle native_handle, u8* name_buf, usize name_cap, usize* out_name_len, Mel_Vfs_Stat* out_stat);
    void (*dir_close)(Mel_Vfs_Backend*, Mel_Vfs_Native_Handle native_handle);
    i32  (*map)(Mel_Vfs_Backend*, Mel_Vfs_Native_Handle native_handle, u64 offset, usize size, u32 flags, void** out_ptr);
    void (*unmap)(Mel_Vfs_Backend*, void* ptr, usize size);
    i32  (*watch_open)(Mel_Vfs_Backend*, str8 path, bool recursive, u32 flags, Mel_Vfs_Native_Handle* out_native_handle);
    i32  (*watch_next)(Mel_Vfs_Backend*, Mel_Vfs_Native_Handle native_handle, i32 timeout_ms, u8* path_buf, usize path_cap, usize* out_path_len, i32* out_action);
    void (*watch_close)(Mel_Vfs_Backend*, Mel_Vfs_Native_Handle native_handle);
    i32  (*sync)(Mel_Vfs_Backend*, Mel_Vfs_Native_Handle native_handle);
    i32  (*rename)(Mel_Vfs_Backend*, str8 old_path, str8 new_path);
    i32  (*remove)(Mel_Vfs_Backend*, str8 path);
    i32  (*mkdir)(Mel_Vfs_Backend*, str8 path);
    bool (*try_submit_native)(Mel_Vfs_Backend*, struct Mel_Vfs_Sqe* sqe, Mel_Vfs_Native_Handle h, Mel_Vfs* vfs, void* op);
    void (*destroy)(Mel_Vfs_Backend*);
    void* impl_data;
} Mel_Vfs_Backend;
```

All ops return 0/positive on success, negative on error. Read/write return bytes transferred.

`dir_next` and `watch_next` receive explicit buffer + capacity + out-length parameters. Backend writes name/path bytes into the provided buffer up to capacity. If the buffer is too small, backend returns negative and sets `*out_name_len` / `*out_path_len` to the required size — the VFS layer translates this to `MEL_VFS_STATUS_BUFFER_TOO_SMALL` with `result = required_bytes`. The pending entry/event is NOT consumed on buffer-too-small, making retry safe. The VFS layer assembles the final `Mel_Vfs_Dir_Entry` (setting `name.data = name_buf`, `name.len = *out_name_len`) after a successful backend call.

Backends may leave watch function pointers null if unsupported; VFS maps that to `MEL_VFS_STATUS_UNSUPPORTED`. Backend does NOT own handle generation logic — that's VFS-level. `native_handle` is backend-local and pointer-width safe.

### `melody/vfs.backend.os.h`

OS filesystem backend creation/destruction.

- `mel_vfs_backend_os(void)` → `Mel_Vfs_Backend*` (module-static singleton backend used by app-level convenience paths)
- `mel_vfs_backend_os_create(const Mel_Alloc*, str8 root_path)` → Mel_Vfs_Backend* (explicit backend instance for tests/tools/custom roots)
- `mel_vfs_backend_os_destroy(Mel_Vfs_Backend*)`

`root_path` is the physical directory this backend maps to. All paths are resolved relative to it.

### `melody/vfs.backend.os.c`

POSIX implementation (macOS + Linux). Win32 behind `#if MEL_PLATFORM_WINDOWS` for later.

- open/close → `open(2)` / `close(2)`
- read/write → `pread(2)` / `pwrite(2)` (positioned, no lseek state)
- readv/writev → `preadv(2)` / `pwritev(2)`
- stat → `stat(2)` / `fstat(2)` mapped into `Mel_Vfs_Stat` (`mtime_ns`/`change_ns`; `birth_ns` when available, else 0 and no `HAS_BIRTH_TIME`)
- dir_open/dir_next/dir_close → `opendir` / `readdir` / `closedir`
- map/unmap → `mmap(2)` / `munmap(2)` with MAP_READ/MAP_WRITE → PROT_READ/PROT_WRITE
- sync → `fsync(2)`
- rename → `rename(2)`
- remove → `remove(3)`
- mkdir → `mkdir(2, 0755)`
- watch_open/watch_next/watch_close → implemented on Apple (`kqueue` / EVFILT_VNODE, non-recursive); unsupported on non-Apple POSIX in v1

### `melody/vfs.backend.mock.h`

In-memory mock backend for testing.

- `mel_vfs_backend_mock_create(const Mel_Alloc*)` → Mel_Vfs_Backend*
- `mel_vfs_backend_mock_destroy(Mel_Vfs_Backend*)`
- `mel_vfs_backend_mock_inject_file(Mel_Vfs_Backend*, str8 path, const u8* data, usize size)` — pre-inject file data
- `mel_vfs_backend_mock_inject_dir(Mel_Vfs_Backend*, str8 path)` — pre-inject directory entry

Future additions (not yet implemented):
- scripted error injection per path+op
- artificial latency injection
- forced completion ordering for race reproduction

### `melody/vfs.backend.mock.c`

Simple flat entry array. Supports open/close/read/write/readv/writev/stat/dir_open/dir_next/dir_close/map/unmap/sync/rename/remove/mkdir. Watch pointers are NULL (maps to UNSUPPORTED). Dir enumeration returns direct children only — recursive behavior is handled by the VFS layer's enumerate helper. Mock `remove` zeroes the entry in-place (avoids swap-remove to keep `open_files` entry indices stable); `path_eq`, `is_child_of`, and `find_entry` all null-check `path_data` to skip deleted entries. Known limitation: deleted entries become permanent zombie slots in the array — entry count only grows, never shrinks. This is acceptable for test workloads but would degrade O(n) scans in long-running mock sessions.

---

## Tests

`tests/test_vfs.c` — All VFS tests against mock backend (~65 tests, tags: `vfs`, `vfs, async`).
- write/read round-trip, binary data, overwrite
- injected file reads
- exists / stat_sync
- read nonexistent / empty file
- enumerate: flat, skip dirs, include_dirs, recursive, recursive + include_dirs, empty dir, early stop
- async submit/poll: stat, open/read/close, threaded write/read
- mount: priority, prefix specificity, readonly enforcement, insertion order tie-break, no mount returns NOT_FOUND
- positioned I/O: read at non-zero offset, write at non-zero offset, read past EOF
- scatter/gather: readv/writev round-trip
- dir_next_batch
- map/unmap + mel_vfs_map_ptr
- sync op
- ticket monotonicity
- submit chain (LINK_NEXT)
- path normalization: dot, dotdot, double slash, backslash, trailing slash, root escape, pure dotdot, empty, just slash, complex
- watch unsupported (mock has no watch impl)
- cancel op success path
- invalid op → INVALID_ARGUMENT
- user_data passthrough
- open nonexistent without CREATE → NOT_FOUND
- multiple open/close cycles (handle reuse)
- poll nonexistent ticket
- rename: basic round-trip, cross-mount error (INVALID_ARGUMENT), nonexistent source, readonly mount (PERMISSION), async happy path (raw submit/poll), path escape on src, path escape on dst, dst-mount-readonly (same backend, src writable/dst readonly)
- delete: file, nonexistent (failure), directory, readonly mount (PERMISSION), async happy path (raw submit/poll), path escape
- mkdir: basic + stat IS_DIR, already exists (ALREADY_EXISTS), readonly mount (PERMISSION), async happy path (raw submit/poll), path escape

`tests/test_vfs_os.c` — OS backend tests with real temp directory (tags: `vfs`).
- write/read round-trip
- stat + size verification
- nonexistent file returns NOT_FOUND
- directory enumerate (flat, with include_dirs)
- subdirectory enumerate
- overwrite
- mmap read + unmap
- sync
- threaded write/read (worker_count = 2)
- positioned I/O (read at offset)
- dir_next buffer-too-small retry
- rename: real file rename + verify old gone / new readable
- delete: real file delete + verify gone
- mkdir: real dir creation + stat IS_DIR
- Apple watch tests (modify/delete/timeout/small-buffer-retry/recursive-unsupported/close)

Future test files (aspirational, not yet written):
- stress: concurrent mixed ops, handle churn, starvation measurement
- mock scripting: once error injection / latency / forced ordering are implemented

---

## Delete (after migration)

- `melody/assets.h`
- `melody/assets.c`

## Modify

- `melody/texture.c` / `melody/texture.h`
- `melody/gpu.texture.c` / `melody/gpu.texture.h`
- `melody/font.atlas.c` / `melody/font.atlas.h`
- `melody/sprite.sheet.c` / `melody/sprite.sheet.h`
- `melody/texture.atlas.c` / `melody/texture.atlas.h`
- `melody/tile.set.c` / `melody/tile.set.h`
- `melody/tile.map.c` / `melody/tile.map.h`
- `game/main.c`
- `nob.c` (remove PhysFS link)
- docs (`todo.md`, `CLAUDE.md`)

---

## Execution History

The VFS was originally built as a monolith (~1048 lines in `vfs.async.c`), then decomposed:

1. **Phase 1 — 64-bit slotmap handles:** `Mel_SlotMap_Handle` became `{ u32 index; u32 generation; }`. All 6 domain handle consumers migrated to `{ Mel_SlotMap_Handle handle; }` wrapper pattern.
2. **Phase 2 — Extract `string.path.*`:** Path normalization, join, parent, filename, extension, is_absolute extracted from VFS into reusable module.
3. **Phase 3 — Extract `async.io.*`:** Generic I/O executor with worker threads, SQ/CQ rings, condvar sync, fence/link semantics, ticket management.
4. **Phase 4 — Thin VFS refactor:** VFS gutted of threading, ring buffers, handle tables. Now registers handler with Mel_Io, uses slotmaps for handles, heap-allocates per-op, delegates all queuing to Mel_Io.
5. **Phase 5 — Test updates:** All tests updated for new init pattern (Mel_Io + Mel_Vfs), new test coverage added. 819/819 tests passing.
6. **Phase 6 — Assets migration complete:** All consumers (`texture`, `texture.pool`, `texture.atlas`, `tile.set`, `tile.map`, `sprite.sheet`, `game/main.c`) migrated from `Mel_Assets`/PhysFS to `Mel_Vfs`. PhysFS dependency removed from linker. `assets.h`/`assets.c` deleted. 822/822 tests passing.
7. **Phase 7 — Error reporting fix:** Added `mel__vfs_fill_error` and `mel__vfs_errno_to_status` helpers. All backend error paths now use errno-based error reporting (`MEL_VFS_ERRCAT_OS`). STAT/OPEN error mapping refined: ENOENT/ENOTDIR → NOT_FOUND, EACCES/EPERM → PERMISSION, else IO_ERROR. Mock backend error values changed from ad-hoc magic numbers to real errno constants.
8. **Phase 8 — Lock granularity refactor:** Replaced monolithic `mel__vfs_execute_op` (held `state_lock` during all backend I/O) with three-phase execution: resolve (lock) → dispatch (no lock) → commit (lock, conditional). Backend I/O now runs without holding `state_lock`, enabling concurrent VFS ops across worker threads. 73/73 vfs tests passing.
9. **Phase 9 — Filesystem mutation ops:** Added RENAME, DELETE, MKDIR operations across the full stack: op constants (19-21), SQE union members, backend vtable entries (rename/remove/mkdir), OS backend implementations (rename(2)/remove(3)/mkdir(2)), mock backend implementations, three-phase integration (resolve checks writable + same-backend for rename, dispatch calls backend, no commit needed), ergonomic wrappers (mel_vfs_rename/mel_vfs_delete/mel_vfs_mkdir). 14 new tests (11 mock + 3 OS). 854/854 tests passing.

10. **Phase 10 — Quality hardening:** Broadened `mel__vfs_errno_to_status` to map EEXIST → ALREADY_EXISTS (new status code 11), EXDEV/ENOTEMPTY/EISDIR → INVALID_ARGUMENT. Added null guard to `mel__mock_path_eq` (matching `is_child_of`). Added 8 new tests: async-level happy paths for rename/delete/mkdir (raw submit/poll), path-escape tests for rename src, rename dst, delete, mkdir, and rename dst-readonly test (same backend, src writable, dst readonly). `vfs_mkdir_already_exists` test upgraded to assert specific ALREADY_EXISTS status. 862/862 tests passing.

Remaining execution (future):
10. Implement QoS scheduling in Mel_Io.
11. Improve FENCE semantics in Mel_Io.
12. Expand watch backend support beyond Apple and add recursive watch support.

---

## Verification

1. `cc -o nob nob.c && ./nob test --tag vfs`
2. `./nob test`
3. `./nob build && ./nob run`
4. `rg -n "physfs|PHYSFS" melody nob.c`
5. `./nob demo breakout`

## Validation Matrix (AAA-level hardening)

- Stress: millions of mixed ops across threads (`OPEN/READ/WRITE/STAT/DIR`) with random cancellation.
- Fault injection: force backend errors (`ENOENT`, `EACCES`, `EIO`, short read/write, transient busy).
- Race tests: cancel vs completion, close vs in-flight read, mount/unmount vs open/read.
- Destination-buffer sizing: verify `BUFFER_TOO_SMALL` + required-byte reporting + retry behavior for dir/watch operations.
- Starvation tests: sustained bulk streaming while latency-critical requests measure tail latency.
- Handle safety: stale generation rejection and slot reuse correctness.
- Deterministic replay backend: scripted completion ordering to reproduce rare races.
- Performance baselines: queue throughput, p99 latency, batched dir listing scalability.

---

## v1 Scope and Deferred Items

Implemented in v1:
- async-first queue API (SQE/CQE) backed by `Mel_Io` executor,
- explicit handle-based file and dir operations with generational slotmap handles,
- callback enumerate (flat + recursive + include_dirs) plus explicit single-entry and batched dir iteration,
- OS backend + mock backend + mount priorities with deferred unmount cleanup,
- filesystem mutation ops (rename, delete, mkdir) with backend, async, and ergonomic layers,
- sync helper wrappers on top (read_file_alloc, write_file, exists, stat_sync, sync_file, rename, delete, mkdir, enumerate),
- path normalization extracted to `string.path.*`,
- generic I/O executor extracted to `async.io.*`,
- FENCE and LINK_NEXT semantics (handled by Mel_Io layer),
- per-op heap allocation with automatic cleanup on CQE extraction.

Partially implemented (API present, behavior stubbed or incomplete):
- `DONT_BLOCK` flag: defined but not honored,
- QoS scheduling: fields forwarded to Mel_Io but no lane dispatch, priority ordering, or deadline promotion implemented,
- watch: API fully designed; Apple OS backend implements non-recursive watch, non-Apple POSIX and mock backend still return UNSUPPORTED,
- mock backend scripting: only supports inject_file/inject_dir; no error injection, latency simulation, or forced completion ordering yet.

Deferred:
- QoS lane scheduling with in-flight budgets and deadline promotion,
- native io_uring backend,
- native IOCP backend,
- file watching/hot-reload OS implementation on non-Apple targets and recursive watches,
- archive backend,
- network backend,
- rich mock backend scripting (error injection, latency, forced ordering).
