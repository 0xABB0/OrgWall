#include <test/test.h>
#include <display/display.h>
#include <display/events.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <executor/executor.h>
#include <collection/list.h>

#include <stdatomic.h>
#include <string.h>

#include "../src/display_backend.h"

MEL_TEST(display, dead_handle_is_loud_not_fatal)
{
    Mel_Display bogus = { .h = { .index = 9999, .generation = 7 } };
    MEL_EXPECT(!mel_display_alive(bogus));

    Mel_Display_Describe_Result r = mel_display_describe(bogus);
    MEL_EXPECT_EQ(r.status, MEL_DISPLAY_STATUS_INVALID_HANDLE);
}

MEL_TEST(display, null_handle_is_dead)
{
    Mel_Display null = MEL_DISPLAY_NULL;
    MEL_EXPECT(!mel_display_alive(null));
    MEL_EXPECT(mel_display_equal(null, null));
}

MEL_TEST(display, equal_compares_index_and_generation)
{
    Mel_Display a = { .h = { .index = 3, .generation = 1 } };
    Mel_Display b = { .h = { .index = 3, .generation = 2 } };
    Mel_Display c = { .h = { .index = 3, .generation = 1 } };
    MEL_EXPECT(!mel_display_equal(a, b));
    MEL_EXPECT(mel_display_equal(a, c));
}

typedef struct
{
    u64               stable_id;
    u32               width_px;
    u32               height_px;
    Mel_Display_State state;
} Fake_Display;

static struct
{
    Fake_Display items[8];
    u32          count;
} g_fake;

static u32 fake_enumerate(const Mel_Alloc* alloc, Mel_Display_Raw* out, u32 cap)
{
    (void)alloc;
    u32 n = g_fake.count < cap ? g_fake.count : cap;
    for (u32 i = 0; i < n; i++)
    {
        memset(&out[i], 0, sizeof out[i]);
        out[i].stable_id = g_fake.items[i].stable_id;
        out[i].desc.native_resolution.width_px = g_fake.items[i].width_px;
        out[i].desc.native_resolution.height_px = g_fake.items[i].height_px;
        out[i].desc.scale_factor = 1.0f;
        out[i].desc.state = g_fake.items[i].state;
    }
    return n;
}

static void fake_clear(void) { g_fake.count = 0; }

static void fake_add(u64 id, u32 w, u32 h) { g_fake.items[g_fake.count++] = (Fake_Display){ .stable_id = id, .width_px = w, .height_px = h, .state = MEL_DISPLAY_STATE_ACTIVE }; }

static void fake_set_state(u64 id, Mel_Display_State state)
{
    for (u32 i = 0; i < g_fake.count; i++)
    {
        if (g_fake.items[i].stable_id == id)
        {
            g_fake.items[i].state = state;
            return;
        }
    }
}

static void fake_remove(u64 id)
{
    for (u32 i = 0; i < g_fake.count; i++)
    {
        if (g_fake.items[i].stable_id == id)
        {
            g_fake.items[i] = g_fake.items[g_fake.count - 1];
            g_fake.count--;
            return;
        }
    }
}

static void fake_resize(u64 id, u32 w, u32 h)
{
    for (u32 i = 0; i < g_fake.count; i++)
    {
        if (g_fake.items[i].stable_id == id)
        {
            g_fake.items[i].width_px = w;
            g_fake.items[i].height_px = h;
            return;
        }
    }
}

static u32 drain_kind(Mel_Display_Event_Kind want)
{
    Mel_Display_Event ev[32];
    u32               got = mel_display_poll_events(ev, 32);
    u32               match = 0;
    for (u32 i = 0; i < got; i++)
        if (ev[i].kind == want)
            match++;
    return match;
}

MEL_TEST(display, pull_face_returns_diffed_events)
{
    fake_clear();
    mel_display__set_enumerate(fake_enumerate);
    mel_display_init(mel_alloc_heap());

    Mel_Display_Event drain[32];
    mel_display_poll_events(drain, 32);

    fake_add(1, 1920, 1080);
    fake_add(2, 2560, 1440);
    mel_display_refresh();
    MEL_EXPECT_EQ(mel_display_count(), 2u);
    MEL_EXPECT_EQ(drain_kind(MEL_DISPLAY_EVENT_ADDED), 2u);

    fake_resize(1, 3840, 2160);
    mel_display_refresh();
    MEL_EXPECT_EQ(drain_kind(MEL_DISPLAY_EVENT_CONFIGURATION_CHANGED), 1u);

    fake_remove(2);
    mel_display_refresh();
    MEL_EXPECT_EQ(mel_display_count(), 1u);
    MEL_EXPECT_EQ(drain_kind(MEL_DISPLAY_EVENT_REMOVED), 1u);

    mel_display_shutdown();
}

typedef struct
{
    u32 added;
    u32 removed;
    u32 changed;
} Push_Sink;

static void push_cb(const Mel_Display_Event* ev, void* user)
{
    Push_Sink* s = (Push_Sink*)user;
    switch (ev->kind)
    {
    case MEL_DISPLAY_EVENT_ADDED:
        s->added++;
        break;
    case MEL_DISPLAY_EVENT_REMOVED:
        s->removed++;
        break;
    case MEL_DISPLAY_EVENT_CONFIGURATION_CHANGED:
    case MEL_DISPLAY_EVENT_POWER_STATE_CHANGED:
        s->changed++;
        break;
    }
}

MEL_TEST(display, push_face_delivers_on_refresh)
{
    fake_clear();
    mel_display__set_enumerate(fake_enumerate);
    mel_display_init_ex(mel_alloc_heap(), mel_executor_inline());

    Mel_Display_Event drain[32];
    mel_display_poll_events(drain, 32);

    Push_Sink                s = { 0 };
    Mel_Display_Subscription sub = mel_display_subscribe(mel_executor_inline(), push_cb, &s);
    MEL_REQUIRE(mel_slotmap_handle_valid(sub.handle));

    fake_add(1, 1920, 1080);
    fake_add(2, 2560, 1440);
    mel_display_refresh();
    MEL_EXPECT_EQ(s.added, 2u);

    fake_resize(1, 3840, 2160);
    mel_display_refresh();
    MEL_EXPECT_EQ(s.changed, 1u);

    fake_remove(2);
    mel_display_refresh();
    MEL_EXPECT_EQ(s.removed, 1u);

    mel_display_unsubscribe(sub);
    mel_display_shutdown();
}

MEL_TEST(display, both_faces_observe_the_same_fire)
{
    fake_clear();
    mel_display__set_enumerate(fake_enumerate);
    mel_display_init_ex(mel_alloc_heap(), mel_executor_inline());

    Mel_Display_Event drain[32];
    mel_display_poll_events(drain, 32);

    Push_Sink                s = { 0 };
    Mel_Display_Subscription sub = mel_display_subscribe(mel_executor_inline(), push_cb, &s);

    fake_add(7, 1280, 720);
    mel_display_refresh();

    MEL_EXPECT_EQ(s.added, 1u);
    MEL_EXPECT_EQ(drain_kind(MEL_DISPLAY_EVENT_ADDED), 1u);

    mel_display_unsubscribe(sub);
    mel_display_shutdown();
}

MEL_TEST(display, subscribe_without_executor_is_loud_null)
{
    fake_clear();
    mel_display__set_enumerate(fake_enumerate);
    mel_display_init(mel_alloc_heap());

    Push_Sink                s = { 0 };
    Mel_Display_Subscription sub = mel_display_subscribe(NULL, push_cb, &s);
    MEL_EXPECT(!mel_slotmap_handle_valid(sub.handle));

    mel_display_shutdown();
}

MEL_TEST(display, power_state_change_alone_is_power_event)
{
    fake_clear();
    mel_display__set_enumerate(fake_enumerate);
    mel_display_init(mel_alloc_heap());

    Mel_Display_Event drain[32];
    mel_display_poll_events(drain, 32);

    fake_add(1, 1920, 1080);
    mel_display_refresh();
    MEL_EXPECT_EQ(drain_kind(MEL_DISPLAY_EVENT_ADDED), 1u);

    fake_set_state(1, MEL_DISPLAY_STATE_DIMMED);
    mel_display_refresh();
    MEL_EXPECT_EQ(drain_kind(MEL_DISPLAY_EVENT_POWER_STATE_CHANGED), 1u);
    MEL_EXPECT_EQ(drain_kind(MEL_DISPLAY_EVENT_CONFIGURATION_CHANGED), 0u);

    mel_display_shutdown();
}

MEL_TEST(display, pull_face_overflow_drops_oldest)
{
    fake_clear();
    mel_display__set_enumerate(fake_enumerate);
    mel_display_init(mel_alloc_heap());

    Mel_Display_Event drain[256];
    mel_display_poll_events(drain, 256);

    fake_add(1, 1920, 1080);
    mel_display_refresh();
    mel_display_poll_events(drain, 256);

    for (u32 i = 0; i < 200; i++)
    {
        u32 w = 100u + i;
        fake_resize(1, w, 1080);
        mel_display_refresh();
    }

    u32 got = mel_display_poll_events(drain, 256);
    MEL_EXPECT_EQ(got, 128u);
    for (u32 i = 0; i < got; i++)
        MEL_EXPECT_EQ(drain[i].kind, MEL_DISPLAY_EVENT_CONFIGURATION_CHANGED);

    Mel_Display_Describe_Result r = mel_display_describe(drain[got - 1].display);
    MEL_EXPECT_EQ(r.value.native_resolution.width_px, 299u);

    mel_display_shutdown();
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

MEL_TEST(display, deferred_shutdown_drains_in_flight_without_uaf)
{
    fake_clear();
    mel_display__set_enumerate(fake_enumerate);

    Deferred_Executor def = { .base = { deferred_submit } };
    mel_display_init_ex(mel_alloc_heap(), &def.base);

    Mel_Display_Event drain[32];
    mel_display_poll_events(drain, 32);

    Push_Sink                s = { 0 };
    Mel_Display_Subscription sub = mel_display_subscribe(&def.base, push_cb, &s);
    MEL_REQUIRE(mel_slotmap_handle_valid(sub.handle));

    fake_add(1, 1920, 1080);
    fake_add(2, 2560, 1440);
    mel_display_refresh();

    MEL_EXPECT_EQ(s.added, 0u);

    mel_display_unsubscribe(sub);
    mel_display_shutdown();

    u32 ran = deferred_drain(&def);
    MEL_EXPECT_GE(ran, 1u);
    MEL_EXPECT_EQ(s.added, 2u);
}
