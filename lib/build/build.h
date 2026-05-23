#ifndef MEL_BUILD_H
#define MEL_BUILD_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    MEL_PLATFORM_MACOS,
    MEL_PLATFORM_IOS,
    MEL_PLATFORM_LINUX,
    MEL_PLATFORM_ANDROID,
    MEL_PLATFORM_WIN32,
    MEL_PLATFORM_WEB,
    MEL_PLATFORM_COUNT,
} Mel_Platform;

typedef enum {
    MEL_CONFIG_DEBUG,
    MEL_CONFIG_RELEASE,
} Mel_Config;

typedef enum {
    MEL_TARGET_LIBRARY,
    MEL_TARGET_APPLICATION,
    MEL_TARGET_THIRD_PARTY,
    MEL_TARGET_META,
} Mel_Target_Kind;

typedef enum {
    MEL_PRIVATE,
    MEL_PUBLIC,
} Mel_Visibility;

typedef enum {
    MEL_STAGE_CONFIGURE,
    MEL_STAGE_COMPILE,
    MEL_STAGE_LINK,
    MEL_STAGE_PACKAGE,
    MEL_STAGE_COUNT,
} Mel_Stage;

typedef struct Mel_Build_Target  Mel_Build_Target;
typedef struct Mel_Build_Context Mel_Build_Context;

typedef bool (*Mel_Build_Stage_Fn)(Mel_Build_Context *ctx);

// Every target's build.c must export this. The framework calls it once to
// gather the target's declarative configuration and register callbacks.
bool project(Mel_Build_Target *t);

// --- Declarative configuration (call inside project()) ---

void mel_build_set_name(Mel_Build_Target *t, const char *name);
void mel_build_set_kind(Mel_Build_Target *t, Mel_Target_Kind kind);

// Platforms this target supports. Omit to support all.
void mel_build_set_platforms(Mel_Build_Target *t, const Mel_Platform *platforms, size_t count);

// Directory whose sources (recursively, with platform chains) are compiled
// for this target. For libraries/apps. May be called more than once.
void mel_build_add_source_root(Mel_Build_Target *t, const char *dir);

// Each immediate subdir of modules_dir is a module: <m>/include becomes a
// public include and <m>/src a platform-aware source root.
void mel_build_add_modules(Mel_Build_Target *t, const char *modules_dir);

// Name of another target this one depends on. Public properties of the
// dependency propagate to this target transitively.
void mel_build_add_dependency(Mel_Build_Target *t, const char *dep_name);

// PUBLIC properties propagate to dependents; PRIVATE stay local.
void mel_build_add_cflag(Mel_Build_Target *t, Mel_Visibility vis, const char *flag);
void mel_build_add_include(Mel_Build_Target *t, Mel_Visibility vis, const char *dir);
void mel_build_add_define(Mel_Build_Target *t, Mel_Visibility vis, const char *def);
void mel_build_add_link_flag(Mel_Build_Target *t, Mel_Visibility vis, const char *flag);

// Platform-gated variants: the property applies only when building for p.
void mel_build_add_cflag_on(Mel_Build_Target *t, Mel_Visibility vis, Mel_Platform p, const char *flag);
void mel_build_add_include_on(Mel_Build_Target *t, Mel_Visibility vis, Mel_Platform p, const char *dir);
void mel_build_add_define_on(Mel_Build_Target *t, Mel_Visibility vis, Mel_Platform p, const char *def);
void mel_build_add_link_flag_on(Mel_Build_Target *t, Mel_Visibility vis, Mel_Platform p, const char *flag);

// Which UI backend a platform uses; gates per-backend widget sources.
void mel_build_backend(Mel_Build_Target *t, Mel_Platform p, const char *backend);

// Key/value used to expand {{KEY}} placeholders in platform scaffolding
// templates during the configure stage.
void mel_build_set_config(Mel_Build_Target *t, const char *key, const char *value);

// Additive stage callbacks. Framework defaults run first, in registration
// order, then these.
void mel_build_on_configure(Mel_Build_Target *t, Mel_Build_Stage_Fn fn);
void mel_build_on_compile(Mel_Build_Target *t, Mel_Build_Stage_Fn fn);
void mel_build_on_link(Mel_Build_Target *t, Mel_Build_Stage_Fn fn);
void mel_build_on_package(Mel_Build_Target *t, Mel_Build_Stage_Fn fn);

// Suppress the framework default for a stage; the default callback consults
// this flag and returns immediately.
void mel_build_suppress_default(Mel_Build_Target *t, Mel_Stage stage);

// --- Context API (call inside stage callbacks) ---

Mel_Platform  mel_build_ctx_platform(const Mel_Build_Context *ctx);
Mel_Config    mel_build_ctx_config(const Mel_Build_Context *ctx);
const char   *mel_build_ctx_target_name(const Mel_Build_Context *ctx);
const char   *mel_build_ctx_backend(const Mel_Build_Context *ctx);

// Add a source for the compile stage to build (used from fetch_sources-style
// callbacks for code generation outputs, etc.).
void mel_build_ctx_add_source(Mel_Build_Context *ctx, const char *path);

// Resolved output directory for this target/platform/config.
const char *mel_build_ctx_out_dir(const Mel_Build_Context *ctx);

// Resolved final artifact path (library archive, executable, ...).
const char *mel_build_ctx_artifact(const Mel_Build_Context *ctx);

const char *mel_platform_name(Mel_Platform p);

// --- Third-party build helpers (call from a third-party target's on_compile) ---
// Each builds into the target's resolved prefix; dependents pick up the
// prefix's include/ and lib/ automatically through dependency propagation.

bool mel_tp_single_tu(Mel_Build_Context *ctx, const char *src,
                      const char *const *cflags, size_t cflags_count,
                      const char *const *headers, size_t headers_count);
bool mel_tp_cmake(Mel_Build_Context *ctx, const char *src_rel,
                  const char *const *args, size_t args_count, const char *produced_lib);
bool mel_tp_autotools(Mel_Build_Context *ctx, const char *src_rel, const char *extra_arg,
                      const char *produced_lib);
const char *mel_tp_prefix(Mel_Build_Context *ctx);
const char *mel_tp_dep_prefix(Mel_Build_Context *ctx, const char *target_name);

#endif
