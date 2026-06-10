#include <await/await.h>

#include <collection/list.h>
#include <signal/signal.h>
#include <time/nano.h>

#include <assert.h>

static void mel_await__coro_finish(Mel_Await_Coro* c)
{
    if (c->d.done)
        mel_future_resolve(c->d.done, c->d.frame, MEL_FUTURE_OK);
    if (c->d.on_done)
        c->d.on_done(c->d.user);
    if (c->d.vat)
        mel_vat_release(c->d.vat);
}

static void mel_await__coro_run(Mel_Task* self)
{
    Mel_Await_Coro* c = mel_container_of(self, Mel_Await_Coro, task);

    if (c->status_out)
    {
        *c->status_out = mel_future_status(c->waited);
        c->status_out = NULL;
    }
    c->waited = NULL;

    Mel_Await_Step step = { 0 };
    if (!c->d.resume(c->d.frame, &step))
    {
        mel_await__coro_finish(c);
        return;
    }

    assert((step.future != NULL) + (step.channel != NULL) + (step.after_ns > 0) + (int)step.reschedule == 1);

    if (step.future)
    {
        c->waited = step.future;
        c->status_out = step.status_out;
        mel_future_then(step.future, &c->task, c->d.exec);
        return;
    }

    if (step.channel)
    {
        assert(c->d.alloc != NULL);
        if (step.is_send)
            mel_channel_send_future(step.channel, step.slot, &c->op_future, c->d.exec, c->d.alloc);
        else
            mel_channel_recv_future(step.channel, step.slot, &c->op_future, c->d.exec, c->d.alloc);
        c->waited = &c->op_future;
        c->status_out = step.status_out;
        mel_future_then(&c->op_future, &c->task, c->d.exec);
        return;
    }

    if (step.after_ns > 0)
    {
        assert(c->d.timers != NULL);
        mel_vat_timers_add(c->d.timers, (i64)mel_nanos_since_unspecified_epoch() + step.after_ns, &c->task);
        return;
    }

    assert(step.reschedule);
    mel_executor_submit(c->d.exec, &c->task);
}

void mel_await_coro_start(Mel_Await_Coro* c, Mel_Await_Coro_Desc desc)
{
    assert(c != NULL);
    assert(desc.frame != NULL);
    assert(desc.resume != NULL);
    assert(desc.exec != NULL);
    assert(desc.vat == NULL || desc.exec == mel_vat_executor(desc.vat));
    assert(desc.timers == NULL || desc.vat != NULL);

    c->d = desc;
    c->waited = NULL;
    c->status_out = NULL;
    mel_task_init(&c->task, mel_await__coro_run);

    if (desc.vat)
        mel_vat_retain(desc.vat);
    mel_executor_submit(desc.exec, &c->task);
}

typedef struct
{
    Mel_Task    task;
    Mel_Counter gate;
} Mel_Await__Park;

static void mel_await__park_run(Mel_Task* self)
{
    Mel_Await__Park* p = mel_container_of(self, Mel_Await__Park, task);
    mel_counter_decrement(&p->gate);
}

void* mel_await_future(Mel_Future* f)
{
    assert(f != NULL);

    Mel_Await__Park p;
    Mel_Counter     init = MEL_COUNTER_INIT;
    p.gate = init;
    mel_counter_increment(&p.gate);
    mel_task_init(&p.task, mel_await__park_run);

    mel_future_then(f, &p.task, mel_executor_inline());
    mel_counter_wait(&p.gate);

    return mel_future_value(f);
}
