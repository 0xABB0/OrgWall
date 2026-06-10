#include "runner.h"

#include <stdio.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

typedef void (*Mel_Build_Fn)(Mel_Build*);

static int run_stdout_to_file(char* const argv[], const char* path)
{
#ifdef _WIN32
    int fd = _open(path, _O_WRONLY | _O_CREAT | _O_TRUNC, _S_IREAD | _S_IWRITE);
    if (fd < 0)
        return -1;
    int saved = _dup(1);
    _dup2(fd, 1);
    _close(fd);
    intptr_t rc = _spawnvp(_P_WAIT, argv[0], (const char* const*)argv);
    _dup2(saved, 1);
    _close(saved);
    return rc < 0 ? -1 : (int)rc;
#else
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0)
    {
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0)
            _exit(127);
        dup2(fd, 1);
        close(fd);
        execvp(argv[0], argv);
        _exit(127);
    }
    int st;
    if (waitpid(pid, &st, 0) < 0)
        return -1;
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
#endif
}

static bool loader_stale(const char* so, const char* dep)
{
    struct stat so_st;
    if (stat(so, &so_st) != 0)
        return true;
    char* text = mel_read_file(dep);
    if (!text)
        return true;
    bool  stale     = false;
    bool  saw_input = false;
    char* p         = text;
    while (!stale && *p)
    {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        {
            p++;
            continue;
        }
        if (*p == '\\' && (p[1] == '\n' || p[1] == '\r'))
        {
            p += 2;
            continue;
        }
        char* word = p;
        char* out  = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
        {
            if (*p == '\\' && p[1] == ' ')
            {
                *out++ = ' ';
                p += 2;
            }
            else if (*p == '\\' && (p[1] == '\n' || p[1] == '\r'))
                break;
            else
                *out++ = *p++;
        }
        char saved = *p;
        *out       = 0;
        if (!(out > word && out[-1] == ':'))
        {
            saw_input = true;
            struct stat in_st;
            if (stat(word, &in_st) != 0 || in_st.st_mtime >= so_st.st_mtime)
                stale = true;
        }
        if (saved == '\\')
            p += 2;
        else if (saved)
            p++;
    }
    free(text);
    return stale || !saw_input;
}

bool mel_discover_dir(Mel_Graph* g, const char* dir)
{
    char* build_c = mel_path_join(dir, "build.c");
    if (!mel_path_is_file(build_c))
    {
        free(build_c);
        return true;
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
    char* dep = mel_str_fmt("build/_loadc/%s.d", slug);
    free(slug);
    mel_mkdirs("build/_loadc");

    if (loader_stale(so, dep))
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
        int rc = mel_run((char* const*)cmd.items);
        free(cmd.items);
        if (rc != 0)
        {
            fprintf(stderr, "build: %s failed to compile\n", build_c);
            free(build_c);
            free(so);
            free(dep);
            return false;
        }

        Mel_StrVec dm = { 0 };
        mel_da_push(&dm, "clang");
        mel_da_push(&dm, "-std=c23");
        mel_da_push(&dm, "-Imodules/build");
        mel_da_push(&dm, "-MM");
        mel_da_push(&dm, "-MT");
        mel_da_push(&dm, so);
        mel_da_push(&dm, build_c);
        mel_da_push(&dm, "modules/build/api.c");
        mel_da_push(&dm, NULL);
        rc = run_stdout_to_file((char* const*)dm.items, dep);
        free(dm.items);
        if (rc != 0)
        {
            fprintf(stderr, "build: %s: dependency scan failed\n", build_c);
            free(build_c);
            free(so);
            free(dep);
            return false;
        }
    }
    free(dep);

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
            dlclose(dll);
            free(build_c);
            free(so);
            return false;
        }
        mel_da_push(&g->nodes, ((Mel_Node){ .t = t, .dll = dll, .so = so, .build_c = build_c }));
    }
    return true;
}

static bool discover_root(Mel_Graph* g, const char* root, const char* skip)
{
    DIR* d = opendir(root);
    if (!d)
        return true;
    bool ok = true;
    for (struct dirent* e; ok && (e = readdir(d));)
    {
        if (e->d_name[0] == '.')
            continue;
        if (skip && strcmp(e->d_name, skip) == 0)
            continue;
        char* sub = mel_path_join(root, e->d_name);
        if (mel_path_is_dir(sub))
            ok = mel_discover_dir(g, sub);
        free(sub);
    }
    closedir(d);
    return ok;
}

bool mel_discover(Mel_Graph* g)
{
    return discover_root(g, "modules", "build") && discover_root(g, "apps", NULL) && discover_root(g, "third-party", NULL);
}

Mel_Target* mel_graph_find(Mel_Graph* g, const char* name)
{
    for (size_t i = 0; i < g->nodes.len; i++)
        if (strcmp(g->nodes.items[i].t->name, name) == 0)
            return g->nodes.items[i].t;
    return NULL;
}
