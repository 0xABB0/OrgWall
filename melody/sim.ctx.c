#include "sim.ctx.h"
#include <string.h>

void mel_sim_init_opt(Mel_Sim_Ctx* ctx, Mel_Sim_Opt opt)
{
    assert(opt.event_buffer != NULL);
    assert(opt.event_buffer_size > 0);
    *ctx = (Mel_Sim_Ctx){0};
    ctx->rng = mel_rng(opt.seed);
    mel_block_init(&ctx->events, opt.event_buffer, (usize)opt.event_buffer_size);
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
