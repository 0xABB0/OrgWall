#include "event.channel.h"
#include "allocator.h"
#include <string.h>

typedef struct {
    u32 count;
    Mel_Event_Channel_Entry entries[];
} Mel__Subs_Block;

static Mel__Subs_Block* mel__subs_block_create(const Mel_Alloc* alloc, u32 count)
{
    Mel__Subs_Block* b = mel_alloc(alloc, sizeof(Mel__Subs_Block) + count * sizeof(Mel_Event_Channel_Entry));
    b->count = count;
    return b;
}

void mel_event_channel_init(Mel_Event_Channel* ch, const Mel_Alloc* alloc)
{
    assert(ch != NULL);
    assert(alloc != NULL);
    mel_rcu_init(&ch->rcu, alloc);
    ch->next_id = 1;
}

void mel_event_channel_destroy(Mel_Event_Channel* ch)
{
    assert(ch != NULL);
    mel_rcu_destroy(&ch->rcu);
    ch->next_id = 0;
}

Mel_Event_Sub mel_event_channel_on(Mel_Event_Channel* ch, Mel_Event_Fn fn, void* ctx)
{
    assert(ch != NULL);
    assert(fn != NULL);

    mel_rcu_writer_lock(&ch->rcu);

    Mel__Subs_Block* old = mel_rcu_writer_load(&ch->rcu);
    u32 old_count = old ? old->count : 0;
    u32 new_count = old_count + 1;

    Mel__Subs_Block* block = mel__subs_block_create(ch->rcu.alloc, new_count);
    if (old_count > 0)
        memcpy(block->entries, old->entries, old_count * sizeof(Mel_Event_Channel_Entry));

    u32 id = ch->next_id++;
    block->entries[old_count] = (Mel_Event_Channel_Entry){ .fn = fn, .ctx = ctx, .id = id };

    mel_rcu_writer_store(&ch->rcu, block);
    mel_rcu_writer_unlock(&ch->rcu);

    return (Mel_Event_Sub){ .id = id };
}

void mel_event_channel_off(Mel_Event_Channel* ch, Mel_Event_Sub sub)
{
    assert(ch != NULL);

    mel_rcu_writer_lock(&ch->rcu);

    Mel__Subs_Block* old = mel_rcu_writer_load(&ch->rcu);
    assert(old != NULL);

    i32 found = -1;
    for (u32 i = 0; i < old->count; i++) {
        if (old->entries[i].id == sub.id) {
            found = (i32)i;
            break;
        }
    }
    assert(found >= 0);

    u32 new_count = old->count - 1;
    Mel__Subs_Block* block = NULL;

    if (new_count > 0) {
        block = mel__subs_block_create(ch->rcu.alloc, new_count);
        if (found > 0)
            memcpy(block->entries, old->entries, (u32)found * sizeof(Mel_Event_Channel_Entry));
        if ((u32)found < old->count - 1)
            memcpy(&block->entries[found], &old->entries[found + 1],
                   (old->count - (u32)found - 1) * sizeof(Mel_Event_Channel_Entry));
    }

    mel_rcu_writer_store(&ch->rcu, block);
    mel_rcu_writer_unlock(&ch->rcu);
}

void mel_event_channel_fire(Mel_Event_Channel* ch, const void* event)
{
    assert(ch != NULL);

    Mel_Rcu_Token token;
    Mel__Subs_Block* snap = mel_rcu_read(&ch->rcu, &token);

    if (snap) {
        for (u32 i = 0; i < snap->count; i++)
            snap->entries[i].fn(snap->entries[i].ctx, event);
    }

    mel_rcu_read_end(&ch->rcu, token);
}

u32 mel_event_channel_count(Mel_Event_Channel* ch)
{
    assert(ch != NULL);

    Mel_Rcu_Token token;
    Mel__Subs_Block* snap = mel_rcu_read(&ch->rcu, &token);
    u32 count = snap ? snap->count : 0;
    mel_rcu_read_end(&ch->rcu, token);

    return count;
}
