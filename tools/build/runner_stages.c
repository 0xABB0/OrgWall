#include "runner_internal.h"

// =============================================================================
// Default stages
// =============================================================================

static void append_java_dir(String_Builder *sb, const char *cwd, const char *m, const char *tag) {
    if (!tag) return;
    const char *jd = temp_sprintf("modules/%s/src/%s/java", m, tag);
    if (file_exists(jd) && get_file_type(jd) == NOB_FILE_DIRECTORY) {
        if (sb->count) sb_append_cstr(sb, ",");
        sb_appendf(sb, "%s/%s", cwd, jd);
    }
}

static const char *android_java_srcdirs_csv(const char *cwd) {
    String_Builder sb = {0};
    File_Paths mods = {0};
    if (read_entire_dir("modules", &mods)) {
        for (size_t i = 0; i < mods.count; i++) {
            const char *m = mods.items[i];
            if (strcmp(m, ".") == 0 || strcmp(m, "..") == 0) continue;
            const char *const *chain = mel_platform_chain(MEL_PLATFORM_ANDROID);
            for (size_t c = 0; chain && chain[c]; c++) append_java_dir(&sb, cwd, m, chain[c]);
            append_java_dir(&sb, cwd, m, g_backend);
        }
    }
    sb_append_null(&sb);
    return temp_strdup(sb.items);
}

static const char *configure_out_dir(const Mel_Build_Target *t, Mel_Platform p) {
    return temp_sprintf("%s/%s/%s", MEL_BUILD_DIR, t->name, mel_platform_name(p));
}

static const char *android_sdk_dir(const char *app_name);

static void android_config_defaults(Mel_Build_Target *t) {
    if (!target_config_get(t, "COMPILE_SDK"))      target_config_set(t, "COMPILE_SDK", "36");
    if (!target_config_get(t, "MIN_SDK"))          target_config_set(t, "MIN_SDK", "24");
    if (!target_config_get(t, "TARGET_SDK"))       target_config_set(t, "TARGET_SDK", "36");
    if (!target_config_get(t, "VERSION_CODE"))     target_config_set(t, "VERSION_CODE", "1");
    if (!target_config_get(t, "VERSION_NAME"))     target_config_set(t, "VERSION_NAME", "0.1.0");
    if (!target_config_get(t, "ROOTPROJECT_NAME")) target_config_set(t, "ROOTPROJECT_NAME", t->name);
    if (!target_config_get(t, "NAMESPACE"))        target_config_set(t, "NAMESPACE", temp_sprintf("orgwall.%s", t->name));
    if (!target_config_get(t, "APPLICATION_ID"))   target_config_set(t, "APPLICATION_ID", target_config_get(t, "NAMESPACE"));
    if (!target_config_get(t, "APP_LABEL"))        target_config_set(t, "APP_LABEL", target_config_get(t, "ROOTPROJECT_NAME"));
}

static bool configure_android(Mel_Build_Context *ctx) {
    Mel_Build_Target *t = ctx->target;
    const char *out = temp_sprintf("%s/%s/android", MEL_BUILD_DIR, t->name);

    android_config_defaults(t);

    if (!mel_mkdirs(out)) return false;
    if (!copy_directory_recursively("tools/build/android", out)) return false;

    const char *sdk = android_sdk_dir(t->name);
    if (sdk) {
        const char *lp = temp_sprintf("sdk.dir=%s\n", sdk);
        if (!write_entire_file(temp_sprintf("%s/local.properties", out), lp, strlen(lp))) return false;
    } else {
        nob_log(NOB_WARNING, "no Android SDK resolved; gradle will need ANDROID_HOME");
    }

    nob_log(NOB_INFO, "configured Android project at %s", out);
    return true;
}

static bool plist_set(const char *plist, const char *key, const char *value) {
    Cmd c = {0};
    cmd_append(&c, "/usr/libexec/PlistBuddy", "-c", temp_sprintf("Set :%s %s", key, value), plist);
    return cmd_run_sync_and_reset(&c);
}

static bool configure_apple(Mel_Build_Context *ctx) {
    Mel_Build_Target *t = ctx->target;
    bool ios = ctx->platform == MEL_PLATFORM_IOS;

    const char *label = target_config_get(t, "APP_LABEL");
    if (!target_config_get(t, "BUNDLE_NAME"))         target_config_set(t, "BUNDLE_NAME", t->name);
    if (!target_config_get(t, "BUNDLE_DISPLAY_NAME")) target_config_set(t, "BUNDLE_DISPLAY_NAME", label ? label : t->name);
    if (!target_config_get(t, "BUNDLE_ID"))           target_config_set(t, "BUNDLE_ID", temp_sprintf("orgwall.%s", t->name));
    if (!target_config_get(t, "VERSION_CODE"))        target_config_set(t, "VERSION_CODE", "1");
    if (!target_config_get(t, "VERSION_NAME"))        target_config_set(t, "VERSION_NAME", "0.1.0");
    if (!target_config_get(t, "MIN_MACOS"))           target_config_set(t, "MIN_MACOS", "11.0");
    if (!target_config_get(t, "MIN_IOS"))             target_config_set(t, "MIN_IOS", "13.0");
    target_config_set(t, "EXECUTABLE", t->name);

    const char *out = configure_out_dir(t, ctx->platform);
    if (!mel_mkdirs(out)) return false;
    const char *plist = temp_sprintf("%s/Info.plist", out);
    const char *base = ios ? "tools/build/apple/Info.ios.plist" : "tools/build/apple/Info.macos.plist";
    if (!copy_file(base, plist)) return false;

    bool ok = plist_set(plist, "CFBundleName",            target_config_get(t, "BUNDLE_NAME"))
           && plist_set(plist, "CFBundleDisplayName",     target_config_get(t, "BUNDLE_DISPLAY_NAME"))
           && plist_set(plist, "CFBundleIdentifier",      target_config_get(t, "BUNDLE_ID"))
           && plist_set(plist, "CFBundleExecutable",      target_config_get(t, "EXECUTABLE"))
           && plist_set(plist, "CFBundleVersion",         target_config_get(t, "VERSION_CODE"))
           && plist_set(plist, "CFBundleShortVersionString", target_config_get(t, "VERSION_NAME"));
    ok = ok && (ios ? plist_set(plist, "MinimumOSVersion",     target_config_get(t, "MIN_IOS"))
                    : plist_set(plist, "LSMinimumSystemVersion", target_config_get(t, "MIN_MACOS")));
    if (!ok) return false;

    nob_log(NOB_INFO, "configured Info.plist at %s", plist);
    return true;
}

static bool configure_win32(Mel_Build_Context *ctx) {
    Mel_Build_Target *t = ctx->target;
    const char *out = configure_out_dir(t, ctx->platform);
    if (!mel_mkdirs(out)) return false;
    if (!copy_file("tools/build/win32/app.manifest", temp_sprintf("%s/app.manifest", out))) return false;
    const char *rc = temp_sprintf("%s/app.rc", out);
    if (!copy_file("tools/build/win32/app.rc", rc)) return false;

    // Compile the resource script to app.res here (proper argv quoting) so the
    // ninja link edge can consume it as a plain input.
    const char *res = temp_sprintf("%s/app.res", out);
    if (!file_exists(res) || needs_rebuild1(res, rc) != 0) {
        const char *label = target_config_get(t, "APP_LABEL"); if (!label) label = t->name;
        const char *ver = target_config_get(t, "VERSION_NAME"); if (!ver) ver = "0.1.0";
        Cmd rcc = {0};
        cmd_append(&rcc, "llvm-rc", "/nologo", temp_sprintf("/I%s", out));
        cmd_append(&rcc, temp_sprintf("/DMEL_APP_NAME=\"%s\"", label));
        cmd_append(&rcc, temp_sprintf("/DMEL_APP_VERSION=\"%s\"", ver));
        const char *icon = temp_sprintf("%s/apps/%s/win32/app.ico", get_current_dir_temp(), t->name);
        if (file_exists(icon)) cmd_append(&rcc, temp_sprintf("/DMEL_APP_ICON=\"%s\"", icon));
        cmd_append(&rcc, "/fo", res, rc);
        if (!cmd_run_sync_and_reset(&rcc)) return false;
    }
    nob_log(NOB_INFO, "configured win32 resources at %s", out);
    return true;
}

static bool default_configure(Mel_Build_Context *ctx) {
    if (ctx->target->kind != MEL_TARGET_APPLICATION) return true;
    switch (ctx->platform) {
        case MEL_PLATFORM_ANDROID: return configure_android(ctx);
        case MEL_PLATFORM_MACOS:
        case MEL_PLATFORM_IOS:     return configure_apple(ctx);
        case MEL_PLATFORM_WIN32:   return configure_win32(ctx);
        default:                   return true;
    }
}

static bool copy_preserving(const char *src, const char *dst) {
    Cmd cmd = {0};
    cmd_append(&cmd, "cp", "-p", src, dst);
    return cmd_run_sync_and_reset(&cmd);
}

// Assemble an apple .app bundle from the linked executable and generated plist.
// macOS uses the nested Contents/MacOS layout; iOS bundles are flat (executable
// and Info.plist at the .app root).
static bool package_apple(Mel_Build_Context *ctx) {
    Mel_Build_Target *t = ctx->target;
    const char *pname     = mel_platform_name(ctx->platform);
    const char *bundle    = temp_sprintf("%s/%s/%s/%s.app", MEL_BUILD_DIR, t->name, pname, t->name);
    const char *plist_src = temp_sprintf("%s/%s/%s/Info.plist", MEL_BUILD_DIR, t->name, pname);

    if (ctx->platform == MEL_PLATFORM_IOS) {
        if (!mel_mkdirs(bundle)) return false;
        if (!copy_preserving(ctx->artifact, temp_sprintf("%s/%s", bundle, t->name))) return false;
        if (!copy_file(plist_src, temp_sprintf("%s/Info.plist", bundle))) return false;
        // The simulator refuses to install an unsigned bundle; an ad-hoc
        // signature is enough (no identity, no provisioning profile).
        Cmd sign = {0};
        cmd_append(&sign, "codesign", "--force", "--sign", "-", bundle);
        if (!cmd_run_sync_and_reset(&sign)) return false;
        nob_log(NOB_INFO, "packaged %s", bundle);
        return true;
    }

    const char *contents = temp_sprintf("%s/Contents", bundle);
    const char *macos    = temp_sprintf("%s/MacOS", contents);
    const char *res      = temp_sprintf("%s/Resources", contents);
    if (!mel_mkdirs(macos)) return false;
    if (!mel_mkdirs(res)) return false;

    if (!copy_preserving(ctx->artifact, temp_sprintf("%s/%s", macos, t->name))) return false;
    if (!copy_file(plist_src, temp_sprintf("%s/Info.plist", contents))) return false;

    nob_log(NOB_INFO, "packaged %s", bundle);
    return true;
}

static bool package_android(Mel_Build_Context *ctx);

static bool default_package(Mel_Build_Context *ctx) {
    if (ctx->target->kind != MEL_TARGET_APPLICATION) return true;
    switch (ctx->platform) {
        case MEL_PLATFORM_MACOS:
        case MEL_PLATFORM_IOS:     return package_apple(ctx);
        case MEL_PLATFORM_ANDROID: return package_android(ctx);
        default:                   return true;
    }
}

typedef bool (*Default_Fn)(Mel_Build_Context *);
static const Default_Fn k_defaults[MEL_STAGE_COUNT] = {
    [MEL_STAGE_CONFIGURE] = default_configure,
    [MEL_STAGE_PACKAGE]   = default_package,
};

static bool run_stage(Mel_Build_Context *ctx, Mel_Stage stage) {
    if (!ctx->target->suppress_default[stage] && k_defaults[stage]) {
        if (!k_defaults[stage](ctx)) return false;
    }
    for (size_t i = 0; i < ctx->target->user_cb_count[stage]; i++) {
        if (!ctx->target->user_cbs[stage][i](ctx)) return false;
    }
    return true;
}
