#include "runner.h"

#include <stdio.h>
#include <unistd.h>

static char *abspath(const char *rel) {
    char *cwd = malloc(1 << 14);
    if (!getcwd(cwd, 1 << 14)) {
        free(cwd);
        return mel_str_dup(rel);
    }
    char *p = rel[0] == '/' ? mel_str_dup(rel) : mel_str_fmt("%s/%s", cwd, rel);
    free(cwd);
    return p;
}

static void inject_prefix(Mel_Target *t, const char *absprefix) {
    mel_da_push(&t->includes,
                ((Mel_Flag){(Mel_When){0}, MEL_PUBLIC, mel_str_fmt("%s/include", absprefix)}));
    mel_da_push(&t->links, ((Mel_Flag){(Mel_When){0}, MEL_PUBLIC, mel_str_fmt("-L%s/lib", absprefix)}));
}

static char *dep_prefix_abs(Mel_Graph *g, const char *name, const Mel_Variant *v) {
    int i = mel_graph_index(g, name);
    if (i < 0) return NULL;
    char *outdir = mel_target_outdir(g->nodes.items[i].t->dir, v);
    char *p      = abspath(mel_path_join(outdir, "prefix"));
    free(outdir);
    return p;
}

static bool build_cmake(Mel_Target *t, const Mel_Variant *v) {
    char *outdir    = mel_target_outdir(t->dir, v);
    char *absprefix = abspath(mel_path_join(outdir, "prefix"));
    char *stamp     = mel_path_join(outdir, ".thirdparty-built");
    if (mel_path_is_file(stamp)) {
        inject_prefix(t, absprefix);
        return true;
    }

    char *bld = mel_path_join(outdir, "cmake");
    char *src = t->cmake_dir ? mel_path_join(t->dir, t->cmake_dir) : mel_str_dup(t->dir);
    mel_mkdirs(bld);

    Mel_StrVec c = {0};
    mel_da_push(&c, "cmake");
    mel_da_push(&c, "-G");
    mel_da_push(&c, "Ninja");
    mel_da_push(&c, "-S");
    mel_da_push(&c, src);
    mel_da_push(&c, "-B");
    mel_da_push(&c, bld);
    mel_da_push(&c, mel_str_fmt("-DCMAKE_INSTALL_PREFIX=%s", absprefix));
    mel_da_push(&c, v->config && strcmp(v->config, "release") == 0 ? "-DCMAKE_BUILD_TYPE=Release"
                                                                   : "-DCMAKE_BUILD_TYPE=Debug");
    for (size_t i = 0; i < t->cmake_args.len; i++) mel_da_push(&c, t->cmake_args.items[i]);
    bool ok = mel_run_vec(&c) == 0;
    if (ok) {
        c.len = 0;
        mel_da_push(&c, "cmake");
        mel_da_push(&c, "--build");
        mel_da_push(&c, bld);
        ok = mel_run_vec(&c) == 0;
    }
    if (ok) {
        c.len = 0;
        mel_da_push(&c, "cmake");
        mel_da_push(&c, "--install");
        mel_da_push(&c, bld);
        ok = mel_run_vec(&c) == 0;
    }
    free(c.items);
    if (!ok) {
        fprintf(stderr, "build: cmake failed for '%s'\n", t->name);
        return false;
    }
    mel_write_file(stamp, "ok\n");
    inject_prefix(t, absprefix);
    return true;
}

static bool build_autotools(Mel_Graph *g, Mel_Target *t, const Mel_Variant *v) {
    char *outdir    = mel_target_outdir(t->dir, v);
    char *absprefix = abspath(mel_path_join(outdir, "prefix"));
    char *stamp     = mel_path_join(outdir, ".thirdparty-built");
    if (mel_path_is_file(stamp)) {
        inject_prefix(t, absprefix);
        return true;
    }

    char *bld    = mel_path_join(outdir, "autotools");
    char *abssrc = abspath(t->autotools_dir && strcmp(t->autotools_dir, ".") != 0
                               ? mel_path_join(t->dir, t->autotools_dir)
                               : t->dir);
    mel_mkdirs(bld);
    Mel_Toolchain tc = mel_toolchain(v);

    Mel_StrVec cpp = {0}, ld = {0};
    for (size_t i = 0; i < t->deps.len; i++) {
        char *dp = dep_prefix_abs(g, t->deps.items[i], v);
        if (dp) {
            mel_da_push(&cpp, mel_str_fmt("-I%s/include", dp));
            mel_da_push(&ld, mel_str_fmt("-L%s/lib", dp));
        }
    }

    Mel_StrVec c = {0};
    mel_da_push(&c, mel_str_fmt("%s/configure", abssrc));
    mel_da_push(&c, mel_str_fmt("--prefix=%s", absprefix));
    mel_da_push(&c, "--disable-shared");
    mel_da_push(&c, "--enable-static");
    if (tc.cross) {
        mel_da_push(&c, mel_str_fmt("--host=%s", tc.triple));
        mel_da_push(&c, mel_str_fmt("CC=%s", tc.autotools_cc));
    }
    if (cpp.len) {
        Mel_StrVec j = {0};
        for (size_t i = 0; i < cpp.len; i++) mel_da_push(&j, cpp.items[i]);
        char *s = NULL;
        for (size_t i = 0; i < j.len; i++) {
            char *n = s ? mel_str_fmt("%s %s", s, j.items[i]) : mel_str_dup(j.items[i]);
            s       = n;
        }
        mel_da_push(&c, mel_str_fmt("CPPFLAGS=%s", s));
    }
    if (ld.len) {
        char *s = NULL;
        for (size_t i = 0; i < ld.len; i++) {
            char *n = s ? mel_str_fmt("%s %s", s, ld.items[i]) : mel_str_dup(ld.items[i]);
            s       = n;
        }
        mel_da_push(&c, mel_str_fmt("LDFLAGS=%s", s));
    }
    for (size_t i = 0; i < t->autotools_args.len; i++) mel_da_push(&c, t->autotools_args.items[i]);
    bool ok = mel_run_cwd(bld, &c) == 0;
    if (ok) {
        c.len = 0;
        mel_da_push(&c, "make");
        mel_da_push(&c, "-j8");
        ok = mel_run_cwd(bld, &c) == 0;
    }
    if (ok) {
        c.len = 0;
        mel_da_push(&c, "make");
        mel_da_push(&c, "install");
        ok = mel_run_cwd(bld, &c) == 0;
    }
    free(c.items);
    if (!ok) {
        fprintf(stderr, "build: autotools failed for '%s'\n", t->name);
        return false;
    }
    mel_write_file(stamp, "ok\n");
    inject_prefix(t, absprefix);
    return true;
}

bool mel_prepare_thirdparty(Mel_Graph *g, Mel_IdxVec *order, const Mel_Variant *v) {
    for (size_t i = 0; i < order->len; i++) {
        Mel_Target *t = g->nodes.items[order->items[i]].t;
        if (t->kind != MEL_KIND_THIRD_PARTY || !mel_target_available(t, v)) continue;
        if (t->cmake_dir && !build_cmake(t, v)) return false;
        if (t->autotools_dir && !build_autotools(g, t, v)) return false;
    }
    return true;
}
