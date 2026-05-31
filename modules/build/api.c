#include "internal.h"

#include <stdio.h>

void mel__grow(void **items, size_t *cap, size_t elem) {
    size_t next  = *cap ? *cap * 2 : 8;
    void  *grown = realloc(*items, next * elem);
    if (!grown) abort();
    *items = grown;
    *cap   = next;
}

static Mel_Target *add(Mel_Build *b, const char *name, Mel_Kind kind) {
    Mel_Target *t = calloc(1, sizeof *t);
    if (!t) abort();
    t->name = name;
    t->kind = kind;
    t->dir  = b->dir;
    mel_da_push(&b->targets, t);
    return t;
}

Mel_Target *mel_add_library(Mel_Build *b, const char *name) { return add(b, name, MEL_KIND_LIBRARY); }
Mel_Target *mel_add_executable(Mel_Build *b, const char *name) { return add(b, name, MEL_KIND_EXECUTABLE); }
Mel_Target *mel_add_third_party(Mel_Build *b, const char *name) { return add(b, name, MEL_KIND_THIRD_PARTY); }
Mel_Target *mel_add_host_tool(Mel_Build *b, const char *name) { return add(b, name, MEL_KIND_HOST_TOOL); }

Mel_Target *mel_add_test(Mel_Build *b, const char *name) {
    Mel_Target *t = add(b, name, MEL_KIND_EXECUTABLE);
    t->is_test    = true;
    return t;
}

void mel_depends(Mel_Target *t, const char *name) { mel_da_push(&t->deps, name); }
void mel_depends_host(Mel_Target *t, const char *name) { mel_da_push(&t->host_deps, name); }
void mel_unavailable(Mel_Target *t, Mel_When when) { mel_da_push(&t->unavailable, when); }

void mel_manifest(Mel_Target *t, const char *key, const char *value) {
    mel_da_push(&t->manifest, ((Mel_KV){key, value}));
}

void mel_subsystem(Mel_Target *t, const char *subsystem) {
    if (strcmp(subsystem, "console") != 0 && strcmp(subsystem, "gui") != 0) {
        fprintf(stderr, "build: mel_subsystem('%s', \"%s\"): expected \"console\" or \"gui\"\n", t->name,
                subsystem);
        abort();
    }
    t->subsystem = subsystem;
}

void mel_configure_cstd(Mel_Target *t, const char *std) { t->autotools_cstd = std; }

void mel_codegen_(Mel_Target *t, const char *tool, const char *output, ...) {
    Mel_Codegen cg = {.tool = tool, .output = output};
    va_list     ap;
    va_start(ap, output);
    for (const char *a = va_arg(ap, const char *); a; a = va_arg(ap, const char *))
        mel_da_push(&cg.args, a);
    va_end(ap);
    mel_da_push(&t->codegens, cg);
}

void mel_cmake_(Mel_Target *t, const char *dir, ...) {
    t->cmake_dir = dir;
    va_list ap;
    va_start(ap, dir);
    for (const char *a = va_arg(ap, const char *); a; a = va_arg(ap, const char *))
        mel_da_push(&t->cmake_args, a);
    va_end(ap);
}

void mel_cmake_when(Mel_Target *t, Mel_When when) { t->cmake_when = when; }

void mel_prebuilt(Mel_Target *t, Mel_When when, const char *url, const char *lib) {
    t->prebuilt_when = when;
    t->prebuilt_url  = url;
    t->prebuilt_lib  = lib;
}

void mel_configure_(Mel_Target *t, const char *dir, ...) {
    t->autotools_dir = dir;
    va_list ap;
    va_start(ap, dir);
    for (const char *a = va_arg(ap, const char *); a; a = va_arg(ap, const char *))
        mel_da_push(&t->autotools_args, a);
    va_end(ap);
}

static void push_globs(Mel_GlobVec *vec, Mel_When when, va_list ap) {
    for (const char *g = va_arg(ap, const char *); g; g = va_arg(ap, const char *))
        mel_da_push(vec, ((Mel_Glob){when, g}));
}

void mel_sources_(Mel_Target *t, Mel_When when, ...) {
    va_list ap;
    va_start(ap, when);
    push_globs(&t->sources, when, ap);
    va_end(ap);
}

void mel_exclude_source_(Mel_Target *t, Mel_When when, ...) {
    va_list ap;
    va_start(ap, when);
    push_globs(&t->excludes, when, ap);
    va_end(ap);
}

static void push_flags(Mel_FlagVec *vec, Mel_Visibility vis, Mel_When when, va_list ap) {
    for (const char *v = va_arg(ap, const char *); v; v = va_arg(ap, const char *))
        mel_da_push(vec, ((Mel_Flag){when, vis, v}));
}

void mel_cflags_(Mel_Target *t, Mel_Visibility vis, Mel_When when, ...) {
    va_list ap;
    va_start(ap, when);
    push_flags(&t->cflags, vis, when, ap);
    va_end(ap);
}

void mel_defines_(Mel_Target *t, Mel_Visibility vis, Mel_When when, ...) {
    va_list ap;
    va_start(ap, when);
    push_flags(&t->defines, vis, when, ap);
    va_end(ap);
}

void mel_includes_(Mel_Target *t, Mel_Visibility vis, Mel_When when, ...) {
    va_list ap;
    va_start(ap, when);
    push_flags(&t->includes, vis, when, ap);
    va_end(ap);
}

void mel_link_(Mel_Target *t, Mel_Visibility vis, Mel_When when, ...) {
    va_list ap;
    va_start(ap, when);
    push_flags(&t->links, vis, when, ap);
    va_end(ap);
}
