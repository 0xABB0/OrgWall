#include "runner_internal.h"

static String_Builder g_ninja;
static File_Paths g_ninja_objs;

static void ninja_value(String_Builder *sb, const char *s) {
    for (const char *p = s; *p; p++) {
        if (*p == '$') sb_append_cstr(sb, "$$");
        else sb_append_buf(sb, p, 1);
    }
}

static void ninja_path(String_Builder *sb, const char *s) {
    for (const char *p = s; *p; p++) {
        if (*p == ' ' || *p == ':' || *p == '$') sb_append_buf(sb, "$", 1);
        sb_append_buf(sb, p, 1);
    }
}

static const char *flags_to_value(const Cmd *flags, size_t start) {
    String_Builder sb = {0};
    for (size_t i = start; i < flags->count; i++) {
        if (i > start) sb_append_cstr(&sb, " ");
        ninja_value(&sb, flags->items[i]);
    }
    sb_append_null(&sb);
    return temp_strdup(sb.items);
}

static File_Paths g_android_sos;
static bool emit_android_edges(Mel_Build_Context *ctx);

static void ninja_begin(void) {
    g_ninja.count = 0;
    g_ninja_objs.count = 0;
    g_android_sos.count = 0;
    sb_append_cstr(&g_ninja,
        "rule cc\n"
        "  command = $cc $cflags -MD -MF $out.d -c $in -o $out\n"
        "  depfile = $out.d\n"
        "  deps = gcc\n"
        "  description = CC $out\n"
        "\n"
        "rule ar\n"
        "  command = rm -f $out && $ar rcs $out $in\n"
        "  description = AR $out\n"
        "\n"
        "rule link\n"
        "  command = $ld $in $ldflags -o $out\n"
        "  description = LINK $out\n"
        "\n"
        "rule weblink\n"
        "  command = $webcc $in $ldflags -o $out\n"
        "  description = WEBLINK $out\n"
        "\n");
}

static const char *web_app_artifact(Mel_Build_Context *ctx) {
    const char *out = web_out_dir(ctx->target, ctx->config);
    return web_is_wasi() ? temp_sprintf("%s/app.wasm", out) : temp_sprintf("%s/app.html", out);
}

static const char *target_ninja_output(Mel_Build_Context *ctx) {
    if (ctx->target->kind == MEL_TARGET_APPLICATION) {
        if (ctx->platform == MEL_PLATFORM_WEB)   return web_app_artifact(ctx);
        if (ctx->platform == MEL_PLATFORM_WIN32) return temp_sprintf("%s.exe", ctx->artifact);
    }
    return ctx->artifact;
}

static void emit_deplibs(String_Builder *sb, Mel_Build_Context *ctx) {
    File_Paths deplibs = {0};
    for (size_t i = 0; i < ctx->target->deps.count; i++) {
        Mel_Build_Target *d = registry_find(ctx->target->deps.items[i]);
        if (d && d->kind == MEL_TARGET_LIBRARY && target_supports(d, ctx->platform))
            da_append(&deplibs, library_artifact(d, ctx->platform, ctx->config));
    }
    if (!deplibs.count) return;
    sb_append_cstr(sb, " |");
    for (size_t i = 0; i < deplibs.count; i++) { sb_append_cstr(sb, " "); ninja_path(sb, deplibs.items[i]); }
}

static void emit_cc_edge_raw(const char *cc, const char *cflags, const char *src, const char *obj) {
    sb_append_cstr(&g_ninja, "build ");
    ninja_path(&g_ninja, obj);
    sb_append_cstr(&g_ninja, ": cc ");
    ninja_path(&g_ninja, src);
    sb_append_cstr(&g_ninja, "\n  cc = ");
    ninja_value(&g_ninja, cc);
    sb_append_cstr(&g_ninja, "\n  cflags = ");
    sb_append_cstr(&g_ninja, cflags);
    sb_append_cstr(&g_ninja, "\n");
    da_append(&g_ninja_objs, temp_strdup(obj));
}

static void emit_cc_edge(Mel_Build_Context *ctx, const char *cc, const char *src, const char *obj) {
    Cmd flags = {0};
    append_resolved_flags(&flags, ctx, src);
    const char *cflags = flags_to_value(&flags, 0);
    free(flags.items);
    emit_cc_edge_raw(cc, cflags, src, obj);
}

static void emit_ar_edge(const char *lib, const char *ar, const File_Paths *objs) {
    sb_append_cstr(&g_ninja, "build ");
    ninja_path(&g_ninja, lib);
    sb_append_cstr(&g_ninja, ": ar");
    for (size_t i = 0; i < objs->count; i++) { sb_append_cstr(&g_ninja, " "); ninja_path(&g_ninja, objs->items[i]); }
    sb_append_cstr(&g_ninja, "\n  ar = ");
    ninja_value(&g_ninja, ar);
    sb_append_cstr(&g_ninja, "\n");
}

static void emit_link_edge(Mel_Build_Context *ctx, const File_Paths *objs) {
    Cmd ld = {0};
    if (ctx->platform == MEL_PLATFORM_IOS) mel_ios_clang_flags(&ld);
    for (size_t i = 0; i < ctx->resolved.link_flags.count; i++) cmd_append(&ld, ctx->resolved.link_flags.items[i]);
    const char *ldflags = flags_to_value(&ld, 0);
    free(ld.items);

    sb_append_cstr(&g_ninja, "build ");
    ninja_path(&g_ninja, ctx->artifact);
    sb_append_cstr(&g_ninja, ": link");
    for (size_t i = 0; i < objs->count; i++) { sb_append_cstr(&g_ninja, " "); ninja_path(&g_ninja, objs->items[i]); }
    emit_deplibs(&g_ninja, ctx);
    sb_append_cstr(&g_ninja, "\n  ld = clang\n  ldflags = ");
    sb_append_cstr(&g_ninja, ldflags);
    sb_append_cstr(&g_ninja, "\n");
}

static void emit_win32_link_edge(Mel_Build_Context *ctx, const File_Paths *objs) {
    Mel_Build_Target *t = ctx->target;
    const char *out_bin = temp_sprintf("%s.exe", ctx->artifact);
    const char *rcdir = temp_sprintf("%s/%s/%s", MEL_BUILD_DIR, t->name, mel_platform_name(MEL_PLATFORM_WIN32));
    const char *res = temp_sprintf("%s/app.res", rcdir);

    Cmd ld = {0};
    for (size_t i = 0; i < ctx->resolved.link_flags.count; i++) cmd_append(&ld, ctx->resolved.link_flags.items[i]);
    const char *ldflags = flags_to_value(&ld, 0);
    free(ld.items);

    sb_append_cstr(&g_ninja, "build ");
    ninja_path(&g_ninja, out_bin);
    sb_append_cstr(&g_ninja, ": link");
    for (size_t i = 0; i < objs->count; i++) { sb_append_cstr(&g_ninja, " "); ninja_path(&g_ninja, objs->items[i]); }
    if (file_exists(res)) { sb_append_cstr(&g_ninja, " "); ninja_path(&g_ninja, res); }
    emit_deplibs(&g_ninja, ctx);
    sb_append_cstr(&g_ninja, "\n  ld = clang\n  ldflags = ");
    sb_append_cstr(&g_ninja, ldflags);
    sb_append_cstr(&g_ninja, "\n");
}

static void emit_weblink_edge(Mel_Build_Context *ctx, const File_Paths *objs) {
    Cmd ld = {0};
    web_target_flags(&ld);
    for (size_t i = 0; i < ctx->resolved.link_flags.count; i++) cmd_append(&ld, ctx->resolved.link_flags.items[i]);
    cmd_append(&ld, ctx->config == MEL_CONFIG_RELEASE ? "-O2" : "-g");
    if (!web_is_wasi()) {
        cmd_append(&ld, "-sALLOW_MEMORY_GROWTH=1");
        if (g_web_asyncify)  cmd_append(&ld, "-sASYNCIFY");
        if (g_web_threading) cmd_append(&ld, "-pthread", "-sPTHREAD_POOL_SIZE=4");
        if (file_exists("tools/build/web/shell.html")) cmd_append(&ld, "--shell-file", "tools/build/web/shell.html");
    } else {
        cmd_append(&ld, "-lwasi-emulated-signal", "-lwasi-emulated-getpid");
    }
    const char *ldflags = flags_to_value(&ld, 0);
    free(ld.items);

    sb_append_cstr(&g_ninja, "build ");
    ninja_path(&g_ninja, web_app_artifact(ctx));
    sb_append_cstr(&g_ninja, ": weblink");
    for (size_t i = 0; i < objs->count; i++) { sb_append_cstr(&g_ninja, " "); ninja_path(&g_ninja, objs->items[i]); }
    emit_deplibs(&g_ninja, ctx);
    sb_append_cstr(&g_ninja, "\n  webcc = ");
    ninja_value(&g_ninja, web_cc());
    sb_append_cstr(&g_ninja, "\n  ldflags = ");
    sb_append_cstr(&g_ninja, ldflags);
    sb_append_cstr(&g_ninja, "\n");
}

static bool emit_target_edges(Mel_Build_Context *ctx) {
    Mel_Build_Target *t = ctx->target;

    if (ctx->platform == MEL_PLATFORM_ANDROID) {
        if (t->kind == MEL_TARGET_APPLICATION) return emit_android_edges(ctx);
        return true;
    }

    const char *cc = cc_for(ctx);

    File_Paths bridges = {0};
    if (!target_resolve_sources(t, ctx->platform, &ctx->sources, &bridges)) return false;
    if (t->kind == MEL_TARGET_APPLICATION)
        for (size_t i = 0; i < ctx->bridge_sources.count; i++)
            da_append(&ctx->sources, ctx->bridge_sources.items[i]);

    File_Paths objs = {0};
    for (size_t i = 0; i < ctx->sources.count; i++) {
        const char *obj = object_for(t, ctx->platform, ctx->config, ctx->sources.items[i]);
        emit_cc_edge(ctx, cc, ctx->sources.items[i], obj);
        da_append(&objs, obj);
    }

    if (t->kind == MEL_TARGET_LIBRARY) {
        emit_ar_edge(ctx->artifact, ar_for(ctx), &objs);
    } else if (t->kind == MEL_TARGET_APPLICATION) {
        if (ctx->platform == MEL_PLATFORM_WEB)        emit_weblink_edge(ctx, &objs);
        else if (ctx->platform == MEL_PLATFORM_WIN32) emit_win32_link_edge(ctx, &objs);
        else                                          emit_link_edge(ctx, &objs);
    }
    return true;
}

static bool run_ninja(Mel_Platform p, Mel_Config c, const char *default_target, const char *target) {
    sb_append_cstr(&g_ninja, "build compile: phony");
    for (size_t i = 0; i < g_ninja_objs.count; i++) { sb_append_cstr(&g_ninja, " "); ninja_path(&g_ninja, g_ninja_objs.items[i]); }
    sb_append_cstr(&g_ninja, "\n");
    if (g_android_sos.count) {
        sb_append_cstr(&g_ninja, "build android: phony");
        for (size_t i = 0; i < g_android_sos.count; i++) { sb_append_cstr(&g_ninja, " "); ninja_path(&g_ninja, g_android_sos.items[i]); }
        sb_append_cstr(&g_ninja, "\n");
    }
    if (default_target) {
        sb_append_cstr(&g_ninja, "\ndefault ");
        ninja_path(&g_ninja, default_target);
        sb_append_cstr(&g_ninja, "\n");
    }

    const char *dir = temp_sprintf("%s/%s/%s", MEL_BUILD_DIR, variant_dir(p), config_name(c));
    if (!mel_mkdirs(dir)) return false;
    const char *nfile = temp_sprintf("%s/build.ninja", dir);
    if (!write_entire_file(nfile, g_ninja.items, g_ninja.count)) return false;

    Cmd cmd = {0};
    cmd_append(&cmd, "ninja", "-f", nfile);
    if (target) cmd_append(&cmd, target);
    if (!cmd_run_sync_and_reset(&cmd)) return false;

    Cmd db = {0};
    cmd_append(&db, "sh", "-c", temp_sprintf("ninja -f %s -t compdb cc > compile_commands.json", nfile));
    if (!cmd_run_sync_and_reset(&db)) nob_log(NOB_WARNING, "failed to write compile_commands.json");
    return true;
}
