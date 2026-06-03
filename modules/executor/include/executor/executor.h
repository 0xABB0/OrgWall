#pragma once

#include <core/types.h>
#include <allocator/allocator.fwd.h>
#include <collection.mpsc/mpsc.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Task     Mel_Task;
typedef struct Mel_Executor Mel_Executor;

struct Mel_Task
{
    void (*run)(Mel_Task* self);
    Mel_Mpsc_Node link;
    _Atomic(i32)  armed;
};

struct Mel_Executor
{
    void (*submit)(Mel_Executor* self, Mel_Task* task);
};

typedef struct
{
    void (*wake)(void* user);
    void* user;
} Mel_Waker;

static inline void mel_task_init(Mel_Task* task, void (*run)(Mel_Task* self))
{
    task->run = run;
    atomic_store_explicit(&task->link.next, NULL, memory_order_relaxed);
    atomic_store_explicit(&task->armed, 0, memory_order_relaxed);
}

static inline void mel_executor_submit(Mel_Executor* exec, Mel_Task* task) { exec->submit(exec, task); }

Mel_Executor* mel_executor_inline(void);

typedef struct
{
    Mel_Executor* exec;
    Mel_Task*     task;
} Mel_Resubmit_Cell;

Mel_Waker mel_resubmit_waker(Mel_Resubmit_Cell* cell);

void mel_executor_call(Mel_Executor* exec, void (*fn)(void* data), void* data, const Mel_Alloc* alloc);

#ifdef __cplusplus
}
#endif
