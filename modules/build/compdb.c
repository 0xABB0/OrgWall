#include "runner.h"

#include <dirent.h>
#include <stdio.h>
#include <unistd.h>

static char *compdb_cwd(void) {
    char buf[4096];
    if (!getcwd(buf, sizeof buf)) return mel_str_dup(".");
    return mel_str_dup(buf);
}

static char *join_cmd(Mel_StrVec *v) {
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

static void json_str(FILE *f, const char *s) {
    fputc('"', f);
    for (const char *p = s; *p; p++) {
        if (*p == '"' || *p == '\\') fputc('\\', f);
        fputc(*p, f);
    }
    fputc('"', f);
}

static void entry(FILE *f, bool *first, const char *dir, Mel_StrVec *cmd, const char *file) {
    char *command = join_cmd(cmd);
    if (!*first) fputs(",\n", f);
    *first = false;
    fputs("  {\n    \"directory\": ", f);
    json_str(f, dir);
    fputs(",\n    \"command\": ", f);
    json_str(f, command);
    fputs(",\n    \"file\": ", f);
    json_str(f, file);
    fputs("\n  }", f);
    free(command);
}

static void config_cflags(const char *config, Mel_StrVec *out) {
    mel_da_push(out, "-std=c23");
    if (config && strcmp(config, "release") == 0) {
        mel_da_push(out, "-O2");
        mel_da_push(out, "-DNDEBUG");
    } else {
        mel_da_push(out, "-g");
        mel_da_push(out, "-O0");
    }
}

static char *read_all(FILE *p) {
    size_t cap = 1 << 16, len = 0;
    char  *buf = malloc(cap);
    for (size_t n; (n = fread(buf + len, 1, cap - len, p)) > 0;) {
        len += n;
        if (len == cap) buf = realloc(buf, cap *= 2);
    }
    buf[len] = 0;
    return buf;
}

static void probe_system_includes(const char *cc, const char *base_cflags, Mel_StrVec *out) {
    char *cmd = mel_str_fmt("%s %s -E -v -x c /dev/null 2>&1", cc, base_cflags);
    FILE *p   = popen(cmd, "r");
    free(cmd);
    if (!p) return;
    char *text = read_all(p);
    pclose(p);

    const char *start = strstr(text, "#include <...> search starts here:");
    const char *end   = start ? strstr(start, "End of search list.") : NULL;
    if (start && end) {
        const char *line = strchr(start, '\n');
        if (line) line++;
        while (line && line < end) {
            const char *nl  = strchr(line, '\n');
            const char *lim = (nl && nl < end) ? nl : end;
            const char *s   = line;
            size_t      len = (size_t)(lim - s);
            while (len && (*s == ' ' || *s == '\t')) s++, len--;
            while (len && (s[len - 1] == ' ' || s[len - 1] == '\r')) len--;
            if (len) {
                char *d        = mel_str_fmt("%.*s", (int)len, s);
                char *freestd  = mel_path_join(d, "__stddef_max_align_t.h");
                bool  resource = mel_path_is_file(freestd);
                free(freestd);
                if (!resource && !strstr(d, "(framework directory)") && mel_path_is_dir(d)) {
                    mel_da_push(out, "-isystem");
                    mel_da_push(out, d);
                } else
                    free(d);
            }
            if (!nl) break;
            line = nl + 1;
        }
    }
    free(text);
}

static void build_prefix(const Mel_Variant *v, bool host_tool, Mel_StrVec *prefix) {
    mel_da_push(prefix, "clang");
    if (!host_tool) {
        Mel_Toolchain tc = mel_toolchain(v);
        if (tc.cross && !strstr(tc.base_cflags, "-target")) {
            mel_da_push(prefix, "-target");
            mel_da_push(prefix, tc.triple);
        }
        if (tc.base_cflags[0]) mel_da_push(prefix, tc.base_cflags);
        if (tc.cross && !strstr(tc.base_cflags, "-isysroot"))
            probe_system_includes(tc.cc, tc.base_cflags, prefix);
    }
    config_cflags(v->config, prefix);
}

static bool closure_available(Mel_Graph *g, const char *name, const Mel_Variant *v) {
    Mel_IdxVec order = {0};
    if (!mel_topo_closure(g, name, &order)) {
        free(order.items);
        return false;
    }
    bool ok = true;
    for (size_t i = 0; i < order.len && ok; i++)
        if (!mel_target_available(g->nodes.items[order.items[i]].t, v)) ok = false;
    free(order.items);
    return ok;
}

static void emit_target(FILE *f, bool *first, const char *dir, Mel_Graph *g, size_t idx,
                        const Mel_Variant *v, const Mel_StrVec *prefix) {
    Mel_Target *t = g->nodes.items[idx].t;
    if (!closure_available(g, t->name, v)) return;

    Mel_StrVec srcs = {0}, gathered = {0};
    if (!mel_gather_compile(g, idx, v, &srcs, &gathered)) return;

    for (size_t i = 0; i < srcs.len; i++) {
        Mel_StrVec cmd = {0};
        for (size_t k = 0; k < prefix->len; k++) mel_da_push(&cmd, prefix->items[k]);
        for (size_t k = 0; k < gathered.len; k++) mel_da_push(&cmd, gathered.items[k]);
        mel_da_push(&cmd, "-c");
        mel_da_push(&cmd, srcs.items[i]);
        entry(f, first, dir, &cmd, srcs.items[i]);
        free(cmd.items);
    }
}

static void emit_build_c(FILE *f, bool *first, const char *dir, const char *file) {
    Mel_StrVec cmd = {0};
    mel_da_push(&cmd, "clang");
    mel_da_push(&cmd, "-std=c23");
    mel_da_push(&cmd, "-Imodules/build");
    mel_da_push(&cmd, "-c");
    mel_da_push(&cmd, file);
    entry(f, first, dir, &cmd, file);
    free(cmd.items);
}

static void scan_build_c(FILE *f, bool *first, const char *dir, const char *root) {
    DIR *d = opendir(root);
    if (!d) return;
    for (struct dirent *e; (e = readdir(d));) {
        if (e->d_name[0] == '.') continue;
        char *sub     = mel_path_join(root, e->d_name);
        char *build_c = mel_path_join(sub, "build.c");
        if (mel_path_is_file(build_c)) emit_build_c(f, first, dir, build_c);
        free(build_c);
        free(sub);
    }
    closedir(d);
}

static void scan_build_system(FILE *f, bool *first, const char *dir) {
    DIR *d = opendir("modules/build");
    if (d) {
        for (struct dirent *e; (e = readdir(d));) {
            const char *dot = strrchr(e->d_name, '.');
            if (!dot || strcmp(dot, ".c") != 0) continue;
            char *src = mel_str_fmt("modules/build/%s", e->d_name);
            emit_build_c(f, first, dir, src);
            free(src);
        }
        closedir(d);
    }
    if (mel_path_is_file("nob.c")) emit_build_c(f, first, dir, "nob.c");
}

bool mel_emit_compdb(Mel_Graph *g, const Mel_Variant *variants, size_t nvar, const char *out_path) {
    FILE *f = fopen(out_path, "w");
    if (!f) {
        fprintf(stderr, "build: cannot write %s\n", out_path);
        return false;
    }
    char *dir   = compdb_cwd();
    bool  first = true;
    fputs("[\n", f);

    scan_build_c(f, &first, dir, "modules");
    scan_build_c(f, &first, dir, "apps");
    scan_build_c(f, &first, dir, "third-party");
    scan_build_system(f, &first, dir);

    Mel_StrVec host_prefix = {0};
    if (nvar) build_prefix(&variants[0], true, &host_prefix);

    for (size_t vi = 0; vi < nvar; vi++) {
        const Mel_Variant *v = &variants[vi];
        fprintf(stderr, "build: compdb resolving %s\n", mel_platform_name(v->platform));
        Mel_StrVec prefix = {0};
        build_prefix(v, false, &prefix);
        for (size_t i = 0; i < g->nodes.len; i++) {
            bool is_host = g->nodes.items[i].t->kind == MEL_KIND_HOST_TOOL;
            if (is_host && vi != 0) continue;
            if (is_host)
                emit_target(f, &first, dir, g, i, &variants[0], &host_prefix);
            else
                emit_target(f, &first, dir, g, i, v, &prefix);
        }
        free(prefix.items);
    }
    free(host_prefix.items);

    fputs("\n]\n", f);
    fclose(f);
    free(dir);
    return true;
}
