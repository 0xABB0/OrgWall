#include "runner_internal.h"

// =============================================================================
// Build orchestration
// =============================================================================

static void context_init(Mel_Build_Context *ctx, Mel_Build_Target *t, Mel_Platform p, Mel_Config c) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->target = t;
    ctx->platform = p;
    ctx->config = c;
    ctx->backend = g_backend;
    ctx->runtime = g_runtime;
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

static bool run_target_stage(Mel_Build_Target *t, Mel_Platform p, Mel_Config c, Mel_Stage stage) {
    Mel_Build_Context ctx;
    context_init(&ctx, t, p, c);
    return run_stage(&ctx, stage);
}

static bool build_graph_ninja(Mel_Build_Target **order, size_t order_count,
                              Mel_Build_Target *root, Mel_Platform p, Mel_Config c, Mel_Stage last) {
    for (size_t i = 0; i < order_count; i++)
        if (!run_target_stage(order[i], p, c, MEL_STAGE_CONFIGURE)) return false;
    if (last == MEL_STAGE_CONFIGURE) return true;

    // Android builds its third-party deps per-ABI inside emit_android_edges; on
    // every other platform the host third-party archives are built here first.
    if (p != MEL_PLATFORM_ANDROID) {
        for (size_t i = 0; i < order_count; i++) {
            if (order[i]->kind != MEL_TARGET_THIRD_PARTY) continue;
            if (!run_target_stage(order[i], p, c, MEL_STAGE_COMPILE)) return false;
            if (!run_target_stage(order[i], p, c, MEL_STAGE_LINK)) return false;
        }
    }

    ninja_begin();
    const char *root_artifact = NULL;
    for (size_t i = 0; i < order_count; i++) {
        Mel_Build_Target *t = order[i];
        if (t->kind != MEL_TARGET_LIBRARY && t->kind != MEL_TARGET_APPLICATION) continue;
        Mel_Build_Context ctx;
        context_init(&ctx, t, p, c);
        if (!emit_target_edges(&ctx)) return false;
        if (t == root) root_artifact = target_ninja_output(&ctx);
    }
    const char *default_target = (p == MEL_PLATFORM_ANDROID) ? "android" : root_artifact;
    const char *target = (last == MEL_STAGE_COMPILE) ? "compile" : default_target;
    if (!run_ninja(p, c, default_target, target)) return false;
    if (last < MEL_STAGE_PACKAGE) return true;

    return run_target_stage(root, p, c, MEL_STAGE_PACKAGE);
}

static bool build_graph(Mel_Build_Target *root, Mel_Platform p, Mel_Config c, Mel_Stage last) {
    Mel_Build_Target **order = malloc(sizeof(*order) * (g_targets.count + 1));
    size_t order_count = 0;
    File_Paths visited = {0};
    topo_visit(root, p, c, &visited, &order, &order_count);

    bool ok = build_graph_ninja(order, order_count, root, p, c, last);
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
