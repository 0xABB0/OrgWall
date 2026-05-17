#pragma once

#include "core.types.h"
#include "str8.fwd.h"
#include "allocator.fwd.h"

typedef struct {
    str8 name;
    str8 command;
    i32 time;
} Mugen_Cmd_Def;

#define MUGEN_STATETYPE_ANY 0
#define MUGEN_STATETYPE_S   1
#define MUGEN_STATETYPE_C   2
#define MUGEN_STATETYPE_A   3

typedef struct {
    str8 label;
    u32 action_number;
    str8 command_name;
    str8 neg_command_name;
    str8 hold_command_name;
    u32 statetype;
    bool statetype_not;
    bool requires_ctrl;
    bool multi_command;
} Mugen_Cmd_State_Entry;

typedef struct {
    Mugen_Cmd_Def* commands;
    u32 command_count;
    Mugen_Cmd_State_Entry* state_entries;
    u32 state_entry_count;
} Mugen_Cmd;

bool mugen_cmd_load(Mugen_Cmd* out, str8 data, const Mel_Alloc* alloc);
void mugen_cmd_shutdown(Mugen_Cmd* cmd, const Mel_Alloc* alloc);
