#include "sim.ctx.h"
#include "allocator.h"
#include "allocator.heap.h"
#include <string.h>

void mel_sim_init_opt(Mel_Sim_Ctx* ctx, Mel_Sim_Opt opt)
{
    assert(opt.event_buffer != NULL);
    assert(opt.event_buffer_size > 0);
    *ctx = (Mel_Sim_Ctx){0};
    ctx->rng = mel_rng(opt.seed);
    ctx->user = opt.user;
    ctx->time_scale = opt.time_scale != 0 ? opt.time_scale : 1.0f;
    ctx->alloc = opt.alloc ? opt.alloc : mel_alloc_heap();
    ctx->next_update_id = 1;
    mel_block_init(&ctx->events, opt.event_buffer, (usize)opt.event_buffer_size);
}

void mel_sim_shutdown(Mel_Sim_Ctx* ctx)
{
    assert(ctx != NULL);

    for (u32 i = 0; i < ctx->fixed_count; i++)
    {
        Mel_Sim_Fixed* fixed = ctx->fixed_contexts[i];
        if (fixed->updates)
            mel_dealloc(fixed->alloc, fixed->updates);
        mel_dealloc(ctx->alloc, fixed);
    }

    if (ctx->fixed_contexts)
        mel_dealloc(ctx->alloc, ctx->fixed_contexts);
    if (ctx->variable_updates)
        mel_dealloc(ctx->alloc, ctx->variable_updates);

    *ctx = (Mel_Sim_Ctx){0};
}

void mel_sim_push(Mel_Sim_Ctx* ctx, i32 type, const void* data, size len)
{
    assert(ctx != NULL);
    assert(len >= 0);
    assert(len == 0 || data != NULL);

    usize total = sizeof(Mel_Sim_Event_Header) + (usize)len;
    void* ptr = mel_block_push(&ctx->events, total, _Alignof(max_align_t));

    Mel_Sim_Event_Header* header = (Mel_Sim_Event_Header*)ptr;
    header->type = type;
    header->size = (u32)len;

    if (len > 0)
        memcpy((u8*)ptr + sizeof(Mel_Sim_Event_Header), data, (usize)len);
}

void* mel_sim_next(Mel_Sim_Ctx* ctx, i32 type, Mel_Sim_Iter* iter)
{
    assert(ctx != NULL);

    if (iter->block_iter.alloc == NULL)
        iter->block_iter = mel_block_iter_begin(&ctx->events);

    usize entry_size;
    void* entry;
    while ((entry = mel_block_iter_next(&iter->block_iter, &entry_size)) != NULL)
    {
        Mel_Sim_Event_Header* header = (Mel_Sim_Event_Header*)entry;
        if (header->type == type)
            return (u8*)entry + sizeof(Mel_Sim_Event_Header);
    }
    return NULL;
}

bool mel_sim_next_any(Mel_Sim_Ctx* ctx, Mel_Sim_Iter* iter, Mel_Sim_Event* event)
{
    assert(ctx != NULL);

    if (iter->block_iter.alloc == NULL)
        iter->block_iter = mel_block_iter_begin(&ctx->events);

    usize entry_size;
    void* entry = mel_block_iter_next(&iter->block_iter, &entry_size);
    if (entry == NULL)
        return false;

    Mel_Sim_Event_Header* header = (Mel_Sim_Event_Header*)entry;
    event->type = header->type;
    event->size = (size)header->size;
    event->data = header->size > 0 ? (u8*)entry + sizeof(Mel_Sim_Event_Header) : NULL;
    return true;
}

void mel_sim_clear(Mel_Sim_Ctx* ctx)
{
    assert(ctx != NULL);
    mel_block_reset(&ctx->events);
    ctx->tick++;
}

static void grow_fixed(Mel_Sim_Ctx* ctx)
{
    u32 new_cap = ctx->fixed_capacity == 0 ? 4 : ctx->fixed_capacity * 2;

    if (ctx->fixed_contexts == NULL)
        ctx->fixed_contexts = mel_alloc(ctx->alloc, sizeof(Mel_Sim_Fixed*) * new_cap);
    else
        ctx->fixed_contexts = mel_realloc(ctx->alloc, ctx->fixed_contexts, sizeof(Mel_Sim_Fixed*) * new_cap);

    ctx->fixed_capacity = new_cap;
}

Mel_Sim_Fixed* mel_sim_add_fixed_opt(Mel_Sim_Ctx* sim, Mel_Sim_Add_Fixed_Opt opt)
{
    assert(sim != NULL);
    assert(opt.fixed_dt > 0);

    if (sim->fixed_count >= sim->fixed_capacity)
        grow_fixed(sim);

    Mel_Sim_Fixed* fixed = mel_alloc_type(sim->alloc, Mel_Sim_Fixed);
    *fixed = (Mel_Sim_Fixed){0};
    fixed->fixed_dt = opt.fixed_dt;
    fixed->alloc = sim->alloc;
    fixed->next_id = 1;

    sim->fixed_contexts[sim->fixed_count++] = fixed;
    return fixed;
}

void mel_sim_remove_fixed(Mel_Sim_Ctx* sim, Mel_Sim_Fixed* fixed)
{
    assert(sim != NULL);
    assert(fixed != NULL);

    for (u32 i = 0; i < sim->fixed_count; i++)
    {
        if (sim->fixed_contexts[i] == fixed)
        {
            if (fixed->updates)
                mel_dealloc(fixed->alloc, fixed->updates);
            mel_dealloc(sim->alloc, fixed);

            sim->fixed_contexts[i] = sim->fixed_contexts[--sim->fixed_count];
            return;
        }
    }

    assert(false);
}

static void grow_updates(Mel_Sim_Update** updates, u32* capacity, const Mel_Alloc* alloc)
{
    u32 new_cap = *capacity == 0 ? 4 : *capacity * 2;

    if (*updates == NULL)
        *updates = mel_alloc(alloc, sizeof(Mel_Sim_Update) * new_cap);
    else
        *updates = mel_realloc(alloc, *updates, sizeof(Mel_Sim_Update) * new_cap);

    *capacity = new_cap;
}

Mel_Update_Handle mel_sim_fixed_add_update_opt(Mel_Sim_Fixed* fixed, Mel_Update_Fn fn, Mel_Fixed_Add_Update_Opt opt)
{
    assert(fixed != NULL);
    assert(fn != NULL);

    if (fixed->update_count >= fixed->update_capacity)
        grow_updates(&fixed->updates, &fixed->update_capacity, fixed->alloc);

    u32 id = fixed->next_id++;
    fixed->updates[fixed->update_count++] = (Mel_Sim_Update){
        .fn = fn,
        .user = opt.user,
        .id = id,
    };

    return (Mel_Update_Handle){ .id = id };
}

void mel_sim_fixed_remove_update(Mel_Sim_Fixed* fixed, Mel_Update_Handle handle)
{
    assert(fixed != NULL);
    assert(handle.id != 0);

    for (u32 i = 0; i < fixed->update_count; i++)
    {
        if (fixed->updates[i].id == handle.id)
        {
            memmove(&fixed->updates[i], &fixed->updates[i + 1],
                sizeof(Mel_Sim_Update) * (fixed->update_count - i - 1));
            fixed->update_count--;
            return;
        }
    }

    assert(false);
}

f32 mel_sim_fixed_alpha(Mel_Sim_Fixed* fixed)
{
    assert(fixed != NULL);
    return fixed->alpha;
}

Mel_Update_Handle mel_sim_add_variable_opt(Mel_Sim_Ctx* sim, Mel_Update_Fn fn, Mel_Sim_Add_Variable_Opt opt)
{
    assert(sim != NULL);
    assert(fn != NULL);

    if (sim->variable_count >= sim->variable_capacity)
        grow_updates(&sim->variable_updates, &sim->variable_capacity, sim->alloc);

    u32 id = sim->next_update_id++;
    sim->variable_updates[sim->variable_count++] = (Mel_Sim_Update){
        .fn = fn,
        .user = opt.user,
        .id = id,
    };

    return (Mel_Update_Handle){ .id = id };
}

void mel_sim_remove_variable(Mel_Sim_Ctx* sim, Mel_Update_Handle handle)
{
    assert(sim != NULL);
    assert(handle.id != 0);

    for (u32 i = 0; i < sim->variable_count; i++)
    {
        if (sim->variable_updates[i].id == handle.id)
        {
            memmove(&sim->variable_updates[i], &sim->variable_updates[i + 1],
                sizeof(Mel_Sim_Update) * (sim->variable_count - i - 1));
            sim->variable_count--;
            return;
        }
    }

    assert(false);
}

void mel_sim_tick(Mel_Sim_Ctx* ctx, f32 frame_dt)
{
    assert(ctx != NULL);

    f32 scaled_dt = frame_dt * ctx->time_scale;

    for (u32 i = 0; i < ctx->fixed_count; i++)
    {
        Mel_Sim_Fixed* fixed = ctx->fixed_contexts[i];
        fixed->accumulator += scaled_dt;

        while (fixed->accumulator >= fixed->fixed_dt)
        {
            for (u32 j = 0; j < fixed->update_count; j++)
                fixed->updates[j].fn(ctx, fixed->fixed_dt, fixed->updates[j].user);

            fixed->accumulator -= fixed->fixed_dt;
            fixed->tick++;
        }

        fixed->alpha = fixed->fixed_dt > 0 ? fixed->accumulator / fixed->fixed_dt : 1.0f;
    }

    for (u32 i = 0; i < ctx->variable_count; i++)
        ctx->variable_updates[i].fn(ctx, scaled_dt, ctx->variable_updates[i].user);

    mel_sim_clear(ctx);
}
