// Melody build library.
//
// The declarative configuration API (mel_build_*), the context accessors, and
// the third-party build helpers (mel_tp_*) that every target's build.c calls.
// Compiled standalone into libmelbuild.a and statically linked into each
// target's build.c shared object, so those modules resolve the API at link time
// with no dynamic-symbol games. The same source is also part of the nob driver
// translation unit (nob.c includes this file, then runner.c); the engine in
// runner.c reuses the static helpers defined here.
//
// Because this file compiles standalone for the archive, every function it
// defines must reach only other functions defined here (plus nob) — nothing
// that lives in runner.c.

#define NOB_IMPLEMENTATION
#include "internal.h"

#include <stdarg.h>

// Autotools' configure scripts reject paths containing backslashes ("unsafe
// srcdir"); flip Windows separators to forward slashes. Drive letters are
// preserved verbatim and MSYS tools accept "D:/foo/bar" without complaint.
#ifdef _WIN32
static const char *mel_to_autotools_path(const char *p) {
    if (!p) return p;
    size_t len = strlen(p);
    char *out = (char *)malloc(len + 1);
    for (size_t i = 0; i < len; i++) out[i] = (p[i] == '\\') ? '/' : p[i];
    out[len] = 0;
    return out;
}

// GNU make on Windows resolves $(SHELL) by searching PATH for sh.exe and
// substitutes the resolved path verbatim into recipes. Git for Windows ships
// sh at "C:\Program Files\Git\usr\bin\sh.exe" — the embedded space then
// crashes libtool recipes like `$(SHELL) ../libtool ...`. Prepend the 8.3
// short form of the chosen sh's directory so make picks a path with no spaces.
static void mel_win32_ensure_no_space_sh_in_path(void) {
    static bool done = false;
    if (done) return;
    done = true;

    char sh_full[MAX_PATH];
    DWORD n = SearchPathA(NULL, "sh.exe", NULL, MAX_PATH, sh_full, NULL);
    if (n == 0 || n >= MAX_PATH) return;

    bool has_space = false;
    for (const char *p = sh_full; *p; p++) if (*p == ' ') { has_space = true; break; }
    if (!has_space) return;

    char *sep = strrchr(sh_full, '\\');
    if (!sep) sep = strrchr(sh_full, '/');
    if (!sep) return;
    *sep = '\0';

    char short_dir[MAX_PATH];
    if (GetShortPathNameA(sh_full, short_dir, MAX_PATH) == 0) return;

    const char *old_path = getenv("PATH");
    if (!old_path) old_path = "";
    size_t need = strlen(short_dir) + 1 + strlen(old_path) + 1;
    char *new_path = (char *)malloc(need);
    snprintf(new_path, need, "%s;%s", short_dir, old_path);
    _putenv_s("PATH", new_path);
    free(new_path);
}
#else
static const char *mel_to_autotools_path(const char *p) { return p; }
static void mel_win32_ensure_no_space_sh_in_path(void) {}
#endif

// =============================================================================
// Platforms
// =============================================================================

static const char *const k_platform_names[MEL_PLATFORM_COUNT] = {
    [MEL_PLATFORM_MACOS]   = "macos",
    [MEL_PLATFORM_IOS]     = "ios",
    [MEL_PLATFORM_LINUX]   = "linux",
    [MEL_PLATFORM_ANDROID] = "android",
    [MEL_PLATFORM_WIN32]   = "win32",
    [MEL_PLATFORM_WEB]     = "web",
};

const char *mel_platform_name(Mel_Platform p) {
    if (p < 0 || p >= MEL_PLATFORM_COUNT) return "unknown";
    return k_platform_names[p];
}

// =============================================================================
// Declarative configuration API
// =============================================================================

static void prop_add(Prop_List *l, const char *v, uint32_t mask) {
    Prop p = { temp_strdup(v), mask };
    da_append(l, p);
}

static Props *props_for(Mel_Build_Target *t, Mel_Visibility vis) {
    return vis == MEL_PUBLIC ? &t->pub : &t->priv;
}

static void target_config_set(Mel_Build_Target *t, const char *key, const char *value) {
    for (size_t i = 0; i < t->cfg_keys.count; i++) {
        if (strcmp(t->cfg_keys.items[i], key) == 0) { t->cfg_vals.items[i] = temp_strdup(value); return; }
    }
    da_append(&t->cfg_keys, temp_strdup(key));
    da_append(&t->cfg_vals, temp_strdup(value));
}

void mel_build_set_name(Mel_Build_Target *t, const char *name) { t->name = temp_strdup(name); }
void mel_build_set_kind(Mel_Build_Target *t, Mel_Target_Kind kind) { t->kind = kind; }

void mel_build_set_platforms(Mel_Build_Target *t, const Mel_Platform *platforms, size_t count) {
    t->platform_set = true;
    for (int i = 0; i < MEL_PLATFORM_COUNT; i++) t->platforms[i] = false;
    for (size_t i = 0; i < count; i++) t->platforms[platforms[i]] = true;
}

void mel_build_add_source_root(Mel_Build_Target *t, const char *dir) {
    da_append(&t->source_roots, temp_strdup(dir));
}

void mel_build_add_modules(Mel_Build_Target *t, const char *modules_dir) {
    da_append(&t->module_roots, temp_strdup(modules_dir));
}

void mel_build_exclude_module_on(Mel_Build_Target *t, Mel_Platform p, const char *module_name) {
    prop_add(&t->excluded_modules, module_name, 1u << p);
}

void mel_build_exclude_source_on(Mel_Build_Target *t, Mel_Platform p, const char *basename) {
    prop_add(&t->excluded_sources, basename, 1u << p);
}

void mel_build_add_dependency(Mel_Build_Target *t, const char *dep_name) {
    da_append(&t->deps, temp_strdup(dep_name));
}

void mel_build_set_config(Mel_Build_Target *t, const char *key, const char *value) {
    target_config_set(t, key, value);
}

// Drain a NULL-terminated token list (the macro wrappers append the NULL),
// recording each token as its own masked property.
static void prop_add_v(Prop_List *l, uint32_t mask, va_list ap) {
    const char *v;
    while ((v = va_arg(ap, const char *)) != NULL) prop_add(l, v, mask);
}

void mel_build_add_cflag_(Mel_Build_Target *t, Mel_Visibility vis, ...) {
    va_list ap; va_start(ap, vis);
    prop_add_v(&props_for(t, vis)->cflags, 0, ap);
    va_end(ap);
}
void mel_build_add_include_(Mel_Build_Target *t, Mel_Visibility vis, ...) {
    va_list ap; va_start(ap, vis);
    prop_add_v(&props_for(t, vis)->includes, 0, ap);
    va_end(ap);
}
void mel_build_add_define_(Mel_Build_Target *t, Mel_Visibility vis, ...) {
    va_list ap; va_start(ap, vis);
    prop_add_v(&props_for(t, vis)->defines, 0, ap);
    va_end(ap);
}
void mel_build_add_link_flag_(Mel_Build_Target *t, Mel_Visibility vis, ...) {
    va_list ap; va_start(ap, vis);
    prop_add_v(&props_for(t, vis)->link_flags, 0, ap);
    va_end(ap);
}

void mel_build_add_cflag_on_(Mel_Build_Target *t, Mel_Visibility vis, Mel_Platform p, ...) {
    va_list ap; va_start(ap, p);
    prop_add_v(&props_for(t, vis)->cflags, 1u << p, ap);
    va_end(ap);
}
void mel_build_add_include_on_(Mel_Build_Target *t, Mel_Visibility vis, Mel_Platform p, ...) {
    va_list ap; va_start(ap, p);
    prop_add_v(&props_for(t, vis)->includes, 1u << p, ap);
    va_end(ap);
}
void mel_build_add_define_on_(Mel_Build_Target *t, Mel_Visibility vis, Mel_Platform p, ...) {
    va_list ap; va_start(ap, p);
    prop_add_v(&props_for(t, vis)->defines, 1u << p, ap);
    va_end(ap);
}
void mel_build_add_link_flag_on_(Mel_Build_Target *t, Mel_Visibility vis, Mel_Platform p, ...) {
    va_list ap; va_start(ap, p);
    prop_add_v(&props_for(t, vis)->link_flags, 1u << p, ap);
    va_end(ap);
}

void mel_build_use_backend_on(Mel_Build_Target *t, Mel_Platform p, const char *backend) {
    t->backends[p] = temp_strdup(backend);
}
void mel_build_use_runtime_on(Mel_Build_Target *t, Mel_Platform p, const char *runtime) {
    t->runtimes[p] = temp_strdup(runtime);
}

void mel_build_web_threading(Mel_Build_Target *t, bool enable) { t->web_threading = enable; }
void mel_build_web_asyncify(Mel_Build_Target *t, bool enable)  { t->web_asyncify = enable; }

static void register_cb(Mel_Build_Target *t, Mel_Stage stage, Mel_Build_Stage_Fn fn) {
    size_t *n = &t->user_cb_count[stage];
    if (*n >= MEL_MAX_STAGE_CBS) {
        nob_log(NOB_ERROR, "target '%s': too many callbacks for stage %d", t->name, stage);
        return;
    }
    t->user_cbs[stage][(*n)++] = fn;
}

void mel_build_on_configure(Mel_Build_Target *t, Mel_Build_Stage_Fn fn) { register_cb(t, MEL_STAGE_CONFIGURE, fn); }
void mel_build_on_compile(Mel_Build_Target *t, Mel_Build_Stage_Fn fn)   { register_cb(t, MEL_STAGE_COMPILE, fn); }
void mel_build_on_link(Mel_Build_Target *t, Mel_Build_Stage_Fn fn)      { register_cb(t, MEL_STAGE_LINK, fn); }
void mel_build_on_package(Mel_Build_Target *t, Mel_Build_Stage_Fn fn)   { register_cb(t, MEL_STAGE_PACKAGE, fn); }

void mel_build_suppress_default(Mel_Build_Target *t, Mel_Stage stage) {
    t->suppress_default[stage] = true;
}

// =============================================================================
// Context API
// =============================================================================

Mel_Platform mel_build_ctx_platform(const Mel_Build_Context *ctx) { return ctx->platform; }
Mel_Config   mel_build_ctx_config(const Mel_Build_Context *ctx)   { return ctx->config; }
const char  *mel_build_ctx_target_name(const Mel_Build_Context *ctx) { return ctx->target->name; }
const char  *mel_build_ctx_backend(const Mel_Build_Context *ctx) { return ctx->backend; }
const char  *mel_build_ctx_runtime(const Mel_Build_Context *ctx) { return ctx->runtime; }
const char  *mel_build_ctx_out_dir(const Mel_Build_Context *ctx) { return ctx->out_dir; }
const char  *mel_build_ctx_artifact(const Mel_Build_Context *ctx) { return ctx->artifact; }

void mel_build_ctx_add_source(Mel_Build_Context *ctx, const char *path) {
    da_append(&ctx->sources, temp_strdup(path));
}

// =============================================================================
// Shared path / filesystem helpers (also used by the runner engine)
// =============================================================================

static const char *tp_prefix_named(Mel_Platform p, const char *abi, const char *name) {
    if (abi) return temp_sprintf("%s/third-party/%s-%s/%s", MEL_BUILD_DIR, mel_platform_name(p), abi, name);
    return temp_sprintf("%s/third-party/%s/%s", MEL_BUILD_DIR, mel_platform_name(p), name);
}

static const char *static_ar_tool(Mel_Platform p) {
    return p == MEL_PLATFORM_WIN32 ? "llvm-ar" : "ar";
}

static bool mel_mkdirs(const char *path) {
    char buf[4096];
    size_t n = strlen(path);
    if (n >= sizeof buf) return false;
    memcpy(buf, path, n + 1);
    for (size_t i = 1; i < n; i++) {
        if (buf[i] == '/') {
            buf[i] = '\0';
            if (!mkdir_if_not_exists(buf)) return false;
            buf[i] = '/';
        }
    }
    return mkdir_if_not_exists(buf);
}

// =============================================================================
// Third-party build helpers (called from a third-party target's on_compile).
// Each builds into the target's resolved prefix; dependents pick up the
// prefix's include/ and lib/ automatically through dependency propagation.
// =============================================================================

static const char *ctx_abi(const Mel_Build_Context *ctx) { return ctx->cross ? ctx->cross->abi : NULL; }
static const char *ctx_tp_prefix(const Mel_Build_Context *ctx) {
    return tp_prefix_named(ctx->platform, ctx_abi(ctx), ctx->target->name);
}

// iOS simulator cross-compilation. We pin a single slice: arm64 against the
// iphonesimulator SDK, with the deployment target carried in the clang/cmake
// target triple. xcrun resolves the SDK path lazily and caches it.
#define MEL_IOS_SIM_TRIPLE "arm64-apple-ios13.0-simulator"

static const char *mel_ios_sdk_path(void) {
    static const char *cached = NULL;
    if (cached) return cached;
    FILE *p = popen("xcrun --sdk iphonesimulator --show-sdk-path 2>/dev/null", "r");
    if (!p) return NULL;
    char buf[1024];
    size_t n = fread(buf, 1, sizeof buf - 1, p);
    pclose(p);
    if (n == 0) return NULL;
    buf[n] = 0;
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' ')) buf[--n] = 0;
    cached = temp_strdup(buf);
    return cached;
}

static void mel_ios_clang_flags(Cmd *cmd) {
    cmd_append(cmd, "-target", MEL_IOS_SIM_TRIPLE);
    const char *sdk = mel_ios_sdk_path();
    if (sdk) cmd_append(cmd, "-isysroot", sdk);
}

bool mel_tp_single_tu(Mel_Build_Context *ctx, const char *src, const char *const *cflags,
                      size_t cflags_count, const char *const *headers, size_t headers_count) {
    const char *prefix = ctx_tp_prefix(ctx);
    const char *lib_dir = temp_sprintf("%s/lib", prefix);
    const char *inc_dir = temp_sprintf("%s/include", prefix);
    if (!mel_mkdirs(lib_dir)) return false;
    if (!mel_mkdirs(inc_dir)) return false;

    const char *lib = ctx->platform == MEL_PLATFORM_WIN32
        ? temp_sprintf("%s/%s.lib", lib_dir, ctx->target->name)
        : temp_sprintf("%s/lib%s.a", lib_dir, ctx->target->name);
    if (file_exists(lib) && needs_rebuild1(lib, src) == 0) goto headers;

    const char *obj = temp_sprintf("%s/%s.o", prefix, ctx->target->name);
    Cmd cmd = {0};
    cmd_append(&cmd, ctx->cross ? ctx->cross->cc : "clang", "-c", "-O2");
    if (ctx->platform != MEL_PLATFORM_WIN32) cmd_append(&cmd, "-fPIC");
    if (ctx->platform == MEL_PLATFORM_IOS) mel_ios_clang_flags(&cmd);
    for (size_t i = 0; i < cflags_count; i++) cmd_append(&cmd, cflags[i]);
    cmd_append(&cmd, src, "-o", obj);
    if (!cmd_run_sync_and_reset(&cmd)) return false;

    if (file_exists(lib)) delete_file(lib);
    Cmd ar = {0};
    const char *ar_tool = ctx->cross ? ctx->cross->ar : static_ar_tool(ctx->platform);
    cmd_append(&ar, ar_tool, "rcs", lib, obj);
    if (!cmd_run_sync_and_reset(&ar)) return false;

headers:
    for (size_t i = 0; i < headers_count; i++) {
        const char *base = strrchr(headers[i], '/');
        base = base ? base + 1 : headers[i];
        if (!copy_file(headers[i], temp_sprintf("%s/%s", inc_dir, base))) return false;
    }
    return true;
}

bool mel_tp_cmake(Mel_Build_Context *ctx, const char *src_rel,
                  const char *const *args, size_t args_count, const char *produced_lib) {
    const char *cwd = get_current_dir_temp();
    const char *prefix = ctx_tp_prefix(ctx);
    if (produced_lib && file_exists(temp_sprintf("%s/lib/%s", prefix, produced_lib))) return true;

    const char *abs_src = temp_sprintf("%s/%s", cwd, src_rel);
    const char *abs_prefix = temp_sprintf("%s/%s", cwd, prefix);
    const char *build_rel = temp_sprintf("%s/%s-build", prefix, ctx->target->name);
    if (!mel_mkdirs(build_rel)) return false;

    Cmd cmd = {0};
    cmd_append(&cmd, "cmake", "-S", abs_src, "-B", build_rel);
    cmd_append(&cmd, temp_sprintf("-DCMAKE_INSTALL_PREFIX=%s", abs_prefix));
    cmd_append(&cmd, "-DCMAKE_BUILD_TYPE=Release", "-DBUILD_SHARED_LIBS=OFF");
    if (ctx->cross) {
        cmd_append(&cmd, temp_sprintf("-DCMAKE_TOOLCHAIN_FILE=%s/build/cmake/android.toolchain.cmake", ctx->cross->ndk));
        cmd_append(&cmd, temp_sprintf("-DANDROID_ABI=%s", ctx->cross->abi));
        cmd_append(&cmd, temp_sprintf("-DANDROID_PLATFORM=android-%d", ctx->cross->api));
    } else if (ctx->platform == MEL_PLATFORM_IOS) {
        cmd_append(&cmd, "-DCMAKE_SYSTEM_NAME=iOS");
        cmd_append(&cmd, "-DCMAKE_OSX_SYSROOT=iphonesimulator");
        cmd_append(&cmd, "-DCMAKE_OSX_ARCHITECTURES=arm64");
        cmd_append(&cmd, "-DCMAKE_OSX_DEPLOYMENT_TARGET=13.0");
    }
    for (size_t i = 0; i < args_count; i++) cmd_append(&cmd, args[i]);
    if (!cmd_run_sync_and_reset(&cmd)) return false;

    Cmd build = {0};
    cmd_append(&build, "cmake", "--build", build_rel, "--config", "Release",
               "--parallel", temp_sprintf("%d", nob_nprocs()));
    if (!cmd_run_sync_and_reset(&build)) return false;

    Cmd install = {0};
    cmd_append(&install, "cmake", "--install", build_rel, "--config", "Release");
    return cmd_run_sync_and_reset(&install);
}

bool mel_tp_autotools(Mel_Build_Context *ctx, const char *src_rel, const char *extra_arg,
                      const char *produced_lib) {
    const char *cwd = get_current_dir_temp();
    const char *prefix = ctx_tp_prefix(ctx);
    if (produced_lib && file_exists(temp_sprintf("%s/lib/%s", prefix, produced_lib))) return true;
    const char *abs_prefix = temp_sprintf("%s/%s", cwd, prefix);
    const char *build_rel = temp_sprintf("%s/%s-build", prefix, ctx->target->name);
    const char *abs_build = temp_sprintf("%s/%s", cwd, build_rel);
    if (!mel_mkdirs(build_rel)) return false;

    const char *abs_src = temp_sprintf("%s/%s", cwd, src_rel);
    const char *configure = temp_sprintf("%s/configure", abs_src);

    // On Win32, autotools shells out to sh.exe for $(SHELL); make sure the
    // chosen sh's directory has no embedded space, and flip any backslashes
    // we pass into configure to forward slashes (autotools refuses backslashes
    // in srcdir/prefix as "unsafe"). We also avoid handing configure an
    // absolute path containing a drive letter: configure splits its aux-dir
    // candidate list on $PATH_SEPARATOR, which is `:` under MSYS sh, so
    // "D:/repo/..." gets shredded into "D" + "/repo/..." and the aux-file
    // probe fails. A relative srcdir from the build dir sidesteps that.
    bool win32_native = (ctx->platform == MEL_PLATFORM_WIN32) && (ctx->cross == NULL);
    const char *cfg_prefix = abs_prefix;
    const char *cfg_path = configure;
    if (win32_native) {
        mel_win32_ensure_no_space_sh_in_path();
        cfg_prefix = mel_to_autotools_path(abs_prefix);
        // build_rel sits N components below cwd; reach repo root with N "../".
        size_t ups = 1;
        for (const char *q = build_rel; *q; q++) if (*q == '/') ups++;
        String_Builder rel = {0};
        for (size_t i = 0; i < ups; i++) sb_append_cstr(&rel, "../");
        sb_append_cstr(&rel, src_rel);
        sb_append_cstr(&rel, "/configure");
        sb_append_null(&rel);
        cfg_path = temp_strdup(rel.items);
        free(rel.items);
    }

    if (!set_current_dir(abs_build)) return false;
    bool ok = true;

    Cmd cmd = {0};
    if (win32_native) cmd_append(&cmd, "sh");
    cmd_append(&cmd, cfg_path, temp_sprintf("--prefix=%s", cfg_prefix));
    cmd_append(&cmd, "--disable-shared", "--enable-static", "--with-pic", "--disable-maintainer-mode");
    if (ctx->cross) {
        cmd_append(&cmd, temp_sprintf("--host=%s", ctx->cross->triple));
        cmd_append(&cmd, temp_sprintf("CC=%s", ctx->cross->cc));
        cmd_append(&cmd, temp_sprintf("AR=%s", ctx->cross->ar));
        cmd_append(&cmd, temp_sprintf("RANLIB=%s", ctx->cross->ranlib));
    } else if (ctx->platform == MEL_PLATFORM_IOS) {
        const char *sdk = mel_ios_sdk_path();
        cmd_append(&cmd, "--host=aarch64-apple-darwin");
        cmd_append(&cmd, temp_sprintf("CC=clang -target %s -isysroot %s", MEL_IOS_SIM_TRIPLE, sdk ? sdk : ""));
    } else if (win32_native) {
        cmd_append(&cmd, "CC=clang", "AR=llvm-ar");
        // VS LLVM ships ld.lld / llvm-nm / llvm-strip but no GNU-named
        // aliases. Autotools/libtool probe for "ld" / "nm" by name and bail
        // otherwise.
        cmd_append(&cmd, "LD=ld.lld", "NM=llvm-nm", "STRIP=llvm-strip");
    }
    if (extra_arg) cmd_append(&cmd, extra_arg);
    if (!cmd_run_sync_and_reset(&cmd)) ok = false;

    if (ok) {
        Cmd make = {0};
        cmd_append(&make, "make", temp_sprintf("-j%d", nob_nprocs()));
        if (!cmd_run_sync_and_reset(&make)) ok = false;
    }
    if (ok) {
        Cmd install = {0};
        cmd_append(&install, "make", "install");
        if (!cmd_run_sync_and_reset(&install)) ok = false;
    }
    set_current_dir(cwd);
    return ok;
}

const char *mel_tp_prefix(Mel_Build_Context *ctx) {
    return ctx_tp_prefix(ctx);
}

// Absolute install prefix of another third-party target (same platform/ABI).
// On Win32 the cwd comes back with backslashes; autotools refuses backslash
// paths as srcdir/prefix and cmake doesn't care either way, so we always
// hand back a forward-slash path.
const char *mel_tp_dep_prefix(Mel_Build_Context *ctx, const char *target_name) {
    const char *cwd = get_current_dir_temp();
    const char *p = temp_sprintf("%s/%s", cwd, tp_prefix_named(ctx->platform, ctx_abi(ctx), target_name));
    return (ctx->platform == MEL_PLATFORM_WIN32) ? mel_to_autotools_path(p) : p;
}
