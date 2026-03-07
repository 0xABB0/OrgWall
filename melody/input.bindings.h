#pragma once

#include "input.bindings.fwd.h"
#include "input.stack.h"
#include "allocator.fwd.h"
#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_events.h>

typedef struct {
    SDL_Scancode key;
    Mel_Input_Action action;
} Mel_Input_Binding;

struct Mel_Input_Bindings {
    Mel_Input_Binding* entries;
    u32 count;
    u32 capacity;
    const Mel_Alloc* alloc;
};

typedef struct {
    Mel_Input_Binding* bindings;
    u32 binding_count;
    const Mel_Alloc* alloc;
} Mel_Input_Bindings_Opt;

void mel_input_bindings_init_opt(Mel_Input_Bindings* b, Mel_Input_Bindings_Opt opt);
#define mel_input_bindings_init(b, ...) mel_input_bindings_init_opt((b), (Mel_Input_Bindings_Opt){__VA_ARGS__})

void mel_input_bindings_shutdown(Mel_Input_Bindings* b);

void mel_input_bindings_add(Mel_Input_Bindings* b, SDL_Scancode key, Mel_Input_Action action);
void mel_input_bindings_remove_key(Mel_Input_Bindings* b, SDL_Scancode key);
void mel_input_bindings_remove_action(Mel_Input_Bindings* b, Mel_Input_Action action);
SDL_Scancode mel_input_bindings_get_key(Mel_Input_Bindings* b, Mel_Input_Action action);

Mel_Input_Map_Output mel_input_mapper_keyboard(SDL_Event* event, void* user);
