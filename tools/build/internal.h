// Internal definitions shared between the build library (build.c, archived into
// libmelbuild.a and statically linked by every target's build.c) and the build
// runner (runner.c, the engine compiled into the nob driver). The public API
// header build.h stays opaque; this header carries the concrete target/context
// layout and the helper surface the two translation units share.
//
// In the nob driver both build.c and runner.c are part of one translation unit
// (nob.c includes build.c then runner.c), so static helpers in build.c are
// visible to runner.c. build.c also compiles standalone for the archive, so its
// public-API closure must reference nothing defined only in runner.c.
#ifndef MEL_BUILD_INTERNAL_H
#define MEL_BUILD_INTERNAL_H

#ifndef NOB_STRIP_PREFIX
#define NOB_STRIP_PREFIX
#endif
#ifndef NOB_NO_ECHO
#define NOB_NO_ECHO
#endif
#ifndef NOB_TEMP_CAPACITY
#define NOB_TEMP_CAPACITY (64*1024*1024)
#endif
#include "../../nob.h"
#include "build.h"

#include <stdint.h>
#include <sys/stat.h>
#include <time.h>

#ifndef _WIN32
#include <sys/time.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

// Win32 shims for the small POSIX surface the build-cache uses.
#ifndef S_ISREG
#  define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
#define popen  _popen
#define pclose _pclose
#endif

#ifndef MEL_BUILD_DIR
#define MEL_BUILD_DIR "build"
#endif
#define MEL_CACHE_DIR MEL_BUILD_DIR "/cache"

#define MEL_MAX_STAGE_CBS 16

// A property value carries a platform mask: 0 means "all platforms", otherwise
// bit (1u << platform) gates the value to specific platforms.
typedef struct { const char *value; uint32_t mask; } Prop;
typedef struct { Prop *items; size_t count, capacity; } Prop_List;

typedef struct {
    Prop_List cflags;
    Prop_List includes;
    Prop_List defines;
    Prop_List link_flags;
} Props;

// Platform-resolved (flat) properties for a single build context.
typedef struct {
    File_Paths cflags;
    File_Paths includes;
    File_Paths defines;
    File_Paths link_flags;
} Resolved;

struct Mel_Build_Target {
    const char *name;
    const char *dir;            // directory containing this target's build.c
    Mel_Target_Kind kind;

    bool platform_set;
    bool platforms[MEL_PLATFORM_COUNT];

    File_Paths source_roots;    // recursively globbed, platform-aware
    File_Paths module_roots;    // dirs whose immediate children are modules
    File_Paths deps;            // dependency target names
    Prop_List  excluded_modules;// platform-masked module-basename exclusions
    Prop_List  excluded_sources;// platform-masked source-basename exclusions

    File_Paths cfg_keys;        // scaffolding template substitution keys
    File_Paths cfg_vals;        // parallel to cfg_keys

    Props pub;
    Props priv;

    const char *backends[MEL_PLATFORM_COUNT];

    // Web platform knobs (see build.h). web_toolchain NULL => emscripten.
    bool        web_threading;
    bool        web_asyncify;
    const char *web_toolchain;

    Mel_Build_Stage_Fn user_cbs[MEL_STAGE_COUNT][MEL_MAX_STAGE_CBS];
    size_t             user_cb_count[MEL_STAGE_COUNT];
    bool               suppress_default[MEL_STAGE_COUNT];

    // third-party prefix (set when kind == THIRD_PARTY)
    const char *tp_prefix;

    void *dl_handle;
};

// Cross-compilation descriptor; non-NULL on the context selects a toolchain and
// an ABI sub-axis (used by Android, which builds per-ABI).
typedef struct {
    const char *abi;      // e.g. "arm64-v8a"
    const char *triple;   // e.g. "aarch64-linux-android"
    int         api;      // e.g. 23
    const char *cc;       // NDK clang wrapper
    const char *ar;
    const char *ranlib;
    const char *sysroot;
    const char *ndk;      // NDK root (for the cmake toolchain file)
} Cross;

struct Mel_Build_Context {
    Mel_Build_Target *target;
    Mel_Platform platform;
    Mel_Config config;
    const Cross *cross;   // NULL for native host builds

    const char *out_dir;
    const char *artifact;

    // Fully resolved compile inputs (own + transitive public from deps).
    Resolved resolved;

    File_Paths sources;       // sources to compile this target
    File_Paths bridge_sources;// bridge sources (compiled into dependents, not archived)
    File_Paths objects;       // produced object files
};

#endif
