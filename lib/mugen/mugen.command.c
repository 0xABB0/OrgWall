#include "mugen.command.h"
#include "str8.h"
#include "collection.array.h"
#include <string.h>

static i32 max2(i32 a, i32 b) { return a > b ? a : b; }

static bool key_matches(Command_Key key, Input_Buffer* buf)
{
    i32 t = input_buffer_state(buf, key);

    bool ok;
    if (key.slash) ok = (t > 0);
    else           ok = (t == 1);

    if (ok && key.chargetime > 1)
        ok = input_buffer_state_charge(buf, key) >= key.chargetime;

    return ok;
}

static bool step_matches(Command_Step* step, Input_Buffer* buf)
{
    if (step->or_logic)
    {
        for (u32 i = 0; i < step->key_count; i++)
            if (key_matches(step->keys[i], buf)) return true;
        return false;
    }

    for (u32 i = 0; i < step->key_count; i++)
        if (!key_matches(step->keys[i], buf)) return false;
    return step->key_count > 0;
}

static bool step_is_dir_only(Command_Step* s)
{
    if (s->key_count == 0) return false;
    for (u32 i = 0; i < s->key_count; i++)
        if (cmd_key_is_button(s->keys[i])) return false;
    return true;
}

static bool step_has_btn_press(Command_Step* s)
{
    for (u32 i = 0; i < s->key_count; i++)
        if (cmd_key_is_btn_press(s->keys[i])) return true;
    return false;
}

static bool step_has_slash(Command_Step* s)
{
    for (u32 i = 0; i < s->key_count; i++)
        if (s->keys[i].slash) return true;
    return false;
}

static bool step_shares_key(Command_Step* a, Command_Step* b)
{
    for (u32 i = 0; i < a->key_count; i++)
        for (u32 j = 0; j < b->key_count; j++)
            if (a->keys[i].key == b->keys[j].key) return true;
    return false;
}

static bool step_has_dir_release(Command_Step* s)
{
    for (u32 i = 0; i < s->key_count; i++)
        if (cmd_key_is_dir_release(s->keys[i])) return true;
    return false;
}

static bool step_has_non_dir_release(Command_Step* s)
{
    for (u32 i = 0; i < s->key_count; i++)
        if (!cmd_key_is_dir_release(s->keys[i])) return true;
    return false;
}

static bool is_dir_to_button(Command_Step* prev, Command_Step* cur)
{
    if (step_is_dir_only(prev) && step_has_btn_press(cur) && !step_has_slash(cur) && !step_shares_key(prev, cur))
        return true;
    if (step_has_dir_release(prev) && step_has_non_dir_release(cur))
        return true;
    return false;
}

static bool step_uses_lr_group(Command_Step* s)
{
    for (u32 i = 0; i < s->key_count; i++)
    {
        Cmd_Key_Id k = s->keys[i].key;
        if (k == CK_L || k == CK_R || k == CK_UL || k == CK_UR || k == CK_DL || k == CK_DR)
            return true;
    }
    return false;
}

static bool step_contains_key(Command_Step* s, Cmd_Key_Id key, bool tilde)
{
    for (u32 i = 0; i < s->key_count; i++)
        if (s->keys[i].key == key && s->keys[i].tilde == tilde) return true;
    return false;
}

static bool step_contains_key_any_tilde(Command_Step* s, Cmd_Key_Id key)
{
    for (u32 i = 0; i < s->key_count; i++)
        if (s->keys[i].key == key) return true;
    return false;
}

static bool greater_check_fail(Command* cmd, u32 step_idx, Input_Buffer* buf)
{
    Command_Step* step = &cmd->steps[step_idx];
    bool use_lr = step_uses_lr_group(step);

    Cmd_Key_Id dirs_bf[] = { CK_U, CK_D, CK_B, CK_F, CK_UB, CK_UF, CK_DB, CK_DF };
    Cmd_Key_Id dirs_lr[] = { CK_U, CK_D, CK_L, CK_R, CK_UL, CK_UR, CK_DL, CK_DR };
    Cmd_Key_Id* dirs = use_lr ? dirs_lr : dirs_bf;
    u32 dir_count = 8;

    for (u32 i = 0; i < dir_count; i++)
    {
        if (step_contains_key_any_tilde(step, dirs[i]))
            continue;

        Command_Key probe = { .key = dirs[i], .tilde = false };
        if (input_buffer_state(buf, probe) == 1)
            return true;

        probe.tilde = true;
        if (input_buffer_state(buf, probe) == 1)
            return true;
    }

    for (Cmd_Key_Id b = CK_a; b <= CK_m; b++)
    {
        if (step_contains_key_any_tilde(step, b))
            continue;

        Command_Key probe = { .key = b, .tilde = false };
        if (input_buffer_state(buf, probe) == 1)
            return true;

        probe.tilde = true;
        if (input_buffer_state(buf, probe) == 1)
            return true;
    }

    return false;
}

static void command_clear(Command* cmd, bool bufreset)
{
    cmd->cur_time = 0;
    if (bufreset) cmd->cur_buf_time = 0;
    for (u32 i = 0; i < cmd->step_count; i++)
    {
        cmd->completed[i] = false;
        cmd->step_timers[i] = 0;
    }
}

static void build_loop_order(Command* cmd, const Mel_Alloc* alloc)
{
    cmd->loop_order = mel_alloc(alloc, sizeof(i32) * cmd->step_count);
    cmd->loop_order_count = 0;

    typedef Mel_Array(i32) I32_Array;
    I32_Array result;
    mel_array_init(&result, alloc);

    i32 i = (i32)cmd->step_count - 1;
    while (i >= 0)
    {
        if (i > 0 && is_dir_to_button(&cmd->steps[i - 1], &cmd->steps[i]))
        {
            i32 chain_start = i - 1;
            while (chain_start > 0 && is_dir_to_button(&cmd->steps[chain_start - 1], &cmd->steps[chain_start]))
                chain_start--;

            for (i32 j = chain_start; j <= i; j++)
                mel_array_push(&result, j);
            i = chain_start - 1;
        }
        else
        {
            mel_array_push(&result, i);
            i--;
        }
    }

    memcpy(cmd->loop_order, result.items, sizeof(i32) * result.count);
    cmd->loop_order_count = (u32)result.count;
    mel_array_free(&result);
}

static bool is_single_dir(Command_Step* s)
{
    if (s->key_count != 1) return false;
    return cmd_key_is_dir_press(s->keys[0]) || cmd_key_is_dir_release(s->keys[0]);
}

static void auto_greater_expand(Command* cmd, const Mel_Alloc* alloc)
{
    typedef Mel_Array(Command_Step) Step_Array;
    Step_Array expanded;
    mel_array_init(&expanded, alloc);

    for (u32 i = 0; i < cmd->step_count; i++)
    {
        mel_array_push(&expanded, cmd->steps[i]);

        if (i + 1 < cmd->step_count
            && is_single_dir(&cmd->steps[i])
            && is_single_dir(&cmd->steps[i + 1])
            && !cmd->steps[i].keys[0].tilde
            && !cmd->steps[i + 1].keys[0].tilde
            && cmd->steps[i].keys[0].key == cmd->steps[i + 1].keys[0].key)
        {
            Command_Key release_key = cmd->steps[i].keys[0];
            release_key.tilde = true;

            Command_Key* rk = mel_alloc(alloc, sizeof(Command_Key));
            *rk = release_key;

            Command_Step release_step = {
                .keys = rk,
                .key_count = 1,
                .greater = true,
                .or_logic = false,
            };
            mel_array_push(&expanded, release_step);

            cmd->steps[i + 1].greater = true;
        }
    }

    if (expanded.count != cmd->step_count)
    {
        mel_dealloc(alloc, cmd->steps);
        cmd->steps = expanded.items;
        cmd->step_count = (u32)expanded.count;
    }
    else
    {
        mel_array_free(&expanded);
    }
}

static void skip_whitespace(const u8** p, const u8* end)
{
    while (*p < end && (**p == ' ' || **p == '\t')) (*p)++;
}

static i32 parse_number(const u8** p, const u8* end)
{
    i32 val = 0;
    while (*p < end && **p >= '0' && **p <= '9')
    {
        val = val * 10 + (**p - '0');
        (*p)++;
    }
    return val;
}

static bool parse_key_name(const u8** p, const u8* end, Cmd_Key_Id* out)
{
    if (*p >= end) return false;

    if (*p + 1 < end)
    {
        u8 c0 = **p, c1 = *(*p + 1);
        if (c0 == 'U' && c1 == 'B') { *out = CK_UB; *p += 2; return true; }
        if (c0 == 'U' && c1 == 'F') { *out = CK_UF; *p += 2; return true; }
        if (c0 == 'D' && c1 == 'B') { *out = CK_DB; *p += 2; return true; }
        if (c0 == 'D' && c1 == 'F') { *out = CK_DF; *p += 2; return true; }
        if (c0 == 'U' && c1 == 'L') { *out = CK_UL; *p += 2; return true; }
        if (c0 == 'U' && c1 == 'R') { *out = CK_UR; *p += 2; return true; }
        if (c0 == 'D' && c1 == 'L') { *out = CK_DL; *p += 2; return true; }
        if (c0 == 'D' && c1 == 'R') { *out = CK_DR; *p += 2; return true; }
    }

    switch (**p)
    {
        case 'U': *out = CK_U; (*p)++; return true;
        case 'D': *out = CK_D; (*p)++; return true;
        case 'B': *out = CK_B; (*p)++; return true;
        case 'F': *out = CK_F; (*p)++; return true;
        case 'L': *out = CK_L; (*p)++; return true;
        case 'R': *out = CK_R; (*p)++; return true;
        case 'N': *out = CK_N; (*p)++; return true;
        case 'a': *out = CK_a; (*p)++; return true;
        case 'b': *out = CK_b; (*p)++; return true;
        case 'c': *out = CK_c; (*p)++; return true;
        case 'x': *out = CK_x; (*p)++; return true;
        case 'y': *out = CK_y; (*p)++; return true;
        case 'z': *out = CK_z; (*p)++; return true;
        case 's': *out = CK_s; (*p)++; return true;
        case 'd': *out = CK_d; (*p)++; return true;
        case 'w': *out = CK_w; (*p)++; return true;
        case 'm': *out = CK_m; (*p)++; return true;
        default:  return false;
    }
}

static bool command_parse(str8 cmd_string, Command* cmd, const Mel_Alloc* alloc)
{
    typedef Mel_Array(Command_Step) Step_Array;
    typedef Mel_Array(Command_Key) Key_Array;

    Step_Array steps;
    mel_array_init(&steps, alloc);

    const u8* p = cmd_string.data;
    const u8* end = cmd_string.data + cmd_string.len;

    while (p < end)
    {
        skip_whitespace(&p, end);
        if (p >= end) break;

        bool step_greater = false;
        if (*p == '>')
        {
            step_greater = true;
            p++;
            skip_whitespace(&p, end);
        }

        Key_Array keys;
        mel_array_init(&keys, alloc);
        bool or_logic = false;
        bool first_key = true;
        bool seen_combinator = false;
        (void)seen_combinator;

        while (p < end)
        {
            skip_whitespace(&p, end);
            if (p >= end) break;

            bool tilde = false;
            bool slash = false;
            bool dollar = false;
            i32 chargetime = 0;

            if (*p == '~')
            {
                tilde = true;
                p++;
                if (p < end && *p >= '0' && *p <= '9')
                    chargetime = parse_number(&p, end);
            }
            else if (*p == '/')
            {
                slash = true;
                p++;
                if (p < end && *p >= '0' && *p <= '9')
                    chargetime = parse_number(&p, end);
            }

            if (p < end && *p == '$')
            {
                dollar = true;
                p++;
            }

            Cmd_Key_Id key_id;
            if (!parse_key_name(&p, end, &key_id))
                break;

            Command_Key key = {
                .key = key_id,
                .slash = slash,
                .tilde = tilde,
                .dollar = dollar,
                .chargetime = chargetime,
            };
            mel_array_push(&keys, key);

            skip_whitespace(&p, end);

            if (p < end && *p == '+')
            {
                if (first_key) { or_logic = false; seen_combinator = true; }
                p++;
                first_key = false;
                continue;
            }
            if (p < end && *p == '|')
            {
                if (first_key) { or_logic = true; seen_combinator = true; }
                p++;
                first_key = false;
                continue;
            }

            break;
        }

        if (keys.count > 0)
        {
            Command_Step step = {
                .keys = keys.items,
                .key_count = (u32)keys.count,
                .greater = step_greater,
                .or_logic = or_logic,
            };
            mel_array_push(&steps, step);
        }
        else
        {
            mel_array_free(&keys);
        }

        skip_whitespace(&p, end);
        if (p < end && *p == ',')
        {
            p++;
            continue;
        }

        break;
    }

    cmd->steps = steps.items;
    cmd->step_count = (u32)steps.count;
    return true;
}

void command_list_init(Command_List* cl, bool facing_right, const Mel_Alloc* alloc)
{
    memset(cl, 0, sizeof(*cl));
    input_buffer_init(&cl->buffer, facing_right);
    cl->alloc = alloc;
    cl->default_time = 15;
    cl->default_step_time = -1;
    cl->default_buf_time = 1;
    cl->default_autogreater = true;
    cl->default_buffer_hitpause = true;
    cl->default_buffer_pauseend = true;
    cl->default_buffer_shared = true;
}

void command_list_shutdown(Command_List* cl)
{
    for (u32 i = 0; i < cl->command_count; i++)
    {
        Command* cmd = &cl->commands[i];
        for (u32 j = 0; j < cmd->step_count; j++)
            mel_dealloc(cl->alloc, cmd->steps[j].keys);
        mel_dealloc(cl->alloc, cmd->steps);
        mel_dealloc(cl->alloc, cmd->step_timers);
        mel_dealloc(cl->alloc, cmd->completed);
        mel_dealloc(cl->alloc, cmd->loop_order);
    }
    mel_dealloc(cl->alloc, cl->commands);
}

void command_list_add_opt(Command_List* cl, str8 name, str8 cmd_string, Command_Add_Opt opt)
{
    if (cl->command_count >= cl->command_capacity)
    {
        u32 new_cap = cl->command_capacity == 0 ? 8 : cl->command_capacity * 2;
        Command* new_cmds = mel_alloc(cl->alloc, sizeof(Command) * new_cap);
        if (cl->commands)
        {
            memcpy(new_cmds, cl->commands, sizeof(Command) * cl->command_count);
            mel_dealloc(cl->alloc, cl->commands);
        }
        cl->commands = new_cmds;
        cl->command_capacity = new_cap;
    }

    Command* cmd = &cl->commands[cl->command_count++];
    memset(cmd, 0, sizeof(*cmd));

    cmd->name = name;
    cmd->max_time = opt.time > 0 ? opt.time : cl->default_time;
    cmd->max_buf_time = opt.buf_time > 0 ? opt.buf_time : cl->default_buf_time;
    cmd->max_step_time = opt.step_time != 0 ? opt.step_time : cl->default_step_time;

    cmd->buffer_hitpause = !opt.no_buffer_hitpause && cl->default_buffer_hitpause;
    cmd->buffer_pauseend = !opt.no_buffer_pauseend && cl->default_buffer_pauseend;
    cmd->buffer_shared = !opt.no_buffer_shared && cl->default_buffer_shared;
    cmd->autogreater = !opt.no_autogreater && cl->default_autogreater;

    command_parse(cmd_string, cmd, cl->alloc);

    if (cmd->autogreater && cmd->step_count > 1)
        auto_greater_expand(cmd, cl->alloc);

    if (cmd->step_count > 0)
    {
        cmd->step_timers = mel_alloc(cl->alloc, sizeof(i32) * cmd->step_count);
        cmd->completed = mel_alloc(cl->alloc, sizeof(bool) * cmd->step_count);
        memset(cmd->step_timers, 0, sizeof(i32) * cmd->step_count);
        memset(cmd->completed, 0, sizeof(bool) * cmd->step_count);
        build_loop_order(cmd, cl->alloc);
    }
}

static void command_tick(Command* cmd, Input_Buffer* buf, bool hpbuf, bool pausebuf, i32 extratime)
{
    if (!cmd->buffer_hitpause) { hpbuf = false; extratime = 0; }
    if (!cmd->buffer_pauseend) { pausebuf = false; extratime = 0; }

    cmd->complete_frame = false;

    if (cmd->cur_buf_time > 0 && !hpbuf && !pausebuf)
        cmd->cur_buf_time--;

    if (cmd->step_count == 0) return;

    bool any_done = false;
    for (u32 i = 0; i < cmd->step_count; i++)
    {
        if (cmd->completed[i])
        {
            cmd->step_timers[i]++;
            if (cmd->max_step_time > 0 && cmd->step_timers[i] > cmd->max_step_time)
            {
                cmd->completed[i] = false;
                cmd->step_timers[i] = 0;
                continue;
            }
            any_done = true;
        }
    }

    if (any_done)
        cmd->cur_time++;
    else if (cmd->cur_time > 0)
        command_clear(cmd, false);

    u32 last = cmd->step_count - 1;

    for (u32 li = 0; li < cmd->loop_order_count; li++)
    {
        i32 i = cmd->loop_order[li];

        if (i > 0 && !cmd->completed[i - 1])
            continue;

        bool matched = step_matches(&cmd->steps[i], buf);

        if (cmd->steps[i].greater && i > 0 && cmd->completed[i - 1] && !cmd->completed[i])
        {
            if (greater_check_fail(cmd, (u32)i, buf))
            {
                matched = false;
                cmd->completed[i - 1] = false;
                cmd->step_timers[i - 1] = 0;
            }
        }

        if (matched)
        {
            cmd->completed[i] = true;
            cmd->step_timers[i] = 0;
            if (i > 0)
            {
                cmd->completed[i - 1] = false;
                cmd->step_timers[i - 1] = 0;
            }
            if (i == 0)
                cmd->cur_time = 0;
        }
    }

    cmd->complete_frame = cmd->completed[last];
    if (!cmd->complete_frame)
    {
        if (cmd->cur_time < cmd->max_time)
            return;
    }

    command_clear(cmd, false);
    if (cmd->complete_frame)
        cmd->cur_buf_time = max2(cmd->cur_buf_time, cmd->max_buf_time + extratime);
}

void command_list_step(Command_List* cl, bool U, bool D, bool L, bool R,
                       bool a, bool b, bool c, bool x, bool y, bool z,
                       bool s, bool d, bool w, bool m,
                       bool hpbuf, bool pausebuf, i32 extratime)
{
    input_buffer_update(&cl->buffer, U, D, L, R, a, b, c, x, y, z, s, d, w, m);

    for (u32 i = 0; i < cl->command_count; i++)
        command_tick(&cl->commands[i], &cl->buffer, hpbuf, pausebuf, extratime);

    for (u32 i = 0; i < cl->command_count; i++)
    {
        if (!cl->commands[i].complete_frame) continue;
        if (!cl->commands[i].buffer_shared) continue;
        command_list_clear_name(cl, cl->commands[i].name);
    }
}

static bool str8_eq_cstr_i(str8 a, const char* b)
{
    size blen = 0;
    while (b[blen]) blen++;
    if (a.len != blen) return false;
    for (size i = 0; i < blen; i++)
    {
        u8 ac = a.data[i]; if (ac >= 'A' && ac <= 'Z') ac += 32;
        u8 bc = (u8)b[i]; if (bc >= 'A' && bc <= 'Z') bc += 32;
        if (ac != bc) return false;
    }
    return true;
}

bool command_list_active(Command_List* cl, str8 name)
{
    if (str8_eq_cstr_i(name, "holdfwd"))   return cl->buffer.Fb > 0;
    if (str8_eq_cstr_i(name, "holdback"))  return cl->buffer.Bb > 0;
    if (str8_eq_cstr_i(name, "holdup"))    return cl->buffer.Ub > 0;
    if (str8_eq_cstr_i(name, "holddown"))  return cl->buffer.Db > 0;

    for (u32 i = 0; i < cl->command_count; i++)
        if (str8_equals(cl->commands[i].name, name) && cl->commands[i].cur_buf_time > 0)
            return true;
    return false;
}

void command_list_assert(Command_List* cl, str8 name, i32 time)
{
    for (u32 i = 0; i < cl->command_count; i++)
        if (str8_equals(cl->commands[i].name, name))
            cl->commands[i].cur_buf_time = time;
}

void command_list_clear_name(Command_List* cl, str8 name)
{
    for (u32 i = 0; i < cl->command_count; i++)
    {
        if (!str8_equals(cl->commands[i].name, name)) continue;
        if (!cl->commands[i].buffer_shared) continue;
        if (cl->commands[i].complete_frame) continue;
        command_clear(&cl->commands[i], false);
    }
}

void command_list_set_facing(Command_List* cl, bool facing_right)
{
    cl->buffer.facing_right = facing_right;
}
