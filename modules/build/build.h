#ifndef MEL_BUILD_H
#define MEL_BUILD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__cplusplus)
#define MEL_API extern "C"
#else
#define MEL_API extern
#endif

typedef enum
{
    MEL_PRIVATE,
    MEL_PUBLIC
} Mel_Visibility;

typedef enum
{
    MEL_PLATFORM_MACOS,
    MEL_PLATFORM_IOS,
    MEL_PLATFORM_LINUX,
    MEL_PLATFORM_ANDROID,
    MEL_PLATFORM_WIN32,
    MEL_PLATFORM_WASM,
    MEL_PLATFORM_COUNT
} Mel_Platform;

typedef struct
{
    uint32_t    platforms;
    const char* config;
    const char* backend;
    const char* gpu;
    const char* runtime;
    const char* arch;
} Mel_When;

#define MEL_ON(p) (1u << (MEL_PLATFORM_##p))
#define WHEN(...) ((Mel_When){ __VA_ARGS__ })
#define ALWAYS    ((Mel_When){ 0 })

typedef struct Mel_Build  Mel_Build;
typedef struct Mel_Target Mel_Target;

MEL_API Mel_Target* mel_add_library(Mel_Build* b, const char* name);
MEL_API Mel_Target* mel_add_executable(Mel_Build* b, const char* name);
MEL_API Mel_Target* mel_add_third_party(Mel_Build* b, const char* name);
MEL_API Mel_Target* mel_add_host_tool(Mel_Build* b, const char* name);
MEL_API Mel_Target* mel_add_test(Mel_Build* b, const char* name);

MEL_API void mel_depends(Mel_Target* t, const char* name);
MEL_API void mel_depends_host(Mel_Target* t, const char* name);
MEL_API void mel_unavailable(Mel_Target* t, Mel_When when);
MEL_API void mel_whole_archive(Mel_Target* t, Mel_When when);
MEL_API void mel_manifest(Mel_Target* t, const char* key, const char* value);
MEL_API void mel_subsystem(Mel_Target* t, const char* subsystem);
MEL_API void mel_android_manifest(Mel_Target* t, const char* path);
MEL_API void mel_android_java(Mel_Target* t, const char* dir);
MEL_API void mel_android_namespace(Mel_Target* t, const char* ns);

MEL_API void mel_sources_(Mel_Target* t, Mel_When when, ...);
MEL_API void mel_exclude_source_(Mel_Target* t, Mel_When when, ...);
MEL_API void mel_cflags_(Mel_Target* t, Mel_Visibility vis, Mel_When when, ...);
MEL_API void mel_defines_(Mel_Target* t, Mel_Visibility vis, Mel_When when, ...);
MEL_API void mel_includes_(Mel_Target* t, Mel_Visibility vis, Mel_When when, ...);
MEL_API void mel_link_(Mel_Target* t, Mel_Visibility vis, Mel_When when, ...);
MEL_API void mel_cmake_(Mel_Target* t, const char* dir, ...);
MEL_API void mel_cmake_when(Mel_Target* t, Mel_When when);
MEL_API void mel_prebuilt(Mel_Target* t, Mel_When when, const char* url, const char* lib);
MEL_API void mel_configure_(Mel_Target* t, const char* dir, ...);
MEL_API void mel_configure_cstd(Mel_Target* t, const char* std);
MEL_API void mel_codegen_(Mel_Target* t, const char* tool, const char* output, ...);
#define mel_codegen(t, tool, output, ...) mel_codegen_(t, tool, output __VA_OPT__(, ) __VA_ARGS__, NULL)

#define mel_sources(t, when, ...)         mel_sources_(t, when __VA_OPT__(, ) __VA_ARGS__, NULL)
#define mel_exclude_source(t, when, ...)  mel_exclude_source_(t, when __VA_OPT__(, ) __VA_ARGS__, NULL)
#define mel_cflags(t, vis, when, ...)     mel_cflags_(t, vis, when __VA_OPT__(, ) __VA_ARGS__, NULL)
#define mel_defines(t, vis, when, ...)    mel_defines_(t, vis, when __VA_OPT__(, ) __VA_ARGS__, NULL)
#define mel_includes(t, vis, when, ...)   mel_includes_(t, vis, when __VA_OPT__(, ) __VA_ARGS__, NULL)
#define mel_link(t, vis, when, ...)       mel_link_(t, vis, when __VA_OPT__(, ) __VA_ARGS__, NULL)
#define mel_cmake(t, dir, ...)            mel_cmake_(t, dir __VA_OPT__(, ) __VA_ARGS__, NULL)
#define mel_configure(t, dir, ...)        mel_configure_(t, dir __VA_OPT__(, ) __VA_ARGS__, NULL)

MEL_API int mel_build_main(int argc, char** argv);

#endif
