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
    return mugen_sc_lookup_name(trim(s));
}

static i32 parse_trigger_level(str8 key)
{
    if (str8_ieq_cstr(key, "triggerall")) return 0;
    if (starts_with_i(key, "trigger"))
    {
        str8 num_part = str8_from_parts(key.data + 7, key.len - 7);
        return parse_int(num_part);
    }
    return -1;
}

static void parse_controller_param(Mugen_State_Controller* sc, str8 key, str8 val, const Mel_Alloc* alloc)
{
    Mugen_SC_Reg* reg = mugen_sc_get_reg(sc->type);
    if (reg && reg->parse_param)
        reg->parse_param(sc, key, val, alloc);
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
            if (str8_ieq_cstr(key, "life"))               c->life = parse_int(val);
            else if (str8_ieq_cstr(key, "attack"))         c->attack = parse_int(val);
            else if (str8_ieq_cstr(key, "defence"))        c->defence = parse_int(val);
            else if (str8_ieq_cstr(key, "fall.defence_up")) c->fall_defence_up = parse_int(val);
            else if (str8_ieq_cstr(key, "liedown.time"))   c->liedown_time = parse_int(val);
            else if (str8_ieq_cstr(key, "airjuggle"))      c->airjuggle = parse_int(val);
            else if (str8_ieq_cstr(key, "sparkno"))        c->sparkno = parse_int(val);
            else if (str8_ieq_cstr(key, "guard.sparkno"))  c->guard_sparkno = parse_int(val);
            else if (str8_ieq_cstr(key, "power.max"))      c->power_max = parse_int(val);
            break;
        }
        case 2:
        {
            if (str8_ieq_cstr(key, "xscale"))              c->xscale = parse_float(val);
            else if (str8_ieq_cstr(key, "yscale"))          c->yscale = parse_float(val);
            else if (str8_ieq_cstr(key, "ground.back"))     c->ground_back = parse_float(val);
            else if (str8_ieq_cstr(key, "ground.front"))    c->ground_front = parse_float(val);
            else if (str8_ieq_cstr(key, "air.back"))        c->air_back = parse_float(val);
            else if (str8_ieq_cstr(key, "air.front"))       c->air_front = parse_float(val);
            else if (str8_ieq_cstr(key, "height"))          c->height = parse_float(val);
            else if (str8_ieq_cstr(key, "attack.dist"))     c->attack_dist = parse_float(val);
            break;
        }
        case 3:
        {
            str8 rest;
            str8 first = split_comma_first(val, &rest);
            if (str8_ieq_cstr(key, "walk.fwd"))             c->walk_fwd_x = parse_float(first);
            else if (str8_ieq_cstr(key, "walk.back"))       c->walk_back_x = parse_float(first);
            else if (str8_ieq_cstr(key, "run.fwd"))
            {
                c->run_fwd_x = parse_float(first);
                if (rest.len > 0) c->run_fwd_y = -parse_float(rest);
            }
            else if (str8_ieq_cstr(key, "run.back"))
            {
                c->run_back_x = parse_float(first);
                if (rest.len > 0) c->run_back_y = -parse_float(rest);
            }
            else if (str8_ieq_cstr(key, "jump.neu"))
            {
                c->jump_neu_x = parse_float(first);
                if (rest.len > 0) c->jump_y = -parse_float(rest);
            }
            else if (str8_ieq_cstr(key, "jump.back"))       c->jump_back_x = parse_float(first);
            else if (str8_ieq_cstr(key, "jump.fwd"))        c->jump_fwd_x = parse_float(first);
            else if (str8_ieq_cstr(key, "runjump.back"))
            {
                c->runjump_back_x = parse_float(first);
                if (rest.len > 0) c->runjump_y = -parse_float(rest);
            }
            else if (str8_ieq_cstr(key, "runjump.fwd"))
            {
                c->runjump_fwd_x = parse_float(first);
                if (rest.len > 0) c->runjump_y = -parse_float(rest);
            }
            else if (str8_ieq_cstr(key, "airjump.neu"))
            {
                c->airjump_neu_x = parse_float(first);
                if (rest.len > 0) c->airjump_y = -parse_float(rest);
            }
            else if (str8_ieq_cstr(key, "airjump.back"))    c->airjump_back_x = parse_float(first);
            else if (str8_ieq_cstr(key, "airjump.fwd"))     c->airjump_fwd_x = parse_float(first);
            break;
        }
        case 4:
        {
            if (str8_ieq_cstr(key, "airjump.num"))                    c->airjump_num = parse_int(val);
            else if (str8_ieq_cstr(key, "airjump.height"))             c->airjump_height = parse_int(val);
            else if (str8_ieq_cstr(key, "yaccel"))                     c->yaccel = parse_float(val);
            else if (str8_ieq_cstr(key, "stand.friction"))             c->stand_friction = parse_float(val);
            else if (str8_ieq_cstr(key, "crouch.friction"))            c->crouch_friction = parse_float(val);
            else if (str8_ieq_cstr(key, "stand.friction.threshold"))   c->stand_friction_threshold = parse_float(val);
            else if (str8_ieq_cstr(key, "crouch.friction.threshold"))  c->crouch_friction_threshold = parse_float(val);
            else if (str8_ieq_cstr(key, "down.bounce.offset"))
            {
                str8 brest;
                str8 bfirst = split_comma_first(val, &brest);
                c->down_bounce_offset_x = parse_float(bfirst);
                if (brest.len > 0) c->down_bounce_offset_y = parse_float(brest);
            }
            else if (str8_ieq_cstr(key, "down.bounce.yaccel"))         c->down_bounce_yaccel = parse_float(val);
            else if (str8_ieq_cstr(key, "down.bounce.groundlevel"))    c->down_bounce_groundlevel = parse_float(val);
            else if (str8_ieq_cstr(key, "down.friction.threshold"))    c->down_friction_threshold = parse_float(val);
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
            if (str8_ieq_cstr(key, "type")) current_def.statetype = parse_statetype_char(val);
            else if (str8_ieq_cstr(key, "movetype")) current_def.movetype = parse_movetype_char(val);
            else if (str8_ieq_cstr(key, "physics")) current_def.physics = parse_statetype_char(val);
            else if (str8_ieq_cstr(key, "anim")) current_def.anim = parse_int(val);
            else if (str8_ieq_cstr(key, "velset"))
            {
                str8 rest;
                str8 vx = split_comma_first(val, &rest);
                current_def.velset_x = parse_float(vx);
                if (rest.len > 0) current_def.velset_y = parse_float(rest);
                current_def.has_velset = true;
            }
            else if (str8_ieq_cstr(key, "ctrl")) current_def.ctrl = parse_int(val);
            else if (str8_ieq_cstr(key, "juggle")) current_def.juggle = parse_int(val);
            else if (str8_ieq_cstr(key, "poweradd")) current_def.poweradd = parse_int(val);
            else if (str8_ieq_cstr(key, "sprpriority")) current_def.sprpriority = parse_int(val);
            else if (str8_ieq_cstr(key, "hitdefpersist")) current_def.hitdefpersist = parse_int(val) != 0;
            else if (str8_ieq_cstr(key, "movehitpersist")) current_def.movehitpersist = parse_int(val) != 0;
            else if (str8_ieq_cstr(key, "hitcountpersist")) current_def.hitcountpersist = parse_int(val) != 0;
        }
        else
        {
            if (str8_ieq_cstr(key, "type"))
            {
                current_ctrl.type = parse_controller_type(val);
            }
            else if (str8_ieq_cstr(key, "persistent"))
            {
                current_ctrl.persistent = parse_int(val);
            }
            else if (str8_ieq_cstr(key, "ignorehitpause"))
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

