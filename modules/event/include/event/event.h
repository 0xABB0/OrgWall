#pragma once

#include <core/types.h>
#include <allocator/allocator.fwd.h>
#include <collection/slotmap.fwd.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Executor Mel_Executor;
typedef struct Mel_Event    Mel_Event;

typedef struct
{
    Mel_SlotMap_Handle handle;
} Mel_Event_Sub;

#define MEL_EVENT_SUB_NULL ((Mel_Event_Sub){ MEL_SLOTMAP_HANDLE_NULL })

typedef struct
{
    Mel_Event_Sub sub;
    u32           ring_capacity;
    u32           ring_count;
    u64           total_lagged;
    bool          push;
    bool          drop_oldest;
    bool          accepted;
    bool          backpressured;
} Mel_Event_Overflow_Info;

typedef void (*Mel_Event_Overflow_Fn)(Mel_Event_Overflow_Info* info, void* user);
typedef void (*Mel_Event_On_Overflow)(const Mel_Event_Overflow_Info* info, void* user);

typedef struct
{
    Mel_Event_Overflow_Fn overflow;
    Mel_Event_On_Overflow on_overflow;
    void*                 user;
} Mel_Event_Policy;

Mel_Event_Policy mel_event_policy_latest(Mel_Event_On_Overflow on_overflow, void* user);
Mel_Event_Policy mel_event_policy_lossy_lag(Mel_Event_On_Overflow on_overflow, void* user);
Mel_Event_Policy mel_event_policy_lossless(Mel_Event_On_Overflow on_overflow, void* user);
Mel_Event_Policy mel_event_policy_custom(Mel_Event_Overflow_Fn overflow, Mel_Event_On_Overflow on_overflow, void* user);

Mel_Event* mel_event_create(const Mel_Alloc* alloc, usize item_size, u32 ring_capacity, Mel_Event_Policy policy);
void       mel_event_destroy(Mel_Event* ev);

typedef void (*Mel_Event_Callback)(const void* item, void* user);

Mel_Event_Sub mel_event_subscribe_push(Mel_Event* ev, Mel_Executor* exec, Mel_Event_Callback cb, void* user);
Mel_Event_Sub mel_event_subscribe_pull(Mel_Event* ev, void* user);
void          mel_event_unsubscribe(Mel_Event* ev, Mel_Event_Sub sub);

void mel_event_fire(Mel_Event* ev, const void* item);

bool mel_event_pull(Mel_Event* ev, Mel_Event_Sub sub, void* item_out);
u32  mel_event_pull_pending(Mel_Event* ev, Mel_Event_Sub sub);
u64  mel_event_lag(Mel_Event* ev, Mel_Event_Sub sub);

u32 mel_event_subscriber_count(const Mel_Event* ev);

#ifdef __cplusplus
}
#endif
