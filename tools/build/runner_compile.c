#include "runner_internal.h"

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
