#include "internal.h"

void mel__grow(void **items, size_t *cap, size_t elem) {
    size_t next = *cap ? *cap * 2 : 8;
    void  *grown = realloc(*items, next * elem);
    if (!grown) abort();
    *items = grown;
    *cap   = next;
}

Mel_Kind mel_kind_library(void) { return 1; }
Mel_Kind mel_kind_application(void) { return 2; }
Mel_Kind mel_kind_module(void) { return 3; }
Mel_Kind mel_kind_third_party(void) { return 4; }
Mel_Kind mel_kind_host_tool(void) { return 5; }

Mel_Stage mel_stage_configure(void) { return 1; }
Mel_Stage mel_stage_compile(void) { return 2; }
Mel_Stage mel_stage_link(void) { return 3; }
Mel_Stage mel_stage_package(void) { return 4; }
Mel_Stage mel_substage_fetch_sources(void) { return 5; }
Mel_Stage mel_substage_compile_source(void) { return 6; }

void mel_name(Mel_Build_Target *t, const char *name) { t->name = name; }

void mel_kind(Mel_Build_Target *t, Mel_Kind kind) {
    t->kind     = kind;
    t->kind_set = true;
}

void mel_depends(Mel_Build_Target *t, const char *name) { mel_da_push(&t->deps, name); }

void mel_depends_host(Mel_Build_Target *t, const char *name) { mel_da_push(&t->host_deps, name); }

void mel_unavailable(Mel_Build_Target *t, Mel_When when) { mel_da_push(&t->unavailable, when); }

void mel_manifest(Mel_Build_Target *t, const char *key, const char *value) {
    mel_da_push(&t->manifest, ((Mel_KV){key, value}));
}

void mel_enum_to_string(Mel_Build_Target *t, const char *header) {
    mel_da_push(&t->codegens, ((Mel_Codegen){header}));
}

void mel_on(Mel_Build_Target *t, Mel_Stage stage, Mel_Build_Stage_Fn fn) {
    mel_da_push(&t->hooks, ((Mel_Hook){stage, fn}));
}

void mel_suppress_default(Mel_Build_Target *t, Mel_Stage stage) {
    mel_da_push(&t->suppressed, stage);
}

static void push_globs(Mel_GlobVec *vec, Mel_When when, va_list ap) {
    for (const char *g = va_arg(ap, const char *); g; g = va_arg(ap, const char *))
        mel_da_push(vec, ((Mel_Glob){when, g}));
}

void mel_sources_(Mel_Build_Target *t, Mel_When when, ...) {
    va_list ap;
    va_start(ap, when);
    push_globs(&t->sources, when, ap);
    va_end(ap);
}

void mel_exclude_source_(Mel_Build_Target *t, Mel_When when, ...) {
    va_list ap;
    va_start(ap, when);
    push_globs(&t->excludes, when, ap);
    va_end(ap);
}

static void push_flags(Mel_FlagVec *vec, Mel_Visibility vis, Mel_When when, va_list ap) {
    for (const char *v = va_arg(ap, const char *); v; v = va_arg(ap, const char *))
        mel_da_push(vec, ((Mel_Flag){when, vis, v}));
}

void mel_cflags_(Mel_Build_Target *t, Mel_Visibility vis, Mel_When when, ...) {
    va_list ap;
    va_start(ap, when);
    push_flags(&t->cflags, vis, when, ap);
    va_end(ap);
}

void mel_defines_(Mel_Build_Target *t, Mel_Visibility vis, Mel_When when, ...) {
    va_list ap;
    va_start(ap, when);
    push_flags(&t->defines, vis, when, ap);
    va_end(ap);
}

void mel_includes_(Mel_Build_Target *t, Mel_Visibility vis, Mel_When when, ...) {
    va_list ap;
    va_start(ap, when);
    push_flags(&t->includes, vis, when, ap);
    va_end(ap);
}

void mel_link_(Mel_Build_Target *t, Mel_Visibility vis, Mel_When when, ...) {
    va_list ap;
    va_start(ap, when);
    push_flags(&t->links, vis, when, ap);
    va_end(ap);
}
