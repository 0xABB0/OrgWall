#include "mugen_cmd.h"
#include "string.str8.h"
#include "allocator.h"
#include <SDL3/SDL.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>

typedef struct {
    str8 data;
    usize pos;
} Parser;

static str8 next_line(Parser* p)
{
    if (p->pos >= (usize)p->data.len) return STR8_EMPTY;

    usize start = p->pos;
    while (p->pos < (usize)p->data.len && p->data.data[p->pos] != '\n')
        p->pos++;

    usize end = p->pos;
    if (end > start && p->data.data[end - 1] == '\r')
        end--;

    if (p->pos < (usize)p->data.len)
        p->pos++;

    return str8_from_parts(p->data.data + start, (size)(end - start));
}

static str8 trim(str8 s)
{
    size start = 0;
    while (start < s.len && (s.data[start] == ' ' || s.data[start] == '\t'))
        start++;
    size end = s.len;
    while (end > start && (s.data[end - 1] == ' ' || s.data[end - 1] == '\t'))
        end--;
    return str8_from_parts(s.data + start, end - start);
}

static bool starts_with(str8 s, const char* prefix)
{
    size plen = (size)strlen(prefix);
    if (s.len < plen) return false;
    return memcmp(s.data, prefix, (size_t)plen) == 0;
}

static bool str8_eq_cstr(str8 s, const char* c)
{
    size clen = (size)strlen(c);
    if (s.len != clen) return false;
    return memcmp(s.data, c, (size_t)clen) == 0;
}

static str8 after_eq(str8 line)
{
    for (size i = 0; i < line.len; i++)
    {
        if (line.data[i] == '=')
            return trim(str8_from_parts(line.data + i + 1, line.len - i - 1));
    }
    return STR8_EMPTY;
}

static str8 strip_quotes(str8 s)
{
    if (s.len >= 2 && s.data[0] == '"' && s.data[s.len - 1] == '"')
        return str8_from_parts(s.data + 1, s.len - 2);
    return s;
}

typedef struct {
    Mugen_Cmd_Def* items;
    u32 count;
    u32 capacity;
} Cmd_Def_List;

typedef struct {
    Mugen_Cmd_State_Entry* items;
    u32 count;
    u32 capacity;
} State_Entry_List;

static void cmd_def_push(Cmd_Def_List* list, Mugen_Cmd_Def def, const Mel_Alloc* alloc)
{
    if (list->count >= list->capacity)
    {
        u32 new_cap = list->capacity == 0 ? 32 : list->capacity * 2;
        Mugen_Cmd_Def* new_items = mel_alloc(alloc, new_cap * sizeof(Mugen_Cmd_Def));
        if (list->items)
        {
            memcpy(new_items, list->items, list->count * sizeof(Mugen_Cmd_Def));
            mel_dealloc(alloc, list->items);
        }
        list->items = new_items;
        list->capacity = new_cap;
    }
    list->items[list->count++] = def;
}

static void state_entry_push(State_Entry_List* list, Mugen_Cmd_State_Entry entry, const Mel_Alloc* alloc)
{
    if (list->count >= list->capacity)
    {
        u32 new_cap = list->capacity == 0 ? 32 : list->capacity * 2;
        Mugen_Cmd_State_Entry* new_items = mel_alloc(alloc, new_cap * sizeof(Mugen_Cmd_State_Entry));
        if (list->items)
        {
            memcpy(new_items, list->items, list->count * sizeof(Mugen_Cmd_State_Entry));
            mel_dealloc(alloc, list->items);
        }
        list->items = new_items;
        list->capacity = new_cap;
    }
    list->items[list->count++] = entry;
}

static bool is_hold_command(str8 name)
{
    return str8_eq_cstr(name, "holddown")
        || str8_eq_cstr(name, "holdfwd")
        || str8_eq_cstr(name, "holdback")
        || str8_eq_cstr(name, "holdup");
}

static void parse_trigger_condition(str8 cond, Mugen_Cmd_State_Entry* entry)
{
    cond = trim(cond);

    if (starts_with(cond, "command"))
    {
        bool negated = false;
        str8 rest = str8_from_parts(cond.data + 7, cond.len - 7);
        rest = trim(rest);

        if (rest.len > 0 && rest.data[0] == '!')
        {
            negated = true;
            rest = trim(str8_from_parts(rest.data + 1, rest.len - 1));
        }

        if (rest.len > 0 && rest.data[0] == '=')
        {
            rest = trim(str8_from_parts(rest.data + 1, rest.len - 1));
            str8 name = strip_quotes(rest);
            if (negated)
            {
                entry->neg_command_name = name;
            }
            else if (is_hold_command(name))
            {
                entry->hold_command_name = name;
            }
            else
            {
                if (entry->command_name.len > 0)
                    entry->multi_command = true;
                entry->command_name = name;
            }
        }
    }
    else if (starts_with(cond, "statetype"))
    {
        str8 rest = str8_from_parts(cond.data + 9, cond.len - 9);
        rest = trim(rest);

        bool negated = false;
        if (rest.len >= 2 && rest.data[0] == '!' && rest.data[1] == '=')
        {
            negated = true;
            rest = trim(str8_from_parts(rest.data + 2, rest.len - 2));
        }
        else if (rest.len >= 1 && rest.data[0] == '=')
        {
            rest = trim(str8_from_parts(rest.data + 1, rest.len - 1));
        }

        entry->statetype_not = negated;
        if (rest.len > 0)
        {
            switch (rest.data[0])
            {
                case 'S': entry->statetype = MUGEN_STATETYPE_S; break;
                case 'C': entry->statetype = MUGEN_STATETYPE_C; break;
                case 'A': entry->statetype = MUGEN_STATETYPE_A; break;
            }
        }
    }
    else if (str8_eq_cstr(cond, "ctrl"))
    {
        entry->requires_ctrl = true;
    }
}

bool mugen_cmd_load(Mugen_Cmd* out, str8 data, const Mel_Alloc* alloc)
{
    assert(out);
    assert(alloc);

    *out = (Mugen_Cmd){0};

    Parser p = { .data = data, .pos = 0 };

    Cmd_Def_List cmds = {0};
    State_Entry_List entries = {0};

    bool in_command = false;
    bool in_state_entry = false;
    bool past_statedef = false;

    Mugen_Cmd_Def current_cmd = {0};
    Mugen_Cmd_State_Entry current_entry = {0};

    while (p.pos < (usize)p.data.len)
    {
        str8 raw_line = next_line(&p);
        str8 line = trim(raw_line);

        if (line.len == 0) continue;
        if (line.data[0] == ';') continue;

        if (line.data[0] == '[')
        {
            if (in_command && current_cmd.name.len > 0 && current_cmd.command.len > 0)
                cmd_def_push(&cmds, current_cmd, alloc);

            if (in_state_entry && current_entry.command_name.len > 0)
                state_entry_push(&entries, current_entry, alloc);

            in_command = false;
            in_state_entry = false;

            if (starts_with(line, "[Command]"))
            {
                in_command = true;
                current_cmd = (Mugen_Cmd_Def){ .time = 15 };
            }
            else if (starts_with(line, "[Statedef -1]"))
            {
                past_statedef = true;
            }
            else if (past_statedef && starts_with(line, "[State -1"))
            {
                in_state_entry = true;
                current_entry = (Mugen_Cmd_State_Entry){0};
                current_entry.requires_ctrl = false;

                for (size i = 0; i < line.len; i++)
                {
                    if (line.data[i] == ',')
                    {
                        str8 rest = trim(str8_from_parts(line.data + i + 1, line.len - i - 1));
                        if (rest.len > 0 && rest.data[rest.len - 1] == ']')
                            rest = str8_from_parts(rest.data, rest.len - 1);
                        current_entry.label = trim(rest);
                        break;
                    }
                }
            }

            continue;
        }

        if (in_command)
        {
            if (starts_with(line, "name"))
            {
                str8 val = after_eq(line);
                current_cmd.name = strip_quotes(val);
            }
            else if (starts_with(line, "command"))
            {
                current_cmd.command = after_eq(line);
            }
            else if (starts_with(line, "time"))
            {
                str8 val = after_eq(line);
                current_cmd.time = (i32)strtol((const char*)val.data, NULL, 10);
            }
        }

        if (in_state_entry)
        {
            if (starts_with(line, "type"))
            {
                // we only care about ChangeState
            }
            else if (starts_with(line, "value"))
            {
                str8 val = after_eq(line);
                current_entry.action_number = (u32)strtoul((const char*)val.data, NULL, 10);
            }
            else if (starts_with(line, "trigger"))
            {
                str8 val = after_eq(line);
                parse_trigger_condition(val, &current_entry);
            }
        }
    }

    if (in_command && current_cmd.name.len > 0 && current_cmd.command.len > 0)
        cmd_def_push(&cmds, current_cmd, alloc);

    if (in_state_entry && current_entry.command_name.len > 0)
        state_entry_push(&entries, current_entry, alloc);

    out->commands = cmds.items;
    out->command_count = cmds.count;
    out->state_entries = entries.items;
    out->state_entry_count = entries.count;

    SDL_Log("CMD: parsed %u commands, %u state entries", cmds.count, entries.count);

    return true;
}

void mugen_cmd_shutdown(Mugen_Cmd* cmd, const Mel_Alloc* alloc)
{
    assert(cmd);
    if (cmd->commands)
        mel_dealloc(alloc, cmd->commands);
    if (cmd->state_entries)
        mel_dealloc(alloc, cmd->state_entries);
    *cmd = (Mugen_Cmd){0};
}

