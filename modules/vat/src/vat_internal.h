#pragma once

#include <vat/vat.h>

#include <collection/array.h>
#include <collection/mpsc.h>
#include <thread/thread.h>

#include <stdatomic.h>

struct Mel_Vat_Source
{
    Mel_Vat*                   vat;
    const Mel_Vat_Source_Vtbl* vt;
    void*                      state;
    bool                       closing;
};

struct Mel_Vat
{
    const Mel_Alloc* alloc;
    Mel_Vat_Waiter*  waiter;
    Mel_Vat_Driver*  driver;
    Mel_Thread_Id    owner;
    Mel_Mpsc         mailbox;
    Mel_Mpsc_Node*   ready_head;
    Mel_Mpsc_Node*   ready_tail;
    Mel_Array(Mel_Vat_Source*) sources;
    i32          depth;
    i32          draining;
    i32          retains;
    bool         reap_pending;
    atomic_bool  quitting;
    atomic_bool  parked;
    Mel_Executor executor;
};

i64   mel_vat__now(void);
void  mel_vat__drain_mailbox(Mel_Vat* vat);
i64   mel_vat__reduce(Mel_Vat* vat);
usize mel_vat__run_ready(Mel_Vat* vat, u32 budget);
usize mel_vat__drain_sources(Mel_Vat* vat, i64 now, u32 budget);
bool  mel_vat__retained(const Mel_Vat* vat);
void  mel_vat__reap(Mel_Vat* vat);
