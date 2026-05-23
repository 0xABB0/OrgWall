#pragma once

#include "rcu.fwd.h"
#include <allocator/allocator.fwd.h>
#include <thread/mutex.h>

typedef struct Mel__Rcu_Garbage Mel__Rcu_Garbage;

struct Mel_Rcu {
    _Atomic(void*) ptr;
    _Atomic(u32) epoch;
    _Atomic(i32) readers[2];
    Mel_Mutex writer_lock;
    Mel__Rcu_Garbage* garbage;
    const Mel_Alloc* alloc;
};

void  mel_rcu_init(Mel_Rcu* rcu, const Mel_Alloc* alloc);
void  mel_rcu_destroy(Mel_Rcu* rcu);

void* mel_rcu_read(Mel_Rcu* rcu, Mel_Rcu_Token* token);
void  mel_rcu_read_end(Mel_Rcu* rcu, Mel_Rcu_Token token);

void  mel_rcu_writer_lock(Mel_Rcu* rcu);
void  mel_rcu_writer_unlock(Mel_Rcu* rcu);
void* mel_rcu_writer_load(Mel_Rcu* rcu);
void  mel_rcu_writer_store(Mel_Rcu* rcu, void* new_ptr);
