#include <test/test.h>

#include <window/window.h>
#include <window/state.h>

#include <allocator/heap.h>
#include <executor/executor.h>
#include <future/future.h>
#include <collection.mpsc/mpsc.h>
#include <collection.list/list.h>

#include <stdatomic.h>
#include <string.h>

MEL_TEST(window_state, status_predicates)
{
    MEL_EXPECT(mel_window_status_ok(MEL_WINDOW_OK));
    MEL_EXPECT(mel_window_status_failed(MEL_WINDOW_ERROR | MEL_WINDOW_INVALID_HANDLE));
    MEL_EXPECT(mel_window_status_warned(MEL_WINDOW_WARNED | MEL_WINDOW_UNAVAILABLE));
    MEL_EXPECT(mel_window_status_unavailable(MEL_WINDOW_WARNED | MEL_WINDOW_UNAVAILABLE));
    MEL_EXPECT(mel_window_status_clamped(MEL_WINDOW_WARNED | MEL_WINDOW_CLAMPED));
    MEL_EXPECT(!mel_window_status_failed(MEL_WINDOW_WARNED));
}

MEL_TEST(window_state, op_handle_validity)
{
    MEL_EXPECT(!mel_window_op_valid(MEL_WINDOW_OP_NULL));
    Mel_Window_Op op = { .index = 4, .generation = 1 };
    MEL_EXPECT(mel_window_op_valid(op));
}

MEL_TEST(window_state, dead_handle_is_loud_not_fatal)
{
    Mel_Window bogus = { .index = 4242, .generation = 9 };
    MEL_EXPECT(!mel_window_alive(bogus));

    Mel_Window_Status s = mel_window_set_opacity(bogus, 0.5f);
    MEL_EXPECT(mel_window_status_failed(s));
    MEL_EXPECT((s & MEL_WINDOW_INVALID_HANDLE) != 0u);

    Mel_Window_State_Result r = mel_window_query_state(bogus);
    MEL_EXPECT(mel_window_status_failed(r.status));

    Mel_Window_Surface_Result sr = mel_window_get_surface(bogus);
    MEL_EXPECT(mel_window_status_failed(sr.status));

    Mel_Window_Icc_Result ir = mel_window_icc_profile(bogus);
    MEL_EXPECT(mel_window_status_failed(ir.status));
}

MEL_TEST(window_state, enumerate_empty_is_zero)
{
    mel_window_init(NULL);
    Mel_Window buf[8];
    u32        n = mel_window_enumerate_all(buf, 8);
    MEL_EXPECT_EQ(n, 0u);
    MEL_EXPECT(mel_window_is_none(mel_window_by_id(123456)));
    mel_window_shutdown();
}

MEL_TEST(window_state, live_window_ops_round_trip)
{
    mel_window_init(NULL);

    Mel_Window w = mel_window_create(.title = S8("state test"), .w = 320, .h = 240, .start_hidden = true);
    MEL_REQUIRE(mel_window_alive(w));

    Mel_Window_Status so = mel_window_set_opacity(w, 0.5f);
    MEL_EXPECT(!mel_window_status_failed(so));
    MEL_EXPECT_FLOAT_EQ(mel_window_get_opacity(w), 0.5f, 0.001f);

    Mel_Window_Status soc = mel_window_set_opacity(w, 2.0f);
    MEL_EXPECT(mel_window_status_clamped(soc));
    MEL_EXPECT_FLOAT_EQ(mel_window_get_opacity(w), 1.0f, 0.001f);

    Mel_Window_Status sa = mel_window_set_aspect_ratio(w, 2.0f, 1.0f);
    MEL_EXPECT(mel_window_status_failed(sa));
    MEL_EXPECT((sa & MEL_WINDOW_REJECTED) != 0u);

    MEL_EXPECT(!mel_window_status_failed(mel_window_set_always_on_top(w, true)));
    MEL_EXPECT(!mel_window_status_failed(mel_window_set_min_size(w, 100, 100)));
    MEL_EXPECT(!mel_window_status_failed(mel_window_set_progress_value(w, 0.3f)));

    Mel_Window_State_Result r = mel_window_query_state(w);
    MEL_EXPECT(!mel_window_status_failed(r.status));
    MEL_EXPECT((r.value.flags & MEL_WINDOW_STATE_ALWAYS_TOP) != 0u);
    MEL_EXPECT_EQ(r.value.bounds_px.w, 320);

    u32 count = mel_window_enumerate_all(NULL, 0);
    MEL_EXPECT_EQ(count, 1u);

    Mel_Window listed[4] = { 0 };
    u32        got = mel_window_enumerate_all(listed, 4);
    MEL_EXPECT_EQ(got, 1u);
    MEL_EXPECT(mel_window_eq(listed[0], w));

    mel_window_destroy(w);
    mel_window_shutdown();
}

MEL_TEST(window_state, surface_present_round_trip)
{
    mel_window_init(NULL);
    Mel_Window w = mel_window_create(.title = S8("surf"), .w = 64, .h = 48, .start_hidden = true);
    MEL_REQUIRE(mel_window_alive(w));

    Mel_Window_Surface_Result sr = mel_window_get_surface(w);
    MEL_EXPECT(!mel_window_status_failed(sr.status));
    MEL_REQUIRE_NOT_NULL(sr.value.pixels);
    MEL_EXPECT_EQ(sr.value.width_px, 64);
    MEL_EXPECT_EQ(sr.value.height_px, 48);
    MEL_EXPECT_EQ(sr.value.stride_bytes, 64 * 4);

    memset(sr.value.pixels, 0x7F, (usize)sr.value.stride_bytes * (usize)sr.value.height_px);
    Mel_Window_Status ps = mel_window_present_surface(w);
    MEL_EXPECT(!mel_window_status_failed(ps));

    mel_window_destroy(w);
    mel_window_shutdown();
}

typedef struct
{
    Mel_Executor   base;
    Mel_Mpsc_Node* head;
    Mel_Mpsc_Node* tail;
} Deferred_Executor;

static void deferred_submit(Mel_Executor* self, Mel_Task* task)
{
    Deferred_Executor* d = (Deferred_Executor*)self;
    atomic_store_explicit(&task->link.next, NULL, memory_order_relaxed);
    if (d->tail != NULL)
        atomic_store_explicit(&d->tail->next, &task->link, memory_order_relaxed);
    else
        d->head = &task->link;
    d->tail = &task->link;
}

static u32 deferred_drain(Deferred_Executor* d)
{
    u32 ran = 0;
    while (d->head != NULL)
    {
        Mel_Mpsc_Node* node = d->head;
        Mel_Task*      task = mel_container_of(node, Mel_Task, link);
        d->head = atomic_load_explicit(&node->next, memory_order_relaxed);
        if (d->head == NULL)
            d->tail = NULL;
        atomic_store_explicit(&node->next, NULL, memory_order_relaxed);
        atomic_store_explicit(&task->armed, 0, memory_order_release);
        task->run(task);
        ran++;
    }
    return ran;
}

MEL_TEST(window_state, async_icc_fetch_delivers_on_executor)
{
    mel_window_init(NULL);
    Mel_Window w = mel_window_create(.title = S8("icc"), .w = 128, .h = 96, .start_hidden = true);
    MEL_REQUIRE(mel_window_alive(w));

    Deferred_Executor def = { .base = { deferred_submit } };
    Mel_Window_Op     op = MEL_WINDOW_OP_NULL;
    Mel_Future*       f = mel_window_fetch_icc(w, .deliver = &def.base, .out_op = &op);
    MEL_REQUIRE_NOT_NULL(f);
    MEL_EXPECT(mel_window_op_valid(op));
    MEL_EXPECT(!mel_future_resolved(f));

    u32 ran = deferred_drain(&def);
    MEL_EXPECT_GE(ran, 1u);
    MEL_EXPECT(mel_future_resolved(f));

    const Mel_Window_Icc_Result* r = mel_window_icc_future_result(f);
    MEL_REQUIRE_NOT_NULL(r);
    if (!mel_window_status_failed(r->status) && !mel_window_status_unavailable(r->status))
    {
        MEL_EXPECT_NOT_NULL(r->value.data);
        MEL_EXPECT_GT(r->value.size, 0u);
        mel_dealloc(mel_alloc_heap(), (void*)r->value.data);
    }

    mel_window_icc_future_release(f);
    mel_window_destroy(w);
    mel_window_shutdown();
}

MEL_TEST(window_state, async_icc_cancel_marks_cancelled)
{
    mel_window_init(NULL);
    Mel_Window w = mel_window_create(.title = S8("icc2"), .w = 100, .h = 100, .start_hidden = true);
    MEL_REQUIRE(mel_window_alive(w));

    Deferred_Executor def = { .base = { deferred_submit } };
    Mel_Window_Op     op = MEL_WINDOW_OP_NULL;
    Mel_Future*       f = mel_window_fetch_icc(w, .deliver = &def.base, .out_op = &op);
    MEL_REQUIRE_NOT_NULL(f);

    MEL_EXPECT(mel_window_cancel(op));

    deferred_drain(&def);
    MEL_EXPECT(mel_future_resolved(f));
    const Mel_Window_Icc_Result* r = mel_window_icc_future_result(f);
    MEL_EXPECT((r->status & MEL_WINDOW_CANCELLED) != 0u);
    if (!mel_window_status_failed(r->status) && r->value.data)
        mel_dealloc(mel_alloc_heap(), (void*)r->value.data);

    mel_window_icc_future_release(f);
    mel_window_destroy(w);
    mel_window_shutdown();
}
