#include "runner.h"

#include <stdio.h>
#ifndef _WIN32
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifdef _WIN32
static void ensure_llvm_on_path(void)
{
    static bool done = false;
    if (done)
        return;
    done = true;
    FILE* p = popen("where clang", "r");
    if (!p)
        return;
    char buf[1024];
    if (fgets(buf, sizeof buf, p))
    {
        size_t n = strlen(buf);
        while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' '))
            buf[--n] = 0;
        char* slash = strrchr(buf, '\\');
        if (!slash)
            slash = strrchr(buf, '/');
        if (slash)
        {
            *slash = 0;
            const char* old = getenv("PATH");
            _putenv(mel_str_fmt("PATH=%s;%s", buf, old ? old : ""));
        }
    }
    pclose(p);
}
#endif

static char* abspath(const char* rel)
{
    char* cwd = malloc(1 << 14);
    if (!getcwd(cwd, 1 << 14))
    {
        free(cwd);
        return mel_str_dup(rel);
    }
    char* p = rel[0] == '/' ? mel_str_dup(rel) : mel_str_fmt("%s/%s", cwd, rel);
    free(cwd);
#ifdef _WIN32
    for (char* s = p; *s; s++)
        if (*s == '\\')
            *s = '/';
#endif
    return p;
}

static bool is_real_dir(const char* p)
{
#ifdef _WIN32
    DWORD a = GetFileAttributesA(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) && !(a & FILE_ATTRIBUTE_REPARSE_POINT);
#else
    struct stat st;
    return lstat(p, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

static void wipe_tree(const char* p)
{
    if (is_real_dir(p))
    {
        DIR* d = opendir(p);
        if (d)
        {
            for (struct dirent* e; (e = readdir(d));)
            {
                if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
                    continue;
                char* child = mel_path_join(p, e->d_name);
                wipe_tree(child);
                free(child);
            }
            closedir(d);
        }
#ifdef _WIN32
        _rmdir(p);
#else
        rmdir(p);
#endif
    }
    else
        remove(p);
}

static bool fingerprint_matches(const char* stamp, const char* fp)
{
    char* old = mel_read_file(stamp);
    if (!old)
        return false;
    bool match = strcmp(old, fp) == 0;
    free(old);
    return match;
}

#ifdef _WIN32
static void mirror_archives_to_lib(const char* absprefix)
{
    char* libdir = mel_str_fmt("%s/lib", absprefix);
    DIR*  d = opendir(libdir);
    if (d)
    {
        for (struct dirent* e; (e = readdir(d));)
        {
            const char* n = e->d_name;
            size_t      len = strlen(n);
            if (len > 5 && strncmp(n, "lib", 3) == 0 && strcmp(n + len - 2, ".a") == 0)
            {
                char* stem = mel_str_fmt("%.*s", (int)(len - 5), n + 3);
                char* src = mel_str_fmt("%s/%s", libdir, n);
                char* dst = mel_str_fmt("%s/%s.lib", libdir, stem);
                if (!mel_path_is_file(dst))
                    mel_copy_file(src, dst);
                free(stem);
                free(src);
                free(dst);
            }
        }
        closedir(d);
    }
    free(libdir);
}
#endif

static void inject_prefix(Mel_Target* t, const char* absprefix, const Mel_Variant* v)
{
    mel_da_push(&t->includes, ((Mel_Flag){ (Mel_When){ 0 }, MEL_PUBLIC, mel_str_fmt("%s/include", absprefix) }));
    mel_da_push(&t->links, ((Mel_Flag){ (Mel_When){ 0 }, MEL_PUBLIC, mel_str_fmt("-L%s/lib", absprefix) }));
    if (v->platform != MEL_PLATFORM_WIN32)
        mel_da_push(&t->links, ((Mel_Flag){ (Mel_When){ 0 }, MEL_PUBLIC, mel_str_fmt("-Wl,-rpath,%s/lib", absprefix) }));
    char* lto = mel_str_fmt("%s/lib/libLTO.dylib", absprefix);
    if (mel_path_is_file(lto))
        mel_da_push(&t->links, ((Mel_Flag){ (Mel_When){ 0 }, MEL_PUBLIC, mel_str_fmt("-Wl,-lto_library,%s", lto) }));
    free(lto);
#ifdef _WIN32
    mirror_archives_to_lib(absprefix);
#endif
}

static char* dep_prefix_abs(Mel_Graph* g, const char* name, const Mel_Variant* v)
{
    int i = mel_graph_index(g, name);
    if (i < 0)
        return NULL;
    char* outdir = mel_target_outdir(g->nodes.items[i].t->dir, v);
    char* p = abspath(mel_path_join(outdir, "prefix"));
    free(outdir);
    return p;
}

static bool build_cmake(Mel_Target* t, const Mel_Variant* v)
{
    char* outdir = mel_target_outdir(t->dir, v);
    char* absprefix = abspath(mel_path_join(outdir, "prefix"));
    char* stamp = mel_path_join(outdir, ".thirdparty-built");
    char* bld = mel_path_join(outdir, "cmake");
    char* src = t->cmake_dir ? mel_path_join(t->dir, t->cmake_dir) : mel_str_dup(t->dir);

    Mel_StrVec c = { 0 };
    mel_da_push(&c, "cmake");
    mel_da_push(&c, "-G");
    mel_da_push(&c, "Ninja");
    mel_da_push(&c, "-S");
    mel_da_push(&c, src);
    mel_da_push(&c, "-B");
    mel_da_push(&c, bld);
    mel_da_push(&c, mel_str_fmt("-DCMAKE_INSTALL_PREFIX=%s", absprefix));
    mel_da_push(&c, v->config && strcmp(v->config, "release") == 0 ? "-DCMAKE_BUILD_TYPE=Release" : "-DCMAKE_BUILD_TYPE=Debug");
    for (size_t i = 0; i < t->cmake_args.len; i++)
        mel_da_push(&c, t->cmake_args.items[i]);

    Mel_Toolchain tc = mel_toolchain(v);
    char*         fp = mel_str_fmt("kind=cmake\ncc=%s\ntriple=%s\nconfig=%s\n", tc.cc, tc.triple, v->config ? v->config : "");
    for (size_t i = 0; i < c.len; i++)
        fp = mel_str_fmt("%sarg=%s\n", fp, c.items[i]);

    bool fresh = fingerprint_matches(stamp, fp);
    bool ok = true;
    if (!fresh)
    {
        remove(stamp);
        wipe_tree(bld);
        mel_mkdirs(bld);
        ok = mel_run_vec(&c) == 0;
    }
    if (ok)
    {
        c.len = 0;
        mel_da_push(&c, "cmake");
        mel_da_push(&c, "--build");
        mel_da_push(&c, bld);
        ok = mel_run_vec(&c) == 0;
    }
    if (ok)
    {
        c.len = 0;
        mel_da_push(&c, "cmake");
        mel_da_push(&c, "--install");
        mel_da_push(&c, bld);
        ok = mel_run_vec(&c) == 0;
    }
    free(c.items);
    if (!ok)
    {
        fprintf(stderr, "build: cmake failed for '%s'\n", t->name);
        return false;
    }
    if (!fresh)
        mel_write_file(stamp, fp);
    return true;
}

static bool build_autotools(Mel_Graph* g, Mel_Target* t, const Mel_Variant* v)
{
    char* outdir = mel_target_outdir(t->dir, v);
    char* absprefix = abspath(mel_path_join(outdir, "prefix"));
    char* stamp = mel_path_join(outdir, ".thirdparty-built");
    char* bld = mel_path_join(outdir, "autotools");
    char* abssrc = abspath(t->autotools_dir && strcmp(t->autotools_dir, ".") != 0 ? mel_path_join(t->dir, t->autotools_dir) : t->dir);
#ifdef _WIN32
    ensure_llvm_on_path();
#endif
    Mel_Toolchain tc = mel_toolchain(v);

    Mel_StrVec cpp = { 0 }, ld = { 0 };
    for (size_t i = 0; i < t->deps.len; i++)
    {
        Mel_Dep dep = t->deps.items[i];
        if (!mel_when_match(dep.when, v))
            continue;
        char* dp = dep_prefix_abs(g, dep.name, v);
        if (dp)
        {
            mel_da_push(&cpp, mel_str_fmt("-I%s/include", dp));
            mel_da_push(&ld, mel_str_fmt("-L%s/lib", dp));
        }
    }

    const char* cc_cfg = tc.autotools_cc;
    const char* srcpath = abssrc;
#ifdef _WIN32
    srcpath = (t->autotools_dir && strcmp(t->autotools_dir, ".") != 0) ? mel_str_fmt("../../../%s", t->autotools_dir) : "../../..";
#endif

    const char* cc_extra = v->platform == MEL_PLATFORM_ANDROID ? " -fPIC" : "";

    char* cfg = mel_str_fmt("'%s/configure' --prefix='%s' --disable-shared --enable-static", srcpath, absprefix);
    if (tc.cross)
        cfg = mel_str_fmt("%s --host=%s", cfg, tc.triple);
    if (v->platform == MEL_PLATFORM_ANDROID)
        cfg = mel_str_fmt("%s --disable-dependency-tracking", cfg);
    if (tc.cross || v->platform == MEL_PLATFORM_WIN32)
    {
        cfg = t->autotools_cstd ? mel_str_fmt("%s CC='%s -std=%s%s'", cfg, cc_cfg, t->autotools_cstd, cc_extra) : mel_str_fmt("%s CC='%s%s'", cfg, cc_cfg, cc_extra);
    }
    // Emscripten objects are wasm/bitcode; the host ar/ranlib choke on them
    // ("malformed uleb128"). Hand autotools/libtool the emscripten archiver
    // and index tool so `make install` indexes the static archive correctly.
    if (v->platform == MEL_PLATFORM_WASM)
        cfg = mel_str_fmt("%s AR=emar RANLIB=emranlib", cfg);
#ifdef _WIN32
    cfg = mel_str_fmt("%s AR=llvm-ar RANLIB=llvm-ranlib NM=llvm-nm LD=ld.lld", cfg);
#endif
    if (cpp.len)
    {
        char* s = mel_str_dup(cpp.items[0]);
        for (size_t i = 1; i < cpp.len; i++)
            s = mel_str_fmt("%s %s", s, cpp.items[i]);
        cfg = mel_str_fmt("%s CPPFLAGS='%s'", cfg, s);
    }
    if (ld.len)
    {
        char* s = mel_str_dup(ld.items[0]);
        for (size_t i = 1; i < ld.len; i++)
            s = mel_str_fmt("%s %s", s, ld.items[i]);
        cfg = mel_str_fmt("%s LDFLAGS='%s'", cfg, s);
    }
    for (size_t i = 0; i < t->autotools_args.len; i++)
        cfg = mel_str_fmt("%s %s", cfg, t->autotools_args.items[i]);

    char* fp = mel_str_fmt("kind=autotools\ncc=%s\ntriple=%s\nconfig=%s\ncmd=%s\n", tc.cc, tc.triple, v->config ? v->config : "", cfg);

    bool       fresh = fingerprint_matches(stamp, fp);
    bool       ok = true;
    Mel_StrVec c = { 0 };
    if (!fresh)
    {
        remove(stamp);
        wipe_tree(bld);
        mel_mkdirs(bld);
        char* script = mel_path_join(bld, "_mel_configure.sh");
        mel_write_file(script, mel_str_fmt("%s\n", cfg));
        mel_da_push(&c, "sh");
        mel_da_push(&c, "_mel_configure.sh");
        ok = mel_run_cwd(bld, &c) == 0;
    }
    if (ok)
    {
        c.len = 0;
        mel_da_push(&c, "make");
        mel_da_push(&c, "-j8");
        ok = mel_run_cwd(bld, &c) == 0;
    }
    if (ok)
    {
        c.len = 0;
        mel_da_push(&c, "make");
        mel_da_push(&c, "install");
        ok = mel_run_cwd(bld, &c) == 0;
    }
    free(c.items);
    if (!ok)
    {
        fprintf(stderr, "build: autotools failed for '%s'\n", t->name);
        return false;
    }
    if (!fresh)
        mel_write_file(stamp, fp);
    return true;
}

static bool build_prebuilt(Mel_Target* t, const Mel_Prebuilt* pb, const Mel_Variant* v)
{
    char* outdir = mel_target_outdir(t->dir, v);
    char* absprefix = abspath(mel_path_join(outdir, "prefix"));
    char* stamp = mel_path_join(outdir, ".thirdparty-built");
    char* lib = pb->lib ? mel_str_fmt("%s/lib/%s", absprefix, pb->lib) : NULL;

    Mel_Toolchain tc = mel_toolchain(v);
    char*         fp = mel_str_fmt("kind=prebuilt\ncc=%s\ntriple=%s\nconfig=%s\nurl=%s\nlib=%s\n", tc.cc, tc.triple, v->config ? v->config : "", pb->url, pb->lib ? pb->lib : "");

    if (fingerprint_matches(stamp, fp) && (!lib || mel_path_is_file(lib)))
        return true;
    remove(stamp);
    wipe_tree(absprefix);
    mel_mkdirs(absprefix);

    size_t      ulen = strlen(pb->url);
    bool        is_tar = (ulen > 7 && strcmp(pb->url + ulen - 7, ".tar.xz") == 0) ||
                  (ulen > 7 && strcmp(pb->url + ulen - 7, ".tar.gz") == 0) ||
                  (ulen > 4 && strcmp(pb->url + ulen - 4, ".tgz") == 0);
    const char* archive_name = is_tar ? "_prebuilt.tar" : "_prebuilt.zip";
    char*       archive = mel_path_join(absprefix, archive_name);

    Mel_StrVec c = { 0 };
    mel_da_push(&c, "curl");
    mel_da_push(&c, "-fSL");
    mel_da_push(&c, "-o");
    mel_da_push(&c, archive);
    mel_da_push(&c, pb->url);
    bool ok = mel_run_vec(&c) == 0;
    if (ok)
    {
        c.len = 0;
        if (is_tar)
        {
            mel_da_push(&c, "tar");
            mel_da_push(&c, "--strip-components=1");
            mel_da_push(&c, "-xf");
            mel_da_push(&c, archive);
            mel_da_push(&c, "-C");
            mel_da_push(&c, absprefix);
        }
        else
        {
#ifdef _WIN32
            mel_da_push(&c, "tar");
            mel_da_push(&c, "-xf");
            mel_da_push(&c, archive);
            mel_da_push(&c, "-C");
            mel_da_push(&c, absprefix);
#else
            mel_da_push(&c, "unzip");
            mel_da_push(&c, "-oq");
            mel_da_push(&c, archive);
            mel_da_push(&c, "-d");
            mel_da_push(&c, absprefix);
#endif
        }
        ok = mel_run_vec(&c) == 0;
    }
    free(c.items);
    if (!ok || (lib && !mel_path_is_file(lib)))
    {
        fprintf(stderr, "build: prebuilt fetch failed for '%s'\n", t->name);
        return false;
    }
    mel_write_file(stamp, fp);
    return true;
}

void mel_inject_thirdparty(Mel_Graph* g, const Mel_Variant* v)
{
    for (size_t i = 0; i < g->nodes.len; i++)
    {
        Mel_Target* t = g->nodes.items[i].t;
        if (t->kind != MEL_KIND_THIRD_PARTY || !mel_target_available(t, v))
            continue;
        char* outdir = mel_target_outdir(t->dir, v);
        char* absprefix = abspath(mel_path_join(outdir, "prefix"));
        inject_prefix(t, absprefix, v);
        free(absprefix);
        free(outdir);
    }
}

bool mel_prepare_thirdparty(Mel_Graph* g, Mel_IdxVec* order, const Mel_Variant* v)
{
    for (size_t i = 0; i < order->len; i++)
    {
        Mel_Target* t = g->nodes.items[order->items[i]].t;
        if (t->kind != MEL_KIND_THIRD_PARTY || !mel_target_available(t, v))
            continue;
        const Mel_Prebuilt* pb = NULL;
        for (size_t j = 0; j < t->prebuilts.len && !pb; j++)
            if (mel_when_match(t->prebuilts.items[j].when, v))
                pb = &t->prebuilts.items[j];
        if (pb)
        {
            if (!build_prebuilt(t, pb, v))
                return false;
            continue;
        }
        if (t->cmake_dir && mel_when_match(t->cmake_when, v) && !build_cmake(t, v))
            return false;
        if (t->autotools_dir && !build_autotools(g, t, v))
            return false;
    }
    return true;
}
