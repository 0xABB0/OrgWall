#pragma once

#include "mugen_cns.h"
#include "string.str8.h"
#include "allocator.h"
#include <string.h>
#include <stdlib.h>

static inline str8 mcns_trim(str8 s)
{
    size start = 0;
    while (start < s.len && (s.data[start] == ' ' || s.data[start] == '\t')) start++;
    size end = s.len;
    while (end > start && (s.data[end - 1] == ' ' || s.data[end - 1] == '\t')) end--;
    return str8_from_parts(s.data + start, end - start);
}


static inline bool mcns_starts_with_i(str8 s, const char* prefix)
{
    size plen = (size)strlen(prefix);
    if (s.len < plen) return false;
    for (size i = 0; i < plen; i++)
    {
        u8 a = s.data[i]; if (a >= 'A' && a <= 'Z') a += 32;
        u8 b = (u8)prefix[i]; if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return false;
    }
    return true;
}

static inline str8 mcns_split_comma_first(str8 s, str8* rest)
{
    for (size i = 0; i < s.len; i++)
    {
        if (s.data[i] == ',')
        {
            *rest = mcns_trim(str8_from_parts(s.data + i + 1, s.len - i - 1));
            return mcns_trim(str8_from_parts(s.data, i));
        }
    }
    *rest = (str8){0};
    return mcns_trim(s);
}

static inline void* mcns_alloc_params(const Mel_Alloc* alloc, usize size)
{
    void* p = mel_alloc(alloc, size);
    memset(p, 0, size);
    return p;
}

static inline f32 mcns_eval_or_default(Mugen_Expr* e, Mugen_Char_State* st, f32 def)
{
    return e ? mugen_expr_eval(e, st) : def;
}

static inline u8 mcns_parse_animtype(str8 s)
{
    s = mcns_trim(s);
    if (str8_ieq_cstr(s, "light"))  return MUGEN_ANIMTYPE_LIGHT;
    if (str8_ieq_cstr(s, "medium")) return MUGEN_ANIMTYPE_MEDIUM;
    if (str8_ieq_cstr(s, "hard"))   return MUGEN_ANIMTYPE_HARD;
    if (str8_ieq_cstr(s, "back"))   return MUGEN_ANIMTYPE_BACK;
    if (str8_ieq_cstr(s, "up"))     return MUGEN_ANIMTYPE_UP;
    if (str8_ieq_cstr(s, "diagup")) return MUGEN_ANIMTYPE_DIAGUP;
    return MUGEN_ANIMTYPE_LIGHT;
}

static inline u8 mcns_parse_groundtype(str8 s)
{
    s = mcns_trim(s);
    if (str8_ieq_cstr(s, "high")) return MUGEN_GROUNDTYPE_HIGH;
    if (str8_ieq_cstr(s, "low"))  return MUGEN_GROUNDTYPE_LOW;
    if (str8_ieq_cstr(s, "trip")) return MUGEN_GROUNDTYPE_TRIP;
    return MUGEN_GROUNDTYPE_HIGH;
}

static inline u32 mcns_parse_attr(str8 s)
{
    u32 flags = 0;
    str8 rest;
    str8 state_part = mcns_split_comma_first(s, &rest);

    for (size i = 0; i < state_part.len; i++)
    {
        u8 c = state_part.data[i]; if (c >= 'a' && c <= 'z') c -= 32;
        if (c == 'S') flags |= MUGEN_ATTR_S;
        if (c == 'C') flags |= MUGEN_ATTR_C;
        if (c == 'A') flags |= MUGEN_ATTR_A;
    }

    str8 atk = mcns_trim(rest);
    for (size i = 0; i + 1 < atk.len; i++)
    {
        u8 a = atk.data[i]; if (a >= 'a' && a <= 'z') a -= 32;
        u8 b = atk.data[i + 1]; if (b >= 'a' && b <= 'z') b -= 32;
        if (a == 'N' && b == 'A') { flags |= MUGEN_ATTR_NA; i++; }
        else if (a == 'S' && b == 'A') { flags |= MUGEN_ATTR_SA; i++; }
        else if (a == 'H' && b == 'A') { flags |= MUGEN_ATTR_HA; i++; }
        else if (a == 'N' && b == 'P') { flags |= MUGEN_ATTR_NP; i++; }
        else if (a == 'S' && b == 'P') { flags |= MUGEN_ATTR_SP; i++; }
        else if (a == 'H' && b == 'P') { flags |= MUGEN_ATTR_HP; i++; }
        else if (a == 'N' && b == 'T') { flags |= MUGEN_ATTR_NT; i++; }
        else if (a == 'S' && b == 'T') { flags |= MUGEN_ATTR_ST; i++; }
        else if (a == 'H' && b == 'T') { flags |= MUGEN_ATTR_HT; i++; }
    }

    return flags;
}

static inline u32 mcns_parse_hitflag(str8 s)
{
    u32 flags = 0;
    for (size i = 0; i < s.len; i++)
    {
        u8 c = s.data[i]; if (c >= 'a' && c <= 'z') c -= 32;
        if (c == 'H') flags |= MUGEN_HF_H;
        if (c == 'L') flags |= MUGEN_HF_L;
        if (c == 'M') flags |= MUGEN_HF_M;
        if (c == 'A') flags |= MUGEN_HF_A;
        if (c == 'D') flags |= MUGEN_HF_D;
        if (c == 'F') flags |= MUGEN_HF_F;
        if (c == 'P') flags |= MUGEN_HF_P;
        if (c == '-') flags |= MUGEN_HF_MNS;
        if (c == '+') flags |= MUGEN_HF_PLS;
    }
    return flags;
}
