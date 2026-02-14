#include "event.channel.h"
#include "allocator.h"
#include "collection.array.h"

void mel_event_channel_init(Mel_Event_Channel* ch, const Mel_Alloc* alloc)
{
    assert(ch != NULL);
    assert(alloc != NULL);
    mel_array_init(&ch->subs, alloc);
    ch->next_id = 1;
    ch->firing = false;
}

void mel_event_channel_destroy(Mel_Event_Channel* ch)
{
    assert(ch != NULL);
    assert(!ch->firing);
    mel_array_free(&ch->subs);
    ch->next_id = 0;
    ch->firing = false;
}

Mel_Event_Sub mel_event_channel_on(Mel_Event_Channel* ch, Mel_Event_Fn fn, void* ctx)
{
    assert(ch != NULL);
    assert(fn != NULL);
    assert(!ch->firing);

    u32 id = ch->next_id++;
    mel_array_push(&ch->subs, ((Mel_Event_Channel_Entry){ .fn = fn, .ctx = ctx, .id = id }));
    return (Mel_Event_Sub){ .id = id };
}

void mel_event_channel_off(Mel_Event_Channel* ch, Mel_Event_Sub sub)
{
    assert(ch != NULL);
    assert(!ch->firing);

    for (usize i = 0; i < ch->subs.count; i++) {
        if (ch->subs.items[i].id == sub.id) {
            mel_array_remove_ordered(&ch->subs, i);
            return;
        }
    }
    assert(false);
}

void mel_event_channel_fire(Mel_Event_Channel* ch, const void* event)
{
    assert(ch != NULL);
    assert(!ch->firing);

    ch->firing = true;
    for (usize i = 0; i < ch->subs.count; i++) {
        ch->subs.items[i].fn(ch->subs.items[i].ctx, event);
    }
    ch->firing = false;
}
