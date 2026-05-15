#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "nob.h"

#define BUILD_DIR     "build"
#define OBJ_DIR       "build/obj"
#define MODULES_DIR   "modules"
#define LIB_PATH      "build/libmelody.a"
#define CCMDS_PATH    "compile_commands.json"
#define ANDROID_APP_DIR "apps/android"
#define ANDROID_JNILIBS_DIR "apps/android/app/src/main/jniLibs"

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
    "-fPIC",
};

static const char *extra_include_dirs[] = {
    "third-party",
    "/opt/homebrew/include",
};

typedef struct {
    const char *abi;
    const char *clang;
} Android_Abi;

static const Android_Abi android_abis[] = {
    { "arm64-v8a",   "aarch64-linux-android23-clang" },
    { "armeabi-v7a", "armv7a-linux-androideabi23-clang" },
    { "x86",         "i686-linux-android23-clang" },
    { "x86_64",      "x86_64-linux-android23-clang" },
};

static const char *android_sources[] = {
    "apps/android/app/src/main/c/native_gui_android.c",
    "modules/gui/src/gui.c",
    "modules/gui.platform.android/src/android/gui.platform.android.c",
    "modules/allocator/src/allocator.c",
    "modules/allocator/src/allocator.heap.c",
};

static const char *android_headers[] = {
    "modules/gui/include/gui/gui.h",
    "modules/gui.control/include/gui.control/gui.control.h",
    "modules/gui.platform.android/include/gui.platform.android/gui.platform.android.h",
};

typedef struct {
    File_Paths modules;
    File_Paths sources;
    File_Paths includes;
} Layout;

static const char *android_sdk_dir(void);
static const char *android_ndk_dir(const char *sdk);
static const char *android_toolchain_bin(const char *ndk);

static bool ends_with(const char *s, const char *suffix) {
    size_t ls = strlen(s), lsuf = strlen(suffix);
    if (lsuf > ls) return false;
    return memcmp(s + ls - lsuf, suffix, lsuf) == 0;
}

static bool source_is_buildable(const char *name) {
    if (!ends_with(name, ".c")) return false;
    if (ends_with(name, ".build.c")) return false;
    if (!PLATFORM_WIN32 && ends_with(name, ".win32.c")) return false;
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

        const char *src_path = temp_sprintf("%s/src", mod_path);
        if (!file_exists(src_path) || get_file_type(src_path) != NOB_FILE_DIRECTORY) continue;

        File_Paths files = {0};
        if (!read_entire_dir(src_path, &files)) return false;
        for (size_t j = 0; j < files.count; j++) {
            const char *fname = files.items[j];
            if (!source_is_buildable(fname)) continue;
            const char *fpath = temp_sprintf("%s/%s", src_path, fname);
            if (get_file_type(fpath) != NOB_FILE_REGULAR) continue;
            da_append(&L->sources, temp_strdup(fpath));
        }
    }
    return true;
}

static const char *object_for(const char *src) {
    const char *rel = src + strlen(MODULES_DIR) + 1;
    String_Builder sb = {0};
    sb_appendf(&sb, "%s/", OBJ_DIR);
    for (const char *p = rel; *p; p++) {
        sb_append_buf(&sb, (*p == '/') ? "." : p, 1);
    }
    sb_append_cstr(&sb, ".o");
    sb_append_null(&sb);
    return temp_strdup(sb.items);
}

static void append_include_flags(Cmd *cmd, const Layout *L) {
    for (size_t i = 0; i < NOB_ARRAY_LEN(extra_include_dirs); i++) {
        cmd_append(cmd, temp_sprintf("-I%s", extra_include_dirs[i]));
    }
    for (size_t i = 0; i < L->includes.count; i++) {
        cmd_append(cmd, temp_sprintf("-I%s", L->includes.items[i]));
    }
}

static void append_base_cflags(Cmd *cmd) {
    for (size_t i = 0; i < NOB_ARRAY_LEN(base_cflags); i++) {
        cmd_append(cmd, base_cflags[i]);
    }
}

static bool compile_all(const Layout *L, File_Paths *objects) {
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
        append_include_flags(&cmd, L);
        cmd_append(&cmd, "-c", src, "-o", obj);

        Proc p = cmd_run_async_and_reset(&cmd);
        if (!procs_append_with_flush(&procs, p, parallelism)) return false;
    }
    return procs_wait_and_reset(&procs);
}

static bool archive(const File_Paths *objects) {
    if (file_exists(LIB_PATH)) delete_file(LIB_PATH);
    Cmd cmd = {0};
    cmd_append(&cmd, "ar", "rcs", LIB_PATH);
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

static bool emit_compile_commands(const Layout *L) {
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
        append_include_flags(&cmd, L);
        cmd_append(&cmd, "-c", src, "-o", obj);
        append_compile_command(&sb, &entries, cwd, src, cmd);
    }

    const char *sdk = android_sdk_dir();
    const char *ndk = sdk ? android_ndk_dir(sdk) : NULL;
    const char *toolchain_bin = ndk ? android_toolchain_bin(ndk) : NULL;
    if (toolchain_bin != NULL) {
        const Android_Abi abi = android_abis[0];
        const char *sysroot = temp_sprintf("%s/../sysroot", toolchain_bin);

        for (size_t i = 0; i < NOB_ARRAY_LEN(android_sources); i++) {
            const char *src = android_sources[i];
            const char *obj = temp_sprintf("%s/android.%s.%zu.o", OBJ_DIR, abi.abi, i);

            Cmd cmd = {0};
            cmd_append(&cmd, temp_sprintf("%s/%s", toolchain_bin, abi.clang));
            append_base_cflags(&cmd);
            cmd_append(&cmd,
                "-DANDROID",
                temp_sprintf("--sysroot=%s", sysroot),
                temp_sprintf("-isystem%s/usr/include", sysroot),
                "-Iapps/android/app/src/main/c",
                "-Imodules/gui/include",
                "-Imodules/gui.control/include",
                "-Imodules/gui.layout/include",
                "-Imodules/gui.input/include",
                "-Imodules/gui.accessibility/include",
                "-Imodules/gui.app/include",
                "-Imodules/gui.platform.android/include",
                "-Imodules/core/include",
                "-Imodules/string/include",
                "-Imodules/allocator/include",
                "-Imodules/collection/include",
                "-c", src, "-o", obj);
            append_compile_command(&sb, &entries, cwd, src, cmd);
        }

        for (size_t i = 0; i < NOB_ARRAY_LEN(android_headers); i++) {
            const char *src = android_headers[i];
            const char *obj = temp_sprintf("%s/android.%s.header.%zu.o", OBJ_DIR, abi.abi, i);

            Cmd cmd = {0};
            cmd_append(&cmd, temp_sprintf("%s/%s", toolchain_bin, abi.clang));
            append_base_cflags(&cmd);
            cmd_append(&cmd,
                "-DANDROID",
                "-x", "c-header",
                temp_sprintf("--sysroot=%s", sysroot),
                temp_sprintf("-isystem%s/usr/include", sysroot),
                "-Iapps/android/app/src/main/c",
                "-Imodules/gui/include",
                "-Imodules/gui.control/include",
                "-Imodules/gui.layout/include",
                "-Imodules/gui.input/include",
                "-Imodules/gui.accessibility/include",
                "-Imodules/gui.app/include",
                "-Imodules/gui.platform.android/include",
                "-Imodules/core/include",
                "-Imodules/string/include",
                "-Imodules/allocator/include",
                "-Imodules/collection/include",
                "-c", src, "-o", obj);
            append_compile_command(&sb, &entries, cwd, src, cmd);
        }
    } else {
        nob_log(NOB_WARNING, "Android SDK/NDK not found; compile_commands.json will not include Android JNI sources");
    }

    sb_append_cstr(&sb, "\n]\n");
    return write_entire_file(CCMDS_PATH, sb.items, sb.count);
}

static const char *android_sdk_dir(void) {
    const char *sdk = getenv("ANDROID_HOME");
    if (sdk && sdk[0]) return sdk;

    sdk = getenv("ANDROID_SDK_ROOT");
    if (sdk && sdk[0]) return sdk;

    FILE *f = fopen(ANDROID_APP_DIR "/local.properties", "r");
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

static bool build_android(void) {
    const char *sdk = android_sdk_dir();
    if (sdk == NULL) {
        nob_log(NOB_ERROR, "Android SDK not found. Set ANDROID_HOME/ANDROID_SDK_ROOT or %s/local.properties", ANDROID_APP_DIR);
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

    if (!mkdir_if_not_exists(ANDROID_JNILIBS_DIR)) return false;

    bool ok = true;
    for (size_t i = 0; i < NOB_ARRAY_LEN(android_abis); i++) {
        const Android_Abi abi = android_abis[i];
        const char *abi_dir = temp_sprintf("%s/%s", ANDROID_JNILIBS_DIR, abi.abi);
        if (!mkdir_if_not_exists(abi_dir)) return false;

        Cmd cmd = {0};
        cmd_append(&cmd, temp_sprintf("%s/%s", toolchain_bin, abi.clang));
        append_base_cflags(&cmd);
        cmd_append(&cmd,
            "-shared",
            "-fPIC",
            "-DANDROID",
            "-Iapps/android/app/src/main/c",
            "-Imodules/gui/include",
            "-Imodules/gui.control/include",
            "-Imodules/gui.layout/include",
            "-Imodules/gui.input/include",
            "-Imodules/gui.accessibility/include",
            "-Imodules/gui.app/include",
            "-Imodules/gui.platform.android/include",
            "-Imodules/core/include",
            "-Imodules/string/include",
            "-Imodules/allocator/include",
            "-Imodules/collection/include");

        for (size_t j = 0; j < NOB_ARRAY_LEN(android_sources); j++) {
            cmd_append(&cmd, android_sources[j]);
        }

        cmd_append(&cmd, "-llog", "-o", temp_sprintf("%s/libmelody_android.so", abi_dir));

        if (!cmd_run_sync_and_reset(&cmd)) ok = false;
    }

    return ok;
}

int main(int argc, char **argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);

    if (argc > 1 && strcmp(argv[1], "android") == 0) {
        return build_android() ? 0 : 1;
    }

    if (!mkdir_if_not_exists(BUILD_DIR)) return 1;
    if (!mkdir_if_not_exists(OBJ_DIR))   return 1;

    Layout L = {0};
    if (!discover(&L)) {
        nob_log(NOB_ERROR, "failed to discover modules under %s/", MODULES_DIR);
        return 1;
    }

    nob_log(NOB_INFO, "discovered %zu modules, %zu source files",
            L.modules.count, L.sources.count);

    if (!emit_compile_commands(&L)) {
        nob_log(NOB_ERROR, "failed to write %s", CCMDS_PATH);
        return 1;
    }
    nob_log(NOB_INFO, "wrote %s", CCMDS_PATH);

    File_Paths objects = {0};
    bool ok = compile_all(&L, &objects);
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
        return ok ? 0 : 1;
    }

    if (!archive(&existing)) {
        nob_log(NOB_ERROR, "failed to archive %s", LIB_PATH);
        return 1;
    }
    nob_log(NOB_INFO, "wrote %s (%zu/%zu objects)",
            LIB_PATH, existing.count, objects.count);

    return ok ? 0 : 1;
}
