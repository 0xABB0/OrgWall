#include "command.h"
#include "mugen_cns.h"
#include "string.str8.h"
#include "allocator.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>

static u64 mugen_rng_next(u64* s)
{
    u64 x = *s;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *s = x;
    return x;
}

typedef struct {
    str8 text;
    usize pos;
    const Mel_Alloc* alloc;
} Expr_Parser;

static void skip_ws(Expr_Parser* p)
{
    while (p->pos < (usize)p->text.len)
    {
        u8 c = p->text.data[p->pos];
        if (c == ' ' || c == '\t') p->pos++;
        else break;
    }
}

static bool at_end(Expr_Parser* p)
{
    return p->pos >= (usize)p->text.len;
}

static u8 peek(Expr_Parser* p)
{
    if (at_end(p)) return 0;
    return p->text.data[p->pos];
}

static u8 advance(Expr_Parser* p)
{
    if (at_end(p)) return 0;
    return p->text.data[p->pos++];
}

static bool match_char(Expr_Parser* p, u8 c)
{
    skip_ws(p);
    if (peek(p) == c) { p->pos++; return true; }
    return false;
}

static Mugen_Expr* alloc_expr(Expr_Parser* p, u8 type)
{
    Mugen_Expr* e = mel_alloc(p->alloc, sizeof(Mugen_Expr));
    memset(e, 0, sizeof(Mugen_Expr));
    e->type = type;
    return e;
}

static Mugen_Expr* make_int(Expr_Parser* p, i32 val)
{
    Mugen_Expr* e = alloc_expr(p, MUGEN_EXPR_LIT_INT);
    e->lit_int = val;
    return e;
}

static Mugen_Expr* make_float(Expr_Parser* p, f32 val)
{
    Mugen_Expr* e = alloc_expr(p, MUGEN_EXPR_LIT_FLOAT);
    e->lit_float = val;
    return e;
}

static Mugen_Expr* make_string(Expr_Parser* p, str8 val)
{
    Mugen_Expr* e = alloc_expr(p, MUGEN_EXPR_LIT_STRING);
    e->lit_string = val;
    return e;
}

static Mugen_Expr* make_unary(Expr_Parser* p, u8 op, Mugen_Expr* operand)
{
    Mugen_Expr* e = alloc_expr(p, MUGEN_EXPR_UNARY);
    e->unary.op = op;
    e->unary.operand = operand;
    return e;
}

static Mugen_Expr* make_binary(Expr_Parser* p, u8 op, Mugen_Expr* lhs, Mugen_Expr* rhs)
{
    Mugen_Expr* e = alloc_expr(p, MUGEN_EXPR_BINARY);
    e->binary.op = op;
    e->binary.lhs = lhs;
    e->binary.rhs = rhs;
    return e;
}

static Mugen_Expr* make_query(Expr_Parser* p, u8 id, Mugen_Expr* arg)
{
    Mugen_Expr* e = alloc_expr(p, MUGEN_EXPR_QUERY);
    e->query.id = id;
    e->query.arg = arg;
    return e;
}

static Mugen_Expr* make_var(Expr_Parser* p, u8 var_type, Mugen_Expr* index)
{
    Mugen_Expr* e = alloc_expr(p, MUGEN_EXPR_VAR);
    e->var.var_type = var_type;
    e->var.index = index;
    return e;
}

static Mugen_Expr* make_func(Expr_Parser* p, u8 id, Mugen_Expr** args, u8 count)
{
    Mugen_Expr* e = alloc_expr(p, MUGEN_EXPR_FUNC);
    e->func.id = id;
    e->func.args = args;
    e->func.arg_count = count;
    return e;
}

static bool ident_char(u8 c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || (c >= '0' && c <= '9') || c == '.';
}

static str8 read_ident(Expr_Parser* p)
{
    skip_ws(p);
    usize start = p->pos;
    while (p->pos < (usize)p->text.len && ident_char(p->text.data[p->pos]))
        p->pos++;
    if (p->pos == start) return (str8){0};
    return str8_from_parts(p->text.data + start, (size)(p->pos - start));
}

static bool str8_ieq(str8 a, const char* b)
{
    size blen = (size)strlen(b);
    if (a.len != blen) return false;
    for (size i = 0; i < blen; i++)
    {
        u8 ac = a.data[i];
        u8 bc = (u8)b[i];
        if (ac >= 'A' && ac <= 'Z') ac += 32;
        if (bc >= 'A' && bc <= 'Z') bc += 32;
        if (ac != bc) return false;
    }
    return true;
}

static Mugen_Expr* parse_expr(Expr_Parser* p);

static Mugen_Expr* parse_number(Expr_Parser* p)
{
    skip_ws(p);
    usize start = p->pos;
    bool has_dot = false;
    bool has_neg = false;

    if (peek(p) == '-') { has_neg = true; p->pos++; }

    while (p->pos < (usize)p->text.len)
    {
        u8 c = p->text.data[p->pos];
        if (c >= '0' && c <= '9') p->pos++;
        else if (c == '.' && !has_dot) { has_dot = true; p->pos++; }
        else break;
    }

    if (p->pos == start || (has_neg && p->pos == start + 1))
    {
        p->pos = start;
        return NULL;
    }

    char buf[64];
    size len = (size)(p->pos - start);
    if (len >= 63) len = 63;
    memcpy(buf, p->text.data + start, (size_t)len);
    buf[len] = 0;

    if (has_dot)
        return make_float(p, strtof(buf, NULL));
    else
        return make_int(p, (i32)strtol(buf, NULL, 10));
}

static str8 parse_string_literal(Expr_Parser* p)
{
    skip_ws(p);
    if (peek(p) != '"') return (str8){0};
    p->pos++;
    usize start = p->pos;
    while (p->pos < (usize)p->text.len && p->text.data[p->pos] != '"')
        p->pos++;
    str8 result = str8_from_parts(p->text.data + start, (size)(p->pos - start));
    if (peek(p) == '"') p->pos++;
    return result;
}

typedef struct {
    const char* name;
    u8 id;
} Query_Entry;

static const Query_Entry s_queries[] = {
    {"time",         MUGEN_QUERY_TIME},
    {"animtime",     MUGEN_QUERY_ANIMTIME},
    {"animelem",     MUGEN_QUERY_ANIMELEM},
    {"animelemtime", MUGEN_QUERY_ANIMELEMTIME},
    {"stateno",      MUGEN_QUERY_STATENO},
    {"prevstateno",  MUGEN_QUERY_PREVSTATENO},
    {"statetype",    MUGEN_QUERY_STATETYPE},
    {"movetype",     MUGEN_QUERY_MOVETYPE},
    {"ctrl",         MUGEN_QUERY_CTRL},
    {"command",      MUGEN_QUERY_COMMAND},
    {"vel x",        MUGEN_QUERY_VEL_X},
    {"vel y",        MUGEN_QUERY_VEL_Y},
    {"pos x",        MUGEN_QUERY_POS_X},
    {"pos y",        MUGEN_QUERY_POS_Y},
    {"movecontact",  MUGEN_QUERY_MOVECONTACT},
    {"movehit",      MUGEN_QUERY_MOVEHIT},
    {"moveguarded",  MUGEN_QUERY_MOVEGUARDED},
    {"hitcount",     MUGEN_QUERY_HITCOUNT},
    {"life",         MUGEN_QUERY_LIFE},
    {"lifemax",      MUGEN_QUERY_LIFEMAX},
    {"power",        MUGEN_QUERY_POWER},
    {"powermax",     MUGEN_QUERY_POWERMAX},
    {"facing",       MUGEN_QUERY_FACING},
    {"p2bodydist x", MUGEN_QUERY_P2BODYDIST_X},
    {"p2dist x",     MUGEN_QUERY_P2DIST_X},
    {"p2dist y",     MUGEN_QUERY_P2DIST_Y},
    {"numhelper",    MUGEN_QUERY_NUMHELPER},
    {"alive",        MUGEN_QUERY_ALIVE},
    {"random",       MUGEN_QUERY_RANDOM},
    {"gametime",     MUGEN_QUERY_GAMETIME},
    {"roundstate",   MUGEN_QUERY_ROUNDSTATE},
    {"ishelper",     MUGEN_QUERY_ISHELPER},
    {"anim",         MUGEN_QUERY_ANIM},
    {"hitshakeover", MUGEN_QUERY_HITSHAKEOVER},
    {"hitover",      MUGEN_QUERY_HITOVER},
    {"hitfall",      MUGEN_QUERY_HITFALL},
    {"p2statetype",  MUGEN_QUERY_P2STATETYPE},
    {"p2movetype",   MUGEN_QUERY_P2MOVETYPE},
    {"frontedgedist", MUGEN_QUERY_FRONTEDGEDIST},
    {"backedgedist",  MUGEN_QUERY_BACKEDGEDIST},
    {"frontedgebodydist", MUGEN_QUERY_FRONTEDGEBODYDIST},
    {"backedgebodydist",  MUGEN_QUERY_BACKEDGEBODYDIST},
    {"roundno",      MUGEN_QUERY_ROUNDNO},
    {"roundsexisted", MUGEN_QUERY_ROUNDSEXISTED},
    {"inguarddist",  MUGEN_QUERY_INGUARDDIST},
    {"canrecover",   MUGEN_QUERY_CANRECOVER},
    {"palno",        MUGEN_QUERY_PALNO},
    {"hitdefattr",   MUGEN_QUERY_HITDEFATTR},
    {"lose",         MUGEN_QUERY_LOSE},
    {"win",          MUGEN_QUERY_WIN},
    {"matchover",    MUGEN_QUERY_MATCHOVER},
    {"statetime",    MUGEN_QUERY_TIME},
    {"numtarget",    MUGEN_QUERY_NUMTARGET},
    {NULL, 0}
};

typedef struct {
    const char* name;
    u8 id;
} Func_Entry;

static const Func_Entry s_funcs[] = {
    {"ceil",   MUGEN_FUNC_CEIL},
    {"floor",  MUGEN_FUNC_FLOOR},
    {"abs",    MUGEN_FUNC_ABS},
    {"ifelse", MUGEN_FUNC_IFELSE},
    {NULL, 0}
};

typedef struct {
    const char* name;
    u8 var_type;
} Var_Entry;

static const Var_Entry s_vars[] = {
    {"var",     MUGEN_VAR_INT},
    {"fvar",    MUGEN_VAR_FLOAT},
    {"sysvar",  MUGEN_VAR_SYSINT},
    {"sysfvar", MUGEN_VAR_SYSFLOAT},
    {NULL, 0}
};

static bool try_query_with_space(Expr_Parser* p, str8 ident, Mugen_Expr** out)
{
    for (const Query_Entry* q = s_queries; q->name; q++)
    {
        const char* space = strchr(q->name, ' ');
        if (!space) continue;

        size prefix_len = (size)(space - q->name);
        if (ident.len != prefix_len) continue;
        bool match = true;
        for (size i = 0; i < prefix_len; i++)
        {
            u8 a = ident.data[i]; if (a >= 'A' && a <= 'Z') a += 32;
            u8 b = (u8)q->name[i]; if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) { match = false; break; }
        }
        if (!match) continue;

        skip_ws(p);
        usize saved = p->pos;
        str8 suffix = read_ident(p);
        const char* expected = space + 1;
        if (str8_ieq(suffix, expected))
        {
            *out = make_query(p, q->id, NULL);
            return true;
        }
        p->pos = saved;
    }
    return false;
}

static Mugen_Expr* parse_primary(Expr_Parser* p)
{
    skip_ws(p);
    if (at_end(p)) return make_int(p, 0);

    u8 c = peek(p);

    if (c == '(')
    {
        p->pos++;
        Mugen_Expr* inner = parse_expr(p);
        match_char(p, ')');
        return inner;
    }

    if (c == '"')
    {
        str8 s = parse_string_literal(p);
        return make_string(p, s);
    }

    if ((c >= '0' && c <= '9') || (c == '.' && p->pos + 1 < (usize)p->text.len && p->text.data[p->pos + 1] >= '0'))
    {
        return parse_number(p);
    }

    if (c == '!' || c == '-')
    {
        if (c == '!' && p->pos + 1 < (usize)p->text.len && p->text.data[p->pos + 1] == '=')
            return NULL;
        p->pos++;
        Mugen_Expr* operand = parse_primary(p);
        return make_unary(p, c == '!' ? MUGEN_OP_NOT : MUGEN_OP_NEG, operand);
    }

    if (!ident_char(c)) return make_int(p, 0);

    usize ident_start = p->pos;
    str8 ident = read_ident(p);
    if (ident.len == 0) return make_int(p, 0);

    if (ident.len == 1)
    {
        u8 ch = ident.data[0];
        if (ch == 'S' || ch == 's') return make_int(p, 'S');
        if (ch == 'C' || ch == 'c') return make_int(p, 'C');
        if (ch == 'A' || ch == 'a') return make_int(p, 'A');
        if (ch == 'L' || ch == 'l') return make_int(p, 'L');
        if (ch == 'I' || ch == 'i') return make_int(p, 'I');
        if (ch == 'H' || ch == 'h') return make_int(p, 'H');
        if (ch == 'N' || ch == 'n') return make_int(p, 'N');
        if (ch == 'U' || ch == 'u') return make_int(p, 'U');
    }

    if (str8_ieq(ident, "const"))
    {
        match_char(p, '(');
        str8 name = read_ident(p);
        match_char(p, ')');
        Mugen_Expr* e = make_query(p, MUGEN_QUERY_CONST, NULL);
        e->query.arg = make_string(p, name);
        return e;
    }

    if (str8_ieq(ident, "const720p"))
    {
        match_char(p, '(');
        Mugen_Expr* arg = parse_expr(p);
        match_char(p, ')');
        return arg;
    }

    if (str8_ieq(ident, "selfanimexist"))
    {
        match_char(p, '(');
        Mugen_Expr* arg = parse_expr(p);
        match_char(p, ')');
        return make_query(p, MUGEN_QUERY_SELFANIMEXIST, arg);
    }

    if (str8_ieq(ident, "animelemtime"))
    {
        skip_ws(p);
        if (peek(p) == '(')
        {
            match_char(p, '(');
            Mugen_Expr* arg = parse_expr(p);
            match_char(p, ')');
            return make_query(p, MUGEN_QUERY_ANIMELEMTIME, arg);
        }
        return make_query(p, MUGEN_QUERY_ANIMELEMTIME, NULL);
    }

    if (str8_ieq(ident, "gethitvar"))
    {
        match_char(p, '(');
        str8 name = read_ident(p);
        match_char(p, ')');
        Mugen_Expr* e = make_query(p, MUGEN_QUERY_GETHITVAR, NULL);
        e->query.arg = make_string(p, name);
        return e;
    }

    if (str8_ieq(ident, "numprojid") || str8_ieq(ident, "numhelper"))
    {
        u8 qid = str8_ieq(ident, "numhelper") ? MUGEN_QUERY_NUMHELPER : MUGEN_QUERY_NUMPROJID;
        skip_ws(p);
        if (peek(p) == '(')
        {
            match_char(p, '(');
            Mugen_Expr* arg = parse_expr(p);
            match_char(p, ')');
            return make_query(p, qid, arg);
        }
        return make_query(p, qid, NULL);
    }

    if (str8_ieq(ident, "helper"))
    {
        skip_ws(p);
        if (peek(p) == '(')
        {
            match_char(p, '(');
            Mugen_Expr* id_expr = parse_expr(p);
            match_char(p, ')');
            skip_ws(p);
            if (peek(p) == ',')
            {
                p->pos++;
                Mugen_Expr* sub = parse_expr(p);
                Mugen_Expr* e = alloc_expr(p, MUGEN_EXPR_REDIRECT);
                e->redirect.target_type = MUGEN_REDIRECT_HELPER;
                e->redirect.id = id_expr;
                e->redirect.sub_expr = sub;
                return e;
            }
            return make_query(p, MUGEN_QUERY_NUMHELPER, id_expr);
        }
        return make_query(p, MUGEN_QUERY_NUMHELPER, NULL);
    }

    if (str8_ieq(ident, "root"))
    {
        skip_ws(p);
        if (peek(p) == ',')
        {
            p->pos++;
            Mugen_Expr* sub = parse_expr(p);
            Mugen_Expr* e = alloc_expr(p, MUGEN_EXPR_REDIRECT);
            e->redirect.target_type = MUGEN_REDIRECT_ROOT;
            e->redirect.id = NULL;
            e->redirect.sub_expr = sub;
            return e;
        }
        return make_int(p, 0);
    }

    if (str8_ieq(ident, "parent"))
    {
        skip_ws(p);
        if (peek(p) == ',')
        {
            p->pos++;
            Mugen_Expr* sub = parse_expr(p);
            Mugen_Expr* e = alloc_expr(p, MUGEN_EXPR_REDIRECT);
            e->redirect.target_type = MUGEN_REDIRECT_PARENT;
            e->redirect.id = NULL;
            e->redirect.sub_expr = sub;
            return e;
        }
        return make_int(p, 0);
    }

    for (const Var_Entry* v = s_vars; v->name; v++)
    {
        if (str8_ieq(ident, v->name))
        {
            match_char(p, '(');
            Mugen_Expr* index = parse_expr(p);
            match_char(p, ')');
            return make_var(p, v->var_type, index);
        }
    }

    for (const Func_Entry* f = s_funcs; f->name; f++)
    {
        if (str8_ieq(ident, f->name))
        {
            match_char(p, '(');
            Mugen_Expr* args[8];
            u8 count = 0;
            if (!match_char(p, ')'))
            {
                args[count++] = parse_expr(p);
                while (match_char(p, ',') && count < 8)
                    args[count++] = parse_expr(p);
                match_char(p, ')');
            }
            Mugen_Expr** arg_arr = mel_alloc(p->alloc, count * sizeof(Mugen_Expr*));
            memcpy(arg_arr, args, count * sizeof(Mugen_Expr*));
            return make_func(p, f->id, arg_arr, count);
        }
    }

    Mugen_Expr* space_query = NULL;
    usize saved_pos = p->pos;
    if (try_query_with_space(p, ident, &space_query))
        return space_query;
    p->pos = saved_pos;

    for (const Query_Entry* q = s_queries; q->name; q++)
    {
        if (strchr(q->name, ' ')) continue;
        if (str8_ieq(ident, q->name))
            return make_query(p, q->id, NULL);
    }

    p->pos = ident_start;
    Mugen_Expr* num = parse_number(p);
    if (num) return num;

    p->pos = ident_start + (usize)ident.len;
    fprintf(stderr, "MUGEN EXPR: unknown identifier '%.*s'\n", (int)ident.len, (char*)ident.data);
    return make_int(p, 0);
}

static Mugen_Expr* parse_mul(Expr_Parser* p)
{
    Mugen_Expr* lhs = parse_primary(p);
    for (;;)
    {
        skip_ws(p);
        u8 c = peek(p);
        u8 op;
        if (c == '*')
        {
            if (p->pos + 1 < (usize)p->text.len && p->text.data[p->pos + 1] == '*')
            { p->pos += 2; op = MUGEN_OP_POW; }
            else { p->pos++; op = MUGEN_OP_MUL; }
        }
        else if (c == '/') { p->pos++; op = MUGEN_OP_DIV; }
        else if (c == '%') { p->pos++; op = MUGEN_OP_MOD; }
        else break;
        Mugen_Expr* rhs = parse_primary(p);
        lhs = make_binary(p, op, lhs, rhs);
    }
    return lhs;
}

static Mugen_Expr* parse_add(Expr_Parser* p)
{
    Mugen_Expr* lhs = parse_mul(p);
    for (;;)
    {
        skip_ws(p);
        u8 c = peek(p);
        if (c == '+') { p->pos++; lhs = make_binary(p, MUGEN_OP_ADD, lhs, parse_mul(p)); }
        else if (c == '-') { p->pos++; lhs = make_binary(p, MUGEN_OP_SUB, lhs, parse_mul(p)); }
        else break;
    }
    return lhs;
}

static Mugen_Expr* parse_cmp(Expr_Parser* p)
{
    Mugen_Expr* lhs = parse_add(p);
    for (;;)
    {
        skip_ws(p);
        u8 c = peek(p);
        if (c == '<')
        {
            p->pos++;
            if (peek(p) == '=') { p->pos++; lhs = make_binary(p, MUGEN_OP_LE, lhs, parse_add(p)); }
            else lhs = make_binary(p, MUGEN_OP_LT, lhs, parse_add(p));
        }
        else if (c == '>')
        {
            p->pos++;
            if (peek(p) == '=') { p->pos++; lhs = make_binary(p, MUGEN_OP_GE, lhs, parse_add(p)); }
            else lhs = make_binary(p, MUGEN_OP_GT, lhs, parse_add(p));
        }
        else break;
    }
    return lhs;
}

static u32 parse_attr_inline(Expr_Parser* p)
{
    u32 flags = 0;
    skip_ws(p);

    bool past_first_comma = false;
    while (!at_end(p))
    {
        u8 c = peek(p);
        if (c == ' ' || c == '\t') { p->pos++; continue; }
        if (c == ',') { p->pos++; past_first_comma = true; continue; }

        if (c >= 'a' && c <= 'z') c -= 32;

        if (!past_first_comma)
        {
            if (c == 'S') flags |= MUGEN_ATTR_S;
            else if (c == 'C') flags |= MUGEN_ATTR_C;
            else if (c == 'A') flags |= MUGEN_ATTR_A;
            else break;
            p->pos++;
        }
        else
        {
            if (!ident_char(c)) break;
            p->pos++;
            skip_ws(p);
            u8 next = peek(p);
            if (next >= 'a' && next <= 'z') next -= 32;
            if (ident_char(next))
            {
                p->pos++;
                if (c == 'N' && next == 'A') flags |= MUGEN_ATTR_NA;
                else if (c == 'S' && next == 'A') flags |= MUGEN_ATTR_SA;
                else if (c == 'H' && next == 'A') flags |= MUGEN_ATTR_HA;
                else if (c == 'N' && next == 'P') flags |= MUGEN_ATTR_NP;
                else if (c == 'S' && next == 'P') flags |= MUGEN_ATTR_SP;
                else if (c == 'H' && next == 'P') flags |= MUGEN_ATTR_HP;
                else if (c == 'N' && next == 'T') flags |= MUGEN_ATTR_NT;
                else if (c == 'S' && next == 'T') flags |= MUGEN_ATTR_ST;
                else if (c == 'H' && next == 'T') flags |= MUGEN_ATTR_HT;
            }
        }
    }

    return flags;
}

static Mugen_Expr* parse_range(Expr_Parser* p, Mugen_Expr* value, bool lo_inclusive, bool negate)
{
    p->pos++;
    Mugen_Expr* lo = parse_add(p);
    match_char(p, ',');
    Mugen_Expr* hi = parse_add(p);
    skip_ws(p);
    bool hi_inclusive = (peek(p) == ']');
    if (peek(p) == ']' || peek(p) == ')') p->pos++;

    Mugen_Expr* e = alloc_expr(p, MUGEN_EXPR_RANGE);
    e->range.value = value;
    e->range.lo = lo;
    e->range.hi = hi;
    e->range.lo_inclusive = lo_inclusive;
    e->range.hi_inclusive = hi_inclusive;

    if (negate)
        return make_unary(p, MUGEN_OP_NOT, e);
    return e;
}

static Mugen_Expr* try_animelem_compound(Expr_Parser* p, Mugen_Expr* eq_node)
{
    if (eq_node->type != MUGEN_EXPR_BINARY) return eq_node;
    if (eq_node->binary.op != MUGEN_OP_EQ) return eq_node;
    if (!eq_node->binary.lhs || eq_node->binary.lhs->type != MUGEN_EXPR_QUERY) return eq_node;
    if (eq_node->binary.lhs->query.id != MUGEN_QUERY_ANIMELEM) return eq_node;

    Mugen_Expr* elem_num = eq_node->binary.rhs;

    skip_ws(p);
    if (peek(p) == ',')
    {
        p->pos++;
        skip_ws(p);

        u8 op = MUGEN_OP_EQ;
        u8 c = peek(p);
        if (c == '<')
        {
            p->pos++;
            if (peek(p) == '=') { p->pos++; op = MUGEN_OP_LE; }
            else op = MUGEN_OP_LT;
        }
        else if (c == '>')
        {
            p->pos++;
            if (peek(p) == '=') { p->pos++; op = MUGEN_OP_GE; }
            else op = MUGEN_OP_GT;
        }
        else if (c == '!' && p->pos + 1 < (usize)p->text.len && p->text.data[p->pos + 1] == '=')
        {
            p->pos += 2;
            op = MUGEN_OP_NEQ;
        }
        else if (c == '=')
        {
            p->pos++;
            op = MUGEN_OP_EQ;
        }

        Mugen_Expr* offset = parse_add(p);
        return make_binary(p, op, make_query(p, MUGEN_QUERY_ANIMELEMTIME, elem_num), offset);
    }

    return make_binary(p, MUGEN_OP_EQ, make_query(p, MUGEN_QUERY_ANIMELEMTIME, elem_num), make_int(p, 0));
}

static Mugen_Expr* parse_eq(Expr_Parser* p)
{
    Mugen_Expr* lhs = parse_cmp(p);
    for (;;)
    {
        skip_ws(p);
        u8 c = peek(p);
        if (c == '=' && (p->pos + 1 >= (usize)p->text.len || p->text.data[p->pos + 1] != '='))
        {
            p->pos++;
            skip_ws(p);

            bool is_hitdefattr = (lhs && lhs->type == MUGEN_EXPR_QUERY &&
                                  lhs->query.id == MUGEN_QUERY_HITDEFATTR);
            if (is_hitdefattr)
            {
                u32 mask = parse_attr_inline(p);
                lhs = make_binary(p, MUGEN_OP_EQ, lhs, make_int(p, (i32)mask));
            }
            else
            {
                u8 next = peek(p);
                if (next == '[' || next == '(')
                {
                    lhs = parse_range(p, lhs, next == '[', false);
                }
                else
                {
                    Mugen_Expr* rhs = parse_cmp(p);
                    lhs = make_binary(p, MUGEN_OP_EQ, lhs, rhs);
                    lhs = try_animelem_compound(p, lhs);
                }
            }
        }
        else if (c == '!' && p->pos + 1 < (usize)p->text.len && p->text.data[p->pos + 1] == '=')
        {
            p->pos += 2;
            skip_ws(p);

            bool is_hitdefattr = (lhs && lhs->type == MUGEN_EXPR_QUERY &&
                                  lhs->query.id == MUGEN_QUERY_HITDEFATTR);
            if (is_hitdefattr)
            {
                u32 mask = parse_attr_inline(p);
                lhs = make_binary(p, MUGEN_OP_NEQ, lhs, make_int(p, (i32)mask));
            }
            else
            {
                u8 next = peek(p);
                if (next == '[' || next == '(')
                {
                    lhs = parse_range(p, lhs, next == '[', true);
                }
                else
                {
                    lhs = make_binary(p, MUGEN_OP_NEQ, lhs, parse_cmp(p));
                }
            }
        }
        else break;
    }
    return lhs;
}

static Mugen_Expr* parse_band(Expr_Parser* p)
{
    Mugen_Expr* lhs = parse_eq(p);
    for (;;)
    {
        skip_ws(p);
        if (peek(p) == '&' && p->pos + 1 < (usize)p->text.len && p->text.data[p->pos + 1] != '&')
        { p->pos++; lhs = make_binary(p, MUGEN_OP_BAND, lhs, parse_eq(p)); }
        else break;
    }
    return lhs;
}

static Mugen_Expr* parse_bor(Expr_Parser* p)
{
    Mugen_Expr* lhs = parse_band(p);
    for (;;)
    {
        skip_ws(p);
        if (peek(p) == '|' && p->pos + 1 < (usize)p->text.len && p->text.data[p->pos + 1] != '|')
        { p->pos++; lhs = make_binary(p, MUGEN_OP_BOR, lhs, parse_band(p)); }
        else break;
    }
    return lhs;
}

static Mugen_Expr* parse_and(Expr_Parser* p)
{
    Mugen_Expr* lhs = parse_bor(p);
    for (;;)
    {
        skip_ws(p);
        if (peek(p) == '&' && p->pos + 1 < (usize)p->text.len && p->text.data[p->pos + 1] == '&')
        { p->pos += 2; lhs = make_binary(p, MUGEN_OP_AND, lhs, parse_bor(p)); }
        else break;
    }
    return lhs;
}

static Mugen_Expr* parse_xor(Expr_Parser* p)
{
    Mugen_Expr* lhs = parse_and(p);
    for (;;)
    {
        skip_ws(p);
        if (peek(p) == '^' && p->pos + 1 < (usize)p->text.len && p->text.data[p->pos + 1] == '^')
        { p->pos += 2; lhs = make_binary(p, MUGEN_OP_XOR, lhs, parse_and(p)); }
        else break;
    }
    return lhs;
}

static Mugen_Expr* parse_or(Expr_Parser* p)
{
    Mugen_Expr* lhs = parse_xor(p);
    for (;;)
    {
        skip_ws(p);
        if (peek(p) == '|' && p->pos + 1 < (usize)p->text.len && p->text.data[p->pos + 1] == '|')
        { p->pos += 2; lhs = make_binary(p, MUGEN_OP_OR, lhs, parse_xor(p)); }
        else break;
    }
    return lhs;
}

static Mugen_Expr* parse_expr(Expr_Parser* p)
{
    return parse_or(p);
}

Mugen_Expr* mugen_expr_parse(str8 text, const Mel_Alloc* alloc)
{
    Expr_Parser p = { .text = text, .pos = 0, .alloc = alloc };
    return parse_expr(&p);
}

f32 mugen_expr_eval(Mugen_Expr* expr, Mugen_Char_State* state)
{
    if (!expr) return 0.0f;

    switch (expr->type)
    {
        case MUGEN_EXPR_LIT_INT:   return (f32)expr->lit_int;
        case MUGEN_EXPR_LIT_FLOAT: return expr->lit_float;
        case MUGEN_EXPR_LIT_STRING: return 0.0f;

        case MUGEN_EXPR_UNARY:
        {
            f32 val = mugen_expr_eval(expr->unary.operand, state);
            switch (expr->unary.op)
            {
                case MUGEN_OP_NEG: return -val;
                case MUGEN_OP_NOT: return val == 0.0f ? 1.0f : 0.0f;
            }
            return 0.0f;
        }

        case MUGEN_EXPR_BINARY:
        {
            if (expr->binary.op == MUGEN_OP_EQ || expr->binary.op == MUGEN_OP_NEQ)
            {
                bool is_command = (expr->binary.lhs && expr->binary.lhs->type == MUGEN_EXPR_QUERY &&
                                   expr->binary.lhs->query.id == MUGEN_QUERY_COMMAND);
                if (is_command && expr->binary.rhs && expr->binary.rhs->type == MUGEN_EXPR_LIT_STRING)
                {
                    bool active = false;
                    if (state->commands)
                        active = command_list_active(state->commands, expr->binary.rhs->lit_string);
                    return (expr->binary.op == MUGEN_OP_EQ) ? (active ? 1.0f : 0.0f) : (active ? 0.0f : 1.0f);
                }

                bool is_statetype = (expr->binary.lhs && expr->binary.lhs->type == MUGEN_EXPR_QUERY &&
                                     (expr->binary.lhs->query.id == MUGEN_QUERY_STATETYPE ||
                                      expr->binary.lhs->query.id == MUGEN_QUERY_P2STATETYPE));
                if (is_statetype && expr->binary.rhs && expr->binary.rhs->type == MUGEN_EXPR_LIT_INT)
                {
                    u8 st = expr->binary.lhs->query.id == MUGEN_QUERY_P2STATETYPE
                        ? state->p2_statetype : state->statetype;
                    u8 expected = 0;
                    i32 val = expr->binary.rhs->lit_int;
                    if (val == 'S') expected = MUGEN_PHYSICS_S;
                    else if (val == 'C') expected = MUGEN_PHYSICS_C;
                    else if (val == 'A') expected = MUGEN_PHYSICS_A;
                    else if (val == 'L') expected = MUGEN_PHYSICS_L;
                    bool eq = (st == expected);
                    return (expr->binary.op == MUGEN_OP_EQ) ? (eq ? 1.0f : 0.0f) : (eq ? 0.0f : 1.0f);
                }

                bool is_hitdefattr = (expr->binary.lhs && expr->binary.lhs->type == MUGEN_EXPR_QUERY &&
                                     expr->binary.lhs->query.id == MUGEN_QUERY_HITDEFATTR);
                if (is_hitdefattr && expr->binary.rhs && expr->binary.rhs->type == MUGEN_EXPR_LIT_INT)
                {
                    u32 mask = (u32)expr->binary.rhs->lit_int;
                    u32 state_mask = mask & (MUGEN_ATTR_S | MUGEN_ATTR_C | MUGEN_ATTR_A);
                    u32 attack_mask = mask & ~(MUGEN_ATTR_S | MUGEN_ATTR_C | MUGEN_ATTR_A);
                    bool match = false;
                    if (state->movetype == MUGEN_MOVETYPE_A && state->hitdef_active)
                    {
                        u32 hd_state = state->hitdef.attr & (MUGEN_ATTR_S | MUGEN_ATTR_C | MUGEN_ATTR_A);
                        u32 hd_attack = state->hitdef.attr & ~(MUGEN_ATTR_S | MUGEN_ATTR_C | MUGEN_ATTR_A);
                        match = (hd_state & state_mask) != 0 && (hd_attack & attack_mask) != 0;
                    }
                    return (expr->binary.op == MUGEN_OP_EQ) ? (match ? 1.0f : 0.0f) : (match ? 0.0f : 1.0f);
                }

                bool is_movetype = (expr->binary.lhs && expr->binary.lhs->type == MUGEN_EXPR_QUERY &&
                                    (expr->binary.lhs->query.id == MUGEN_QUERY_MOVETYPE ||
                                     expr->binary.lhs->query.id == MUGEN_QUERY_P2MOVETYPE));
                if (is_movetype && expr->binary.rhs && expr->binary.rhs->type == MUGEN_EXPR_LIT_INT)
                {
                    u8 mt = expr->binary.lhs->query.id == MUGEN_QUERY_P2MOVETYPE
                        ? state->p2_movetype : state->movetype;
                    u8 expected = 0;
                    i32 val = expr->binary.rhs->lit_int;
                    if (val == 'I') expected = MUGEN_MOVETYPE_I;
                    else if (val == 'A') expected = MUGEN_MOVETYPE_A;
                    else if (val == 'H') expected = MUGEN_MOVETYPE_H;
                    bool eq = (mt == expected);
                    return (expr->binary.op == MUGEN_OP_EQ) ? (eq ? 1.0f : 0.0f) : (eq ? 0.0f : 1.0f);
                }
            }

            f32 l = mugen_expr_eval(expr->binary.lhs, state);
            f32 r = mugen_expr_eval(expr->binary.rhs, state);

            switch (expr->binary.op)
            {
                case MUGEN_OP_ADD: return l + r;
                case MUGEN_OP_SUB: return l - r;
                case MUGEN_OP_MUL: return l * r;
                case MUGEN_OP_DIV: return r != 0.0f ? l / r : 0.0f;
                case MUGEN_OP_MOD: return r != 0.0f ? fmodf(l, r) : 0.0f;
                case MUGEN_OP_EQ:  return l == r ? 1.0f : 0.0f;
                case MUGEN_OP_NEQ: return l != r ? 1.0f : 0.0f;
                case MUGEN_OP_LT:  return l < r ? 1.0f : 0.0f;
                case MUGEN_OP_LE:  return l <= r ? 1.0f : 0.0f;
                case MUGEN_OP_GT:  return l > r ? 1.0f : 0.0f;
                case MUGEN_OP_GE:  return l >= r ? 1.0f : 0.0f;
                case MUGEN_OP_AND: return (l != 0.0f && r != 0.0f) ? 1.0f : 0.0f;
                case MUGEN_OP_OR:  return (l != 0.0f || r != 0.0f) ? 1.0f : 0.0f;
                case MUGEN_OP_XOR: return ((l != 0.0f) != (r != 0.0f)) ? 1.0f : 0.0f;
                case MUGEN_OP_BAND: return (f32)((i32)l & (i32)r);
                case MUGEN_OP_BOR:  return (f32)((i32)l | (i32)r);
                case MUGEN_OP_POW: return powf(l, r);
            }
            return 0.0f;
        }

        case MUGEN_EXPR_FUNC:
        {
            switch (expr->func.id)
            {
                case MUGEN_FUNC_CEIL:
                    return expr->func.arg_count > 0 ? ceilf(mugen_expr_eval(expr->func.args[0], state)) : 0.0f;
                case MUGEN_FUNC_FLOOR:
                    return expr->func.arg_count > 0 ? floorf(mugen_expr_eval(expr->func.args[0], state)) : 0.0f;
                case MUGEN_FUNC_ABS:
                    return expr->func.arg_count > 0 ? fabsf(mugen_expr_eval(expr->func.args[0], state)) : 0.0f;
                case MUGEN_FUNC_IFELSE:
                    if (expr->func.arg_count >= 3)
                    {
                        f32 cond = mugen_expr_eval(expr->func.args[0], state);
                        return cond != 0.0f ? mugen_expr_eval(expr->func.args[1], state)
                                            : mugen_expr_eval(expr->func.args[2], state);
                    }
                    return 0.0f;
            }
            return 0.0f;
        }

        case MUGEN_EXPR_QUERY:
        {
            if (!state) return 0.0f;
            switch (expr->query.id)
            {
                case MUGEN_QUERY_TIME:         return (f32)state->time;
                case MUGEN_QUERY_ANIMTIME:     return (f32)state->animtime;
                case MUGEN_QUERY_ANIMELEM:
                    return (f32)state->animelem;
                case MUGEN_QUERY_ANIMELEMTIME:
                {
                    if (expr->query.arg)
                    {
                        i32 elem = (i32)mugen_expr_eval(expr->query.arg, state);
                        i32 idx = elem - 1;
                        if (idx >= 0 && idx < (i32)state->anim_elem_count)
                            return (f32)(state->time - state->anim_elem_start_ticks[idx]);
                        if (idx < 0) return (f32)state->time;
                        return -9999.0f;
                    }
                    return (f32)state->animelemtime;
                }
                case MUGEN_QUERY_STATENO:      return (f32)state->stateno;
                case MUGEN_QUERY_PREVSTATENO:  return (f32)state->prevstateno;
                case MUGEN_QUERY_STATETYPE:    return (f32)state->statetype;
                case MUGEN_QUERY_MOVETYPE:     return (f32)state->movetype;
                case MUGEN_QUERY_CTRL:         return state->ctrl ? 1.0f : 0.0f;
                case MUGEN_QUERY_COMMAND:      return 0.0f;
                case MUGEN_QUERY_ANIM:         return (f32)state->anim;
                case MUGEN_QUERY_VEL_X:        return state->vel_x;
                case MUGEN_QUERY_VEL_Y:        return state->vel_y;
                case MUGEN_QUERY_POS_X:        return state->pos_x;
                case MUGEN_QUERY_POS_Y:        return state->pos_y;
                case MUGEN_QUERY_MOVECONTACT:  return (f32)(state->mctime > 0 ? state->mctime : 0);
                case MUGEN_QUERY_MOVEHIT:      return (f32)(state->movehit > 0 ? state->movehit : 0);
                case MUGEN_QUERY_MOVEGUARDED:  return (f32)(state->moveguarded > 0 ? state->moveguarded : 0);
                case MUGEN_QUERY_HITCOUNT:     return (f32)state->hitcount;
                case MUGEN_QUERY_LIFE:         return state->life;
                case MUGEN_QUERY_LIFEMAX:      return state->lifemax;
                case MUGEN_QUERY_POWER:        return state->power;
                case MUGEN_QUERY_POWERMAX:     return state->powermax;
                case MUGEN_QUERY_FACING:       return state->facing;
                case MUGEN_QUERY_ALIVE:        return state->alive ? 1.0f : 0.0f;
                case MUGEN_QUERY_RANDOM:       return (f32)(mugen_rng_next(&state->rng_state) % 1000);
                case MUGEN_QUERY_GAMETIME:     return (f32)state->gametime;
                case MUGEN_QUERY_ROUNDSTATE:   return (f32)state->roundstate;
                case MUGEN_QUERY_ISHELPER:     return state->is_helper ? 1.0f : 0.0f;
                case MUGEN_QUERY_NUMHELPER:
                {
                    if (state->query_num_helper)
                    {
                        i32 id = expr->query.arg ? (i32)mugen_expr_eval(expr->query.arg, state) : 0;
                        return (f32)state->query_num_helper(state->helper_ctx, id);
                    }
                    return 0.0f;
                }
                case MUGEN_QUERY_P2BODYDIST_X:
                {
                    f32 dist = state->p2_pos_x - state->pos_x;
                    if (state->facing < 0) dist = -dist;
                    dist -= state->ground_front + state->p2_width;
                    if (dist < 0) dist = 0;
                    return dist;
                }
                case MUGEN_QUERY_P2DIST_X:
                {
                    f32 dist = state->p2_pos_x - state->pos_x;
                    if (state->facing < 0) dist = -dist;
                    return dist;
                }
                case MUGEN_QUERY_P2DIST_Y:
                    return state->p2_pos_y - state->pos_y;
                case MUGEN_QUERY_CONST:
                {
                    if (!expr->query.arg || expr->query.arg->type != MUGEN_EXPR_LIT_STRING) return 0.0f;
                    str8 name = expr->query.arg->lit_string;
                    if (str8_ieq(name, "velocity.walk.fwd.x"))     return state->walk_fwd_x;
                    if (str8_ieq(name, "velocity.walk.back.x"))    return state->walk_back_x;
                    if (str8_ieq(name, "velocity.jump.neu.x"))     return state->jump_neu_x;
                    if (str8_ieq(name, "velocity.jump.fwd.x"))     return state->jump_fwd_x;
                    if (str8_ieq(name, "velocity.jump.back.x"))    return state->jump_back_x;
                    if (str8_ieq(name, "velocity.jump.y"))         return state->jump_y;
                    if (str8_ieq(name, "velocity.runjump.fwd.x"))  return state->runjump_fwd_x;
                    if (str8_ieq(name, "velocity.runjump.back.x")) return state->runjump_back_x;
                    if (str8_ieq(name, "velocity.runjump.y"))      return state->runjump_y;
                    if (str8_ieq(name, "velocity.run.fwd.x"))     return state->run_fwd_x;
                    if (str8_ieq(name, "velocity.run.back.x"))    return state->run_back_x;
                    if (str8_ieq(name, "velocity.run.back.y"))    return state->run_back_y;
                    if (str8_ieq(name, "velocity.airjump.neu.x")) return state->airjump_neu_x;
                    if (str8_ieq(name, "velocity.airjump.fwd.x")) return state->airjump_fwd_x;
                    if (str8_ieq(name, "velocity.airjump.back.x")) return state->airjump_back_x;
                    if (str8_ieq(name, "velocity.airjump.y"))     return state->airjump_y;
                    if (str8_ieq(name, "movement.stand.friction.threshold")) return state->stand_friction_threshold;
                    if (str8_ieq(name, "movement.crouch.friction.threshold")) return state->crouch_friction_threshold;
                    if (str8_ieq(name, "movement.yaccel"))        return state->gravity;
                    if (str8_ieq(name, "data.attack"))             return state->data_attack;
                    if (str8_ieq(name, "size.ground.front"))      return state->ground_front;
                    if (str8_ieq(name, "size.ground.back"))       return state->ground_back;
                    if (str8_ieq(name, "size.height"))            return state->data_height;
                    if (str8_ieq(name, "data.defence"))           return state->data_defence;
                    if (str8_ieq(name, "data.liedown.time"))      return state->data_liedown_time;
                    if (str8_ieq(name, "data.airjuggle"))         return state->data_airjuggle;
                    if (str8_ieq(name, "data.life"))             return state->lifemax;
                    if (str8_ieq(name, "data.power"))            return state->powermax;
                    if (str8_ieq(name, "data.sparkno"))          return state->data_sparkno;
                    if (str8_ieq(name, "data.guard.sparkno"))    return state->data_guard_sparkno;
                    if (str8_ieq(name, "size.xscale"))           return state->data_xscale;
                    if (str8_ieq(name, "size.yscale"))           return state->data_yscale;
                    if (str8_ieq(name, "size.air.front"))        return state->data_air_front;
                    if (str8_ieq(name, "size.air.back"))         return state->data_air_back;
                    if (str8_ieq(name, "movement.airjump.num"))  return state->data_airjump_num;
                    if (str8_ieq(name, "movement.airjump.height")) return state->data_airjump_height;
                    if (str8_ieq(name, "movement.down.bounce.offset.x")) return state->down_bounce_offset_x;
                    if (str8_ieq(name, "movement.down.bounce.offset.y")) return state->down_bounce_offset_y;
                    if (str8_ieq(name, "movement.down.bounce.yaccel"))   return state->down_bounce_yaccel;
                    if (str8_ieq(name, "movement.down.bounce.groundlevel")) return state->down_bounce_groundlevel;
                    if (str8_ieq(name, "movement.down.friction.threshold")) return state->down_friction_threshold;
                    return 0.0f;
                }
                case MUGEN_QUERY_SELFANIMEXIST:
                {
                    if (!state->anim_exists) return 0.0f;
                    i32 anim_id = expr->query.arg ? (i32)mugen_expr_eval(expr->query.arg, state) : (i32)state->anim;
                    return state->anim_exists(state->anim_exists_ctx, (u32)anim_id) ? 1.0f : 0.0f;
                }
                case MUGEN_QUERY_GETHITVAR:
                {
                    if (!expr->query.arg || expr->query.arg->type != MUGEN_EXPR_LIT_STRING) return 0.0f;
                    str8 name = expr->query.arg->lit_string;
                    Mugen_GetHitVar* ghv = &state->ghv;
                    if (str8_ieq(name, "animtype"))        return (f32)ghv->animtype;
                    if (str8_ieq(name, "air.animtype"))    return (f32)ghv->air_animtype;
                    if (str8_ieq(name, "ground.animtype")) return (f32)ghv->ground_animtype;
                    if (str8_ieq(name, "groundtype"))      return (f32)ghv->groundtype;
                    if (str8_ieq(name, "airtype"))          return (f32)ghv->airtype;
                    if (str8_ieq(name, "damage"))          return (f32)ghv->damage;
                    if (str8_ieq(name, "hitcount"))        return (f32)ghv->hitcount;
                    if (str8_ieq(name, "guardcount"))      return (f32)ghv->guardcount;
                    if (str8_ieq(name, "hitshaketime"))    return (f32)ghv->hitshaketime;
                    if (str8_ieq(name, "hittime"))         return (f32)ghv->hittime;
                    if (str8_ieq(name, "slidetime"))       return (f32)ghv->slidetime;
                    if (str8_ieq(name, "ctrltime"))        return (f32)ghv->ctrltime;
                    if (str8_ieq(name, "xvel"))            return ghv->xvel;
                    if (str8_ieq(name, "yvel"))            return ghv->yvel;
                    if (str8_ieq(name, "xaccel"))          return ghv->xaccel;
                    if (str8_ieq(name, "yaccel"))          return ghv->yaccel;
                    if (str8_ieq(name, "xoff"))            return ghv->xoff;
                    if (str8_ieq(name, "yoff"))            return ghv->yoff;
                    if (str8_ieq(name, "isbound"))         return ghv->isbound ? 1.0f : 0.0f;
                    if (str8_ieq(name, "guarded"))         return ghv->guarded ? 1.0f : 0.0f;
                    if (str8_ieq(name, "fall"))            return ghv->fallflag ? 1.0f : 0.0f;
                    if (str8_ieq(name, "fall.recover"))    return ghv->fall_recover ? 1.0f : 0.0f;
                    if (str8_ieq(name, "fall.recovertime")) return (f32)ghv->fall_recovertime;
                    if (str8_ieq(name, "fall.xvel"))       return ghv->fall_xvel;
                    if (str8_ieq(name, "fall.yvel"))       return ghv->fall_yvel;
                    if (str8_ieq(name, "fall.damage"))     return (f32)ghv->fall_damage;
                    if (str8_ieq(name, "fall.kill"))       return ghv->fall_kill ? 1.0f : 0.0f;
                    if (str8_ieq(name, "attr"))            return (f32)ghv->attr;
                    if (str8_ieq(name, "priority"))        return (f32)ghv->priority;
                    if (str8_ieq(name, "forcestand"))      return ghv->forcestand ? 1.0f : 0.0f;
                    return 0.0f;
                }
                case MUGEN_QUERY_HITSHAKEOVER:
                    return state->ghv.hitshaketime <= 0 ? 1.0f : 0.0f;
                case MUGEN_QUERY_HITOVER:
                    return state->ghv.hittime <= 0 ? 1.0f : 0.0f;
                case MUGEN_QUERY_HITFALL:
                    return state->ghv.fallflag ? 1.0f : 0.0f;
                case MUGEN_QUERY_P2STATETYPE:
                    return (f32)state->p2_statetype;
                case MUGEN_QUERY_P2MOVETYPE:
                    return (f32)state->p2_movetype;
                case MUGEN_QUERY_FRONTEDGEDIST:
                {
                    if (state->facing > 0)
                        return state->stage_right - state->pos_x;
                    return state->pos_x - state->stage_left;
                }
                case MUGEN_QUERY_BACKEDGEDIST:
                {
                    if (state->facing > 0)
                        return state->pos_x - state->stage_left;
                    return state->stage_right - state->pos_x;
                }
                case MUGEN_QUERY_FRONTEDGEBODYDIST:
                {
                    if (state->facing > 0)
                        return state->stage_right - state->pos_x - state->ground_front;
                    return state->pos_x - state->stage_left - state->ground_front;
                }
                case MUGEN_QUERY_BACKEDGEBODYDIST:
                {
                    if (state->facing > 0)
                        return state->pos_x - state->stage_left - state->ground_back;
                    return state->stage_right - state->pos_x - state->ground_back;
                }
                case MUGEN_QUERY_ROUNDNO:
                    return (f32)state->roundno;
                case MUGEN_QUERY_ROUNDSEXISTED:
                    return (f32)state->roundsexisted;
                case MUGEN_QUERY_INGUARDDIST:
                {
                    f32 dist = state->p2_pos_x - state->pos_x;
                    if (state->facing < 0) dist = -dist;
                    return (dist >= 0 && dist <= state->attack_dist) ? 1.0f : 0.0f;
                }
                case MUGEN_QUERY_CANRECOVER:
                    return (state->ghv.fall_recover && state->fall_time >= state->ghv.fall_recovertime) ? 1.0f : 0.0f;
                case MUGEN_QUERY_PALNO:
                    return (f32)state->palno;
                case MUGEN_QUERY_LOSE:
                    return state->lose ? 1.0f : 0.0f;
                case MUGEN_QUERY_WIN:
                    return state->win ? 1.0f : 0.0f;
                case MUGEN_QUERY_MATCHOVER:
                    return state->matchover ? 1.0f : 0.0f;
                case MUGEN_QUERY_NUMTARGET:
                    return (f32)state->target_count;
                case MUGEN_QUERY_NUMPROJID:
                    return 0.0f;
            }
            return 0.0f;
        }

        case MUGEN_EXPR_VAR:
        {
            if (!state) return 0.0f;
            i32 idx = (i32)mugen_expr_eval(expr->var.index, state);
            switch (expr->var.var_type)
            {
                case MUGEN_VAR_INT:
                    return (idx >= 0 && idx < 60) ? (f32)state->var[idx] : 0.0f;
                case MUGEN_VAR_FLOAT:
                    return (idx >= 0 && idx < 40) ? state->fvar[idx] : 0.0f;
                case MUGEN_VAR_SYSINT:
                    return (idx >= 0 && idx < 5) ? (f32)state->sysvar[idx] : 0.0f;
                case MUGEN_VAR_SYSFLOAT:
                    return (idx >= 0 && idx < 5) ? state->sysfvar[idx] : 0.0f;
            }
            return 0.0f;
        }

        case MUGEN_EXPR_REDIRECT:
        {
            if (!state) return 0.0f;
            Mugen_Char_State* target = NULL;
            switch (expr->redirect.target_type)
            {
                case MUGEN_REDIRECT_HELPER:
                    if (state->query_helper_state)
                    {
                        i32 id = expr->redirect.id ? (i32)mugen_expr_eval(expr->redirect.id, state) : 0;
                        target = state->query_helper_state(state->helper_ctx, id);
                    }
                    break;
                case MUGEN_REDIRECT_ROOT:
                    if (state->query_root_state)
                        target = state->query_root_state(state->helper_ctx);
                    break;
                case MUGEN_REDIRECT_PARENT:
                    if (state->query_root_state)
                        target = state->query_root_state(state->helper_ctx);
                    break;
            }
            if (!target) return 0.0f;
            return mugen_expr_eval(expr->redirect.sub_expr, target);
        }

        case MUGEN_EXPR_RANGE:
        {
            f32 val = mugen_expr_eval(expr->range.value, state);
            f32 lo = mugen_expr_eval(expr->range.lo, state);
            f32 hi = mugen_expr_eval(expr->range.hi, state);
            bool lo_ok = expr->range.lo_inclusive ? (val >= lo) : (val > lo);
            bool hi_ok = expr->range.hi_inclusive ? (val <= hi) : (val < hi);
            return (lo_ok && hi_ok) ? 1.0f : 0.0f;
        }
    }

    return 0.0f;
}

bool mugen_expr_eval_bool(Mugen_Expr* expr, Mugen_Char_State* state)
{
    return mugen_expr_eval(expr, state) != 0.0f;
}
