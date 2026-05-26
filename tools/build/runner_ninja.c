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

static const char *flags_to_value(const Cmd *flags) {
    String_Builder sb = {0};
    for (size_t i = 0; i < flags->count; i++) {
        if (i) sb_append_cstr(&sb, " ");
        ninja_value(&sb, flags->items[i]);
    }
    sb_append_null(&sb);
    return temp_strdup(sb.items);
}

static void ninja_begin(void) {
    g_ninja.count = 0;
    g_ninja_objs.count = 0;
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
        "\n");
}

static void emit_cc_edge(Mel_Build_Context *ctx, const char *cc, const char *src, const char *obj) {
    Cmd flags = {0};
    append_resolved_flags(&flags, ctx, src);
    const char *cflags = flags_to_value(&flags);
    free(flags.items);

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
    Mel_Build_Target *t = ctx->target;

    File_Paths deplibs = {0};
    for (size_t i = 0; i < t->deps.count; i++) {
        Mel_Build_Target *d = registry_find(t->deps.items[i]);
        if (d && d->kind == MEL_TARGET_LIBRARY && target_supports(d, ctx->platform))
            da_append(&deplibs, library_artifact(d, ctx->platform, ctx->config));
    }

    Cmd ld = {0};
    if (ctx->platform == MEL_PLATFORM_IOS) mel_ios_clang_flags(&ld);
    for (size_t i = 0; i < ctx->resolved.link_flags.count; i++) cmd_append(&ld, ctx->resolved.link_flags.items[i]);
    const char *ldflags = flags_to_value(&ld);
    free(ld.items);

    sb_append_cstr(&g_ninja, "build ");
    ninja_path(&g_ninja, ctx->artifact);
    sb_append_cstr(&g_ninja, ": link");
    for (size_t i = 0; i < objs->count; i++) { sb_append_cstr(&g_ninja, " "); ninja_path(&g_ninja, objs->items[i]); }
    if (deplibs.count) {
        sb_append_cstr(&g_ninja, " |");
        for (size_t i = 0; i < deplibs.count; i++) { sb_append_cstr(&g_ninja, " "); ninja_path(&g_ninja, deplibs.items[i]); }
    }
    sb_append_cstr(&g_ninja, "\n  ld = clang\n  ldflags = ");
    sb_append_cstr(&g_ninja, ldflags);
    sb_append_cstr(&g_ninja, "\n");
}

static bool emit_target_edges(Mel_Build_Context *ctx) {
    Mel_Build_Target *t = ctx->target;
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

    if (t->kind == MEL_TARGET_LIBRARY)          emit_ar_edge(ctx->artifact, ar_for(ctx), &objs);
    else if (t->kind == MEL_TARGET_APPLICATION) emit_link_edge(ctx, &objs);
    return true;
}

static bool run_ninja(Mel_Platform p, Mel_Config c, const char *default_target, const char *target) {
    if (default_target) {
        sb_append_cstr(&g_ninja, "build compile: phony");
        for (size_t i = 0; i < g_ninja_objs.count; i++) { sb_append_cstr(&g_ninja, " "); ninja_path(&g_ninja, g_ninja_objs.items[i]); }
        sb_append_cstr(&g_ninja, "\n\ndefault ");
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
