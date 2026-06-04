#include "runner.h"

#include <stdio.h>
#ifndef _WIN32
#include <dirent.h>
#include <dlfcn.h>
#endif

typedef void (*Mel_Build_Fn)(Mel_Build*);

bool mel_discover_dir(Mel_Graph* g, const char* dir)
{
    char* build_c = mel_path_join(dir, "build.c");
    if (!mel_path_is_file(build_c))
    {
        free(build_c);
        return false;
    }

    char* slug = mel_str_dup(dir);
    for (char* s = slug; *s; s++)
        if (*s == '/' || *s == '\\')
            *s = '_';
#ifdef _WIN32
    char* so = mel_str_fmt("build/_loadc/%s.dll", slug);
#else
    char* so = mel_str_fmt("build/_loadc/%s.so", slug);
#endif
    free(slug);
    mel_mkdirs("build/_loadc");

    const char* inputs[] = { build_c, "modules/build/api.c", "modules/build/internal.h", "modules/build/build.h" };
    if (nob_needs_rebuild(so, inputs, sizeof(inputs) / sizeof(inputs[0])) != 0)
    {
        Mel_StrVec cmd = { 0 };
        mel_da_push(&cmd, "clang");
        mel_da_push(&cmd, "-std=c23");
        mel_da_push(&cmd, "-shared");
#ifndef _WIN32
        mel_da_push(&cmd, "-fPIC");
#endif
        mel_da_push(&cmd, "-Imodules/build");
#ifdef _WIN32
        mel_da_push(&cmd, "-Wl,/export:build");
#endif
        mel_da_push(&cmd, "-o");
        mel_da_push(&cmd, so);
        mel_da_push(&cmd, build_c);
        mel_da_push(&cmd, "modules/build/api.c");
        mel_da_push(&cmd, NULL);
        int rc = mel_run_quiet((char* const*)cmd.items);
        free(cmd.items);
        if (rc != 0)
        {
            free(build_c);
            free(so);
            return false;
        }
    }

    void* dll = dlopen(so, RTLD_NOW | RTLD_LOCAL);
    if (!dll)
    {
        fprintf(stderr, "build: dlopen %s: %s\n", so, dlerror());
        free(build_c);
        free(so);
        return false;
    }

    Mel_Build_Fn build_fn = (Mel_Build_Fn)dlsym(dll, "build");
    if (!build_fn)
    {
        fprintf(stderr, "build: %s: missing build()\n", build_c);
        dlclose(dll);
        free(build_c);
        free(so);
        return false;
    }

    Mel_Build b = { .dir = mel_str_dup(dir) };
    build_fn(&b);
    if (b.targets.len == 0)
    {
        fprintf(stderr, "build: %s: declared no targets\n", build_c);
        dlclose(dll);
        free(build_c);
        free(so);
        return false;
    }

    for (size_t i = 0; i < b.targets.len; i++)
    {
        Mel_Target* t = b.targets.items[i];
        if (!t->name)
        {
            fprintf(stderr, "build: %s: a target has no name\n", build_c);
            continue;
        }
        mel_da_push(&g->nodes, ((Mel_Node){ .t = t, .dll = dll, .so = so, .build_c = build_c }));
    }
    return true;
}

static void discover_root(Mel_Graph* g, const char* root, const char* skip)
{
    DIR* d = opendir(root);
    if (!d)
        return;
    for (struct dirent* e; (e = readdir(d));)
    {
        if (e->d_name[0] == '.')
            continue;
        if (skip && strcmp(e->d_name, skip) == 0)
            continue;
        char* sub = mel_path_join(root, e->d_name);
        if (mel_path_is_dir(sub))
            mel_discover_dir(g, sub);
        free(sub);
    }
    closedir(d);
}

void mel_discover(Mel_Graph* g)
{
    discover_root(g, "modules", "build");
    discover_root(g, "apps", NULL);
    discover_root(g, "third-party", NULL);
}

Mel_Target* mel_graph_find(Mel_Graph* g, const char* name)
{
    for (size_t i = 0; i < g->nodes.len; i++)
        if (strcmp(g->nodes.items[i].t->name, name) == 0)
            return g->nodes.items[i].t;
    return NULL;
}
