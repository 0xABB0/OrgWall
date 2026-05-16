#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#define NOB_NO_ECHO
#include "nob.h"

#define BUILD_DIR     "build"
#define OBJ_DIR       "build/obj"
#define MODULES_DIR   "modules"
#define APPS_DIR      "apps"
#if defined(_WIN32)
#define LIB_PATH      "build/melody.lib"
#else
#define LIB_PATH      "build/libmelody.a"
#endif
#define CCMDS_PATH    "compile_commands.json"
#define THIRD_PARTY_DIR    "third-party"
#define TP_BUILD_DIR       "build/third-party"

#if defined(_WIN32)
#define PLATFORM_WIN32 1
#else
#define PLATFORM_WIN32 0
#endif

static const char *base_cflags[] = {
    "-std=c23",
    "-g", "-O0",
    "-Wall", "-Wextra",
    "-Wno-unused-parameter",
    "-Wno-unused-function",
    "-Wno-missing-field-initializers",
#if !defined(_WIN32)
    "-fPIC",
#endif
};

typedef struct {
    const char *abi;
    const char *clang;
    const char *configure_host;
} Android_Abi;

static const Android_Abi android_abis[] = {
    { "arm64-v8a", "aarch64-linux-android23-clang", "aarch64-linux-android" },
    { "x86_64",    "x86_64-linux-android23-clang",  "x86_64-linux-android"  },
};

typedef struct {
    const char *name;
    const char *configure_host;
    const char *cc;
    const char *ar;
    const char *ranlib;
    const char *android_ndk;
    const char *android_abi;
    int android_api;
} Target;

typedef struct {
    File_Paths modules;
    File_Paths sources;
    File_Paths includes;
} Layout;

static const char *android_sdk_dir(const char *app_name);
static const char *android_ndk_dir(const char *sdk);
static const char *android_toolchain_bin(const char *ndk);

static const char *host_platform(void) {
#if defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#elif defined(_WIN32)
    return "win32";
#else
    return "unknown";
#endif
}

static const Target *target_host(void) {
#if PLATFORM_WIN32
    static Target host = {
        .name   = "host",
        .cc     = "clang",
        .ar     = "llvm-ar",
        .ranlib = NULL,
    };
#else
    static Target host = { .name = "host" };
#endif
    return &host;
}

static Target *target_android(const Android_Abi *abi, const char *toolchain_bin, const char *ndk) {
    Target *t = (Target*)temp_alloc(sizeof(Target));
    memset(t, 0, sizeof(*t));
    t->name = temp_sprintf("android-%s", abi->abi);
    t->configure_host = abi->configure_host;
    t->cc = temp_sprintf("%s/%s", toolchain_bin, abi->clang);
    t->ar = temp_sprintf("%s/llvm-ar", toolchain_bin);
    t->ranlib = temp_sprintf("%s/llvm-ranlib", toolchain_bin);
    t->android_ndk = ndk;
    t->android_abi = abi->abi;
    t->android_api = 23;
    return t;
}

static const char *target_prefix_rel(const Target *t) {
    return temp_sprintf("%s/%s", TP_BUILD_DIR, t->name);
}

#if PLATFORM_WIN32
// Autotools' configure scripts reject paths containing backslashes (unsafe srcdir).
// Keep Windows drive paths intact and only normalize slashes. MSYS tools accept
// "D:/foo/bar", and Windows-target compilers can still open those paths.
static const char *to_autotools_path(const char *p) {
    if (!p) return p;
    size_t len = strlen(p);
    char *out = (char*)temp_alloc(len + 1);
    for (size_t i = 0; i < len; i++) {
        out[i] = (p[i] == '\\') ? '/' : p[i];
    }
    out[len] = 0;
    return out;
}

// GNU make on Windows resolves $(SHELL) by searching PATH for sh.exe and
// substitutes the resolved path verbatim into recipes. Git for Windows ships
// sh at "C:\Program Files\Git\usr\bin\sh.exe" -- the embedded space then
// crashes libtool recipes like `$(SHELL) ../libtool ...`. Prepend the 8.3
// short form of the chosen sh's directory so make picks a path with no spaces.
static void win32_ensure_no_space_sh_in_path(void) {
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
    char *new_path = (char*)temp_alloc(need);
    snprintf(new_path, need, "%s;%s", short_dir, old_path);
    _putenv_s("PATH", new_path);
}
#else
static const char *to_autotools_path(const char *p) { return p; }
static void win32_ensure_no_space_sh_in_path(void) {}
#endif

static const char *target_prefix_abs(const Target *t) {
    return temp_sprintf("%s/%s", get_current_dir_temp(), target_prefix_rel(t));
}

static const char *target_include(const Target *t) {
    return temp_sprintf("%s/include", target_prefix_rel(t));
}

static const char *target_lib(const Target *t) {
    return temp_sprintf("%s/lib", target_prefix_rel(t));
}

static bool target_uses_msvc_lib_names(const Target *t) {
    return PLATFORM_WIN32 && t->configure_host == NULL;
}

static const char *autotools_static_lib(const Target *t, const char *name) {
    const char *prefix = target_prefix_rel(t);
    if (target_uses_msvc_lib_names(t)) {
        return temp_sprintf("%s/lib/%s.lib", prefix, name);
    }
    return temp_sprintf("%s/lib/lib%s.a", prefix, name);
}

static bool autotools_build(const Target *t, const char *src_rel, const char *name, const char *extra_arg) {
    win32_ensure_no_space_sh_in_path();

    const char *cwd = get_current_dir_temp();
    const char *abs_prefix = target_prefix_abs(t);
    const char *build_dir_rel = temp_sprintf("%s/%s-build", target_prefix_rel(t), name);
    const char *abs_build = temp_sprintf("%s/%s", cwd, build_dir_rel);
    const char *configure = temp_sprintf("../../../../%s/configure", src_rel);

    if (!mkdir_if_not_exists(build_dir_rel)) return false;
    if (!set_current_dir(abs_build)) return false;

    bool ok = true;

    Cmd cmd = {0};
#if PLATFORM_WIN32
    cmd_append(&cmd, "sh");
#endif
    cmd_append(&cmd, to_autotools_path(configure));
    cmd_append(&cmd, temp_sprintf("--prefix=%s", to_autotools_path(abs_prefix)));
    cmd_append(&cmd, "--disable-shared", "--enable-static", "--with-pic", "--disable-maintainer-mode");
    if (t->configure_host) cmd_append(&cmd, temp_sprintf("--host=%s", t->configure_host));
    if (extra_arg) cmd_append(&cmd, extra_arg);
    if (t->cc)     cmd_append(&cmd, temp_sprintf("CC=%s",     t->cc));
    if (t->ar)     cmd_append(&cmd, temp_sprintf("AR=%s",     t->ar));
    if (t->ranlib) cmd_append(&cmd, temp_sprintf("RANLIB=%s", t->ranlib));
#if PLATFORM_WIN32
    // VS LLVM ships ld.lld / llvm-nm / llvm-strip but no GNU-named aliases.
    // Autotools/libtool probe for "ld" / "nm" by name and bail otherwise.
    cmd_append(&cmd, "LD=ld.lld");
    cmd_append(&cmd, "NM=llvm-nm");
    cmd_append(&cmd, "STRIP=llvm-strip");
#endif
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

static bool single_tu_build(const Target *t, const char *src_rel, const char *name,
                             const char *const *headers, size_t headers_count,
                             const char *const *extra_cflags, size_t extra_cflags_count) {
    const char *cwd = get_current_dir_temp();
    const char *abs_src = temp_sprintf("%s/%s", cwd, src_rel);
    const char *abs_prefix = target_prefix_abs(t);
    const char *build_dir_rel = temp_sprintf("%s/%s-build", target_prefix_rel(t), name);
    if (!mkdir_if_not_exists(build_dir_rel)) return false;

    const char *obj = temp_sprintf("%s/%s.o", build_dir_rel, name);
    Cmd cmd = {0};
    cmd_append(&cmd, t->cc ? t->cc : "clang");
    cmd_append(&cmd, "-c", "-O2");
#if !PLATFORM_WIN32
    cmd_append(&cmd, "-fPIC");
#endif
    for (size_t i = 0; i < extra_cflags_count; i++) cmd_append(&cmd, extra_cflags[i]);
    cmd_append(&cmd, abs_src, "-o", obj);
    if (!cmd_run_sync_and_reset(&cmd)) return false;

    const char *lib_dir = temp_sprintf("%s/lib", abs_prefix);
    const char *inc_dir = temp_sprintf("%s/include", abs_prefix);
    if (!mkdir_if_not_exists(lib_dir)) return false;
    if (!mkdir_if_not_exists(inc_dir)) return false;

    const char *lib = temp_sprintf("%s/lib%s.a", lib_dir, name);
    if (file_exists(lib)) delete_file(lib);

    Cmd ar_cmd = {0};
    cmd_append(&ar_cmd, t->ar ? t->ar : "ar", "rcs", lib, obj);
    if (!cmd_run_sync_and_reset(&ar_cmd)) return false;

    if (t->ranlib) {
        Cmd ranlib_cmd = {0};
        cmd_append(&ranlib_cmd, t->ranlib, lib);
        if (!cmd_run_sync_and_reset(&ranlib_cmd)) return false;
    }

    for (size_t i = 0; i < headers_count; i++) {
        const char *hname = strrchr(headers[i], '/');
        hname = hname ? hname + 1 : headers[i];
        if (!copy_file(headers[i], temp_sprintf("%s/%s", inc_dir, hname))) return false;
    }
    return true;
}

static bool cmake_build(const Target *t, const char *src_rel, const char *name, const char *const *extra_args, size_t extra_args_count) {
    const char *cwd = get_current_dir_temp();
    const char *abs_src = temp_sprintf("%s/%s", cwd, src_rel);
    const char *abs_prefix = target_prefix_abs(t);
    const char *build_dir_rel = temp_sprintf("%s/%s-build", target_prefix_rel(t), name);

    if (!mkdir_if_not_exists(build_dir_rel)) return false;

    Cmd cmd = {0};
    cmd_append(&cmd, "cmake", "-S", abs_src, "-B", build_dir_rel);
    cmd_append(&cmd, temp_sprintf("-DCMAKE_INSTALL_PREFIX=%s", abs_prefix));
    cmd_append(&cmd, "-DCMAKE_BUILD_TYPE=Release");
    cmd_append(&cmd, "-DBUILD_SHARED_LIBS=OFF");

    if (t->android_ndk) {
        cmd_append(&cmd, temp_sprintf("-DCMAKE_TOOLCHAIN_FILE=%s/build/cmake/android.toolchain.cmake", t->android_ndk));
        cmd_append(&cmd, temp_sprintf("-DANDROID_ABI=%s", t->android_abi));
        cmd_append(&cmd, temp_sprintf("-DANDROID_PLATFORM=android-%d", t->android_api));
    } else if (t->cc) {
        cmd_append(&cmd, temp_sprintf("-DCMAKE_C_COMPILER=%s", t->cc));
    }

    for (size_t i = 0; i < extra_args_count; i++) cmd_append(&cmd, extra_args[i]);
    if (!cmd_run_sync_and_reset(&cmd)) return false;

    Cmd build = {0};
    cmd_append(&build, "cmake", "--build", build_dir_rel, "--config", "Release", "--parallel", temp_sprintf("%d", nob_nprocs()));
    if (!cmd_run_sync_and_reset(&build)) return false;

    Cmd install = {0};
    cmd_append(&install, "cmake", "--install", build_dir_rel, "--config", "Release");
    return cmd_run_sync_and_reset(&install);
}

static bool bootstrap_third_party(const Target *t) {
    const char *prefix = target_prefix_rel(t);
    const char *gmp_a    = autotools_static_lib(t, "gmp");
    const char *mpfr_a   = autotools_static_lib(t, "mpfr");
#if PLATFORM_WIN32
    const char *sdl_a    = temp_sprintf("%s/lib/SDL3-static.lib", prefix);
#else
    const char *sdl_a    = temp_sprintf("%s/lib/libSDL3.a",    prefix);
#endif
    const char *sqlite_a = temp_sprintf("%s/lib/libsqlite3.a", prefix);

    if (file_exists(gmp_a) && file_exists(mpfr_a) && file_exists(sdl_a) && file_exists(sqlite_a)) return true;

    nob_log(NOB_INFO, "bootstrapping third-party (gmp, mpfr, sdl3, sqlite3) for target '%s'", t->name);

    if (!mkdir_if_not_exists(BUILD_DIR))      return false;
    if (!mkdir_if_not_exists(TP_BUILD_DIR))   return false;
    if (!mkdir_if_not_exists(prefix))         return false;

    if (!file_exists(gmp_a)) {
        if (!autotools_build(t, "third-party/gmp", "gmp", NULL)) return false;
    }
    if (!file_exists(mpfr_a)) {
        const char *with_gmp = temp_sprintf("--with-gmp=%s", to_autotools_path(target_prefix_abs(t)));
        if (!autotools_build(t, "third-party/mpfr", "mpfr", with_gmp)) return false;
    }
    if (!file_exists(sdl_a)) {
        static const char *sdl_args[] = {
            "-DSDL_SHARED=OFF",
            "-DSDL_STATIC=ON",
            "-DSDL_TEST_LIBRARY=OFF",
            "-DSDL_TESTS=OFF",
            "-DSDL_EXAMPLES=OFF",
            "-DSDL_INSTALL_TESTS=OFF",
            "-DSDL_DISABLE_INSTALL_DOCS=ON",
            "-DSDL_AUDIO=OFF",
            "-DSDL_VIDEO=OFF",
            "-DSDL_GPU=OFF",
            "-DSDL_RENDER=OFF",
            "-DSDL_CAMERA=OFF",
            "-DSDL_JOYSTICK=OFF",
            "-DSDL_HAPTIC=OFF",
            "-DSDL_HIDAPI=OFF",
            "-DSDL_POWER=OFF",
            "-DSDL_SENSOR=OFF",
            "-DSDL_DIALOG=OFF",
            "-DSDL_OPENGL=OFF",
            "-DSDL_OPENGLES=OFF",
            "-DSDL_VULKAN=OFF",
        };
        if (!cmake_build(t, "third-party/sdl3", "sdl3", sdl_args, NOB_ARRAY_LEN(sdl_args))) return false;
    }
    if (!file_exists(sqlite_a)) {
        static const char *sqlite_headers[] = {
            "third-party/sqlite3/sqlite3.h",
            "third-party/sqlite3/sqlite3ext.h",
        };
        static const char *sqlite_cflags[] = {
            "-DSQLITE_OMIT_LOAD_EXTENSION",
            "-DSQLITE_THREADSAFE=1",
        };
        if (!single_tu_build(t, "third-party/sqlite3/sqlite3.c", "sqlite3",
                             sqlite_headers, NOB_ARRAY_LEN(sqlite_headers),
                             sqlite_cflags,  NOB_ARRAY_LEN(sqlite_cflags))) return false;
    }
    return true;
}

static bool ends_with(const char *s, const char *suffix) {
    size_t ls = strlen(s), lsuf = strlen(suffix);
    if (lsuf > ls) return false;
    return memcmp(s + ls - lsuf, suffix, lsuf) == 0;
}

static bool source_is_buildable(const char *name) {
    bool is_c = ends_with(name, ".c");
    bool is_m = ends_with(name, ".m");
    if (!is_c && !is_m) return false;
    if (ends_with(name, ".build.c")) return false;
    if (!PLATFORM_WIN32 && ends_with(name, ".win32.c")) return false;
    if (PLATFORM_WIN32 && (ends_with(name, ".posix.c") || ends_with(name, ".unix.c"))) return false;
    if (PLATFORM_WIN32 && is_m) return false;
    return true;
}

static bool source_is_objc(const char *name) {
    return ends_with(name, ".m");
}

static bool source_is_bridge(const char *name) {
    return ends_with(name, ".bridge.c") || ends_with(name, ".bridge.m");
}

static bool collect_dir_sources(const char *dir, File_Paths *out) {
    if (!file_exists(dir) || get_file_type(dir) != NOB_FILE_DIRECTORY) return true;
    File_Paths files = {0};
    if (!read_entire_dir(dir, &files)) return false;
    for (size_t i = 0; i < files.count; i++) {
        const char *n = files.items[i];
        if (!source_is_buildable(n)) continue;
        const char *full = temp_sprintf("%s/%s", dir, n);
        if (get_file_type(full) != NOB_FILE_REGULAR) continue;
        da_append(out, temp_strdup(full));
    }
    return true;
}

static bool discover(Layout *L) {
    File_Paths entries = {0};
    if (!read_entire_dir(MODULES_DIR, &entries)) return false;

    for (size_t i = 0; i < entries.count; i++) {
        const char *name = entries.items[i];
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;

        const char *mod_path = temp_sprintf("%s/%s", MODULES_DIR, name);
        if (get_file_type(mod_path) != NOB_FILE_DIRECTORY) continue;

        const char *include_path = temp_sprintf("%s/include", mod_path);
        if (get_file_type(include_path) == NOB_FILE_DIRECTORY) {
            da_append(&L->includes, temp_strdup(include_path));
        }

        da_append(&L->modules, temp_strdup(name));

        if (!collect_dir_sources(temp_sprintf("%s/src", mod_path), &L->sources)) return false;
    }
    return true;
}

static bool discover_for_platform(Layout *L, const char *platform) {
    if (!discover(L)) return false;
    for (size_t i = 0; i < L->modules.count; i++) {
        const char *mod = L->modules.items[i];
        if (!collect_dir_sources(temp_sprintf("%s/%s/src/%s", MODULES_DIR, mod, platform), &L->sources)) return false;
    }
    return true;
}

static const char *object_for(const char *src) {
    String_Builder sb = {0};
    sb_appendf(&sb, "%s/", OBJ_DIR);
    for (const char *p = src; *p; p++) {
        sb_append_buf(&sb, (*p == '/') ? "." : p, 1);
    }
    sb_append_cstr(&sb, ".o");
    sb_append_null(&sb);
    return temp_strdup(sb.items);
}

static void append_include_flags(Cmd *cmd, const Target *t, const Layout *L) {
    cmd_append(cmd, temp_sprintf("-I%s", target_include(t)));
    for (size_t i = 0; i < L->includes.count; i++) {
        cmd_append(cmd, temp_sprintf("-I%s", L->includes.items[i]));
    }
}

static void append_base_cflags(Cmd *cmd) {
    for (size_t i = 0; i < NOB_ARRAY_LEN(base_cflags); i++) {
        cmd_append(cmd, base_cflags[i]);
    }
}

static bool compile_all(const Target *t, const Layout *L, File_Paths *objects) {
    Procs procs = {0};
    size_t parallelism = nob_nprocs();
    if (parallelism < 1) parallelism = 1;

    for (size_t i = 0; i < L->sources.count; i++) {
        const char *src = L->sources.items[i];
        const char *obj = object_for(src);
        da_append(objects, obj);

        Cmd cmd = {0};
        cmd_append(&cmd, "clang");
        append_base_cflags(&cmd);
        if (source_is_objc(src)) cmd_append(&cmd, "-fobjc-arc");
        append_include_flags(&cmd, t, L);
        cmd_append(&cmd, "-c", src, "-o", obj);

        Proc p = cmd_run_async_and_reset(&cmd);
        if (!procs_append_with_flush(&procs, p, parallelism)) return false;
    }
    return procs_wait_and_reset(&procs);
}

static bool archive(const File_Paths *objects) {
    if (file_exists(LIB_PATH)) delete_file(LIB_PATH);
    Cmd cmd = {0};
    cmd_append(&cmd, PLATFORM_WIN32 ? "llvm-ar" : "ar", "rcs", LIB_PATH);
    for (size_t i = 0; i < objects->count; i++) {
        cmd_append(&cmd, objects->items[i]);
    }
    return cmd_run_sync_and_reset(&cmd);
}

static void json_append_escaped(String_Builder *sb, const char *s) {
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

static void append_compile_command(String_Builder *sb, size_t *entries, const char *cwd, const char *src, Cmd cmd) {
    String_Builder cmdline = {0};
    cmd_render(cmd, &cmdline);
    sb_append_null(&cmdline);

    if ((*entries)++ > 0) sb_append_cstr(sb, ",\n");
    sb_append_cstr(sb, "  {\n    \"directory\": ");
    json_append_escaped(sb, cwd);
    sb_append_cstr(sb, ",\n    \"command\": ");
    json_append_escaped(sb, cmdline.items);
    sb_append_cstr(sb, ",\n    \"file\": ");
    json_append_escaped(sb, src);
    sb_append_cstr(sb, "\n  }");

    free(cmdline.items);
    free(cmd.items);
}

static bool emit_compile_commands(const Target *t, const Layout *L) {
    const char *cwd = get_current_dir_temp();
    String_Builder sb = {0};
    sb_append_cstr(&sb, "[\n");

    size_t entries = 0;
    for (size_t i = 0; i < L->sources.count; i++) {
        const char *src = L->sources.items[i];
        const char *obj = object_for(src);

        Cmd cmd = {0};
        cmd_append(&cmd, "clang");
        append_base_cflags(&cmd);
        append_include_flags(&cmd, t, L);
        cmd_append(&cmd, "-c", src, "-o", obj);
        append_compile_command(&sb, &entries, cwd, src, cmd);
    }

    sb_append_cstr(&sb, "\n]\n");
    return write_entire_file(CCMDS_PATH, sb.items, sb.count);
}

static bool build_library(void) {
    if (!bootstrap_third_party(target_host())) return false;

    if (!mkdir_if_not_exists(BUILD_DIR)) return false;
    if (!mkdir_if_not_exists(OBJ_DIR))   return false;

    Layout L = {0};
    if (!discover_for_platform(&L, host_platform())) {
        nob_log(NOB_ERROR, "failed to discover modules under %s/", MODULES_DIR);
        return false;
    }

    Layout archived = L;
    archived.sources = (File_Paths){0};
    for (size_t i = 0; i < L.sources.count; i++) {
        const char *s = L.sources.items[i];
        if (!source_is_bridge(s)) da_append(&archived.sources, s);
    }

    nob_log(NOB_INFO, "discovered %zu modules, %zu source files",
            archived.modules.count, archived.sources.count);

    if (!emit_compile_commands(target_host(), &L)) {
        nob_log(NOB_ERROR, "failed to write %s", CCMDS_PATH);
        return false;
    }
    nob_log(NOB_INFO, "wrote %s", CCMDS_PATH);

    File_Paths objects = {0};
    bool ok = compile_all(target_host(), &archived, &objects);
    if (!ok) {
        nob_log(NOB_WARNING, "one or more translation units failed to compile");
    }

    File_Paths existing = {0};
    for (size_t i = 0; i < objects.count; i++) {
        if (file_exists(objects.items[i])) {
            da_append(&existing, objects.items[i]);
        }
    }

    if (existing.count == 0) {
        nob_log(NOB_ERROR, "no object files produced; skipping archive");
        return ok;
    }

    if (!archive(&existing)) {
        nob_log(NOB_ERROR, "failed to archive %s", LIB_PATH);
        return false;
    }
    nob_log(NOB_INFO, "wrote %s (%zu/%zu objects)",
            LIB_PATH, existing.count, objects.count);
    return ok;
}

static const char *android_sdk_dir(const char *app_name) {
    const char *sdk = getenv("ANDROID_HOME");
    if (sdk && sdk[0]) return sdk;

    sdk = getenv("ANDROID_SDK_ROOT");
    if (sdk && sdk[0]) return sdk;

    const char *local = temp_sprintf("%s/%s/android/local.properties", APPS_DIR, app_name);
    FILE *f = fopen(local, "r");
    if (!f) return NULL;

    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        const char *prefix = "sdk.dir=";
        size_t prefix_len = strlen(prefix);
        if (strncmp(line, prefix, prefix_len) != 0) continue;

        char *value = line + prefix_len;
        size_t len = strlen(value);
        while (len > 0 && (value[len - 1] == '\n' || value[len - 1] == '\r')) {
            value[--len] = 0;
        }
        fclose(f);
        return temp_strdup(value);
    }

    fclose(f);
    return NULL;
}

static const char *android_ndk_dir(const char *sdk) {
    const char *ndk_root = temp_sprintf("%s/ndk", sdk);
    File_Paths entries = {0};
    if (!read_entire_dir(ndk_root, &entries)) return NULL;

    const char *best = NULL;
    for (size_t i = 0; i < entries.count; i++) {
        const char *name = entries.items[i];
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        const char *path = temp_sprintf("%s/%s", ndk_root, name);
        if (get_file_type(path) != NOB_FILE_DIRECTORY) continue;
        if (best == NULL || strcmp(name, best) > 0) best = temp_strdup(name);
    }

    if (best == NULL) return NULL;
    return temp_sprintf("%s/%s", ndk_root, best);
}

static const char *android_toolchain_bin(const char *ndk) {
    static const char *hosts[] = {
        "darwin-x86_64",
        "darwin-arm64",
        "linux-x86_64",
        "windows-x86_64",
    };

    for (size_t i = 0; i < NOB_ARRAY_LEN(hosts); i++) {
        const char *path = temp_sprintf("%s/toolchains/llvm/prebuilt/%s/bin", ndk, hosts[i]);
        if (get_file_type(path) == NOB_FILE_DIRECTORY) return path;
    }

    return NULL;
}

static const char *android_gradle_dir(const char *app_name) {
    return temp_sprintf("%s/%s/android", APPS_DIR, app_name);
}

static const char *android_jnilibs_dir(const char *app_name) {
    return temp_sprintf("%s/app/src/main/jniLibs", android_gradle_dir(app_name));
}

static const char *android_apk_path(const char *app_name) {
    return temp_sprintf("%s/app/build/outputs/apk/debug/app-debug.apk", android_gradle_dir(app_name));
}

static const char *android_application_id(const char *app_name) {
    const char *path = temp_sprintf("%s/app/build.gradle.kts", android_gradle_dir(app_name));
    String_Builder sb = {0};
    if (!read_entire_file(path, &sb)) return NULL;

    const char *needle = "applicationId";
    size_t nlen = strlen(needle);
    const char *p = sb.items;
    const char *end = sb.items + sb.count;
    const char *result = NULL;

    while (p + nlen < end) {
        if (memcmp(p, needle, nlen) == 0) {
            const char *q = p + nlen;
            while (q < end && (*q == ' ' || *q == '\t' || *q == '=')) q++;
            if (q < end && *q == '"') {
                q++;
                const char *s = q;
                while (q < end && *q != '"') q++;
                if (q < end) {
                    size_t len = (size_t)(q - s);
                    char *out = (char*)temp_alloc(len + 1);
                    memcpy(out, s, len);
                    out[len] = 0;
                    result = out;
                    break;
                }
            }
        }
        p++;
    }

    free(sb.items);
    return result;
}

static const char *adb_path(const char *app_name) {
    const char *sdk = android_sdk_dir(app_name);
    if (sdk == NULL) return "adb";
    return temp_sprintf("%s/platform-tools/adb", sdk);
}

static const char *target_obj_dir(const Target *t) {
    return temp_sprintf("%s/obj/%s", BUILD_DIR, t->name);
}

static const char *target_lib_path(const Target *t) {
    return temp_sprintf("%s/%s/libmelody.a", BUILD_DIR, t->name);
}

static const char *target_object_for(const Target *t, const char *src) {
    String_Builder sb = {0};
    sb_appendf(&sb, "%s/", target_obj_dir(t));
    for (const char *p = src; *p; p++) {
        sb_append_buf(&sb, (*p == '/') ? "." : p, 1);
    }
    sb_append_cstr(&sb, ".o");
    sb_append_null(&sb);
    return temp_strdup(sb.items);
}

static bool target_compile_one(const Target *t, const Layout *L, const char *src, const char *obj, const char *extra_define) {
    Cmd cmd = {0};
    cmd_append(&cmd, t->cc ? t->cc : "clang");
    append_base_cflags(&cmd);
    if (source_is_objc(src)) cmd_append(&cmd, "-fobjc-arc");
    if (extra_define) cmd_append(&cmd, extra_define);
    cmd_append(&cmd, temp_sprintf("-I%s", target_include(t)));
    for (size_t i = 0; i < L->includes.count; i++) {
        cmd_append(&cmd, temp_sprintf("-I%s", L->includes.items[i]));
    }
    cmd_append(&cmd, "-c", src, "-o", obj);
    return cmd_run_sync_and_reset(&cmd);
}

static bool target_archive(const Target *t, const File_Paths *objects) {
    const char *lib = target_lib_path(t);
    if (file_exists(lib)) delete_file(lib);

    Cmd cmd = {0};
    cmd_append(&cmd, t->ar ? t->ar : "ar", "rcs", lib);
    for (size_t i = 0; i < objects->count; i++) cmd_append(&cmd, objects->items[i]);
    if (!cmd_run_sync_and_reset(&cmd)) return false;

    if (t->ranlib) {
        Cmd r = {0};
        cmd_append(&r, t->ranlib, lib);
        if (!cmd_run_sync_and_reset(&r)) return false;
    }
    return true;
}

static bool android_build_so(const char *app_name) {
    const char *sdk = android_sdk_dir(app_name);
    if (sdk == NULL) {
        nob_log(NOB_ERROR, "Android SDK not found. Set ANDROID_HOME/ANDROID_SDK_ROOT or %s/%s/android/local.properties", APPS_DIR, app_name);
        return false;
    }
    const char *ndk = android_ndk_dir(sdk);
    if (ndk == NULL) {
        nob_log(NOB_ERROR, "Android NDK not found under %s/ndk", sdk);
        return false;
    }
    const char *toolchain_bin = android_toolchain_bin(ndk);
    if (toolchain_bin == NULL) {
        nob_log(NOB_ERROR, "Android NDK LLVM toolchain not found under %s/toolchains/llvm/prebuilt", ndk);
        return false;
    }

    nob_log(NOB_INFO, "using Android SDK: %s", sdk);
    nob_log(NOB_INFO, "using Android NDK: %s", ndk);

    Layout L = {0};
    if (!discover_for_platform(&L, "android")) return false;

    File_Paths lib_sources = {0};
    File_Paths bridge_sources = {0};
    for (size_t i = 0; i < L.sources.count; i++) {
        const char *s = L.sources.items[i];
        if (ends_with(s, ".bridge.c")) da_append(&bridge_sources, s);
        else                            da_append(&lib_sources, s);
    }

    File_Paths app_sources = {0};
    if (!collect_dir_sources(temp_sprintf("%s/%s/src", APPS_DIR, app_name), &app_sources)) return false;
    if (app_sources.count == 0) {
        nob_log(NOB_ERROR, "no app sources found under %s/%s/src", APPS_DIR, app_name);
        return false;
    }

    const char *jnilibs = android_jnilibs_dir(app_name);
    if (!mkdir_if_not_exists(jnilibs)) return false;

    for (size_t i = 0; i < NOB_ARRAY_LEN(android_abis); i++) {
        const Android_Abi *abi = &android_abis[i];
        Target *t = target_android(abi, toolchain_bin, ndk);

        if (!bootstrap_third_party(t)) return false;

        if (!mkdir_if_not_exists(temp_sprintf("%s/%s", BUILD_DIR, t->name))) return false;
        if (!mkdir_if_not_exists(target_obj_dir(t))) return false;

        File_Paths lib_objects = {0};
        bool all_ok = true;
        for (size_t j = 0; j < lib_sources.count; j++) {
            const char *src = lib_sources.items[j];
            const char *obj = target_object_for(t, src);
            if (!target_compile_one(t, &L, src, obj, "-DANDROID")) { all_ok = false; continue; }
            da_append(&lib_objects, obj);
        }
        if (!all_ok) nob_log(NOB_WARNING, "one or more translation units failed to compile for %s", t->name);
        if (lib_objects.count == 0) {
            nob_log(NOB_ERROR, "no objects produced for %s; aborting", t->name);
            return false;
        }
        if (!target_archive(t, &lib_objects)) return false;
        nob_log(NOB_INFO, "wrote %s (%zu/%zu objects)", target_lib_path(t), lib_objects.count, lib_sources.count);

        File_Paths link_objects = {0};
        for (size_t j = 0; j < bridge_sources.count; j++) {
            const char *src = bridge_sources.items[j];
            const char *obj = target_object_for(t, src);
            if (!target_compile_one(t, &L, src, obj, "-DANDROID")) return false;
            da_append(&link_objects, obj);
        }
        for (size_t j = 0; j < app_sources.count; j++) {
            const char *src = app_sources.items[j];
            const char *obj = target_object_for(t, src);
            if (!target_compile_one(t, &L, src, obj, "-DANDROID")) return false;
            da_append(&link_objects, obj);
        }

        const char *abi_dir = temp_sprintf("%s/%s", jnilibs, abi->abi);
        if (!mkdir_if_not_exists(abi_dir)) return false;

        Cmd cmd = {0};
        cmd_append(&cmd, t->cc);
        cmd_append(&cmd, "-shared", "-fPIC");
        cmd_append(&cmd, "-Wl,--gc-sections", "-Wl,--as-needed");
        for (size_t j = 0; j < link_objects.count; j++) cmd_append(&cmd, link_objects.items[j]);
        cmd_append(&cmd, temp_sprintf("-L%s/%s", BUILD_DIR, t->name));
        cmd_append(&cmd, temp_sprintf("-L%s", target_lib(t)));
        cmd_append(&cmd, "-lmelody", "-lmpfr", "-lgmp", "-lSDL3", "-lsqlite3", "-llog");
        cmd_append(&cmd, "-o", temp_sprintf("%s/libmelody.so", abi_dir));
        if (!cmd_run_sync_and_reset(&cmd)) return false;
    }

    return true;
}

static bool android_assemble_apk(const char *app_name) {
    Cmd cmd = {0};
    cmd_append(&cmd, "gradle", "-p", android_gradle_dir(app_name), ":app:assembleDebug");
    return cmd_run_sync_and_reset(&cmd);
}

static bool android_install(const char *app_name) {
    const char *apk = android_apk_path(app_name);
    if (!file_exists(apk)) {
        nob_log(NOB_ERROR, "APK not found at %s", apk);
        return false;
    }
    Cmd cmd = {0};
    cmd_append(&cmd, adb_path(app_name), "install", "-r", apk);
    return cmd_run_sync_and_reset(&cmd);
}

static bool android_launch(const char *app_name) {
    const char *pkg = android_application_id(app_name);
    if (pkg == NULL) {
        nob_log(NOB_ERROR, "could not read applicationId from %s/app/build.gradle.kts", android_gradle_dir(app_name));
        return false;
    }
    Cmd cmd = {0};
    cmd_append(&cmd, adb_path(app_name), "shell", "monkey", "-p", pkg, "-c", "android.intent.category.LAUNCHER", "1");
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

static bool android_build_app(const char *app_name) {
    if (!android_build_so(app_name)) return false;
    if (!android_assemble_apk(app_name)) return false;
    return true;
}

static int usage(void) {
    nob_log(NOB_ERROR,
        "usage:\n"
        "  ./nob                                  build library archive\n"
        "  ./nob build app <name> [<platform>]    build app (default platform: host)\n"
        "  ./nob run   app <name> [<platform>]    build + install + launch\n"
        "  ./nob debug app <name> [<platform>]    run + stream Melody logcat");
    return 1;
}

static const char *macos_binary_path(const char *app_name) {
    return temp_sprintf("%s/macos/%s/%s", BUILD_DIR, app_name, app_name);
}

static bool macos_build_app(const char *app_name) {
    if (!build_library()) return false;

    Layout L = {0};
    if (!discover_for_platform(&L, "macos")) return false;

    File_Paths bridge_sources = {0};
    for (size_t i = 0; i < L.sources.count; i++) {
        const char *s = L.sources.items[i];
        if (source_is_bridge(s)) da_append(&bridge_sources, s);
    }

    File_Paths app_sources = {0};
    if (!collect_dir_sources(temp_sprintf("%s/%s/src", APPS_DIR, app_name), &app_sources)) return false;
    if (app_sources.count == 0) {
        nob_log(NOB_ERROR, "no app sources found under %s/%s/src", APPS_DIR, app_name);
        return false;
    }

    const char *out_dir = temp_sprintf("%s/macos/%s", BUILD_DIR, app_name);
    if (!mkdir_if_not_exists(temp_sprintf("%s/macos", BUILD_DIR))) return false;
    if (!mkdir_if_not_exists(out_dir)) return false;

    File_Paths link_objs = {0};
    for (size_t i = 0; i < bridge_sources.count; i++) {
        const char *src = bridge_sources.items[i];
        const char *obj = object_for(src);
        if (!target_compile_one(target_host(), &L, src, obj, NULL)) return false;
        da_append(&link_objs, obj);
    }
    for (size_t i = 0; i < app_sources.count; i++) {
        const char *src = app_sources.items[i];
        const char *obj = object_for(src);
        if (!target_compile_one(target_host(), &L, src, obj, NULL)) return false;
        da_append(&link_objs, obj);
    }

    const char *bin = macos_binary_path(app_name);

    Cmd cmd = {0};
    cmd_append(&cmd, "clang");
    for (size_t i = 0; i < link_objs.count; i++) cmd_append(&cmd, link_objs.items[i]);
    cmd_append(&cmd, "-L" BUILD_DIR);
    cmd_append(&cmd, temp_sprintf("-L%s", target_lib(target_host())));
    cmd_append(&cmd, "-lmelody", "-lmpfr", "-lgmp", "-lSDL3", "-lsqlite3");
    cmd_append(&cmd, "-framework", "Cocoa");
    cmd_append(&cmd, "-framework", "CoreMIDI");
    cmd_append(&cmd, "-framework", "CoreFoundation");
    cmd_append(&cmd, "-o", bin);
    if (!cmd_run_sync_and_reset(&cmd)) return false;

    nob_log(NOB_INFO, "wrote %s", bin);
    return true;
}

static bool macos_launch(const char *app_name) {
    Cmd cmd = {0};
    cmd_append(&cmd, macos_binary_path(app_name));
    return cmd_run_sync_and_reset(&cmd);
}

static const char *win32_binary_path(const char *app_name) {
    return temp_sprintf("%s/win32/%s/%s.exe", BUILD_DIR, app_name, app_name);
}

static bool win32_build_app(const char *app_name) {
    if (!build_library()) return false;

    Layout L = {0};
    if (!discover_for_platform(&L, "win32")) return false;

    File_Paths bridge_sources = {0};
    for (size_t i = 0; i < L.sources.count; i++) {
        const char *s = L.sources.items[i];
        if (source_is_bridge(s)) da_append(&bridge_sources, s);
    }

    File_Paths app_sources = {0};
    if (!collect_dir_sources(temp_sprintf("%s/%s/src", APPS_DIR, app_name), &app_sources)) return false;
    if (app_sources.count == 0) {
        nob_log(NOB_ERROR, "no app sources found under %s/%s/src", APPS_DIR, app_name);
        return false;
    }

    const char *out_dir = temp_sprintf("%s/win32/%s", BUILD_DIR, app_name);
    if (!mkdir_if_not_exists(temp_sprintf("%s/win32", BUILD_DIR))) return false;
    if (!mkdir_if_not_exists(out_dir)) return false;

    File_Paths link_objs = {0};
    for (size_t i = 0; i < bridge_sources.count; i++) {
        const char *src = bridge_sources.items[i];
        const char *obj = object_for(src);
        if (!target_compile_one(target_host(), &L, src, obj, NULL)) return false;
        da_append(&link_objs, obj);
    }
    for (size_t i = 0; i < app_sources.count; i++) {
        const char *src = app_sources.items[i];
        const char *obj = object_for(src);
        if (!target_compile_one(target_host(), &L, src, obj, NULL)) return false;
        da_append(&link_objs, obj);
    }

    File_Paths res_files = {0};
    const char *win32_dir = temp_sprintf("%s/%s/win32", APPS_DIR, app_name);
    if (file_exists(win32_dir) && get_file_type(win32_dir) == NOB_FILE_DIRECTORY) {
        File_Paths win32_entries = {0};
        if (!read_entire_dir(win32_dir, &win32_entries)) return false;
        for (size_t i = 0; i < win32_entries.count; i++) {
            const char *n = win32_entries.items[i];
            if (!ends_with(n, ".rc")) continue;
            const char *rc_path = temp_sprintf("%s/%s", win32_dir, n);
            const char *res_path = temp_sprintf("%s/%s.res", out_dir, n);
            Cmd rc = {0};
            cmd_append(&rc, "llvm-rc", "/nologo");
            cmd_append(&rc, temp_sprintf("/I%s", win32_dir));
            cmd_append(&rc, "/fo", res_path, rc_path);
            if (!cmd_run_sync_and_reset(&rc)) return false;
            da_append(&res_files, temp_strdup(res_path));
        }
    }

    const char *bin = win32_binary_path(app_name);

    Cmd cmd = {0};
    cmd_append(&cmd, "clang");
    for (size_t i = 0; i < link_objs.count; i++) cmd_append(&cmd, link_objs.items[i]);
    for (size_t i = 0; i < res_files.count; i++) cmd_append(&cmd, res_files.items[i]);
    cmd_append(&cmd, "-L" BUILD_DIR);
    cmd_append(&cmd, "-lmelody");
    cmd_append(&cmd, "-luser32", "-lgdi32", "-lcomctl32", "-lcomdlg32", "-lkernel32", "-lwinmm");
    cmd_append(&cmd, "-Wl,/SUBSYSTEM:WINDOWS", "-Wl,/ENTRY:WinMainCRTStartup", "-Wl,/MANIFEST:NO");
    cmd_append(&cmd, "-o", bin);
    if (!cmd_run_sync_and_reset(&cmd)) return false;

    nob_log(NOB_INFO, "wrote %s", bin);
    return true;
}

static bool win32_launch(const char *app_name) {
    Cmd cmd = {0};
    cmd_append(&cmd, win32_binary_path(app_name));
    return cmd_run_sync_and_reset(&cmd);
}

static const char *linux_binary_path(const char *app_name) {
    return temp_sprintf("%s/linux/%s/%s", BUILD_DIR, app_name, app_name);
}

static bool linux_build_app(const char *app_name) {
    if (!build_library()) return false;

    Layout L = {0};
    if (!discover_for_platform(&L, "linux")) return false;

    File_Paths bridge_sources = {0};
    for (size_t i = 0; i < L.sources.count; i++) {
        const char *s = L.sources.items[i];
        if (source_is_bridge(s)) da_append(&bridge_sources, s);
    }

    File_Paths app_sources = {0};
    if (!collect_dir_sources(temp_sprintf("%s/%s/src", APPS_DIR, app_name), &app_sources)) return false;
    if (app_sources.count == 0) {
        nob_log(NOB_ERROR, "no app sources found under %s/%s/src", APPS_DIR, app_name);
        return false;
    }

    const char *out_dir = temp_sprintf("%s/linux/%s", BUILD_DIR, app_name);
    if (!mkdir_if_not_exists(temp_sprintf("%s/linux", BUILD_DIR))) return false;
    if (!mkdir_if_not_exists(out_dir)) return false;

    File_Paths link_objs = {0};
    for (size_t i = 0; i < bridge_sources.count; i++) {
        const char *src = bridge_sources.items[i];
        const char *obj = object_for(src);
        if (!target_compile_one(target_host(), &L, src, obj, NULL)) return false;
        da_append(&link_objs, obj);
    }
    for (size_t i = 0; i < app_sources.count; i++) {
        const char *src = app_sources.items[i];
        const char *obj = object_for(src);
        if (!target_compile_one(target_host(), &L, src, obj, NULL)) return false;
        da_append(&link_objs, obj);
    }

    const char *bin = linux_binary_path(app_name);

    Cmd cmd = {0};
    cmd_append(&cmd, "clang");
    for (size_t i = 0; i < link_objs.count; i++) cmd_append(&cmd, link_objs.items[i]);
    cmd_append(&cmd, "-L" BUILD_DIR);
    cmd_append(&cmd, temp_sprintf("-L%s", target_lib(target_host())));
    cmd_append(&cmd, "-lmelody", "-lmpfr", "-lgmp", "-lSDL3", "-lsqlite3");
    cmd_append(&cmd, "-lasound", "-lpthread", "-lm");
    cmd_append(&cmd, "-o", bin);
    if (!cmd_run_sync_and_reset(&cmd)) return false;

    nob_log(NOB_INFO, "wrote %s", bin);
    return true;
}

static bool linux_launch(const char *app_name) {
    Cmd cmd = {0};
    cmd_append(&cmd, linux_binary_path(app_name));
    return cmd_run_sync_and_reset(&cmd);
}

static int run_app_command(const char *cmd, int argc, char **argv) {
    if (argc < 2 || strcmp(argv[0], "app") != 0) return usage();
    const char *name = argv[1];
    const char *platform = argc >= 3 ? argv[2] : host_platform();

    const char *app_dir = temp_sprintf("%s/%s", APPS_DIR, name);
    if (get_file_type(app_dir) != NOB_FILE_DIRECTORY) {
        nob_log(NOB_ERROR, "app '%s' not found at %s", name, app_dir);
        return 1;
    }

    if (strcmp(platform, "android") == 0) {
        if (!android_build_app(name)) return 1;
        if (strcmp(cmd, "build") == 0) return 0;
        if (!android_install(name)) return 1;
        if (!android_launch(name)) return 1;
        if (strcmp(cmd, "run") == 0) return 0;
        return android_logcat(name) ? 0 : 1;
    }

    if (strcmp(platform, "macos") == 0) {
        if (!macos_build_app(name)) return 1;
        if (strcmp(cmd, "build") == 0) return 0;
        return macos_launch(name) ? 0 : 1;
    }

    if (strcmp(platform, "win32") == 0) {
        if (!win32_build_app(name)) return 1;
        if (strcmp(cmd, "build") == 0) return 0;
        return win32_launch(name) ? 0 : 1;
    }

    if (strcmp(platform, "linux") == 0) {
        if (!linux_build_app(name)) return 1;
        if (strcmp(cmd, "build") == 0) return 0;
        return linux_launch(name) ? 0 : 1;
    }

    nob_log(NOB_ERROR, "platform '%s' is not implemented yet", platform);
    return 1;
}

int main(int argc, char **argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);

    if (argc >= 2) {
        const char *cmd = argv[1];
        if (strcmp(cmd, "build") == 0 || strcmp(cmd, "run") == 0 || strcmp(cmd, "debug") == 0) {
            return run_app_command(cmd, argc - 2, argv + 2);
        }
        nob_log(NOB_ERROR, "unknown command: %s", cmd);
        return usage();
    }

    return build_library() ? 0 : 1;
}
