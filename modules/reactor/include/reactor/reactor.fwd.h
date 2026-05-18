#pragma once

#include <core/types.h>
#include <collection.slotmap/slotmap.fwd.h>

typedef struct { Mel_SlotMap_Handle handle; } Mel_Reactor;

typedef struct {
    Mel_Reactor reactor;
    u8          kind;
    u32         index;
    u32         generation;
} Mel_Reactor_Listener;

#define MEL_REACTOR_NULL          ((Mel_Reactor){ .handle = MEL_SLOTMAP_HANDLE_NULL })
#define MEL_REACTOR_LISTENER_NULL ((Mel_Reactor_Listener){0})

static inline bool mel_reactor_valid(Mel_Reactor r)
{
    return mel_slotmap_handle_valid(r.handle);
}

static inline bool mel_reactor_eq(Mel_Reactor a, Mel_Reactor b)
{
    return a.handle.index == b.handle.index && a.handle.generation == b.handle.generation;
}

static inline bool mel_reactor_listener_valid(Mel_Reactor_Listener l)
{
    return mel_reactor_valid(l.reactor) && l.generation != 0;
}
