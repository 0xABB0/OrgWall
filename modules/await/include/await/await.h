#pragma once

#include <core/types.h>
#include <allocator/allocator.fwd.h>
#include <executor/executor.h>
#include <future/future.h>
#include <channel/channel.h>
#include <vat/vat.h>
#include <vat/timer.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Await_Step
{
    Mel_Future*        future;
    Mel_Channel*       channel;
    void*              slot;
    bool               is_send;
    i64                after_ns;
    bool               reschedule;
    Mel_Future_Status* status_out;
} Mel_Await_Step;

typedef bool (*Mel_Await_Resume)(void* frame, Mel_Await_Step* out);

typedef struct Mel_Await_Coro_Desc
{
    void*            frame;
    Mel_Await_Resume resume;
    Mel_Executor*    exec;
    const Mel_Alloc* alloc;
    Mel_Vat*         vat;
    Mel_Vat_Timers*  timers;
    Mel_Future*      done;
    void (*on_done)(void* user);
    void* user;
} Mel_Await_Coro_Desc;

typedef struct Mel_Await_Coro
{
    Mel_Task            task;
    Mel_Await_Coro_Desc d;
    Mel_Future          op_future;
    Mel_Future*         waited;
    Mel_Future_Status*  status_out;
} Mel_Await_Coro;

void mel_await_coro_start(Mel_Await_Coro* c, Mel_Await_Coro_Desc desc);

void* mel_await_future(Mel_Future* f);

#ifdef __cplusplus
}
#endif
