#pragma once

#include "core.types.h"
#include "math.rng.h"
#include "allocator.block.h"

typedef struct {
    i32 type;
    u32 size;
} Mel_Sim_Event_Header;

typedef struct Mel_Sim_Ctx {
    Mel_Rng rng;
    u64 tick;
    Mel_Block_Alloc events;
} Mel_Sim_Ctx;

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
} Mel_Sim_Opt;

void  mel_sim_init_opt(Mel_Sim_Ctx* ctx, Mel_Sim_Opt opt);
#define mel_sim_init(ctx, ...) mel_sim_init_opt((ctx), (Mel_Sim_Opt){__VA_ARGS__})

void  mel_sim_push(Mel_Sim_Ctx* ctx, i32 type, const void* data, size len);
void* mel_sim_next(Mel_Sim_Ctx* ctx, i32 type, Mel_Sim_Iter* iter);
bool  mel_sim_next_any(Mel_Sim_Ctx* ctx, Mel_Sim_Iter* iter, Mel_Sim_Event* event);
void  mel_sim_clear(Mel_Sim_Ctx* ctx);
