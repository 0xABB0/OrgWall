#ifndef MEL_BUILD_H
#define MEL_BUILD_H

#if defined(_CLANGD) && !defined(NOB_H_)
#define NOB_STRIP_PREFIX
#include "../../nob.h"
#include <string.h>
#endif

#include <stdbool.h>
#include <stddef.h>

// The mel_build_*/mel_tp_* API is statically linked into each target module
// (via libmelbuild.a), so no import/export annotations are needed. project()
// lives in the target DLL and must stay visible for the runner to dlsym it.
#define MEL_API
#if defined(_WIN32)
#  define MEL_EXPORT __declspec(dllexport)
#else
#  define MEL_EXPORT
#endif

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
MEL_EXPORT bool project(Mel_Build_Target *t);

// --- Declarative configuration (call inside project()) ---

MEL_API void mel_build_set_name(Mel_Build_Target *t, const char *name);
MEL_API void mel_build_set_kind(Mel_Build_Target *t, Mel_Target_Kind kind);

// Platforms this target supports. Omit to support all.
MEL_API void mel_build_set_platforms(Mel_Build_Target *t, const Mel_Platform *platforms, size_t count);

// Directory whose sources (recursively, with platform chains) are compiled
// for this target. For libraries/apps. May be called more than once.
MEL_API void mel_build_add_source_root(Mel_Build_Target *t, const char *dir);

// Each immediate subdir of modules_dir is a module: <m>/include becomes a
// public include and <m>/src a platform-aware source root.
MEL_API void mel_build_add_modules(Mel_Build_Target *t, const char *modules_dir);

// Exclude an immediate subdir of any modules_dir on the given platform. The
// excluded module's sources and includes are skipped entirely. Use when a
// module only makes sense on some platforms (e.g. depends on a third-party
// library that isn't bootstrapped on the excluded platform).
MEL_API void mel_build_exclude_module_on(Mel_Build_Target *t, Mel_Platform p, const char *module_name);

// Exclude a single source file by basename on the given platform. Use for
// individual files in an otherwise-cross-platform module that depend on a
// third-party library not available on the excluded platform.
MEL_API void mel_build_exclude_source_on(Mel_Build_Target *t, Mel_Platform p, const char *basename);

// Exclude a module on a given runtime (e.g. "wasi"). The platform axis can't
// distinguish web's two runtimes, so use this when a module makes sense under
// one wasm runtime but not the other (e.g. a DOM GUI under emscripten but not
// the DOM-less wasi compute runtime).
MEL_API void mel_build_exclude_module_on_runtime(Mel_Build_Target *t, const char *runtime, const char *module_name);

// Name of another target this one depends on. Public properties of the
// dependency propagate to this target transitively.
MEL_API void mel_build_add_dependency(Mel_Build_Target *t, const char *dep_name);

// PUBLIC properties propagate to dependents; PRIVATE stay local. Each accepts
// one or more value tokens, recorded as distinct flags — pass split arguments
// for flags that the compiler/linker expects as separate argv entries, e.g.
// mel_build_add_link_flag_on(t, MEL_PUBLIC, MEL_PLATFORM_MACOS, "-framework", "Cocoa").
// The wrapper macros append the NULL terminator the variadic impls scan for.
MEL_API void mel_build_add_cflag_(Mel_Build_Target *t, Mel_Visibility vis, ...);
MEL_API void mel_build_add_include_(Mel_Build_Target *t, Mel_Visibility vis, ...);
MEL_API void mel_build_add_define_(Mel_Build_Target *t, Mel_Visibility vis, ...);
MEL_API void mel_build_add_link_flag_(Mel_Build_Target *t, Mel_Visibility vis, ...);

#define mel_build_add_cflag(t, vis, ...)     mel_build_add_cflag_(t, vis, __VA_ARGS__, NULL)
#define mel_build_add_include(t, vis, ...)   mel_build_add_include_(t, vis, __VA_ARGS__, NULL)
#define mel_build_add_define(t, vis, ...)    mel_build_add_define_(t, vis, __VA_ARGS__, NULL)
#define mel_build_add_link_flag(t, vis, ...) mel_build_add_link_flag_(t, vis, __VA_ARGS__, NULL)

// Platform-gated variants: the property applies only when building for p.
MEL_API void mel_build_add_cflag_on_(Mel_Build_Target *t, Mel_Visibility vis, Mel_Platform p, ...);
MEL_API void mel_build_add_include_on_(Mel_Build_Target *t, Mel_Visibility vis, Mel_Platform p, ...);
MEL_API void mel_build_add_define_on_(Mel_Build_Target *t, Mel_Visibility vis, Mel_Platform p, ...);
MEL_API void mel_build_add_link_flag_on_(Mel_Build_Target *t, Mel_Visibility vis, Mel_Platform p, ...);

#define mel_build_add_cflag_on(t, vis, p, ...)     mel_build_add_cflag_on_(t, vis, p, __VA_ARGS__, NULL)
#define mel_build_add_include_on(t, vis, p, ...)   mel_build_add_include_on_(t, vis, p, __VA_ARGS__, NULL)
#define mel_build_add_define_on(t, vis, p, ...)    mel_build_add_define_on_(t, vis, p, __VA_ARGS__, NULL)
#define mel_build_add_link_flag_on(t, vis, p, ...) mel_build_add_link_flag_on_(t, vis, p, __VA_ARGS__, NULL)

// Runtime-gated variants: the property applies only when the active runtime
// matches (e.g. "emscripten" but not "wasi"). Use for toolchain flags that one
// runtime understands and another rejects.
MEL_API void mel_build_add_cflag_on_runtime_(Mel_Build_Target *t, Mel_Visibility vis, const char *runtime, ...);
MEL_API void mel_build_add_link_flag_on_runtime_(Mel_Build_Target *t, Mel_Visibility vis, const char *runtime, ...);

#define mel_build_add_cflag_on_runtime(t, vis, rt, ...)     mel_build_add_cflag_on_runtime_(t, vis, rt, __VA_ARGS__, NULL)
#define mel_build_add_link_flag_on_runtime(t, vis, rt, ...) mel_build_add_link_flag_on_runtime_(t, vis, rt, __VA_ARGS__, NULL)

// GPU-backend-gated variants: the property applies only when the active gpu
// backend matches (e.g. "vulkan", "metal", "webgpu"). Use for backend-specific
// libraries (e.g. the Vulkan loader only when the vulkan backend is built).
MEL_API void mel_build_add_cflag_on_gpu_(Mel_Build_Target *t, Mel_Visibility vis, const char *gpu_backend, ...);
MEL_API void mel_build_add_link_flag_on_gpu_(Mel_Build_Target *t, Mel_Visibility vis, const char *gpu_backend, ...);

#define mel_build_add_cflag_on_gpu(t, vis, gpu, ...)     mel_build_add_cflag_on_gpu_(t, vis, gpu, __VA_ARGS__, NULL)
#define mel_build_add_link_flag_on_gpu(t, vis, gpu, ...) mel_build_add_link_flag_on_gpu_(t, vis, gpu, __VA_ARGS__, NULL)

// Override the UI backend or runtime for a platform. Omit to take the
// framework default (e.g. macos -> cocoa backend, web -> emscripten runtime).
MEL_API void mel_build_use_backend_on(Mel_Build_Target *t, Mel_Platform p, const char *backend);
MEL_API void mel_build_use_runtime_on(Mel_Build_Target *t, Mel_Platform p, const char *runtime);

// Override the GPU backend for a platform. This axis is independent of the UI
// backend: it selects which src/<gpu_backend>/ subdir a module compiles (metal,
// vulkan, dx12, webgpu). Omit to take the per-platform default (apple -> metal,
// win32 -> dx12, linux/android -> vulkan, web -> webgpu). Lets a portable
// backend (e.g. vulkan via MoltenVK) be forced where another is the default.
MEL_API void mel_build_use_gpu_backend_on(Mel_Build_Target *t, Mel_Platform p, const char *gpu_backend);

// --- Web platform configuration (consulted only when building for web) ---
//
// Defaults aim at the smallest viable web app: single-threaded, no Asyncify,
// emscripten toolchain. Apps that need more flip these in project().

// Spin up real Workers backed by SharedArrayBuffer. Requires the deployment to
// serve COOP/COEP headers; the dev server started by `nob run` sets them.
MEL_API void mel_build_web_threading(Mel_Build_Target *t, bool enable);

// Enable Asyncify so blocking C can yield into JS-backed async I/O. Roughly
// doubles the wasm size.
MEL_API void mel_build_web_asyncify(Mel_Build_Target *t, bool enable);

// Key/value used to expand {{KEY}} placeholders in platform scaffolding
// templates during the configure stage.
MEL_API void mel_build_set_config(Mel_Build_Target *t, const char *key, const char *value);

// Additive stage callbacks. Framework defaults run first, in registration
// order, then these.
MEL_API void mel_build_on_configure(Mel_Build_Target *t, Mel_Build_Stage_Fn fn);
MEL_API void mel_build_on_compile(Mel_Build_Target *t, Mel_Build_Stage_Fn fn);
MEL_API void mel_build_on_link(Mel_Build_Target *t, Mel_Build_Stage_Fn fn);
MEL_API void mel_build_on_package(Mel_Build_Target *t, Mel_Build_Stage_Fn fn);

// Suppress the framework default for a stage; the default callback consults
// this flag and returns immediately.
MEL_API void mel_build_suppress_default(Mel_Build_Target *t, Mel_Stage stage);

// --- Context API (call inside stage callbacks) ---

MEL_API Mel_Platform  mel_build_ctx_platform(const Mel_Build_Context *ctx);
MEL_API Mel_Config    mel_build_ctx_config(const Mel_Build_Context *ctx);
MEL_API const char   *mel_build_ctx_target_name(const Mel_Build_Context *ctx);
MEL_API const char   *mel_build_ctx_backend(const Mel_Build_Context *ctx);
MEL_API const char   *mel_build_ctx_runtime(const Mel_Build_Context *ctx);

// Add a source for the compile stage to build (used from fetch_sources-style
// callbacks for code generation outputs, etc.).
MEL_API void mel_build_ctx_add_source(Mel_Build_Context *ctx, const char *path);

// Resolved output directory for this target/platform/config.
MEL_API const char *mel_build_ctx_out_dir(const Mel_Build_Context *ctx);

// Resolved final artifact path (library archive, executable, ...).
MEL_API const char *mel_build_ctx_artifact(const Mel_Build_Context *ctx);

MEL_API const char *mel_platform_name(Mel_Platform p);

// --- Third-party build helpers (call from a third-party target's on_compile) ---
// Each builds into the target's resolved prefix; dependents pick up the
// prefix's include/ and lib/ automatically through dependency propagation.

MEL_API bool mel_tp_single_tu(Mel_Build_Context *ctx, const char *src,
                              const char *const *cflags, size_t cflags_count,
                              const char *const *headers, size_t headers_count);
MEL_API bool mel_tp_cmake(Mel_Build_Context *ctx, const char *src_rel,
                          const char *const *args, size_t args_count, const char *produced_lib);
MEL_API bool mel_tp_autotools(Mel_Build_Context *ctx, const char *src_rel, const char *extra_arg,
                              const char *produced_lib);
MEL_API const char *mel_tp_prefix(Mel_Build_Context *ctx);
MEL_API const char *mel_tp_dep_prefix(Mel_Build_Context *ctx, const char *target_name);

#endif
