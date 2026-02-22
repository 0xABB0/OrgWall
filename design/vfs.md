# VFS Module: Async-First, OS-Style, Layered API

## Context

The engine currently uses `assets.h/c` as a thin wrapper around PhysFS. Problems:

- `assets.c` has fixed path buffers and ad-hoc string conversions.
- VFS behavior is tied to one dependency (`PhysFS`) instead of owned by Melody.
- Different systems load files differently (`mel_assets_read`, `SDL_IOFromFile`, raw `stbi_load`).
- No proper async I/O contract for future hot-reload and streaming work.

`todo.md` already calls this out:
- `[OLD FILES] assets.* needs restructuring into VFS + async I/O`

This plan moves to an async-first VFS and keeps a clean split:
- low-level API for maximum control (OS-style queue model),
- high-level API for ergonomic one-shot usage.

## What We Learn From Existing APIs

### io_uring / IOCP patterns to adopt

- Submit work through queue entries, consume completions through completion entries.
- Keep an opaque user token per request (`user_data`) echoed in completion.
- Separate submit from wait/poll to support frame-loop integration and batching.

### Triple-A Requirements (The "Mog" Factor)

- **Zero-Copy / Memory Mapping:** Large assets (textures, geometry) should be mapped, not read.
- **Prioritization:** Streaming LODs requires priority queues. Foreground > Background.
- **Scatter/Gather:** Read headers and bodies into separate buffers in one syscall.
- **Path Safety:** Async submissions must own their path data. The VFS copies paths internally during submission.
- **Hot-Reload:** File watching is a first-class citizen of the VFS, not a separate hack.

## Layered Architecture

### Level 0: Backend Driver Interface (internal)

Internal backend contract that can be implemented by:
- OS filesystem backend (v1),
- zip/archive backend (future),
- memory backend (future),
- network backend (future).

This layer is explicit about open/read/write/stat/dir operations and does not hide allocations.

### Level 1: Core Async VFS API (public, low-level)

Primary API exposed to the engine:
- queue submission (`SQE`) and completion (`CQE`),
- explicit file/dir handles,
- positioned I/O (`offset`),
- caller-owned buffers for read/write.

This is the "control" layer and the source of truth.

### Level 2: Ergonomic API (public, high-level)

Thin wrappers over Level 1:
- read whole file into allocated buffer,
- write whole file from buffer,
- read text/write text,
- callback-based directory walk.

No hidden thread model changes. These wrappers only compose Level 1 calls.

## Core Async API Shape (`melody/vfs.async.h`)

```c
#define MEL_VFS_OP_OPEN      1
#define MEL_VFS_OP_CLOSE     2
#define MEL_VFS_OP_READ      3
#define MEL_VFS_OP_WRITE     4
#define MEL_VFS_OP_READV     5  // Scatter/Gather Read
#define MEL_VFS_OP_WRITEV    6  // Scatter/Gather Write
#define MEL_VFS_OP_MAP       7  // mmap
#define MEL_VFS_OP_UNMAP     8  // munmap
#define MEL_VFS_OP_STAT      9
#define MEL_VFS_OP_DIR_OPEN  10
#define MEL_VFS_OP_DIR_NEXT  11
#define MEL_VFS_OP_DIR_NEXT_BATCH 12
#define MEL_VFS_OP_DIR_CLOSE 13
#define MEL_VFS_OP_WATCH_OPEN  14 // begin watch subscription
#define MEL_VFS_OP_WATCH_NEXT  15 // fetch one watch event
#define MEL_VFS_OP_WATCH_CLOSE 16 // end watch subscription
#define MEL_VFS_OP_SYNC        17 // fsync equivalent
#define MEL_VFS_OP_CANCEL      18

#define MEL_VFS_STATUS_OK          0
#define MEL_VFS_STATUS_PENDING     1
#define MEL_VFS_STATUS_NOT_FOUND   2
#define MEL_VFS_STATUS_EOF         3
#define MEL_VFS_STATUS_PERMISSION  4
#define MEL_VFS_STATUS_UNSUPPORTED 5
#define MEL_VFS_STATUS_CANCELLED   6
#define MEL_VFS_STATUS_IO_ERROR    7
#define MEL_VFS_STATUS_INVALID_ARGUMENT 8
#define MEL_VFS_STATUS_BUFFER_TOO_SMALL 9
#define MEL_VFS_STATUS_TIMEOUT     10

#define MEL_VFS_ERRCAT_NONE     0
#define MEL_VFS_ERRCAT_GENERIC  1
#define MEL_VFS_ERRCAT_OS       2
#define MEL_VFS_ERRCAT_BACKEND  3

#define MEL_VFS_OPEN_READ      (1u << 0)
#define MEL_VFS_OPEN_WRITE     (1u << 1)
#define MEL_VFS_OPEN_CREATE    (1u << 2)
#define MEL_VFS_OPEN_TRUNCATE  (1u << 3)
#define MEL_VFS_OPEN_DIRECT    (1u << 4) // O_DIRECT / FILE_FLAG_NO_BUFFERING

#define MEL_VFS_MAP_READ       (1u << 0)
#define MEL_VFS_MAP_WRITE      (1u << 1)

// Watch Actions (returned in CQE result for WATCH_NEXT op)
#define MEL_VFS_WATCH_ADDED    1
#define MEL_VFS_WATCH_MODIFIED 2
#define MEL_VFS_WATCH_REMOVED  3
#define MEL_VFS_WATCH_RENAMED  4

// Priority levels for streaming
#define MEL_VFS_PRIORITY_LOW      0
#define MEL_VFS_PRIORITY_NORMAL   128
#define MEL_VFS_PRIORITY_HIGH     255

#define MEL_VFS_QOS_LATENCY_CRITICAL 0
#define MEL_VFS_QOS_STREAMING        1
#define MEL_VFS_QOS_BULK             2

#define MEL_VFS_SQE_F_FENCE      (1u << 0)
#define MEL_VFS_SQE_F_LINK_NEXT  (1u << 1)
#define MEL_VFS_SQE_F_DONT_BLOCK (1u << 2)

typedef struct {
    u32 slot;
    u32 generation;
} Mel_Vfs_File;
#define MEL_VFS_FILE_INVALID ((Mel_Vfs_File){0})

typedef struct {
    u32 slot;
    u32 generation;
} Mel_Vfs_Dir;
#define MEL_VFS_DIR_INVALID ((Mel_Vfs_Dir){0})

typedef struct {
    u32 slot;
    u32 generation;
} Mel_Vfs_Map;
#define MEL_VFS_MAP_INVALID ((Mel_Vfs_Map){0})

typedef struct {
    u32 slot;
    u32 generation;
} Mel_Vfs_Watch;
#define MEL_VFS_WATCH_INVALID ((Mel_Vfs_Watch){0})

typedef struct Mel_IoVec {
    void* buffer;
    usize len;
} Mel_IoVec;

typedef struct {
    u16 category;     // MEL_VFS_ERRCAT_*
    u16 backend_id;   // backend instance/type id
    i32 code;         // backend-defined error code
    i32 native_code;  // errno/GetLastError/etc
} Mel_Vfs_Error;

typedef struct {
    u64 size;
    u64 mtime_ns;    // content modification, nanoseconds since epoch
    u64 change_ns;   // metadata/inode change, nanoseconds since epoch
    u64 birth_ns;    // creation time, 0 if unavailable on backend/platform
    u32 flags;       // MEL_VFS_STAT_IS_FILE, _IS_DIR, _IS_SYMLINK, _IS_READONLY, _HAS_BIRTH_TIME
} Mel_Vfs_Stat;

#define MEL_VFS_STAT_IS_FILE     (1u << 0)
#define MEL_VFS_STAT_IS_DIR      (1u << 1)
#define MEL_VFS_STAT_IS_SYMLINK  (1u << 2)
#define MEL_VFS_STAT_IS_READONLY (1u << 3)
#define MEL_VFS_STAT_HAS_BIRTH_TIME (1u << 4)

typedef struct {
    str8 name; // Ptr points into caller memory (`name_buf` or `str_blob`)
    Mel_Vfs_Stat stat;
} Mel_Vfs_Dir_Entry;

typedef struct Mel_Vfs_Sqe {
    u64 ticket;       // required non-zero value from mel_vfs_next_ticket
    u32 op;
    u32 flags;
    u8  priority;     // 0-255, higher is processed first
    u8  qos_class;    // MEL_VFS_QOS_*
    u64 deadline_ns;  // absolute monotonic deadline, 0 means no deadline
    void* user_data;
    
    // NOTE: All str8 paths AND Mel_IoVec arrays passed here are COPIED into the
    // VFS ring buffer immediately upon submission. The caller does not need to
    // keep the path string or iov array alive after mel_vfs_submit returns.
    union {
        struct { str8 path; u32 open_flags; } open;
        struct { Mel_Vfs_File file; } close;
        struct { Mel_Vfs_File file; u64 offset; void* dst; usize size; } read;
        struct { Mel_Vfs_File file; u64 offset; const void* src; usize size; } write;
        struct { Mel_Vfs_File file; u64 offset; Mel_IoVec* iov; usize iov_cnt; } readv;
        struct { Mel_Vfs_File file; u64 offset; const Mel_IoVec* iov; usize iov_cnt; } writev;
        struct { Mel_Vfs_File file; u64 offset; usize size; u32 flags; } map;
        struct { Mel_Vfs_Map map; } unmap;
        struct { str8 path; } stat;
        struct { str8 path; } dir_open;
        struct { Mel_Vfs_Dir dir; Mel_Vfs_Dir_Entry* entry; u8* name_buf; usize name_cap; } dir_next;
        struct { Mel_Vfs_Dir dir; Mel_Vfs_Dir_Entry* entries; usize entry_cap; void* str_blob; usize str_blob_cap; } dir_next_batch;
        struct { Mel_Vfs_Dir dir; } dir_close;
        struct { str8 path; bool recursive; u32 flags; } watch_open;
        struct { Mel_Vfs_Watch watch; u8* path_buf; usize path_cap; } watch_next;
        struct { Mel_Vfs_Watch watch; } watch_close;
        struct { Mel_Vfs_File file; } sync;
        struct { u64 ticket_to_cancel; } cancel;
    };
} Mel_Vfs_Sqe;

typedef struct Mel_Vfs_Cqe {
    u64 ticket;
    u32 op;
    u32 status;
    i64 result;      // bytes transferred, entries filled, or watch action
    Mel_Vfs_Error error;
    void* user_data;
    union {
        Mel_Vfs_File file;
        Mel_Vfs_Dir  dir;
        Mel_Vfs_Map  map; // Valid map handle on success
        Mel_Vfs_Watch watch;
        Mel_Vfs_Stat  stat;

        // Used by WATCH_NEXT completions only.
        // path_str.data points into the caller-provided path_buf from the SQE.
        // path_str.len is the actual length written. result = MEL_VFS_WATCH_* action.
        str8          path_str;
    };
} Mel_Vfs_Cqe;

typedef struct {
    Mel_Alloc* allocator;   // For internal allocations (backend states, ring buffers)
    usize sq_capacity;      // Submission Queue size (power of 2)
    usize cq_capacity;      // Completion Queue size (power of 2)
    u32 worker_threads;     // Number of background IO threads (0 for main-thread only polling)
} Mel_Vfs_Desc;

bool mel_vfs_init(Mel_Vfs* vfs, const Mel_Vfs_Desc* desc);
void mel_vfs_shutdown(Mel_Vfs* vfs);

// Monotonic ticket allocator. Tickets are never reused for a given VFS instance.
u64  mel_vfs_next_ticket(Mel_Vfs* vfs);

// Returns number of SQEs accepted (prefix-based capacity gating).
// LINK_NEXT admission is atomic per chain: if a chain cannot fit fully, reject
// that entire chain from its root SQE onward (no split admission).
// Accepted SQEs always produce exactly one terminal CQE.
// Paths and iov arrays are copied into the internal ring buffer here.
i32  mel_vfs_submit(Mel_Vfs* vfs, const Mel_Vfs_Sqe* sqes, i32 count);

// Non-blocking poll. Returns number of CQEs written.
i32  mel_vfs_poll(Mel_Vfs* vfs, Mel_Vfs_Cqe* out_cqes, i32 max_count);

// Blocking wait.
bool mel_vfs_wait(Mel_Vfs* vfs, i32 min_count, u32 timeout_ms);

// Ticket-targeted completion helpers used by blocking wrappers.
// Both scan existing CQ entries first before waiting — a CQE that arrived
// before the call is found immediately, not missed.
bool mel_vfs_poll_ticket(Mel_Vfs* vfs, u64 ticket, Mel_Vfs_Cqe* out_cqe);
bool mel_vfs_wait_ticket(Mel_Vfs* vfs, u64 ticket, u32 timeout_ms, Mel_Vfs_Cqe* out_cqe);

// Resolve mapped handle to pointer metadata.
void* mel_vfs_map_ptr(Mel_Vfs* vfs, Mel_Vfs_Map map, usize* out_size);
```

Important properties:
- **Zero-Allocation Submission:** The submit call copies paths to a pre-allocated ring buffer. No `malloc` per IO request.
- **Priority Queueing:** Backend workers pick high-priority tasks first (critical for streaming).
- **Scatter/Gather:** `readv` supported for efficient parsing.
- **Memory Mapping:** `map` returns a handle and `mel_vfs_map_ptr` exposes pointer metadata safely.
- **Generational Handles:** `Mel_Vfs_File` / `Mel_Vfs_Dir` / `Mel_Vfs_Map` are ID+generation pairs to reject stale handles safely.
- **Typed Errors:** `Mel_Vfs_Error` carries category + backend + native OS code for production diagnostics.
- **Ticket Ownership:** caller reserves ticket IDs through `mel_vfs_next_ticket` before submit.

## Operation Lifecycle (strict contract)

Every submitted op follows a fixed state machine:

- `SUBMITTED` -> `QUEUED` -> `RUNNING` -> `COMPLETED`
- `SUBMITTED` -> `QUEUED` -> `CANCELLED` (if cancellation wins race)
- `SUBMITTED` -> `QUEUED` -> `REJECTED` (invalid op payload/handle generation)
- `SUBMITTED` -> `NOT_ACCEPTED` (queue full at submit time)

Rules:
- each accepted `ticket` yields exactly one terminal CQE,
- non-accepted SQEs are reported only through `mel_vfs_submit` return count,
- partial submit is prefix-based with chain atomicity: the first N SQEs are accepted, then remaining SQEs are rejected; if a `LINK_NEXT` chain straddles the boundary, the entire chain is rejected from its root SQE (no split chains),
- validation and routing failures for accepted SQEs (bad handles, invalid paths, permission failures, backend errors) are returned as terminal CQEs with explicit `status`/`error` fields,
- cancellation is best-effort and race-safe,
- `FENCE` prevents later SQEs from entering `RUNNING` until fenced SQE completes,
- `LINK_NEXT` creates fail-fast chains (if parent fails/cancels, linked child CQE returns `CANCELLED`),
- watches are explicit sessions: `WATCH_OPEN` returns a watch handle, `WATCH_NEXT` yields one event into caller-provided buffer, `WATCH_CLOSE` terminates,
- `WATCH_NEXT` with no event before timeout returns `MEL_VFS_STATUS_TIMEOUT` (not EOF).

## Directory Listing: Three Public Styles

### Style A: Callback walk (ergonomic)

```c
typedef bool (*Mel_Vfs_Enum_Cb)(str8 virtual_path, const Mel_Vfs_Stat* stat, void* user);

typedef struct {
    bool recursive;
    bool include_dirs;
} Mel_Vfs_Enum_Opt;

bool mel_vfs_enumerate_opt(Mel_Vfs* vfs, str8 path, Mel_Vfs_Enum_Cb cb, void* user, Mel_Vfs_Enum_Opt opt);
#define mel_vfs_enumerate(vfs, path, cb, user, ...) \
    mel_vfs_enumerate_opt((vfs), (path), (cb), (user), (Mel_Vfs_Enum_Opt){__VA_ARGS__})
```

Behavior:
- built on top of `DIR_OPEN` / `DIR_NEXT` / `DIR_CLOSE`,
- callback can stop early by returning `false`,
- good for tools, importers, and asset scans.

### Style B: Explicit iterator control (low-level)

Use core async ops directly:
- submit `DIR_OPEN`,
- loop `DIR_NEXT` until `MEL_VFS_STATUS_EOF`,
- submit `DIR_CLOSE`.

This is for callers that need full scheduling control and custom batching.

### Style C: Explicit batched iterator (low-level, high-throughput)

Use `DIR_NEXT_BATCH` with caller-owned `Mel_Vfs_Dir_Entry[]` AND a single large string buffer:
- one completion can return many directory entries (`result = entries_filled`),
- caller provides one large memory blob for names (`str_blob`), VFS sub-allocates strings from it,
- `entries[i].name` points into `str_blob`,
- avoids per-entry allocation/setup overhead (1 allocation vs N).

## Mount Registry (stays, but resolved at open)

```c
typedef struct {
    str8             prefix;    // duped into VFS allocator
    Mel_Vfs_Backend* backend;
    u8               priority;  // higher checked first
    bool             writable;
} Mel_Vfs_Mount;
```

Path normalization + validation (applied for all admitted path-bearing SQEs):
- separator is always forward slash `/`,
- backslashes converted to forward slash on input,
- consecutive slashes collapsed (`a//b` → `a/b`),
- `.` segments removed,
- `..` segments resolved (but never above virtual root),
- no trailing slash (stripped if present),
- case preserved (case sensitivity is a backend concern, not VFS-level),
- virtual root is `/`, all paths are absolute within the virtual namespace,
- paths that escape root after `..` resolution complete with `MEL_VFS_STATUS_PERMISSION`.

Resolution rules:
- normalize virtual path once,
- canonicalize mount prefixes at mount time using the same normalization rules; invalid mount prefixes are rejected by `mel_vfs_mount`,
- match mounts deterministically using sort key:
- first by `priority` descending,
- then by `prefix` length descending,
- then by mount insertion order ascending,
- `OPEN` resolves mount and binds handle to that backend,
- `READ/WRITE` on handle do not re-run mount lookup.
- each `OPEN` captures a mount-table generation snapshot internally.

This keeps per-I/O overhead low and behavior deterministic.

Mount update safety:
- mount/unmount increments global `mount_generation`,
- in-flight handles keep operating against the generation captured at open,
- new opens use the newest generation.

## Backend Interface (internal, updated)

Backends expose explicit handle-based operations.
Optional extension point for native async drivers:
- `submit_native` may complete directly into CQ when backend can use true OS async (future),
- if missing, VFS worker path executes blocking backend op and posts CQ.

This allows v1 to ship with a robust fallback and evolve to io_uring/IOCP backends later without changing public API.

## Threading, Completion, and Safety

- VFS owns a bounded SQ/CQ capacity configured at init.
- v1 execution path can run on job workers (`Mel_Job_Context`) and post completions back into CQ.
- callbacks from high-level enumerate API execute on the thread that drives polling/waiting, not on random workers.
- completion ordering is per-request, not global ordering across all tickets.
- cancellation is best-effort:
  - if not started: completes as `CANCELLED`,
  - if already running in backend: may complete as normal.

QoS scheduling model:
- QoS class selects the scheduling lane (separate per-lane queues with independent in-flight budgets),
- priority (0-255) determines ordering within a lane (higher = picked first),
- `deadline_ns` triggers promotion: when a request's deadline is about to expire, it is promoted to the `LATENCY_CRITICAL` lane regardless of its original QoS class,
- in-flight budgets per lane prevent starvation: `LATENCY_CRITICAL` always has reserved slots that `BULK` cannot consume,
- a `BULK` request with priority 255 still cannot preempt a `LATENCY_CRITICAL` request with priority 0 — lane boundaries are hard.

Thread safety of blocking wrappers (`mel_vfs_read_file_alloc`, etc.):
- blocking wrappers internally call `mel_vfs_submit` + `mel_vfs_wait_ticket`,
- wrappers never consume/re-post foreign CQEs; ticket-targeted wait/poll isolates completion ownership,
- this allows multiple wrapper calls concurrently as long as SQ/CQ internals are thread-safe (required),
- mixing blocking wrappers with manual `mel_vfs_poll` is safe if manual pollers do not deliberately steal wrapper-owned tickets,
- `mel_vfs_shutdown` asserts that all deferred-cleanup mounts have reached refcount zero.

Memory ownership contract:
- file read/write buffers are always caller-owned,
- `DIR_NEXT` writes entry name into caller-provided `name_buf`,
- `DIR_NEXT_BATCH` writes names into caller-provided `str_blob` and `entries[i].name` points into that blob,
- `WATCH_NEXT` writes event path into caller-provided `path_buf`; CQE `path_str` has `.data = path_buf, .len = actual length`,
- if destination buffer is too small, operation completes with `MEL_VFS_STATUS_BUFFER_TOO_SMALL` and `result = required_bytes`,
- for `DIR_NEXT` and `WATCH_NEXT`, `BUFFER_TOO_SMALL` does not consume the pending entry/event (retry-safe),
- all buffers passed in SQEs must remain valid until the corresponding CQE is consumed (this is the caller's responsibility — the VFS copies paths and iovecs, but NOT the read/write/dir/watch destination buffers).

## High-Level Ergonomic API (`melody/vfs.h`)

These wrappers are convenience-only and are implemented over Level 1:

```c
u8*  mel_vfs_read_file_alloc(Mel_Vfs* vfs, str8 path, usize* out_size, Mel_Alloc* alloc);
str8 mel_vfs_read_text_alloc(Mel_Vfs* vfs, str8 path, Mel_Alloc* alloc);
bool mel_vfs_write_file(Mel_Vfs* vfs, str8 path, const u8* data, usize size);
bool mel_vfs_write_text(Mel_Vfs* vfs, str8 path, str8 text);
bool mel_vfs_exists(Mel_Vfs* vfs, str8 path);
bool mel_vfs_stat_sync(Mel_Vfs* vfs, str8 path, Mel_Vfs_Stat* out);
```

These wrappers can block by internally waiting on completions.
Core async API remains the primary contract.

## Files

### `melody/vfs.fwd.h`

Forward declarations only. Any `.h` that needs VFS types without pulling in the full API includes this.

Forward-declared types:
- `Mel_Vfs`
- `Mel_Vfs_File`, `Mel_Vfs_Dir`, `Mel_Vfs_Map`, `Mel_Vfs_Watch` (generational handles)
- `Mel_Vfs_Stat`
- `Mel_Vfs_Error`
- `Mel_Vfs_Sqe`, `Mel_Vfs_Cqe`
- `Mel_IoVec`
- `Mel_Vfs_Backend`
- `Mel_Vfs_Mount`

### `melody/vfs.cfg.h`

Configuration macros with ifdef/default pattern per MEL-STYLE-003.

- `MEL_VFS_DEFAULT_SQ_CAPACITY` (default: 256)
- `MEL_VFS_DEFAULT_CQ_CAPACITY` (default: 512)
- `MEL_VFS_MAX_PATH` (default: 4096)
- `MEL_VFS_HANDLE_TABLE_INITIAL` (default: 256)
- `MEL_VFS_MOUNT_TABLE_INITIAL` (default: 16)
- `MEL_VFS_RING_BUFFER_SIZE` (path/iovec copy ring, default: 64K)
- `MEL_VFS_VALIDATE_HANDLES` (default: 1 in debug, 0 in release)

### `melody/vfs.async.h`

Level 1 core async API. This is the primary contract.

Includes: `vfs.cfg.h`, `core.types.h`, `string.str8.fwd.h`, `allocator.h`

Defines all constants:
- `MEL_VFS_OP_*` (OPEN, CLOSE, READ, WRITE, READV, WRITEV, MAP, UNMAP, STAT, DIR_OPEN, DIR_NEXT, DIR_NEXT_BATCH, DIR_CLOSE, WATCH_OPEN, WATCH_NEXT, WATCH_CLOSE, SYNC, CANCEL)
- `MEL_VFS_STATUS_*` (OK, PENDING, NOT_FOUND, EOF, PERMISSION, UNSUPPORTED, CANCELLED, IO_ERROR, INVALID_ARGUMENT, BUFFER_TOO_SMALL, TIMEOUT)
- `MEL_VFS_ERRCAT_*` (NONE, GENERIC, OS, BACKEND)
- `MEL_VFS_OPEN_*` (READ, WRITE, CREATE, TRUNCATE, DIRECT)
- `MEL_VFS_WATCH_*` (ADDED, MODIFIED, REMOVED, RENAMED)
- `MEL_VFS_PRIORITY_*` (LOW=0, NORMAL=128, HIGH=255)
- `MEL_VFS_QOS_*` (LATENCY_CRITICAL, STREAMING, BULK)
- `MEL_VFS_SQE_F_*` (FENCE, LINK_NEXT, DONT_BLOCK)
- `MEL_VFS_MAP_*` (READ, WRITE)

Full struct definitions:
- `Mel_Vfs_File`, `Mel_Vfs_Dir`, `Mel_Vfs_Map`, `Mel_Vfs_Watch` — `{ u32 slot; u32 generation; }` + INVALID sentinels
- `Mel_IoVec` — `{ void* buffer; usize len; }`
- `Mel_Vfs_Stat` — `{ u64 size; u64 mtime_ns; u64 change_ns; u64 birth_ns; u32 flags; }` with `MEL_VFS_STAT_IS_FILE`, `_IS_DIR`, `_IS_SYMLINK`, `_IS_READONLY`, `_HAS_BIRTH_TIME` flag defines
- `Mel_Vfs_Error` — `{ u16 category; u16 backend_id; i32 code; i32 native_code; }`
- `Mel_Vfs_Dir_Entry` — `{ str8 name; Mel_Vfs_Stat stat; }`
- `Mel_Vfs_Sqe` — ticket, op, flags, priority, qos_class, deadline_ns, user_data, tagged union
- `Mel_Vfs_Cqe` — ticket, op, status, result, error, user_data, tagged union
- `Mel_Vfs_Desc` — `{ Mel_Alloc* allocator; usize sq_capacity; usize cq_capacity; u32 worker_threads; }`
- `Mel_Vfs` — the instance struct (fully visible, not opaque):
  - SQ/CQ ring buffers
  - handle tables (file, dir, map, watch) backed by slotmap-style arrays
  - path copy ring buffer
  - mount table pointer + generation
  - worker thread pool state
  - ticket counter (atomic u64, monotonic)

Functions:
- `mel_vfs_init(Mel_Vfs*, const Mel_Vfs_Desc*)` → bool
- `mel_vfs_shutdown(Mel_Vfs*)`
- `mel_vfs_next_ticket(Mel_Vfs*)` → u64 (monotonic, never returns 0)
- `mel_vfs_submit(Mel_Vfs*, const Mel_Vfs_Sqe*, i32 count)` → i32 (prefix-accepted count, LINK_NEXT admission is atomic per chain)
- `mel_vfs_poll(Mel_Vfs*, Mel_Vfs_Cqe*, i32 max_count)` → i32
- `mel_vfs_wait(Mel_Vfs*, i32 min_count, u32 timeout_ms)` → bool
- `mel_vfs_poll_ticket(Mel_Vfs*, u64 ticket, Mel_Vfs_Cqe*)` → bool
- `mel_vfs_wait_ticket(Mel_Vfs*, u64 ticket, u32 timeout_ms, Mel_Vfs_Cqe*)` → bool
- `mel_vfs_map_ptr(Mel_Vfs*, Mel_Vfs_Map, usize* out_size)` → void*

### `melody/vfs.async.inl`

Inline helpers for hot-path validation and convenience.

- `mel_vfs_file_valid(Mel_Vfs_File)` → bool (not INVALID sentinel)
- `mel_vfs_dir_valid(Mel_Vfs_Dir)` → bool
- `mel_vfs_map_valid(Mel_Vfs_Map)` → bool
- `mel_vfs_watch_valid(Mel_Vfs_Watch)` → bool

### `melody/vfs.async.c`

Core implementation of the async engine.

- SQ/CQ ring buffer management (power-of-two masking, producer/consumer indices)
- Ticket generation: per-VFS atomic monotonic u64 counter exposed via `mel_vfs_next_ticket`
- Submit path: capacity admission + chain admission + copy str8 paths AND Mel_IoVec arrays into ring buffer, enqueue
- Partial submit: prefix-based acceptance; `LINK_NEXT` is all-or-none per chain
- Dispatch: dequeue from SQ, resolve handle → backend, execute blocking backend call or hand off to worker
- Validation and path/routing failures for admitted SQEs are emitted as terminal CQEs
- Worker thread pool (spawned when `worker_threads > 0`): per-QoS lanes, priority ordering within each lane, in-flight budgets per QoS class to prevent starvation
- QoS/priority/deadline interaction: QoS picks lane, priority orders in-lane, near-expired `deadline_ns` promotes to LATENCY_CRITICAL lane
- Completion posting: thread-safe CQ push (lock-free MPSC or mutex, TBD during impl)
- Cancellation: best-effort, sets cancelled flag, racing completion is valid
- FENCE: blocks later SQEs from entering RUNNING until fenced SQE completes
- LINK_NEXT: if parent fails/cancels, linked children get CANCELLED CQEs
- Watch sessions: `WATCH_OPEN` allocates watch handle, `WATCH_NEXT` blocks/polls one event into caller-provided `path_buf`, `WATCH_CLOSE` releases handle
- Handle table: slot allocation, generation increment on close, stale-generation rejection via assert
- Ticket-targeted completion (`mel_vfs_poll_ticket` / `mel_vfs_wait_ticket`): scan CQ for existing match first, then condvar-wait on new completions; completions post to CQ and broadcast condvar so all waiters re-check

### `melody/vfs.h`

Level 2 ergonomic API + mount lifecycle.

Includes: `vfs.async.h`

Types:
- `Mel_Vfs_Mount` — `{ str8 prefix; Mel_Vfs_Backend* backend; u8 priority; bool writable; }`
- `Mel_Vfs_Enum_Cb` — `bool (*)(str8 virtual_path, const Mel_Vfs_Stat* stat, void* user)`
- `Mel_Vfs_Enum_Opt` — `{ bool recursive; bool include_dirs; }`

Functions:
- `mel_vfs_mount(Mel_Vfs*, str8 prefix, Mel_Vfs_Backend*, u8 priority, bool writable)`
- `mel_vfs_unmount(Mel_Vfs*, str8 prefix)` — retires mount for new opens, destruction deferred until active handle refs drop to zero
- `mel_vfs_read_file_alloc(Mel_Vfs*, str8 path, usize* out_size, Mel_Alloc*)` → u8*
- `mel_vfs_read_text_alloc(Mel_Vfs*, str8 path, Mel_Alloc*)` → str8
- `mel_vfs_write_file(Mel_Vfs*, str8 path, const u8* data, usize size)` → bool
- `mel_vfs_write_text(Mel_Vfs*, str8 path, str8 text)` → bool
- `mel_vfs_exists(Mel_Vfs*, str8 path)` → bool
- `mel_vfs_stat_sync(Mel_Vfs*, str8 path, Mel_Vfs_Stat*)` → bool
- `mel_vfs_sync(Mel_Vfs*, Mel_Vfs_File)` → bool (fsync equivalent)
- `mel_vfs_enumerate_opt(Mel_Vfs*, str8 path, Mel_Vfs_Enum_Cb, void*, Mel_Vfs_Enum_Opt)` → bool
- `mel_vfs_enumerate(vfs, path, cb, user, ...)` (macro with _opt pattern)

### `melody/vfs.c`

Level 2 implementation.

- Mount registry: dynamic array of Mel_Vfs_Mount sorted by priority (higher first)
- Mount generation counter: atomic u32, incremented on mount/unmount
- Path normalization: forward slash only, no trailing slash, collapse `..`, case preserved, virtual root is `/`
- Mount prefix canonicalization at mount time using same normalization rules; invalid prefixes are rejected
- Open-time mount resolution order: priority desc, prefix_len desc, insertion_order asc; captures mount generation snapshot into handle metadata
- Mount unmount safety: unmount retires mount immediately for new opens, actual backend cleanup after last live handle releases
- Blocking wrappers: submit SQE, call `mel_vfs_wait_ticket` for owned ticket, no foreign CQE re-post path
- Enumerate: built on DIR_OPEN → DIR_NEXT loop → DIR_CLOSE, with early exit on callback returning false
- Sync: submit MEL_VFS_OP_SYNC, wait for completion

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
    void (*destroy)(Mel_Vfs_Backend*);
    void* impl_data;
} Mel_Vfs_Backend;
```

All ops return 0/positive on success, negative on error. Read/write return bytes transferred.

`dir_next` and `watch_next` receive explicit buffer + capacity + out-length parameters. Backend writes name/path bytes into the provided buffer up to capacity. If the buffer is too small, backend returns negative and sets `*out_name_len` / `*out_path_len` to the required size — the VFS layer translates this to `MEL_VFS_STATUS_BUFFER_TOO_SMALL` with `result = required_bytes`. The pending entry/event is NOT consumed on buffer-too-small, making retry safe. The VFS layer assembles the final `Mel_Vfs_Dir_Entry` (setting `name.data = name_buf`, `name.len = *out_name_len`) after a successful backend call.

Backends may leave watch function pointers null if unsupported; VFS maps that to `MEL_VFS_STATUS_UNSUPPORTED`. Backend does NOT own handle generation logic — that's VFS-level. `native_handle` is backend-local and pointer-width safe.

### `melody/vfs.backend.os.h`

OS filesystem backend creation/destruction.

- `mel_vfs_backend_os_create(Mel_Alloc*, str8 root_path)` → Mel_Vfs_Backend*
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
- watch_open/watch_next/watch_close → v1 returns unsupported (watch API is present, backend impl deferred)

### `melody/vfs.backend.mock.h`

Deterministic mock/replay backend for testing. First-class, not an afterthought.

- `mel_vfs_backend_mock_create(Mel_Alloc*)` → Mel_Vfs_Backend*
- `mel_vfs_backend_mock_destroy(Mel_Vfs_Backend*)`
- `mel_vfs_backend_mock_script_read(backend, path, data, size, status, latency_us)` — pre-program a read result
- `mel_vfs_backend_mock_script_write(backend, path, status, latency_us)` — pre-program a write result
- `mel_vfs_backend_mock_script_error(backend, path, op, error)` — inject error for specific op+path
- `mel_vfs_backend_mock_script_stat(backend, path, stat, status)` — pre-program stat result
- `mel_vfs_backend_mock_set_completion_order(backend, ticket_order[], count)` — force completions in exact order for race reproduction

### `melody/vfs.backend.mock.c`

Scripted response tables, artificial latency injection, forced completion ordering. Backed by hashmap keyed on (path, op).

### Tests

`tests/test_vfs_async.c` — Core SQ/CQ mechanics.
- submit/poll round-trip, ticket monotonicity
- open/read/write/stat against OS backend with real temp files
- positioned I/O correctness (read at offset)
- scatter/gather (readv into separate header+body buffers)
- cancellation (cancel pending op, verify CANCELLED CQE)
- FENCE (later ops blocked until fenced op completes)
- LINK_NEXT (parent failure cascades CANCELLED to children)
- LINK_NEXT all-or-none admission at capacity boundary (no split chains)
- handle generation rejection (close file, attempt read on stale handle)
- partial submit (fill SQ, verify prefix acceptance and chain-root rejection)
- per-ticket wait/poll helpers (`mel_vfs_wait_ticket`, `mel_vfs_poll_ticket`) under concurrent wrapper usage
- Tags: `vfs, async`

`tests/test_vfs_enum.c` — Directory enumeration.
- callback walk (flat and recursive)
- early termination (callback returns false)
- explicit DIR_OPEN/DIR_NEXT/DIR_CLOSE loop
- batched DIR_NEXT_BATCH (verify entries_filled count)
- DIR_NEXT buffer too small (`BUFFER_TOO_SMALL`, required bytes in `result`, retry same entry)
- DIR_NEXT_BATCH first-entry too large contract (`BUFFER_TOO_SMALL`, required bytes)
- empty directory edge case
- Tags: `vfs, collection`

`tests/test_vfs_watch.c` — Watch session semantics.
- WATCH_OPEN unsupported path on OS backend v1 (`UNSUPPORTED`)
- mock backend WATCH_OPEN → WATCH_NEXT → WATCH_CLOSE lifecycle
- WATCH_NEXT timeout returns `TIMEOUT`
- WATCH_NEXT buffer too small returns `BUFFER_TOO_SMALL` and retry returns same event
- Tags: `vfs, async`

`tests/test_vfs_mount.c` — Mount registry.
- priority ordering (higher priority mount wins)
- longest prefix matching
- deterministic tie-break (same priority + same prefix length => earlier mount insertion wins)
- mount prefix canonicalization and invalid prefix rejection
- mount generation snapshot (open before unmount keeps working)
- unmount retires mount (new opens failover) while existing handles keep working until close
- writable flag enforcement (write to read-only mount fails)
- Tags: `vfs`

`tests/test_vfs_mock.c` — Mock backend.
- scripted read/write/stat results
- error injection (specific path + op returns error)
- latency simulation
- forced completion ordering for deterministic race reproduction
- Tags: `vfs, async`

`tests/test_vfs_stress.c` — Stress and starvation.
- many concurrent mixed ops across threads with random cancellation
- QoS starvation measurement (bulk streaming vs latency-critical tail latency)
- handle slot reuse after heavy churn
- Tags: `vfs, async` (consider running manually, not in default suite)

### Delete (after migration)

- `melody/assets.h`
- `melody/assets.c`

### Modify

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

## Execution Order

1. Implement Level 1 core (`vfs.async.*`) with handle tables (generation counters), SQ/CQ, and typed error propagation.
2. Add mount registry + path normalization + open-time resolution with mount generation snapshots.
3. Implement directory operations (`DIR_OPEN/NEXT/NEXT_BATCH/CLOSE`) and tests.
4. Implement QoS scheduling and submission semantics (`FENCE`, `LINK_NEXT`, `DONT_BLOCK`).
5. Implement Level 2 ergonomic wrappers on top of Level 1.
6. Migrate one consumer at a time from `assets.*` to VFS.
7. Remove PhysFS dependency and delete `assets.*`.
8. Run full build and tests.

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

## v1 Scope and Deferred Items

Included in v1:
- async-first queue API,
- explicit handle-based file and dir operations,
- callback enumerate plus explicit single-entry and batched dir iteration,
- OS backend + mount priorities,
- sync helper wrappers on top.

Deferred:
- native io_uring backend,
- native IOCP backend,
- file watching/hot-reload (API present, implementation deferred),
- archive backend,
- network backend.
