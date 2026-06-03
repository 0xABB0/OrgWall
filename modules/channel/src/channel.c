#include "channel_internal.h"

#include <allocator/allocator.h>

#include <assert.h>
#include <string.h>

static byte* mel_channel__slot(Mel_Channel* ch, usize index) { return ch->ring + index * ch->item_size; }

Mel_Channel* mel_channel_create(usize item_size, usize capacity, const Mel_Alloc* alloc)
{
    assert(item_size > 0);
    assert(alloc != NULL);

    Mel_Channel* ch = mel_alloc_type(alloc, Mel_Channel);
    assert(ch != NULL);

    bool ok = mel_mutex_init(&ch->lock, MEL_MUTEX_PLAIN);
    assert(ok);
    MEL_UNUSED(ok);

    ch->item_size = item_size;
    ch->capacity = capacity;
    ch->head = 0;
    ch->tail = 0;
    ch->count = 0;
    ch->closed = false;
    ch->alloc = alloc;

    if (capacity > 0)
    {
        ch->ring = mel_alloc(alloc, item_size * capacity);
        assert(ch->ring != NULL);
    }
    else
    {
        ch->ring = NULL;
    }

    mel_list_init(&ch->senders);
    mel_list_init(&ch->receivers);

    return ch;
}

void mel_channel_destroy(Mel_Channel* ch)
{
    assert(ch != NULL);
    assert(mel_list_empty(&ch->senders));
    assert(mel_list_empty(&ch->receivers));

    const Mel_Alloc* alloc = ch->alloc;
    if (ch->ring != NULL)
        mel_dealloc(alloc, ch->ring);
    mel_mutex_destroy(&ch->lock);
    mel_dealloc(alloc, ch);
}

usize mel_channel_item_size(const Mel_Channel* ch)
{
    assert(ch != NULL);
    return ch->item_size;
}

usize mel_channel_capacity(const Mel_Channel* ch)
{
    assert(ch != NULL);
    return ch->capacity;
}

bool mel_channel_is_closed(const Mel_Channel* ch)
{
    Mel_Channel* m = (Mel_Channel*)ch;
    assert(m != NULL);
    mel_mutex_lock(&m->lock);
    bool c = m->closed;
    mel_mutex_unlock(&m->lock);
    return c;
}

bool mel_channel__claim(Mel_Channel_Op* op, i32 outcome)
{
    i32 expected = MEL_CHANNEL__WAITER_PENDING;
    return atomic_compare_exchange_strong_explicit(op->group_state, &expected, outcome, memory_order_acq_rel, memory_order_acquire);
}

static Mel_Channel_Op* mel_channel__pop_claim(Mel_ListNode* queue)
{
    while (!mel_list_empty(queue))
    {
        Mel_ListNode*   node = mel_list_front(queue);
        Mel_Channel_Op* op = mel_container_of(node, Mel_Channel_Op, link);
        mel_list_remove(node);
        atomic_store_explicit(&op->owner_channel, NULL, memory_order_relaxed);
        if (mel_channel__claim(op, MEL_CHANNEL__WAITER_COMMITTED))
            return op;
    }
    return NULL;
}

static void mel_channel__park(Mel_Channel* ch, Mel_ListNode* queue, Mel_Channel_Op* op)
{
    atomic_store_explicit(&op->owner_channel, ch, memory_order_relaxed);
    mel_list_push_back(queue, &op->link);
}

void mel_channel__park_held(Mel_Channel* ch, Mel_Channel_Op* op)
{
    Mel_ListNode* queue = op->is_send ? &ch->senders : &ch->receivers;
    mel_channel__park(ch, queue, op);
}

void mel_channel__retract(Mel_Channel* ch, Mel_Channel_Op* op)
{
    mel_mutex_lock(&ch->lock);
    if (atomic_load_explicit(&op->owner_channel, memory_order_relaxed) == ch)
    {
        mel_list_remove(&op->link);
        atomic_store_explicit(&op->owner_channel, NULL, memory_order_relaxed);
    }
    mel_mutex_unlock(&ch->lock);
}

Mel_Channel_Status mel_channel__send_locked(Mel_Channel* ch, const void* item, Mel_Channel_Op* self_or_null)
{
    if (ch->closed)
        return MEL_CHANNEL_ERROR | MEL_CHANNEL_CLOSED;

    Mel_Channel_Op* recv = mel_channel__pop_claim(&ch->receivers);
    if (recv != NULL)
    {
        memcpy(recv->slot, item, ch->item_size);
        *recv->group_winner = recv;
        recv->waker.wake(recv->waker.user);
        return MEL_CHANNEL_OK;
    }

    if (ch->count < ch->capacity)
    {
        memcpy(mel_channel__slot(ch, ch->tail), item, ch->item_size);
        ch->tail = (ch->tail + 1) % ch->capacity;
        ch->count++;
        return MEL_CHANNEL_OK;
    }

    if (self_or_null == NULL)
        return MEL_CHANNEL_WOULD_BLOCK;

    self_or_null->slot = (void*)item;
    self_or_null->is_send = 1;
    mel_channel__park(ch, &ch->senders, self_or_null);
    return MEL_CHANNEL_WOULD_BLOCK;
}

Mel_Channel_Status mel_channel__recv_locked(Mel_Channel* ch, void* out, Mel_Channel_Op* self_or_null)
{
    if (ch->count > 0)
    {
        memcpy(out, mel_channel__slot(ch, ch->head), ch->item_size);
        ch->head = (ch->head + 1) % ch->capacity;
        ch->count--;

        Mel_Channel_Op* snd = mel_channel__pop_claim(&ch->senders);
        if (snd != NULL)
        {
            memcpy(mel_channel__slot(ch, ch->tail), snd->slot, ch->item_size);
            ch->tail = (ch->tail + 1) % ch->capacity;
            ch->count++;
            *snd->group_winner = snd;
            snd->waker.wake(snd->waker.user);
        }
        return MEL_CHANNEL_OK;
    }

    Mel_Channel_Op* snd = mel_channel__pop_claim(&ch->senders);
    if (snd != NULL)
    {
        memcpy(out, snd->slot, ch->item_size);
        *snd->group_winner = snd;
        snd->waker.wake(snd->waker.user);
        return MEL_CHANNEL_OK;
    }

    if (ch->closed)
        return MEL_CHANNEL_CLOSED;

    if (self_or_null == NULL)
        return MEL_CHANNEL_WOULD_BLOCK;

    self_or_null->slot = out;
    self_or_null->is_send = 0;
    mel_channel__park(ch, &ch->receivers, self_or_null);
    return MEL_CHANNEL_WOULD_BLOCK;
}

void mel_channel_close(Mel_Channel* ch)
{
    assert(ch != NULL);

    mel_mutex_lock(&ch->lock);

    if (ch->closed)
    {
        mel_mutex_unlock(&ch->lock);
        return;
    }
    ch->closed = true;

    Mel_ListNode woken_recv;
    Mel_ListNode woken_send;
    mel_list_init(&woken_recv);
    mel_list_init(&woken_send);
    mel_list_splice(&woken_recv, &ch->receivers);
    mel_list_splice(&woken_send, &ch->senders);

    mel_mutex_unlock(&ch->lock);

    mel_list_foreach_safe(node, tmp, &woken_recv)
    {
        Mel_Channel_Op* op = mel_container_of(node, Mel_Channel_Op, link);
        atomic_store_explicit(&op->owner_channel, NULL, memory_order_relaxed);
        if (mel_channel__claim(op, MEL_CHANNEL__WAITER_CLOSED))
            op->waker.wake(op->waker.user);
    }
    mel_list_foreach_safe(node, tmp, &woken_send)
    {
        Mel_Channel_Op* op = mel_container_of(node, Mel_Channel_Op, link);
        atomic_store_explicit(&op->owner_channel, NULL, memory_order_relaxed);
        if (mel_channel__claim(op, MEL_CHANNEL__WAITER_CLOSED))
            op->waker.wake(op->waker.user);
    }
}

Mel_Channel_Status mel_channel_try_send(Mel_Channel* ch, const void* item)
{
    assert(ch != NULL);
    assert(item != NULL);
    mel_mutex_lock(&ch->lock);
    Mel_Channel_Status s = mel_channel__send_locked(ch, item, NULL);
    mel_mutex_unlock(&ch->lock);
    return s;
}

Mel_Channel_Status mel_channel_try_recv(Mel_Channel* ch, void* out)
{
    assert(ch != NULL);
    assert(out != NULL);
    mel_mutex_lock(&ch->lock);
    Mel_Channel_Status s = mel_channel__recv_locked(ch, out, NULL);
    mel_mutex_unlock(&ch->lock);
    return s;
}

void mel_channel_sel_init(Mel_Channel_Sel* sel, Mel_Channel_Op* ops, usize n)
{
    assert(sel != NULL);
    assert(n == 0 || ops != NULL);
    sel->ops = ops;
    sel->n = n;
}

Mel_Channel_Op* mel_channel_sel_try(Mel_Channel_Sel* sel)
{
    assert(sel != NULL);
    for (usize i = 0; i < sel->n; i++)
    {
        Mel_Channel_Op* op = &sel->ops[i];
        Mel_Channel*    ch = op->channel;
        mel_mutex_lock(&ch->lock);
        Mel_Channel_Status s = op->is_send ? mel_channel__send_locked(ch, op->slot, NULL) : mel_channel__recv_locked(ch, op->slot, NULL);
        mel_mutex_unlock(&ch->lock);
        if (s != MEL_CHANNEL_WOULD_BLOCK)
            return op;
    }
    return NULL;
}

typedef struct
{
    Mel_Channel__Wait wait;
    Mel_Task          task;
    Mel_Channel_Op    op;
    Mel_Future*       future;
    Mel_Executor*     exec;
    const Mel_Alloc*  alloc;
} Mel_Channel__Future_Wait;

static void mel_channel__future_settle(Mel_Channel__Future_Wait* fw)
{
    i32               final = atomic_load_explicit(&fw->wait.state, memory_order_acquire);
    Mel_Future_Status s;
    if (final == MEL_CHANNEL__WAITER_CLOSED)
        s = MEL_FUTURE_ERROR | MEL_FUTURE_BROKEN;
    else
        s = MEL_FUTURE_OK;

    const Mel_Alloc* alloc = fw->alloc;
    Mel_Future*      future = fw->future;
    mel_dealloc(alloc, fw);
    mel_future_resolve(future, NULL, s);
}

static void mel_channel__future_task(Mel_Task* self)
{
    Mel_Channel__Future_Wait* fw = mel_container_of(self, Mel_Channel__Future_Wait, task);
    mel_channel__future_settle(fw);
}

static void mel_channel__future_wake(void* user)
{
    Mel_Channel__Future_Wait* fw = (Mel_Channel__Future_Wait*)user;
    fw->exec->submit(fw->exec, &fw->task);
}

static void mel_channel__future_op(Mel_Channel* ch, void* slot, bool is_send, Mel_Future* out_future, Mel_Executor* exec, const Mel_Alloc* alloc)
{
    assert(ch != NULL);
    assert(out_future != NULL);
    assert(exec != NULL);
    assert(alloc != NULL);

    mel_future_init(out_future, NULL, alloc);

    Mel_Channel__Future_Wait* fw = mel_alloc_type(alloc, Mel_Channel__Future_Wait);
    assert(fw != NULL);
    fw->wait.winner = NULL;
    atomic_store_explicit(&fw->wait.state, MEL_CHANNEL__WAITER_PENDING, memory_order_relaxed);
    fw->future = out_future;
    fw->exec = exec;
    fw->alloc = alloc;
    mel_task_init(&fw->task, mel_channel__future_task);

    fw->op.channel = ch;
    fw->op.slot = slot;
    fw->op.is_send = is_send ? 1 : 0;
    mel_channel__op_bind(&fw->op, &fw->wait, mel_channel__future_wake, fw);

    mel_mutex_lock(&ch->lock);
    Mel_Channel_Status s = is_send ? mel_channel__send_locked(ch, slot, &fw->op) : mel_channel__recv_locked(ch, slot, &fw->op);
    mel_mutex_unlock(&ch->lock);

    if (s != MEL_CHANNEL_WOULD_BLOCK)
    {
        i32 outcome = mel_channel_status_closed(s) ? MEL_CHANNEL__WAITER_CLOSED : MEL_CHANNEL__WAITER_COMMITTED;
        i32 expected = MEL_CHANNEL__WAITER_PENDING;
        if (atomic_compare_exchange_strong_explicit(&fw->wait.state, &expected, outcome, memory_order_acq_rel, memory_order_acquire))
            mel_channel__future_settle(fw);
    }
}

void mel_channel_send_future(Mel_Channel* ch, const void* item, Mel_Future* out_future, Mel_Executor* exec, const Mel_Alloc* alloc)
{
    assert(item != NULL);
    mel_channel__future_op(ch, (void*)item, true, out_future, exec, alloc);
}

void mel_channel_recv_future(Mel_Channel* ch, void* out, Mel_Future* out_future, Mel_Executor* exec, const Mel_Alloc* alloc)
{
    assert(out != NULL);
    mel_channel__future_op(ch, out, false, out_future, exec, alloc);
}
