#pragma once

#include "input.stack.fwd.h"
#include "allocator.fwd.h"
#include <SDL3/SDL_events.h>

typedef struct {
    Mel_Input_Action action;
    f32 value;
} Mel_Input_Mapped;

typedef struct {
    Mel_Input_Mapped results[MEL_INPUT_MAP_MAX];
    u32 count;
} Mel_Input_Map_Output;

typedef Mel_Input_Map_Output (*Mel_Input_Mapper_Fn)(SDL_Event* event, void* user);
typedef bool (*Mel_Input_Action_Fn)(Mel_Input_Action action, f32 value, void* user);

struct Mel_Input_Layer {
    Mel_Input_Mapper_Fn mapper;
    void* mapper_user;
    Mel_Input_Action_Fn on_action;
    void* user;
    bool opaque;
};

struct Mel_Input_Stack {
    Mel_Input_Layer** layers;
    u32 count;
    u32 capacity;
    const Mel_Alloc* alloc;
};

typedef struct {
    const Mel_Alloc* alloc;
} Mel_Input_Stack_Opt;

void mel_input_stack_init_opt(Mel_Input_Stack* stack, Mel_Input_Stack_Opt opt);
#define mel_input_stack_init(stack, ...) mel_input_stack_init_opt((stack), (Mel_Input_Stack_Opt){__VA_ARGS__})

void mel_input_stack_shutdown(Mel_Input_Stack* stack);

typedef struct {
    Mel_Input_Mapper_Fn mapper;
    void* mapper_user;
    Mel_Input_Action_Fn on_action;
    void* user;
    bool opaque;
} Mel_Input_Layer_Desc;

Mel_Input_Layer* mel_input_stack_push_opt(Mel_Input_Stack* stack, Mel_Input_Layer_Desc desc);
#define mel_input_stack_push(stack, ...) mel_input_stack_push_opt((stack), (Mel_Input_Layer_Desc){__VA_ARGS__})

void mel_input_stack_remove(Mel_Input_Stack* stack, Mel_Input_Layer* layer);

void mel_input_stack_dispatch(Mel_Input_Stack* stack, SDL_Event* event);
