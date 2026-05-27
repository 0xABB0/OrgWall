#include "runner_internal.h"

// =============================================================================
// Source classification
// =============================================================================

static bool ends_with(const char *s, const char *suffix) {
    size_t ls = strlen(s), lsuf = strlen(suffix);
    if (lsuf > ls) return false;
    return memcmp(s + ls - lsuf, suffix, lsuf) == 0;
}

static bool source_is_objc(const char *name) { return ends_with(name, ".m"); }

static bool source_is_bridge(const char *name) {
    return ends_with(name, ".bridge.c") || ends_with(name, ".bridge.m");
}

static bool source_is_buildable(const char *name, Mel_Platform p) {
    bool is_c = ends_with(name, ".c");
    bool is_m = ends_with(name, ".m");
    if (!is_c && !is_m) return false;
    if (ends_with(name, ".build.c")) return false;
    bool win = (p == MEL_PLATFORM_WIN32);
    if (!win && ends_with(name, ".win32.c")) return false;
    if (win && (ends_with(name, ".posix.c") || ends_with(name, ".unix.c"))) return false;
    if (win && is_m) return false;
    return true;
}

static void prop_resolve(File_Paths *dst, const Prop_List *src, Mel_Platform p) {
    uint32_t bit = 1u << p;
    for (size_t i = 0; i < src->count; i++) {
        const Prop *e = &src->items[i];
        if (e->mask != 0 && !(e->mask & bit)) continue;
        if (e->runtime && (!g_runtime || strcmp(e->runtime, g_runtime) != 0)) continue;
        if (e->gpu_backend && (!g_gpu_backend || strcmp(e->gpu_backend, g_gpu_backend) != 0)) continue;
        da_append(dst, e->value);
    }
}

// =============================================================================
// Target registry
// =============================================================================

typedef struct {
    Mel_Build_Target *items;
    size_t count, capacity;
} Target_Registry;

static Target_Registry g_targets;

static Mel_Build_Target *registry_find(const char *name) {
    for (size_t i = 0; i < g_targets.count; i++) {
        if (strcmp(g_targets.items[i].name, name) == 0) return &g_targets.items[i];
    }
    return NULL;
}

static bool module_excluded(const Mel_Build_Target *t, Mel_Platform p, const char *name) {
    uint32_t bit = 1u << p;
    for (size_t i = 0; i < t->excluded_modules.count; i++) {
        const Prop *e = &t->excluded_modules.items[i];
        if ((e->mask & bit) && strcmp(e->value, name) == 0) return true;
    }
    for (size_t i = 0; i < t->excluded_modules_rt.count; i++) {
        const Rt_Prop *e = &t->excluded_modules_rt.items[i];
        if (g_runtime && strcmp(e->runtime, g_runtime) == 0 && strcmp(e->value, name) == 0) return true;
    }
    return false;
}

static bool source_excluded(const Mel_Build_Target *t, Mel_Platform p, const char *path) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    uint32_t bit = 1u << p;
    for (size_t i = 0; i < t->excluded_sources.count; i++) {
        const Prop *e = &t->excluded_sources.items[i];
        if ((e->mask & bit) && strcmp(e->value, base) == 0) return true;
    }
    return false;
}

static void filter_excluded_sources(const Mel_Build_Target *t, Mel_Platform p, File_Paths *paths) {
    size_t w = 0;
    for (size_t r = 0; r < paths->count; r++) {
        if (source_excluded(t, p, paths->items[r])) continue;
        paths->items[w++] = paths->items[r];
    }
    paths->count = w;
}

static const char *target_config_get(const Mel_Build_Target *t, const char *key) {
    for (size_t i = 0; i < t->cfg_keys.count; i++) {
        if (strcmp(t->cfg_keys.items[i], key) == 0) return t->cfg_vals.items[i];
    }
    return NULL;
}

// =============================================================================
// Source discovery
// =============================================================================

static bool name_seen(const File_Paths *seen, const char *n) {
    for (size_t i = 0; i < seen->count; i++) if (strcmp(seen->items[i], n) == 0) return true;
    return false;
}

// Recurse a directory collecting buildable sources. When skip_platform_dirs is
// set, directories named like a platform are not descended (they are handled
// separately via the platform chain). seen, when non-NULL, enforces basename
// shadowing across a chain.
static bool collect_dir(const char *dir, Mel_Platform p, bool skip_platform_dirs,
                        File_Paths *seen, File_Paths *out_sources, File_Paths *out_bridges) {
    if (!file_exists(dir) || get_file_type(dir) != NOB_FILE_DIRECTORY) return true;
    File_Paths files = {0};
    if (!read_entire_dir(dir, &files)) return false;
    for (size_t i = 0; i < files.count; i++) {
        const char *n = files.items[i];
        if (strcmp(n, ".") == 0 || strcmp(n, "..") == 0) continue;
        const char *full = temp_sprintf("%s/%s", dir, n);
        Nob_File_Type ft = get_file_type(full);
        if (ft == NOB_FILE_DIRECTORY) {
            if (skip_platform_dirs && is_axis_dir(n)) continue;
            if (!collect_dir(full, p, skip_platform_dirs, seen, out_sources, out_bridges)) return false;
        } else if (ft == NOB_FILE_REGULAR) {
            if (!source_is_buildable(n, p)) continue;
            if (seen) {
                if (name_seen(seen, n)) continue;
                da_append(seen, temp_strdup(n));
            }
            if (source_is_bridge(n)) da_append(out_bridges, temp_strdup(full));
            else                     da_append(out_sources, temp_strdup(full));
        }
    }
    return true;
}

// Resolve one source root across the three axes: common sources (dirs owned by
// no axis), then the platform chain with basename shadowing, then the active
// backend dir and active runtime dir. Backend/runtime dirs are additive (their
// basenames are toolkit/runtime-specific and do not collide with the platform
// chain), so shadowing stays confined to the platform chain.
static bool resolve_source_root(const char *root, Mel_Platform p,
                                File_Paths *out_sources, File_Paths *out_bridges) {
    if (!collect_dir(root, p, true, NULL, out_sources, out_bridges)) return false;
    const char *const *chain = mel_platform_chain(p);
    File_Paths seen = {0};
    for (size_t c = 0; chain && chain[c]; c++) {
        const char *sub = temp_sprintf("%s/%s", root, chain[c]);
        if (!collect_dir(sub, p, false, &seen, out_sources, out_bridges)) return false;
    }
    if (g_backend) {
        const char *sub = temp_sprintf("%s/%s", root, g_backend);
        if (!collect_dir(sub, p, false, NULL, out_sources, out_bridges)) return false;
    }
    if (g_gpu_backend) {
        const char *sub = temp_sprintf("%s/%s", root, g_gpu_backend);
        if (!collect_dir(sub, p, false, NULL, out_sources, out_bridges)) return false;
    }
    if (g_runtime && strcmp(g_runtime, "native") != 0) {
        const char *sub = temp_sprintf("%s/%s", root, g_runtime);
        if (!collect_dir(sub, p, false, NULL, out_sources, out_bridges)) return false;
    }
    return true;
}

// Module include dirs are platform-independent, so they are resolved into the
// target's public includes at load time (before property propagation snapshots
// them), separately from per-platform source resolution. Per-platform module
// exclusions are encoded by recording the include with the inverted platform
// mask (so the property only resolves on platforms where the module applies).
static bool resolve_module_includes(Mel_Build_Target *t) {
    for (size_t r = 0; r < t->module_roots.count; r++) {
        const char *modules_dir = t->module_roots.items[r];
        File_Paths entries = {0};
        if (!read_entire_dir(modules_dir, &entries)) return false;
        for (size_t i = 0; i < entries.count; i++) {
            const char *name = entries.items[i];
            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
            const char *mod = temp_sprintf("%s/%s", modules_dir, name);
            if (get_file_type(mod) != NOB_FILE_DIRECTORY) continue;
            const char *inc = temp_sprintf("%s/include", mod);
            if (get_file_type(inc) != NOB_FILE_DIRECTORY) continue;

            uint32_t excl_mask = 0;
            for (size_t k = 0; k < t->excluded_modules.count; k++) {
                const Prop *e = &t->excluded_modules.items[k];
                if (strcmp(e->value, name) == 0) excl_mask |= e->mask;
            }
            // mask == 0 means "all platforms"; otherwise restrict to the
            // complement of the exclusion mask (within the known platform set).
            uint32_t all = (excl_mask == 0) ? 0 : (((1u << MEL_PLATFORM_COUNT) - 1) & ~excl_mask);

            bool dup = false;
            for (size_t k = 0; k < t->pub.includes.count; k++) {
                if (strcmp(t->pub.includes.items[k].value, inc) == 0) { dup = true; break; }
            }
            if (!dup) prop_add(&t->pub.includes, inc, all);
        }
    }
    return true;
}

// Expand a module-root's sources: each immediate subdir is a module whose <m>/src
// is a platform-aware source root. Modules excluded on this platform are skipped.
static bool resolve_module_root(Mel_Build_Target *t, const char *modules_dir, Mel_Platform p,
                                File_Paths *out_sources, File_Paths *out_bridges) {
    File_Paths entries = {0};
    if (!read_entire_dir(modules_dir, &entries)) return false;
    for (size_t i = 0; i < entries.count; i++) {
        const char *name = entries.items[i];
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        const char *mod = temp_sprintf("%s/%s", modules_dir, name);
        if (get_file_type(mod) != NOB_FILE_DIRECTORY) continue;
        if (module_excluded(t, p, name)) continue;
        if (!resolve_source_root(temp_sprintf("%s/src", mod), p, out_sources, out_bridges)) return false;
    }
    return true;
}

static bool target_resolve_sources(Mel_Build_Target *t, Mel_Platform p,
                                    File_Paths *out_sources, File_Paths *out_bridges) {
    for (size_t i = 0; i < t->module_roots.count; i++) {
        if (!resolve_module_root(t, t->module_roots.items[i], p, out_sources, out_bridges)) return false;
    }
    for (size_t i = 0; i < t->source_roots.count; i++) {
        if (!resolve_source_root(t->source_roots.items[i], p, out_sources, out_bridges)) return false;
    }
    filter_excluded_sources(t, p, out_sources);
    filter_excluded_sources(t, p, out_bridges);
    return true;
}
