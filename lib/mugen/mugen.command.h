#pragma once

#include "mugen.input.h"
#include "string.str8.fwd.h"
#include "allocator.h"

typedef struct {
    Command_Key* keys;
    u32 key_count;
    bool greater;
    bool or_logic;
} Command_Step;

typedef struct {
    str8 name;

    Command_Step* steps;
    u32 step_count;

    i32 max_time;
    i32 cur_time;

    i32 max_buf_time;
    i32 cur_buf_time;

    i32 max_step_time;
    i32* step_timers;

    bool* completed;
    i32* loop_order;
    u32 loop_order_count;

    bool complete_frame;

    bool buffer_hitpause;
    bool buffer_pauseend;
    bool buffer_shared;
    bool autogreater;
} Command;

typedef struct Command_List {
    Input_Buffer buffer;

    Command* commands;
    u32 command_count;
    u32 command_capacity;

    const Mel_Alloc* alloc;

    i32 default_time;
    i32 default_step_time;
    i32 default_buf_time;
    bool default_autogreater;
    bool default_buffer_hitpause;
    bool default_buffer_pauseend;
    bool default_buffer_shared;
} Command_List;
#define COMMAND_LIST_DEFINED

typedef struct {
    i32 time;
    i32 buf_time;
    i32 step_time;
    bool no_buffer_hitpause;
    bool no_buffer_pauseend;
    bool no_buffer_shared;
    bool no_autogreater;
} Command_Add_Opt;

void command_list_init(Command_List* cl, bool facing_right, const Mel_Alloc* alloc);
void command_list_shutdown(Command_List* cl);

void command_list_add_opt(Command_List* cl, str8 name, str8 cmd_string, Command_Add_Opt opt);
#define command_list_add(cl, name, cmd, ...) \
    command_list_add_opt((cl), (name), (cmd), (Command_Add_Opt){__VA_ARGS__})

void command_list_step(Command_List* cl, bool U, bool D, bool L, bool R,
                       bool a, bool b, bool c, bool x, bool y, bool z,
                       bool s, bool d, bool w, bool m,
                       bool hpbuf, bool pausebuf, i32 extratime);

bool command_list_active(Command_List* cl, str8 name);
void command_list_assert(Command_List* cl, str8 name, i32 time);
void command_list_clear_name(Command_List* cl, str8 name);
void command_list_set_facing(Command_List* cl, bool facing_right);
