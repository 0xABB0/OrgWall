# VFS Module: Async-First, OS-Style, Layered API

## Context

The engine historically used `assets.h/c` as a thin wrapper around PhysFS. Problems were:

- `assets.c` has fixed path buffers and ad-hoc string conversions.
- VFS behavior is tied to one dependency (`PhysFS`) instead of owned by Melody.
- Different systems load files differently (`mel_assets_read`, `SDL_IOFromFile`, raw `stbi_load`).
- No proper async I/O contract for future hot-reload and streaming work.

This refactor was tracked in `todo.md` and is now implemented in v1 (with deferred items listed below).

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
#define MEL_VFS_OP_RENAME      19
#define MEL_VFS_OP_DELETE      20
#define MEL_VFS_OP_MKDIR       21

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
#define MEL_VFS_STATUS_TIMEOUT          10
#define MEL_VFS_STATUS_ALREADY_EXISTS   11

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

typedef struct { Mel_SlotMap_Handle handle; } Mel_Vfs_File;
#define MEL_VFS_FILE_INVALID  ((Mel_Vfs_File){.handle = MEL_SLOTMAP_HANDLE_NULL})

typedef struct { Mel_SlotMap_Handle handle; } Mel_Vfs_Dir;
#define MEL_VFS_DIR_INVALID   ((Mel_Vfs_Dir){.handle = MEL_SLOTMAP_HANDLE_NULL})

typedef struct { Mel_SlotMap_Handle handle; } Mel_Vfs_Map;
#define MEL_VFS_MAP_INVALID   ((Mel_Vfs_Map){.handle = MEL_SLOTMAP_HANDLE_NULL})

typedef struct { Mel_SlotMap_Handle handle; } Mel_Vfs_Watch;
#define MEL_VFS_WATCH_INVALID ((Mel_Vfs_Watch){.handle = MEL_SLOTMAP_HANDLE_NULL})

typedef struct {
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

    // NOTE: All str8 paths AND Mel_IoVec arrays passed here are COPIED via
    // heap allocation (per-op) on submission. The caller does not need to
    // keep the path string or iov array alive after mel_vfs_submit returns.
    // Copies are freed automatically when the CQE is extracted.
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
        struct { str8 src_path; str8 dst_path; } rename;
        struct { str8 path; } del;
        struct { str8 path; } mkdir;
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
    const Mel_Alloc* allocator;
    Mel_Io*          io;    // External I/O executor (caller creates and owns)
} Mel_Vfs_Desc;

bool mel_vfs_init(Mel_Vfs* vfs, const Mel_Vfs_Desc* desc);
void mel_vfs_shutdown(Mel_Vfs* vfs);

// Monotonic ticket allocator. Tickets are never reused for a given VFS instance.
u64  mel_vfs_next_ticket(Mel_Vfs* vfs);

// Returns number of SQEs accepted (prefix-based capacity gating).
// LINK_NEXT admission is atomic per chain: if a chain cannot fit fully, reject
// that entire chain from its root SQE onward (no split admission).
// Accepted SQEs always produce exactly one terminal CQE.
// Paths and iov arrays are copied into per-op heap-owned memory here.
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
- **Per-Op Heap Allocation:** Submit copies paths and iov arrays into per-op heap allocations, freed when CQE is extracted. Straightforward ownership model at the cost of a malloc per submit.
- **Delegated Execution:** VFS registers a handler with `Mel_Io` and delegates threading, queuing, fence/link semantics, and ticket management to the generic I/O executor.
- **Scatter/Gather:** `readv`/`writev` supported for efficient parsing and batched writes.
- **Memory Mapping:** `map` returns a handle and `mel_vfs_map_ptr` exposes pointer metadata safely.
- **Generational Handles:** `Mel_Vfs_File` / `Mel_Vfs_Dir` / `Mel_Vfs_Map` / `Mel_Vfs_Watch` wrap `Mel_SlotMap_Handle` (u32 index + u32 generation) for stale-handle rejection.
- **Typed Errors:** `Mel_Vfs_Error` carries category + backend + native OS code for production diagnostics.
- **Ticket Ownership:** caller reserves ticket IDs through `mel_vfs_next_ticket` (delegates to `mel_io_next_ticket`) before submit.

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
struct Mel_Vfs_Mount {
    str8             prefix;          // duped into VFS allocator, normalized at mount time
    Mel_Vfs_Backend* backend;
    u8               priority;        // higher checked first
    bool             writable;
    u32              refcount;        // live handles referencing this mount
    bool             retired;         // set by unmount, blocks new opens
    u32              insertion_order; // tie-break for same priority + prefix length
};
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
- `rename`, `remove`, `mkdir` are optional backend entries (NULL → `UNSUPPORTED`). Return 0 on success, `-errno` on failure.

This allows v1 to ship with a robust fallback and evolve to io_uring/IOCP backends later without changing public API.

## Threading, Completion, and Safety

- VFS does NOT own threads, SQ/CQ rings, or condvars. All of that is delegated to `Mel_Io` (see `melody/async.io.*`).
- VFS registers a handler via `mel_io_register_handler()` at init. The handler callback (`mel__vfs_io_handler`) is called by `Mel_Io` workers.
- The handler uses a three-phase execution model to minimize lock contention:
  - **Phase 0 (no lock):** path normalization + escape checking for path-bearing ops.
  - **Phase 1 (lock):** `mel__vfs_resolve` — slotmap lookups, mount resolution, permission checks. Populates `Mel_Vfs__Op_Ctx`.
  - **Phase 2 (no lock):** `mel__vfs_dispatch` — backend I/O calls using resolved context. This is the expensive phase and runs without holding `state_lock`.
  - **Phase 3 (lock, conditional):** `mel__vfs_commit` — slotmap insertions and mount refcount updates, only if the operation succeeded and needs state mutation (e.g., OPEN, CLOSE, MAP).
- Callbacks from high-level enumerate API execute on the thread that drives polling/waiting, not on random workers.
- Completion ordering is per-request, not global ordering across all tickets.
- Cancellation is best-effort via `mel_io_cancel` (`MEL_VFS_OP_CANCEL` delegates to Mel_Io).

QoS scheduling model (PARTIALLY IMPLEMENTED):
- QoS class selects the scheduling lane (separate per-lane queues with independent in-flight budgets),
- priority (0-255) ordering within a lane is not implemented yet,
- `deadline_ns` timeout checks exist, but deadline-based lane promotion is not implemented yet,
- in-flight budgets per lane are not implemented yet.

Thread safety of blocking wrappers (`mel_vfs_read_file_alloc`, etc.):
- blocking wrappers internally call `mel_vfs_submit` + `mel_vfs_wait_ticket`,
- wrappers never consume/re-post foreign CQEs; ticket-targeted wait/poll isolates completion ownership,
- this allows multiple wrapper calls concurrently because `Mel_Io`'s SQ/CQ internals are thread-safe,
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
u8*  mel_vfs_read_file_alloc(Mel_Vfs* vfs, str8 path, usize* out_size, const Mel_Alloc* alloc);
str8 mel_vfs_read_text_alloc(Mel_Vfs* vfs, str8 path, const Mel_Alloc* alloc);
bool mel_vfs_write_file(Mel_Vfs* vfs, str8 path, const u8* data, usize size);
bool mel_vfs_write_text(Mel_Vfs* vfs, str8 path, str8 text);
bool mel_vfs_exists(Mel_Vfs* vfs, str8 path);
bool mel_vfs_stat_sync(Mel_Vfs* vfs, str8 path, Mel_Vfs_Stat* out);
bool mel_vfs_sync_file(Mel_Vfs* vfs, Mel_Vfs_File file);
bool mel_vfs_rename(Mel_Vfs* vfs, str8 src, str8 dst);
bool mel_vfs_delete(Mel_Vfs* vfs, str8 path);
bool mel_vfs_mkdir(Mel_Vfs* vfs, str8 path);
```

These wrappers can block by internally waiting on completions.
Core async API remains the primary contract.

→ Implementation tracking, per-file notes, tests, execution history: `vfs.status.md`
