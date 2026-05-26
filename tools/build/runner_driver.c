#include "runner_internal.h"

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
