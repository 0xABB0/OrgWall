#include <test/test.h>

#include <hid/hid.h>
#include <hid/events.h>
#include <hid/provider.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <executor/executor.h>
#include <collection/slotmap.fwd.h>

#include <stdio.h>
#include <string.h>

#include "../src/hid_internal.h"

typedef struct
{
    u64  stable_id;
    u16  vid;
    u16  pid;
    bool present;
} Fake_Entry;

static struct
{
    Fake_Entry items[8];
    u32        count;
    bool       opened[8];
    u8         pending[8][64];
    usize      pending_len[8];
    bool       has_pending[8];
} g_fake;

static Fake_Entry* fake_lookup(u64 id)
{
    for (u32 i = 0; i < g_fake.count; i++)
        if (g_fake.items[i].stable_id == id)
            return &g_fake.items[i];
    return NULL;
}

static u32 fake_index(u64 id)
{
    for (u32 i = 0; i < g_fake.count; i++)
        if (g_fake.items[i].stable_id == id)
            return i;
    return 0;
}

static u32 fake_enumerate(void* user, Mel_Hid_Raw* out, u32 cap)
{
    (void)user;
    u32 n = 0;
    for (u32 i = 0; i < g_fake.count && n < cap; i++)
    {
        if (!g_fake.items[i].present)
            continue;
        Mel_Hid_Raw* r = &out[n++];
        memset(r, 0, sizeof *r);
        r->stable_id = g_fake.items[i].stable_id;
        r->desc.vendor_id = g_fake.items[i].vid;
        r->desc.product_id = g_fake.items[i].pid;
        r->desc.bus = MEL_HID_BUS_USB;
        r->desc.input_report_len = 8;
        snprintf(r->desc.product, MEL_HID_STRING_CAP, "fake-%llu", (unsigned long long)g_fake.items[i].stable_id);
    }
    return n;
}

static Mel_Hid_Status fake_open(void* user, u64 stable_id, Mel_Hid_Channel* out_channel)
{
    (void)user;
    Fake_Entry* e = fake_lookup(stable_id);
    if (!e)
        return MEL_HID_ERROR | MEL_HID_DEVICE_LOST;
    g_fake.opened[fake_index(stable_id)] = true;
    *out_channel = (Mel_Hid_Channel){ .value = (void*)(usize)stable_id, .fd = MEL_HID_NO_FD, .bus = MEL_HID_BUS_USB };
    return MEL_HID_OK;
}

static void fake_close(void* user, u64 stable_id, Mel_Hid_Channel channel)
{
    (void)user;
    (void)channel;
    g_fake.opened[fake_index(stable_id)] = false;
}

static Mel_Hid_Io_Result fake_write(void* user, Mel_Hid_Channel channel, const u8* data, usize len)
{
    (void)user;
    (void)channel;
    (void)data;
    return (Mel_Hid_Io_Result){ .bytes = len, .status = MEL_HID_OK };
}

static Mel_Hid_Io_Result fake_read(void* user, Mel_Hid_Channel channel, u8* out, usize cap, i32 timeout_ms)
{
    (void)user;
    u32 idx = fake_index((u64)(usize)channel.value);
    if (!g_fake.has_pending[idx])
    {
        if (timeout_ms == MEL_HID_TIMEOUT_POLL)
            return (Mel_Hid_Io_Result){ .status = MEL_HID_WOULD_BLOCK };
        return (Mel_Hid_Io_Result){ .status = MEL_HID_TIMED_OUT | MEL_HID_WARNED };
    }
    usize copy = g_fake.pending_len[idx] < cap ? g_fake.pending_len[idx] : cap;
    memcpy(out, g_fake.pending[idx], copy);
    g_fake.has_pending[idx] = false;
    return (Mel_Hid_Io_Result){ .bytes = copy, .status = MEL_HID_OK };
}

static Mel_Hid_Io_Result fake_get_feature(void* user, Mel_Hid_Channel channel, u8 report_id, u8* out, usize cap)
{
    (void)user;
    (void)channel;
    if (cap < 2)
        return (Mel_Hid_Io_Result){ .status = MEL_HID_ERROR };
    out[0] = report_id;
    out[1] = 0xAB;
    return (Mel_Hid_Io_Result){ .bytes = 2, .status = MEL_HID_OK };
}

static Mel_Hid_Io_Result fake_get_report_descriptor(void* user, Mel_Hid_Channel channel, u8* out, usize cap)
{
    (void)user;
    (void)channel;
    static const u8 desc[] = { 0x05, 0x01, 0x09, 0x06, 0xC0 };
    usize           copy = sizeof desc < cap ? sizeof desc : cap;
    memcpy(out, desc, copy);
    Mel_Hid_Status st = MEL_HID_OK;
    if (copy < sizeof desc)
        st |= MEL_HID_PARTIAL | MEL_HID_WARNED;
    return (Mel_Hid_Io_Result){ .bytes = sizeof desc, .status = st };
}

static Mel_Hid_Provider register_fake(void)
{
    Mel_Hid_Provider_Desc desc = {
        .name = "fake",
        .user = NULL,
        .enumerate = fake_enumerate,
        .open = fake_open,
        .close = fake_close,
        .write = fake_write,
        .read = fake_read,
        .get_feature = fake_get_feature,
        .get_report_descriptor = fake_get_report_descriptor,
    };
    return mel_hid_provider_register(&desc);
}

static void fake_reset(void) { memset(&g_fake, 0, sizeof g_fake); }

static void fake_add(u64 id, u16 vid, u16 pid) { g_fake.items[g_fake.count++] = (Fake_Entry){ .stable_id = id, .vid = vid, .pid = pid, .present = true }; }

static void fake_remove(u64 id)
{
    Fake_Entry* e = fake_lookup(id);
    if (e)
        e->present = false;
}

static u32 drain_kind(Mel_Hid_Event_Kind want)
{
    Mel_Hid_Event ev[32];
    u32           got = mel_hid_poll_events(ev, 32);
    u32           match = 0;
    for (u32 i = 0; i < got; i++)
        if (ev[i].kind == want)
            match++;
    return match;
}

MEL_TEST(hid, dead_handle_is_loud_not_fatal)
{
    Mel_Hid_Device bogus = { .h = { .index = 9999, .generation = 7 } };
    MEL_EXPECT(!mel_hid_alive(bogus));
    Mel_Hid_Describe_Result r = mel_hid_describe(bogus);
    MEL_EXPECT(mel_hid_failed(r.status));
    MEL_EXPECT((r.status & MEL_HID_INVALID_HANDLE) != 0u);
}

MEL_TEST(hid, null_handle_is_dead)
{
    Mel_Hid_Device null = MEL_HID_DEVICE_NULL;
    MEL_EXPECT(!mel_hid_alive(null));
    MEL_EXPECT(mel_hid_equal(null, null));
}

MEL_TEST(hid, enumerate_diff_pull)
{
    fake_reset();
    mel_hid__set_skip_host_providers(true);
    mel_hid_init(mel_alloc_heap());
    register_fake();

    Mel_Hid_Event drain[32];
    mel_hid_poll_events(drain, 32);

    fake_add(1, 0x046d, 0xc52b);
    fake_add(2, 0x054c, 0x05c4);
    mel_hid_refresh();
    MEL_EXPECT_EQ(mel_hid_count(), 2u);
    MEL_EXPECT_EQ(drain_kind(MEL_HID_EVENT_ADDED), 2u);

    u64 before = mel_hid_device_change_count();
    fake_remove(2);
    mel_hid_refresh();
    MEL_EXPECT_EQ(mel_hid_count(), 1u);
    MEL_EXPECT_EQ(drain_kind(MEL_HID_EVENT_REMOVED), 1u);
    MEL_EXPECT_GT(mel_hid_device_change_count(), before);

    mel_hid_shutdown();
}

MEL_TEST(hid, change_event_on_identity_shift)
{
    fake_reset();
    mel_hid__set_skip_host_providers(true);
    mel_hid_init(mel_alloc_heap());
    register_fake();

    Mel_Hid_Event drain[32];
    mel_hid_poll_events(drain, 32);

    fake_add(7, 0x1234, 0x0001);
    mel_hid_refresh();
    MEL_EXPECT_EQ(drain_kind(MEL_HID_EVENT_ADDED), 1u);

    fake_lookup(7)->pid = 0x0002;
    mel_hid_refresh();
    MEL_EXPECT_EQ(drain_kind(MEL_HID_EVENT_CHANGED), 1u);

    mel_hid_shutdown();
}

MEL_TEST(hid, open_io_roundtrip)
{
    fake_reset();
    mel_hid__set_skip_host_providers(true);
    mel_hid_init(mel_alloc_heap());
    register_fake();

    fake_add(42, 0x16c0, 0x05df);
    mel_hid_refresh();

    Mel_Hid_Device devs[4];
    u32            n = mel_hid_list(devs, 4);
    MEL_REQUIRE_EQ(n, 1u);
    Mel_Hid_Device d = devs[0];

    MEL_EXPECT(!mel_hid_is_open(d));
    MEL_EXPECT_EQ(mel_hid_open(d), MEL_HID_OK);
    MEL_EXPECT(mel_hid_is_open(d));

    u8                out[8] = { 0 };
    Mel_Hid_Io_Result wr = mel_hid_write(d, out, sizeof out);
    MEL_EXPECT(!mel_hid_failed(wr.status));
    MEL_EXPECT_EQ(wr.bytes, 8u);

    Mel_Hid_Io_Result poll = mel_hid_read(d, out, sizeof out, MEL_HID_TIMEOUT_POLL);
    MEL_EXPECT(mel_hid_would_block(poll.status));

    u32 idx = fake_index(42);
    g_fake.pending[idx][0] = 0x11;
    g_fake.pending[idx][1] = 0x22;
    g_fake.pending_len[idx] = 2;
    g_fake.has_pending[idx] = true;
    Mel_Hid_Io_Result rd = mel_hid_read(d, out, sizeof out, MEL_HID_TIMEOUT_POLL);
    MEL_EXPECT(!mel_hid_failed(rd.status));
    MEL_EXPECT_EQ(rd.bytes, 2u);
    MEL_EXPECT_EQ(out[0], 0x11);

    u8                feat[8] = { 0 };
    Mel_Hid_Io_Result gf = mel_hid_get_feature(d, 3, feat, sizeof feat);
    MEL_EXPECT(!mel_hid_failed(gf.status));
    MEL_EXPECT_EQ(feat[0], 3);
    MEL_EXPECT_EQ(feat[1], 0xAB);

    u8                rdesc[16] = { 0 };
    Mel_Hid_Io_Result rdsc = mel_hid_get_report_descriptor(d, rdesc, sizeof rdesc);
    MEL_EXPECT(!mel_hid_failed(rdsc.status));
    MEL_EXPECT_EQ(rdsc.bytes, 5u);
    MEL_EXPECT_EQ(rdesc[0], 0x05);

    mel_hid_close(d);
    MEL_EXPECT(!mel_hid_is_open(d));

    Mel_Hid_Io_Result closed = mel_hid_write(d, out, sizeof out);
    MEL_EXPECT(mel_hid_failed(closed.status));
    MEL_EXPECT((closed.status & MEL_HID_NOT_OPEN) != 0u);

    mel_hid_shutdown();
}

MEL_TEST(hid, report_descriptor_truncates_partial)
{
    fake_reset();
    mel_hid__set_skip_host_providers(true);
    mel_hid_init(mel_alloc_heap());
    register_fake();

    fake_add(99, 0x2341, 0x8036);
    mel_hid_refresh();
    Mel_Hid_Device devs[1];
    mel_hid_list(devs, 1);
    MEL_EXPECT_EQ(mel_hid_open(devs[0]), MEL_HID_OK);

    u8                small[3] = { 0 };
    Mel_Hid_Io_Result r = mel_hid_get_report_descriptor(devs[0], small, sizeof small);
    MEL_EXPECT(mel_hid_warned(r.status));
    MEL_EXPECT((r.status & MEL_HID_PARTIAL) != 0u);
    MEL_EXPECT_EQ(r.bytes, 5u);

    mel_hid_shutdown();
}

typedef struct
{
    u32 added;
    u32 removed;
    u32 changed;
} Push_Sink;

static void push_cb(const Mel_Hid_Event* ev, void* user)
{
    Push_Sink* s = user;
    if (ev->kind == MEL_HID_EVENT_ADDED)
        s->added++;
    else if (ev->kind == MEL_HID_EVENT_REMOVED)
        s->removed++;
    else if (ev->kind == MEL_HID_EVENT_CHANGED)
        s->changed++;
}

MEL_TEST(hid, push_face_delivers_on_refresh)
{
    fake_reset();
    mel_hid__set_skip_host_providers(true);
    mel_hid_init_ex(mel_alloc_heap(), mel_executor_inline());
    register_fake();

    Mel_Hid_Event drain[32];
    mel_hid_poll_events(drain, 32);

    Push_Sink            s = { 0 };
    Mel_Hid_Subscription sub = mel_hid_subscribe(mel_executor_inline(), push_cb, &s);
    MEL_REQUIRE(mel_slotmap_handle_valid(sub.handle));

    fake_add(1, 0x045e, 0x028e);
    mel_hid_refresh();
    MEL_EXPECT_EQ(s.added, 1u);

    fake_remove(1);
    mel_hid_refresh();
    MEL_EXPECT_EQ(s.removed, 1u);

    mel_hid_unsubscribe(sub);
    mel_hid_shutdown();
}

MEL_TEST(hid, subscribe_without_executor_is_loud_null)
{
    fake_reset();
    mel_hid__set_skip_host_providers(true);
    mel_hid_init(mel_alloc_heap());
    register_fake();

    Push_Sink            s = { 0 };
    Mel_Hid_Subscription sub = mel_hid_subscribe(NULL, push_cb, &s);
    MEL_EXPECT(!mel_slotmap_handle_valid(sub.handle));

    mel_hid_shutdown();
}
