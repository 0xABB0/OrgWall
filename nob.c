#define NOB_IMPLEMENTATION
#define NOB_STRIP_PREFIX
#include "nob.h"

#define BUILD_DIR     "build"
#define OBJ_DIR       "build/obj"
#define MODULES_DIR   "modules"
#define LIB_PATH      "build/libmelody.a"
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
    "-fPIC",
};

static const char *extra_include_dirs[] = {
    MODULES_DIR,
    "third-party",
    "/opt/homebrew/include",
};

typedef struct {
    File_Paths modules;
    File_Paths sources;
    File_Paths includes;
} Layout;

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

        da_append(&L->modules, temp_strdup(name));
        da_append(&L->includes, temp_strdup(mod_path));

        File_Paths files = {0};
        if (!read_entire_dir(mod_path, &files)) return false;
        for (size_t j = 0; j < files.count; j++) {
            const char *fname = files.items[j];
            if (!source_is_buildable(fname)) continue;
            const char *fpath = temp_sprintf("%s/%s", mod_path, fname);
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

static bool emit_compile_commands(const Layout *L) {
    const char *cwd = get_current_dir_temp();
    String_Builder sb = {0};
    sb_append_cstr(&sb, "[\n");

    for (size_t i = 0; i < L->sources.count; i++) {
        const char *src = L->sources.items[i];
        const char *obj = object_for(src);

        Cmd cmd = {0};
        cmd_append(&cmd, "clang");
        append_base_cflags(&cmd);
        append_include_flags(&cmd, L);
        cmd_append(&cmd, "-c", src, "-o", obj);

        String_Builder cmdline = {0};
        cmd_render(cmd, &cmdline);
        sb_append_null(&cmdline);

        if (i > 0) sb_append_cstr(&sb, ",\n");
        sb_append_cstr(&sb, "  {\n    \"directory\": ");
        json_append_escaped(&sb, cwd);
        sb_append_cstr(&sb, ",\n    \"command\": ");
        json_append_escaped(&sb, cmdline.items);
        sb_append_cstr(&sb, ",\n    \"file\": ");
        json_append_escaped(&sb, src);
        sb_append_cstr(&sb, "\n  }");

        free(cmdline.items);
        free(cmd.items);
    }

    sb_append_cstr(&sb, "\n]\n");
    return write_entire_file(CCMDS_PATH, sb.items, sb.count);
}

int main(int argc, char **argv) {
    NOB_GO_REBUILD_URSELF(argc, argv);

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
