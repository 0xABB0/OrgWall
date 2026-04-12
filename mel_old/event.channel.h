#pragma once

#include "event.channel.fwd.h"
#include "allocator.fwd.h"
#include "collection.rcu.h"

typedef struct {
    Mel_Event_Fn fn;
    void* ctx;
    u32 id;
} Mel_Event_Channel_Entry;

struct Mel_Event_Channel {
    Mel_Rcu rcu;
    u32 next_id;
};

void          mel_event_channel_init(Mel_Event_Channel* ch, const Mel_Alloc* alloc);
void          mel_event_channel_destroy(Mel_Event_Channel* ch);
Mel_Event_Sub mel_event_channel_on(Mel_Event_Channel* ch, Mel_Event_Fn fn, void* ctx);
void          mel_event_channel_off(Mel_Event_Channel* ch, Mel_Event_Sub sub);
void          mel_event_channel_fire(Mel_Event_Channel* ch, const void* event);
u32           mel_event_channel_count(Mel_Event_Channel* ch);
