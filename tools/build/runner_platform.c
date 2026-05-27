#include "runner_internal.h"

#ifdef _WIN32
// CreateHardLinkA returns BOOL (nonzero on success); link() returns 0 on success.
static int link(const char *src, const char *dst) {
    return CreateHardLinkA(dst, src, NULL) ? 0 : -1;
}
// utimes(path, NULL) bumps mtime/atime to now. On Win32 we open the file and
// stamp it with the current time via SetFileTime.
static int utimes(const char *path, void *unused) {
    (void)unused;
    HANDLE h = CreateFileA(path, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return -1;
    SYSTEMTIME st_now; GetSystemTime(&st_now);
    FILETIME ft; SystemTimeToFileTime(&st_now, &ft);
    BOOL ok = SetFileTime(h, NULL, &ft, &ft);
    CloseHandle(h);
    return ok ? 0 : -1;
}

static void *mel_dlopen(const char *path) { return (void *)LoadLibraryA(path); }
static void *mel_dlsym(void *h, const char *name) { return (void *)GetProcAddress((HMODULE)h, name); }
static void  mel_dlclose(void *h) { if (h) FreeLibrary((HMODULE)h); }
static const char *mel_dlerror(void) {
    static char buf[1024];
    DWORD err = GetLastError();
    DWORD n = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                             NULL, err, 0, buf, sizeof buf, NULL);
    if (n == 0) snprintf(buf, sizeof buf, "win32 error %lu", (unsigned long)err);
    return buf;
}
#else
#include <dlfcn.h>
static void *mel_dlopen(const char *path) { return dlopen(path, RTLD_NOW | RTLD_LOCAL); }
static void *mel_dlsym(void *h, const char *name) { return dlsym(h, name); }
static void  mel_dlclose(void *h) { if (h) dlclose(h); }
static const char *mel_dlerror(void) { const char *e = dlerror(); return e ? e : "unknown"; }
#endif

// =============================================================================
// Platforms
// =============================================================================

static bool mel_platform_from_name(const char *name, Mel_Platform *out) {
    for (int i = 0; i < MEL_PLATFORM_COUNT; i++) {
        if (strcmp(name, mel_platform_name((Mel_Platform)i)) == 0) { *out = (Mel_Platform)i; return true; }
    }
    return false;
}

static Mel_Platform mel_host_platform(void) {
#if defined(__APPLE__)
    return MEL_PLATFORM_MACOS;
#elif defined(__linux__)
    return MEL_PLATFORM_LINUX;
#elif defined(_WIN32)
    return MEL_PLATFORM_WIN32;
#else
    return MEL_PLATFORM_COUNT;
#endif
}

static bool g_web_threading;
static bool g_web_asyncify;

// Platform source-subdirectory chains: a more specific platform dir shadows a
// more general one when two files share a basename within the same module. The
// backend and runtime axes are resolved separately (see resolve_source_root),
// so emscripten/wasi no longer live in the web chain.
static const char *const k_macos_chain[]   = { "macos", "apple", "posix", NULL };
static const char *const k_ios_chain[]     = { "ios",   "apple", "posix", NULL };
static const char *const k_linux_chain[]   = { "linux", "posix", NULL };
static const char *const k_android_chain[] = { "android", "posix", NULL };
static const char *const k_win32_chain[]   = { "win32", "win", NULL };
static const char *const k_web_chain[]     = { "web", "posix", NULL };

static const char *const *mel_platform_chain(Mel_Platform p) {
    switch (p) {
        case MEL_PLATFORM_MACOS:   return k_macos_chain;
        case MEL_PLATFORM_IOS:     return k_ios_chain;
        case MEL_PLATFORM_LINUX:   return k_linux_chain;
        case MEL_PLATFORM_ANDROID: return k_android_chain;
        case MEL_PLATFORM_WIN32:   return k_win32_chain;
        case MEL_PLATFORM_WEB:     return k_web_chain;
        default:                   return NULL;
    }
}

// A directory name owned by one of the three axes (platform/family, backend, or
// runtime). Such dirs are never collected as common sources; they are pulled in
// only when their axis value is the active one for the build.
static bool is_axis_dir(const char *name) {
    static const char *const all[] = {
        "macos", "ios", "linux", "android", "win32", "windows", "web", "emscripten", "wasi",
        "apple", "posix", "win", "asm",
        "cocoa", "uikit", "winui", "androidnative", "dom", "qt", "compose",
        "metal", "vulkan", "dx12", "webgpu",
    };
    for (size_t i = 0; i < NOB_ARRAY_LEN(all); i++) {
        if (strcmp(name, all[i]) == 0) return true;
    }
    return false;
}

static const char *const k_default_backend[MEL_PLATFORM_COUNT] = {
    [MEL_PLATFORM_MACOS]   = "cocoa",
    [MEL_PLATFORM_IOS]     = "uikit",
    [MEL_PLATFORM_LINUX]   = NULL,
    [MEL_PLATFORM_ANDROID] = "androidnative",
    [MEL_PLATFORM_WIN32]   = "winui",
    [MEL_PLATFORM_WEB]     = "dom",
};

static const char *const k_default_runtime[MEL_PLATFORM_COUNT] = {
    [MEL_PLATFORM_MACOS]   = "native",
    [MEL_PLATFORM_IOS]     = "native",
    [MEL_PLATFORM_LINUX]   = "native",
    [MEL_PLATFORM_ANDROID] = "native",
    [MEL_PLATFORM_WIN32]   = "native",
    [MEL_PLATFORM_WEB]     = "emscripten",
};

// The GPU backend axis is independent of the UI backend axis: it selects which
// src/<gpu_backend>/ subdir a module compiles, mapping each platform to its
// native API by default. Overridable per platform (e.g. vulkan on macOS via
// MoltenVK) through mel_build_use_gpu_backend_on.
static const char *const k_default_gpu_backend[MEL_PLATFORM_COUNT] = {
    [MEL_PLATFORM_MACOS]   = "metal",
    [MEL_PLATFORM_IOS]     = "metal",
    [MEL_PLATFORM_LINUX]   = "vulkan",
    [MEL_PLATFORM_ANDROID] = "vulkan",
    [MEL_PLATFORM_WIN32]   = "dx12",
    [MEL_PLATFORM_WEB]     = "webgpu",
};

static const char *g_backend;
static const char *g_gpu_backend;
static const char *g_runtime;

static const char *resolve_backend(const Mel_Build_Target *root, Mel_Platform p) {
    return root->backends[p] ? root->backends[p] : k_default_backend[p];
}
static const char *resolve_gpu_backend(const Mel_Build_Target *root, Mel_Platform p) {
    return root->gpu_backends[p] ? root->gpu_backends[p] : k_default_gpu_backend[p];
}
static const char *resolve_runtime(const Mel_Build_Target *root, Mel_Platform p) {
    return root->runtimes[p] ? root->runtimes[p] : k_default_runtime[p];
}

static const char *const k_macos_backends[]   = { "cocoa", NULL };
static const char *const k_ios_backends[]     = { "uikit", NULL };
static const char *const k_android_backends[] = { "androidnative", NULL };
static const char *const k_win32_backends[]   = { "winui", NULL };
static const char *const k_web_backends[]     = { "dom", NULL };
static const char *const k_no_backends[]      = { NULL };

static const char *const *valid_backends(Mel_Platform p) {
    switch (p) {
        case MEL_PLATFORM_MACOS:   return k_macos_backends;
        case MEL_PLATFORM_IOS:     return k_ios_backends;
        case MEL_PLATFORM_ANDROID: return k_android_backends;
        case MEL_PLATFORM_WIN32:   return k_win32_backends;
        case MEL_PLATFORM_WEB:     return k_web_backends;
        default:                   return k_no_backends;
    }
}

static const char *const k_macos_gpu[]   = { "metal", "vulkan", NULL };
static const char *const k_ios_gpu[]     = { "metal", NULL };
static const char *const k_linux_gpu[]   = { "vulkan", NULL };
static const char *const k_android_gpu[] = { "vulkan", NULL };
static const char *const k_win32_gpu[]   = { "dx12", "vulkan", NULL };
static const char *const k_web_gpu[]     = { "webgpu", NULL };

static const char *const *valid_gpu_backends(Mel_Platform p) {
    switch (p) {
        case MEL_PLATFORM_MACOS:   return k_macos_gpu;
        case MEL_PLATFORM_IOS:     return k_ios_gpu;
        case MEL_PLATFORM_LINUX:   return k_linux_gpu;
        case MEL_PLATFORM_ANDROID: return k_android_gpu;
        case MEL_PLATFORM_WIN32:   return k_win32_gpu;
        case MEL_PLATFORM_WEB:     return k_web_gpu;
        default:                   return k_no_backends;
    }
}

static const char *const k_web_runtimes[]    = { "emscripten", "wasi", NULL };
static const char *const k_native_runtimes[] = { "native", NULL };
static const char *const *valid_runtimes(Mel_Platform p) {
    return p == MEL_PLATFORM_WEB ? k_web_runtimes : k_native_runtimes;
}

static bool axis_in_list(const char *const *list, const char *v) {
    for (; *list; list++) if (strcmp(*list, v) == 0) return true;
    return false;
}

// =============================================================================
// Web toolchains
// =============================================================================
//
// Two runtimes target wasm: emscripten (emcc/emar, DOM + JS interop) and wasi
// (the wasi-sdk clang/llvm-ar, standards-only, no DOM). The runtime is resolved
// once from the root target into g_runtime, so every target in the graph builds
// against one agreed-upon toolchain.

static bool web_is_wasi(void) { return g_runtime && strcmp(g_runtime, "wasi") == 0; }

static const char *wasi_sdk_dir(void) {
    const char *e = getenv("WASI_SDK_PATH");
    if (e && e[0]) return e;
    const char *home = getenv("HOME");
    return temp_sprintf("%s/wasi-sdk", home ? home : ".");
}

static const char *web_cc(void) {
    if (web_is_wasi()) return temp_sprintf("%s/bin/clang", wasi_sdk_dir());
    return "emcc";
}

static const char *web_ar(void) {
    if (web_is_wasi()) return temp_sprintf("%s/bin/llvm-ar", wasi_sdk_dir());
    return "emar";
}

// Per-context compiler / archiver: the web toolchain on web, clang otherwise.
static const char *cc_for(const Mel_Build_Context *ctx) {
    return ctx->platform == MEL_PLATFORM_WEB ? web_cc() : "clang";
}
static const char *ar_for(const Mel_Build_Context *ctx) {
    return ctx->platform == MEL_PLATFORM_WEB ? web_ar() : static_ar_tool(ctx->platform);
}

// Compile/link flags pinning the wasi target triple + sysroot. Emscripten needs
// none (emcc drives target selection itself).
static void web_target_flags(Cmd *cmd) {
    if (web_is_wasi()) {
        cmd_append(cmd, "--target=wasm32-wasip1");
        cmd_append(cmd, temp_sprintf("--sysroot=%s/share/wasi-sysroot", wasi_sdk_dir()));
    }
}
