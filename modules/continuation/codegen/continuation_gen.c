#include <clang-c/Index.h>

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    char*  data;
    size_t len, cap;
} Sb;

static void sb_putn(Sb* b, const char* s, size_t n)
{
    if (b->len + n + 1 > b->cap)
    {
        while (b->len + n + 1 > b->cap) b->cap = b->cap ? b->cap * 2 : 256;
        b->data = realloc(b->data, b->cap);
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = 0;
}

static void sb_puts(Sb* b, const char* s) { sb_putn(b, s, strlen(s)); }

static void sb_putf(Sb* b, const char* fmt, ...)
{
    char    tmp[4096];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n > 0) sb_putn(b, tmp, (size_t)n);
}

static char* dup_cstr(const char* s)
{
    size_t n = strlen(s) + 1;
    char*  p = malloc(n);
    memcpy(p, s, n);
    return p;
}

static char* dup_cx(CXString s)
{
    char* p = dup_cstr(clang_getCString(s));
    clang_disposeString(s);
    return p;
}

static char*  g_src;
static size_t g_src_len;
static char*  g_path;
static int    g_fatal;
static int    g_quiet;

static char* read_file(const char* path, size_t* out_len)
{
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* p = malloc((size_t)n + 1);
    if (fread(p, 1, (size_t)n, f) != (size_t)n)
    {
        fclose(f);
        free(p);
        return NULL;
    }
    fclose(f);
    p[n]     = 0;
    *out_len = (size_t)n;
    return p;
}

static unsigned off_of(CXSourceLocation loc)
{
    unsigned off = 0;
    clang_getExpansionLocation(loc, NULL, NULL, NULL, &off);
    return off;
}

static unsigned cursor_start(CXCursor c) { return off_of(clang_getRangeStart(clang_getCursorExtent(c))); }

static unsigned off_spelling(CXSourceLocation loc)
{
    unsigned off = 0;
    clang_getSpellingLocation(loc, NULL, NULL, NULL, &off);
    return off;
}

static unsigned cursor_spelling_start(CXCursor c) { return off_spelling(clang_getRangeStart(clang_getCursorExtent(c))); }

static void line_col(unsigned off, unsigned* line, unsigned* col)
{
    unsigned l = 1, c = 1;
    for (unsigned i = 0; i < off && i < g_src_len; i++)
    {
        if (g_src[i] == '\n')
        {
            l++;
            c = 1;
        }
        else
            c++;
    }
    *line = l;
    *col  = c;
}

static void reject_at(unsigned off, const char* fmt, ...)
{
    if (g_quiet)
    {
        g_fatal = 1;
        return;
    }
    unsigned line, col;
    line_col(off, &line, &col);
    fprintf(stderr, "%s:%u:%u: error: ", g_path, line, col);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    g_fatal = 1;
}

typedef struct
{
    char*    name;
    char*    type;
    unsigned decl_off;
    int      is_param;
    int      referenced;
    int      lifted;
    int      addr_taken;
    int      is_vla;
    int      has_init;
    int      is_array;
    unsigned init_off;
    unsigned name_off;
    CXCursor decl;
} Var;

typedef struct
{
    unsigned call_start;
    unsigned open_paren;
    unsigned close_paren;
    unsigned stmt_end;
    int      is_return;
    int      is_await;
    int      has_value;
    int      state;
    char*    child_name;
    char*    yield_type;
} Suspend;

typedef struct
{
    unsigned start, end;
} Loop;

typedef struct
{
    int      var;
    unsigned start, end;
} Ref;

typedef struct
{
    char*    name;
    char*    ret_type;
    int      has_ret;
    char*    yield_type;
    unsigned header_start;
    unsigned body_lbrace;
    unsigned body_rbrace;

    Var*   vars;
    size_t nvar;
    Ref*   refs;
    size_t nref;
    Suspend* susp;
    size_t   nsusp;
    Loop*    loops;
    size_t   nloop;

    int has_goto_or_label;
    int switch_count;
} Func;

static Func g_f;

typedef struct
{
    char* name;
    char* yield_type;
} Sig;

static Sig*   g_sigs;
static size_t g_nsig;

static const char* sig_yield(const char* name)
{
    for (size_t i = 0; i < g_nsig; i++)
        if (strcmp(g_sigs[i].name, name) == 0) return g_sigs[i].yield_type;
    return NULL;
}

static void* push(void* arr, size_t* n, size_t elem)
{
    char* a = realloc(arr, (*n + 1) * elem);
    memset(a + (*n) * elem, 0, elem);
    (*n)++;
    return a;
}

static int find_var(CXCursor decl)
{
    for (size_t i = 0; i < g_f.nvar; i++)
        if (clang_equalCursors(g_f.vars[i].decl, decl)) return (int)i;
    return -1;
}

static int is_marker(CXCursor call, const char* which)
{
    CXCursor ref = clang_getCursorReferenced(call);
    if (clang_Cursor_isNull(ref)) return 0;
    char* sp = dup_cx(clang_getCursorSpelling(ref));
    int   r  = strcmp(sp, which) == 0;
    free(sp);
    return r;
}

static int type_is_vla(CXType t) { return clang_getCanonicalType(t).kind == CXType_VariableArray; }
static int type_is_array(CXType t)
{
    enum CXTypeKind k = clang_getCanonicalType(t).kind;
    return k == CXType_ConstantArray || k == CXType_VariableArray || k == CXType_IncompleteArray;
}

static void scan_call(Suspend* s)
{
    unsigned i = s->call_start;
    while (i < g_src_len && g_src[i] != '(') i++;
    s->open_paren = i;
    int depth     = 0;
    for (; i < g_src_len; i++)
    {
        if (g_src[i] == '(') depth++;
        else if (g_src[i] == ')')
        {
            depth--;
            if (depth == 0)
            {
                s->close_paren = i;
                break;
            }
        }
    }
    unsigned j = s->close_paren + 1;
    while (j < g_src_len && g_src[j] != ';') j++;
    s->stmt_end = j < g_src_len ? j + 1 : j;

    unsigned a = s->open_paren + 1, b = s->close_paren;
    while (a < b && (g_src[a] == ' ' || g_src[a] == '\t' || g_src[a] == '\n' || g_src[a] == '\r')) a++;
    s->has_value = a < b;
}

static enum CXChildVisitResult visit_body(CXCursor c, CXCursor parent, CXClientData d)
{
    (void)parent;
    (void)d;
    enum CXCursorKind k = clang_getCursorKind(c);

    if (k == CXCursor_VarDecl)
    {
        CXType t  = clang_getCursorType(c);
        g_f.vars  = push(g_f.vars, &g_f.nvar, sizeof(Var));
        Var* v    = &g_f.vars[g_f.nvar - 1];
        v->name   = dup_cx(clang_getCursorSpelling(c));
        v->type   = dup_cx(clang_getTypeSpelling(t));
        v->decl   = c;
        v->decl_off  = cursor_start(c);
        v->name_off  = off_of(clang_getCursorLocation(c));
        v->is_vla    = type_is_vla(t);
        v->is_array  = type_is_array(t);
        CXCursor ini = clang_Cursor_getVarDeclInitializer(c);
        if (!clang_Cursor_isNull(ini))
        {
            v->has_init = 1;
            v->init_off = cursor_start(ini);
        }
    }
    else if (k == CXCursor_DeclRefExpr)
    {
        CXCursor ref = clang_getCursorReferenced(c);
        int      vi  = find_var(ref);
        if (vi >= 0)
        {
            g_f.vars[vi].referenced = 1;
            unsigned start          = cursor_spelling_start(c);
            g_f.refs                = push(g_f.refs, &g_f.nref, sizeof(Ref));
            Ref* r                  = &g_f.refs[g_f.nref - 1];
            r->var                  = vi;
            r->start                = start;
            r->end                  = start + (unsigned)strlen(g_f.vars[vi].name);

            if (clang_getCursorKind(parent) == CXCursor_UnaryOperator)
            {
                CXTranslationUnit tu  = clang_Cursor_getTranslationUnit(parent);
                CXToken*          tks = NULL;
                unsigned          nt  = 0;
                clang_tokenize(tu, clang_getCursorExtent(parent), &tks, &nt);
                if (nt > 0)
                {
                    char* op = dup_cx(clang_getTokenSpelling(tu, tks[0]));
                    if (strcmp(op, "&") == 0) g_f.vars[vi].addr_taken = 1;
                    free(op);
                }
                if (tks) clang_disposeTokens(tu, tks, nt);
            }
        }
    }
    else if (k == CXCursor_CallExpr && (is_marker(c, "__mel_cont_yield") || is_marker(c, "__mel_cont_await") || is_marker(c, "__mel_cont_return")))
    {
        g_f.susp   = push(g_f.susp, &g_f.nsusp, sizeof(Suspend));
        Suspend* s = &g_f.susp[g_f.nsusp - 1];
        s->call_start = cursor_start(c);
        s->is_return  = is_marker(c, "__mel_cont_return");
        s->is_await   = is_marker(c, "__mel_cont_await");
        scan_call(s);
        if (s->has_value && clang_Cursor_getNumArguments(c) >= 2)
        {
            CXCursor arg = clang_Cursor_getArgument(c, 1);
            CXType   at  = clang_getCursorType(arg);
            if (s->is_await)
            {
                char* ts = dup_cx(clang_getTypeSpelling(clang_getCanonicalType(at)));
                char* p  = strstr(ts, "Mel_Cont_Frame_");
                if (p) s->child_name = dup_cstr(p + strlen("Mel_Cont_Frame_"));
                free(ts);
            }
            else
                s->yield_type = dup_cx(clang_getTypeSpelling(clang_getCanonicalType(at)));
        }
    }
    else if (k == CXCursor_WhileStmt || k == CXCursor_ForStmt || k == CXCursor_DoStmt)
    {
        CXSourceRange ext = clang_getCursorExtent(c);
        g_f.loops         = push(g_f.loops, &g_f.nloop, sizeof(Loop));
        Loop* lp          = &g_f.loops[g_f.nloop - 1];
        lp->start         = off_of(clang_getRangeStart(ext));
        lp->end           = off_of(clang_getRangeEnd(ext));
    }
    else if (k == CXCursor_SwitchStmt)
        g_f.switch_count++;
    else if (k == CXCursor_GotoStmt || k == CXCursor_LabelStmt || k == CXCursor_IndirectGotoStmt)
        g_f.has_goto_or_label = 1;

    return CXChildVisit_Recurse;
}

static int loop_encloses(const Loop* lp, unsigned off) { return lp->start <= off && off <= lp->end; }

static void compute_lifts(void)
{
    for (size_t vi = 0; vi < g_f.nvar; vi++)
    {
        Var* v = &g_f.vars[vi];
        if (v->is_param)
        {
            v->lifted = v->referenced;
            continue;
        }
        if (!v->referenced) continue;

        for (size_t si = 0; si < g_f.nsusp && !v->lifted; si++)
        {
            Suspend* s = &g_f.susp[si];

            if (v->decl_off <= s->call_start)
                for (size_t ri = 0; ri < g_f.nref; ri++)
                    if (g_f.refs[ri].var == (int)vi && g_f.refs[ri].start >= s->stmt_end)
                    {
                        v->lifted = 1;
                        break;
                    }

            for (size_t li = 0; li < g_f.nloop && !v->lifted; li++)
            {
                Loop* lp = &g_f.loops[li];
                if (!loop_encloses(lp, s->call_start)) continue;
                if (v->decl_off > lp->end) continue;
                for (size_t ri = 0; ri < g_f.nref; ri++)
                    if (g_f.refs[ri].var == (int)vi && loop_encloses(lp, g_f.refs[ri].start))
                    {
                        v->lifted = 1;
                        break;
                    }
            }
        }
    }

    for (size_t si = 0; si < g_f.nsusp; si++)
    {
        Suspend* s = &g_f.susp[si];
        if (!s->is_await) continue;
        for (size_t ri = 0; ri < g_f.nref; ri++)
            if (g_f.refs[ri].start >= s->open_paren && g_f.refs[ri].end <= s->close_paren)
                g_f.vars[g_f.refs[ri].var].lifted = 1;
    }
}

typedef struct
{
    unsigned start, end;
    char*    text;
} Edit;

static Edit*  g_edits;
static size_t g_nedit;

static void edit(unsigned start, unsigned end, char* text)
{
    g_edits        = push(g_edits, &g_nedit, sizeof(Edit));
    Edit* e        = &g_edits[g_nedit - 1];
    e->start       = start;
    e->end         = end;
    e->text        = text;
}

static char* heap_printf(const char* fmt, ...)
{
    char    tmp[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    return dup_cstr(tmp);
}

static int edit_cmp(const void* a, const void* b)
{
    const Edit* x = a;
    const Edit* y = b;
    return x->start < y->start ? -1 : x->start > y->start ? 1 : 0;
}

static uint64_t layout_hash(const char* s)
{
    uint64_t h = 1469598103934665603ull;
    for (const char* p = s; *p; p++)
    {
        h ^= (unsigned char)*p;
        h *= 1099511628211ull;
    }
    return h;
}

static void emit_func(Sb* hdr, Sb* impl)
{
    Func* f = &g_f;

    int state = 0;
    for (size_t i = 0; i < f->nsusp; i++) f->susp[i].state = ++state;

    Sb fields = {0};
    sb_puts(&fields, "i32 state;");
    Sb frame = {0};
    sb_putf(&frame, "typedef struct Mel_Cont_Frame_%s\n{\n    i32 state;\n", f->name);
    for (size_t i = 0; i < f->nvar; i++)
    {
        Var* v = &f->vars[i];
        if (!v->is_param || !v->lifted) continue;
        sb_putf(&frame, "    %s %s;\n", v->type, v->name);
        sb_putf(&fields, "%s %s;", v->type, v->name);
    }
    for (size_t i = 0; i < f->nvar; i++)
    {
        Var* v = &f->vars[i];
        if (v->is_param || !v->lifted) continue;
        sb_putf(&frame, "    %s %s;\n", v->type, v->name);
        sb_putf(&fields, "%s %s;", v->type, v->name);
    }
    const char* yt0 = f->yield_type;
    for (size_t i = 0; i < f->nsusp; i++)
    {
        Suspend* s = &f->susp[i];
        if (!s->is_await || !s->child_name) continue;
        const char* cy = sig_yield(s->child_name);
        if (cy && !(yt0 && strcmp(yt0, cy) == 0))
        {
            sb_putf(&frame, "    %s __await_out_%d;\n", cy, s->state);
            sb_putf(&fields, "%s __await_out_%d;", cy, s->state);
        }
    }
    if (f->has_ret)
    {
        sb_putf(&frame, "    %s __ret;\n", f->ret_type);
        sb_putf(&fields, "%s __ret;", f->ret_type);
    }
    sb_putf(&frame, "} Mel_Cont_Frame_%s;\n\n", f->name);

    sb_puts(hdr, frame.data);
    sb_putf(hdr, "#define MEL_CONT_LAYOUT_HASH_%s 0x%016llxull\n\n", f->name, (unsigned long long)layout_hash(fields.data));

    const char* yt = f->yield_type;
    if (yt)
    {
        sb_putf(hdr, "Mel_Cont_Suspended %s__resume(Mel_Cont_Frame_%s* __f, %s* __f_out);\n\n", f->name, f->name, yt);
    }
    else
    {
        sb_putf(hdr, "Mel_Cont_Suspended %s__resume(Mel_Cont_Frame_%s* __f);\n\n", f->name, f->name);
    }
    free(fields.data);
    free(frame.data);

    char* sig;
    if (yt) sig = heap_printf("Mel_Cont_Suspended %s__resume(Mel_Cont_Frame_%s* __f, %s* __f_out)\n{\n    switch (__f->state)\n    {\n    case MEL_CONT_STATE_START:;\n", f->name, f->name, yt);
    else    sig = heap_printf("Mel_Cont_Suspended %s__resume(Mel_Cont_Frame_%s* __f)\n{\n    switch (__f->state)\n    {\n    case MEL_CONT_STATE_START:;\n", f->name, f->name);
    edit(f->header_start, f->body_lbrace + 1, sig);

    edit(f->body_rbrace, f->body_rbrace + 1, dup_cstr("\n    default:;\n    }\n    __f->state = MEL_CONT_STATE_DONE;\n    return false;\n}\n"));

    for (size_t i = 0; i < f->nref; i++)
    {
        Ref* r = &f->refs[i];
        Var* v = &f->vars[r->var];
        if (!v->lifted) continue;
        if (strncmp(g_src + r->start, v->name, strlen(v->name)) != 0)
        {
            reject_at(r->start, "reference to lifted local '%s' is not spliceable (came through a macro)", v->name);
            continue;
        }
        edit(r->start, r->end, heap_printf("__f->%s", v->name));
    }

    for (size_t i = 0; i < f->nvar; i++)
    {
        Var* v = &f->vars[i];
        if (v->is_param || !v->lifted) continue;
        if (v->is_vla || (v->is_array && v->has_init)) continue;
        if (v->has_init)
        {
            unsigned p = v->init_off;
            while (p < g_src_len && (g_src[p] == ' ' || g_src[p] == '\t' || g_src[p] == '\n' || g_src[p] == '\r')) p++;
            if (p < g_src_len && g_src[p] == '{')
                edit(v->decl_off, v->init_off, heap_printf("__f->%s = (%s)", v->name, v->type));
            else
                edit(v->decl_off, v->init_off, heap_printf("__f->%s = ", v->name));
        }
        else
            edit(v->decl_off, v->name_off + (unsigned)strlen(v->name), dup_cstr("(void)0"));
    }

    for (size_t i = 0; i < f->nsusp; i++)
    {
        Suspend* s = &f->susp[i];
        if (s->is_await)
        {
            if (!s->child_name)
            {
                reject_at(s->call_start, "mel_cont_await argument is not a continuation frame");
                continue;
            }
            const char* cy  = sig_yield(s->child_name);
            char*       fwd;
            if (!cy) fwd = dup_cstr("");
            else if (yt && strcmp(yt, cy) == 0) fwd = dup_cstr(", __f_out");
            else fwd = heap_printf(", &__f->__await_out_%d", s->state);
            edit(s->call_start, s->open_paren + 1, heap_printf("{ for (;;) { if (!%s__resume(&(", s->child_name));
            edit(s->close_paren, s->stmt_end, heap_printf(")%s)) break; __f->state = %d; return true; case %d:; } }", fwd, s->state, s->state));
            free(fwd);
        }
        else if (s->is_return)
        {
            if (s->has_value)
            {
                edit(s->call_start, s->open_paren + 1, dup_cstr("{ __f->__ret = ("));
                edit(s->close_paren, s->stmt_end, dup_cstr("); __f->state = MEL_CONT_STATE_DONE; return false; }"));
            }
            else
                edit(s->call_start, s->stmt_end, dup_cstr("{ __f->state = MEL_CONT_STATE_DONE; return false; }"));
        }
        else
        {
            if (s->has_value)
            {
                edit(s->call_start, s->open_paren + 1, dup_cstr("{ *__f_out = ("));
                edit(s->close_paren, s->stmt_end, heap_printf("); __f->state = %d; return true; case %d:; }", s->state, s->state));
            }
            else
                edit(s->call_start, s->stmt_end, heap_printf("{ __f->state = %d; return true; case %d:; }", s->state, s->state));
        }
    }

    qsort(g_edits, g_nedit, sizeof(Edit), edit_cmp);
    for (size_t i = 1; i < g_nedit; i++)
        if (g_edits[i].start < g_edits[i - 1].end)
        {
            reject_at(g_edits[i].start, "overlapping rewrite (unsupported construct near a suspension point)");
            return;
        }

    unsigned pos = f->header_start;
    for (size_t i = 0; i < g_nedit; i++)
    {
        Edit* e = &g_edits[i];
        sb_putn(impl, g_src + pos, e->start - pos);
        sb_puts(impl, e->text);
        pos = e->end;
    }
    sb_putn(impl, g_src + pos, (f->body_rbrace + 1) - pos);
    sb_puts(impl, "\n\n");

    g_nedit = 0;
}

static void validate(void)
{
    if (g_f.switch_count > 0)
        for (size_t i = 0; i < g_f.nsusp; i++)
            reject_at(g_f.susp[i].call_start, "suspension point inside a switch statement is unsupported");
    if (g_f.has_goto_or_label && g_f.nsusp)
        reject_at(g_f.susp[0].call_start, "goto/label in a continuation with a suspension point is unsupported");
    for (size_t i = 0; i < g_f.nvar; i++)
    {
        Var* v = &g_f.vars[i];
        if (!v->lifted) continue;
        if (v->addr_taken)
            reject_at(v->decl_off, "address taken of lifted local '%s' would make the frame non-relocatable", v->name);
        if (v->is_vla)
            reject_at(v->decl_off, "lifted local '%s' is a variable-length array", v->name);
        if (v->is_array && v->has_init)
            reject_at(v->decl_off, "lifted array local '%s' with an initializer is unsupported", v->name);
    }

    char* yt = NULL;
    for (size_t i = 0; i < g_f.nsusp; i++)
    {
        Suspend* s = &g_f.susp[i];
        if (s->is_await || s->is_return || !s->yield_type) continue;
        if (!yt) yt = s->yield_type;
        else if (strcmp(yt, s->yield_type) != 0)
            reject_at(s->call_start, "inconsistent yield type: '%s' vs '%s'", yt, s->yield_type);
    }
    if (!yt)
        for (size_t i = 0; i < g_f.nsusp; i++)
        {
            Suspend* s = &g_f.susp[i];
            if (!s->is_await || !s->child_name) continue;
            const char* cy = sig_yield(s->child_name);
            if (cy) { yt = dup_cstr(cy); break; }
        }
    g_f.yield_type = yt;
}

static enum CXChildVisitResult find_compound(CXCursor c, CXCursor parent, CXClientData d)
{
    (void)parent;
    if (clang_getCursorKind(c) == CXCursor_CompoundStmt)
    {
        *(CXCursor*)d = c;
        return CXChildVisit_Break;
    }
    return CXChildVisit_Continue;
}

static void process_function(CXCursor fn, Sb* hdr, Sb* impl)
{
    memset(&g_f, 0, sizeof g_f);
    g_f.name     = dup_cx(clang_getCursorSpelling(fn));
    g_f.ret_type = dup_cx(clang_getTypeSpelling(clang_getCursorResultType(fn)));
    g_f.has_ret  = strcmp(g_f.ret_type, "void") != 0;
    g_f.header_start = off_of(clang_getRangeStart(clang_getCursorExtent(fn)));

    int n = clang_Cursor_getNumArguments(fn);
    for (int i = 0; i < n; i++)
    {
        CXCursor a  = clang_Cursor_getArgument(fn, i);
        g_f.vars    = push(g_f.vars, &g_f.nvar, sizeof(Var));
        Var* v      = &g_f.vars[g_f.nvar - 1];
        v->name     = dup_cx(clang_getCursorSpelling(a));
        v->type     = dup_cx(clang_getTypeSpelling(clang_getCursorType(a)));
        v->decl     = a;
        v->is_param = 1;
    }

    CXCursor body = clang_getNullCursor();
    clang_visitChildren(fn, find_compound, &body);
    if (clang_Cursor_isNull(body))
    {
        reject_at(g_f.header_start, "continuation '%s' has no body", g_f.name);
        return;
    }
    CXSourceRange be = clang_getCursorExtent(body);
    g_f.body_lbrace  = off_of(clang_getRangeStart(be));
    g_f.body_rbrace  = off_of(clang_getRangeEnd(be));
    if (g_f.body_rbrace >= g_src_len) g_f.body_rbrace = g_src_len - 1;
    while (g_f.body_rbrace > g_f.body_lbrace && g_src[g_f.body_rbrace] != '}') g_f.body_rbrace--;

    clang_visitChildren(body, visit_body, NULL);
    compute_lifts();
    validate();
    if (g_fatal) return;
    emit_func(hdr, impl);
}

static Sb       g_hdr, g_impl;
static unsigned g_nfound;

static enum CXChildVisitResult visit_yield_type(CXCursor c, CXCursor parent, CXClientData d)
{
    (void)parent;
    char** out = d;
    if (*out) return CXChildVisit_Break;
    if (clang_getCursorKind(c) == CXCursor_CallExpr && is_marker(c, "__mel_cont_yield") && clang_Cursor_getNumArguments(c) >= 2)
        *out = dup_cx(clang_getTypeSpelling(clang_getCanonicalType(clang_getCursorType(clang_Cursor_getArgument(c, 1)))));
    return CXChildVisit_Recurse;
}

static char* infer_yield_type(CXCursor fn)
{
    char* yt = NULL;
    clang_visitChildren(fn, visit_yield_type, &yt);
    return yt;
}

static enum CXChildVisitResult visit_annot(CXCursor a, CXCursor parent, CXClientData d)
{
    (void)parent;
    if (clang_getCursorKind(a) == CXCursor_AnnotateAttr)
    {
        char* s = dup_cx(clang_getCursorSpelling(a));
        if (strcmp(s, "mel:continuation") == 0) *(int*)d = 1;
        free(s);
    }
    return CXChildVisit_Continue;
}

static int is_marked_definition(CXCursor c)
{
    if (clang_getCursorKind(c) != CXCursor_FunctionDecl) return 0;
    if (!clang_isCursorDefinition(c)) return 0;
    int marked = 0;
    clang_visitChildren(c, visit_annot, &marked);
    return marked;
}

static enum CXChildVisitResult visit_sig(CXCursor c, CXCursor parent, CXClientData d)
{
    (void)parent;
    (void)d;
    if (!is_marked_definition(c)) return CXChildVisit_Continue;
    g_sigs           = push(g_sigs, &g_nsig, sizeof(Sig));
    g_sigs[g_nsig - 1].name       = dup_cx(clang_getCursorSpelling(c));
    g_sigs[g_nsig - 1].yield_type = infer_yield_type(c);
    return CXChildVisit_Continue;
}

static enum CXChildVisitResult visit_tu(CXCursor c, CXCursor parent, CXClientData d)
{
    (void)parent;
    (void)d;
    if (!is_marked_definition(c)) return CXChildVisit_Continue;
    g_nfound++;
    process_function(c, &g_hdr, &g_impl);
    return CXChildVisit_Continue;
}

#define MARK_BEG "/* >>> mel_cont generated frames — managed region, do not edit >>> */"
#define MARK_END "/* <<< mel_cont generated frames <<< */"

static Sb inject_region(const char* src, const char* region)
{
    Sb          out = {0};
    const char* beg = strstr(src, MARK_BEG);
    if (beg)
    {
        const char* end = strstr(beg, MARK_END);
        if (end)
        {
            const char* after = end + strlen(MARK_END);
            if (*after == '\n') after++;
            sb_putn(&out, src, (size_t)(beg - src));
            sb_putf(&out, "%s\n%s%s\n", MARK_BEG, region, MARK_END);
            sb_puts(&out, after);
            return out;
        }
    }
    const char* mc = strstr(src, "mel_cont(");
    if (!mc)
    {
        sb_puts(&out, src);
        return out;
    }
    sb_putn(&out, src, (size_t)(mc - src));
    sb_putf(&out, "%s\n%s%s\n\n", MARK_BEG, region, MARK_END);
    sb_puts(&out, mc);
    return out;
}

static int run_pass(CXIndex idx, const char* path, const char** args, int n, int quiet)
{
    free(g_src);
    g_src = read_file(path, &g_src_len);
    if (!g_src) { fprintf(stderr, "continuation_gen: cannot read %s\n", path); return -1; }

    g_hdr.len = g_impl.len = 0;
    g_nsig    = 0;
    g_nfound  = 0;
    g_fatal   = 0;
    g_quiet   = quiet;

    CXTranslationUnit tu = clang_parseTranslationUnit(idx, path, args, n, NULL, 0, CXTranslationUnit_DetailedPreprocessingRecord);
    if (!tu) { fprintf(stderr, "continuation_gen: failed to parse %s\n", path); return -1; }

    int parse_fatal = 0;
    if (!quiet)
        for (unsigned i = 0, dn = clang_getNumDiagnostics(tu); i < dn; i++)
        {
            CXDiagnostic dg = clang_getDiagnostic(tu, i);
            if (clang_getDiagnosticSeverity(dg) >= CXDiagnostic_Error)
            {
                CXString s = clang_formatDiagnostic(dg, clang_defaultDiagnosticDisplayOptions());
                fprintf(stderr, "continuation_gen: %s\n", clang_getCString(s));
                clang_disposeString(s);
                parse_fatal = 1;
            }
            clang_disposeDiagnostic(dg);
        }

    clang_visitChildren(clang_getTranslationUnitCursor(tu), visit_sig, NULL);
    clang_visitChildren(clang_getTranslationUnitCursor(tu), visit_tu, NULL);
    clang_disposeTranslationUnit(tu);
    return parse_fatal;
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "usage: continuation_gen <header.h> <out.gen.c> [clang-args...]\n");
        return 2;
    }
    const char*  header_path = argv[1];
    const char*  out_c       = argv[2];
    int          n           = argc - 3;
    const char** args        = (const char**)(argv + 3);
    g_path                   = (char*)header_path;

    CXIndex idx = clang_createIndex(0, 0);

    char* prev = NULL;
    for (int iter = 0; iter < 8; iter++)
    {
        if (run_pass(idx, header_path, args, n, 1) < 0) return 1;
        Sb h = inject_region(g_src, g_hdr.data ? g_hdr.data : "");
        if (prev && strcmp(prev, h.data) == 0)
        {
            free(h.data);
            break;
        }
        FILE* fh = fopen(header_path, "w");
        if (!fh) { fprintf(stderr, "continuation_gen: cannot open %s\n", header_path); return 1; }
        fwrite(h.data, 1, h.len, fh);
        fclose(fh);
        free(prev);
        prev = h.data;
    }

    int pf = run_pass(idx, header_path, args, n, 0);
    if (pf < 0 || pf) return 1;
    if (g_fatal) return 1;
    if (g_nfound == 0) fprintf(stderr, "continuation_gen: no mel_cont definitions in %s\n", header_path);

    const char* slash = strrchr(header_path, '/');
    Sb          impl  = {0};
    sb_putf(&impl, "#include \"%s\"\n\n", slash ? slash + 1 : header_path);
    sb_puts(&impl, g_impl.data ? g_impl.data : "");

    FILE* fc = fopen(out_c, "w");
    if (!fc) { fprintf(stderr, "continuation_gen: cannot open %s\n", out_c); return 1; }
    fwrite(impl.data, 1, impl.len, fc);
    fclose(fc);

    clang_disposeIndex(idx);
    return 0;
}
