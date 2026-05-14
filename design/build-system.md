# Build System — Module Plan

## Overview

Melody's build system replaces `nob.h`/`nob.c` with Melody-native modules + a build application. The build system is an application (`build.c`) using reusable Melody modules.

## Modules to create / complete

### 1. `process` — COMPLETE from stub

**Status:** Partially exists. Has `Mel_Proc`, `Mel_Fd`, `Mel_Cmd` typedefs (as `uint64_t` — broken), and `mel__cmd_start_process` with POSIX+Win32 platform impls. The implementation code still uses nob types (`Nob_Cmd`, `nob_da_append_many`, `nob_cmd_render`). Must be rewritten with Melody-native types.

**Dependencies:** `core.types`, `collection.array` (for `Mel_Cmd`), `string.str8` (for rendering), `time` (for timing).

**Files:**
```
modules/process/
├── include/
│   ├── process.h          # public API
│   ├── process.fwd.h      # forward declarations
│   └── process.cfg.h      # configuration (echo on/off, etc.)
└── src/
    ├── process.c           # dispatch + shared logic
    ├── process.posix.c     # POSIX impl (fork/exec/waitpid)
    └── process.win32.c     # Win32 impl (CreateProcess)
```

**Types:**
```c
typedef struct Mel_Cmd {
    str8* items;
    usize count;
    usize capacity;
} Mel_Cmd;

// OS process handle (int on POSIX, HANDLE on Win32)
typedef uint64_t Mel_Proc;

// OS file descriptor (int on POSIX, HANDLE on Win32)
typedef uint64_t Mel_Fd;

typedef struct Mel_Proc_Pool {
    Mel_Proc* items;
    usize count;
    usize capacity;
} Mel_Proc_Pool;

typedef struct Mel_Cmd_Redirect {
    // File paths (opens fd internally)
    const char* stdin_path;
    const char* stdout_path;
    const char* stderr_path;
    // Or raw FDs (mutually exclusive with paths)
    Mel_Fd fd_in;
    Mel_Fd fd_out;
    Mel_Fd fd_err;
} Mel_Cmd_Redirect;

typedef struct Mel_Cmd_Opt {
    // Run async: returns immediately, appends to pool
    Mel_Proc_Pool* async;
    // Max processes in the pool (0 = mel_nprocs())
    usize max_procs;
    // Redirect stdin/stdout/stderr
    Mel_Cmd_Redirect* redirect;
    // Don't reset cmd.count after execution
    bool dont_reset;
    // Echo the command via log before running
    bool echo;
} Mel_Cmd_Opt;
```

**Public API:**
```c
// Cmd building
#define mel_cmd_append(cmd, ...)  /* variadic macro */
void mel_cmd_appendv(Mel_Cmd* cmd, const char** args, usize count);
void mel_cmd_extend(Mel_Cmd* cmd, const Mel_Cmd* other);
str8 mel_cmd_render(Mel_Cmd cmd, Mel_Arena* arena);
void mel_cmd_free(Mel_Cmd* cmd);

// Cmd execution (sync and async)
bool mel_cmd_run(Mel_Cmd* cmd);
bool mel_cmd_run_opt(Mel_Cmd* cmd, Mel_Cmd_Opt opt);
bool mel_cmd_run_async(Mel_Cmd* cmd); // fire-and-forget, returns proc handle
Mel_Proc mel_cmd_run_async_ex(Mel_Cmd* cmd, Mel_Cmd_Redirect* redirect);

// Process management
bool mel_proc_wait(Mel_Proc proc);
bool mel_procs_wait(Mel_Proc_Pool* pool);    // wait for all, clear pool
void mel_procs_flush(Mel_Proc_Pool* pool);   // wait for all, don't clear

// FD operations (for direct file descriptors)
Mel_Fd mel_fd_open_read(const char* path);
Mel_Fd mel_fd_open_write(const char* path);
void   mel_fd_close(Mel_Fd fd);

// Utility
i32 mel_nprocs(void);  // number of CPU cores
```

**Implementation notes:**
- `mel_cmd_run_opt` is the central function. Sync is the default (no `.async` field). When `.async` is set, the process is appended to the pool and the function blocks if pool reaches `max_procs`.
- `mel_cmd_render` produces a display-ready string like `CMD: cc -Wall -o main main.c` for logging.
- Platform files use compile-time guards (`#ifdef _WIN32` etc.) in the dispatch layer.
- `Mel_Cmd` is a dynamic array of `str8` — each item is a non-owning view. The actual string data comes from temp/scratch arenas managed by the caller.
- `Mel_Fd` is `uint64_t` which fits both `int` (POSIX) and `HANDLE` (Win32 pointer-sized). On 32-bit it's oversized but harmless.

---

### 2. `build` — NEW

**Status:** Does not exist. Build system primitives.

**Dependencies:** None from modules. Uses raw POSIX/Win32 syscalls (`stat`/`GetFileTime`, `open`/`read`) until `fs` module exists.

**Files:**
```
modules/build/
├── include/
│   ├── build.h            # public API
│   └── build.fwd.h        # forward declarations
└── src/
    └── build.c            # implementation
```

**Public API:**
```c
// Rebuild detection
// Returns: -1 on error, 0 if output is up-to-date, 1 if rebuild needed
i32 mel_build_needs_rebuild(const char* output_path, const char** input_paths, usize count);
i32 mel_build_needs_rebuild1(const char* output_path, const char* input_path);

// Depfile parsing
// Parses clang's -MD/-MF output format:
//   "output.o: dep1.c dep2.h\\\n dep3.h"
// Returns list of dependency paths in the arena.
bool mel_build_parse_depfile(const char* path, /* arena + output list */);
```

**Implementation notes:**
- `mel_build_needs_rebuild`: stat() the output, if it doesn't exist → needs rebuild. For each input, get mtime; if any input is newer than output → needs rebuild. If any input doesn't exist → error.
- `mel_build_parse_depfile`: reads the depfile (raw open/read), skips the target (before `:`), then parses space-separated paths with `\` line continuation support.
- Will migrate to `fs` module for I/O and `string.str8` for parsing once those are mature enough.

---

## Modules NOT being created (deferred)

### `fs` — Filesystem I/O
Deferred. Needs async-first design. The build application uses raw `stat`/`open`/`read`/`write` for now. When `fs` is ready, `build` module and `build.c` will migrate to it.

### `temp` — Scratch memory
Killed. `allocator.arena` already provides `Mel_Arena_Scratch` (save/rewind checkpoints) and `str8_fmt_arena` covers formatted string building. The build application creates its own arena for scratch space.

---

## Application: `tools/build/build.c`

The actual Melody build script. Lives in `tools/build/`, replaces `nob.c`. Uses the modules above plus `string.*`, `collection.array`, `time`, `allocator.arena`.

Key responsibilities:
- Self-rebuild: `MEL_REBUILD_URSELF(argc, argv)` macro — uses `mel_build_needs_rebuild1` + `mel_cmd_run`
- Source collection: recursive directory walking (raw syscalls for now)
- Platform/backend suffix filtering
- Build signature: hash of compile flags for cache invalidation
- Compilation: C, C++, ObjC, ASM → .o files
- Archiving: `ar rcs` → `libmelody.a`
- Linking: executables for examples, demos, tests, showcases
- `compile_commands.json` generation
- Subcommands: build, test, example, demo, showcase, clean, compdb

---

## Order of implementation

1. **`process` module** — complete it. This is the hard one and everything depends on it.
2. **`build` module** — small, quick to implement.
3. **`build.c`** — the application, using 1 and 2.
