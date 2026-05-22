#pragma once

#include <core/types.h>
#include <stdatomic.h>

#include "mpsc.cfg.h"
#include "mpsc.fwd.h"

struct Mel_Mpsc_Node {
    _Atomic(Mel_Mpsc_Node*) next;
};

struct Mel_Mpsc {
    _Alignas(64) _Atomic(Mel_Mpsc_Node*) producer_tail;
    _Alignas(64) Mel_Mpsc_Node*          consumer_head;
    _Alignas(64) Mel_Mpsc_Node           stub;
#if MEL_COLLECTION_MPSC_DEBUG
    _Atomic(usize) push_count;
    _Atomic(usize) pop_count;
    const char*    name;
#endif
};

void mel_mpsc_init(Mel_Mpsc* q);

inline static void           mel_mpsc_push(Mel_Mpsc* q, Mel_Mpsc_Node* node);
inline static Mel_Mpsc_Node* mel_mpsc_pop (Mel_Mpsc* q);

#include "mpsc.inl"
