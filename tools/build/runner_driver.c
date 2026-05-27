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

static bool launch_test(Mel_Build_Target *t, Mel_Platform p, Mel_Config c, int argc, char **argv) {
    Cmd cmd = {0};
    const char *bin = app_artifact(t, p, c);
    if (p == MEL_PLATFORM_WIN32) bin = temp_sprintf("%s.exe", bin);
    cmd_append(&cmd, bin);
    for (int i = 0; i < argc; i++) cmd_append(&cmd, argv[i]);
    return cmd_run_sync_and_reset(&cmd);
}

#ifdef _WIN32
static bool tool_on_path(const char *tool) {
    char found[MAX_PATH];
    return SearchPathA(NULL, temp_sprintf("%s.exe", tool), NULL, MAX_PATH, found, NULL) != 0;
}
#else
static bool tool_on_path(const char *tool) {
    const char *path = getenv("PATH");
    if (!path || !*path) return false;
    char *dup = temp_strdup(path);
    for (char *dir = strtok(dup, ":"); dir; dir = strtok(NULL, ":")) {
        if (file_exists(temp_sprintf("%s/%s", dir, tool)) > 0) return true;
    }
    return false;
}
#endif

// Synthesize the test target in-code: an application linking melody, whose
// sources are the runner plus every modules/<m>/test directory. No build.c on
// disk; appended straight into the registry. Module test/ dirs are invisible to
// the ordinary build (source discovery only descends <m>/src), so this is the
// sole path that compiles them.
static Mel_Build_Target *synthesize_test_target(void) {
    Mel_Build_Target t = {0};
    t.dir = temp_strdup(".");
    mel_build_set_name(&t, "melody-test");
    mel_build_set_kind(&t, MEL_TARGET_APPLICATION);
    mel_build_add_dependency(&t, "melody");
    mel_build_suppress_default(&t, MEL_STAGE_CONFIGURE);
    mel_build_add_source_root(&t, "tools/test/src");

    File_Paths mods = {0};
    if (!read_entire_dir("modules", &mods)) return NULL;
    for (size_t i = 0; i < mods.count; i++) {
        const char *m = mods.items[i];
        if (strcmp(m, ".") == 0 || strcmp(m, "..") == 0) continue;
        const char *td = temp_sprintf("modules/%s/test", m);
        if (get_file_type(td) == NOB_FILE_DIRECTORY) mel_build_add_source_root(&t, td);
    }

    da_append(&g_targets, t);
    return &g_targets.items[g_targets.count - 1];
}

static bool structural_lint(void) {
    if (!tool_on_path("ast-grep")) {
        nob_log(NOB_WARNING, "ast-grep not on PATH; skipping structural lint");
        return true;
    }
    nob_log(NOB_INFO, "structural lint (ast-grep)");
    Cmd cmd = {0};
    cmd_append(&cmd, "ast-grep", "scan");
    return cmd_run_sync_and_reset(&cmd);
}

int mel_build_main(int argc, char **argv) {
    const char *command = argc >= 2 ? argv[1] : "build";

    // --release / --debug select the build configuration anywhere on the line;
    // the remaining positionals are target then platform.
    Mel_Config config = MEL_CONFIG_DEBUG;
    const char *target_name = NULL;
    const char *platform_name = NULL;
    const char *cli_gpu = NULL;
    char **test_args = NULL;
    int    test_argc = 0;
    for (int i = 2; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--") == 0)             { test_args = &argv[i + 1]; test_argc = argc - (i + 1); break; }
        if (strcmp(a, "--release") == 0)      { config = MEL_CONFIG_RELEASE; continue; }
        if (strcmp(a, "--debug") == 0)        { config = MEL_CONFIG_DEBUG;   continue; }
        if (strncmp(a, "--gpu=", 6) == 0)     { cli_gpu = a + 6; continue; }
        if (strcmp(a, "--gpu") == 0)          { if (i + 1 < argc) cli_gpu = argv[++i]; continue; }
        if (!target_name)        target_name = a;
        else if (!platform_name) platform_name = a;
    }

    // `test` takes no target name (the target is synthesized), so its lone
    // positional is the platform.
    if (strcmp(command, "test") == 0 && target_name && !platform_name) {
        platform_name = target_name;
        target_name = NULL;
    }

    // The platform positional is platform[:backend[:runtime]]; an empty axis
    // field (e.g. "web::wasi") falls back to that axis's default.
    Mel_Platform platform = mel_host_platform();
    const char *cli_backend = NULL, *cli_runtime = NULL;
    if (platform_name) {
        char *spec = temp_strdup(platform_name);
        char *c1 = strchr(spec, ':');
        if (c1) {
            *c1 = '\0';
            cli_backend = c1 + 1;
            char *c2 = strchr(c1 + 1, ':');
            if (c2) { *c2 = '\0'; cli_runtime = c2 + 1; }
        }
        if (cli_backend && !*cli_backend) cli_backend = NULL;
        if (cli_runtime && !*cli_runtime) cli_runtime = NULL;
        if (!mel_platform_from_name(spec, &platform)) {
            nob_log(NOB_ERROR, "unknown platform '%s'", spec);
            return 1;
        }
    }

    if (!discover_targets()) return 1;

    Mel_Stage last = MEL_STAGE_LINK;
    bool do_run = false, do_debug = false, do_test = false, full = false;
    if (strcmp(command, "configure") == 0) last = MEL_STAGE_CONFIGURE;
    else if (strcmp(command, "compile") == 0) last = MEL_STAGE_COMPILE;
    else if (strcmp(command, "link") == 0) last = MEL_STAGE_LINK;
    else if (strcmp(command, "package") == 0) last = MEL_STAGE_PACKAGE;
    else if (strcmp(command, "build") == 0) full = true;
    else if (strcmp(command, "run") == 0) { full = true; do_run = true; }
    else if (strcmp(command, "debug") == 0) { full = true; do_run = true; do_debug = true; }
    else if (strcmp(command, "test") == 0) { full = true; do_test = true; }
    else { nob_log(NOB_ERROR, "unknown command '%s'", command); return 1; }

    // A full build produces the platform's final artifact: an APK on Android, a
    // .app bundle on iOS (both needed to install/run), otherwise the linked
    // binary.
    if (full) last = (platform == MEL_PLATFORM_ANDROID || platform == MEL_PLATFORM_IOS)
                         ? MEL_STAGE_PACKAGE : MEL_STAGE_LINK;

    Mel_Build_Target *root = NULL;
    if (do_test) {
        root = synthesize_test_target();
        if (!root) return 1;
    } else if (target_name) {
        root = registry_find(target_name);
        if (!root) { nob_log(NOB_ERROR, "unknown target '%s'", target_name); return 1; }
    } else {
        root = registry_find("melody");
        if (!root) { nob_log(NOB_ERROR, "no default target 'melody' found"); return 1; }
    }

    // The variant (backend + runtime) binds the whole dependency graph, with
    // precedence CLI > target override > framework default.
    g_backend = cli_backend ? cli_backend : resolve_backend(root, platform);
    g_gpu_backend = cli_gpu ? cli_gpu : resolve_gpu_backend(root, platform);
    g_runtime = cli_runtime ? cli_runtime : resolve_runtime(root, platform);
    if (g_backend && !axis_in_list(valid_backends(platform), g_backend)) {
        nob_log(NOB_ERROR, "backend '%s' is not valid for platform '%s'", g_backend, mel_platform_name(platform));
        return 1;
    }
    if (g_gpu_backend && !axis_in_list(valid_gpu_backends(platform), g_gpu_backend)) {
        nob_log(NOB_ERROR, "gpu backend '%s' is not valid for platform '%s'", g_gpu_backend, mel_platform_name(platform));
        return 1;
    }
    if (!axis_in_list(valid_runtimes(platform), g_runtime)) {
        nob_log(NOB_ERROR, "runtime '%s' is not valid for platform '%s'", g_runtime, mel_platform_name(platform));
        return 1;
    }
    if (platform == MEL_PLATFORM_WEB) {
        g_web_threading = root->web_threading;
        g_web_asyncify  = root->web_asyncify;
    }
    nob_log(NOB_INFO, "variant %s/%s/%s/%s%s%s",
            mel_platform_name(platform), g_backend ? g_backend : "headless",
            g_gpu_backend ? g_gpu_backend : "no-gpu", g_runtime,
            g_web_threading ? " +threading" : "", g_web_asyncify ? " +asyncify" : "");

    if (last >= MEL_STAGE_COMPILE && !structural_lint()) return 1;

    if (!build_graph(root, platform, config, last)) return 1;

    if (do_test) return launch_test(root, platform, config, test_argc, test_args) ? 0 : 1;

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
