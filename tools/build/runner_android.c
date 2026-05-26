#include "runner_internal.h"

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
