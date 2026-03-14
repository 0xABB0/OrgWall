# log.* -- Centralized Logging System

## Overview

Structured logging system with configurable sinks, a lock-free ring buffer for decoupled I/O, and a dedicated writer thread. The star sink writes to SQLite, giving you queryable, structured logs instead of text grep nightmares.

Replaces all raw `SDL_Log`/`fprintf`/`printf` calls in the codebase (~100 across 20 files).

## Files

```
log.h                // main interface: macros, context stack, level constants, init/shutdown
log.fwd.h            // forward decls: Mel_Log_Entry, Mel_Log_Sink, Mel_Log_Sink_Handle
log.cfg.h            // compile-time config: ring buffer default size, max context depth
log.c                // core: ring buffer, writer thread, sink dispatch, level registry
log.sink.h           // sink interface (vtable)
log.sink.console.c   // stderr sink with optional color
log.sink.sqlite.c    // SQLite WAL-mode sink with batched writes
log.sink.file.c      // plain text file sink
```

## Levels

Levels are `u32` constants. Lower number = more severe. Spaced by 100 so custom levels can be inserted between them.

```c
#define MEL_LOG_FATAL   100
#define MEL_LOG_ERROR   200
#define MEL_LOG_WARN    300
#define MEL_LOG_INFO    400
#define MEL_LOG_DEBUG   500
#define MEL_LOG_TRACE   600
```

Custom levels are registered at runtime:

```c
void mel_log_level_register(u32 value, str8 name);
str8 mel_log_level_name(u32 value);
```

Predefined levels are auto-registered during `mel_log_init`. If a level is not registered, `mel_log_level_name` returns `S8("UNKNOWN")`.

Sinks filter by threshold: a sink with `level_threshold = MEL_LOG_WARN` (300) accepts entries where `level <= 300` (i.e., FATAL, ERROR, WARN).

## Domains

Domains are plain `str8` string literals identifying the source subsystem: `S8("gpu")`, `S8("render")`, `S8("alloc")`, `S8("physics")`, etc.

No registration needed. Convention only. Sinks can filter on domain strings if they want to.

Domain strings MUST be string literals or otherwise static-lifetime. The logging system stores the pointer, it does not copy domain bytes.

## Log Entry

```c
typedef struct {
    u64     timestamp_ns;
    u32     level;
    str8    domain;
    str8    message;
    str8    file;
    u32     line;
    u32     thread_id;
    u64     global_frame;
    u64     sim_frame;
    u32     fixed_tick;
    str8    context;
} Mel_Log_Entry;
```

**Ownership:** when a sink's `write` callback receives a `Mel_Log_Entry*`, all fields are valid for the duration of that call only. The `message` and `context` str8 fields point into the writer thread's scratch buffer. If a sink needs to keep the data longer (e.g., batching for SQLite), it must copy.

**Static pointers:** `domain` and `file` point to static data (string literals / `__FILE__`). Valid for program lifetime.

## Core API

### Init / Shutdown

```c
typedef struct {
    usize   ring_buffer_size;
    u32     overflow_policy;
} Mel_Log_Init_Opt;

#define MEL_LOG_OVERFLOW_DROP   0
#define MEL_LOG_OVERFLOW_BLOCK  1

void mel_log_init_opt(Mel_Log_Init_Opt opt);
#define mel_log_init(...) mel_log_init_opt((Mel_Log_Init_Opt){__VA_ARGS__})

void mel_log_shutdown(void);
```

`mel_log_init`:
- Allocates the ring buffer (default: 1 MB).
- Registers predefined levels.
- Spawns the writer thread.
- Does NOT add any sinks. Caller adds what they need after init.

`mel_log_shutdown`:
- Signals the writer thread to drain remaining entries and exit.
- Blocks until the writer thread joins.
- Calls `destroy` on all registered sinks.
- Frees the ring buffer.

**Calling `mel_log` before `mel_log_init` or after `mel_log_shutdown`:** asserts. Offensive programming (MEL-X-006).

### Logging

```c
void mel__log(u32 level, str8 domain, const char* file, u32 line, const char* fmt, ...);
```

This is the internal function. Always called through macros:

```c
#define MEL_LOG(level, domain, fmt, ...) \
    mel__log((level), S8(domain), __FILE__, __LINE__, (fmt), ##__VA_ARGS__)

#define mel_log_fatal(domain, fmt, ...)  MEL_LOG(MEL_LOG_FATAL, domain, fmt, ##__VA_ARGS__)
#define mel_log_error(domain, fmt, ...)  MEL_LOG(MEL_LOG_ERROR, domain, fmt, ##__VA_ARGS__)
#define mel_log_warn(domain, fmt, ...)   MEL_LOG(MEL_LOG_WARN, domain, fmt, ##__VA_ARGS__)
#define mel_log_info(domain, fmt, ...)   MEL_LOG(MEL_LOG_INFO, domain, fmt, ##__VA_ARGS__)
#define mel_log_debug(domain, fmt, ...)  MEL_LOG(MEL_LOG_DEBUG, domain, fmt, ##__VA_ARGS__)
#define mel_log_trace(domain, fmt, ...)  MEL_LOG(MEL_LOG_TRACE, domain, fmt, ##__VA_ARGS__)
```

Usage:
```c
mel_log_info("gpu", "created pipeline %u", pipeline_id);
mel_log_error("render", "failed to acquire swapchain image");
mel_log_warn("alloc", "ring buffer at 90%% capacity");
```

`mel__log` does:
1. Format the message into a thread-local scratch buffer (vsnprintf).
2. Read the current thread's context stack and serialize it (e.g., `"physics/cloth_sim"`).
3. Read current frame counters (global_frame, sim_frame, fixed_tick) from thread-local state.
4. Serialize everything into a contiguous blob and push it into the ring buffer.
5. Return. The call site cost is format + memcpy. No I/O.

### Context Stack

Thread-local stack of tags that every log call within that scope automatically inherits.

```c
void mel_log_context_push(str8 tag);
void mel_log_context_pop(void);
```

Scoped helper using `__attribute__((cleanup))` (clang-only, MEL-X-002):

```c
#define MEL_LOG_SCOPE(tag) \
    __attribute__((cleanup(mel__log_context_cleanup))) \
    str8 mel__log_scope_##__LINE__ = (mel_log_context_push(S8(tag)), S8(tag))
```

Usage:
```c
void physics_step(void) {
    MEL_LOG_SCOPE("physics");

    mel_log_trace("verlet", "starting cloth sim");
    // entry.context = "physics"

    {
        MEL_LOG_SCOPE("cloth");
        mel_log_trace("verlet", "iteration %d", i);
        // entry.context = "physics/cloth"
    }
}
```

Works with early returns (cleanup fires on scope exit). Max context depth is compile-time configurable via `log.cfg.h` (default: 16).

Context exceeding max depth: assert.

### Frame Counters

Set by the engine/sim systems. Thread-local.

```c
void mel_log_set_global_frame(u64 frame);
void mel_log_set_sim_frame(u64 frame);
void mel_log_set_fixed_tick(u32 tick);
```

The engine frame loop calls `mel_log_set_global_frame` once per frame. `Mel_Sim_Ctx` calls `mel_log_set_sim_frame` and `mel_log_set_fixed_tick` at the appropriate points. Every log entry captures whatever values are current on the calling thread.

**Fiber/job migration:** when the fiber system context-switches, it must save and restore the log context stack and frame counters as part of the fiber state. This is a contract between `log.*` and `async.fiber.*`. The log system exposes:

```c
typedef struct {
    // opaque, but contains context stack snapshot + frame counters
} Mel_Log_Thread_State;

Mel_Log_Thread_State mel_log_thread_state_save(void);
void mel_log_thread_state_restore(Mel_Log_Thread_State state);
```

Fiber context switch calls save before suspending and restore after resuming. This keeps log context correct across fiber migrations.

### Sink Management

```c
typedef struct { u32 id; u32 gen; } Mel_Log_Sink_Handle;

Mel_Log_Sink_Handle mel_log_sink_add(Mel_Log_Sink* sink);
void                mel_log_sink_remove(Mel_Log_Sink_Handle handle);
void                mel_log_sink_flush_all(void);
```

`mel_log_sink_add`:
- Takes ownership of the sink pointer. The log system will call `destroy` on shutdown or removal.
- Returns a generational handle for later removal.
- Thread-safe. Can be called from any thread at any time after init.

`mel_log_sink_remove`:
- Removes the sink from the dispatch list.
- Calls `flush` then `destroy` on the removed sink.
- Stale handles (wrong generation) are no-ops.
- Thread-safe.

`mel_log_sink_flush_all`:
- Signals the writer thread to drain the ring buffer and flush all sinks.
- Blocks until complete.
- Use before crash handlers, before process exit, or when you need logs committed NOW.

**Sink list modification while writer thread is dispatching:** the sink list is protected by a read-write lock. Writer thread takes a read lock during dispatch (multiple sinks called in sequence). `sink_add`/`sink_remove` take a write lock. Adding/removing sinks is infrequent, so the write lock contention is negligible.

## Sink Interface

```c
typedef struct Mel_Log_Sink Mel_Log_Sink;

typedef void (*Mel_Log_Sink_Write_Fn)(Mel_Log_Sink* self, const Mel_Log_Entry* entry);
typedef void (*Mel_Log_Sink_Flush_Fn)(Mel_Log_Sink* self);
typedef void (*Mel_Log_Sink_Destroy_Fn)(Mel_Log_Sink* self);

struct Mel_Log_Sink {
    Mel_Log_Sink_Write_Fn   write;
    Mel_Log_Sink_Flush_Fn   flush;
    Mel_Log_Sink_Destroy_Fn destroy;
    u32                     level_threshold;
};
```

The writer thread calls `write` for each entry where `entry->level <= sink->level_threshold`. The log system checks the threshold before calling write -- sinks don't need to check it themselves.

`flush` is called when `mel_log_sink_flush_all` is invoked or on removal/shutdown.

`destroy` is called after the final `flush`, once. Must free all resources owned by the sink (including the `Mel_Log_Sink` struct itself if heap-allocated).

Custom sink example:
```c
typedef struct {
    Mel_Log_Sink base;
    int          socket_fd;
} My_Network_Sink;

void my_write(Mel_Log_Sink* self, const Mel_Log_Entry* entry) {
    My_Network_Sink* s = (My_Network_Sink*)self;
    // send entry over network
}

void my_flush(Mel_Log_Sink* self) { /* fsync or whatever */ }
void my_destroy(Mel_Log_Sink* self) { close(((My_Network_Sink*)self)->socket_fd); /* free self */ }

My_Network_Sink* sink = /* allocate */;
sink->base = (Mel_Log_Sink){
    .write = my_write,
    .flush = my_flush,
    .destroy = my_destroy,
    .level_threshold = MEL_LOG_INFO,
};
mel_log_sink_add(&sink->base);
```

## Ring Buffer Architecture

MPSC (Multiple Producer, Single Consumer) byte ring buffer.

**Producers** (any thread calling `mel__log`):
1. Format message into thread-local scratch buffer.
2. Compute total entry size (fixed header + message bytes + context bytes).
3. Atomically reserve space in the ring buffer (CAS on the write cursor).
4. Write the entry into the reserved region.
5. Mark the entry as committed (atomic store on a per-entry flag).

**Consumer** (writer thread):
1. Spin/sleep waiting for committed entries at the read cursor.
2. Deserialize entry into `Mel_Log_Entry` (pointing message/context into a scratch buffer).
3. For each registered sink: if `entry.level <= sink.level_threshold`, call `sink.write`.
4. Advance the read cursor.

**Overflow (ring buffer full):**
- `MEL_LOG_OVERFLOW_DROP` (default): producer increments an atomic dropped counter and returns. The writer thread periodically logs the drop count itself (as a WARN to all sinks).
- `MEL_LOG_OVERFLOW_BLOCK`: producer spins until space is available. Use for debugging when you cannot afford to lose entries.

**Entry wire format in the ring buffer:**
```
[u32 total_size] [u32 committed_flag] [u64 timestamp_ns] [u32 level]
[u32 thread_id] [u64 global_frame] [u64 sim_frame] [u32 fixed_tick]
[str8_ref domain] [str8_ref file] [u32 line]
[u16 message_len] [u16 context_len]
[message bytes ...] [context bytes ...]
```

`str8_ref` for domain/file is just the raw str8 (pointer + len) since they point to static data.

## Built-in Sinks

### Console Sink (`log.sink.console.c`)

```c
typedef struct {
    bool color;
} Mel_Log_Sink_Console_Opt;

Mel_Log_Sink* mel_log_sink_console_create_opt(Mel_Log_Sink_Console_Opt opt);
#define mel_log_sink_console_create(...) mel_log_sink_console_create_opt((Mel_Log_Sink_Console_Opt){__VA_ARGS__})
```

Writes to stderr. Format:
```
[timestamp] [LEVEL] [domain] (context) message    (file:line)
```

With `.color = true`, level names are colorized via ANSI codes (FATAL=red, ERROR=red, WARN=yellow, INFO=white, DEBUG=gray, TRACE=dark gray).

### SQLite Sink (`log.sink.sqlite.c`)

```c
typedef struct {
    str8    db_path;
    u32     batch_size;
    u32     flush_interval_ms;
} Mel_Log_Sink_Sqlite_Opt;

Mel_Log_Sink* mel_log_sink_sqlite_create_opt(Mel_Log_Sink_Sqlite_Opt opt);
#define mel_log_sink_sqlite_create(...) mel_log_sink_sqlite_create_opt((Mel_Log_Sink_Sqlite_Opt){__VA_ARGS__})
```

- `db_path`: path to the `.db` file. Created if it doesn't exist.
- `batch_size`: entries per transaction (default: 100).
- `flush_interval_ms`: max time between commits (default: 1000). Whichever comes first (batch full or timer expired) triggers a commit.

Opens the database in WAL mode for concurrent read access (query while writing).

Uses prepared statements for inserts. One `BEGIN`/`COMMIT` per batch.

**Schema:**
```sql
CREATE TABLE IF NOT EXISTS entries (
    id              INTEGER PRIMARY KEY,
    timestamp_ns    INTEGER NOT NULL,
    level           INTEGER NOT NULL,
    level_name      TEXT NOT NULL,
    domain          TEXT NOT NULL,
    message         TEXT NOT NULL,
    file            TEXT,
    line            INTEGER,
    thread_id       INTEGER,
    global_frame    INTEGER,
    sim_frame       INTEGER,
    fixed_tick      INTEGER,
    context         TEXT
);

CREATE INDEX IF NOT EXISTS idx_entries_level ON entries(level);
CREATE INDEX IF NOT EXISTS idx_entries_domain ON entries(domain);
CREATE INDEX IF NOT EXISTS idx_entries_timestamp ON entries(timestamp_ns);
CREATE INDEX IF NOT EXISTS idx_entries_global_frame ON entries(global_frame);

CREATE TABLE IF NOT EXISTS metadata (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
```

The `metadata` table stores run info on creation:
- `run_start`: ISO 8601 timestamp
- `platform`: OS/arch string
- `engine_version`: melody version if available

**Ownership:** The sink owns the sqlite3 handle. `destroy` closes it.

**Failure behavior:** If a write or commit fails, the sink logs the SQLite error to stderr (not through the log system -- avoids recursion) and continues. Failed entries are lost. The sink does not crash the engine.

### File Sink (`log.sink.file.c`)

```c
typedef struct {
    str8    file_path;
} Mel_Log_Sink_File_Opt;

Mel_Log_Sink* mel_log_sink_file_create_opt(Mel_Log_Sink_File_Opt opt);
#define mel_log_sink_file_create(...) mel_log_sink_file_create_opt((Mel_Log_Sink_File_Opt){__VA_ARGS__})
```

Plain text append. Same format as console (without ANSI colors). Flushes with fflush on `flush` callback.

## Thread Safety

| Function | Thread safety |
|---|---|
| `mel_log_init` / `mel_log_shutdown` | Main thread only. Not concurrent with anything. |
| `mel__log` (via macros) | Any thread. Lock-free (ring buffer CAS). |
| `mel_log_context_push/pop` | Thread-local. No synchronization needed. |
| `mel_log_set_global_frame` etc. | Thread-local. No synchronization needed. |
| `mel_log_thread_state_save/restore` | Called on the thread being saved/restored. |
| `mel_log_sink_add/remove` | Any thread. Write-locked against writer thread. |
| `mel_log_sink_flush_all` | Any thread. Blocks until writer drains and flushes. |
| `mel_log_level_register` | Any thread. Internally synchronized. |

## Typical Setup

```c
mel_log_init(.ring_buffer_size = 2 * 1024 * 1024);

mel_log_sink_add(mel_log_sink_console_create(.color = true));

mel_log_sink_add(mel_log_sink_sqlite_create(
    .db_path = S8("logs/session.db"),
    .batch_size = 200,
    .flush_interval_ms = 500,
));

mel_log_info("engine", "logging system online");
```

## Future Extensions (not in scope now)

- **In-engine log viewer (imgui):** registers as a sink for live tail, queries SQLite for historical/filtered views. Async queries via the job system so the UI thread doesn't block.
- **Backtrace capture for FATAL/ERROR:** optionally capture `mel_backtrace_capture` for severe entries. Stored as a separate column or linked table.
- **Custom key-value tags per entry:** structured metadata beyond the context stack. Could be a JSON column or a separate tags table.
- **Log rotation / retention:** max DB size, auto-delete entries older than N, multiple DB files per session.
- **Remote sink:** ship logs over the network to a central collector.
- **`db.*` module:** general database abstraction, separate design. The SQLite sink uses sqlite3 directly, not through `db.*`.
