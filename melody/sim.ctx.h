#pragma once

#include "sim.ctx.fwd.h"
#include "core.types.h"
#include "math.rng.h"
#include "allocator.block.h"
#include "allocator.fwd.h"

typedef struct {
    i32 type;
    u32 size;
} Mel_Sim_Event_Header;

typedef void (*Mel_Update_Fn)(Mel_Sim_Ctx* sim, f32 dt, void* user);

typedef struct {
    Mel_Update_Fn fn;
    void* user;
    u32 id;
} Mel_Sim_Update;

struct Mel_Sim_Fixed {
    f32 fixed_dt;
    f32 accumulator;
    f32 alpha;
    u64 tick;
    Mel_Sim_Update* updates;
    u32 update_count;
    u32 update_capacity;
    u32 next_id;
    const Mel_Alloc* alloc;
};

struct Mel_Sim_Ctx {
    Mel_Rng rng;
    u64 tick;
    Mel_Block_Alloc events;
    void* user;
    f32 time_scale;
    const Mel_Alloc* alloc;
    Mel_Sim_Fixed** fixed_contexts;
    u32 fixed_count;
    u32 fixed_capacity;
    Mel_Sim_Update* variable_updates;
    u32 variable_count;
    u32 variable_capacity;
    u32 next_update_id;
};

typedef struct {
    Mel_Block_Iter block_iter;
} Mel_Sim_Iter;

typedef struct {
    i32 type;
    void* data;
    size size;
} Mel_Sim_Event;

typedef struct {
    u64 seed;
    void* event_buffer;
    size event_buffer_size;
    void* user;
    f32 time_scale;
    const Mel_Alloc* alloc;
} Mel_Sim_Opt;

void  mel_sim_init_opt(Mel_Sim_Ctx* ctx, Mel_Sim_Opt opt);
#define mel_sim_init(ctx, ...) mel_sim_init_opt((ctx), (Mel_Sim_Opt){__VA_ARGS__})

void  mel_sim_shutdown(Mel_Sim_Ctx* ctx);

void  mel_sim_push(Mel_Sim_Ctx* ctx, i32 type, const void* data, size len);
void* mel_sim_next(Mel_Sim_Ctx* ctx, i32 type, Mel_Sim_Iter* iter);
bool  mel_sim_next_any(Mel_Sim_Ctx* ctx, Mel_Sim_Iter* iter, Mel_Sim_Event* event);
void  mel_sim_clear(Mel_Sim_Ctx* ctx);

void  mel_sim_tick(Mel_Sim_Ctx* ctx, f32 frame_dt);

typedef struct {
    f32 fixed_dt;
} Mel_Sim_Add_Fixed_Opt;

Mel_Sim_Fixed* mel_sim_add_fixed_opt(Mel_Sim_Ctx* sim, Mel_Sim_Add_Fixed_Opt opt);
#define mel_sim_add_fixed(sim, ...) mel_sim_add_fixed_opt((sim), (Mel_Sim_Add_Fixed_Opt){__VA_ARGS__})

void mel_sim_remove_fixed(Mel_Sim_Ctx* sim, Mel_Sim_Fixed* fixed);

typedef struct {
    void* user;
} Mel_Fixed_Add_Update_Opt;

Mel_Update_Handle mel_sim_fixed_add_update_opt(Mel_Sim_Fixed* fixed, Mel_Update_Fn fn, Mel_Fixed_Add_Update_Opt opt);
#define mel_sim_fixed_add_update(fixed, fn, ...) mel_sim_fixed_add_update_opt((fixed), (fn), (Mel_Fixed_Add_Update_Opt){__VA_ARGS__})

void mel_sim_fixed_remove_update(Mel_Sim_Fixed* fixed, Mel_Update_Handle handle);

f32 mel_sim_fixed_alpha(Mel_Sim_Fixed* fixed);

typedef struct {
    void* user;
} Mel_Sim_Add_Variable_Opt;

Mel_Update_Handle mel_sim_add_variable_opt(Mel_Sim_Ctx* sim, Mel_Update_Fn fn, Mel_Sim_Add_Variable_Opt opt);
#define mel_sim_add_variable(sim, fn, ...) mel_sim_add_variable_opt((sim), (fn), (Mel_Sim_Add_Variable_Opt){__VA_ARGS__})

void mel_sim_remove_variable(Mel_Sim_Ctx* sim, Mel_Update_Handle handle);
