#include <future/future.h>
#include <test/test.h>

#include <allocator/heap.h>
#include <allocator/allocator.h>
#include <collection.list/list.h>

#include <stdatomic.h>
#include <stdio.h>

#ifndef _WIN32
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#endif

typedef struct
{
    Mel_Task          task;
    int               ran;
    void*             seen_value;
    Mel_Future_Status seen_status;
    Mel_Future*       fut;
} Cont;

static void cont_run(Mel_Task* self)
{
    Cont* c = mel_container_of(self, Cont, task);
    c->ran++;
    if (c->fut != NULL)
    {
        c->seen_value = mel_future_value(c->fut);
        c->seen_status = mel_future_status(c->fut);
    }
}

static void cont_init(Cont* c, Mel_Future* f)
{
    c->ran = 0;
    c->seen_value = NULL;
    c->seen_status = MEL_FUTURE_OK;
    c->fut = f;
    mel_task_init(&c->task, cont_run);
}

MEL_TEST(future, resolve_before_then_delivers_once)
{
    Mel_Future f;
    mel_future_init(&f, NULL, mel_alloc_heap());

    int payload = 7;
    MEL_REQUIRE(mel_future_resolve(&f, &payload, MEL_FUTURE_OK));

    Cont c;
    cont_init(&c, &f);
    mel_future_then(&f, &c.task, mel_executor_inline());

    MEL_EXPECT_EQ(c.ran, 1);
    MEL_EXPECT_EQ(c.seen_value, (void*)&payload);
    MEL_EXPECT_EQ(c.seen_status & MEL_FUTURE_SEVERITY_MASK, MEL_FUTURE_OK);
}

MEL_TEST(future, then_before_resolve_delivers_once)
{
    Mel_Future f;
    mel_future_init(&f, NULL, mel_alloc_heap());

    Cont c;
    cont_init(&c, &f);
    mel_future_then(&f, &c.task, mel_executor_inline());

    MEL_EXPECT_EQ(c.ran, 0);

    int payload = 11;
    MEL_REQUIRE(mel_future_resolve(&f, &payload, MEL_FUTURE_OK));

    MEL_EXPECT_EQ(c.ran, 1);
    MEL_EXPECT_EQ(c.seen_value, (void*)&payload);
}

MEL_TEST(future, resolved_query_reflects_state)
{
    Mel_Future f;
    mel_future_init(&f, NULL, mel_alloc_heap());
    MEL_EXPECT(!mel_future_resolved(&f));

    int v = 1;
    mel_future_resolve(&f, &v, MEL_FUTURE_OK);
    MEL_EXPECT(mel_future_resolved(&f));
    MEL_EXPECT_EQ(mel_future_value(&f), (void*)&v);
}

MEL_TEST(future, status_carries_severity_and_bits)
{
    Mel_Future f;
    mel_future_init(&f, NULL, mel_alloc_heap());

    int v = 0;
    mel_future_resolve(&f, &v, MEL_FUTURE_WARNED | MEL_FUTURE_PARTIAL);

    Mel_Future_Status s = mel_future_status(&f);
    MEL_EXPECT(mel_future_status_warned(s));
    MEL_EXPECT(!mel_future_status_failed(s));
    MEL_EXPECT((s & MEL_FUTURE_PARTIAL) != 0u);
}

MEL_TEST(future, cancel_resumes_with_cancelled_status)
{
    Mel_Future f;
    mel_future_init(&f, NULL, mel_alloc_heap());

    Cont c;
    cont_init(&c, &f);
    mel_future_then(&f, &c.task, mel_executor_inline());

    MEL_REQUIRE(mel_future_cancel(&f));

    MEL_EXPECT_EQ(c.ran, 1);
    MEL_EXPECT(mel_future_status_cancelled(c.seen_status));
    MEL_EXPECT(mel_future_status_failed(c.seen_status));
    MEL_EXPECT_EQ(c.seen_value, NULL);
}

MEL_TEST(future, cancel_before_then_resumes_cancelled)
{
    Mel_Future f;
    mel_future_init(&f, NULL, mel_alloc_heap());

    MEL_REQUIRE(mel_future_cancel(&f));

    Cont c;
    cont_init(&c, &f);
    mel_future_then(&f, &c.task, mel_executor_inline());

    MEL_EXPECT_EQ(c.ran, 1);
    MEL_EXPECT(mel_future_status_cancelled(c.seen_status));
}

MEL_TEST(future, resolve_after_cancel_is_noop)
{
    Mel_Future f;
    mel_future_init(&f, NULL, mel_alloc_heap());

    MEL_REQUIRE(mel_future_cancel(&f));

    int v = 5;
    MEL_EXPECT(!mel_future_resolve(&f, &v, MEL_FUTURE_OK));
    MEL_EXPECT(mel_future_status_cancelled(mel_future_status(&f)));
}

MEL_TEST(future, cancel_after_resolve_is_noop)
{
    Mel_Future f;
    mel_future_init(&f, NULL, mel_alloc_heap());

    int v = 5;
    MEL_REQUIRE(mel_future_resolve(&f, &v, MEL_FUTURE_OK));
    MEL_EXPECT(!mel_future_cancel(&f));
    MEL_EXPECT(!mel_future_status_cancelled(mel_future_status(&f)));
    MEL_EXPECT_EQ(mel_future_value(&f), (void*)&v);
}

MEL_TEST(future, double_resolve_second_is_noop)
{
    Mel_Future f;
    mel_future_init(&f, NULL, mel_alloc_heap());

    int a = 1, bb = 2;
    MEL_REQUIRE(mel_future_resolve(&f, &a, MEL_FUTURE_OK));
    MEL_EXPECT(!mel_future_resolve(&f, &bb, MEL_FUTURE_OK));
    MEL_EXPECT_EQ(mel_future_value(&f), (void*)&a);
}

static _Atomic(i64) g_live;
static _Atomic(i64) g_total_allocs;
static _Atomic(i64) g_total_frees;

static void* counting_cb(void* ptr, usize size, u32 align, const char* file, const char* func, u32 line, void* user_data)
{
    const Mel_Alloc* heap = mel_alloc_heap();
    if (ptr == NULL)
    {
        void* p = heap->alloc_cb(NULL, size, align, file, func, line, heap->user_data);
        if (p)
        {
            atomic_fetch_add_explicit(&g_live, 1, memory_order_relaxed);
            atomic_fetch_add_explicit(&g_total_allocs, 1, memory_order_relaxed);
        }
        return p;
    }
    if (size == 0)
    {
        heap->alloc_cb(ptr, 0, align, file, func, line, heap->user_data);
        atomic_fetch_sub_explicit(&g_live, 1, memory_order_relaxed);
        atomic_fetch_add_explicit(&g_total_frees, 1, memory_order_relaxed);
        return NULL;
    }
    return heap->alloc_cb(ptr, size, align, file, func, line, heap->user_data);
}

static _Atomic(i64) g_value_frees;

static void free_counting_value(void* value, const Mel_Alloc* alloc)
{
    atomic_fetch_add_explicit(&g_value_frees, 1, memory_order_relaxed);
    mel_dealloc(alloc, value);
}

MEL_TEST(future, cancel_races_resolve_one_wins_value_freed_once)
{
    atomic_store(&g_live, 0);
    atomic_store(&g_value_frees, 0);

    Mel_Alloc counting = { .alloc_cb = counting_cb, .user_data = NULL };

    Mel_Future f;
    mel_future_init(&f, free_counting_value, &counting);

    int* payload = mel_alloc_type(&counting, int);
    *payload = 99;

    bool resolved = mel_future_resolve(&f, payload, MEL_FUTURE_OK);
    bool cancelled = mel_future_cancel(&f);

    MEL_EXPECT(resolved != cancelled);

    if (resolved)
    {
        MEL_EXPECT_EQ((i64)atomic_load(&g_value_frees), (i64)0);
        free_counting_value(mel_future_value(&f), &counting);
    }
    else
    {
        MEL_EXPECT_EQ((i64)atomic_load(&g_value_frees), (i64)1);
    }

    MEL_EXPECT_EQ((i64)atomic_load(&g_live), (i64)0);
}

MEL_TEST(future, resolve_loser_frees_value_no_double_free)
{
    atomic_store(&g_live, 0);
    atomic_store(&g_value_frees, 0);

    Mel_Alloc counting = { .alloc_cb = counting_cb, .user_data = NULL };

    Mel_Future f;
    mel_future_init(&f, free_counting_value, &counting);

    int* winner = mel_alloc_type(&counting, int);
    int* loser = mel_alloc_type(&counting, int);

    MEL_REQUIRE(mel_future_resolve(&f, winner, MEL_FUTURE_OK));
    MEL_EXPECT(!mel_future_resolve(&f, loser, MEL_FUTURE_OK));

    MEL_EXPECT_EQ((i64)atomic_load(&g_value_frees), (i64)1);
    MEL_EXPECT_EQ(mel_future_value(&f), (void*)winner);

    free_counting_value(winner, &counting);
    MEL_EXPECT_EQ((i64)atomic_load(&g_live), (i64)0);
}

MEL_TEST(future, resolve_with_no_continuation_zero_alloc)
{
    atomic_store(&g_live, 0);
    atomic_store(&g_total_allocs, 0);

    Mel_Alloc counting = { .alloc_cb = counting_cb, .user_data = NULL };

    Mel_Future f;
    mel_future_init(&f, NULL, &counting);

    int v = 3;
    mel_future_resolve(&f, &v, MEL_FUTURE_OK);

    MEL_EXPECT_EQ((i64)atomic_load(&g_total_allocs), (i64)0);
}

MEL_TEST(future, resolve_with_continuation_zero_alloc)
{
    atomic_store(&g_total_allocs, 0);

    Mel_Alloc counting = { .alloc_cb = counting_cb, .user_data = NULL };

    Mel_Future f;
    mel_future_init(&f, NULL, &counting);

    Cont c;
    cont_init(&c, &f);
    mel_future_then(&f, &c.task, mel_executor_inline());

    int v = 3;
    mel_future_resolve(&f, &v, MEL_FUTURE_OK);

    MEL_EXPECT_EQ(c.ran, 1);
    MEL_EXPECT_EQ((i64)atomic_load(&g_total_allocs), (i64)0);
}

MEL_TEST(future, when_all_resolves_after_every_child)
{
    Mel_Future a, b, c;
    mel_future_init(&a, NULL, mel_alloc_heap());
    mel_future_init(&b, NULL, mel_alloc_heap());
    mel_future_init(&c, NULL, mel_alloc_heap());

    Mel_Future*      inputs[3] = { &a, &b, &c };
    Mel_Future_When* w = mel_future_when_all(inputs, 3, mel_alloc_heap());
    Mel_Future*      agg = mel_future_when_future(w);

    Cont done;
    cont_init(&done, agg);
    mel_future_then(agg, &done.task, mel_executor_inline());

    int x = 1;
    mel_future_resolve(&a, &x, MEL_FUTURE_OK);
    MEL_EXPECT_EQ(done.ran, 0);
    mel_future_resolve(&b, &x, MEL_FUTURE_OK);
    MEL_EXPECT_EQ(done.ran, 0);
    mel_future_resolve(&c, &x, MEL_FUTURE_OK);
    MEL_EXPECT_EQ(done.ran, 1);

    MEL_EXPECT_EQ(mel_future_status(agg) & MEL_FUTURE_SEVERITY_MASK, MEL_FUTURE_OK);

    mel_future_when_free(w);
}

MEL_TEST(future, when_all_aggregates_worst_severity_and_partial)
{
    Mel_Future a, b;
    mel_future_init(&a, NULL, mel_alloc_heap());
    mel_future_init(&b, NULL, mel_alloc_heap());

    Mel_Future*      inputs[2] = { &a, &b };
    Mel_Future_When* w = mel_future_when_all(inputs, 2, mel_alloc_heap());
    Mel_Future*      agg = mel_future_when_future(w);

    int x = 1;
    mel_future_resolve(&a, &x, MEL_FUTURE_OK);
    mel_future_cancel(&b);

    MEL_REQUIRE(mel_future_resolved(agg));
    Mel_Future_Status s = mel_future_status(agg);
    MEL_EXPECT(mel_future_status_failed(s));
    MEL_EXPECT((s & MEL_FUTURE_PARTIAL) != 0u);

    mel_future_when_free(w);
}

MEL_TEST(future, when_all_empty_resolves_immediately)
{
    Mel_Future_When* w = mel_future_when_all(NULL, 0, mel_alloc_heap());
    Mel_Future*      agg = mel_future_when_future(w);
    MEL_EXPECT(mel_future_resolved(agg));
    MEL_EXPECT_EQ(mel_future_status(agg) & MEL_FUTURE_SEVERITY_MASK, MEL_FUTURE_OK);
    mel_future_when_free(w);
}

MEL_TEST(future, when_any_resolves_on_first_child)
{
    Mel_Future a, b, c;
    mel_future_init(&a, NULL, mel_alloc_heap());
    mel_future_init(&b, NULL, mel_alloc_heap());
    mel_future_init(&c, NULL, mel_alloc_heap());

    Mel_Future*      inputs[3] = { &a, &b, &c };
    Mel_Future_When* w = mel_future_when_any(inputs, 3, mel_alloc_heap());
    Mel_Future*      agg = mel_future_when_future(w);

    Cont done;
    cont_init(&done, agg);
    mel_future_then(agg, &done.task, mel_executor_inline());

    int x = 42;
    mel_future_resolve(&b, &x, MEL_FUTURE_OK);
    MEL_EXPECT_EQ(done.ran, 1);
    MEL_EXPECT_EQ(mel_future_value(agg), (void*)&x);

    int y = 7;
    mel_future_resolve(&a, &y, MEL_FUTURE_OK);
    mel_future_resolve(&c, &y, MEL_FUTURE_OK);
    MEL_EXPECT_EQ(done.ran, 1);
    MEL_EXPECT_EQ(mel_future_value(agg), (void*)&x);

    mel_future_when_free(w);
}

MEL_TEST(future, when_any_first_cancel_propagates_cancel)
{
    Mel_Future a, b;
    mel_future_init(&a, NULL, mel_alloc_heap());
    mel_future_init(&b, NULL, mel_alloc_heap());

    Mel_Future*      inputs[2] = { &a, &b };
    Mel_Future_When* w = mel_future_when_any(inputs, 2, mel_alloc_heap());
    Mel_Future*      agg = mel_future_when_future(w);

    mel_future_cancel(&a);
    MEL_REQUIRE(mel_future_resolved(agg));
    MEL_EXPECT(mel_future_status_cancelled(mel_future_status(agg)));

    mel_future_when_free(w);
}

MEL_TEST(future, when_any_input_already_resolved_at_build)
{
    Mel_Future a, b;
    mel_future_init(&a, NULL, mel_alloc_heap());
    mel_future_init(&b, NULL, mel_alloc_heap());

    int x = 314;
    mel_future_resolve(&a, &x, MEL_FUTURE_OK);

    Mel_Future*      inputs[2] = { &a, &b };
    Mel_Future_When* w = mel_future_when_any(inputs, 2, mel_alloc_heap());
    Mel_Future*      agg = mel_future_when_future(w);

    MEL_REQUIRE(mel_future_resolved(agg));
    MEL_EXPECT_EQ(mel_future_value(agg), (void*)&x);
    MEL_EXPECT_EQ(mel_future_status(agg) & MEL_FUTURE_SEVERITY_MASK, MEL_FUTURE_OK);

    int y = 1;
    mel_future_resolve(&b, &y, MEL_FUTURE_OK);
    MEL_EXPECT_EQ(mel_future_value(agg), (void*)&x);

    mel_future_when_free(w);
}

MEL_TEST(future, when_all_input_already_terminal_at_build)
{
    Mel_Future a, b;
    mel_future_init(&a, NULL, mel_alloc_heap());
    mel_future_init(&b, NULL, mel_alloc_heap());

    int x = 1;
    mel_future_resolve(&a, &x, MEL_FUTURE_OK);
    mel_future_resolve(&b, &x, MEL_FUTURE_OK);

    Mel_Future*      inputs[2] = { &a, &b };
    Mel_Future_When* w = mel_future_when_all(inputs, 2, mel_alloc_heap());
    Mel_Future*      agg = mel_future_when_future(w);

    MEL_REQUIRE(mel_future_resolved(agg));
    MEL_EXPECT_EQ(mel_future_status(agg) & MEL_FUTURE_SEVERITY_MASK, MEL_FUTURE_OK);

    mel_future_when_free(w);
}

MEL_TEST(future, then_after_cancel_delivers_immediately_cancelled)
{
    Mel_Future f;
    mel_future_init(&f, NULL, mel_alloc_heap());

    MEL_REQUIRE(mel_future_cancel(&f));

    Cont c;
    cont_init(&c, &f);
    mel_future_then(&f, &c.task, mel_executor_inline());

    MEL_EXPECT_EQ(c.ran, 1);
    MEL_EXPECT(mel_future_status_cancelled(c.seen_status));
}

MEL_TEST(future, when_empty_no_leak)
{
    atomic_store(&g_live, 0);
    Mel_Alloc counting = { .alloc_cb = counting_cb, .user_data = NULL };

    Mel_Future_When* w = mel_future_when_all(NULL, 0, &counting);
    mel_future_when_free(w);

    MEL_EXPECT_EQ((i64)atomic_load(&g_live), (i64)0);
}

MEL_TEST(future, when_all_no_leak)
{
    atomic_store(&g_live, 0);
    Mel_Alloc counting = { .alloc_cb = counting_cb, .user_data = NULL };

    Mel_Future a, b;
    mel_future_init(&a, NULL, &counting);
    mel_future_init(&b, NULL, &counting);

    Mel_Future*      inputs[2] = { &a, &b };
    Mel_Future_When* w = mel_future_when_all(inputs, 2, &counting);

    int x = 1;
    mel_future_resolve(&a, &x, MEL_FUTURE_OK);
    mel_future_resolve(&b, &x, MEL_FUTURE_OK);

    mel_future_when_free(w);
    MEL_EXPECT_EQ((i64)atomic_load(&g_live), (i64)0);
}

MEL_TEST(future, scope_teardown_cancels_children_woken_cancelled)
{
    Mel_Future a, b;
    mel_future_init(&a, NULL, mel_alloc_heap());
    mel_future_init(&b, NULL, mel_alloc_heap());

    Cont ca, cb;
    cont_init(&ca, &a);
    cont_init(&cb, &b);
    mel_future_then(&a, &ca.task, mel_executor_inline());
    mel_future_then(&b, &cb.task, mel_executor_inline());

    Mel_Future_Scope scope;
    mel_future_scope_init(&scope, mel_alloc_heap());
    mel_future_scope_adopt(&scope, &a);
    mel_future_scope_adopt(&scope, &b);

    mel_future_scope_teardown(&scope);

    MEL_EXPECT_EQ(ca.ran, 1);
    MEL_EXPECT_EQ(cb.ran, 1);
    MEL_EXPECT(mel_future_status_cancelled(ca.seen_status));
    MEL_EXPECT(mel_future_status_cancelled(cb.seen_status));
}

MEL_TEST(future, scope_cancel_leaves_resolved_children_intact)
{
    Mel_Future a, b;
    mel_future_init(&a, NULL, mel_alloc_heap());
    mel_future_init(&b, NULL, mel_alloc_heap());

    int v = 8;
    mel_future_resolve(&a, &v, MEL_FUTURE_OK);

    Mel_Future_Scope scope;
    mel_future_scope_init(&scope, mel_alloc_heap());
    mel_future_scope_adopt(&scope, &a);
    mel_future_scope_adopt(&scope, &b);

    mel_future_scope_cancel(&scope);

    MEL_EXPECT(!mel_future_status_cancelled(mel_future_status(&a)));
    MEL_EXPECT(mel_future_status_cancelled(mel_future_status(&b)));

    mel_future_scope_teardown(&scope);
}

MEL_TEST(future, second_then_asserts)
{
#ifdef _WIN32
    MEL_SKIP("death test needs fork");
#else
    fflush(NULL);
    pid_t pid = fork();
    MEL_REQUIRE(pid >= 0);
    if (pid == 0)
    {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0)
            dup2(devnull, STDERR_FILENO);

        Mel_Future f;
        mel_future_init(&f, NULL, mel_alloc_heap());

        Cont a, b;
        cont_init(&a, &f);
        cont_init(&b, &f);
        mel_future_then(&f, &a.task, mel_executor_inline());
        mel_future_then(&f, &b.task, mel_executor_inline());
        _exit(0);
    }

    int st = 0;
    while (waitpid(pid, &st, 0) < 0)
    {
    }
    MEL_EXPECT(WIFSIGNALED(st));
    MEL_EXPECT(!(WIFEXITED(st) && WEXITSTATUS(st) == 0));
#endif
}

MEL_TEST(future, scope_teardown_before_executor_no_leaked_task)
{
    Mel_Future a;
    mel_future_init(&a, NULL, mel_alloc_heap());

    Cont c;
    cont_init(&c, &a);
    mel_future_then(&a, &c.task, mel_executor_inline());

    Mel_Future_Scope scope;
    mel_future_scope_init(&scope, mel_alloc_heap());
    mel_future_scope_adopt(&scope, &a);

    mel_future_scope_teardown(&scope);

    MEL_EXPECT_EQ(c.ran, 1);
    MEL_EXPECT(mel_future_status_cancelled(c.seen_status));
    MEL_EXPECT(atomic_load_explicit(&c.task.armed, memory_order_acquire) == 0);
}

MEL_TEST(future, settler_resolve_wins_cancel_loser_then_before)
{
    atomic_store(&g_live, 0);
    atomic_store(&g_value_frees, 0);
    Mel_Alloc counting = { .alloc_cb = counting_cb, .user_data = NULL };

    Mel_Future f;
    mel_future_init(&f, free_counting_value, &counting);

    Cont c;
    cont_init(&c, &f);
    mel_future_then(&f, &c.task, mel_executor_inline());

    int* payload = mel_alloc_type(&counting, int);
    *payload = 1234;

    bool won_resolve = mel_future_resolve(&f, payload, MEL_FUTURE_OK);
    bool won_cancel = mel_future_cancel(&f);

    MEL_EXPECT(won_resolve);
    MEL_EXPECT(!won_cancel);
    MEL_EXPECT_EQ(c.ran, 1);
    MEL_EXPECT(!mel_future_status_cancelled(c.seen_status));
    MEL_EXPECT_EQ(c.seen_value, (void*)payload);
    MEL_EXPECT_EQ((i64)atomic_load(&g_value_frees), (i64)0);

    free_counting_value(payload, &counting);
    MEL_EXPECT_EQ((i64)atomic_load(&g_live), (i64)0);
}

MEL_TEST(future, settler_cancel_wins_resolve_loser_frees_once_then_before)
{
    atomic_store(&g_live, 0);
    atomic_store(&g_value_frees, 0);
    Mel_Alloc counting = { .alloc_cb = counting_cb, .user_data = NULL };

    Mel_Future f;
    mel_future_init(&f, free_counting_value, &counting);

    Cont c;
    cont_init(&c, &f);
    mel_future_then(&f, &c.task, mel_executor_inline());

    int* payload = mel_alloc_type(&counting, int);
    *payload = 4321;

    bool won_cancel = mel_future_cancel(&f);
    bool won_resolve = mel_future_resolve(&f, payload, MEL_FUTURE_OK);

    MEL_EXPECT(won_cancel);
    MEL_EXPECT(!won_resolve);
    MEL_EXPECT_EQ(c.ran, 1);
    MEL_EXPECT(mel_future_status_cancelled(c.seen_status));
    MEL_EXPECT_EQ(c.seen_value, NULL);
    MEL_EXPECT_EQ((i64)atomic_load(&g_value_frees), (i64)1);
    MEL_EXPECT_EQ((i64)atomic_load(&g_live), (i64)0);
}

MEL_TEST(future, settler_cancel_wins_resolve_loser_frees_once_then_after)
{
    atomic_store(&g_live, 0);
    atomic_store(&g_value_frees, 0);
    Mel_Alloc counting = { .alloc_cb = counting_cb, .user_data = NULL };

    Mel_Future f;
    mel_future_init(&f, free_counting_value, &counting);

    int* payload = mel_alloc_type(&counting, int);
    *payload = 777;

    bool won_cancel = mel_future_cancel(&f);
    bool won_resolve = mel_future_resolve(&f, payload, MEL_FUTURE_OK);

    MEL_EXPECT(won_cancel);
    MEL_EXPECT(!won_resolve);
    MEL_EXPECT_EQ((i64)atomic_load(&g_value_frees), (i64)1);

    Cont c;
    cont_init(&c, &f);
    mel_future_then(&f, &c.task, mel_executor_inline());

    MEL_EXPECT_EQ(c.ran, 1);
    MEL_EXPECT(mel_future_status_cancelled(c.seen_status));
    MEL_EXPECT_EQ(c.seen_value, NULL);
    MEL_EXPECT_EQ((i64)atomic_load(&g_live), (i64)0);
}

MEL_TEST(future, settler_cancel_writes_no_shared_payload_field)
{
    Mel_Future f;
    mel_future_init(&f, NULL, mel_alloc_heap());

    int sentinel = 0xBEEF;
    f.value = &sentinel;
    f.status = MEL_FUTURE_WARNED | MEL_FUTURE_BROKEN;

    MEL_REQUIRE(mel_future_cancel(&f));

    MEL_EXPECT_EQ(f.value, (void*)&sentinel);
    MEL_EXPECT_EQ(f.status, (Mel_Future_Status)(MEL_FUTURE_WARNED | MEL_FUTURE_BROKEN));

    MEL_EXPECT_EQ(mel_future_value(&f), NULL);
    MEL_EXPECT_EQ(mel_future_status(&f), (Mel_Future_Status)(MEL_FUTURE_ERROR | MEL_FUTURE_CANCELLED));
}
