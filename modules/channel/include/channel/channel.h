#pragma once

#include <core/types.h>
#include <core/compiler.h>
#include <allocator/allocator.fwd.h>
#include <collection.list/list.h>
#include <executor/executor.h>
#include <future/future.h>

#include <stdatomic.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef u32 Mel_Channel_Status;

#define MEL_CHANNEL_SEVERITY_MASK 0x3u
#define MEL_CHANNEL_OK            0u
#define MEL_CHANNEL_WARNED        1u
#define MEL_CHANNEL_ERROR         2u

#define MEL_CHANNEL_CLOSED        (1u << 2)
#define MEL_CHANNEL_WOULD_BLOCK   (1u << 3)

static inline bool mel_channel_status_failed(Mel_Channel_Status s) { return (s & MEL_CHANNEL_SEVERITY_MASK) == MEL_CHANNEL_ERROR; }
static inline bool mel_channel_status_closed(Mel_Channel_Status s) { return (s & MEL_CHANNEL_CLOSED) != 0u; }
static inline bool mel_channel_status_would_block(Mel_Channel_Status s) { return (s & MEL_CHANNEL_WOULD_BLOCK) != 0u; }

typedef struct Mel_Channel Mel_Channel;

typedef struct Mel_Channel_Op  Mel_Channel_Op;
typedef struct Mel_Channel_Sel Mel_Channel_Sel;

#define MEL_CHANNEL__WAITER_PENDING   0
#define MEL_CHANNEL__WAITER_COMMITTED 1
#define MEL_CHANNEL__WAITER_CLOSED    2

struct Mel_Channel_Op
{
    Mel_Channel* channel;
    void*        slot;
    u8           is_send;

    Mel_ListNode          link;
    _Atomic(Mel_Channel*) owner_channel;

    _Atomic(i32)*    group_state;
    Mel_Channel_Op** group_winner;
    Mel_Waker        waker;
};

Mel_Channel* mel_channel_create(usize item_size, usize capacity, const Mel_Alloc* alloc);
void         mel_channel_destroy(Mel_Channel* ch);

usize mel_channel_item_size(const Mel_Channel* ch);
usize mel_channel_capacity(const Mel_Channel* ch);
bool  mel_channel_is_closed(const Mel_Channel* ch);

Mel_Channel_Status mel_channel_send(Mel_Channel* ch, const void* item);
Mel_Channel_Status mel_channel_recv(Mel_Channel* ch, void* out);

Mel_Channel_Status mel_channel_try_send(Mel_Channel* ch, const void* item);
Mel_Channel_Status mel_channel_try_recv(Mel_Channel* ch, void* out);

void mel_channel_send_future(Mel_Channel* ch, const void* item, Mel_Future* out_future, Mel_Executor* exec, const Mel_Alloc* alloc);
void mel_channel_recv_future(Mel_Channel* ch, void* out, Mel_Future* out_future, Mel_Executor* exec, const Mel_Alloc* alloc);

void mel_channel_close(Mel_Channel* ch);

void               mel_channel_sel_init(Mel_Channel_Sel* sel, Mel_Channel_Op* ops, usize n);
Mel_Channel_Op*    mel_channel_sel_wait(Mel_Channel_Sel* sel);
Mel_Channel_Op*    mel_channel_sel_try(Mel_Channel_Sel* sel);
static inline void mel_channel_op_send(Mel_Channel_Op* op, Mel_Channel* ch, const void* item);
static inline void mel_channel_op_recv(Mel_Channel_Op* op, Mel_Channel* ch, void* out);

struct Mel_Channel_Sel
{
    Mel_Channel_Op* ops;
    usize           n;
};

static inline void mel_channel_op_send(Mel_Channel_Op* op, Mel_Channel* ch, const void* item)
{
    op->channel = ch;
    op->slot = (void*)item;
    op->is_send = 1;
}

static inline void mel_channel_op_recv(Mel_Channel_Op* op, Mel_Channel* ch, void* out)
{
    op->channel = ch;
    op->slot = out;
    op->is_send = 0;
}

#ifdef __cplusplus
}
#endif
