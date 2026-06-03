#pragma once

#include <channel/channel.h>

#include <thread/mutex.h>

#include <stdatomic.h>

struct Mel_Channel
{
    Mel_Mutex lock;

    usize item_size;
    usize capacity;

    byte* ring;
    usize head;
    usize tail;
    usize count;

    Mel_ListNode senders;
    Mel_ListNode receivers;

    bool closed;

    const Mel_Alloc* alloc;
};

typedef struct
{
    _Atomic(i32)    state;
    Mel_Channel_Op* winner;
} Mel_Channel__Wait;

bool mel_channel__claim(Mel_Channel_Op* op, i32 outcome);
void mel_channel__retract(Mel_Channel* ch, Mel_Channel_Op* op);
void mel_channel__park_held(Mel_Channel* ch, Mel_Channel_Op* op);

Mel_Channel_Status mel_channel__send_locked(Mel_Channel* ch, const void* item, Mel_Channel_Op* self_or_null);
Mel_Channel_Status mel_channel__recv_locked(Mel_Channel* ch, void* out, Mel_Channel_Op* self_or_null);

static inline void mel_channel__op_bind(Mel_Channel_Op* op, Mel_Channel__Wait* w, void (*wake)(void*), void* user)
{
    op->group_state = &w->state;
    op->group_winner = &w->winner;
    op->waker.wake = wake;
    op->waker.user = user;
    atomic_store_explicit(&op->owner_channel, NULL, memory_order_relaxed);
}

static inline void mel_channel__lock(Mel_Channel* ch) { mel_mutex_lock(&ch->lock); }
static inline void mel_channel__unlock(Mel_Channel* ch) { mel_mutex_unlock(&ch->lock); }
