#include <clang-c/Index.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *name;
    char *label;
    long long value;
    int   skip;
} Item;

typedef struct {
    char  *fn;
    char  *ret;
    char  *param;
    char  *def;
    Item  *items;
    size_t n, cap;
} Func;

static Func  *g_funcs;
static size_t g_func_n, g_func_cap;

static char *dup_cstr(const char *s) {
    size_t n = strlen(s) + 1;
    char  *p = malloc(n);
    memcpy(p, s, n);
    return p;
}

static char *dup_cx(CXString s) {
    char *p = dup_cstr(clang_getCString(s));
    clang_disposeString(s);
    return p;
}

static int func_seen(const char *fn) {
    for (size_t i = 0; i < g_func_n; i++) if (strcmp(g_funcs[i].fn, fn) == 0) return 1;
    return 0;
}

static Func *func_push(void) {
    if (g_func_n == g_func_cap) {
        g_func_cap = g_func_cap ? g_func_cap * 2 : 8;
        g_funcs = realloc(g_funcs, g_func_cap * sizeof *g_funcs);
    }
    Func *f = &g_funcs[g_func_n++];
    memset(f, 0, sizeof *f);
    return f;
}

static Item *item_push(Func *f) {
    if (f->n == f->cap) {
        f->cap = f->cap ? f->cap * 2 : 8;
        f->items = realloc(f->items, f->cap * sizeof *f->items);
    }
    Item *it = &f->items[f->n++];
    memset(it, 0, sizeof *it);
    return it;
}

static enum CXChildVisitResult visit_item(CXCursor c, CXCursor parent, CXClientData d) {
    (void)parent;
    Item *it = d;
    if (clang_getCursorKind(c) == CXCursor_AnnotateAttr) {
        char *a = dup_cx(clang_getCursorSpelling(c));
        if (strncmp(a, "mel:str:", 8) == 0)    it->label = dup_cstr(a + 8);
        else if (strcmp(a, "mel:skip") == 0)    it->skip = 1;
        free(a);
    }
    return CXChildVisit_Continue;
}

static enum CXChildVisitResult visit_enum(CXCursor c, CXCursor parent, CXClientData d) {
    (void)parent;
    Func *f = d;
    if (clang_getCursorKind(c) == CXCursor_EnumConstantDecl) {
        Item *it = item_push(f);
        it->name  = dup_cx(clang_getCursorSpelling(c));
        it->value = clang_getEnumConstantDeclValue(c);
        clang_visitChildren(c, visit_item, it);
    }
    return CXChildVisit_Continue;
}

typedef struct { int found; char *def; } Marker;

static enum CXChildVisitResult visit_marker(CXCursor c, CXCursor parent, CXClientData d) {
    (void)parent;
    Marker *m = d;
    if (clang_getCursorKind(c) == CXCursor_AnnotateAttr) {
        char *a = dup_cx(clang_getCursorSpelling(c));
        if (strncmp(a, "mel:enum_to_string", 18) == 0) {
            m->found = 1;
            m->def = dup_cstr(a[18] == ':' ? a + 19 : "?");
        }
        free(a);
    }
    return CXChildVisit_Continue;
}

static enum CXChildVisitResult visit_tu(CXCursor c, CXCursor parent, CXClientData d) {
    (void)parent;
    (void)d;
    if (clang_getCursorKind(c) != CXCursor_FunctionDecl) return CXChildVisit_Recurse;
    if (clang_Cursor_getNumArguments(c) != 1) return CXChildVisit_Continue;

    Marker m = {0};
    clang_visitChildren(c, visit_marker, &m);
    if (!m.found) { free(m.def); return CXChildVisit_Continue; }

    char *fn = dup_cx(clang_getCursorSpelling(c));
    if (func_seen(fn)) { free(fn); free(m.def); return CXChildVisit_Continue; }

    CXType param = clang_getCursorType(clang_Cursor_getArgument(c, 0));
    CXCursor enum_decl = clang_getTypeDeclaration(clang_getCanonicalType(param));
    if (clang_getCursorKind(enum_decl) != CXCursor_EnumDecl) {
        fprintf(stderr, "enum_str_gen: %s: parameter is not an enum\n", fn);
        free(fn); free(m.def);
        return CXChildVisit_Continue;
    }

    Func *f = func_push();
    f->fn    = fn;
    f->ret   = dup_cx(clang_getTypeSpelling(clang_getCursorResultType(c)));
    f->param = dup_cx(clang_getTypeSpelling(param));
    f->def   = m.def;
    clang_visitChildren(enum_decl, visit_enum, f);
    return CXChildVisit_Continue;
}

static size_t common_prefix_len(const Func *f) {
    const char *base = NULL;
    size_t len = 0;
    for (size_t i = 0; i < f->n; i++) {
        if (f->items[i].skip) continue;
        const char *name = f->items[i].name;
        if (!base) { base = name; len = strlen(name); continue; }
        size_t j = 0;
        while (j < len && name[j] && base[j] == name[j]) j++;
        len = j;
    }
    while (len > 0 && base[len - 1] != '_') len--;
    return len;
}

static void write_str8_literal(FILE *f, const char *s) {
    fputs("(str8){ (u8*)\"", f);
    for (const char *p = s; *p; p++) {
        if (*p == '"' || *p == '\\') fputc('\\', f);
        fputc(*p, f);
    }
    fprintf(f, "\", %zu }", strlen(s));
}

static int emit(const char *outc, char **spellings, int spelling_n) {
    FILE *c = fopen(outc, "w");
    if (!c) { fprintf(stderr, "enum_str_gen: cannot open %s\n", outc); return 1; }
    for (int i = 0; i < spelling_n; i++) fprintf(c, "#include <%s>\n", spellings[i]);
    fputc('\n', c);

    for (size_t i = 0; i < g_func_n; i++) {
        Func  *f   = &g_funcs[i];
        size_t pfx = common_prefix_len(f);
        fprintf(c, "%s %s(%s v) {\n    switch (v) {\n", f->ret, f->fn, f->param);
        long long *seen = malloc((f->n + 1) * sizeof *seen);
        size_t     seen_n = 0;
        for (size_t j = 0; j < f->n; j++) {
            Item *it = &f->items[j];
            if (it->skip) continue;
            int dup = 0;
            for (size_t s = 0; s < seen_n; s++) if (seen[s] == it->value) dup = 1;
            if (dup) {
                fprintf(stderr, "enum_str_gen: %s: skipping alias %s (duplicate value)\n", f->fn, it->name);
                continue;
            }
            seen[seen_n++] = it->value;
            fprintf(c, "        case %s: return ", it->name);
            write_str8_literal(c, it->label ? it->label : it->name + pfx);
            fputs(";\n", c);
        }
        free(seen);
        fputs("        default: return ", c);
        write_str8_literal(c, f->def);
        fputs(";\n    }\n}\n", c);
    }
    fclose(c);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: enum_str_gen <out.c> [clang-args...] -- <header-spelling>...\n");
        return 2;
    }
    const char *outc = argv[1];

    int sep = -1;
    for (int i = 2; i < argc; i++) if (strcmp(argv[i], "--") == 0) { sep = i; break; }
    if (sep < 0) { fprintf(stderr, "enum_str_gen: missing '--' before headers\n"); return 2; }

    int          clang_n    = sep - 2;
    const char **clang_args = (const char **)(argv + 2);
    char       **spellings  = argv + sep + 1;
    int          spelling_n = argc - sep - 1;
    if (spelling_n == 0) { fprintf(stderr, "enum_str_gen: no headers given\n"); return 2; }

    size_t cap = 1;
    for (int i = 0; i < spelling_n; i++) cap += strlen(spellings[i]) + 12;
    char  *umbrella = malloc(cap);
    umbrella[0] = 0;
    for (int i = 0; i < spelling_n; i++) {
        strcat(umbrella, "#include <");
        strcat(umbrella, spellings[i]);
        strcat(umbrella, ">\n");
    }

    CXIndex idx = clang_createIndex(0, 0);
    struct CXUnsavedFile unsaved = { .Filename = "mel_umbrella.c", .Contents = umbrella, .Length = strlen(umbrella) };
    CXTranslationUnit tu = clang_parseTranslationUnit(
        idx, "mel_umbrella.c", clang_args, clang_n, &unsaved, 1, CXTranslationUnit_None);
    if (!tu) { fprintf(stderr, "enum_str_gen: failed to parse translation unit\n"); return 1; }

    int fatal = 0;
    for (unsigned i = 0, n = clang_getNumDiagnostics(tu); i < n; i++) {
        CXDiagnostic dg = clang_getDiagnostic(tu, i);
        if (clang_getDiagnosticSeverity(dg) >= CXDiagnostic_Error) {
            CXString s = clang_formatDiagnostic(dg, clang_defaultDiagnosticDisplayOptions());
            fprintf(stderr, "enum_str_gen: %s\n", clang_getCString(s));
            clang_disposeString(s);
            fatal = 1;
        }
        clang_disposeDiagnostic(dg);
    }
    if (fatal) return 1;

    clang_visitChildren(clang_getTranslationUnitCursor(tu), visit_tu, NULL);

    int rc = emit(outc, spellings, spelling_n);

    clang_disposeTranslationUnit(tu);
    clang_disposeIndex(idx);
    free(umbrella);
    return rc;
}
