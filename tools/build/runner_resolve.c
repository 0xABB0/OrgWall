#include "runner_internal.h"

// =============================================================================
// Path resolution
// =============================================================================

static const char *config_name(Mel_Config c) { return c == MEL_CONFIG_RELEASE ? "release" : "debug"; }

static bool variant_is_default(const char *val, const char *def) {
    if (!val && !def) return true;
    if (!val || !def) return false;
    return strcmp(val, def) == 0;
}

// Platform path segment, suffixed with the backend and/or runtime whenever
// either departs from the framework default, so variant builds (e.g. web vs
// web-wasi, win32 vs win32-qt) never share an output directory.
static const char *variant_dir(Mel_Platform p) {
    const char *base = mel_platform_name(p);
    bool b_def = variant_is_default(g_backend, k_default_backend[p]);
    bool r_def = variant_is_default(g_runtime, k_default_runtime[p]);
    if (b_def && r_def) return base;
    String_Builder sb = {0};
    sb_appendf(&sb, "%s", base);
    if (!b_def && g_backend) sb_appendf(&sb, "-%s", g_backend);
    if (!r_def && g_runtime) sb_appendf(&sb, "-%s", g_runtime);
    sb_append_null(&sb);
    return temp_strdup(sb.items);
}

static const char *target_out_dir(const Mel_Build_Target *t, Mel_Platform p, Mel_Config c) {
    return temp_sprintf("%s/%s/%s", MEL_BUILD_DIR, variant_dir(p), config_name(c));
}

static const char *target_obj_dir(const Mel_Build_Target *t, Mel_Platform p, Mel_Config c) {
    return temp_sprintf("%s/obj/%s/%s/%s", MEL_BUILD_DIR, variant_dir(p), config_name(c), t->name);
}

static const char *thirdparty_prefix(const Mel_Build_Target *t, Mel_Platform p) {
    // Mirror ctx_abi: web archives are keyed on the wasm runtime so an
    // emscripten consumer never links the wasi build of a dep, or vice versa.
    const char *abi = (p == MEL_PLATFORM_WEB) ? g_runtime : NULL;
    return tp_prefix_named(p, abi, t->name);
}

static const char *library_artifact(const Mel_Build_Target *t, Mel_Platform p, Mel_Config c) {
    if (p == MEL_PLATFORM_WIN32) return temp_sprintf("%s/%s.lib", target_out_dir(t, p, c), t->name);
    return temp_sprintf("%s/lib%s.a", target_out_dir(t, p, c), t->name);
}

static const char *app_artifact(const Mel_Build_Target *t, Mel_Platform p, Mel_Config c) {
    return temp_sprintf("%s/%s/%s", target_out_dir(t, p, c), t->name, t->name);
}

// Web bundles live target-first (build/<target>/web/<config>/) so the wasm, its
// loader JS, and the HTML shell sit together ready to host.
static const char *web_out_dir(const Mel_Build_Target *t, Mel_Config c) {
    return temp_sprintf("%s/%s/web/%s", MEL_BUILD_DIR, t->name, config_name(c));
}

static const char *object_for(const Mel_Build_Target *t, Mel_Platform p, Mel_Config c, const char *src) {
    String_Builder sb = {0};
    sb_appendf(&sb, "%s/", target_obj_dir(t, p, c));
    for (const char *q = src; *q; q++) sb_append_buf(&sb, (*q == '/') ? "." : q, 1);
    sb_append_cstr(&sb, ".o");
    sb_append_null(&sb);
    return temp_strdup(sb.items);
}

// Config-independent depfile path for a source: the recorded header list is the
// same for debug and release, so both configs share one depfile and either can
// derive the other's content key from it.
static const char *depfile_for_src(const Mel_Build_Target *t, Mel_Platform p, const char *src) {
    String_Builder sb = {0};
    sb_appendf(&sb, "%s/obj/%s/%s/deps/", MEL_BUILD_DIR, variant_dir(p), t->name);
    for (const char *q = src; *q; q++) sb_append_buf(&sb, (*q == '/') ? "." : q, 1);
    sb_append_cstr(&sb, ".d");
    sb_append_null(&sb);
    return temp_strdup(sb.items);
}

// =============================================================================
// Property propagation
// =============================================================================

static void props_resolve_into(Resolved *dst, const Props *src, Mel_Platform p) {
    prop_resolve(&dst->cflags, &src->cflags, p);
    prop_resolve(&dst->includes, &src->includes, p);
    prop_resolve(&dst->defines, &src->defines, p);
    prop_resolve(&dst->link_flags, &src->link_flags, p);
}

static bool target_supports(const Mel_Build_Target *t, Mel_Platform p);

static void collect_deps_recursive(Mel_Build_Target *t, Mel_Platform p, Mel_Config c,
                                    Resolved *out, File_Paths *visited, File_Paths *bridges) {
    for (size_t i = 0; i < t->deps.count; i++) {
        const char *dep_name = t->deps.items[i];
        if (name_seen(visited, dep_name)) continue;
        da_append(visited, dep_name);

        Mel_Build_Target *d = registry_find(dep_name);
        if (!d) {
            nob_log(NOB_ERROR, "target '%s' depends on unknown target '%s'", t->name, dep_name);
            continue;
        }

        // Silently drop deps that don't support this platform — the consumer
        // is expected to have either gated its own use of the dep's API or
        // marked itself as not supporting this platform either.
        if (!target_supports(d, p)) continue;

        // The dependency's public interface (platform-filtered).
        props_resolve_into(out, &d->pub, p);

        // Auto-derived linkage to the dependency's artifact.
        if (d->kind == MEL_TARGET_LIBRARY) {
            da_append(&out->link_flags, temp_sprintf("-L%s", target_out_dir(d, p, c)));
            da_append(&out->link_flags, temp_sprintf("-l%s", d->name));
        } else if (d->kind == MEL_TARGET_THIRD_PARTY) {
            const char *prefix = thirdparty_prefix(d, p);
            da_append(&out->includes, temp_sprintf("%s/include", prefix));
            da_append(&out->link_flags, temp_sprintf("-L%s/lib", prefix));
        }

        // Library deps contribute their bridge sources to the consumer.
        if (bridges && d->kind == MEL_TARGET_LIBRARY) {
            File_Paths s = {0}, b = {0};
            target_resolve_sources(d, p, &s, &b);
            for (size_t k = 0; k < b.count; k++) da_append(bridges, b.items[k]);
        }

        collect_deps_recursive(d, p, c, out, visited, bridges);
    }
}

static void context_resolve_props(Mel_Build_Context *ctx) {
    Mel_Build_Target *t = ctx->target;
    // Own properties (public + private) come first, platform-filtered.
    props_resolve_into(&ctx->resolved, &t->priv, ctx->platform);
    props_resolve_into(&ctx->resolved, &t->pub, ctx->platform);
    // Then transitive public properties of dependencies.
    File_Paths visited = {0};
    collect_deps_recursive(t, ctx->platform, ctx->config, &ctx->resolved, &visited, &ctx->bridge_sources);
}
