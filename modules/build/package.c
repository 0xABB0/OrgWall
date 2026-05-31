#include "runner.h"

#include <dirent.h>
#include <stdio.h>

typedef MEL_VEC(char) Mel_CharVec;

static void put_str(Mel_CharVec *v, const char *s) {
    for (; *s; s++) mel_da_push(v, *s);
}

static const char *kv_get(Mel_KVVec *vars, const char *key, size_t klen) {
    for (size_t i = 0; i < vars->len; i++)
        if (strlen(vars->items[i].key) == klen && strncmp(vars->items[i].key, key, klen) == 0)
            return vars->items[i].value ? vars->items[i].value : "";
    return "";
}

static char *substitute(const char *tpl, Mel_KVVec *vars) {
    Mel_CharVec out = {0};
    for (const char *p = tpl; *p;) {
        if (p[0] == '{' && p[1] == '{') {
            const char *end = strstr(p + 2, "}}");
            if (end) {
                put_str(&out, kv_get(vars, p + 2, (size_t)(end - (p + 2))));
                p = end + 2;
                continue;
            }
        }
        mel_da_push(&out, *p);
        p++;
    }
    mel_da_push(&out, 0);
    return out.items;
}

static char *find_override(const char *target_dir, const char *platform, const char *rel) {
    char *p = mel_str_fmt("%s/%s/%s", target_dir, platform, rel);
    if (mel_path_is_file(p)) return p;
    free(p);
    return NULL;
}

static char *find_by_ext(const char *dir, const char *ext) {
    DIR *d = opendir(dir);
    if (!d) return NULL;
    char  *hit = NULL;
    size_t el  = strlen(ext);
    for (struct dirent *e; (e = readdir(d));) {
        size_t nl = strlen(e->d_name);
        if (nl > el && strcmp(e->d_name + nl - el, ext) == 0) {
            hit = mel_str_dup(e->d_name);
            break;
        }
    }
    closedir(d);
    return hit;
}

static void build_vars(Mel_Target *t, const char *icon, Mel_KVVec *vars) {
    for (size_t i = 0; i < t->manifest.len; i++) mel_da_push(vars, t->manifest.items[i]);
    mel_da_push(vars, ((Mel_KV){"EXECUTABLE", t->name}));
    mel_da_push(vars, ((Mel_KV){"ICON", icon ? icon : ""}));
    bool has_version = false, has_label = false, has_bundle = false;
    for (size_t i = 0; i < t->manifest.len; i++) {
        if (strcmp(t->manifest.items[i].key, "VERSION") == 0) has_version = true;
        if (strcmp(t->manifest.items[i].key, "APP_LABEL") == 0) has_label = true;
        if (strcmp(t->manifest.items[i].key, "BUNDLE_ID") == 0) has_bundle = true;
    }
    if (!has_version) mel_da_push(vars, ((Mel_KV){"VERSION", "1.0.0"}));
    if (!has_label) mel_da_push(vars, ((Mel_KV){"APP_LABEL", t->name}));
    if (!has_bundle) mel_da_push(vars, ((Mel_KV){"BUNDLE_ID", t->name}));
}

static bool package_apple(Mel_Target *t, const char *outdir, const char *exe, const char *plat,
                          bool flat) {
    char *appdir = mel_str_fmt("%s/%s.app", outdir, t->name);
    char *macdir = flat ? mel_str_dup(appdir) : mel_str_fmt("%s/Contents/MacOS", appdir);
    char *resdir = flat ? mel_str_dup(appdir) : mel_str_fmt("%s/Contents/Resources", appdir);
    char *plistdir = flat ? mel_str_dup(appdir) : mel_str_fmt("%s/Contents", appdir);
    mel_mkdirs(macdir);
    mel_mkdirs(resdir);

    char *dstexe = mel_path_join(macdir, t->name);
    if (!mel_copy_file(exe, dstexe)) {
        fprintf(stderr, "build: package: cannot copy %s\n", exe);
        return false;
    }

    char *platdir = mel_str_fmt("%s/%s", t->dir, plat);
    char *icon    = find_by_ext(platdir, ".icns");
    if (icon) {
        char *src = mel_path_join(platdir, icon);
        char *dst = mel_path_join(resdir, icon);
        mel_copy_file(src, dst);
        free(src);
        free(dst);
    }

    Mel_KVVec vars = {0};
    build_vars(t, icon, &vars);

    char *tpl_path = find_override(t->dir, plat, "Info.plist.in");
    bool  owned    = tpl_path != NULL;
    if (!tpl_path) tpl_path = mel_str_fmt("modules/build/%s/Info.plist.in", plat);
    char *tpl = mel_read_file(tpl_path);
    if (!tpl) {
        fprintf(stderr, "build: package: missing template %s\n", tpl_path);
        return false;
    }
    char *plist     = substitute(tpl, &vars);
    char *plist_dst = mel_path_join(plistdir, "Info.plist");
    mel_write_file(plist_dst, plist);

    fprintf(stderr, "build: packaged %s%s\n", appdir, owned ? " (app override)" : "");
    return true;
}

static const char *manifest_get(Mel_Target *t, const char *key, const char *dflt) {
    for (size_t i = 0; i < t->manifest.len; i++)
        if (strcmp(t->manifest.items[i].key, key) == 0) return t->manifest.items[i].value;
    return dflt;
}

char *mel_win32_resource(Mel_Target *t, const char *outdir) {
    char *tpl_path    = find_override(t->dir, "win32", "app.rc");
    if (!tpl_path) tpl_path = mel_str_dup("modules/build/win32/app.rc");
    char *win32dir = mel_str_fmt("%s/win32", t->dir);
    char *icon     = find_by_ext(win32dir, ".ico");

    Mel_KVVec vars = {0};
    mel_da_push(&vars, ((Mel_KV){"APP_LABEL", manifest_get(t, "APP_LABEL", t->name)}));
    mel_da_push(&vars, ((Mel_KV){"VERSION", manifest_get(t, "VERSION", "1.0.0")}));
    mel_da_push(&vars, ((Mel_KV){"MANIFEST", "1 24 \"app.manifest\""}));
    mel_da_push(&vars, ((Mel_KV){"ICON", icon ? mel_str_fmt("100 ICON \"%s\"", icon) : ""}));

    char *tpl = mel_read_file(tpl_path);
    if (!tpl) {
        fprintf(stderr, "build: win32 resource: missing template %s\n", tpl_path);
        return NULL;
    }
    char *rc_text = substitute(tpl, &vars);
    mel_mkdirs(outdir);
    char *rc_file = mel_str_fmt("%s/app.rc", outdir);
    mel_write_file(rc_file, rc_text);

    char      *res = mel_str_fmt("%s/app.res.o", outdir);
    Mel_StrVec c   = {0};
    mel_da_push(&c, "x86_64-w64-mingw32-windres");
    mel_da_push(&c, rc_file);
    mel_da_push(&c, "-I");
    mel_da_push(&c, win32dir);
    mel_da_push(&c, "-I");
    mel_da_push(&c, "modules/build/win32");
    mel_da_push(&c, "-o");
    mel_da_push(&c, res);
    int rcode = mel_run_vec(&c);
    free(c.items);
    if (rcode != 0) {
        fprintf(stderr, "build: win32 resource compile failed for '%s'\n", t->name);
        return NULL;
    }
    return res;
}

bool mel_package(Mel_Target *t, const Mel_Variant *v, const char *outdir, const char *exe) {
    if (t->kind != MEL_KIND_EXECUTABLE) return true;
    if (v->platform == MEL_PLATFORM_MACOS) return package_apple(t, outdir, exe, "macos", false);
    if (v->platform == MEL_PLATFORM_IOS) return package_apple(t, outdir, exe, "ios", true);
    return true;
}
