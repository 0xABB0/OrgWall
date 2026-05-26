#include "runner_internal.h"

// =============================================================================
// Compile flags
// =============================================================================

static const char *const k_base_cflags[] = {
    "-std=c23", "-Wall", "-Wextra",
    "-Wno-unused-parameter", "-Wno-unused-function", "-Wno-missing-field-initializers",
};

static void append_resolved_flags(Cmd *cmd, Mel_Build_Context *ctx, const char *src) {
    for (size_t i = 0; i < NOB_ARRAY_LEN(k_base_cflags); i++) cmd_append(cmd, k_base_cflags[i]);
    if (ctx->config == MEL_CONFIG_DEBUG) cmd_append(cmd, "-g", "-O0");
    else                                 cmd_append(cmd, "-O2");
    // wasm is not position-independent in the ELF sense; -fPIC means SIDE_MODULE
    // to emscripten and is rejected by wasi clang, so it is dropped on web.
    if (ctx->platform != MEL_PLATFORM_WIN32 && ctx->platform != MEL_PLATFORM_WEB)
        cmd_append(cmd, "-fPIC");
    if (ctx->platform == MEL_PLATFORM_IOS) mel_ios_clang_flags(cmd);
    if (ctx->platform == MEL_PLATFORM_WEB) {
        web_target_flags(cmd);
        // musl (emscripten/wasi libc) gates POSIX/GNU symbols behind feature
        // macros that strict -std=c23 leaves undefined; the desktop Apple/glibc
        // headers expose them anyway, so this only bites on web.
        cmd_append(cmd, "-D_GNU_SOURCE");
        if (!web_is_wasi() && g_web_threading) cmd_append(cmd, "-pthread");
    }
    if (source_is_objc(src)) cmd_append(cmd, "-fobjc-arc");

    Resolved *r = &ctx->resolved;
    for (size_t i = 0; i < r->cflags.count; i++)  cmd_append(cmd, r->cflags.items[i]);
    for (size_t i = 0; i < r->defines.count; i++) cmd_append(cmd, temp_sprintf("-D%s", r->defines.items[i]));
    for (size_t i = 0; i < r->includes.count; i++) {
        const char *inc = r->includes.items[i];
        if (strstr(inc, "third-party") != NULL) cmd_append(cmd, "-isystem", inc);
        else                                    cmd_append(cmd, temp_sprintf("-I%s", inc));
    }
}
