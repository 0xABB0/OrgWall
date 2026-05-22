#pragma once

#include <core/types.h>
#include <async.signal/signal.cfg.h>
#include <async.fiber/fiber.fwd.h>
#include <stdatomic.h>

typedef struct {
    u16 next;
    Mel_Fiber fiber;
} Mel__Park_Node;

typedef struct Mel_Signal {
    _Atomic(i32) state;
    u32 generation;
} Mel_Signal;

typedef struct Mel_Counter {
    Mel_Signal signal;
} Mel_Counter;

typedef struct Mel_Fiber_Mutex {
    Mel_Signal signal;
} Mel_Fiber_Mutex;

typedef struct {
    Mel__Park_Node* park_pool;
    u32 park_pool_size;
    void (*schedule_fiber)(u16 park_index);
    void (*wake_workers)(i32 count);
} Mel__Signal_Runtime;

static inline u16 mel__signal_counter(i32 state) { return (u16)(state & 0xFFFF); }
static inline u16 mel__signal_head(i32 state)     { return (u16)((state >> 16) & 0xFFFF); }
static inline i32 mel__signal_pack(u16 counter, u16 head) {
    return (i32)counter | ((i32)head << 16);
}

#define MEL__SIGNAL_GREEN ((i32)((i32)MEL_SIGNAL_NULL_INDEX << 16))

#define MEL_SIGNAL_INIT  { .state = MEL__SIGNAL_GREEN, .generation = 0 }
#define MEL_COUNTER_INIT { .signal = MEL_SIGNAL_INIT }
#define MEL_FIBER_MUTEX_INIT   { .signal = MEL_SIGNAL_INIT }

void mel__signal_init_runtime(Mel__Signal_Runtime rt);
u32  mel__signal_next_generation(void);

void mel_signal_set(Mel_Signal* s);
void mel_signal_clear(Mel_Signal* s);
void mel_signal_wait(Mel_Signal* s);
void mel_signal_wait_and_set(Mel_Signal* s);

void mel_counter_increment(Mel_Counter* c);
void mel_counter_decrement(Mel_Counter* c);
void mel_counter_wait(Mel_Counter* c);

void mel_fiber_mutex_enter(Mel_Fiber_Mutex* m);
void mel_fiber_mutex_exit(Mel_Fiber_Mutex* m);
