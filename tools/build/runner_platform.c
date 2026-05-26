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

// The web "platform" carries a toolchain sub-axis: emscripten (DOM/JS) and
// wasi-sdk (standards-only, no DOM) are genuinely different runtimes, so they
// resolve different source-chains. Resolved from the root target in
// mel_build_main; read by mel_platform_chain and the compiler/flag helpers.
typedef enum { WEB_EMSCRIPTEN, WEB_WASI } Web_Toolchain;

static Web_Toolchain g_web_tc = WEB_EMSCRIPTEN;
static bool          g_web_threading;
static bool          g_web_asyncify;

// Source-subdirectory resolution chains: a more specific platform shadows a
// more general one when two files share a basename within the same module.
static const char *const k_macos_chain[]   = { "macos", "apple", "posix", NULL };
static const char *const k_ios_chain[]     = { "ios",   "apple", "posix", NULL };
static const char *const k_linux_chain[]   = { "linux", "posix", NULL };
static const char *const k_android_chain[] = { "android", "posix", NULL };
static const char *const k_win32_chain[]   = { "win32", "win", NULL };
static const char *const k_emscripten_chain[] = { "emscripten", "web", "posix", NULL };
static const char *const k_wasi_chain[]       = { "wasi", "web", "posix", NULL };

static const char *const *mel_platform_chain(Mel_Platform p) {
    switch (p) {
        case MEL_PLATFORM_MACOS:   return k_macos_chain;
        case MEL_PLATFORM_IOS:     return k_ios_chain;
        case MEL_PLATFORM_LINUX:   return k_linux_chain;
        case MEL_PLATFORM_ANDROID: return k_android_chain;
        case MEL_PLATFORM_WIN32:   return k_win32_chain;
        case MEL_PLATFORM_WEB:     return g_web_tc == WEB_WASI ? k_wasi_chain : k_emscripten_chain;
        default:                   return NULL;
    }
}

static bool is_platform_subdir(const char *name) {
    static const char *const all[] = {
        "macos", "ios", "linux", "android", "win32", "windows", "web", "emscripten", "wasi",
        "apple", "posix", "win", "asm",
    };
    for (size_t i = 0; i < NOB_ARRAY_LEN(all); i++) {
        if (strcmp(name, all[i]) == 0) return true;
    }
    return false;
}

// =============================================================================
// Web toolchains
// =============================================================================
//
// Two toolchains target wasm: emscripten (emcc/emar, DOM + JS interop) and
// wasi-sdk (its bundled clang/llvm-ar, standards-only, no DOM). The choice plus
// the threading/Asyncify knobs are resolved once from the root target at the
// start of a build (mel_build_main) and read here, so every target in the graph
// — melody included — compiles against one agreed-upon toolchain. The toolchain
// enum + globals live up in the Platforms section because the source-chain
// selection (mel_platform_chain) needs them.

static const char *wasi_sdk_dir(void) {
    const char *e = getenv("WASI_SDK_PATH");
    if (e && e[0]) return e;
    const char *home = getenv("HOME");
    return temp_sprintf("%s/wasi-sdk", home ? home : ".");
}

static const char *web_cc(void) {
    if (g_web_tc == WEB_WASI) return temp_sprintf("%s/bin/clang", wasi_sdk_dir());
    return "emcc";
}

static const char *web_ar(void) {
    if (g_web_tc == WEB_WASI) return temp_sprintf("%s/bin/llvm-ar", wasi_sdk_dir());
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
    if (g_web_tc == WEB_WASI) {
        cmd_append(cmd, "--target=wasm32-wasip1");
        cmd_append(cmd, temp_sprintf("--sysroot=%s/share/wasi-sysroot", wasi_sdk_dir()));
    }
}
