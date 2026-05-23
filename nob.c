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

// --- Shared types (used by included sub-files) ---

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

typedef struct {
    const char *abi;
    const char *clang;
    const char *configure_host;
} Android_Abi;

static const Android_Abi android_abis[] = {
    { "arm64-v8a", "aarch64-linux-android23-clang", "aarch64-linux-android" },
    { "x86_64",    "x86_64-linux-android23-clang",  "x86_64-linux-android"  },
};

// --- Subsystems (included for single-compilation-unit build) ---
#include "nob_third_party.c"

// --- Platform detection ---

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

// --- Source file helpers ---

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

static bool source_is_third_party(const char *name) {
    return strstr(name, "third-party/") != NULL;
}

// --- Compilation helpers ---

static void append_include_flags(Cmd *cmd, const Target *t, const Layout *L) {
    cmd_append(cmd, temp_sprintf("-I%s", target_include(t)));
    for (size_t i = 0; i < L->includes.count; i++) {
        const char *inc = L->includes.items[i];
        if (strstr(inc, "third-party/") == inc || strstr(inc, "third-party\\") == inc) {
            cmd_append(cmd, "-isystem", inc);
        } else {
            cmd_append(cmd, temp_sprintf("-I%s", inc));
        }
    }
}

static void append_base_cflags(Cmd *cmd) {
    for (size_t i = 0; i < NOB_ARRAY_LEN(base_cflags); i++) {
        cmd_append(cmd, base_cflags[i]);
    }
}

// --- Target-based compilation (used by cross-compilation and app builds) ---

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

static bool target_compile_one(const Target *t, const Layout *L, const char *src,
                               const char *obj, const char *extra_define) {
    Cmd cmd = {0};
    cmd_append(&cmd, t->cc ? t->cc : "clang");
    append_base_cflags(&cmd);
    if (source_is_objc(src)) cmd_append(&cmd, "-fobjc-arc");
    if (extra_define) cmd_append(&cmd, extra_define);
    if (source_is_third_party(src)) cmd_append(&cmd, "-Wno-deprecated-declarations");
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

// --- Module discovery ---

static bool is_platform_subdir(const char *name) {
    static const char *const platform_names[] = {
        "macos", "ios", "linux", "android", "win32", "windows", "web", "emscripten",
        "apple", "posix", "win", "asm",
    };
    for (size_t i = 0; i < NOB_ARRAY_LEN(platform_names); i++) {
        if (strcmp(name, platform_names[i]) == 0) return true;
    }
    return false;
}

static bool collect_dir_sources(const char *dir, File_Paths *out) {
    if (!file_exists(dir) || get_file_type(dir) != NOB_FILE_DIRECTORY) return true;
    File_Paths files = {0};
    if (!read_entire_dir(dir, &files)) return false;
    for (size_t i = 0; i < files.count; i++) {
        const char *n = files.items[i];
        if (strcmp(n, ".") == 0 || strcmp(n, "..") == 0) continue;
        const char *full = temp_sprintf("%s/%s", dir, n);
        Nob_File_Type ft = get_file_type(full);
        if (ft == NOB_FILE_DIRECTORY) {
            if (is_platform_subdir(n)) continue;
            if (!collect_dir_sources(full, out)) return false;
        } else if (ft == NOB_FILE_REGULAR) {
            if (!source_is_buildable(n)) continue;
            da_append(out, temp_strdup(full));
        }
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

static const char *const macos_chain[]      = { "macos",      "apple", "posix", NULL };
static const char *const ios_chain[]        = { "ios",        "apple", "posix", NULL };
static const char *const linux_chain[]      = { "linux",      "posix", NULL };
static const char *const android_chain[]    = { "android",    "posix", NULL };
static const char *const win32_chain[]      = { "win32",      "win",   NULL };
static const char *const web_chain[]        = { "web",        "posix", NULL };
static const char *const emscripten_chain[] = { "emscripten", "web",   "posix", NULL };

typedef struct { const char *platform; const char *const *chain; } Platform_Chain;

static const Platform_Chain platform_chains[] = {
    { "macos",      macos_chain      },
    { "ios",        ios_chain        },
    { "linux",      linux_chain      },
    { "android",    android_chain    },
    { "win32",      win32_chain      },
    { "web",        web_chain        },
    { "emscripten", emscripten_chain },
};

static const char *const *find_platform_chain(const char *platform) {
    for (size_t i = 0; i < NOB_ARRAY_LEN(platform_chains); i++) {
        if (strcmp(platform_chains[i].platform, platform) == 0) return platform_chains[i].chain;
    }
    return NULL;
}

static bool collect_dir_sources_filtered(const char *dir, File_Paths *out, File_Paths *seen_names) {
    if (!file_exists(dir) || get_file_type(dir) != NOB_FILE_DIRECTORY) return true;
    File_Paths files = {0};
    if (!read_entire_dir(dir, &files)) return false;
    for (size_t i = 0; i < files.count; i++) {
        const char *n = files.items[i];
        if (strcmp(n, ".") == 0 || strcmp(n, "..") == 0) continue;
        const char *full = temp_sprintf("%s/%s", dir, n);
        Nob_File_Type ft = get_file_type(full);
        if (ft == NOB_FILE_DIRECTORY) {
            if (!collect_dir_sources_filtered(full, out, seen_names)) return false;
            continue;
        }
        if (ft != NOB_FILE_REGULAR) continue;
        if (!source_is_buildable(n)) continue;

        bool shadowed = false;
        for (size_t k = 0; k < seen_names->count; k++) {
            if (strcmp(seen_names->items[k], n) == 0) { shadowed = true; break; }
        }
        if (shadowed) continue;

        da_append(seen_names, temp_strdup(n));
        da_append(out, temp_strdup(full));
    }
    return true;
}

static bool discover_for_platform(Layout *L, const char *platform) {
    if (!discover(L)) return false;

    const char *const *chain = find_platform_chain(platform);
    if (chain == NULL) {
        for (size_t i = 0; i < L->modules.count; i++) {
            const char *mod = L->modules.items[i];
            if (!collect_dir_sources(temp_sprintf("%s/%s/src/%s", MODULES_DIR, mod, platform), &L->sources)) return false;
        }
        return true;
    }

    for (size_t i = 0; i < L->modules.count; i++) {
        const char *mod = L->modules.items[i];
        File_Paths seen = {0};
        for (size_t c = 0; chain[c] != NULL; c++) {
            const char *dir = temp_sprintf("%s/%s/src/%s", MODULES_DIR, mod, chain[c]);
            if (!collect_dir_sources_filtered(dir, &L->sources, &seen)) return false;
        }
    }
    return true;
}

#include "nob_android.c"

// --- Library compilation ---

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

// --- compile_commands.json ---

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

static void append_android_compile_commands(String_Builder *sb, size_t *entries, const char *cwd) {
    const char *sdk = android_sdk_dir_any();
    if (sdk == NULL) {
        nob_log(NOB_INFO, "compile_commands: no Android SDK found, skipping android entries");
        return;
    }
    const char *ndk = android_ndk_dir(sdk);
    if (ndk == NULL) return;
    const char *toolchain_bin = android_toolchain_bin(ndk);
    if (toolchain_bin == NULL) return;
    const char *sysroot = android_sysroot_dir(toolchain_bin);

    const Android_Abi *abi = &android_abis[0];
    Target *t = target_android(abi, toolchain_bin, ndk);

    Layout L = {0};
    if (!discover_for_platform(&L, "android")) return;
    da_append(&L.includes, "third-party/mongoose");

    const char *triple = temp_sprintf("%s%d", abi->configure_host, t->android_api);

    size_t emitted = 0;
    for (size_t i = 0; i < L.sources.count; i++) {
        const char *src = L.sources.items[i];
        if (strstr(src, "/android/") == NULL) continue;

        const char *obj = target_object_for(t, src);
        Cmd cmd = {0};
        cmd_append(&cmd, t->cc);
        append_base_cflags(&cmd);
        cmd_append(&cmd, temp_sprintf("--target=%s", triple));
        cmd_append(&cmd, temp_sprintf("--sysroot=%s", sysroot));
        cmd_append(&cmd, "-DANDROID");
        if (source_is_third_party(src)) cmd_append(&cmd, "-Wno-deprecated-declarations");
        append_include_flags(&cmd, t, &L);
        cmd_append(&cmd, "-c", src, "-o", obj);
        append_compile_command(sb, entries, cwd, src, cmd);
        emitted++;
    }
    nob_log(NOB_INFO, "compile_commands: added %zu android entries", emitted);
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
        if (source_is_third_party(src)) cmd_append(&cmd, "-Wno-deprecated-declarations");
        append_include_flags(&cmd, t, L);
        cmd_append(&cmd, "-c", src, "-o", obj);
        append_compile_command(&sb, &entries, cwd, src, cmd);
    }

    append_android_compile_commands(&sb, &entries, cwd);

    sb_append_cstr(&sb, "\n]\n");
    return write_entire_file(CCMDS_PATH, sb.items, sb.count);
}

// --- Core library build ---

static bool build_library(void) {
    if (!bootstrap_third_party(target_host())) return false;

    if (!mkdir_if_not_exists(BUILD_DIR)) return false;
    if (!mkdir_if_not_exists(OBJ_DIR))   return false;

    Layout L = {0};
    if (!discover_for_platform(&L, host_platform())) {
        nob_log(NOB_ERROR, "failed to discover modules under %s/", MODULES_DIR);
        return false;
    }

    // Third-party libraries compiled directly into melody.lib
    da_append(&L.includes, "third-party/mongoose");
    da_append(&L.sources, "third-party/mongoose/mongoose.c");

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

// =============================================================================
// Unified desktop app build — data-driven platform descriptor replaces the
// four formerly copy-pasted *_build_app functions.
// =============================================================================

typedef struct {
    const char *platform;               // platform subdirectory name (e.g. "win32")
    const char *binary_fmt;             // output path, with one %s for app name
    const char *const *link_libs;       // library/framework flags (-l, -framework)
    size_t link_libs_count;
    bool has_resources;                 // true if .rc resource files are compiled
    bool needs_third_party_libs;        // true if -lmpfr/-lgmp/-lSDL3/-lsqlite3 needed
} PlatformConfig;

// ---- Platform descriptors (the only platform-specific data) ----

#if PLATFORM_WIN32
static const char *win32_libs[] = {
    "-lmelody",
    "-luser32", "-lgdi32", "-lcomctl32", "-lcomdlg32", "-lkernel32", "-lwinmm",
    "-Wl,/SUBSYSTEM:WINDOWS", "-Wl,/ENTRY:WinMainCRTStartup", "-Wl,/MANIFEST:NO",
};
#else
static const char *macos_libs[] = {
    "-lmelody", "-lmpfr", "-lgmp", "-lSDL3", "-lsqlite3",
    "-framework", "Cocoa",
    "-framework", "CoreMIDI",
    "-framework", "CoreFoundation",
};

static const char *linux_libs[] = {
    "-lmelody", "-lmpfr", "-lgmp", "-lSDL3", "-lsqlite3",
    "-lasound", "-lpthread", "-lm",
};
#endif

static const PlatformConfig platforms[] = {
#if PLATFORM_WIN32
    { "win32", "build/win32/%s/%s.exe", win32_libs, NOB_ARRAY_LEN(win32_libs), true,  false },
#else
    { "macos", "build/macos/%s/%s",      macos_libs, NOB_ARRAY_LEN(macos_libs), false, true  },
    { "linux", "build/linux/%s/%s",      linux_libs, NOB_ARRAY_LEN(linux_libs), false, true  },
#endif
};

// ---- Unified build ----

static const PlatformConfig *platform_find(const char *name) {
    for (size_t i = 0; i < NOB_ARRAY_LEN(platforms); i++) {
        if (strcmp(platforms[i].platform, name) == 0) return &platforms[i];
    }
    return NULL;
}

static bool desktop_build_app(const char *app_name, const PlatformConfig *cfg) {
    if (!build_library()) return false;

    Layout L = {0};
    if (!discover_for_platform(&L, cfg->platform)) return false;

    // Third-party includes needed by modules
    da_append(&L.includes, "third-party/mongoose");

    // Split bridge sources from regular module sources
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

    const char *out_dir = temp_sprintf("%s/%s/%s", BUILD_DIR, cfg->platform, app_name);
    if (!mkdir_if_not_exists(temp_sprintf("%s/%s", BUILD_DIR, cfg->platform))) return false;
    if (!mkdir_if_not_exists(out_dir)) return false;

    // Compile bridge + app sources
    File_Paths link_objs = {0};
    File_Paths all_link_sources[2] = { bridge_sources, app_sources };
    for (int si = 0; si < 2; si++) {
        for (size_t i = 0; i < all_link_sources[si].count; i++) {
            const char *src = all_link_sources[si].items[i];
            const char *obj = object_for(src);
            if (!target_compile_one(target_host(), &L, src, obj, NULL)) return false;
            da_append(&link_objs, obj);
        }
    }

    // Compile resources if this platform has them
    File_Paths res_files = {0};
    if (cfg->has_resources) {
        const char *res_dir = temp_sprintf("%s/%s/%s", APPS_DIR, app_name, cfg->platform);
        if (file_exists(res_dir) && get_file_type(res_dir) == NOB_FILE_DIRECTORY) {
            File_Paths entries = {0};
            if (!read_entire_dir(res_dir, &entries)) return false;
            for (size_t i = 0; i < entries.count; i++) {
                const char *n = entries.items[i];
                if (!ends_with(n, ".rc")) continue;
                const char *rc_path = temp_sprintf("%s/%s", res_dir, n);
                const char *res_path = temp_sprintf("%s/%s.res", out_dir, n);
                Cmd rc = {0};
                cmd_append(&rc, "llvm-rc", "/nologo");
                cmd_append(&rc, temp_sprintf("/I%s", res_dir));
                cmd_append(&rc, "/fo", res_path, rc_path);
                if (!cmd_run_sync_and_reset(&rc)) return false;
                da_append(&res_files, temp_strdup(res_path));
            }
        }
    }

    // Link
    const char *bin = temp_sprintf(cfg->binary_fmt, app_name, app_name);

    Cmd cmd = {0};
    cmd_append(&cmd, "clang");
    for (size_t i = 0; i < link_objs.count; i++) cmd_append(&cmd, link_objs.items[i]);
    for (size_t i = 0; i < res_files.count; i++) cmd_append(&cmd, res_files.items[i]);
    cmd_append(&cmd, "-L" BUILD_DIR);
    if (cfg->needs_third_party_libs) {
        cmd_append(&cmd, temp_sprintf("-L%s", target_lib(target_host())));
    }
    for (size_t i = 0; i < cfg->link_libs_count; i++) cmd_append(&cmd, cfg->link_libs[i]);
    cmd_append(&cmd, "-o", bin);
    if (!cmd_run_sync_and_reset(&cmd)) return false;

    nob_log(NOB_INFO, "wrote %s", bin);
    return true;
}

// ---- Platform-specific binary paths & launch ----

static const char *macos_binary_path(const char *app_name) {
    return temp_sprintf("build/macos/%s/%s", app_name, app_name);
}

static const char *win32_binary_path(const char *app_name) {
    return temp_sprintf("build/win32/%s/%s.exe", app_name, app_name);
}

static const char *linux_binary_path(const char *app_name) {
    return temp_sprintf("build/linux/%s/%s", app_name, app_name);
}

static bool macos_launch(const char *app_name) {
    Cmd cmd = {0};
    cmd_append(&cmd, macos_binary_path(app_name));
    return cmd_run_sync_and_reset(&cmd);
}

static bool win32_launch(const char *app_name) {
    Cmd cmd = {0};
    cmd_append(&cmd, win32_binary_path(app_name));
    return cmd_run_sync_and_reset(&cmd);
}

static bool linux_launch(const char *app_name) {
    Cmd cmd = {0};
    cmd_append(&cmd, linux_binary_path(app_name));
    return cmd_run_sync_and_reset(&cmd);
}

// ---- Dispatch ----

static int usage(void) {
    nob_log(NOB_ERROR,
        "usage:\n"
        "  ./nob                                  build library archive\n"
        "  ./nob build app <name> [<platform>]    build app (default platform: host)\n"
        "  ./nob run   app <name> [<platform>]    build + install + launch\n"
        "  ./nob debug app <name> [<platform>]    run + stream Melody logcat");
    return 1;
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

    // Android has its own build pipeline
    if (strcmp(platform, "android") == 0) {
        if (!android_build_app(name)) return 1;
        if (strcmp(cmd, "build") == 0) return 0;
        if (!android_install(name)) return 1;
        if (!android_launch(name)) return 1;
        if (strcmp(cmd, "run") == 0) return 0;
        return android_logcat(name) ? 0 : 1;
    }

    // Desktop platforms use the unified build
    const PlatformConfig *cfg = platform_find(platform);
    if (cfg == NULL) {
        nob_log(NOB_ERROR, "platform '%s' is not implemented yet", platform);
        return 1;
    }

    if (!desktop_build_app(name, cfg)) return 1;
    if (strcmp(cmd, "build") == 0) return 0;

    // Launch
    if (strcmp(platform, "macos") == 0) return macos_launch(name) ? 0 : 1;
    if (strcmp(platform, "win32") == 0) return win32_launch(name) ? 0 : 1;
    if (strcmp(platform, "linux") == 0) return linux_launch(name) ? 0 : 1;

    return 0;
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
