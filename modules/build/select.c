#include "runner.h"

#include <dirent.h>
#include <fnmatch.h>

bool mel_when_match(Mel_When w, const Mel_Variant *v) {
    if (w.platforms && !(w.platforms & (1u << v->platform))) return false;
    if (w.config && (!v->config || strcmp(w.config, v->config) != 0)) return false;
    if (w.backend && (!v->backend || strcmp(w.backend, v->backend) != 0)) return false;
    if (w.gpu && (!v->gpu || strcmp(w.gpu, v->gpu) != 0)) return false;
    if (w.runtime && (!v->runtime || strcmp(w.runtime, v->runtime) != 0)) return false;
    return true;
}

static void glob_rec(const char *cur, const char *const *segs, size_t nseg, size_t i, Mel_StrVec *out) {
    if (i == nseg) {
        if (mel_path_is_file(cur)) mel_da_push(out, mel_str_dup(cur));
        return;
    }

    const char *seg  = segs[i];
    bool        last = i + 1 == nseg;

    if (strcmp(seg, "**") == 0) {
        glob_rec(cur, segs, nseg, i + 1, out);
        DIR *d = opendir(cur);
        if (!d) return;
        for (struct dirent *e; (e = readdir(d));) {
            if (e->d_name[0] == '.') continue;
            char *sub = mel_path_join(cur, e->d_name);
            if (mel_path_is_dir(sub)) glob_rec(sub, segs, nseg, i, out);
            free(sub);
        }
        closedir(d);
        return;
    }

    if (!strchr(seg, '*') && !strchr(seg, '?') && !strchr(seg, '[')) {
        char *next = mel_path_join(cur, seg);
        if (last) {
            if (mel_path_is_file(next)) mel_da_push(out, mel_str_dup(next));
        } else if (mel_path_is_dir(next)) {
            glob_rec(next, segs, nseg, i + 1, out);
        }
        free(next);
        return;
    }

    DIR *d = opendir(cur);
    if (!d) return;
    for (struct dirent *e; (e = readdir(d));) {
        if (e->d_name[0] == '.') continue;
        if (fnmatch(seg, e->d_name, FNM_PATHNAME) != 0) continue;
        char *sub = mel_path_join(cur, e->d_name);
        if (last) {
            if (mel_path_is_file(sub)) mel_da_push(out, mel_str_dup(sub));
        } else if (mel_path_is_dir(sub)) {
            glob_rec(sub, segs, nseg, i + 1, out);
        }
        free(sub);
    }
    closedir(d);
}

void mel_glob(const char *base, const char *pattern, Mel_StrVec *out) {
    char      *copy = mel_str_dup(pattern);
    Mel_StrVec segs = {0};
    for (char *tok = strtok(copy, "/"); tok; tok = strtok(NULL, "/")) mel_da_push(&segs, tok);
    if (segs.len) glob_rec(base && *base ? base : ".", segs.items, segs.len, 0, out);
    free(segs.items);
    free(copy);
}
