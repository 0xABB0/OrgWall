#include "mugen_cns.h"
#include "string.str8.h"
#include "allocator.h"
#include <SDL3/SDL.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>

typedef struct {
    str8 data;
    usize pos;
} Line_Parser;

static str8 next_line(Line_Parser* p)
{
    if (p->pos >= (usize)p->data.len) return (str8){0};
    usize start = p->pos;
    while (p->pos < (usize)p->data.len && p->data.data[p->pos] != '\n')
        p->pos++;
    usize end = p->pos;
    if (end > start && p->data.data[end - 1] == '\r') end--;
    if (p->pos < (usize)p->data.len) p->pos++;
    return str8_from_parts(p->data.data + start, (size)(end - start));
}

static str8 trim(str8 s)
{
    size start = 0;
    while (start < s.len && (s.data[start] == ' ' || s.data[start] == '\t')) start++;
    size end = s.len;
    while (end > start && (s.data[end - 1] == ' ' || s.data[end - 1] == '\t')) end--;
    return str8_from_parts(s.data + start, end - start);
}

static str8 strip_comment(str8 line)
{
    for (size i = 0; i < line.len; i++)
    {
        if (line.data[i] == ';')
            return str8_from_parts(line.data, i);
    }
    return line;
}

static bool starts_with_i(str8 s, const char* prefix)
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

static bool str8_ieq(str8 a, const char* b)
{
    size blen = (size)strlen(b);
    if (a.len != blen) return false;
    for (size i = 0; i < blen; i++)
    {
        u8 ac = a.data[i]; if (ac >= 'A' && ac <= 'Z') ac += 32;
        u8 bc = (u8)b[i]; if (bc >= 'A' && bc <= 'Z') bc += 32;
        if (ac != bc) return false;
    }
    return true;
}

static str8 after_eq(str8 line)
{
    for (size i = 0; i < line.len; i++)
    {
        if (line.data[i] == '=')
            return trim(str8_from_parts(line.data + i + 1, line.len - i - 1));
    }
    return (str8){0};
}

static str8 before_eq(str8 line)
{
    for (size i = 0; i < line.len; i++)
    {
        if (line.data[i] == '=')
            return trim(str8_from_parts(line.data, i));
    }
    return trim(line);
}

static i32 parse_int(str8 s)
{
    if (s.len == 0) return 0;
    char buf[32];
    size len = s.len < 31 ? s.len : 31;
    memcpy(buf, s.data, (size_t)len);
    buf[len] = 0;
    return (i32)strtol(buf, NULL, 10);
}

static f32 parse_float(str8 s)
{
    if (s.len == 0) return 0.0f;
    char buf[32];
    size len = s.len < 31 ? s.len : 31;
    memcpy(buf, s.data, (size_t)len);
    buf[len] = 0;
    return strtof(buf, NULL);
}

static str8 split_comma_first(str8 s, str8* rest)
{
    for (size i = 0; i < s.len; i++)
    {
        if (s.data[i] == ',')
        {
            *rest = trim(str8_from_parts(s.data + i + 1, s.len - i - 1));
            return trim(str8_from_parts(s.data, i));
        }
    }
    *rest = (str8){0};
    return trim(s);
}

static u8 parse_statetype_char(str8 s)
{
    s = trim(s);
    if (s.len == 0) return 0;
    u8 c = s.data[0];
    if (c >= 'a' && c <= 'z') c -= 32;
    switch (c)
    {
        case 'S': return MUGEN_PHYSICS_S;
        case 'C': return MUGEN_PHYSICS_C;
        case 'A': return MUGEN_PHYSICS_A;
        case 'N': return MUGEN_PHYSICS_N;
        case 'L': return MUGEN_PHYSICS_L;
        case 'U': return MUGEN_STATETYPE_U;
    }
    return 0;
}

static u8 parse_movetype_char(str8 s)
{
    s = trim(s);
    if (s.len == 0) return MUGEN_MOVETYPE_I;
    u8 c = s.data[0];
    if (c >= 'a' && c <= 'z') c -= 32;
    switch (c)
    {
        case 'I': return MUGEN_MOVETYPE_I;
        case 'A': return MUGEN_MOVETYPE_A;
        case 'H': return MUGEN_MOVETYPE_H;
        case 'U': return MUGEN_MOVETYPE_U;
    }
    return MUGEN_MOVETYPE_I;
}

static u8 parse_animtype(str8 s)
{
    s = trim(s);
    if (str8_ieq(s, "light"))  return MUGEN_ANIMTYPE_LIGHT;
    if (str8_ieq(s, "medium")) return MUGEN_ANIMTYPE_MEDIUM;
    if (str8_ieq(s, "hard"))   return MUGEN_ANIMTYPE_HARD;
    if (str8_ieq(s, "back"))   return MUGEN_ANIMTYPE_BACK;
    if (str8_ieq(s, "up"))     return MUGEN_ANIMTYPE_UP;
    if (str8_ieq(s, "diagup")) return MUGEN_ANIMTYPE_DIAGUP;
    return MUGEN_ANIMTYPE_LIGHT;
}

static u8 parse_groundtype(str8 s)
{
    s = trim(s);
    if (str8_ieq(s, "high")) return MUGEN_GROUNDTYPE_HIGH;
    if (str8_ieq(s, "low"))  return MUGEN_GROUNDTYPE_LOW;
    if (str8_ieq(s, "trip")) return MUGEN_GROUNDTYPE_TRIP;
    return MUGEN_GROUNDTYPE_HIGH;
}

static u32 parse_attr(str8 s)
{
    u32 flags = 0;
    str8 rest;
    str8 state_part = split_comma_first(s, &rest);

    for (size i = 0; i < state_part.len; i++)
    {
        u8 c = state_part.data[i]; if (c >= 'a' && c <= 'z') c -= 32;
        if (c == 'S') flags |= MUGEN_ATTR_S;
        if (c == 'C') flags |= MUGEN_ATTR_C;
        if (c == 'A') flags |= MUGEN_ATTR_A;
    }

    str8 atk = trim(rest);
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

static u32 parse_hitflag(str8 s)
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

typedef struct {
    void** items;
    u32 count;
    u32 capacity;
} Ptr_List;

static void ptr_push(Ptr_List* list, void* item, const Mel_Alloc* alloc)
{
    if (list->count >= list->capacity)
    {
        u32 new_cap = list->capacity == 0 ? 16 : list->capacity * 2;
        void** new_items = mel_alloc(alloc, new_cap * sizeof(void*));
        if (list->items)
        {
            memcpy(new_items, list->items, list->count * sizeof(void*));
            mel_dealloc(alloc, list->items);
        }
        list->items = new_items;
        list->capacity = new_cap;
    }
    list->items[list->count++] = item;
}

typedef struct {
    Mugen_Statedef* items;
    u32 count;
    u32 capacity;
} Statedef_List;

static void statedef_push(Statedef_List* list, Mugen_Statedef def, const Mel_Alloc* alloc)
{
    if (list->count >= list->capacity)
    {
        u32 new_cap = list->capacity == 0 ? 32 : list->capacity * 2;
        Mugen_Statedef* new_items = mel_alloc(alloc, new_cap * sizeof(Mugen_Statedef));
        if (list->items)
        {
            memcpy(new_items, list->items, list->count * sizeof(Mugen_Statedef));
            mel_dealloc(alloc, list->items);
        }
        list->items = new_items;
        list->capacity = new_cap;
    }
    list->items[list->count++] = def;
}

typedef struct {
    Mugen_State_Controller* items;
    u32 count;
    u32 capacity;
} Controller_List;

static void ctrl_push(Controller_List* list, Mugen_State_Controller ctrl, const Mel_Alloc* alloc)
{
    if (list->count >= list->capacity)
    {
        u32 new_cap = list->capacity == 0 ? 16 : list->capacity * 2;
        Mugen_State_Controller* new_items = mel_alloc(alloc, new_cap * sizeof(Mugen_State_Controller));
        if (list->items)
        {
            memcpy(new_items, list->items, list->count * sizeof(Mugen_State_Controller));
            mel_dealloc(alloc, list->items);
        }
        list->items = new_items;
        list->capacity = new_cap;
    }
    list->items[list->count++] = ctrl;
}

static u8 parse_controller_type(str8 s)
{
    s = trim(s);
    if (str8_ieq(s, "changestate")) return MUGEN_SC_CHANGESTATE;
    if (str8_ieq(s, "changeanim"))  return MUGEN_SC_CHANGEANIM;
    if (str8_ieq(s, "velset"))      return MUGEN_SC_VELSET;
    if (str8_ieq(s, "veladd"))      return MUGEN_SC_VELADD;
    if (str8_ieq(s, "velmul"))      return MUGEN_SC_VELMUL;
    if (str8_ieq(s, "posset"))      return MUGEN_SC_POSSET;
    if (str8_ieq(s, "posadd"))      return MUGEN_SC_POSADD;
    if (str8_ieq(s, "hitdef"))      return MUGEN_SC_HITDEF;
    if (str8_ieq(s, "playsnd"))     return MUGEN_SC_PLAYSND;
    if (str8_ieq(s, "gravity"))     return MUGEN_SC_GRAVITY;
    if (str8_ieq(s, "ctrlset"))     return MUGEN_SC_CTRL;
    if (str8_ieq(s, "varset"))      return MUGEN_SC_VARSET;
    if (str8_ieq(s, "varadd"))      return MUGEN_SC_VARADD;
    if (str8_ieq(s, "nothitby"))    return MUGEN_SC_NOTHITBY;
    if (str8_ieq(s, "width"))       return MUGEN_SC_WIDTH;
    if (str8_ieq(s, "turn"))        return MUGEN_SC_TURN;
    if (str8_ieq(s, "superpause"))  return MUGEN_SC_SUPERPAUSE;
    if (str8_ieq(s, "afterimage"))  return MUGEN_SC_AFTERIMAGE;
    if (str8_ieq(s, "afterimagetime")) return MUGEN_SC_AFTERIMAGETIME;
    if (str8_ieq(s, "poweradd"))    return MUGEN_SC_POWERADD;
    if (str8_ieq(s, "sprpriority")) return MUGEN_SC_SPRPRIORITY;
    if (str8_ieq(s, "varrandom"))   return MUGEN_SC_VARRANDOM;
    if (str8_ieq(s, "statetypeset")) return MUGEN_SC_STATETYPESET;
    if (str8_ieq(s, "assertspecial")) return MUGEN_SC_ASSERTSPECIAL;
    if (str8_ieq(s, "forcefeedback")) return MUGEN_SC_NULL;
    if (str8_ieq(s, "hitvelset"))   return MUGEN_SC_HITVELSET;
    if (str8_ieq(s, "defencemulset")) return MUGEN_SC_DEFENCEMULSET;
    if (str8_ieq(s, "makedust"))    return MUGEN_SC_NULL;
    if (str8_ieq(s, "explod"))      return MUGEN_SC_NULL;
    if (str8_ieq(s, "posfreeze"))   return MUGEN_SC_POSFREEZE;
    if (str8_ieq(s, "palfx"))       return MUGEN_SC_NULL;
    if (str8_ieq(s, "selfstate"))   return MUGEN_SC_SELFSTATE;
    if (str8_ieq(s, "hitfalldamage")) return MUGEN_SC_HITFALLDAMAGE;
    if (str8_ieq(s, "hitfallvel"))  return MUGEN_SC_HITFALLVEL;
    if (str8_ieq(s, "hitfallset"))  return MUGEN_SC_HITFALLSET;
    if (str8_ieq(s, "fallenvshake")) return MUGEN_SC_FALLENVSHAKE;
    if (str8_ieq(s, "varrangeset")) return MUGEN_SC_VARRANGESET;
    if (str8_ieq(s, "remappal"))    return MUGEN_SC_NULL;
    if (str8_ieq(s, "changeanim2")) return MUGEN_SC_CHANGEANIM2;
    if (str8_ieq(s, "targetbind"))  return MUGEN_SC_TARGETBIND;
    if (str8_ieq(s, "targetfacing")) return MUGEN_SC_TARGETFACING;
    if (str8_ieq(s, "targetlifeadd")) return MUGEN_SC_TARGETLIFEADD;
    if (str8_ieq(s, "targetpoweradd")) return MUGEN_SC_TARGETPOWERADD;
    if (str8_ieq(s, "targetstate")) return MUGEN_SC_TARGETSTATE;
    if (str8_ieq(s, "screenbound")) return MUGEN_SC_NULL;
    if (str8_ieq(s, "envshake"))    return MUGEN_SC_NULL;
    if (str8_ieq(s, "hitoverride")) return MUGEN_SC_NULL;
    if (str8_ieq(s, "reversaldef")) return MUGEN_SC_NULL;
    if (str8_ieq(s, "pause"))       return MUGEN_SC_NULL;
    if (str8_ieq(s, "helper"))      return MUGEN_SC_HELPER;
    if (str8_ieq(s, "projectile"))  return MUGEN_SC_NULL;
    if (str8_ieq(s, "destroyself")) return MUGEN_SC_DESTROYSELF;
    if (str8_ieq(s, "lifeset"))     return MUGEN_SC_LIFESET;
    if (str8_ieq(s, "gamemakeanim")) return MUGEN_SC_NULL;
    if (str8_ieq(s, "displaytoclipboard")) return MUGEN_SC_NULL;
    if (str8_ieq(s, "appendtoclipboard")) return MUGEN_SC_NULL;
    if (str8_ieq(s, "null"))        return MUGEN_SC_NULL;
    return MUGEN_SC_NULL;
}

static i32 parse_trigger_level(str8 key)
{
    if (str8_ieq(key, "triggerall")) return 0;
    if (starts_with_i(key, "trigger"))
    {
        str8 num_part = str8_from_parts(key.data + 7, key.len - 7);
        return parse_int(num_part);
    }
    return -1;
}

static void* alloc_params(const Mel_Alloc* alloc, usize size)
{
    void* p = mel_alloc(alloc, size);
    memset(p, 0, size);
    return p;
}

static void parse_controller_param(Mugen_State_Controller* sc, str8 key, str8 val, const Mel_Alloc* alloc)
{
    switch (sc->type)
    {
        case MUGEN_SC_VELSET:
        case MUGEN_SC_VELADD:
        case MUGEN_SC_VELMUL:
        {
            if (!sc->params) sc->params = alloc_params(alloc, sizeof(Mugen_Vel_Params));
            Mugen_Vel_Params* p = sc->params;
            if (str8_ieq(key, "x")) p->x = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "y")) p->y = mugen_expr_parse(val, alloc);
            break;
        }
        case MUGEN_SC_POSSET:
        case MUGEN_SC_POSADD:
        {
            if (!sc->params) sc->params = alloc_params(alloc, sizeof(Mugen_Pos_Params));
            Mugen_Pos_Params* p = sc->params;
            if (str8_ieq(key, "x")) p->x = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "y")) p->y = mugen_expr_parse(val, alloc);
            break;
        }
        case MUGEN_SC_CHANGESTATE:
        {
            if (!sc->params) sc->params = alloc_params(alloc, sizeof(Mugen_ChangeState_Params));
            Mugen_ChangeState_Params* p = sc->params;
            if (str8_ieq(key, "value")) p->value = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "ctrl")) p->ctrl = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "anim")) p->anim = mugen_expr_parse(val, alloc);
            break;
        }
        case MUGEN_SC_CHANGEANIM:
        {
            if (!sc->params) sc->params = alloc_params(alloc, sizeof(Mugen_ChangeAnim_Params));
            Mugen_ChangeAnim_Params* p = sc->params;
            if (str8_ieq(key, "value")) p->value = mugen_expr_parse(val, alloc);
            break;
        }
        case MUGEN_SC_CTRL:
        {
            if (!sc->params) sc->params = alloc_params(alloc, sizeof(Mugen_Ctrl_Params));
            Mugen_Ctrl_Params* p = sc->params;
            if (str8_ieq(key, "value")) p->value = mugen_expr_parse(val, alloc);
            break;
        }
        case MUGEN_SC_PLAYSND:
        {
            if (!sc->params) sc->params = alloc_params(alloc, sizeof(Mugen_PlaySnd_Params));
            Mugen_PlaySnd_Params* p = sc->params;
            if (str8_ieq(key, "value"))
            {
                str8 rest;
                str8 grp = split_comma_first(val, &rest);
                p->group = mugen_expr_parse(grp, alloc);
                if (rest.len > 0) p->index = mugen_expr_parse(rest, alloc);
            }
            else if (str8_ieq(key, "channel")) p->channel = mugen_expr_parse(val, alloc);
            break;
        }
        case MUGEN_SC_VARSET:
        case MUGEN_SC_VARADD:
        {
            if (!sc->params) sc->params = alloc_params(alloc, sizeof(Mugen_VarSet_Params));
            Mugen_VarSet_Params* p = sc->params;
            if (str8_ieq(key, "value")) p->value = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "v")) { p->var_type = MUGEN_VAR_INT; p->index = mugen_expr_parse(val, alloc); }
            else if (str8_ieq(key, "fv")) { p->var_type = MUGEN_VAR_FLOAT; p->index = mugen_expr_parse(val, alloc); }
            else if (starts_with_i(key, "var("))
            {
                p->var_type = MUGEN_VAR_INT;
                str8 inner = str8_from_parts(key.data + 4, key.len - 5);
                p->index = mugen_expr_parse(inner, alloc);
                p->value = mugen_expr_parse(val, alloc);
            }
            else if (starts_with_i(key, "fvar("))
            {
                p->var_type = MUGEN_VAR_FLOAT;
                str8 inner = str8_from_parts(key.data + 5, key.len - 6);
                p->index = mugen_expr_parse(inner, alloc);
                p->value = mugen_expr_parse(val, alloc);
            }
            else if (starts_with_i(key, "sysvar("))
            {
                p->var_type = MUGEN_VAR_SYSINT;
                str8 inner = str8_from_parts(key.data + 7, key.len - 8);
                p->index = mugen_expr_parse(inner, alloc);
                p->value = mugen_expr_parse(val, alloc);
            }
            else if (starts_with_i(key, "sysfvar("))
            {
                p->var_type = MUGEN_VAR_SYSFLOAT;
                str8 inner = str8_from_parts(key.data + 8, key.len - 9);
                p->index = mugen_expr_parse(inner, alloc);
                p->value = mugen_expr_parse(val, alloc);
            }
            break;
        }
        case MUGEN_SC_VARRANDOM:
        {
            if (!sc->params) sc->params = alloc_params(alloc, sizeof(Mugen_VarRandom_Params));
            Mugen_VarRandom_Params* p = sc->params;
            if (str8_ieq(key, "v")) { p->var_type = MUGEN_VAR_INT; p->index = mugen_expr_parse(val, alloc); }
            else if (str8_ieq(key, "range"))
            {
                str8 rest;
                str8 lo = split_comma_first(val, &rest);
                p->range_low = mugen_expr_parse(lo, alloc);
                if (rest.len > 0) p->range_high = mugen_expr_parse(rest, alloc);
            }
            break;
        }
        case MUGEN_SC_NOTHITBY:
        {
            if (!sc->params) sc->params = alloc_params(alloc, sizeof(Mugen_NotHitBy_Params));
            Mugen_NotHitBy_Params* p = sc->params;
            if (str8_ieq(key, "value")) p->attr_flags = parse_attr(val);
            else if (str8_ieq(key, "time")) p->time = mugen_expr_parse(val, alloc);
            break;
        }
        case MUGEN_SC_WIDTH:
        {
            if (!sc->params) sc->params = alloc_params(alloc, sizeof(Mugen_Width_Params));
            Mugen_Width_Params* p = sc->params;
            if (str8_ieq(key, "value"))
            {
                str8 rest;
                str8 front = split_comma_first(val, &rest);
                p->front = mugen_expr_parse(front, alloc);
                if (rest.len > 0) p->back = mugen_expr_parse(rest, alloc);
            }
            break;
        }
        case MUGEN_SC_SUPERPAUSE:
        {
            if (!sc->params) sc->params = alloc_params(alloc, sizeof(Mugen_SuperPause_Params));
            Mugen_SuperPause_Params* p = sc->params;
            if (str8_ieq(key, "time")) p->time = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "anim")) p->anim = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "sound"))
            {
                str8 rest;
                str8 grp = split_comma_first(val, &rest);
                p->sound_group = mugen_expr_parse(grp, alloc);
                if (rest.len > 0) p->sound_index = mugen_expr_parse(rest, alloc);
            }
            else if (str8_ieq(key, "pos"))
            {
                str8 rest;
                str8 px = split_comma_first(val, &rest);
                p->pos_x = mugen_expr_parse(px, alloc);
                if (rest.len > 0) p->pos_y = mugen_expr_parse(rest, alloc);
            }
            else if (str8_ieq(key, "poweradd")) p->poweradd = mugen_expr_parse(val, alloc);
            break;
        }
        case MUGEN_SC_AFTERIMAGE:
        case MUGEN_SC_AFTERIMAGETIME:
        {
            if (!sc->params) sc->params = alloc_params(alloc, sizeof(Mugen_AfterImage_Params));
            Mugen_AfterImage_Params* p = sc->params;
            if (str8_ieq(key, "time")) p->time = mugen_expr_parse(val, alloc);
            break;
        }
        case MUGEN_SC_POWERADD:
        {
            if (!sc->params) sc->params = alloc_params(alloc, sizeof(Mugen_PowerAdd_Params));
            Mugen_PowerAdd_Params* p = sc->params;
            if (str8_ieq(key, "value")) p->value = mugen_expr_parse(val, alloc);
            break;
        }
        case MUGEN_SC_SPRPRIORITY:
        {
            if (!sc->params) sc->params = alloc_params(alloc, sizeof(Mugen_SprPriority_Params));
            Mugen_SprPriority_Params* p = sc->params;
            if (str8_ieq(key, "value")) p->value = mugen_expr_parse(val, alloc);
            break;
        }
        case MUGEN_SC_STATETYPESET:
        {
            if (!sc->params) sc->params = alloc_params(alloc, sizeof(Mugen_StateTypeSet_Params));
            Mugen_StateTypeSet_Params* p = sc->params;
            if (!p->statetype && !p->movetype && !p->physics)
            {
                p->statetype = -1;
                p->movetype = -1;
                p->physics = -1;
            }
            if (str8_ieq(key, "statetype"))
            {
                str8 v = trim(val);
                if (v.len > 0)
                {
                    u8 c = v.data[0];
                    if (c >= 'a' && c <= 'z') c -= 32;
                    if (c == 'S') p->statetype = MUGEN_PHYSICS_S;
                    else if (c == 'C') p->statetype = MUGEN_PHYSICS_C;
                    else if (c == 'A') p->statetype = MUGEN_PHYSICS_A;
                }
            }
            else if (str8_ieq(key, "movetype"))
            {
                str8 v = trim(val);
                if (v.len > 0)
                {
                    u8 c = v.data[0];
                    if (c >= 'a' && c <= 'z') c -= 32;
                    if (c == 'I') p->movetype = MUGEN_MOVETYPE_I;
                    else if (c == 'A') p->movetype = MUGEN_MOVETYPE_A;
                    else if (c == 'H') p->movetype = MUGEN_MOVETYPE_H;
                }
            }
            else if (str8_ieq(key, "physics"))
            {
                str8 v = trim(val);
                if (v.len > 0)
                {
                    u8 c = v.data[0];
                    if (c >= 'a' && c <= 'z') c -= 32;
                    if (c == 'S') p->physics = MUGEN_PHYSICS_S;
                    else if (c == 'C') p->physics = MUGEN_PHYSICS_C;
                    else if (c == 'A') p->physics = MUGEN_PHYSICS_A;
                    else if (c == 'N') p->physics = MUGEN_PHYSICS_N;
                }
            }
            break;
        }
        case MUGEN_SC_SELFSTATE:
        {
            if (!sc->params) sc->params = alloc_params(alloc, sizeof(Mugen_SelfState_Params));
            Mugen_SelfState_Params* p = sc->params;
            if (str8_ieq(key, "value")) p->value = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "ctrl")) p->ctrl = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "anim")) p->anim = mugen_expr_parse(val, alloc);
            break;
        }
        case MUGEN_SC_HITVELSET:
        {
            if (!sc->params) sc->params = alloc_params(alloc, sizeof(Mugen_HitVelSet_Params));
            Mugen_HitVelSet_Params* p = sc->params;
            if (str8_ieq(key, "x")) p->x = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "y")) p->y = mugen_expr_parse(val, alloc);
            break;
        }
        case MUGEN_SC_HITFALLSET:
        {
            if (!sc->params) sc->params = alloc_params(alloc, sizeof(Mugen_HitFallSet_Params));
            Mugen_HitFallSet_Params* p = sc->params;
            if (str8_ieq(key, "fall.xvel")) { p->fall_xvel = mugen_expr_parse(val, alloc); p->xvel_set = 1; }
            else if (str8_ieq(key, "fall.yvel")) { p->fall_yvel = mugen_expr_parse(val, alloc); p->yvel_set = 1; }
            else if (str8_ieq(key, "fall.recover")) p->fall_recover = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "fall.recovertime")) p->fall_recovertime = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "fall.damage")) p->fall_damage = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "fall.envshake.time")) p->fall_envshake_time = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "xvel")) { p->fall_xvel = mugen_expr_parse(val, alloc); p->xvel_set = 1; }
            else if (str8_ieq(key, "yvel")) { p->fall_yvel = mugen_expr_parse(val, alloc); p->yvel_set = 1; }
            break;
        }
        case MUGEN_SC_LIFESET:
        {
            if (!sc->params) sc->params = alloc_params(alloc, sizeof(Mugen_PowerAdd_Params));
            Mugen_PowerAdd_Params* p = sc->params;
            if (str8_ieq(key, "value")) p->value = mugen_expr_parse(val, alloc);
            break;
        }
        case MUGEN_SC_HITFALLDAMAGE:
        case MUGEN_SC_HITFALLVEL:
        case MUGEN_SC_POSFREEZE:
        case MUGEN_SC_FALLENVSHAKE:
            break;
        case MUGEN_SC_VARRANGESET:
        {
            if (!sc->params) sc->params = alloc_params(alloc, sizeof(Mugen_VarRangeSet_Params));
            Mugen_VarRangeSet_Params* p = sc->params;
            if (str8_ieq(key, "value"))  p->value = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "fvalue")) p->fvalue = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "first"))  p->first = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "last"))   p->last = mugen_expr_parse(val, alloc);
            break;
        }
        case MUGEN_SC_ASSERTSPECIAL:
        {
            if (!sc->params) sc->params = alloc_params(alloc, sizeof(Mugen_AssertSpecial_Params));
            Mugen_AssertSpecial_Params* p = sc->params;
            if (str8_ieq(key, "flag") || str8_ieq(key, "flag2") || str8_ieq(key, "flag3"))
            {
                if (str8_ieq(val, "nowalk"))         p->flags |= MUGEN_ASSERT_NOWALK;
                else if (str8_ieq(val, "noautoturn")) p->flags |= MUGEN_ASSERT_NOAUTOTURN;
                else if (str8_ieq(val, "nostandguard")) p->flags |= MUGEN_ASSERT_NOSTANDGUARD;
                else if (str8_ieq(val, "nocrouchguard")) p->flags |= MUGEN_ASSERT_NOCROUCHGUARD;
                else if (str8_ieq(val, "noairguard")) p->flags |= MUGEN_ASSERT_NOAIRGUARD;
                else if (str8_ieq(val, "unguardable")) p->flags |= MUGEN_ASSERT_UNGUARDABLE;
                else if (str8_ieq(val, "nojugglecheck")) p->flags |= MUGEN_ASSERT_NOJUGGLECHECK;
                else if (str8_ieq(val, "intro"))         p->flags |= MUGEN_ASSERT_INTRO;
                else if (str8_ieq(val, "nocornerpush"))  p->flags |= MUGEN_ASSERT_NOCORNERPUSH;
            }
            break;
        }
        case MUGEN_SC_DEFENCEMULSET:
        {
            if (!sc->params) sc->params = alloc_params(alloc, sizeof(Mugen_DefenceMulSet_Params));
            Mugen_DefenceMulSet_Params* p = sc->params;
            if (str8_ieq(key, "value")) p->value = mugen_expr_parse(val, alloc);
            break;
        }
        case MUGEN_SC_TARGETBIND:
        {
            if (!sc->params) sc->params = alloc_params(alloc, sizeof(Mugen_TargetBind_Params));
            Mugen_TargetBind_Params* p = sc->params;
            if (str8_ieq(key, "pos"))
            {
                str8 rest;
                str8 px = split_comma_first(val, &rest);
                p->pos_x = mugen_expr_parse(px, alloc);
                if (rest.len > 0) p->pos_y = mugen_expr_parse(rest, alloc);
            }
            break;
        }
        case MUGEN_SC_TARGETSTATE:
        {
            if (!sc->params) sc->params = alloc_params(alloc, sizeof(Mugen_TargetState_Params));
            Mugen_TargetState_Params* p = sc->params;
            if (str8_ieq(key, "value")) p->value = mugen_expr_parse(val, alloc);
            break;
        }
        case MUGEN_SC_TARGETLIFEADD:
        {
            if (!sc->params) sc->params = alloc_params(alloc, sizeof(Mugen_TargetLifeAdd_Params));
            Mugen_TargetLifeAdd_Params* p = sc->params;
            if (str8_ieq(key, "value")) p->value = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "kill")) p->kill = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "absolute")) p->absolute = mugen_expr_parse(val, alloc);
            break;
        }
        case MUGEN_SC_TARGETFACING:
        {
            if (!sc->params) sc->params = alloc_params(alloc, sizeof(Mugen_TargetFacing_Params));
            Mugen_TargetFacing_Params* p = sc->params;
            if (str8_ieq(key, "value")) p->value = mugen_expr_parse(val, alloc);
            break;
        }
        case MUGEN_SC_TARGETPOWERADD:
        {
            if (!sc->params) sc->params = alloc_params(alloc, sizeof(Mugen_TargetPowerAdd_Params));
            Mugen_TargetPowerAdd_Params* p = sc->params;
            if (str8_ieq(key, "value")) p->value = mugen_expr_parse(val, alloc);
            break;
        }
        case MUGEN_SC_CHANGEANIM2:
        {
            if (!sc->params) sc->params = alloc_params(alloc, sizeof(Mugen_ChangeAnim2_Params));
            Mugen_ChangeAnim2_Params* p = sc->params;
            if (str8_ieq(key, "value")) p->value = mugen_expr_parse(val, alloc);
            break;
        }
        case MUGEN_SC_HELPER:
        {
            if (!sc->params) sc->params = alloc_params(alloc, sizeof(Mugen_Helper_Params));
            Mugen_Helper_Params* p = sc->params;
            if (str8_ieq(key, "helperid") || str8_ieq(key, "id")) p->id = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "stateno")) p->stateno = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "pos"))
            {
                str8 rest;
                str8 px = split_comma_first(val, &rest);
                p->pos_x = mugen_expr_parse(px, alloc);
                if (rest.len > 0) p->pos_y = mugen_expr_parse(rest, alloc);
            }
            else if (str8_ieq(key, "postype"))
            {
                str8 v = trim(val);
                if (str8_ieq(v, "p1"))         p->postype = MUGEN_POSTYPE_P1;
                else if (str8_ieq(v, "left"))   p->postype = MUGEN_POSTYPE_LEFT;
                else if (str8_ieq(v, "right"))  p->postype = MUGEN_POSTYPE_RIGHT;
                else if (str8_ieq(v, "back"))   p->postype = MUGEN_POSTYPE_BACK;
                else if (str8_ieq(v, "front"))  p->postype = MUGEN_POSTYPE_FRONT;
            }
            else if (str8_ieq(key, "facing")) p->facing = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "ownpal")) p->ownpal = mugen_expr_parse(val, alloc);
            break;
        }
        case MUGEN_SC_DESTROYSELF:
            break;
        case MUGEN_SC_HITDEF:
        {
            if (!sc->params) sc->params = alloc_params(alloc, sizeof(Mugen_HitDef_Params));
            Mugen_HitDef_Params* p = sc->params;
            if (str8_ieq(key, "attr")) p->attr = parse_attr(val);
            else if (str8_ieq(key, "hitflag")) p->hitflag = parse_hitflag(val);
            else if (str8_ieq(key, "guardflag")) p->guardflag = parse_hitflag(val);
            else if (str8_ieq(key, "animtype")) p->animtype = parse_animtype(val);
            else if (str8_ieq(key, "air.animtype")) p->air_animtype = parse_animtype(val);
            else if (str8_ieq(key, "fall.animtype")) p->fall_animtype = parse_animtype(val);
            else if (str8_ieq(key, "ground.type")) p->ground_type = parse_groundtype(val);
            else if (str8_ieq(key, "air.type")) p->air_type = parse_groundtype(val);
            else if (str8_ieq(key, "damage"))
            {
                str8 rest;
                str8 hit = split_comma_first(val, &rest);
                p->damage_hit = mugen_expr_parse(hit, alloc);
                if (rest.len > 0) p->damage_guard = mugen_expr_parse(rest, alloc);
            }
            else if (str8_ieq(key, "pausetime"))
            {
                str8 rest;
                str8 p1 = split_comma_first(val, &rest);
                p->pausetime_p1 = mugen_expr_parse(p1, alloc);
                if (rest.len > 0) p->pausetime_p2 = mugen_expr_parse(rest, alloc);
            }
            else if (str8_ieq(key, "guard.pausetime"))
            {
                str8 rest;
                str8 gp1 = split_comma_first(val, &rest);
                p->guard_pausetime_p1 = mugen_expr_parse(gp1, alloc);
                if (rest.len > 0) p->guard_pausetime_p2 = mugen_expr_parse(rest, alloc);
            }
            else if (str8_ieq(key, "sparkno")) p->sparkno = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "guard.sparkno")) p->guard_sparkno = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "sparkxy"))
            {
                str8 rest;
                str8 sx = split_comma_first(val, &rest);
                p->spark_x = mugen_expr_parse(sx, alloc);
                if (rest.len > 0) p->spark_y = mugen_expr_parse(rest, alloc);
            }
            else if (str8_ieq(key, "hitsound"))
            {
                str8 rest;
                str8 grp = split_comma_first(val, &rest);
                p->hitsound_group = mugen_expr_parse(grp, alloc);
                if (rest.len > 0) p->hitsound_index = mugen_expr_parse(rest, alloc);
            }
            else if (str8_ieq(key, "guardsound"))
            {
                str8 rest;
                str8 grp = split_comma_first(val, &rest);
                p->guardsound_group = mugen_expr_parse(grp, alloc);
                if (rest.len > 0) p->guardsound_index = mugen_expr_parse(rest, alloc);
            }
            else if (str8_ieq(key, "ground.slidetime")) p->ground_slidetime = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "ground.hittime")) p->ground_hittime = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "ground.velocity"))
            {
                str8 rest;
                str8 vx = split_comma_first(val, &rest);
                p->ground_vel_x = mugen_expr_parse(vx, alloc);
                if (rest.len > 0) p->ground_vel_y = mugen_expr_parse(rest, alloc);
            }
            else if (str8_ieq(key, "guard.slidetime")) p->guard_slidetime = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "guard.hittime")) p->guard_hittime = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "guard.ctrltime")) p->guard_ctrltime = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "guard.velocity")) p->guard_velocity = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "air.velocity"))
            {
                str8 rest;
                str8 vx = split_comma_first(val, &rest);
                p->air_vel_x = mugen_expr_parse(vx, alloc);
                if (rest.len > 0) p->air_vel_y = mugen_expr_parse(rest, alloc);
            }
            else if (str8_ieq(key, "airguard.velocity"))
            {
                str8 rest;
                str8 vx = split_comma_first(val, &rest);
                p->airguard_vel_x = mugen_expr_parse(vx, alloc);
                if (rest.len > 0) p->airguard_vel_y = mugen_expr_parse(rest, alloc);
            }
            else if (str8_ieq(key, "air.hittime")) p->air_hittime = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "air.fall")) p->air_fall = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "fall")) p->fall = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "fall.recover")) p->fall_recover = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "fall.recovertime")) p->fall_recovertime = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "fall.damage")) p->fall_damage = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "fall.xvelocity")) p->fall_vel_x = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "fall.yvelocity")) p->fall_vel_y = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "priority")) p->priority = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "getpower"))
            {
                str8 rest;
                str8 hit = split_comma_first(val, &rest);
                p->getpower_hit = mugen_expr_parse(hit, alloc);
                if (rest.len > 0) p->getpower_guard = mugen_expr_parse(rest, alloc);
            }
            else if (str8_ieq(key, "givepower"))
            {
                str8 rest;
                str8 hit = split_comma_first(val, &rest);
                p->givepower_hit = mugen_expr_parse(hit, alloc);
                if (rest.len > 0) p->givepower_guard = mugen_expr_parse(rest, alloc);
            }
            else if (str8_ieq(key, "p1stateno")) p->p1stateno = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "p2stateno")) p->p2stateno = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "p1facing")) p->p1facing = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "p2facing")) p->p2facing = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "p2getp1state")) p->p2getp1state = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "numhits")) p->numhits = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "hitonce")) p->hitonce = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "forcestand")) p->forcestand = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "ground.cornerpush.veloff")) p->ground_cornerpush = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "air.cornerpush.veloff")) p->air_cornerpush = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "guard.cornerpush.veloff")) p->guard_cornerpush = mugen_expr_parse(val, alloc);
            else if (str8_ieq(key, "yaccel")) p->yaccel = mugen_expr_parse(val, alloc);
            break;
        }
    }
}

static void finalize_controller(Mugen_State_Controller* sc, Ptr_List* triggerall_list,
                                 Ptr_List* trigger_groups, u32* group_sizes, u32 max_group,
                                 const Mel_Alloc* alloc)
{
    if (triggerall_list->count > 0)
    {
        sc->triggerall = mel_alloc(alloc, triggerall_list->count * sizeof(Mugen_Expr*));
        memcpy(sc->triggerall, triggerall_list->items, triggerall_list->count * sizeof(Mugen_Expr*));
        sc->triggerall_count = triggerall_list->count;
    }

    u32 group_count = 0;
    for (u32 i = 1; i <= max_group; i++)
        if (group_sizes[i] > 0) group_count = i;

    if (group_count > 0)
    {
        sc->trigger_groups = mel_alloc(alloc, group_count * sizeof(Mugen_Trigger_Group));
        sc->trigger_group_count = group_count;

        u32 offset = 0;
        for (u32 i = 0; i < group_count; i++)
        {
            u32 count = group_sizes[i + 1];
            sc->trigger_groups[i].count = count;
            if (count > 0)
            {
                sc->trigger_groups[i].conditions = mel_alloc(alloc, count * sizeof(Mugen_Expr*));
                memcpy(sc->trigger_groups[i].conditions, (Mugen_Expr**)trigger_groups->items + offset,
                       count * sizeof(Mugen_Expr*));
            }
            offset += count;
        }
    }
}

static void parse_constants_line(Mugen_Char_Constants* c, u8 section, str8 key, str8 val)
{
    switch (section)
    {
        case 1:
        {
            if (str8_ieq(key, "life"))               c->life = parse_int(val);
            else if (str8_ieq(key, "attack"))         c->attack = parse_int(val);
            else if (str8_ieq(key, "defence"))        c->defence = parse_int(val);
            else if (str8_ieq(key, "fall.defence_up")) c->fall_defence_up = parse_int(val);
            else if (str8_ieq(key, "liedown.time"))   c->liedown_time = parse_int(val);
            else if (str8_ieq(key, "airjuggle"))      c->airjuggle = parse_int(val);
            else if (str8_ieq(key, "sparkno"))        c->sparkno = parse_int(val);
            else if (str8_ieq(key, "guard.sparkno"))  c->guard_sparkno = parse_int(val);
            else if (str8_ieq(key, "power.max"))      c->power_max = parse_int(val);
            break;
        }
        case 2:
        {
            if (str8_ieq(key, "xscale"))              c->xscale = parse_float(val);
            else if (str8_ieq(key, "yscale"))          c->yscale = parse_float(val);
            else if (str8_ieq(key, "ground.back"))     c->ground_back = parse_float(val);
            else if (str8_ieq(key, "ground.front"))    c->ground_front = parse_float(val);
            else if (str8_ieq(key, "air.back"))        c->air_back = parse_float(val);
            else if (str8_ieq(key, "air.front"))       c->air_front = parse_float(val);
            else if (str8_ieq(key, "height"))          c->height = parse_float(val);
            else if (str8_ieq(key, "attack.dist"))     c->attack_dist = parse_float(val);
            break;
        }
        case 3:
        {
            str8 rest;
            str8 first = split_comma_first(val, &rest);
            if (str8_ieq(key, "walk.fwd"))             c->walk_fwd_x = parse_float(first);
            else if (str8_ieq(key, "walk.back"))       c->walk_back_x = parse_float(first);
            else if (str8_ieq(key, "run.fwd"))
            {
                c->run_fwd_x = parse_float(first);
                if (rest.len > 0) c->run_fwd_y = -parse_float(rest);
            }
            else if (str8_ieq(key, "run.back"))
            {
                c->run_back_x = parse_float(first);
                if (rest.len > 0) c->run_back_y = -parse_float(rest);
            }
            else if (str8_ieq(key, "jump.neu"))
            {
                c->jump_neu_x = parse_float(first);
                if (rest.len > 0) c->jump_y = -parse_float(rest);
            }
            else if (str8_ieq(key, "jump.back"))       c->jump_back_x = parse_float(first);
            else if (str8_ieq(key, "jump.fwd"))        c->jump_fwd_x = parse_float(first);
            else if (str8_ieq(key, "runjump.back"))
            {
                c->runjump_back_x = parse_float(first);
                if (rest.len > 0) c->runjump_y = -parse_float(rest);
            }
            else if (str8_ieq(key, "runjump.fwd"))
            {
                c->runjump_fwd_x = parse_float(first);
                if (rest.len > 0) c->runjump_y = -parse_float(rest);
            }
            else if (str8_ieq(key, "airjump.neu"))
            {
                c->airjump_neu_x = parse_float(first);
                if (rest.len > 0) c->airjump_y = -parse_float(rest);
            }
            else if (str8_ieq(key, "airjump.back"))    c->airjump_back_x = parse_float(first);
            else if (str8_ieq(key, "airjump.fwd"))     c->airjump_fwd_x = parse_float(first);
            break;
        }
        case 4:
        {
            if (str8_ieq(key, "airjump.num"))                    c->airjump_num = parse_int(val);
            else if (str8_ieq(key, "airjump.height"))             c->airjump_height = parse_int(val);
            else if (str8_ieq(key, "yaccel"))                     c->yaccel = parse_float(val);
            else if (str8_ieq(key, "stand.friction"))             c->stand_friction = parse_float(val);
            else if (str8_ieq(key, "crouch.friction"))            c->crouch_friction = parse_float(val);
            else if (str8_ieq(key, "stand.friction.threshold"))   c->stand_friction_threshold = parse_float(val);
            else if (str8_ieq(key, "crouch.friction.threshold"))  c->crouch_friction_threshold = parse_float(val);
            else if (str8_ieq(key, "down.bounce.offset"))
            {
                str8 brest;
                str8 bfirst = split_comma_first(val, &brest);
                c->down_bounce_offset_x = parse_float(bfirst);
                if (brest.len > 0) c->down_bounce_offset_y = parse_float(brest);
            }
            else if (str8_ieq(key, "down.bounce.yaccel"))         c->down_bounce_yaccel = parse_float(val);
            else if (str8_ieq(key, "down.bounce.groundlevel"))    c->down_bounce_groundlevel = parse_float(val);
            else if (str8_ieq(key, "down.friction.threshold"))    c->down_friction_threshold = parse_float(val);
            break;
        }
    }
}

static Mugen_Char_Constants default_constants(void)
{
    return (Mugen_Char_Constants){
        .life = 1000,
        .attack = 100,
        .defence = 100,
        .fall_defence_up = 50,
        .liedown_time = 60,
        .airjuggle = 15,
        .sparkno = 2,
        .guard_sparkno = 40,
        .power_max = 3000,
        .xscale = 1.0f,
        .yscale = 1.0f,
        .ground_back = 15.0f,
        .ground_front = 16.0f,
        .air_back = 12.0f,
        .air_front = 12.0f,
        .height = 60.0f,
        .attack_dist = 160.0f,
        .walk_fwd_x = 2.4f,
        .walk_back_x = -2.2f,
        .run_fwd_x = 4.6f,
        .run_back_x = -4.5f,
        .run_back_y = 3.8f,
        .jump_y = 8.4f,
        .jump_back_x = -2.55f,
        .jump_fwd_x = 2.5f,
        .runjump_y = 8.1f,
        .runjump_back_x = -2.55f,
        .runjump_fwd_x = 4.0f,
        .airjump_y = 8.1f,
        .airjump_back_x = -2.55f,
        .airjump_fwd_x = 2.5f,
        .airjump_num = 1,
        .airjump_height = 35,
        .yaccel = 0.44f,
        .stand_friction = 0.85f,
        .crouch_friction = 0.82f,
        .stand_friction_threshold = 2.0f,
        .crouch_friction_threshold = 0.05f,
        .down_bounce_offset_x = 0.0f,
        .down_bounce_offset_y = 20.0f,
        .down_bounce_yaccel = 0.4f,
        .down_bounce_groundlevel = 12.0f,
        .down_friction_threshold = 0.05f,
    };
}

bool mugen_cns_load(Mugen_Cns* out, str8 data, const Mel_Alloc* alloc)
{
    assert(out);
    assert(alloc);
    *out = (Mugen_Cns){0};

    Line_Parser lp = { .data = data, .pos = 0 };
    Statedef_List statedefs = {0};

    bool in_statedef = false;
    bool in_controller = false;
    u8 header_section = 0;
    Mugen_Char_Constants constants = default_constants();
    Mugen_Statedef current_def = {0};
    current_def.anim = -1;
    current_def.ctrl = -1;
    Controller_List controllers = {0};

    Mugen_State_Controller current_ctrl = {0};
    Ptr_List triggerall_list = {0};
    Ptr_List trigger_exprs = {0};
    u32 trigger_group_sizes[64] = {0};
    u32 max_trigger_group = 0;

    while (lp.pos < (usize)lp.data.len)
    {
        str8 raw = next_line(&lp);
        str8 line = trim(strip_comment(raw));
        if (line.len == 0) continue;

        if (line.data[0] == '[')
        {
            if (in_controller)
            {
                finalize_controller(&current_ctrl, &triggerall_list, &trigger_exprs,
                                    trigger_group_sizes, max_trigger_group, alloc);
                ctrl_push(&controllers, current_ctrl, alloc);
                in_controller = false;
            }

            header_section = 0;
            if (starts_with_i(line, "[data]"))         { header_section = 1; out->has_constants = true; continue; }
            else if (starts_with_i(line, "[size]"))    { header_section = 2; out->has_constants = true; continue; }
            else if (starts_with_i(line, "[velocity]")) { header_section = 3; out->has_constants = true; continue; }
            else if (starts_with_i(line, "[movement]")) { header_section = 4; out->has_constants = true; continue; }

            if (starts_with_i(line, "[statedef "))
            {
                if (in_statedef)
                {
                    current_def.controllers = controllers.items;
                    current_def.controller_count = controllers.count;
                    statedef_push(&statedefs, current_def, alloc);
                    controllers = (Controller_List){0};
                }

                in_statedef = true;
                current_def = (Mugen_Statedef){0};
                current_def.anim = -1;
                current_def.ctrl = -1;
                current_def.movetype = 0xFF;

                str8 num_part = str8_from_parts(line.data + 10, line.len - 10);
                for (size i = 0; i < num_part.len; i++)
                {
                    if (num_part.data[i] == ']')
                    {
                        num_part = str8_from_parts(num_part.data, i);
                        break;
                    }
                }
                current_def.stateno = parse_int(trim(num_part));
            }
            else if (starts_with_i(line, "[state "))
            {
                in_controller = true;
                current_ctrl = (Mugen_State_Controller){0};
                current_ctrl.persistent = 1;
                triggerall_list = (Ptr_List){0};
                trigger_exprs = (Ptr_List){0};
                memset(trigger_group_sizes, 0, sizeof(trigger_group_sizes));
                max_trigger_group = 0;
            }
            continue;
        }

        if (header_section > 0)
        {
            str8 key = before_eq(line);
            str8 val = after_eq(line);
            parse_constants_line(&constants, header_section, key, val);
            continue;
        }

        if (!in_statedef) continue;

        str8 key = before_eq(line);
        str8 val = after_eq(line);

        if (!in_controller)
        {
            if (str8_ieq(key, "type")) current_def.statetype = parse_statetype_char(val);
            else if (str8_ieq(key, "movetype")) current_def.movetype = parse_movetype_char(val);
            else if (str8_ieq(key, "physics")) current_def.physics = parse_statetype_char(val);
            else if (str8_ieq(key, "anim")) current_def.anim = parse_int(val);
            else if (str8_ieq(key, "velset"))
            {
                str8 rest;
                str8 vx = split_comma_first(val, &rest);
                current_def.velset_x = parse_float(vx);
                if (rest.len > 0) current_def.velset_y = parse_float(rest);
                current_def.has_velset = true;
            }
            else if (str8_ieq(key, "ctrl")) current_def.ctrl = parse_int(val);
            else if (str8_ieq(key, "juggle")) current_def.juggle = parse_int(val);
            else if (str8_ieq(key, "poweradd")) current_def.poweradd = parse_int(val);
            else if (str8_ieq(key, "sprpriority")) current_def.sprpriority = parse_int(val);
            else if (str8_ieq(key, "hitdefpersist")) current_def.hitdefpersist = parse_int(val) != 0;
            else if (str8_ieq(key, "movehitpersist")) current_def.movehitpersist = parse_int(val) != 0;
            else if (str8_ieq(key, "hitcountpersist")) current_def.hitcountpersist = parse_int(val) != 0;
        }
        else
        {
            if (str8_ieq(key, "type"))
            {
                current_ctrl.type = parse_controller_type(val);
            }
            else if (str8_ieq(key, "persistent"))
            {
                current_ctrl.persistent = parse_int(val);
            }
            else if (str8_ieq(key, "ignorehitpause"))
            {
                current_ctrl.ignorehitpause = parse_int(val) != 0;
            }
            else
            {
                i32 trig_level = parse_trigger_level(key);
                if (trig_level == 0)
                {
                    Mugen_Expr* expr = mugen_expr_parse(val, alloc);
                    ptr_push(&triggerall_list, expr, alloc);
                }
                else if (trig_level > 0)
                {
                    Mugen_Expr* expr = mugen_expr_parse(val, alloc);
                    ptr_push(&trigger_exprs, expr, alloc);
                    if ((u32)trig_level < 64)
                    {
                        trigger_group_sizes[trig_level]++;
                        if ((u32)trig_level > max_trigger_group) max_trigger_group = (u32)trig_level;
                    }
                }
                else
                {
                    parse_controller_param(&current_ctrl, key, val, alloc);
                }
            }
        }
    }

    if (in_controller)
    {
        finalize_controller(&current_ctrl, &triggerall_list, &trigger_exprs,
                            trigger_group_sizes, max_trigger_group, alloc);
        ctrl_push(&controllers, current_ctrl, alloc);
    }

    if (in_statedef)
    {
        current_def.controllers = controllers.items;
        current_def.controller_count = controllers.count;
        statedef_push(&statedefs, current_def, alloc);
    }

    out->statedefs = statedefs.items;
    out->statedef_count = statedefs.count;
    out->constants = constants;

    SDL_Log("CNS: parsed %u statedefs", statedefs.count);
    return true;
}

void mugen_cns_merge(Mugen_Cns* dst, Mugen_Cns* src, const Mel_Alloc* alloc)
{
    if (!src || src->statedef_count == 0) return;

    for (u32 si = 0; si < src->statedef_count; si++)
    {
        Mugen_Statedef* incoming = &src->statedefs[si];

        bool replaced = false;
        for (u32 di = 0; di < dst->statedef_count; di++)
        {
            if (dst->statedefs[di].stateno == incoming->stateno)
            {
                dst->statedefs[di] = *incoming;
                replaced = true;
                break;
            }
        }

        if (!replaced)
        {
            u32 new_count = dst->statedef_count + 1;
            Mugen_Statedef* new_defs = mel_alloc(alloc, new_count * sizeof(Mugen_Statedef));
            if (dst->statedefs)
            {
                memcpy(new_defs, dst->statedefs, dst->statedef_count * sizeof(Mugen_Statedef));
                mel_dealloc(alloc, dst->statedefs);
            }
            new_defs[dst->statedef_count] = *incoming;
            dst->statedefs = new_defs;
            dst->statedef_count = new_count;
        }
    }

    src->statedefs = NULL;
    src->statedef_count = 0;
}

void mugen_cns_shutdown(Mugen_Cns* cns, const Mel_Alloc* alloc)
{
    (void)alloc;
    *cns = (Mugen_Cns){0};
}

Mugen_Statedef* mugen_cns_get(Mugen_Cns* cns, i32 stateno)
{
    for (u32 i = 0; i < cns->statedef_count; i++)
    {
        if (cns->statedefs[i].stateno == stateno)
            return &cns->statedefs[i];
    }
    return NULL;
}

