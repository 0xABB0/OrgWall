#pragma once

#include <core/types.h>
#include <core/platform.h>
#include <core/compiler.h>
#include <stdatomic.h>

#include "mpsc.cfg.h"
#include "mpsc.fwd.h"

struct Mel_Mpsc_Node
{
    _Atomic(Mel_Mpsc_Node*) next;
};

struct Mel_Mpsc
{
    MEL_ALIGNAS(MEL_CACHE_LINE_SIZE) _Atomic(Mel_Mpsc_Node*) producer_tail;
    MEL_ALIGNAS(MEL_CACHE_LINE_SIZE) Mel_Mpsc_Node* consumer_head;
    MEL_ALIGNAS(MEL_CACHE_LINE_SIZE) Mel_Mpsc_Node stub;
#if MEL_COLLECTION_MPSC_DEBUG
    _Atomic(usize) push_count;
    _Atomic(usize) pop_count;
#endif
};

void mel_mpsc_init(Mel_Mpsc* q);

inline static void           mel_mpsc_push(Mel_Mpsc* q, Mel_Mpsc_Node* node);
inline static Mel_Mpsc_Node* mel_mpsc_pop(Mel_Mpsc* q);

#include "mpsc.inl"
