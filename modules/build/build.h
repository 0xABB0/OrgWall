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

typedef enum { MEL_PRIVATE, MEL_PUBLIC } Mel_Visibility;

typedef enum {
    MEL_PLATFORM_MACOS,
    MEL_PLATFORM_IOS,
    MEL_PLATFORM_LINUX,
    MEL_PLATFORM_ANDROID,
    MEL_PLATFORM_WIN32,
    MEL_PLATFORM_WASM,
    MEL_PLATFORM_COUNT
} Mel_Platform;

typedef struct {
    Mel_Platform platform;
    const char  *config;
    const char  *backend;
    const char  *gpu;
    const char  *runtime;
    bool         host;
} Mel_Variant;

typedef struct {
    uint32_t    platforms;
    const char *config;
    const char *backend;
    const char *gpu;
    const char *runtime;
} Mel_When;

#define MEL_ON(p) (1u << (MEL_PLATFORM_##p))
#define WHEN(...) ((Mel_When){__VA_ARGS__})
#define ALWAYS    ((Mel_When){0})

typedef struct Mel_Build_Target  Mel_Build_Target;
typedef struct Mel_Build_Context Mel_Build_Context;
typedef struct Mel_Kind_Builder  Mel_Kind_Builder;

typedef uint32_t Mel_Kind;
typedef uint32_t Mel_Stage;

typedef bool (*Mel_Build_Stage_Fn)(Mel_Build_Context *ctx);

typedef struct {
    const char *name;
    void (*register_defaults)(Mel_Kind_Builder *b);
} Mel_Kind_Desc;

typedef struct {
    const char *name;
    const char *after;
} Mel_Stage_Desc;

MEL_API Mel_Kind  mel_register_kind(const Mel_Kind_Desc *desc);
MEL_API Mel_Stage mel_register_stage(const Mel_Stage_Desc *desc);
MEL_API void      mel_kind_default(Mel_Kind_Builder *b, Mel_Stage stage, Mel_Build_Stage_Fn fn);

MEL_API Mel_Kind mel_kind_library(void);
MEL_API Mel_Kind mel_kind_application(void);
MEL_API Mel_Kind mel_kind_module(void);
MEL_API Mel_Kind mel_kind_third_party(void);
MEL_API Mel_Kind mel_kind_host_tool(void);

#define MEL_LIBRARY     mel_kind_library()
#define MEL_APP         mel_kind_application()
#define MEL_MODULE      mel_kind_module()
#define MEL_THIRD_PARTY mel_kind_third_party()
#define MEL_HOST_TOOL   mel_kind_host_tool()

MEL_API Mel_Stage mel_stage_configure(void);
MEL_API Mel_Stage mel_stage_compile(void);
MEL_API Mel_Stage mel_stage_link(void);
MEL_API Mel_Stage mel_stage_package(void);
MEL_API Mel_Stage mel_substage_fetch_sources(void);
MEL_API Mel_Stage mel_substage_compile_source(void);

#define MEL_STAGE_CONFIGURE mel_stage_configure()
#define MEL_STAGE_COMPILE   mel_stage_compile()
#define MEL_STAGE_LINK      mel_stage_link()
#define MEL_STAGE_PACKAGE   mel_stage_package()

MEL_API void mel_name(Mel_Build_Target *t, const char *name);
MEL_API void mel_kind(Mel_Build_Target *t, Mel_Kind kind);
MEL_API void mel_depends(Mel_Build_Target *t, const char *name);
MEL_API void mel_depends_host(Mel_Build_Target *t, const char *name);
MEL_API void mel_unavailable(Mel_Build_Target *t, Mel_When when);
MEL_API void mel_manifest(Mel_Build_Target *t, const char *key, const char *value);

MEL_API void mel_sources_(Mel_Build_Target *t, Mel_When when, ...);
MEL_API void mel_exclude_source_(Mel_Build_Target *t, Mel_When when, ...);
MEL_API void mel_cflags_(Mel_Build_Target *t, Mel_Visibility vis, Mel_When when, ...);
MEL_API void mel_defines_(Mel_Build_Target *t, Mel_Visibility vis, Mel_When when, ...);
MEL_API void mel_includes_(Mel_Build_Target *t, Mel_Visibility vis, Mel_When when, ...);
MEL_API void mel_link_(Mel_Build_Target *t, Mel_Visibility vis, Mel_When when, ...);

#define mel_sources(t, when, ...)        mel_sources_(t, when, __VA_ARGS__, NULL)
#define mel_exclude_source(t, when, ...) mel_exclude_source_(t, when, __VA_ARGS__, NULL)
#define mel_cflags(t, vis, when, ...)    mel_cflags_(t, vis, when, __VA_ARGS__, NULL)
#define mel_defines(t, vis, when, ...)   mel_defines_(t, vis, when, __VA_ARGS__, NULL)
#define mel_includes(t, vis, when, ...)  mel_includes_(t, vis, when, __VA_ARGS__, NULL)
#define mel_link(t, vis, when, ...)      mel_link_(t, vis, when, __VA_ARGS__, NULL)

MEL_API void mel_enum_to_string(Mel_Build_Target *t, const char *header);

MEL_API void mel_on(Mel_Build_Target *t, Mel_Stage stage, Mel_Build_Stage_Fn fn);
MEL_API void mel_suppress_default(Mel_Build_Target *t, Mel_Stage stage);

MEL_API Mel_Variant  mel_ctx_variant(Mel_Build_Context *ctx);
MEL_API Mel_Platform mel_ctx_platform(Mel_Build_Context *ctx);
MEL_API const char  *mel_ctx_config(Mel_Build_Context *ctx);
MEL_API const char  *mel_ctx_out_dir(Mel_Build_Context *ctx);
MEL_API const char  *mel_ctx_target_dir(Mel_Build_Context *ctx);
MEL_API const char  *mel_ctx_host_tool(Mel_Build_Context *ctx, const char *name);
MEL_API void         mel_ctx_add_source(Mel_Build_Context *ctx, const char *path);

MEL_API bool mel_tp_cmake(Mel_Build_Context *ctx, const char *dir, const char *const *args, size_t nargs,
                          const char *lib);

MEL_API int mel_build_main(int argc, char **argv);

#endif
