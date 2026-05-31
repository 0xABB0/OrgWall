#include "runner.h"

#include <dirent.h>
#include <stdio.h>

char *mel_target_outdir(const char *target_dir, const Mel_Variant *v) {
    return mel_str_fmt("%s/build/%s-%s", target_dir, mel_platform_name(v->platform), v->config);
}

static char *outdir_for(Mel_Target *t, const Mel_Variant *v) {
    return t->kind == MEL_KIND_HOST_TOOL ? mel_str_fmt("%s/build/host", t->dir)
                                         : mel_target_outdir(t->dir, v);
}

static void config_base(const char *config, Mel_StrVec *out) {
    mel_da_push(out, mel_str_dup("-std=c23"));
    if (config && strcmp(config, "release") == 0) {
        mel_da_push(out, mel_str_dup("-O2"));
        mel_da_push(out, mel_str_dup("-DNDEBUG"));
    } else {
        mel_da_push(out, mel_str_dup("-g"));
        mel_da_push(out, mel_str_dup("-O0"));
    }
}

static char *obj_path(const char *outdir, const char *dir, const char *src) {
    const char *rel = src;
    size_t      dl  = strlen(dir);
    if (strncmp(src, dir, dl) == 0 && src[dl] == '/') rel = src + dl + 1;
    char *stem = mel_str_dup(rel);
    char *dot  = strrchr(stem, '.');
    if (dot) *dot = 0;
    char *out = mel_str_fmt("%s/obj/%s.o", outdir, stem);
    free(stem);
    return out;
}

static void join_into(FILE *f, Mel_StrVec *v) {
    for (size_t i = 0; i < v->len; i++) fprintf(f, " %s", v->items[i]);
}

static char *join_str(Mel_StrVec *v) {
    size_t n = 1;
    for (size_t i = 0; i < v->len; i++) n += strlen(v->items[i]) + 1;
    char *s = malloc(n);
    s[0]    = 0;
    for (size_t i = 0; i < v->len; i++) {
        if (i) strcat(s, " ");
        strcat(s, v->items[i]);
    }
    return s;
}

static bool produced_has(Mel_StrVec *produced, const char *name) {
    for (size_t i = 0; i < produced->len; i++)
        if (strcmp(produced->items[i], name) == 0) return true;
    return false;
}

static const char *host_clang_flags(void) {
    static char buf[2048];
    static int  done = 0;
    if (done) return buf;
    done   = 1;
    buf[0] = 0;
#if defined(__APPLE__)
    char  sdk[1024] = {0};
    FILE *p         = popen("xcrun --show-sdk-path 2>/dev/null", "r");
    if (p) {
        size_t k = fread(sdk, 1, sizeof sdk - 1, p);
        pclose(p);
        while (k && (sdk[k - 1] == '\n' || sdk[k - 1] == ' ')) k--;
        sdk[k] = 0;
    }
    char        builtin[1024] = {0};
    const char *base          = "/opt/homebrew/opt/llvm/lib/clang";
    DIR        *d             = opendir(base);
    if (d) {
        for (struct dirent *e; (e = readdir(d));) {
            if (e->d_name[0] == '.') continue;
            snprintf(builtin, sizeof builtin, "%s/%s/include", base, e->d_name);
            break;
        }
        closedir(d);
    }
    snprintf(buf, sizeof buf, "%s%s%s%s", sdk[0] ? "-isysroot " : "", sdk,
             builtin[0] ? " -isystem " : "", builtin);
#endif
    return buf;
}

static char *find_header(const char *spelling, Mel_StrVec *cflags) {
    for (size_t i = 0; i < cflags->len; i++) {
        const char *fl = cflags->items[i];
        if (strncmp(fl, "-I", 2) != 0) continue;
        char *cand = mel_path_join(fl + 2, spelling);
        if (mel_path_is_file(cand)) return cand;
        free(cand);
    }
    return NULL;
}

static void emit_codegens(FILE *f, Mel_Graph *g, Mel_Target *t, const char *outdir, Mel_StrVec *cflags,
                          const Mel_Variant *v, Mel_StrVec *objs) {
    char *cflags_joined = join_str(cflags);
    for (size_t c = 0; c < t->codegens.len; c++) {
        Mel_Codegen cg   = t->codegens.items[c];
        int         ti   = mel_graph_index(g, cg.tool);
        if (ti < 0) {
            fprintf(stderr, "build: '%s' codegen references unknown tool '%s'\n", t->name, cg.tool);
            continue;
        }
        Mel_Target *tool = g->nodes.items[ti].t;
        char       *texe = mel_str_fmt("%s/build/host/%s", tool->dir, tool->name);
        char       *genc = mel_str_fmt("%s/gen/%s", outdir, cg.output);

        Mel_StrVec expanded = {0};
        Mel_StrVec headers  = {0};
        for (size_t a = 0; a < cg.args.len; a++) {
            const char *arg = cg.args.items[a];
            if (strcmp(arg, "$out") == 0) {
                mel_da_push(&expanded, mel_str_dup(genc));
            } else if (strcmp(arg, "$cflags") == 0) {
                mel_da_push(&expanded, mel_str_dup(cflags_joined));
            } else if (strcmp(arg, "$hostclang") == 0) {
                mel_da_push(&expanded, mel_str_dup(host_clang_flags()));
            } else {
                mel_da_push(&expanded, mel_str_dup(arg));
                size_t l = strlen(arg);
                if (l > 2 && strcmp(arg + l - 2, ".h") == 0) {
                    char *real = find_header(arg, cflags);
                    if (real) mel_da_push(&headers, real);
                }
            }
        }
        char *cmd = join_str(&expanded);

        fprintf(f, "build %s: codegen %s", genc, texe);
        for (size_t h = 0; h < headers.len; h++) fprintf(f, " %s", headers.items[h]);
        fprintf(f, "\n  cmd = %s %s\n", texe, cmd);

        char *stem = mel_str_dup(cg.output);
        char *dot  = strrchr(stem, '.');
        if (dot) *dot = 0;
        char *obj = mel_str_fmt("%s/obj/gen/%s.o", outdir, stem);
        fprintf(f, "build %s: cc %s\n  cflags = $%s_cflags\n", obj, genc, t->name);
        mel_da_push(objs, obj);
        (void)v;
    }
}

static char *emit_one(FILE *f, Mel_Graph *g, size_t idx, const Mel_Variant *v, Mel_StrVec *produced,
                      Mel_IdxVec *order) {
    Mel_Target *t = g->nodes.items[idx].t;
    if (t->kind == MEL_KIND_THIRD_PARTY && !t->cmake_dir && t->sources.len == 0) return NULL;

    char      *outdir = outdir_for(t, v);
    Mel_StrVec srcs = {0}, cflags = {0};
    config_base(v->config, &cflags);
    if (!mel_gather_compile(g, idx, v, &srcs, &cflags)) return NULL;

    fprintf(f, "%s_cflags =", t->name);
    join_into(f, &cflags);
    fputc('\n', f);

    Mel_StrVec objs = {0};
    for (size_t i = 0; i < srcs.len; i++) {
        char *obj = obj_path(outdir, t->dir, srcs.items[i]);
        fprintf(f, "build %s: cc %s\n  cflags = $%s_cflags\n", obj, srcs.items[i], t->name);
        mel_da_push(&objs, obj);
    }

    emit_codegens(f, g, t, outdir, &cflags, v, &objs);

    char *out = NULL;
    if (t->kind == MEL_KIND_EXECUTABLE || t->kind == MEL_KIND_HOST_TOOL) {
        Mel_StrVec ldflags = {0};
        mel_gather_link(g, idx, v, &ldflags);
        mel_da_push(&ldflags, mel_str_dup("-dead_strip"));

        Mel_StrVec libs = {0};
        if (order) {
            for (size_t i = 0; i < order->len; i++) {
                size_t      di = order->items[i];
                Mel_Target *d  = g->nodes.items[di].t;
                if (di != idx && d->kind == MEL_KIND_LIBRARY && produced_has(produced, d->name)) {
                    char *od = mel_target_outdir(d->dir, v);
                    mel_da_push(&libs, mel_str_fmt("%s/lib%s.a", od, d->name));
                    free(od);
                }
            }
        }

        char *bin = mel_str_fmt("%s/%s", outdir, t->name);
        fprintf(f, "build %s: link", bin);
        join_into(f, &objs);
        join_into(f, &libs);
        fprintf(f, "\n  ldflags =");
        join_into(f, &ldflags);
        fputc('\n', f);
        out = mel_str_dup(bin);
    } else if (t->kind == MEL_KIND_LIBRARY && objs.len) {
        char *lib = mel_str_fmt("%s/lib%s.a", outdir, t->name);
        fprintf(f, "build %s: ar", lib);
        join_into(f, &objs);
        fputc('\n', f);
        mel_da_push(produced, t->name);
        out = mel_str_dup(lib);
    }
    fputc('\n', f);
    return out;
}

bool mel_emit_and_build(Mel_Graph *g, const char *root, const Mel_Variant *v) {
    Mel_IdxVec order = {0};
    if (!mel_topo_closure(g, root, &order)) {
        free(order.items);
        return false;
    }

    int ri = mel_graph_index(g, root);
    if (ri < 0) return false;
    char *root_outdir = outdir_for(g->nodes.items[ri].t, v);
    mel_mkdirs(root_outdir);
    char *ninja_path = mel_str_fmt("%s/build.ninja", root_outdir);
    FILE *f          = fopen(ninja_path, "w");
    if (!f) {
        fprintf(stderr, "build: cannot write %s\n", ninja_path);
        return false;
    }

    fputs("rule cc\n", f);
    fputs("  command = clang $cflags -MMD -MF $out.d -c $in -o $out\n", f);
    fputs("  depfile = $out.d\n  deps = gcc\n  description = CC $out\n\n", f);
    fputs("rule ar\n", f);
    fputs("  command = rm -f $out && ar rcs $out $in\n  description = AR $out\n\n", f);
    fputs("rule link\n", f);
    fputs("  command = clang $in $ldflags -o $out\n  description = LINK $out\n\n", f);
    fputs("rule codegen\n", f);
    fputs("  command = $cmd\n  description = GEN $out\n\n", f);

    Mel_StrVec produced  = {0};
    Mel_IdxVec hosttools = {0};
    for (size_t oi = 0; oi < order.len; oi++) {
        Mel_Target *t = g->nodes.items[order.items[oi]].t;
        for (size_t c = 0; c < t->codegens.len; c++) {
            int ti = mel_graph_index(g, t->codegens.items[c].tool);
            if (ti < 0) continue;
            bool seen = false;
            for (size_t k = 0; k < hosttools.len; k++)
                if (hosttools.items[k] == (size_t)ti) seen = true;
            if (!seen) mel_da_push(&hosttools, (size_t)ti);
        }
    }
    for (size_t i = 0; i < hosttools.len; i++) emit_one(f, g, hosttools.items[i], v, &produced, NULL);

    char *root_out = NULL;
    for (size_t oi = 0; oi < order.len; oi++) {
        char *out = emit_one(f, g, order.items[oi], v, &produced, &order);
        if (order.items[oi] == (size_t)ri && out) root_out = out;
    }

    if (root_out) fprintf(f, "default %s\n", root_out);
    fclose(f);

    fprintf(stderr, "build: emitted %s\n", ninja_path);
    Mel_StrVec cmd = {0};
    mel_da_push(&cmd, "ninja");
    mel_da_push(&cmd, "-f");
    mel_da_push(&cmd, ninja_path);
    int rc = mel_run_vec(&cmd);
    free(cmd.items);
    if (rc != 0) return false;

    Mel_Target *rootT = g->nodes.items[ri].t;
    if (rootT->kind == MEL_KIND_EXECUTABLE && root_out) mel_package(rootT, v, root_outdir, root_out);
    return true;
}
