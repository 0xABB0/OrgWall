#include "runner.h"

#include <stdio.h>
#include <unistd.h>

static char *abspath(const char *rel) {
    char *cwd = malloc(1 << 14);
    if (!getcwd(cwd, 1 << 14)) {
        free(cwd);
        return mel_str_dup(rel);
    }
    char *p = mel_str_fmt("%s/%s", cwd, rel);
    free(cwd);
    return p;
}

static bool build_cmake(Mel_Target *t, const Mel_Variant *v) {
    char *outdir    = mel_target_outdir(t->dir, v);
    char *bld       = mel_path_join(outdir, "cmake");
    char *prefix    = mel_path_join(outdir, "prefix");
    char *absprefix = abspath(prefix);
    char *src       = t->cmake_dir ? mel_path_join(t->dir, t->cmake_dir) : mel_str_dup(t->dir);
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
        fprintf(stderr, "build: cmake failed for third-party '%s'\n", t->name);
        return false;
    }

    mel_da_push(&t->includes,
                ((Mel_Flag){(Mel_When){0}, MEL_PUBLIC, mel_str_fmt("%s/include", absprefix)}));
    mel_da_push(&t->links, ((Mel_Flag){(Mel_When){0}, MEL_PUBLIC, mel_str_fmt("-L%s/lib", absprefix)}));
    return true;
}

bool mel_prepare_thirdparty(Mel_Graph *g, Mel_IdxVec *order, const Mel_Variant *v) {
    for (size_t i = 0; i < order->len; i++) {
        Mel_Target *t = g->nodes.items[order->items[i]].t;
        if (t->kind == MEL_KIND_THIRD_PARTY && t->cmake_dir && mel_target_available(t, v)) {
            if (!build_cmake(t, v)) return false;
        }
    }
    return true;
}
