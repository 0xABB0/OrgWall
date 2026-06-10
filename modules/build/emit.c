#include "runner.h"

#ifndef _WIN32
#include <dirent.h>
#endif
#include <stdio.h>

static Mel_Toolchain g_tc;

char* mel_target_outdir(const char* target_dir, const Mel_Variant* v) { return mel_str_fmt("%s/build/%s%s-%s", target_dir, mel_platform_name(v->platform), v->simulator ? "-sim" : "", v->config); }

static char* outdir_for(Mel_Target* t, const Mel_Variant* v) { return t->kind == MEL_KIND_HOST_TOOL ? mel_str_fmt("%s/build/host", t->dir) : mel_target_outdir(t->dir, v); }

static void config_base(const char* config, Mel_StrVec* out)
{
    mel_da_push(out, mel_str_dup("-std=c23"));
    if (config && strcmp(config, "release") == 0)
    {
        mel_da_push(out, mel_str_dup("-O2"));
        mel_da_push(out, mel_str_dup("-DNDEBUG"));
    }
    else
    {
        mel_da_push(out, mel_str_dup("-g"));
        mel_da_push(out, mel_str_dup("-O0"));
    }
}

static char* obj_path(const char* outdir, const char* dir, const char* src)
{
    const char* rel = src;
    size_t      dl = strlen(dir);
    if (strncmp(src, dir, dl) == 0 && src[dl] == '/')
        rel = src + dl + 1;
    char* stem = mel_str_dup(rel);
    char* dot = strrchr(stem, '.');
    if (dot)
        *dot = 0;
    char* out = mel_str_fmt("%s/obj/%s.o", outdir, stem);
    free(stem);
    return out;
}

static void join_into(FILE* f, Mel_StrVec* v)
{
    for (size_t i = 0; i < v->len; i++)
        fprintf(f, " %s", v->items[i]);
}

static char* join_str(Mel_StrVec* v)
{
    size_t n = 1;
    for (size_t i = 0; i < v->len; i++)
        n += strlen(v->items[i]) + 1;
    char* s = malloc(n);
    s[0] = 0;
    for (size_t i = 0; i < v->len; i++)
    {
        if (i)
            strcat(s, " ");
        strcat(s, v->items[i]);
    }
    return s;
}

static bool produced_has(Mel_StrVec* produced, const char* name)
{
    for (size_t i = 0; i < produced->len; i++)
        if (strcmp(produced->items[i], name) == 0)
            return true;
    return false;
}

static bool closure_has(Mel_Graph* g, Mel_IdxVec* order, const char* name)
{
    if (!order)
        return false;
    for (size_t i = 0; i < order->len; i++)
        if (strcmp(g->nodes.items[order->items[i]].t->name, name) == 0)
            return true;
    return false;
}

static const char* host_clang_flags(void)
{
    static char buf[2048];
    static int  done = 0;
    if (done)
        return buf;
    done = 1;
    buf[0] = 0;
#if defined(__APPLE__)
    char  sdk[1024] = { 0 };
    FILE* p = popen("xcrun --show-sdk-path 2>/dev/null", "r");
    if (p)
    {
        size_t k = fread(sdk, 1, sizeof sdk - 1, p);
        pclose(p);
        while (k && (sdk[k - 1] == '\n' || sdk[k - 1] == ' '))
            k--;
        sdk[k] = 0;
    }
    char        builtin[1024] = { 0 };
    const char* base = "/opt/homebrew/opt/llvm/lib/clang";
    DIR*        d = opendir(base);
    if (d)
    {
        for (struct dirent* e; (e = readdir(d));)
        {
            if (e->d_name[0] == '.')
                continue;
            snprintf(builtin, sizeof builtin, "%s/%s/include", base, e->d_name);
            break;
        }
        closedir(d);
    }
    snprintf(buf, sizeof buf, "%s%s%s%s", sdk[0] ? "-isysroot " : "", sdk, builtin[0] ? " -isystem " : "", builtin);
#endif
    return buf;
}

static char* expand_dir(const char* s, const char* dir)
{
    const char* p = strstr(s, "$dir");
    if (!p)
        return mel_str_dup(s);
    return mel_str_fmt("%.*s%s%s", (int)(p - s), s, dir, p + 4);
}

static bool emit_codegens(FILE* f, Mel_Graph* g, Mel_Target* t, const char* outdir, Mel_StrVec* cflags, const Mel_Variant* v, Mel_StrVec* objs, Mel_StrVec* genout)
{
    char* cflags_joined = join_str(cflags);
    for (size_t c = 0; c < t->codegens.len; c++)
    {
        Mel_Codegen cg = t->codegens.items[c];
        int         ti = mel_graph_index(g, cg.tool);
        if (ti < 0)
        {
            fprintf(stderr, "build: '%s' codegen references unknown tool '%s'\n", t->name, cg.tool);
            return false;
        }
        Mel_Target* tool = g->nodes.items[ti].t;
        char*       texe = mel_str_fmt("%s/build/host/%s", tool->dir, tool->name);
        char*       genc = mel_str_fmt("%s/gen/%s", outdir, cg.output);
        if (genout)
            mel_da_push(genout, mel_str_dup(genc));

        Mel_StrVec expanded = { 0 };
        for (size_t a = 0; a < cg.args.len; a++)
        {
            const char* arg = cg.args.items[a];
            if (strcmp(arg, "$out") == 0)
                mel_da_push(&expanded, mel_str_dup(genc));
            else if (strcmp(arg, "$cflags") == 0)
                mel_da_push(&expanded, mel_str_dup(cflags_joined));
            else if (strcmp(arg, "$hostclang") == 0)
                mel_da_push(&expanded, mel_str_dup(host_clang_flags()));
            else if (strstr(arg, "$dir"))
                mel_da_push(&expanded, expand_dir(arg, t->dir));
            else
                mel_da_push(&expanded, mel_str_dup(arg));
        }
        char* cmd = join_str(&expanded);

        fprintf(f, "build %s: codegen %s", genc, texe);
        for (size_t i = 0; i < cg.inputs.len; i++)
        {
            char* in = expand_dir(cg.inputs.items[i], t->dir);
            fprintf(f, " %s", in);
            free(in);
        }
        fprintf(f, "\n  cmd = %s %s\n", texe, cmd);
        if (cg.depfile)
            fprintf(f, "  depfile = %s.d\n", genc);

        char* stem = mel_str_dup(cg.output);
        char* dot = strrchr(stem, '.');
        if (dot)
            *dot = 0;
        char* obj = mel_str_fmt("%s/obj/gen/%s.o", outdir, stem);
        fprintf(f, "build %s: cc %s\n  cflags = $%s_cflags\n", obj, genc, t->name);
        mel_da_push(objs, obj);
        (void)v;
    }
    return true;
}

#ifdef _WIN32
static char* backslashed(const char* p)
{
    char* s = mel_str_dup(p);
    for (char* c = s; *c; c++)
        if (*c == '/')
            *c = '\\';
    return s;
}
#endif

static char* emit_one(FILE* f, Mel_Graph* g, size_t idx, const Mel_Variant* v, Mel_StrVec* produced, bool* ok)
{
    Mel_Target* t = g->nodes.items[idx].t;
    if (t->kind == MEL_KIND_THIRD_PARTY && !t->cmake_dir && t->sources.len == 0)
        return NULL;

    char*      outdir = outdir_for(t, v);
    Mel_StrVec srcs = { 0 }, cflags = { 0 };
    config_base(v->config, &cflags);
    if (!mel_gather_compile(g, idx, v, &srcs, &cflags))
    {
        *ok = false;
        return NULL;
    }

    bool        host = t->kind == MEL_KIND_HOST_TOOL;
    const char* cc_rule = host ? "hostcc" : "cc";

    fprintf(f, "%s_cflags =%s", t->name, host ? "" : " $base_cflags");
    join_into(f, &cflags);
    fputc('\n', f);

    Mel_StrVec objs = { 0 }, genout = { 0 };
    if (!emit_codegens(f, g, t, outdir, &cflags, v, &objs, &genout))
    {
        *ok = false;
        return NULL;
    }

    for (size_t i = 0; i < srcs.len; i++)
    {
        const char* src = srcs.items[i];
        size_t      sl = strlen(src);
        bool        objc = (sl >= 2 && strcmp(src + sl - 2, ".m") == 0) || (sl >= 3 && strcmp(src + sl - 3, ".mm") == 0);
        bool        cpp = (sl >= 4 && strcmp(src + sl - 4, ".cpp") == 0) || (sl >= 3 && strcmp(src + sl - 3, ".cc") == 0) || (sl >= 4 && strcmp(src + sl - 4, ".cxx") == 0);
        char*       obj = obj_path(outdir, t->dir, src);
        fprintf(f, "build %s: %s %s", obj, cc_rule, src);
        if (genout.len)
        {
            fputs(" ||", f);
            for (size_t k = 0; k < genout.len; k++)
                fprintf(f, " %s", genout.items[k]);
        }
        fprintf(f, "\n  cflags = $%s_cflags%s%s\n", t->name, objc ? " -fobjc-arc" : "", cpp ? " -x c++ -std=c++17" : "");
        mel_da_push(&objs, obj);
    }

    if (!host && t->kind == MEL_KIND_EXECUTABLE && v->platform == MEL_PLATFORM_WIN32)
    {
        char* res = mel_win32_resource(t, outdir);
        if (res)
            mel_da_push(&objs, res);
    }

    char* out = NULL;
    if (t->kind == MEL_KIND_EXECUTABLE || t->kind == MEL_KIND_HOST_TOOL)
    {
        if (objs.len == 0)
        {
            fprintf(stderr, "build: '%s' has no sources on %s\n", t->name, mel_platform_name(v->platform));
            *ok = false;
            return NULL;
        }
        Mel_IdxVec  eorder = { 0 };
        Mel_IdxVec* order = NULL;
        if (!host)
        {
            if (!mel_topo_closure(g, t->name, v, &eorder))
            {
                *ok = false;
                return NULL;
            }
            order = &eorder;
        }
        Mel_StrVec ldflags = { 0 };
        mel_gather_link(g, idx, v, &ldflags);
        bool apple_ld = host || v->platform == MEL_PLATFORM_MACOS || v->platform == MEL_PLATFORM_IOS;
        if (apple_ld)
            mel_da_push(&ldflags, mel_str_dup("-dead_strip"));

        const char* ext = host ? "" : g_tc.exe_ext;
        bool        web_gui = !host && v->platform == MEL_PLATFORM_WASM && closure_has(g, order, "gui");
        if (web_gui)
        {
            ext = ".html";
            mel_da_push(&ldflags, mel_str_dup("--shell-file"));
            mel_da_push(&ldflags, mel_str_dup("modules/build/web/shell.html"));
        }
        bool android_so = !host && v->platform == MEL_PLATFORM_ANDROID && t->kind == MEL_KIND_EXECUTABLE;
        if (android_so)
        {
            mel_da_push(&ldflags, mel_str_dup("-shared"));
            mel_da_push(&ldflags, mel_str_dup("-lm"));
            mel_da_push(&ldflags, mel_str_dup("-landroid"));
        }

        bool win32_gui = !host && v->platform == MEL_PLATFORM_WIN32 && t->kind == MEL_KIND_EXECUTABLE && t->subsystem && strcmp(t->subsystem, "gui") == 0;
        if (win32_gui)
            mel_da_push(&ldflags, mel_str_dup("-Wl,/subsystem:windows"));

        if (!host && v->platform == MEL_PLATFORM_WIN32)
        {
            static const char* win32_libs[] = { "user32", "gdi32", "shell32", "ole32", "comdlg32", "comctl32", "uxtheme", "dwmapi", "winmm", "advapi32", "shlwapi", "kernel32" };
            for (size_t li = 0; li < sizeof(win32_libs) / sizeof(*win32_libs); li++)
                mel_da_push(&ldflags, mel_str_fmt("-l%s", win32_libs[li]));
        }

        Mel_StrVec libs = { 0 }, lib_deps = { 0 };
        bool       reverse_libs = !host && v->platform == MEL_PLATFORM_WASM;
        if (order)
        {
            for (size_t ii = 0; ii < order->len; ii++)
            {
                size_t      i = reverse_libs ? order->len - 1 - ii : ii;
                size_t      di = order->items[i];
                Mel_Target* d = g->nodes.items[di].t;
                if (di != idx && d->kind == MEL_KIND_LIBRARY && produced_has(produced, d->name))
                {
                    char* od = mel_target_outdir(d->dir, v);
                    char* lib = mel_str_fmt("%s/lib%s.a", od, d->name);
                    bool  whole = false;
                    for (size_t k = 0; k < d->whole_archive.len && !whole; k++)
                        whole = mel_when_match(d->whole_archive.items[k], v);
                    if (whole && apple_ld)
                        mel_da_push(&libs, mel_str_fmt("-Wl,-force_load,%s", lib));
                    else if (whole)
                    {
                        mel_da_push(&libs, mel_str_dup("-Wl,--whole-archive"));
                        mel_da_push(&libs, mel_str_dup(lib));
                        mel_da_push(&libs, mel_str_dup("-Wl,--no-whole-archive"));
                    }
                    else
                        mel_da_push(&libs, mel_str_dup(lib));
                    mel_da_push(&lib_deps, mel_str_dup(lib));
                    free(lib);
                    free(od);
                }
            }
        }

        char* bin = android_so ? mel_str_fmt("%s/libmelody.so", outdir) : mel_str_fmt("%s/%s%s", outdir, t->name, ext);
        fprintf(f, "build %s: %s", bin, host ? "hostlink" : "link");
        join_into(f, &objs);
        if (lib_deps.len)
        {
            fputs(" |", f);
            join_into(f, &lib_deps);
        }
        fprintf(f, "\n  libs =");
        join_into(f, &libs);
        fprintf(f, "\n  ldflags =%s", host ? "" : " $base_ldflags");
        join_into(f, &ldflags);
        fputc('\n', f);
        out = mel_str_dup(bin);
        free(eorder.items);
    }
    else if (t->kind == MEL_KIND_LIBRARY)
    {
        if (objs.len)
        {
            char* lib = mel_str_fmt("%s/lib%s.a", outdir, t->name);
            fprintf(f, "build %s: ar", lib);
            join_into(f, &objs);
            fputc('\n', f);
#ifdef _WIN32
            char* bs = backslashed(lib);
            fprintf(f, "  out_win = %s\n", bs);
            free(bs);
#endif
            mel_da_push(produced, t->name);
            out = mel_str_dup(lib);
        }
        else
        {
            bool globbed = false;
            for (size_t i = 0; i < t->sources.len; i++)
                if (mel_when_match(t->sources.items[i].when, v))
                    globbed = true;
            if (globbed)
            {
                fprintf(stderr, "build: library '%s' matched no sources on %s\n", t->name, mel_platform_name(v->platform));
                *ok = false;
                return NULL;
            }
        }
    }
    fputc('\n', f);
    return out;
}

bool mel_emit_and_build(Mel_Graph* g, const char* root, const Mel_Variant* v, bool run_ninja, bool do_package, char** out_bin)
{
    int ri = mel_graph_index(g, root);
    if (ri < 0)
    {
        fprintf(stderr, "build: unknown target '%s'\n", root);
        return false;
    }
    Mel_Target* rootT = g->nodes.items[ri].t;

    Mel_IdxVec rorder = { 0 };
    if (!mel_topo_closure(g, root, v, &rorder))
    {
        free(rorder.items);
        return false;
    }
    for (size_t i = 0; i < rorder.len; i++)
    {
        Mel_Target* d = g->nodes.items[rorder.items[i]].t;
        if (!mel_target_available(d, v))
        {
            fprintf(stderr, "build: '%s' (needed by '%s') is unavailable on %s\n", d->name, root, mel_platform_name(v->platform));
            free(rorder.items);
            return false;
        }
    }

    char* vdir = mel_str_fmt("build/%s%s-%s", mel_platform_name(v->platform), v->simulator ? "-sim" : "", v->config);
    mel_mkdirs(vdir);
    static bool locked = false;
    if (!locked)
    {
        if (!mel_lock_dir(vdir))
        {
            free(rorder.items);
            return false;
        }
        bool removed = remove(".ninja_log") == 0;
        removed = remove(".ninja_deps") == 0 || removed;
        if (removed)
            fprintf(stderr, "build: removed legacy repo-root ninja state\n");
        locked = true;
    }

    if (!mel_prepare_thirdparty(g, &rorder, v))
    {
        free(rorder.items);
        return false;
    }
    static bool injected = false;
    if (!injected)
    {
        mel_inject_thirdparty(g, v);
        injected = true;
    }

    char* ninja_path = mel_str_fmt("%s/build.ninja", vdir);
    FILE* f = fopen(ninja_path, "wb");
    if (!f)
    {
        fprintf(stderr, "build: cannot write %s\n", ninja_path);
        return false;
    }

    g_tc = mel_toolchain(v);
    fprintf(f, "builddir = %s\n", vdir);
    fprintf(f, "cc = %s\n", g_tc.cc);
    fprintf(f, "ar = %s\n", g_tc.ar);
    fprintf(f, "base_cflags = %s\n", g_tc.base_cflags);
    fprintf(f, "base_ldflags = %s\n\n", g_tc.base_ldflags);

    fputs("rule cc\n  command = $cc $cflags -MMD -MF $out.d -c $in -o $out\n", f);
    fputs("  depfile = $out.d\n  deps = gcc\n  description = CC $out\n\n", f);
    fputs("rule hostcc\n  command = clang $cflags -MMD -MF $out.d -c $in -o $out\n", f);
    fputs("  depfile = $out.d\n  deps = gcc\n  description = CC(host) $out\n\n", f);
#ifdef _WIN32
    fputs("rule ar\n  command = cmd /c del /f /q $out_win 2>nul & $ar rcs $out $in\n  description = AR $out\n\n", f);
#else
    fputs("rule ar\n  command = rm -f $out && $ar rcs $out $in\n  description = AR $out\n\n", f);
#endif
    fputs("rule link\n  command = $cc $in $libs $ldflags -o $out\n  description = LINK $out\n\n", f);
    fputs("rule hostlink\n  command = clang $in $libs $ldflags -o $out\n  description = LINK(host) $out\n\n", f);
    fputs("rule codegen\n  command = $cmd\n  description = GEN $out\n\n", f);

    Mel_IdxVec all = { 0 };
    if (!mel_topo_all(g, v, &all))
    {
        fclose(f);
        free(all.items);
        free(rorder.items);
        return false;
    }

    char* skip = calloc(g->nodes.len, 1);
    if (!skip)
        abort();
    size_t nskip = 0;
    for (size_t oi = 0; oi < all.len; oi++)
    {
        size_t      i = all.items[oi];
        Mel_Target* t = g->nodes.items[i].t;
        if (!mel_target_available(t, v))
            skip[i] = 1;
        for (size_t k = 0; !skip[i] && k < t->deps.len; k++)
        {
            Mel_Dep dep = t->deps.items[k];
            if (!mel_when_match(dep.when, v))
                continue;
            int j = mel_graph_index(g, dep.name);
            if (j >= 0 && skip[j])
                skip[i] = 1;
        }
        if (skip[i])
            nskip++;
    }
    if (nskip)
        fprintf(stderr, "build: %zu target(s) unavailable on %s\n", nskip, mel_platform_name(v->platform));

    Mel_StrVec produced = { 0 };
    char*      root_out = NULL;
    bool       ok = true;
    for (size_t oi = 0; oi < all.len && ok; oi++)
    {
        size_t i = all.items[oi];
        if (skip[i])
            continue;
        char* out = emit_one(f, g, i, v, &produced, &ok);
        if (!out)
            continue;
        fprintf(f, "build %s: phony %s\n\n", g->nodes.items[i].t->name, out);
        if (i == (size_t)ri)
            root_out = out;
    }
    fclose(f);
    free(skip);
    free(all.items);
    if (!ok)
    {
        free(rorder.items);
        return false;
    }

    if (out_bin)
        *out_bin = root_out ? mel_str_dup(root_out) : NULL;

    if (!run_ninja)
    {
        fprintf(stderr, "build: configured %s\n", ninja_path);
        free(rorder.items);
        return true;
    }

    if (!root_out)
    {
        free(rorder.items);
        if (rootT->kind == MEL_KIND_THIRD_PARTY)
            return true;
        fprintf(stderr, "build: '%s' produced no outputs on %s\n", root, mel_platform_name(v->platform));
        return false;
    }

    fprintf(stderr, "build: emitted %s\n", ninja_path);
    Mel_StrVec cmd = { 0 };
    mel_da_push(&cmd, "ninja");
    mel_da_push(&cmd, "-f");
    mel_da_push(&cmd, ninja_path);
    mel_da_push(&cmd, root_out);
    int rc = mel_run_vec(&cmd);
    free(cmd.items);
    if (rc != 0)
    {
        free(rorder.items);
        return false;
    }

    Mel_StrVec cd = { 0 };
    mel_da_push(&cd, "ninja");
    mel_da_push(&cd, "-f");
    mel_da_push(&cd, ninja_path);
    mel_da_push(&cd, "-t");
    mel_da_push(&cd, "cleandead");
    mel_da_push(&cd, NULL);
    mel_run_quiet((char* const*)cd.items);
    free(cd.items);

    if (do_package && rootT->kind == MEL_KIND_EXECUTABLE)
    {
        char* root_outdir = outdir_for(rootT, v);
        mel_mkdirs(root_outdir);
        mel_package(g, &rorder, rootT, v, root_outdir, root_out);
        free(root_outdir);
    }
    free(rorder.items);
    return true;
}
