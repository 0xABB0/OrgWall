#include <future/future.h>
#include <executor/executor.h>
#include <allocator/allocator.h>
#include <collection/list.h>

#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    Mel_Executor    base;
    pthread_mutex_t mtx;
    pthread_cond_t  cv;
    Mel_Mpsc_Node*  head;
    Mel_Mpsc_Node*  tail;
    _Atomic(bool)   stop;
    pthread_t       thread;
} Worker_Exec;

static void worker_submit(Mel_Executor* self, Mel_Task* task)
{
    Worker_Exec* w = (Worker_Exec*)self;
    i32          expected = 0;
    if (!atomic_compare_exchange_strong_explicit(&task->armed, &expected, 1, memory_order_acq_rel, memory_order_acquire))
        return;
    atomic_store_explicit(&task->link.next, NULL, memory_order_relaxed);
    pthread_mutex_lock(&w->mtx);
    if (w->tail != NULL)
        atomic_store_explicit(&w->tail->next, &task->link, memory_order_relaxed);
    else
        w->head = &task->link;
    w->tail = &task->link;
    pthread_cond_signal(&w->cv);
    pthread_mutex_unlock(&w->mtx);
}

static void* worker_loop(void* arg)
{
    Worker_Exec* w = (Worker_Exec*)arg;
    for (;;)
    {
        pthread_mutex_lock(&w->mtx);
        while (w->head == NULL && !atomic_load_explicit(&w->stop, memory_order_acquire))
            pthread_cond_wait(&w->cv, &w->mtx);
        if (w->head == NULL && atomic_load_explicit(&w->stop, memory_order_acquire))
        {
            pthread_mutex_unlock(&w->mtx);
            return NULL;
        }
        Mel_Mpsc_Node* node = w->head;
        w->head = atomic_load_explicit(&node->next, memory_order_relaxed);
        if (w->head == NULL)
            w->tail = NULL;
        pthread_mutex_unlock(&w->mtx);

        Mel_Task* task = mel_container_of(node, Mel_Task, link);
        atomic_store_explicit(&node->next, NULL, memory_order_relaxed);
        atomic_store_explicit(&task->armed, 0, memory_order_release);
        task->run(task);
    }
}

static void worker_init(Worker_Exec* w)
{
    w->base.submit = worker_submit;
    pthread_mutex_init(&w->mtx, NULL);
    pthread_cond_init(&w->cv, NULL);
    w->head = NULL;
    w->tail = NULL;
    atomic_store(&w->stop, false);
    pthread_create(&w->thread, NULL, worker_loop, w);
}

static void worker_shutdown(Worker_Exec* w)
{
    pthread_mutex_lock(&w->mtx);
    atomic_store_explicit(&w->stop, true, memory_order_release);
    pthread_cond_signal(&w->cv);
    pthread_mutex_unlock(&w->mtx);
    pthread_join(w->thread, NULL);
    pthread_mutex_destroy(&w->mtx);
    pthread_cond_destroy(&w->cv);
}

typedef struct
{
    Mel_Task          task;
    Mel_Future*       fut;
    _Atomic(i32)      ran;
    void*             observed_value;
    Mel_Future_Status observed_status;
    int               payload_check;
} Cont;

static _Atomic(i64) g_total_runs;
static _Atomic(i64) g_corrupt_reads;

static void cont_run(Mel_Task* self)
{
    Cont* c = mel_container_of(self, Cont, task);
    atomic_fetch_add_explicit(&g_total_runs, 1, memory_order_relaxed);
    i32 prev = atomic_fetch_add_explicit(&c->ran, 1, memory_order_relaxed);
    (void)prev;
    void*             v = mel_future_value(c->fut);
    Mel_Future_Status s = mel_future_status(c->fut);
    c->observed_value = v;
    c->observed_status = s;
    if (!(s & MEL_FUTURE_CANCELLED))
    {
        int* p = (int*)v;
        if (p == NULL || *p != c->payload_check)
            atomic_fetch_add_explicit(&g_corrupt_reads, 1, memory_order_relaxed);
    }
}

static _Atomic(i64) g_value_frees;

static void free_value(void* value, const Mel_Alloc* alloc)
{
    atomic_fetch_add_explicit(&g_value_frees, 1, memory_order_relaxed);
    mel_dealloc(alloc, value);
}

typedef struct
{
    Mel_Future*      futs;
    Cont*            conts;
    int**            payloads;
    usize            n;
    Worker_Exec*     worker;
    const Mel_Alloc* alloc;
    _Atomic(i32)*    gate;
    _Atomic(i32)*    arrived;
    _Atomic(i64)*    winner_resolved;
    _Atomic(i64)*    winner_cancelled;
    bool             do_cancel;
} Race_Args;

static void gate_sync(Race_Args* a)
{
    atomic_fetch_add_explicit(a->arrived, 1, memory_order_acq_rel);
    while (atomic_load_explicit(a->gate, memory_order_acquire) == 0)
    {
    }
}

static void* thread_then(void* arg)
{
    Race_Args* a = (Race_Args*)arg;
    gate_sync(a);
    for (usize i = 0; i < a->n; i++)
        mel_future_then(&a->futs[i], &a->conts[i].task, &a->worker->base);
    return NULL;
}

static void* thread_settle(void* arg)
{
    Race_Args* a = (Race_Args*)arg;
    gate_sync(a);
    for (usize i = 0; i < a->n; i++)
    {
        if (a->do_cancel)
        {
            if (mel_future_cancel(&a->futs[i]))
                atomic_fetch_add_explicit(a->winner_cancelled, 1, memory_order_relaxed);
            else
                atomic_fetch_add_explicit(a->winner_resolved, 1, memory_order_relaxed);
        }
        else
        {
            if (mel_future_resolve(&a->futs[i], a->payloads[i], MEL_FUTURE_OK))
                atomic_fetch_add_explicit(a->winner_resolved, 1, memory_order_relaxed);
        }
    }
    return NULL;
}

static int run_round(usize n, bool do_cancel, const Mel_Alloc* alloc)
{
    Worker_Exec worker;
    worker_init(&worker);

    Mel_Future* futs = (Mel_Future*)calloc(n, sizeof(Mel_Future));
    Cont*       conts = (Cont*)calloc(n, sizeof(Cont));
    int**       payloads = (int**)calloc(n, sizeof(int*));

    for (usize i = 0; i < n; i++)
    {
        mel_future_init(&futs[i], do_cancel ? free_value : NULL, alloc);
        payloads[i] = (int*)malloc(sizeof(int));
        *payloads[i] = (int)(i + 1000);
        conts[i].fut = &futs[i];
        atomic_store_explicit(&conts[i].ran, 0, memory_order_relaxed);
        conts[i].observed_value = NULL;
        conts[i].observed_status = MEL_FUTURE_OK;
        conts[i].payload_check = (int)(i + 1000);
        mel_task_init(&conts[i].task, cont_run);
    }

    _Atomic(i64) winner_resolved = 0;
    _Atomic(i64) winner_cancelled = 0;

    _Atomic(i32) gate = 0;
    _Atomic(i32) arrived = 0;

    Race_Args args = {
        .futs = futs,
        .conts = conts,
        .payloads = payloads,
        .n = n,
        .worker = &worker,
        .alloc = alloc,
        .gate = &gate,
        .arrived = &arrived,
        .winner_resolved = &winner_resolved,
        .winner_cancelled = &winner_cancelled,
        .do_cancel = do_cancel,
    };

    pthread_t t_then, t_settle;
    pthread_create(&t_then, NULL, thread_then, &args);
    pthread_create(&t_settle, NULL, thread_settle, &args);
    while (atomic_load_explicit(&arrived, memory_order_acquire) < 2)
    {
    }
    atomic_store_explicit(&gate, 1, memory_order_release);
    pthread_join(t_then, NULL);
    pthread_join(t_settle, NULL);

    worker_shutdown(&worker);

    int failures = 0;
    for (usize i = 0; i < n; i++)
    {
        i32 ran = atomic_load_explicit(&conts[i].ran, memory_order_relaxed);
        if (ran != 1)
        {
            failures++;
            if (failures <= 8)
                fprintf(stderr, "FAIL fut[%zu] ran=%d (expected 1)\n", i, ran);
        }
    }

    for (usize i = 0; i < n; i++)
        free(payloads[i]);
    free(futs);
    free(conts);
    free(payloads);
    return failures;
}

typedef struct
{
    Mel_Future*      futs;
    Cont*            conts;
    int**            payloads;
    usize            n;
    Worker_Exec*     worker;
    const Mel_Alloc* alloc;
    _Atomic(i32)*    gate;
    _Atomic(i32)*    arrived;
    _Atomic(i64)*    resolved_wins;
    _Atomic(i64)*    cancelled_wins;
} Settler_Args;

static void settler_sync(Settler_Args* a)
{
    atomic_fetch_add_explicit(a->arrived, 1, memory_order_acq_rel);
    while (atomic_load_explicit(a->gate, memory_order_acquire) == 0)
    {
    }
}

static void* thread_resolver(void* arg)
{
    Settler_Args* a = (Settler_Args*)arg;
    settler_sync(a);
    for (usize i = 0; i < a->n; i++)
    {
        if (mel_future_resolve(&a->futs[i], a->payloads[i], MEL_FUTURE_OK))
            atomic_fetch_add_explicit(a->resolved_wins, 1, memory_order_relaxed);
    }
    return NULL;
}

static void* thread_canceller(void* arg)
{
    Settler_Args* a = (Settler_Args*)arg;
    settler_sync(a);
    for (usize i = 0; i < a->n; i++)
    {
        if (mel_future_cancel(&a->futs[i]))
            atomic_fetch_add_explicit(a->cancelled_wins, 1, memory_order_relaxed);
    }
    return NULL;
}

static void* thread_then_concurrent(void* arg)
{
    Settler_Args* a = (Settler_Args*)arg;
    settler_sync(a);
    for (usize i = 0; i < a->n; i++)
        mel_future_then(&a->futs[i], &a->conts[i].task, &a->worker->base);
    return NULL;
}

static int run_concurrent_round(usize n, bool then_before, const Mel_Alloc* alloc)
{
    Worker_Exec worker;
    worker_init(&worker);

    Mel_Future* futs = (Mel_Future*)calloc(n, sizeof(Mel_Future));
    Cont*       conts = (Cont*)calloc(n, sizeof(Cont));
    int**       payloads = (int**)calloc(n, sizeof(int*));

    for (usize i = 0; i < n; i++)
    {
        mel_future_init(&futs[i], free_value, alloc);
        payloads[i] = (int*)malloc(sizeof(int));
        *payloads[i] = (int)(i + 5000);
        conts[i].fut = &futs[i];
        atomic_store_explicit(&conts[i].ran, 0, memory_order_relaxed);
        conts[i].observed_value = NULL;
        conts[i].observed_status = MEL_FUTURE_OK;
        conts[i].payload_check = (int)(i + 5000);
        mel_task_init(&conts[i].task, cont_run);
    }

    if (then_before)
        for (usize i = 0; i < n; i++)
            mel_future_then(&futs[i], &conts[i].task, &worker.base);

    _Atomic(i64) resolved_wins = 0;
    _Atomic(i64) cancelled_wins = 0;
    _Atomic(i32) gate = 0;
    _Atomic(i32) arrived = 0;

    Settler_Args args = {
        .futs = futs,
        .conts = conts,
        .payloads = payloads,
        .n = n,
        .worker = &worker,
        .alloc = alloc,
        .gate = &gate,
        .arrived = &arrived,
        .resolved_wins = &resolved_wins,
        .cancelled_wins = &cancelled_wins,
    };

    pthread_t t_res, t_can, t_then;
    int       threads = then_before ? 2 : 3;
    pthread_create(&t_res, NULL, thread_resolver, &args);
    pthread_create(&t_can, NULL, thread_canceller, &args);
    if (!then_before)
        pthread_create(&t_then, NULL, thread_then_concurrent, &args);

    while (atomic_load_explicit(&arrived, memory_order_acquire) < threads)
    {
    }
    atomic_store_explicit(&gate, 1, memory_order_release);

    pthread_join(t_res, NULL);
    pthread_join(t_can, NULL);
    if (!then_before)
        pthread_join(t_then, NULL);

    worker_shutdown(&worker);

    int failures = 0;

    i64 rw = atomic_load(&resolved_wins);
    i64 cw = atomic_load(&cancelled_wins);
    if (rw + cw != (i64)n)
    {
        fprintf(stderr, "FAIL winner-uniqueness: resolved_wins=%lld cancelled_wins=%lld (sum != %zu)\n", (long long)rw, (long long)cw, n);
        failures++;
    }

    i64 expected_frees = cw;
    i64 actual_frees = atomic_load(&g_value_frees);
    if (actual_frees != expected_frees)
    {
        fprintf(stderr, "FAIL value-freed-once: freed=%lld expected=%lld (cancel-won futures)\n", (long long)actual_frees, (long long)expected_frees);
        failures++;
    }

    for (usize i = 0; i < n; i++)
    {
        i32 ran = atomic_load_explicit(&conts[i].ran, memory_order_relaxed);
        if (ran != 1)
        {
            failures++;
            if (failures <= 8)
                fprintf(stderr, "FAIL fut[%zu] ran=%d (expected 1)\n", i, ran);
            continue;
        }

        bool cont_cancelled = (conts[i].observed_status & MEL_FUTURE_CANCELLED) != 0u;
        if (cont_cancelled)
        {
            if (conts[i].observed_value != NULL)
            {
                failures++;
                if (failures <= 8)
                    fprintf(stderr, "FAIL fut[%zu] cancelled but value!=NULL\n", i);
            }
        }
        else
        {
            int* p = (int*)conts[i].observed_value;
            if (p == NULL || *p != conts[i].payload_check)
            {
                failures++;
                if (failures <= 8)
                    fprintf(stderr, "FAIL fut[%zu] resolved disposition mismatch\n", i);
            }
        }
    }

    for (usize i = 0; i < n; i++)
    {
        u32 cancelled = mel_future_status_cancelled(mel_future_status(&futs[i]));
        if (!cancelled)
            free(payloads[i]);
    }
    free(futs);
    free(conts);
    free(payloads);
    return failures;
}

static void* malloc_cb(void* ptr, usize size, u32 align, const char* file, const char* func, u32 line, void* user_data)
{
    (void)align;
    (void)file;
    (void)func;
    (void)line;
    (void)user_data;
    if (ptr == NULL)
        return malloc(size);
    if (size == 0)
    {
        free(ptr);
        return NULL;
    }
    return realloc(ptr, size);
}

int main(void)
{
    static Mel_Alloc s_alloc = { .alloc_cb = malloc_cb, .user_data = NULL };
    const Mel_Alloc* heap_like = &s_alloc;

    const usize N = 2000;
    const int   ROUNDS = 20;

    int total_failures = 0;
    for (int r = 0; r < ROUNDS; r++)
    {
        atomic_store(&g_total_runs, 0);
        atomic_store(&g_corrupt_reads, 0);
        atomic_store(&g_value_frees, 0);

        bool do_cancel = (r % 2) == 1;
        int  f = run_round(N, do_cancel, heap_like);
        total_failures += f;

        i64 runs = atomic_load(&g_total_runs);
        i64 corrupt = atomic_load(&g_corrupt_reads);
        if (runs != (i64)N)
        {
            fprintf(stderr, "ROUND %d: total continuation runs=%lld (expected %zu)\n", r, (long long)runs, N);
            total_failures++;
        }
        if (corrupt != 0)
        {
            fprintf(stderr, "ROUND %d: corrupt continuation reads=%lld\n", r, (long long)corrupt);
            total_failures++;
        }
    }

    for (int r = 0; r < ROUNDS; r++)
    {
        atomic_store(&g_total_runs, 0);
        atomic_store(&g_corrupt_reads, 0);
        atomic_store(&g_value_frees, 0);

        bool then_before = (r % 2) == 0;
        int  f = run_concurrent_round(N, then_before, heap_like);
        total_failures += f;

        i64 runs = atomic_load(&g_total_runs);
        if (runs != (i64)N)
        {
            fprintf(stderr, "CONCURRENT %d: total continuation runs=%lld (expected %zu)\n", r, (long long)runs, N);
            total_failures++;
        }
    }

    if (total_failures == 0)
        fprintf(stderr, "stress: OK (%d then/settler rounds + %d concurrent-settler rounds, %zu futures each)\n", ROUNDS, ROUNDS, N);
    else
        fprintf(stderr, "stress: FAILED with %d failures\n", total_failures);
    return total_failures == 0 ? 0 : 1;
}
