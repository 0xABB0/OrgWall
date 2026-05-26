// Melody build runner (engine).
//
// The build graph driver: source discovery, the content-addressed cache, the
// default configure/compile/link/package stages, the Android NDK pipeline, and
// target discovery + module loading. Part of the nob driver translation unit
// (nob.c includes build.c, then this file); it reuses the static helpers and
// the public API defined in build.c. Per-target build.c modules statically link
// libmelbuild.a (built from build.c) and are dlopen'd here purely for project().

#include "internal.h"
#include "sha256.h"

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

// =============================================================================
// Source classification
// =============================================================================

static bool ends_with(const char *s, const char *suffix) {
    size_t ls = strlen(s), lsuf = strlen(suffix);
    if (lsuf > ls) return false;
    return memcmp(s + ls - lsuf, suffix, lsuf) == 0;
}

static bool source_is_objc(const char *name) { return ends_with(name, ".m"); }

static bool source_is_bridge(const char *name) {
    return ends_with(name, ".bridge.c") || ends_with(name, ".bridge.m");
}

static bool source_is_buildable(const char *name, Mel_Platform p) {
    bool is_c = ends_with(name, ".c");
    bool is_m = ends_with(name, ".m");
    if (!is_c && !is_m) return false;
    if (ends_with(name, ".build.c")) return false;
    bool win = (p == MEL_PLATFORM_WIN32);
    if (!win && ends_with(name, ".win32.c")) return false;
    if (win && (ends_with(name, ".posix.c") || ends_with(name, ".unix.c"))) return false;
    if (win && is_m) return false;
    return true;
}

static void prop_resolve(File_Paths *dst, const Prop_List *src, Mel_Platform p) {
    uint32_t bit = 1u << p;
    for (size_t i = 0; i < src->count; i++) {
        if (src->items[i].mask == 0 || (src->items[i].mask & bit)) {
            da_append(dst, src->items[i].value);
        }
    }
}

// =============================================================================
// Target registry
// =============================================================================

typedef struct {
    Mel_Build_Target *items;
    size_t count, capacity;
} Target_Registry;

static Target_Registry g_targets;

static Mel_Build_Target *registry_find(const char *name) {
    for (size_t i = 0; i < g_targets.count; i++) {
        if (strcmp(g_targets.items[i].name, name) == 0) return &g_targets.items[i];
    }
    return NULL;
}

static bool module_excluded(const Mel_Build_Target *t, Mel_Platform p, const char *name) {
    uint32_t bit = 1u << p;
    for (size_t i = 0; i < t->excluded_modules.count; i++) {
        const Prop *e = &t->excluded_modules.items[i];
        if ((e->mask & bit) && strcmp(e->value, name) == 0) return true;
    }
    return false;
}

static bool source_excluded(const Mel_Build_Target *t, Mel_Platform p, const char *path) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    uint32_t bit = 1u << p;
    for (size_t i = 0; i < t->excluded_sources.count; i++) {
        const Prop *e = &t->excluded_sources.items[i];
        if ((e->mask & bit) && strcmp(e->value, base) == 0) return true;
    }
    return false;
}

static void filter_excluded_sources(const Mel_Build_Target *t, Mel_Platform p, File_Paths *paths) {
    size_t w = 0;
    for (size_t r = 0; r < paths->count; r++) {
        if (source_excluded(t, p, paths->items[r])) continue;
        paths->items[w++] = paths->items[r];
    }
    paths->count = w;
}

static const char *target_config_get(const Mel_Build_Target *t, const char *key) {
    for (size_t i = 0; i < t->cfg_keys.count; i++) {
        if (strcmp(t->cfg_keys.items[i], key) == 0) return t->cfg_vals.items[i];
    }
    return NULL;
}

// =============================================================================
// Source discovery
// =============================================================================

static bool name_seen(const File_Paths *seen, const char *n) {
    for (size_t i = 0; i < seen->count; i++) if (strcmp(seen->items[i], n) == 0) return true;
    return false;
}

// Recurse a directory collecting buildable sources. When skip_platform_dirs is
// set, directories named like a platform are not descended (they are handled
// separately via the platform chain). seen, when non-NULL, enforces basename
// shadowing across a chain.
static bool collect_dir(const char *dir, Mel_Platform p, bool skip_platform_dirs,
                        File_Paths *seen, File_Paths *out_sources, File_Paths *out_bridges) {
    if (!file_exists(dir) || get_file_type(dir) != NOB_FILE_DIRECTORY) return true;
    File_Paths files = {0};
    if (!read_entire_dir(dir, &files)) return false;
    for (size_t i = 0; i < files.count; i++) {
        const char *n = files.items[i];
        if (strcmp(n, ".") == 0 || strcmp(n, "..") == 0) continue;
        const char *full = temp_sprintf("%s/%s", dir, n);
        Nob_File_Type ft = get_file_type(full);
        if (ft == NOB_FILE_DIRECTORY) {
            if (skip_platform_dirs && is_platform_subdir(n)) continue;
            if (!collect_dir(full, p, skip_platform_dirs, seen, out_sources, out_bridges)) return false;
        } else if (ft == NOB_FILE_REGULAR) {
            if (!source_is_buildable(n, p)) continue;
            if (seen) {
                if (name_seen(seen, n)) continue;
                da_append(seen, temp_strdup(n));
            }
            if (source_is_bridge(n)) da_append(out_bridges, temp_strdup(full));
            else                     da_append(out_sources, temp_strdup(full));
        }
    }
    return true;
}

// Resolve one source root: common sources plus the platform chain with shadowing.
static bool resolve_source_root(const char *root, Mel_Platform p,
                                File_Paths *out_sources, File_Paths *out_bridges) {
    if (!collect_dir(root, p, true, NULL, out_sources, out_bridges)) return false;
    const char *const *chain = mel_platform_chain(p);
    File_Paths seen = {0};
    for (size_t c = 0; chain && chain[c]; c++) {
        const char *sub = temp_sprintf("%s/%s", root, chain[c]);
        if (!collect_dir(sub, p, false, &seen, out_sources, out_bridges)) return false;
    }
    return true;
}

// Module include dirs are platform-independent, so they are resolved into the
// target's public includes at load time (before property propagation snapshots
// them), separately from per-platform source resolution. Per-platform module
// exclusions are encoded by recording the include with the inverted platform
// mask (so the property only resolves on platforms where the module applies).
static bool resolve_module_includes(Mel_Build_Target *t) {
    for (size_t r = 0; r < t->module_roots.count; r++) {
        const char *modules_dir = t->module_roots.items[r];
        File_Paths entries = {0};
        if (!read_entire_dir(modules_dir, &entries)) return false;
        for (size_t i = 0; i < entries.count; i++) {
            const char *name = entries.items[i];
            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
            const char *mod = temp_sprintf("%s/%s", modules_dir, name);
            if (get_file_type(mod) != NOB_FILE_DIRECTORY) continue;
            const char *inc = temp_sprintf("%s/include", mod);
            if (get_file_type(inc) != NOB_FILE_DIRECTORY) continue;

            uint32_t excl_mask = 0;
            for (size_t k = 0; k < t->excluded_modules.count; k++) {
                const Prop *e = &t->excluded_modules.items[k];
                if (strcmp(e->value, name) == 0) excl_mask |= e->mask;
            }
            // mask == 0 means "all platforms"; otherwise restrict to the
            // complement of the exclusion mask (within the known platform set).
            uint32_t all = (excl_mask == 0) ? 0 : (((1u << MEL_PLATFORM_COUNT) - 1) & ~excl_mask);

            bool dup = false;
            for (size_t k = 0; k < t->pub.includes.count; k++) {
                if (strcmp(t->pub.includes.items[k].value, inc) == 0) { dup = true; break; }
            }
            if (!dup) prop_add(&t->pub.includes, inc, all);
        }
    }
    return true;
}

// Expand a module-root's sources: each immediate subdir is a module whose <m>/src
// is a platform-aware source root. Modules excluded on this platform are skipped.
static bool resolve_module_root(Mel_Build_Target *t, const char *modules_dir, Mel_Platform p,
                                File_Paths *out_sources, File_Paths *out_bridges) {
    File_Paths entries = {0};
    if (!read_entire_dir(modules_dir, &entries)) return false;
    for (size_t i = 0; i < entries.count; i++) {
        const char *name = entries.items[i];
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        const char *mod = temp_sprintf("%s/%s", modules_dir, name);
        if (get_file_type(mod) != NOB_FILE_DIRECTORY) continue;
        if (module_excluded(t, p, name)) continue;
        if (!resolve_source_root(temp_sprintf("%s/src", mod), p, out_sources, out_bridges)) return false;
    }
    return true;
}

static bool target_resolve_sources(Mel_Build_Target *t, Mel_Platform p,
                                    File_Paths *out_sources, File_Paths *out_bridges) {
    for (size_t i = 0; i < t->module_roots.count; i++) {
        if (!resolve_module_root(t, t->module_roots.items[i], p, out_sources, out_bridges)) return false;
    }
    for (size_t i = 0; i < t->source_roots.count; i++) {
        if (!resolve_source_root(t->source_roots.items[i], p, out_sources, out_bridges)) return false;
    }
    filter_excluded_sources(t, p, out_sources);
    filter_excluded_sources(t, p, out_bridges);
    return true;
}

// =============================================================================
// Path resolution
// =============================================================================

static const char *config_name(Mel_Config c) { return c == MEL_CONFIG_RELEASE ? "release" : "debug"; }

static const char *target_out_dir(const Mel_Build_Target *t, Mel_Platform p, Mel_Config c) {
    return temp_sprintf("%s/%s/%s", MEL_BUILD_DIR, mel_platform_name(p), config_name(c));
}

static const char *target_obj_dir(const Mel_Build_Target *t, Mel_Platform p, Mel_Config c) {
    return temp_sprintf("%s/obj/%s/%s/%s", MEL_BUILD_DIR, mel_platform_name(p), config_name(c), t->name);
}

static const char *thirdparty_prefix(const Mel_Build_Target *t, Mel_Platform p) {
    return tp_prefix_named(p, NULL, t->name);
}

static const char *library_artifact(const Mel_Build_Target *t, Mel_Platform p, Mel_Config c) {
    if (p == MEL_PLATFORM_WIN32) return temp_sprintf("%s/%s.lib", target_out_dir(t, p, c), t->name);
    return temp_sprintf("%s/lib%s.a", target_out_dir(t, p, c), t->name);
}

static const char *app_artifact(const Mel_Build_Target *t, Mel_Platform p, Mel_Config c) {
    return temp_sprintf("%s/%s/%s", target_out_dir(t, p, c), t->name, t->name);
}

// Web bundles live target-first (build/<target>/web/<config>/) so the wasm, its
// loader JS, and the HTML shell sit together ready to host.
static const char *web_out_dir(const Mel_Build_Target *t, Mel_Config c) {
    return temp_sprintf("%s/%s/web/%s", MEL_BUILD_DIR, t->name, config_name(c));
}

static const char *object_for(const Mel_Build_Target *t, Mel_Platform p, Mel_Config c, const char *src) {
    String_Builder sb = {0};
    sb_appendf(&sb, "%s/", target_obj_dir(t, p, c));
    for (const char *q = src; *q; q++) sb_append_buf(&sb, (*q == '/') ? "." : q, 1);
    sb_append_cstr(&sb, ".o");
    sb_append_null(&sb);
    return temp_strdup(sb.items);
}

// Config-independent depfile path for a source: the recorded header list is the
// same for debug and release, so both configs share one depfile and either can
// derive the other's content key from it.
static const char *depfile_for_src(const Mel_Build_Target *t, Mel_Platform p, const char *src) {
    String_Builder sb = {0};
    sb_appendf(&sb, "%s/obj/%s/%s/deps/", MEL_BUILD_DIR, mel_platform_name(p), t->name);
    for (const char *q = src; *q; q++) sb_append_buf(&sb, (*q == '/') ? "." : q, 1);
    sb_append_cstr(&sb, ".d");
    sb_append_null(&sb);
    return temp_strdup(sb.items);
}

// =============================================================================
// Property propagation
// =============================================================================

static void props_resolve_into(Resolved *dst, const Props *src, Mel_Platform p) {
    prop_resolve(&dst->cflags, &src->cflags, p);
    prop_resolve(&dst->includes, &src->includes, p);
    prop_resolve(&dst->defines, &src->defines, p);
    prop_resolve(&dst->link_flags, &src->link_flags, p);
}

static bool target_supports(const Mel_Build_Target *t, Mel_Platform p);

static void collect_deps_recursive(Mel_Build_Target *t, Mel_Platform p, Mel_Config c,
                                    Resolved *out, File_Paths *visited, File_Paths *bridges) {
    for (size_t i = 0; i < t->deps.count; i++) {
        const char *dep_name = t->deps.items[i];
        if (name_seen(visited, dep_name)) continue;
        da_append(visited, dep_name);

        Mel_Build_Target *d = registry_find(dep_name);
        if (!d) {
            nob_log(NOB_ERROR, "target '%s' depends on unknown target '%s'", t->name, dep_name);
            continue;
        }

        // Silently drop deps that don't support this platform — the consumer
        // is expected to have either gated its own use of the dep's API or
        // marked itself as not supporting this platform either.
        if (!target_supports(d, p)) continue;

        // The dependency's public interface (platform-filtered).
        props_resolve_into(out, &d->pub, p);

        // Auto-derived linkage to the dependency's artifact.
        if (d->kind == MEL_TARGET_LIBRARY) {
            da_append(&out->link_flags, temp_sprintf("-L%s", target_out_dir(d, p, c)));
            da_append(&out->link_flags, temp_sprintf("-l%s", d->name));
        } else if (d->kind == MEL_TARGET_THIRD_PARTY) {
            const char *prefix = thirdparty_prefix(d, p);
            da_append(&out->includes, temp_sprintf("%s/include", prefix));
            da_append(&out->link_flags, temp_sprintf("-L%s/lib", prefix));
        }

        // Library deps contribute their bridge sources to the consumer.
        if (bridges && d->kind == MEL_TARGET_LIBRARY) {
            File_Paths s = {0}, b = {0};
            target_resolve_sources(d, p, &s, &b);
            for (size_t k = 0; k < b.count; k++) da_append(bridges, b.items[k]);
        }

        collect_deps_recursive(d, p, c, out, visited, bridges);
    }
}

static void context_resolve_props(Mel_Build_Context *ctx) {
    Mel_Build_Target *t = ctx->target;
    // Own properties (public + private) come first, platform-filtered.
    props_resolve_into(&ctx->resolved, &t->priv, ctx->platform);
    props_resolve_into(&ctx->resolved, &t->pub, ctx->platform);
    // Then transitive public properties of dependencies.
    File_Paths visited = {0};
    collect_deps_recursive(t, ctx->platform, ctx->config, &ctx->resolved, &visited, &ctx->bridge_sources);
}

// =============================================================================
// Compilation
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
        if (g_web_tc == WEB_EMSCRIPTEN && g_web_threading) cmd_append(cmd, "-pthread");
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

// Parse a make-style .d file, appending listed prerequisites to out. Handles
// the only two escapes clang emits on Windows: "\<newline>" (line continuation)
// and "\ " (literal space). A bare "\" preceding any other character is part
// of the path (Windows paths use backslash as the native separator) and is
// kept verbatim.
static bool parse_depfile(const char *path, File_Paths *out) {
    String_Builder sb = {0};
    if (!read_entire_file(path, &sb)) return false;
    sb_append_null(&sb);
    const char *s = sb.items;
    const char *colon = strchr(s, ':');
    if (colon) s = colon + 1;
    String_Builder tok = {0};
    for (const char *p = s; *p; p++) {
        char ch = *p;
        if (ch == '\\' && (p[1] == '\n' || p[1] == '\r')) { p++; continue; }
        if (ch == '\\' && p[1] == ' ') { sb_append_buf(&tok, " ", 1); p++; continue; }
        if (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t') {
            if (tok.count > 0) {
                sb_append_null(&tok);
                da_append(out, temp_strdup(tok.items));
                tok.count = 0;
            }
            continue;
        }
        sb_append_buf(&tok, &ch, 1);
    }
    if (tok.count > 0) { sb_append_null(&tok); da_append(out, temp_strdup(tok.items)); }
    free(sb.items);
    free(tok.items);
    return true;
}

static bool obj_needs_rebuild(const char *obj, const char *src, const char *dep) {
    if (!file_exists(obj)) return true;
    File_Paths prereqs = {0};
    da_append(&prereqs, src);
    parse_depfile(dep, &prereqs); // best-effort; missing -> just src
    int r = needs_rebuild(obj, prereqs.items, prereqs.count);
    return r != 0; // treat error as "rebuild"
}

static bool mkdirs_parent(const char *path) {
    const char *slash = strrchr(path, '/');
    if (!slash) return true;
    char dir[4096];
    size_t n = (size_t)(slash - path);
    if (n >= sizeof dir) return false;
    memcpy(dir, path, n);
    dir[n] = '\0';
    return mel_mkdirs(dir);
}

// =============================================================================
// Content-addressed cache
// =============================================================================
//
// An object's cache key is a SHA-256 over: the compiler identity, the exact
// flag vector, the source bytes, and the bytes of every header the compiler
// reported via -MD. Because config-affecting flags (-O0 vs -O2) sit inside the
// key, debug and release objects coexist under build/cache/objects and flipping
// config is a hardlink, not a recompile. Linked artifacts key the same way over
// their input object bytes and link flags.

#ifdef _WIN32
// MSVC's stat() leaves st_ino == 0 for every file, so the POSIX
// (st_dev, st_ino) trick reports every pair as identical. Fall back to
// BY_HANDLE_FILE_INFORMATION (FileIndex + VolumeSerialNumber).
static bool same_inode(const char *a, const char *b) {
    HANDLE ha = CreateFileA(a, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (ha == INVALID_HANDLE_VALUE) return false;
    HANDLE hb = CreateFileA(b, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (hb == INVALID_HANDLE_VALUE) { CloseHandle(ha); return false; }
    BY_HANDLE_FILE_INFORMATION ia, ib;
    bool ok = GetFileInformationByHandle(ha, &ia) && GetFileInformationByHandle(hb, &ib);
    CloseHandle(ha); CloseHandle(hb);
    if (!ok) return false;
    return ia.dwVolumeSerialNumber == ib.dwVolumeSerialNumber &&
           ia.nFileIndexHigh == ib.nFileIndexHigh &&
           ia.nFileIndexLow  == ib.nFileIndexLow;
}
#else
static bool same_inode(const char *a, const char *b) {
    struct stat sa, sb;
    if (stat(a, &sa) != 0 || stat(b, &sb) != 0) return false;
    return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}
#endif

// Populate dst from src sharing storage when possible; fall back to a copy
// across filesystem boundaries. Both ends end up as independent paths to the
// same content.
static bool hardlink_or_copy(const char *src, const char *dst) {
    if (!mkdirs_parent(dst)) return false;
    if (file_exists(dst)) {
        if (same_inode(src, dst)) return true;
        delete_file(dst);
    }
    if (link(src, dst) == 0) return true;
    return copy_file(src, dst);
}

// Bump a cache entry's mtime so age-based gc treats it as recently used.
static void cache_touch(const char *path) { utimes(path, NULL); }

// `<cc> --version` output, cached per distinct compiler invocation name. Mixed
// into every object key so a toolchain upgrade invalidates the cache.
static const char *cc_identity(const char *cc) {
    static struct { const char *cc; const char *ver; } cache[16];
    static int count;
    for (int i = 0; i < count; i++)
        if (strcmp(cache[i].cc, cc) == 0) return cache[i].ver;

    const char *ver = cc;
    FILE *p = popen(temp_sprintf("%s --version 2>/dev/null", cc), "r");
    if (p) {
        String_Builder sb = {0};
        char buf[512];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, p)) > 0) sb_append_buf(&sb, buf, n);
        pclose(p);
        sb_append_null(&sb);
        ver = temp_strdup(sb.items);
        free(sb.items);
    }
    if (count < (int)NOB_ARRAY_LEN(cache)) {
        cache[count].cc = temp_strdup(cc);
        cache[count].ver = ver;
        count++;
    }
    return ver;
}

static void sha256_str(Mel_Sha256 *c, const char *s) {
    mel_sha256_update(c, s, strlen(s) + 1); // include the NUL as a separator
}

static bool sha256_file(Mel_Sha256 *c, const char *path) {
    String_Builder sb = {0};
    if (!read_entire_file(path, &sb)) { free(sb.items); return false; }
    uint64_t len = sb.count;
    mel_sha256_update(c, &len, sizeof len); // length-prefix to avoid concatenation ambiguity
    mel_sha256_update(c, sb.items, sb.count);
    free(sb.items);
    return true;
}

// Compute the object content key. `flags` carries the compiler name followed by
// the flag vector (no -c/-o/obj/-MD/-MF). The depfile is config-independent (the
// header list does not vary with -O), so a never-built config can still derive
// its key from a sibling config's depfile and hit the cache. Returns false when
// the depfile is absent or any prerequisite cannot be read (treated as a miss).
static bool compute_obj_key(const Cmd *flags, const char *cc, const char *dep, char out_hex[MEL_SHA256_HEX_LEN]) {
    if (!file_exists(dep)) return false;
    File_Paths prereqs = {0};
    if (!parse_depfile(dep, &prereqs)) return false;
    if (prereqs.count == 0) return false;

    Mel_Sha256 c;
    mel_sha256_init(&c);
    sha256_str(&c, cc_identity(cc));
    for (size_t i = 0; i < flags->count; i++) sha256_str(&c, flags->items[i]);
    for (size_t i = 0; i < prereqs.count; i++) {
        sha256_str(&c, prereqs.items[i]);
        if (!sha256_file(&c, prereqs.items[i])) return false;
    }

    uint8_t digest[MEL_SHA256_DIGEST_LEN];
    mel_sha256_final(&c, digest);
    mel_sha256_hex(digest, out_hex);
    return true;
}

static const char *cache_obj_path(const char *hex) {
    return temp_sprintf("%s/objects/%s.o", MEL_CACHE_DIR, hex);
}

// Restore an object from cache. Returns true (obj now in place, no compile
// needed) on a hit. `flags` is [cc, flag...].
static bool cache_obj_restore(const Cmd *flags, const char *cc, const char *obj, const char *dep) {
    char hex[MEL_SHA256_HEX_LEN];
    if (!compute_obj_key(flags, cc, dep, hex)) return false;
    const char *cached = cache_obj_path(hex);
    if (!file_exists(cached)) return false;
    if (!hardlink_or_copy(cached, obj)) return false;
    cache_touch(cached);
    return true;
}

// Publish a freshly compiled (or already up-to-date) object into the cache.
static void cache_obj_store(const Cmd *flags, const char *cc, const char *obj, const char *dep) {
    if (!file_exists(obj)) return;
    char hex[MEL_SHA256_HEX_LEN];
    if (!compute_obj_key(flags, cc, dep, hex)) return;
    const char *cached = cache_obj_path(hex);
    if (!file_exists(cached)) hardlink_or_copy(obj, cached);
    cache_touch(cached);
}

// --- Linked/packaged artifact caching ---
//
// Key = identity args (linker + flags) plus the bytes of every input file.

static const char *cache_artifact_path(const char *hex) {
    return temp_sprintf("%s/artifacts/%s", MEL_CACHE_DIR, hex);
}

static bool compute_artifact_key(const Cmd *args, const File_Paths *inputs, char out_hex[MEL_SHA256_HEX_LEN]) {
    Mel_Sha256 c;
    mel_sha256_init(&c);
    for (size_t i = 0; i < args->count; i++) sha256_str(&c, args->items[i]);
    for (size_t i = 0; i < inputs->count; i++) {
        sha256_str(&c, inputs->items[i]);
        if (!sha256_file(&c, inputs->items[i])) return false;
    }
    uint8_t digest[MEL_SHA256_DIGEST_LEN];
    mel_sha256_final(&c, digest);
    mel_sha256_hex(digest, out_hex);
    return true;
}

static bool cache_artifact_restore(const Cmd *args, const File_Paths *inputs, const char *artifact) {
    char hex[MEL_SHA256_HEX_LEN];
    if (!compute_artifact_key(args, inputs, hex)) return false;
    const char *cached = cache_artifact_path(hex);
    if (!file_exists(cached)) return false;
    if (!hardlink_or_copy(cached, artifact)) return false;
    cache_touch(cached);
    return true;
}

static void cache_artifact_store(const Cmd *args, const File_Paths *inputs, const char *artifact) {
    if (!file_exists(artifact)) return;
    char hex[MEL_SHA256_HEX_LEN];
    if (!compute_artifact_key(args, inputs, hex)) return;
    const char *cached = cache_artifact_path(hex);
    if (!file_exists(cached)) hardlink_or_copy(artifact, cached);
    cache_touch(cached);
}


// --- compile_commands.json collection ---

static String_Builder g_ccmds;
static size_t g_ccmds_count;

static void json_escaped(String_Builder *sb, const char *s) {
    sb_append_cstr(sb, "\"");
    for (const char *p = s; *p; p++) {
        switch (*p) {
            case '"':  sb_append_cstr(sb, "\\\""); break;
            case '\\': sb_append_cstr(sb, "\\\\"); break;
            case '\n': sb_append_cstr(sb, "\\n");  break;
            case '\r': sb_append_cstr(sb, "\\r");  break;
            case '\t': sb_append_cstr(sb, "\\t");  break;
            default:   sb_append_buf(sb, p, 1);    break;
        }
    }
    sb_append_cstr(sb, "\"");
}

static void ccmds_add(const char *cwd, const char *src, Cmd cmd) {
    String_Builder line = {0};
    cmd_render(cmd, &line);
    sb_append_null(&line);
    if (g_ccmds_count++ > 0) sb_append_cstr(&g_ccmds, ",\n");
    sb_append_cstr(&g_ccmds, "  {\n    \"directory\": ");
    json_escaped(&g_ccmds, cwd);
    sb_append_cstr(&g_ccmds, ",\n    \"command\": ");
    json_escaped(&g_ccmds, line.items);
    sb_append_cstr(&g_ccmds, ",\n    \"file\": ");
    json_escaped(&g_ccmds, src);
    sb_append_cstr(&g_ccmds, "\n  }");
    free(line.items);
}

static bool emit_compile_commands(void) {
    if (g_ccmds_count == 0) return true;
    String_Builder sb = {0};
    sb_append_cstr(&sb, "[\n");
    sb_append_buf(&sb, g_ccmds.items, g_ccmds.count);
    sb_append_cstr(&sb, "\n]\n");
    bool ok = write_entire_file("compile_commands.json", sb.items, sb.count);
    free(sb.items);
    return ok;
}

// Compile one translation unit through the cache. `flags` is [cc, flag...] with
// no -c/-o/-MD/-MF. On a cache hit the object is hardlinked into place and
// *spawned stays false. On a miss the compile is launched asynchronously into
// *procs (with -MD so the next run has a depfile); the caller publishes it to
// the cache after the batch completes. Setting emit_ccmds records the clean
// command (no -MD/-MF) for compile_commands.json.
static bool compile_tu(const char *cc, Cmd flags, const char *src, const char *obj, const char *dep,
                       bool emit_ccmds, const char *cwd, Procs *procs, size_t parallelism,
                       bool *spawned) {
    *spawned = false;

    if (emit_ccmds) {
        Cmd render = {0};
        for (size_t i = 0; i < flags.count; i++) cmd_append(&render, flags.items[i]);
        cmd_append(&render, "-c", src, "-o", obj);
        ccmds_add(cwd, src, render);
        free(render.items);
    }

    if (cache_obj_restore(&flags, cc, obj, dep)) return true;

    // Miss: rebuild only when the object is actually stale; otherwise the
    // existing object is sound and just needs publishing to the cache.
    if (file_exists(obj) && !obj_needs_rebuild(obj, src, dep)) return true;

    if (!mkdirs_parent(dep)) return false;
    Cmd cmd = {0};
    for (size_t i = 0; i < flags.count; i++) cmd_append(&cmd, flags.items[i]);
    cmd_append(&cmd, "-c", src, "-o", obj, "-MD", "-MF", dep);
    Proc p = cmd_run_async_and_reset(&cmd);
    *spawned = true;
    return procs_append_with_flush(procs, p, parallelism);
}

static bool compile_sources(Mel_Build_Context *ctx, const char *cc) {
    if (!mel_mkdirs(target_obj_dir(ctx->target, ctx->platform, ctx->config))) return false;

    const char *cwd = get_current_dir_temp();
    Procs procs = {0};
    size_t parallelism = nob_nprocs();
    if (parallelism < 1) parallelism = 1;
    bool ok = true;

    for (size_t i = 0; i < ctx->sources.count; i++) {
        const char *src = ctx->sources.items[i];
        const char *obj = object_for(ctx->target, ctx->platform, ctx->config, src);
        const char *dep = depfile_for_src(ctx->target, ctx->platform, src);
        da_append(&ctx->objects, obj);

        Cmd flags = {0};
        cmd_append(&flags, cc);
        append_resolved_flags(&flags, ctx, src);

        bool spawned = false;
        if (!compile_tu(cc, flags, src, obj, dep, true, cwd, &procs, parallelism, &spawned)) ok = false;
        free(flags.items);
    }
    if (!procs_wait_and_reset(&procs)) ok = false;
    if (!ok) return false;

    // Publish every object to the cache from its now-current depfile. Cheap when
    // already present (a touch); the store of freshly compiled objects is what
    // makes a later config flip a hardlink instead of a recompile.
    for (size_t i = 0; i < ctx->sources.count; i++) {
        const char *src = ctx->sources.items[i];
        const char *obj = ctx->objects.items[i];
        const char *dep = depfile_for_src(ctx->target, ctx->platform, src);
        Cmd flags = {0};
        cmd_append(&flags, cc);
        append_resolved_flags(&flags, ctx, src);
        cache_obj_store(&flags, cc, obj, dep);
        free(flags.items);
    }
    return ok;
}

// =============================================================================
// Default stages
// =============================================================================

static const char *android_java_srcdirs_csv(const char *cwd) {
    String_Builder sb = {0};
    File_Paths mods = {0};
    if (read_entire_dir("modules", &mods)) {
        for (size_t i = 0; i < mods.count; i++) {
            const char *m = mods.items[i];
            if (strcmp(m, ".") == 0 || strcmp(m, "..") == 0) continue;
            const char *jd = temp_sprintf("modules/%s/src/android/java", m);
            if (file_exists(jd) && get_file_type(jd) == NOB_FILE_DIRECTORY) {
                if (sb.count) sb_append_cstr(&sb, ",");
                sb_appendf(&sb, "%s/%s", cwd, jd);
            }
        }
    }
    sb_append_null(&sb);
    return temp_strdup(sb.items);
}

static const char *configure_out_dir(const Mel_Build_Target *t, Mel_Platform p) {
    return temp_sprintf("%s/%s/%s", MEL_BUILD_DIR, t->name, mel_platform_name(p));
}

static const char *android_sdk_dir(const char *app_name);

static void android_config_defaults(Mel_Build_Target *t) {
    if (!target_config_get(t, "COMPILE_SDK"))      target_config_set(t, "COMPILE_SDK", "36");
    if (!target_config_get(t, "MIN_SDK"))          target_config_set(t, "MIN_SDK", "23");
    if (!target_config_get(t, "TARGET_SDK"))       target_config_set(t, "TARGET_SDK", "36");
    if (!target_config_get(t, "VERSION_CODE"))     target_config_set(t, "VERSION_CODE", "1");
    if (!target_config_get(t, "VERSION_NAME"))     target_config_set(t, "VERSION_NAME", "0.1.0");
    if (!target_config_get(t, "ROOTPROJECT_NAME")) target_config_set(t, "ROOTPROJECT_NAME", t->name);
    if (!target_config_get(t, "NAMESPACE"))        target_config_set(t, "NAMESPACE", temp_sprintf("orgwall.%s", t->name));
    if (!target_config_get(t, "APPLICATION_ID"))   target_config_set(t, "APPLICATION_ID", target_config_get(t, "NAMESPACE"));
    if (!target_config_get(t, "APP_LABEL"))        target_config_set(t, "APP_LABEL", target_config_get(t, "ROOTPROJECT_NAME"));
}

static bool configure_android(Mel_Build_Context *ctx) {
    Mel_Build_Target *t = ctx->target;
    const char *out = temp_sprintf("%s/%s/android", MEL_BUILD_DIR, t->name);

    android_config_defaults(t);

    if (!mel_mkdirs(out)) return false;
    if (!copy_directory_recursively("tools/build/android", out)) return false;

    const char *sdk = android_sdk_dir(t->name);
    if (sdk) {
        const char *lp = temp_sprintf("sdk.dir=%s\n", sdk);
        if (!write_entire_file(temp_sprintf("%s/local.properties", out), lp, strlen(lp))) return false;
    } else {
        nob_log(NOB_WARNING, "no Android SDK resolved; gradle will need ANDROID_HOME");
    }

    nob_log(NOB_INFO, "configured Android project at %s", out);
    return true;
}

static bool plist_set(const char *plist, const char *key, const char *value) {
    Cmd c = {0};
    cmd_append(&c, "/usr/libexec/PlistBuddy", "-c", temp_sprintf("Set :%s %s", key, value), plist);
    return cmd_run_sync_and_reset(&c);
}

static bool configure_apple(Mel_Build_Context *ctx) {
    Mel_Build_Target *t = ctx->target;
    bool ios = ctx->platform == MEL_PLATFORM_IOS;

    const char *label = target_config_get(t, "APP_LABEL");
    if (!target_config_get(t, "BUNDLE_NAME"))         target_config_set(t, "BUNDLE_NAME", t->name);
    if (!target_config_get(t, "BUNDLE_DISPLAY_NAME")) target_config_set(t, "BUNDLE_DISPLAY_NAME", label ? label : t->name);
    if (!target_config_get(t, "BUNDLE_ID"))           target_config_set(t, "BUNDLE_ID", temp_sprintf("orgwall.%s", t->name));
    if (!target_config_get(t, "VERSION_CODE"))        target_config_set(t, "VERSION_CODE", "1");
    if (!target_config_get(t, "VERSION_NAME"))        target_config_set(t, "VERSION_NAME", "0.1.0");
    if (!target_config_get(t, "MIN_MACOS"))           target_config_set(t, "MIN_MACOS", "11.0");
    if (!target_config_get(t, "MIN_IOS"))             target_config_set(t, "MIN_IOS", "13.0");
    target_config_set(t, "EXECUTABLE", t->name);

    const char *out = configure_out_dir(t, ctx->platform);
    if (!mel_mkdirs(out)) return false;
    const char *plist = temp_sprintf("%s/Info.plist", out);
    const char *base = ios ? "tools/build/apple/Info.ios.plist" : "tools/build/apple/Info.macos.plist";
    if (!copy_file(base, plist)) return false;

    bool ok = plist_set(plist, "CFBundleName",            target_config_get(t, "BUNDLE_NAME"))
           && plist_set(plist, "CFBundleDisplayName",     target_config_get(t, "BUNDLE_DISPLAY_NAME"))
           && plist_set(plist, "CFBundleIdentifier",      target_config_get(t, "BUNDLE_ID"))
           && plist_set(plist, "CFBundleExecutable",      target_config_get(t, "EXECUTABLE"))
           && plist_set(plist, "CFBundleVersion",         target_config_get(t, "VERSION_CODE"))
           && plist_set(plist, "CFBundleShortVersionString", target_config_get(t, "VERSION_NAME"));
    ok = ok && (ios ? plist_set(plist, "MinimumOSVersion",     target_config_get(t, "MIN_IOS"))
                    : plist_set(plist, "LSMinimumSystemVersion", target_config_get(t, "MIN_MACOS")));
    if (!ok) return false;

    nob_log(NOB_INFO, "configured Info.plist at %s", plist);
    return true;
}

static bool configure_win32(Mel_Build_Context *ctx) {
    Mel_Build_Target *t = ctx->target;
    const char *out = configure_out_dir(t, ctx->platform);
    if (!mel_mkdirs(out)) return false;
    if (!copy_file("tools/build/win32/app.manifest", temp_sprintf("%s/app.manifest", out))) return false;
    if (!copy_file("tools/build/win32/app.rc", temp_sprintf("%s/app.rc", out))) return false;
    nob_log(NOB_INFO, "configured win32 resources at %s", out);
    return true;
}

static bool default_configure(Mel_Build_Context *ctx) {
    if (ctx->target->kind != MEL_TARGET_APPLICATION) return true;
    switch (ctx->platform) {
        case MEL_PLATFORM_ANDROID: return configure_android(ctx);
        case MEL_PLATFORM_MACOS:
        case MEL_PLATFORM_IOS:     return configure_apple(ctx);
        case MEL_PLATFORM_WIN32:   return configure_win32(ctx);
        default:                   return true;
    }
}

static bool default_compile(Mel_Build_Context *ctx) {
    // Android compiles every ABI inside the application's link stage.
    if (ctx->platform == MEL_PLATFORM_ANDROID) return true;

    File_Paths bridges = {0};
    if (!target_resolve_sources(ctx->target, ctx->platform, &ctx->sources, &bridges)) return false;

    // Libraries archive only their own non-bridge sources. Applications also
    // compile the bridge sources contributed by their library dependencies.
    if (ctx->target->kind == MEL_TARGET_APPLICATION) {
        for (size_t i = 0; i < ctx->bridge_sources.count; i++)
            da_append(&ctx->sources, ctx->bridge_sources.items[i]);
    }
    return compile_sources(ctx, cc_for(ctx));
}

static bool archive_objects(const char *ar, const char *lib, const File_Paths *objects) {
    Cmd ident = {0};
    cmd_append(&ident, ar, "rcs");
    if (cache_artifact_restore(&ident, objects, lib)) { free(ident.items); return true; }
    free(ident.items);

    if (file_exists(lib)) delete_file(lib);
    Cmd cmd = {0};
    cmd_append(&cmd, ar, "rcs", lib);
    for (size_t i = 0; i < objects->count; i++) cmd_append(&cmd, objects->items[i]);
    if (!cmd_run_sync_and_reset(&cmd)) return false;

    Cmd ident2 = {0};
    cmd_append(&ident2, ar, "rcs");
    cache_artifact_store(&ident2, objects, lib);
    free(ident2.items);
    return true;
}

static bool android_build(Mel_Build_Context *ctx);

// Link the application to a wasm bundle. Emscripten emits the HTML shell, the
// loader JS, and app.wasm in one go; wasi-sdk produces a bare app.wasm (no DOM,
// run under a wasi host). Both pull -lmelody and any web-supporting third-party
// archives from the resolved link flags. The final bundle is the hot, one-shot
// last step, so it is linked directly rather than through the artifact cache.
static bool link_web_app(Mel_Build_Context *ctx) {
    Mel_Build_Target *t = ctx->target;
    const char *out = web_out_dir(t, ctx->config);
    if (!mel_mkdirs(out)) return false;

    bool wasi = (g_web_tc == WEB_WASI);
    const char *artifact = wasi ? temp_sprintf("%s/app.wasm", out)
                                : temp_sprintf("%s/app.html", out);

    Cmd cmd = {0};
    cmd_append(&cmd, web_cc());
    web_target_flags(&cmd); // wasi target + sysroot; no-op for emcc
    for (size_t i = 0; i < ctx->objects.count; i++) cmd_append(&cmd, ctx->objects.items[i]);
    for (size_t i = 0; i < ctx->resolved.link_flags.count; i++)
        cmd_append(&cmd, ctx->resolved.link_flags.items[i]);
    if (ctx->config == MEL_CONFIG_RELEASE) cmd_append(&cmd, "-O2");
    else                                   cmd_append(&cmd, "-g");

    if (!wasi) {
        cmd_append(&cmd, "-sALLOW_MEMORY_GROWTH=1");
        if (g_web_asyncify)  cmd_append(&cmd, "-sASYNCIFY");
        if (g_web_threading) cmd_append(&cmd, "-pthread", "-sPTHREAD_POOL_SIZE=4");
        const char *shell = "tools/build/web/shell.html";
        if (file_exists(shell)) cmd_append(&cmd, "--shell-file", shell);
    }
    cmd_append(&cmd, "-o", artifact);

    if (!cmd_run_sync_and_reset(&cmd)) return false;
    nob_log(NOB_INFO, "linked %s", artifact);
    return true;
}

static bool default_link(Mel_Build_Context *ctx) {
    Mel_Build_Target *t = ctx->target;

    if (ctx->platform == MEL_PLATFORM_ANDROID) {
        if (t->kind == MEL_TARGET_APPLICATION) return android_build(ctx);
        return true;
    }

    if (t->kind == MEL_TARGET_LIBRARY) {
        if (!mel_mkdirs(target_out_dir(t, ctx->platform, ctx->config))) return false;
        if (!archive_objects(ar_for(ctx), ctx->artifact, &ctx->objects)) return false;
        nob_log(NOB_INFO, "archived %s (%zu objects)", ctx->artifact, ctx->objects.count);
        return true;
    }

    if (t->kind == MEL_TARGET_APPLICATION) {
        if (ctx->platform == MEL_PLATFORM_WEB) return link_web_app(ctx);

        const char *app_out = temp_sprintf("%s/%s", target_out_dir(t, ctx->platform, ctx->config), t->name);
        if (!mel_mkdirs(app_out)) return false;

        // Win32: compile the generated resource script and emit an .exe.
        const char *res_obj = NULL;
        const char *out_bin = ctx->artifact;
        if (ctx->platform == MEL_PLATFORM_WIN32) {
            out_bin = temp_sprintf("%s.exe", ctx->artifact);
            const char *rc_dir = configure_out_dir(t, ctx->platform);
            const char *rc = temp_sprintf("%s/app.rc", rc_dir);
            if (file_exists(rc)) {
                res_obj = temp_sprintf("%s/app.res", app_out);
                const char *label = target_config_get(t, "APP_LABEL");
                if (!label) label = t->name;
                const char *ver = target_config_get(t, "VERSION_NAME");
                if (!ver) ver = "0.1.0";
                Cmd rcc = {0};
                cmd_append(&rcc, "llvm-rc", "/nologo", temp_sprintf("/I%s", rc_dir));
                cmd_append(&rcc, temp_sprintf("/DMEL_APP_NAME=\"%s\"", label));
                cmd_append(&rcc, temp_sprintf("/DMEL_APP_VERSION=\"%s\"", ver));
                const char *icon = temp_sprintf("%s/apps/%s/win32/app.ico", get_current_dir_temp(), t->name);
                if (file_exists(icon)) cmd_append(&rcc, temp_sprintf("/DMEL_APP_ICON=\"%s\"", icon));
                cmd_append(&rcc, "/fo", res_obj, rc);
                if (!cmd_run_sync_and_reset(&rcc)) return false;
            }
        }

        File_Paths link_inputs = {0};
        for (size_t i = 0; i < ctx->objects.count; i++) da_append(&link_inputs, ctx->objects.items[i]);
        if (res_obj) da_append(&link_inputs, res_obj);

        Cmd ident = {0};
        cmd_append(&ident, "clang");
        if (ctx->platform == MEL_PLATFORM_IOS) mel_ios_clang_flags(&ident);
        for (size_t i = 0; i < ctx->resolved.link_flags.count; i++)
            cmd_append(&ident, ctx->resolved.link_flags.items[i]);
        if (cache_artifact_restore(&ident, &link_inputs, out_bin)) {
            free(ident.items);
            nob_log(NOB_INFO, "linked %s (cached)", out_bin);
            return true;
        }
        free(ident.items);

        Cmd cmd = {0};
        cmd_append(&cmd, "clang");
        if (ctx->platform == MEL_PLATFORM_IOS) mel_ios_clang_flags(&cmd);
        for (size_t i = 0; i < ctx->objects.count; i++) cmd_append(&cmd, ctx->objects.items[i]);
        if (res_obj) cmd_append(&cmd, res_obj);
        for (size_t i = 0; i < ctx->resolved.link_flags.count; i++)
            cmd_append(&cmd, ctx->resolved.link_flags.items[i]);
        cmd_append(&cmd, "-o", out_bin);
        if (!cmd_run_sync_and_reset(&cmd)) return false;
        nob_log(NOB_INFO, "linked %s", out_bin);

        Cmd ident2 = {0};
        cmd_append(&ident2, "clang");
        if (ctx->platform == MEL_PLATFORM_IOS) mel_ios_clang_flags(&ident2);
        for (size_t i = 0; i < ctx->resolved.link_flags.count; i++)
            cmd_append(&ident2, ctx->resolved.link_flags.items[i]);
        cache_artifact_store(&ident2, &link_inputs, out_bin);
        free(ident2.items);
        return true;
    }

    return true; // third-party / meta: linkage handled by custom callbacks
}

static bool copy_preserving(const char *src, const char *dst) {
    Cmd cmd = {0};
    cmd_append(&cmd, "cp", "-p", src, dst);
    return cmd_run_sync_and_reset(&cmd);
}

// Assemble an apple .app bundle from the linked executable and generated plist.
// macOS uses the nested Contents/MacOS layout; iOS bundles are flat (executable
// and Info.plist at the .app root).
static bool package_apple(Mel_Build_Context *ctx) {
    Mel_Build_Target *t = ctx->target;
    const char *pname     = mel_platform_name(ctx->platform);
    const char *bundle    = temp_sprintf("%s/%s/%s/%s.app", MEL_BUILD_DIR, t->name, pname, t->name);
    const char *plist_src = temp_sprintf("%s/%s/%s/Info.plist", MEL_BUILD_DIR, t->name, pname);

    if (ctx->platform == MEL_PLATFORM_IOS) {
        if (!mel_mkdirs(bundle)) return false;
        if (!copy_preserving(ctx->artifact, temp_sprintf("%s/%s", bundle, t->name))) return false;
        if (!copy_file(plist_src, temp_sprintf("%s/Info.plist", bundle))) return false;
        // The simulator refuses to install an unsigned bundle; an ad-hoc
        // signature is enough (no identity, no provisioning profile).
        Cmd sign = {0};
        cmd_append(&sign, "codesign", "--force", "--sign", "-", bundle);
        if (!cmd_run_sync_and_reset(&sign)) return false;
        nob_log(NOB_INFO, "packaged %s", bundle);
        return true;
    }

    const char *contents = temp_sprintf("%s/Contents", bundle);
    const char *macos    = temp_sprintf("%s/MacOS", contents);
    const char *res      = temp_sprintf("%s/Resources", contents);
    if (!mel_mkdirs(macos)) return false;
    if (!mel_mkdirs(res)) return false;

    if (!copy_preserving(ctx->artifact, temp_sprintf("%s/%s", macos, t->name))) return false;
    if (!copy_file(plist_src, temp_sprintf("%s/Info.plist", contents))) return false;

    nob_log(NOB_INFO, "packaged %s", bundle);
    return true;
}

static bool package_android(Mel_Build_Context *ctx);

static bool default_package(Mel_Build_Context *ctx) {
    if (ctx->target->kind != MEL_TARGET_APPLICATION) return true;
    switch (ctx->platform) {
        case MEL_PLATFORM_MACOS:
        case MEL_PLATFORM_IOS:     return package_apple(ctx);
        case MEL_PLATFORM_ANDROID: return package_android(ctx);
        default:                   return true;
    }
}

typedef bool (*Default_Fn)(Mel_Build_Context *);
static const Default_Fn k_defaults[MEL_STAGE_COUNT] = {
    [MEL_STAGE_CONFIGURE] = default_configure,
    [MEL_STAGE_COMPILE]   = default_compile,
    [MEL_STAGE_LINK]      = default_link,
    [MEL_STAGE_PACKAGE]   = default_package,
};

static bool run_stage(Mel_Build_Context *ctx, Mel_Stage stage) {
    if (!ctx->target->suppress_default[stage] && k_defaults[stage]) {
        if (!k_defaults[stage](ctx)) return false;
    }
    for (size_t i = 0; i < ctx->target->user_cb_count[stage]; i++) {
        if (!ctx->target->user_cbs[stage][i](ctx)) return false;
    }
    return true;
}

// =============================================================================
// Android NDK pipeline
// =============================================================================

typedef struct { const char *abi; const char *triple; } Android_Abi;
static const Android_Abi k_android_abis[] = {
    { "arm64-v8a", "aarch64-linux-android" },
    { "x86_64",    "x86_64-linux-android"  },
};
#define ANDROID_API 23

static const char *android_sdk_dir(const char *app_name) {
    (void)app_name;
    const char *sdk = getenv("ANDROID_HOME"); if (sdk && sdk[0]) return sdk;
    sdk = getenv("ANDROID_SDK_ROOT");         if (sdk && sdk[0]) return sdk;
    String_Builder sb = {0};
    if (!read_entire_file("local.properties", &sb)) return NULL;
    sb_append_null(&sb);
    char *p = strstr(sb.items, "sdk.dir=");
    if (!p) return NULL;
    p += strlen("sdk.dir=");
    char *e = p;
    while (*e && *e != '\n' && *e != '\r') e++;
    return temp_sprintf("%.*s", (int)(e - p), p);
}

static const char *android_ndk_dir(const char *sdk) {
    const char *root = temp_sprintf("%s/ndk", sdk);
    File_Paths e = {0};
    if (!read_entire_dir(root, &e)) return NULL;
    const char *best = NULL;
    for (size_t i = 0; i < e.count; i++) {
        const char *n = e.items[i];
        if (strcmp(n, ".") == 0 || strcmp(n, "..") == 0) continue;
        if (get_file_type(temp_sprintf("%s/%s", root, n)) != NOB_FILE_DIRECTORY) continue;
        if (best == NULL || strcmp(n, best) > 0) best = temp_strdup(n);
    }
    return best ? temp_sprintf("%s/%s", root, best) : NULL;
}

static const char *android_toolchain_bin(const char *ndk) {
    static const char *const hosts[] = {
        "darwin-x86_64", "darwin-arm64", "linux-x86_64", "windows-x86_64",
    };
    for (size_t i = 0; i < NOB_ARRAY_LEN(hosts); i++) {
        const char *path = temp_sprintf("%s/toolchains/llvm/prebuilt/%s/bin", ndk, hosts[i]);
        if (get_file_type(path) == NOB_FILE_DIRECTORY) return path;
    }
    return NULL;
}

static Cross make_android_cross(const Android_Abi *a, const char *bin, const char *ndk) {
    Cross c = {0};
    c.abi = a->abi; c.triple = a->triple; c.api = ANDROID_API; c.ndk = ndk;
    c.cc = temp_sprintf("%s/%s%d-clang", bin, a->triple, ANDROID_API);
    c.ar = temp_sprintf("%s/llvm-ar", bin);
    c.ranlib = temp_sprintf("%s/llvm-ranlib", bin);
    return c;
}

// Build a third-party dependency (and its third-party deps) for one ABI.
static bool android_build_tp(Mel_Build_Target *tp, const Cross *cross, Mel_Config cfg,
                             File_Paths *built) {
    if (name_seen(built, tp->name)) return true;
    for (size_t i = 0; i < tp->deps.count; i++) {
        Mel_Build_Target *d = registry_find(tp->deps.items[i]);
        if (d && d->kind == MEL_TARGET_THIRD_PARTY)
            if (!android_build_tp(d, cross, cfg, built)) return false;
    }
    da_append(built, tp->name);

    Mel_Build_Context tctx;
    memset(&tctx, 0, sizeof(tctx));
    tctx.target = tp;
    tctx.platform = MEL_PLATFORM_ANDROID;
    tctx.config = cfg;
    tctx.cross = cross;
    return run_stage(&tctx, MEL_STAGE_COMPILE);
}

// Collect melody's transitive third-party deps, deps-first (build order).
static void collect_tp(Mel_Build_Target *t, Mel_Build_Target **out, size_t *n, File_Paths *seen) {
    for (size_t i = 0; i < t->deps.count; i++) {
        Mel_Build_Target *d = registry_find(t->deps.items[i]);
        if (!d || d->kind != MEL_TARGET_THIRD_PARTY || name_seen(seen, d->name)) continue;
        collect_tp(d, out, n, seen);
        if (!name_seen(seen, d->name)) { da_append(seen, d->name); out[(*n)++] = d; }
    }
}

static void append_android_includes(Cmd *cmd, Mel_Build_Target *melody,
                                    Mel_Build_Target **tp, size_t tp_count, const char *abi) {
    for (size_t i = 0; i < melody->pub.includes.count; i++)
        cmd_append(cmd, temp_sprintf("-I%s", melody->pub.includes.items[i].value));
    for (size_t i = 0; i < tp_count; i++)
        cmd_append(cmd, "-isystem", temp_sprintf("%s/include", tp_prefix_named(MEL_PLATFORM_ANDROID, abi, tp[i]->name)));
}

static const char *android_obj_path(const char *objdir, const char *src) {
    String_Builder mangle = {0};
    for (const char *q = src; *q; q++) sb_append_buf(&mangle, (*q == '/') ? "." : q, 1);
    sb_append_null(&mangle);
    const char *obj = temp_sprintf("%s/%s.o", objdir, mangle.items);
    free(mangle.items);
    return obj;
}

static void android_tu_flags(Cmd *flags, const Cross *cross, Mel_Build_Context *ctx,
                             Mel_Build_Target *melody, Mel_Build_Target **tp, size_t tp_count,
                             const char *abi, const char *src) {
    cmd_append(flags, cross->cc);
    for (size_t k = 0; k < NOB_ARRAY_LEN(k_base_cflags); k++) cmd_append(flags, k_base_cflags[k]);
    cmd_append(flags, ctx->config == MEL_CONFIG_RELEASE ? "-O2" : "-O0", "-g", "-fPIC", "-DANDROID");
    if (source_is_objc(src)) cmd_append(flags, "-fobjc-arc");
    append_android_includes(flags, melody, tp, tp_count, abi);
}

// Compile a source set with the NDK toolchain, appending the produced objects to
// out_objs. Shares the content-addressed cache with the host compile path, so a
// second Android build skips unchanged translation units.
static bool android_compile_set(const Cross *cross, Mel_Build_Context *ctx,
                                Mel_Build_Target *melody, Mel_Build_Target **tp, size_t tp_count,
                                const char *abi, const char *objdir,
                                const File_Paths *srcs, File_Paths *out_objs) {
    size_t par = nob_nprocs(); if (par < 1) par = 1;
    const char *cwd = get_current_dir_temp();
    size_t base_n = out_objs->count;

    Procs procs = {0};
    for (size_t i = 0; i < srcs->count; i++) {
        const char *src = srcs->items[i];
        const char *obj = android_obj_path(objdir, src);
        da_append(out_objs, obj);

        Cmd flags = {0};
        android_tu_flags(&flags, cross, ctx, melody, tp, tp_count, abi, src);
        bool spawned = false;
        if (!compile_tu(cross->cc, flags, src, obj, temp_sprintf("%s.d", obj), false, cwd, &procs, par, &spawned)) {
            free(flags.items);
            return false;
        }
        free(flags.items);
    }
    if (!procs_wait_and_reset(&procs)) return false;

    for (size_t i = 0; i < srcs->count; i++) {
        const char *src = srcs->items[i];
        const char *obj = out_objs->items[base_n + i];
        Cmd flags = {0};
        android_tu_flags(&flags, cross, ctx, melody, tp, tp_count, abi, src);
        cache_obj_store(&flags, cross->cc, obj, temp_sprintf("%s.d", obj));
        free(flags.items);
    }
    return true;
}

static bool android_build(Mel_Build_Context *ctx) {
    Mel_Build_Target *app = ctx->target;
    const char *sdk = android_sdk_dir(app->name);
    if (!sdk) { nob_log(NOB_ERROR, "Android SDK not found (set ANDROID_HOME or sdk.dir in ./local.properties)"); return false; }
    const char *ndk = android_ndk_dir(sdk);
    if (!ndk) { nob_log(NOB_ERROR, "Android NDK not found under %s/ndk", sdk); return false; }
    const char *bin = android_toolchain_bin(ndk);
    if (!bin) { nob_log(NOB_ERROR, "NDK LLVM toolchain not found under %s", ndk); return false; }
    nob_log(NOB_INFO, "android SDK %s", sdk);
    nob_log(NOB_INFO, "android NDK %s", ndk);

    Mel_Build_Target *melody = registry_find("melody");
    if (!melody) { nob_log(NOB_ERROR, "android build requires a 'melody' target"); return false; }

    Mel_Build_Target *tp[64]; size_t tp_count = 0;
    File_Paths tp_seen = {0};
    collect_tp(melody, tp, &tp_count, &tp_seen);

    File_Paths lib_srcs = {0}, bridges = {0}, app_srcs = {0}, app_bridges = {0};
    if (!target_resolve_sources(melody, MEL_PLATFORM_ANDROID, &lib_srcs, &bridges)) return false;
    if (!target_resolve_sources(app, MEL_PLATFORM_ANDROID, &app_srcs, &app_bridges)) return false;

    const char *jnilibs = temp_sprintf("%s/%s/android/app/src/main/jniLibs", MEL_BUILD_DIR, app->name);

    for (size_t ai = 0; ai < NOB_ARRAY_LEN(k_android_abis); ai++) {
        const Android_Abi *abi = &k_android_abis[ai];
        Cross cross = make_android_cross(abi, bin, ndk);

        File_Paths built = {0};
        for (size_t i = 0; i < tp_count; i++)
            if (!android_build_tp(tp[i], &cross, ctx->config, &built)) return false;

        const char *meldir = temp_sprintf("%s/android-%s", MEL_BUILD_DIR, abi->abi);
        const char *objdir = temp_sprintf("%s/obj/%s", meldir, app->name);
        if (!mel_mkdirs(objdir)) return false;

        // Library sources go into a per-ABI archive; linking the .so against
        // -lmelody pulls only the members the app and bridges actually need, so
        // unreferenced objects (and their third-party deps) are dropped.
        File_Paths lib_objs = {0};
        if (!android_compile_set(&cross, ctx, melody, tp, tp_count, abi->abi, objdir, &lib_srcs, &lib_objs)) return false;
        const char *melody_a = temp_sprintf("%s/libmelody.a", meldir);
        if (file_exists(melody_a)) delete_file(melody_a);
        Cmd ar = {0};
        cmd_append(&ar, cross.ar, "rcs", melody_a);
        for (size_t i = 0; i < lib_objs.count; i++) cmd_append(&ar, lib_objs.items[i]);
        if (!cmd_run_sync_and_reset(&ar)) return false;

        File_Paths link_objs = {0};
        if (!android_compile_set(&cross, ctx, melody, tp, tp_count, abi->abi, objdir, &bridges, &link_objs)) return false;
        if (!android_compile_set(&cross, ctx, melody, tp, tp_count, abi->abi, objdir, &app_srcs, &link_objs)) return false;

        const char *abidir = temp_sprintf("%s/%s", jnilibs, abi->abi);
        if (!mel_mkdirs(abidir)) return false;

        Cmd link = {0};
        cmd_append(&link, cross.cc, "-shared", "-fPIC", "-Wl,--gc-sections", "-Wl,--as-needed");
        for (size_t i = 0; i < link_objs.count; i++) cmd_append(&link, link_objs.items[i]);
        cmd_append(&link, temp_sprintf("-L%s", meldir), "-lmelody");
        // Third-party archives, dependents-first (reverse of build order).
        for (size_t i = tp_count; i-- > 0; ) {
            cmd_append(&link, temp_sprintf("-L%s/lib", tp_prefix_named(MEL_PLATFORM_ANDROID, abi->abi, tp[i]->name)));
            File_Paths ls = {0};
            prop_resolve(&ls, &tp[i]->pub.link_flags, MEL_PLATFORM_ANDROID);
            for (size_t k = 0; k < ls.count; k++) cmd_append(&link, ls.items[k]);
        }
        File_Paths mel_ls = {0};
        prop_resolve(&mel_ls, &melody->pub.link_flags, MEL_PLATFORM_ANDROID);
        for (size_t k = 0; k < mel_ls.count; k++) cmd_append(&link, mel_ls.items[k]);
        cmd_append(&link, "-o", temp_sprintf("%s/libmelody.so", abidir));
        if (!cmd_run_sync_and_reset(&link)) return false;
        nob_log(NOB_INFO, "linked %s/libmelody.so", abidir);
    }
    return true;
}

static void android_gradle_props(Cmd *cmd, const Mel_Build_Target *t) {
    const char *cwd = get_current_dir_temp();
    cmd_append(cmd, temp_sprintf("-Pmelody.namespace=%s",       target_config_get(t, "NAMESPACE")));
    cmd_append(cmd, temp_sprintf("-Pmelody.applicationId=%s",   target_config_get(t, "APPLICATION_ID")));
    cmd_append(cmd, temp_sprintf("-Pmelody.appLabel=%s",        target_config_get(t, "APP_LABEL")));
    cmd_append(cmd, temp_sprintf("-Pmelody.compileSdk=%s",      target_config_get(t, "COMPILE_SDK")));
    cmd_append(cmd, temp_sprintf("-Pmelody.minSdk=%s",          target_config_get(t, "MIN_SDK")));
    cmd_append(cmd, temp_sprintf("-Pmelody.targetSdk=%s",       target_config_get(t, "TARGET_SDK")));
    cmd_append(cmd, temp_sprintf("-Pmelody.versionCode=%s",     target_config_get(t, "VERSION_CODE")));
    cmd_append(cmd, temp_sprintf("-Pmelody.versionName=%s",     target_config_get(t, "VERSION_NAME")));
    cmd_append(cmd, temp_sprintf("-Pmelody.rootProjectName=%s", target_config_get(t, "ROOTPROJECT_NAME")));
    cmd_append(cmd, temp_sprintf("-Pmelody.javaSrcDirs=%s",     android_java_srcdirs_csv(cwd)));

    const char *manifest = temp_sprintf("%s/apps/%s/android/manifest-overlay/AndroidManifest.xml", cwd, t->name);
    if (file_exists(manifest)) cmd_append(cmd, temp_sprintf("-Pmelody.manifestOverlay=%s", manifest));
    const char *appjava = temp_sprintf("%s/apps/%s/android/java", cwd, t->name);
    if (file_exists(appjava)) cmd_append(cmd, temp_sprintf("-Pmelody.appJavaDir=%s", appjava));
    const char *res = temp_sprintf("%s/apps/%s/android/res", cwd, t->name);
    if (file_exists(res)) cmd_append(cmd, temp_sprintf("-Pmelody.resOverlayDir=%s", res));
}

static bool package_android(Mel_Build_Context *ctx) {
    const char *proj = temp_sprintf("%s/%s/android", MEL_BUILD_DIR, ctx->target->name);
    const char *task = ctx->config == MEL_CONFIG_RELEASE ? ":app:assembleMelodyRelease" : ":app:assembleMelodyDebug";
    Cmd cmd = {0};
    cmd_append(&cmd, "gradle", "-q", "-p", proj);
    android_gradle_props(&cmd, ctx->target);
    cmd_append(&cmd, task);
    return cmd_run_sync_and_reset(&cmd);
}

// Release builds have no signingConfig (signing is out of scope), so Gradle
// emits the unsigned variant — which adb refuses to install.
static const char *android_apk_path(const char *app_name, Mel_Config c) {
    const char *base = temp_sprintf("%s/%s/android/app/build/outputs/apk/melody", MEL_BUILD_DIR, app_name);
    if (c == MEL_CONFIG_RELEASE) return temp_sprintf("%s/release/app-melody-release-unsigned.apk", base);
    return temp_sprintf("%s/debug/app-melody-debug.apk", base);
}

static const char *adb_path(const char *app_name) {
    const char *sdk = android_sdk_dir(app_name);
    return sdk ? temp_sprintf("%s/platform-tools/adb", sdk) : "adb";
}

static bool android_install(const char *app_name, Mel_Config c) {
    const char *apk = android_apk_path(app_name, c);
    if (!file_exists(apk)) { nob_log(NOB_ERROR, "APK not found at %s", apk); return false; }
    if (c == MEL_CONFIG_RELEASE) {
        nob_log(NOB_ERROR, "release APK %s is unsigned; adb cannot install it (add a signingConfig)", apk);
        return false;
    }
    Cmd cmd = {0};
    cmd_append(&cmd, adb_path(app_name), "install", "-r", apk);
    return cmd_run_sync_and_reset(&cmd);
}

static bool android_launch(const Mel_Build_Target *app) {
    const char *pkg = target_config_get(app, "APPLICATION_ID");
    if (!pkg) { nob_log(NOB_ERROR, "app '%s' has no APPLICATION_ID config", app->name); return false; }
    Cmd cmd = {0};
    cmd_append(&cmd, adb_path(app->name), "shell", "monkey", "-p", pkg,
               "-c", "android.intent.category.LAUNCHER", "1");
    return cmd_run_sync_and_reset(&cmd);
}

static bool android_logcat(const char *app_name) {
    const char *adb = adb_path(app_name);
    Cmd clear = {0};
    cmd_append(&clear, adb, "logcat", "-c");
    cmd_run_sync_and_reset(&clear);
    Cmd cmd = {0};
    cmd_append(&cmd, adb, "logcat", "Melody:V", "AndroidRuntime:E", "*:S");
    return cmd_run_sync_and_reset(&cmd);
}

// Boot a simulator (whatever Simulator.app last used), install the flat bundle,
// and launch it by bundle id. No device target / signing identity is involved.
static bool ios_run(const Mel_Build_Target *app, Mel_Config c) {
    (void)c;
    const char *bundle = temp_sprintf("%s/%s/ios/%s.app", MEL_BUILD_DIR, app->name, app->name);
    if (!file_exists(bundle)) { nob_log(NOB_ERROR, "iOS app not found at %s", bundle); return false; }
    const char *bid = target_config_get(app, "BUNDLE_ID");
    if (!bid) bid = temp_sprintf("orgwall.%s", app->name);

    Cmd open = {0};
    cmd_append(&open, "open", "-a", "Simulator");
    if (!cmd_run_sync_and_reset(&open)) return false;

    Cmd wait = {0};
    cmd_append(&wait, "xcrun", "simctl", "bootstatus", "booted");
    if (!cmd_run_sync_and_reset(&wait)) return false;

    Cmd install = {0};
    cmd_append(&install, "xcrun", "simctl", "install", "booted", bundle);
    if (!cmd_run_sync_and_reset(&install)) return false;

    Cmd launch = {0};
    cmd_append(&launch, "xcrun", "simctl", "launch", "--console-pty", "booted", bid);
    return cmd_run_sync_and_reset(&launch);
}

// Serve the web bundle with a local HTTP server and open the browser. The
// server runs in the foreground (ctrl-c to stop); the browser is opened a beat
// later from a detached subshell so the page loads after the socket is up. With
// threading on, the server adds COOP/COEP so SharedArrayBuffer is available.
static bool web_run(Mel_Build_Target *t, Mel_Config c) {
    if (g_web_tc == WEB_WASI) {
        nob_log(NOB_ERROR, "wasi-sdk produces a bare app.wasm; run it under a wasi host (wasmtime), not the browser");
        return false;
    }
    const char *dir = web_out_dir(t, c);
    const char *page = temp_sprintf("%s/app.html", dir);
    if (!file_exists(page)) { nob_log(NOB_ERROR, "web bundle not found at %s", page); return false; }

    int port = 8080;
    const char *url = temp_sprintf("http://localhost:%d/app.html", port);
    nob_log(NOB_INFO, "serving %s", url);

    Cmd cmd = {0};
    cmd_append(&cmd, "sh", "-c",
               temp_sprintf("(sleep 1; open '%s') & exec python3 tools/build/web/serve.py '%s' %d %d",
                            url, dir, port, g_web_threading ? 1 : 0));
    return cmd_run_sync_and_reset(&cmd);
}

// =============================================================================
// Build orchestration
// =============================================================================

static void context_init(Mel_Build_Context *ctx, Mel_Build_Target *t, Mel_Platform p, Mel_Config c) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->target = t;
    ctx->platform = p;
    ctx->config = c;
    ctx->out_dir = target_out_dir(t, p, c);
    if (t->kind == MEL_TARGET_LIBRARY)          ctx->artifact = library_artifact(t, p, c);
    else if (t->kind == MEL_TARGET_APPLICATION) ctx->artifact = app_artifact(t, p, c);
    else if (t->kind == MEL_TARGET_THIRD_PARTY) ctx->artifact = thirdparty_prefix(t, p);
    context_resolve_props(ctx);
}

static bool target_supports(const Mel_Build_Target *t, Mel_Platform p) {
    if (!t->platform_set) return true;
    return t->platforms[p];
}

static bool build_target_through(Mel_Build_Target *t, Mel_Platform p, Mel_Config c, Mel_Stage last) {
    if (!target_supports(t, p)) {
        nob_log(NOB_ERROR, "target '%s' does not support platform '%s'", t->name, mel_platform_name(p));
        return false;
    }
    // Android builds melody and all third-party libs per-ABI inside the
    // application's link stage, so dependency targets are skipped here.
    if (p == MEL_PLATFORM_ANDROID && t->kind != MEL_TARGET_APPLICATION) return true;

    Mel_Build_Context ctx;
    context_init(&ctx, t, p, c);
    for (int s = 0; s <= (int)last; s++) {
        if (!run_stage(&ctx, (Mel_Stage)s)) {
            nob_log(NOB_ERROR, "target '%s': stage %d failed", t->name, s);
            return false;
        }
    }
    return true;
}

// Topologically order a target and its transitive deps (deps first). Deps that
// don't support the target platform are skipped (and so are their transitive
// deps that are only reachable through them).
static void topo_visit(Mel_Build_Target *t, Mel_Platform p, Mel_Config c,
                       File_Paths *visited, Mel_Build_Target ***order, size_t *order_count) {
    if (name_seen(visited, t->name)) return;
    da_append(visited, t->name);
    for (size_t i = 0; i < t->deps.count; i++) {
        Mel_Build_Target *d = registry_find(t->deps.items[i]);
        if (!d || !target_supports(d, p)) continue;
        topo_visit(d, p, c, visited, order, order_count);
    }
    (*order)[(*order_count)++] = t;
}

static bool build_graph(Mel_Build_Target *root, Mel_Platform p, Mel_Config c, Mel_Stage last) {
    Mel_Build_Target **order = malloc(sizeof(*order) * (g_targets.count + 1));
    size_t order_count = 0;
    File_Paths visited = {0};
    topo_visit(root, p, c, &visited, &order, &order_count);

    // Dependencies only need building (through link) when the root actually
    // compiles. A configure-only invocation runs configure on the root alone.
    Mel_Stage dep_last = (last >= MEL_STAGE_COMPILE) ? MEL_STAGE_LINK : last;
    bool ok = true;
    for (size_t i = 0; i < order_count && ok; i++) {
        Mel_Stage target_last = (order[i] == root) ? last : dep_last;
        ok = build_target_through(order[i], p, c, target_last);
    }
    free(order);
    return ok;
}

// =============================================================================
// Target discovery + module loading
// =============================================================================

// Path of the statically-linked build library each target's build.c links
// against. Built once per run from tools/build/build.c.
static const char *build_lib_path(void) {
#ifdef _WIN32
    return MEL_BUILD_DIR "/build-modules/melbuild.lib";
#else
    return MEL_BUILD_DIR "/build-modules/libmelbuild.a";
#endif
}

static const char *g_build_lib;

// Compile tools/build/build.c into the build library archive. Rebuilt only when
// the library source or its headers change. Target build.c modules statically
// link this, so they resolve the mel_build_*/mel_tp_* API at their own link time
// instead of against the running nob binary.
static bool ensure_build_lib(void) {
    if (!mkdir_if_not_exists(MEL_BUILD_DIR)) return false;
    if (!mkdir_if_not_exists(MEL_BUILD_DIR "/build-modules")) return false;

    const char *src = "tools/build/build.c";
    const char *obj = MEL_BUILD_DIR "/build-modules/melbuild.o";
    const char *lib = build_lib_path();
    g_build_lib = lib;

    const char *deps[] = {
        "tools/build/build.c", "tools/build/build.h", "tools/build/internal.h",
    };
    if (file_exists(lib) && needs_rebuild(lib, deps, NOB_ARRAY_LEN(deps)) == 0) return true;

    Cmd cc = {0};
    cmd_append(&cc, "clang", "-std=c23", "-g");
#ifndef _WIN32
    cmd_append(&cc, "-fPIC");
#endif
    cmd_append(&cc, "-c", "-Itools/build", src, "-o", obj);
    if (!cmd_run_sync_and_reset(&cc)) {
        nob_log(NOB_ERROR, "failed to compile build library %s", src);
        return false;
    }

    if (file_exists(lib)) delete_file(lib);
    Cmd ar = {0};
    cmd_append(&ar, static_ar_tool(mel_host_platform()), "rcs", lib, obj);
    if (!cmd_run_sync_and_reset(&ar)) {
        nob_log(NOB_ERROR, "failed to archive build library %s", lib);
        return false;
    }
    return true;
}

static const char *module_so_ext(void) {
#if defined(__APPLE__)
    return "dylib";
#elif defined(_WIN32)
    return "dll";
#else
    return "so";
#endif
}

static bool load_target(const char *dir) {
    const char *src = temp_sprintf("%s/build.c", dir);
    if (!file_exists(src)) return true; // not a target dir

    if (!mkdir_if_not_exists(MEL_BUILD_DIR)) return false;
    if (!mkdir_if_not_exists(temp_sprintf("%s/build-modules", MEL_BUILD_DIR))) return false;

    String_Builder slug = {0};
    for (const char *q = dir; *q; q++) sb_append_buf(&slug, (*q == '/') ? "." : q, 1);
    sb_append_null(&slug);
    const char *so = temp_sprintf("%s/build-modules/%s.%s", MEL_BUILD_DIR, slug.items, module_so_ext());

    // The module statically links libmelbuild.a, so the mel_build_*/mel_tp_* API
    // is resolved at this link rather than against the nob binary at load time.
    Cmd cmd = {0};
    cmd_append(&cmd, "clang", "-shared", "-Itools/build");
#ifndef _WIN32
    cmd_append(&cmd, "-fPIC");
#endif
    cmd_append(&cmd, src, g_build_lib, "-o", so);
    ccmds_add(get_current_dir_temp(), src, cmd);

    const char *deps[] = { src, g_build_lib };
    if (needs_rebuild(so, deps, NOB_ARRAY_LEN(deps)) != 0) {
        if (!cmd_run_sync_and_reset(&cmd)) {
            nob_log(NOB_ERROR, "failed to compile build module %s", src);
            return false;
        }
    } else {
        free(cmd.items);
    }

    void *handle = mel_dlopen(so);
    if (!handle) {
        nob_log(NOB_ERROR, "load(%s) failed: %s", so, mel_dlerror());
        return false;
    }
    bool (*project_fn)(Mel_Build_Target *) = (bool (*)(Mel_Build_Target *))mel_dlsym(handle, "project");
    if (!project_fn) {
        nob_log(NOB_ERROR, "build module %s does not export project()", so);
        mel_dlclose(handle);
        return false;
    }

    Mel_Build_Target t = {0};
    t.dir = temp_strdup(dir);
    t.dl_handle = handle;
    if (!project_fn(&t)) {
        nob_log(NOB_ERROR, "project() failed for %s", dir);
        return false;
    }
    if (!t.name) {
        nob_log(NOB_ERROR, "target in %s did not set a name", dir);
        return false;
    }
    if (!resolve_module_includes(&t)) return false;
    da_append(&g_targets, t);
    return true;
}

static bool discover_targets(void) {
    if (!ensure_build_lib()) return false;

    // The melody library plus any other root-level targets.
    if (!load_target("modules")) return false;

    static const char *const roots[] = { "apps", "third-party" };
    for (size_t r = 0; r < NOB_ARRAY_LEN(roots); r++) {
        if (get_file_type(roots[r]) != NOB_FILE_DIRECTORY) continue;
        File_Paths entries = {0};
        if (!read_entire_dir(roots[r], &entries)) return false;
        for (size_t i = 0; i < entries.count; i++) {
            const char *n = entries.items[i];
            if (strcmp(n, ".") == 0 || strcmp(n, "..") == 0) continue;
            const char *dir = temp_sprintf("%s/%s", roots[r], n);
            if (get_file_type(dir) != NOB_FILE_DIRECTORY) continue;
            if (!load_target(dir)) return false;
        }
    }
    return true;
}

// =============================================================================
// Public driver
// =============================================================================

static bool launch_app(Mel_Build_Target *t, Mel_Platform p, Mel_Config c) {
    Cmd cmd = {0};
    const char *bin = app_artifact(t, p, c);
    if (p == MEL_PLATFORM_WIN32) bin = temp_sprintf("%s.exe", bin);
    cmd_append(&cmd, bin);
    return cmd_run_sync_and_reset(&cmd);
}

// Remove cache entries whose last-access (mtime, bumped on every hit) is older
// than max_age_days. A live build keeps everything it touches fresh, so anything
// stale is unreferenced by the current build set.
static bool cache_gc_dir(const char *dir, time_t cutoff, size_t *removed, size_t *kept) {
    if (get_file_type(dir) != NOB_FILE_DIRECTORY) return true;
    File_Paths entries = {0};
    if (!read_entire_dir(dir, &entries)) return false;
    for (size_t i = 0; i < entries.count; i++) {
        const char *n = entries.items[i];
        if (strcmp(n, ".") == 0 || strcmp(n, "..") == 0) continue;
        const char *full = temp_sprintf("%s/%s", dir, n);
        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (!S_ISREG(st.st_mode)) continue;
        if (st.st_mtime < cutoff) {
            if (delete_file(full)) (*removed)++;
        } else {
            (*kept)++;
        }
    }
    return true;
}

static int cache_gc(int max_age_days) {
    time_t cutoff = time(NULL) - (time_t)max_age_days * 24 * 60 * 60;
    size_t removed = 0, kept = 0;
    bool ok = true;
    ok &= cache_gc_dir(MEL_CACHE_DIR "/objects", cutoff, &removed, &kept);
    ok &= cache_gc_dir(MEL_CACHE_DIR "/artifacts", cutoff, &removed, &kept);
    nob_log(NOB_INFO, "cache gc: removed %zu, kept %zu (threshold %d days)", removed, kept, max_age_days);
    return ok ? 0 : 1;
}

int mel_build_main(int argc, char **argv) {
    const char *command = argc >= 2 ? argv[1] : "build";

    // --release / --debug select the build configuration anywhere on the line;
    // the remaining positionals are target then platform.
    Mel_Config config = MEL_CONFIG_DEBUG;
    const char *target_name = NULL;
    const char *platform_name = NULL;
    for (int i = 2; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--release") == 0)      { config = MEL_CONFIG_RELEASE; continue; }
        if (strcmp(a, "--debug") == 0)        { config = MEL_CONFIG_DEBUG;   continue; }
        if (!target_name)        target_name = a;
        else if (!platform_name) platform_name = a;
    }

    // Cache maintenance needs no target discovery: ./nob cache gc [days].
    if (strcmp(command, "cache") == 0) {
        const char *sub = argc >= 3 ? argv[2] : NULL;
        if (!sub || strcmp(sub, "gc") != 0) {
            nob_log(NOB_ERROR, "usage: nob cache gc [max_age_days]");
            return 1;
        }
        int days = argc >= 4 ? atoi(argv[3]) : 14;
        if (days <= 0) days = 14;
        return cache_gc(days);
    }

    Mel_Platform platform = mel_host_platform();
    if (platform_name && !mel_platform_from_name(platform_name, &platform)) {
        nob_log(NOB_ERROR, "unknown platform '%s'", platform_name);
        return 1;
    }

    if (!discover_targets()) return 1;

    Mel_Stage last = MEL_STAGE_LINK;
    bool do_run = false, do_debug = false, full = false;
    if (strcmp(command, "configure") == 0) last = MEL_STAGE_CONFIGURE;
    else if (strcmp(command, "compile") == 0) last = MEL_STAGE_COMPILE;
    else if (strcmp(command, "link") == 0) last = MEL_STAGE_LINK;
    else if (strcmp(command, "package") == 0) last = MEL_STAGE_PACKAGE;
    else if (strcmp(command, "build") == 0) full = true;
    else if (strcmp(command, "run") == 0) { full = true; do_run = true; }
    else if (strcmp(command, "debug") == 0) { full = true; do_run = true; do_debug = true; }
    else { nob_log(NOB_ERROR, "unknown command '%s'", command); return 1; }

    // A full build produces the platform's final artifact: an APK on Android, a
    // .app bundle on iOS (both needed to install/run), otherwise the linked
    // binary.
    if (full) last = (platform == MEL_PLATFORM_ANDROID || platform == MEL_PLATFORM_IOS)
                         ? MEL_STAGE_PACKAGE : MEL_STAGE_LINK;

    Mel_Build_Target *root = NULL;
    if (target_name) {
        root = registry_find(target_name);
        if (!root) { nob_log(NOB_ERROR, "unknown target '%s'", target_name); return 1; }
    } else {
        root = registry_find("melody");
        if (!root) { nob_log(NOB_ERROR, "no default target 'melody' found"); return 1; }
    }

    // The web toolchain + feature knobs come from the root target and bind the
    // whole dependency graph, so melody compiles with the same toolchain as the
    // app linking it.
    if (platform == MEL_PLATFORM_WEB) {
        g_web_tc = (root->web_toolchain && strcmp(root->web_toolchain, "wasi-sdk") == 0)
                       ? WEB_WASI : WEB_EMSCRIPTEN;
        g_web_threading = root->web_threading;
        g_web_asyncify  = root->web_asyncify;
        nob_log(NOB_INFO, "web toolchain: %s%s%s",
                g_web_tc == WEB_WASI ? "wasi-sdk" : "emscripten",
                g_web_threading ? " +threading" : "",
                g_web_asyncify ? " +asyncify" : "");
    }

    if (!build_graph(root, platform, config, last)) return 1;

    if (!emit_compile_commands()) nob_log(NOB_WARNING, "failed to write compile_commands.json");

    if (do_run) {
        if (root->kind != MEL_TARGET_APPLICATION) {
            nob_log(NOB_ERROR, "target '%s' is not an application", root->name);
            return 1;
        }
        if (platform == MEL_PLATFORM_ANDROID) {
            if (!android_install(root->name, config)) return 1;
            if (!android_launch(root)) return 1;
            return (do_debug ? android_logcat(root->name) : true) ? 0 : 1;
        }
        if (platform == MEL_PLATFORM_IOS) return ios_run(root, config) ? 0 : 1;
        if (platform == MEL_PLATFORM_WEB) return web_run(root, config) ? 0 : 1;
        return launch_app(root, platform, config) ? 0 : 1;
    }
    return 0;
}
