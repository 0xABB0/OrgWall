#include "channel_internal.h"

#include <signal/signal.h>

#include <assert.h>
#include <stdint.h>

typedef struct
{
    Mel_Channel__Wait wait;
    Mel_Counter       gate;
} Mel_Channel__Fiber_Wait;

static void mel_channel__fiber_wake(void* user)
{
    Mel_Channel__Fiber_Wait* fw = (Mel_Channel__Fiber_Wait*)user;
    mel_counter_decrement(&fw->gate);
}

static void mel_channel__fiber_wait_init(Mel_Channel__Fiber_Wait* fw)
{
    fw->wait.winner = NULL;
    atomic_store_explicit(&fw->wait.state, MEL_CHANNEL__WAITER_PENDING, memory_order_relaxed);
    Mel_Counter init = MEL_COUNTER_INIT;
    fw->gate = init;
    mel_counter_increment(&fw->gate);
}

static Mel_Channel_Status mel_channel__block(Mel_Channel* ch, void* slot, bool is_send)
{
    Mel_Channel__Fiber_Wait fw;
    mel_channel__fiber_wait_init(&fw);

    Mel_Channel_Op op;
    op.channel = ch;
    op.slot = slot;
    op.is_send = is_send ? 1 : 0;
    mel_channel__op_bind(&op, &fw.wait, mel_channel__fiber_wake, &fw);

    mel_channel__lock(ch);
    Mel_Channel_Status s = is_send ? mel_channel__send_locked(ch, slot, &op) : mel_channel__recv_locked(ch, slot, &op);
    mel_channel__unlock(ch);

    if (s != MEL_CHANNEL_WOULD_BLOCK)
        return s;

    mel_counter_wait(&fw.gate);

    i32 final = atomic_load_explicit(&fw.wait.state, memory_order_acquire);
    if (final == MEL_CHANNEL__WAITER_CLOSED)
        return is_send ? (MEL_CHANNEL_ERROR | MEL_CHANNEL_CLOSED) : MEL_CHANNEL_CLOSED;
    return MEL_CHANNEL_OK;
}

Mel_Channel_Status mel_channel_send(Mel_Channel* ch, const void* item)
{
    assert(ch != NULL);
    assert(item != NULL);
    return mel_channel__block(ch, (void*)item, true);
}

Mel_Channel_Status mel_channel_recv(Mel_Channel* ch, void* out)
{
    assert(ch != NULL);
    assert(out != NULL);
    return mel_channel__block(ch, out, false);
}

static void mel_channel__sel_lock_all(Mel_Channel_Sel* sel)
{
    uintptr_t prev = 0;
    bool      have_prev = false;
    for (;;)
    {
        Mel_Channel* next = NULL;
        for (usize i = 0; i < sel->n; i++)
        {
            uintptr_t addr = (uintptr_t)sel->ops[i].channel;
            if (have_prev && addr <= prev)
                continue;
            if (next == NULL || addr < (uintptr_t)next)
                next = sel->ops[i].channel;
        }
        if (next == NULL)
            return;
        mel_channel__lock(next);
        prev = (uintptr_t)next;
        have_prev = true;
    }
}

static void mel_channel__sel_unlock_all(Mel_Channel_Sel* sel)
{
    uintptr_t prev = UINTPTR_MAX;
    bool      have_prev = false;
    for (;;)
    {
        Mel_Channel* next = NULL;
        for (usize i = 0; i < sel->n; i++)
        {
            uintptr_t addr = (uintptr_t)sel->ops[i].channel;
            if (have_prev && addr >= prev)
                continue;
            if (next == NULL || addr > (uintptr_t)next)
                next = sel->ops[i].channel;
        }
        if (next == NULL)
            return;
        mel_channel__unlock(next);
        prev = (uintptr_t)next;
        have_prev = true;
    }
}

Mel_Channel_Op* mel_channel_sel_wait(Mel_Channel_Sel* sel)
{
    assert(sel != NULL);
    assert(sel->n > 0);

    Mel_Channel__Fiber_Wait fw;
    mel_channel__fiber_wait_init(&fw);

    for (usize i = 0; i < sel->n; i++)
        mel_channel__op_bind(&sel->ops[i], &fw.wait, mel_channel__fiber_wake, &fw);

    mel_channel__sel_lock_all(sel);

    Mel_Channel_Op* immediate = NULL;
    for (usize i = 0; i < sel->n; i++)
    {
        Mel_Channel_Op*    op = &sel->ops[i];
        Mel_Channel*       ch = op->channel;
        Mel_Channel_Status s = op->is_send ? mel_channel__send_locked(ch, op->slot, NULL) : mel_channel__recv_locked(ch, op->slot, NULL);
        if (s != MEL_CHANNEL_WOULD_BLOCK)
        {
            atomic_store_explicit(&fw.wait.state, MEL_CHANNEL__WAITER_COMMITTED, memory_order_relaxed);
            immediate = op;
            break;
        }
    }

    if (immediate == NULL)
    {
        for (usize i = 0; i < sel->n; i++)
            mel_channel__park_held(sel->ops[i].channel, &sel->ops[i]);
    }

    mel_channel__sel_unlock_all(sel);

    if (immediate != NULL)
        return immediate;

    mel_counter_wait(&fw.gate);

    for (usize i = 0; i < sel->n; i++)
    {
        Mel_Channel_Op* op = &sel->ops[i];
        if (atomic_load_explicit(&op->owner_channel, memory_order_relaxed) != NULL)
            mel_channel__retract(op->channel, op);
    }

    i32 final = atomic_load_explicit(&fw.wait.state, memory_order_acquire);
    if (final == MEL_CHANNEL__WAITER_CLOSED)
        return NULL;
    return fw.wait.winner;
}
