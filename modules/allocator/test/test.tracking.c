#define MEL_CONFIG_DEBUG_ALLOCATOR 1

#include <allocator/tracking.h>
#include <allocator/leak.h>
#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <test/test.h>

typedef struct
{
    usize count;
    usize live_bytes;
} Bucket_Acc;

static void bucket_acc_cb(const Mel_Track_Bucket* b, void* ud)
{
    Bucket_Acc* a = (Bucket_Acc*)ud;
    a->count += 1;
    a->live_bytes += b->live_bytes;
}

typedef struct
{
    usize count;
    usize bytes;
    usize with_tag;
} Live_Acc;

static void live_acc_cb(const Mel_Track_Record* r, void* ud)
{
    Live_Acc* a = (Live_Acc*)ud;
    a->count += 1;
    a->bytes += r->size;
    if (r->tag)
        a->with_tag += 1;
}

MEL_TEST(track, basic_counts_roundtrip)
{
    Mel_Track_Allocator track;
    mel_track_init(&track, (Mel_Track_Allocator_Opt){ .backing = mel_alloc_heap() });
    Mel_Alloc alloc = mel_track_allocator(&track);

    u8* a = mel_alloc(&alloc, 64);
    u8* b = mel_alloc(&alloc, 128);
    MEL_REQUIRE_NOT_NULL(a);
    MEL_REQUIRE_NOT_NULL(b);

    Mel_Track_Allocator_Stats s = mel_track_stats(&track);
    MEL_REQUIRE_EQ(s.live_bytes, 192);
    MEL_REQUIRE_EQ(s.live_allocs, 2);
    MEL_REQUIRE_EQ(s.peak_bytes, 192);
    MEL_REQUIRE_EQ(s.total_alloc_bytes, 192);
    MEL_REQUIRE_EQ(s.total_alloc_count, 2);

    mel_dealloc(&alloc, a);
    mel_dealloc(&alloc, b);

    s = mel_track_stats(&track);
    MEL_REQUIRE_EQ(s.live_bytes, 0);
    MEL_REQUIRE_EQ(s.live_allocs, 0);
    MEL_REQUIRE_EQ(s.peak_bytes, 192);
    MEL_REQUIRE_EQ(s.total_free_count, 2);

    mel_track_shutdown(&track);
}

MEL_TEST(track, site_aggregation_distinguishes_callsites)
{
    Mel_Track_Allocator track;
    mel_track_init(&track, (Mel_Track_Allocator_Opt){ .backing = mel_alloc_heap(), .flags = MEL_TRACK_FLAG_AGGREGATE_SITE });
    Mel_Alloc alloc = mel_track_allocator(&track);

    u8* a = mel_alloc(&alloc, 32);
    u8* b = mel_alloc(&alloc, 48);
    MEL_REQUIRE_NOT_NULL(a);
    MEL_REQUIRE_NOT_NULL(b);

    Bucket_Acc acc = { 0 };
    mel_track_dump_sites(&track, bucket_acc_cb, &acc);
    MEL_REQUIRE_EQ(acc.count, 2);
    MEL_REQUIRE_EQ(acc.live_bytes, 80);

    mel_dealloc(&alloc, a);
    mel_dealloc(&alloc, b);
    mel_track_shutdown(&track);
}

MEL_TEST(track, tag_scope_attribution)
{
    Mel_Track_Allocator track;
    mel_track_init(&track, (Mel_Track_Allocator_Opt){ .backing = mel_alloc_heap(), .flags = MEL_TRACK_FLAG_AGGREGATE_TAG });
    Mel_Alloc alloc = mel_track_allocator(&track);

    mel_track_scope_push("render");
    u8* a = mel_alloc(&alloc, 100);
    MEL_REQUIRE_NOT_NULL(a);
    mel_track_scope_pop();

    u8* b = mel_alloc(&alloc, 7);
    MEL_REQUIRE_NOT_NULL(b);

    Bucket_Acc acc = { 0 };
    mel_track_dump_tags(&track, bucket_acc_cb, &acc);
    MEL_REQUIRE_EQ(acc.count, 1);
    MEL_REQUIRE_EQ(acc.live_bytes, 100);

    mel_dealloc(&alloc, a);
    mel_dealloc(&alloc, b);
    mel_track_shutdown(&track);
}

MEL_TEST(track, realloc_keeps_single_live)
{
    Mel_Track_Allocator track;
    mel_track_init(&track, (Mel_Track_Allocator_Opt){ .backing = mel_alloc_heap() });
    Mel_Alloc alloc = mel_track_allocator(&track);

    u8* p = mel_alloc(&alloc, 16);
    MEL_REQUIRE_NOT_NULL(p);
    p = mel_realloc(&alloc, p, 128);
    MEL_REQUIRE_NOT_NULL(p);

    Mel_Track_Allocator_Stats s = mel_track_stats(&track);
    MEL_REQUIRE_EQ(s.live_bytes, 128);
    MEL_REQUIRE_EQ(s.live_allocs, 1);
    MEL_REQUIRE_EQ(s.total_realloc_count, 1);

    mel_dealloc(&alloc, p);
    mel_track_shutdown(&track);
}

MEL_TEST(track, live_dump_reports_leaks)
{
    Mel_Track_Allocator track;
    mel_track_init(&track, (Mel_Track_Allocator_Opt){ .backing = mel_alloc_heap(), .flags = MEL_TRACK_FLAG_AGGREGATE_TAG });
    Mel_Alloc alloc = mel_track_allocator(&track);

    mel_track_scope_push("leaky");
    u8* leaked = mel_alloc(&alloc, 64);
    mel_track_scope_pop();
    MEL_REQUIRE_NOT_NULL(leaked);

    Live_Acc acc = { 0 };
    mel_track_dump_live(&track, live_acc_cb, &acc);
    MEL_REQUIRE_EQ(acc.count, 1);
    MEL_REQUIRE_EQ(acc.bytes, 64);
    MEL_REQUIRE_EQ(acc.with_tag, 1);

    mel_dealloc(&alloc, leaked);
    mel_track_shutdown(&track);
}

typedef struct
{
    usize count;
    usize bytes;
} Leak_Acc;

static void leak_report_cb(const char* file, const char* func, u32 line, usize size, void* ud)
{
    MEL_UNUSED(file);
    MEL_UNUSED(func);
    MEL_UNUSED(line);
    Leak_Acc* a = (Leak_Acc*)ud;
    a->count += 1;
    a->bytes += size;
}

MEL_TEST(leak, preset_detects_outstanding)
{
    const Mel_Alloc* a = mel_alloc_leak_detect();

    void* p = mel_alloc(a, 40);
    void* q = mel_alloc(a, 24);
    MEL_REQUIRE_NOT_NULL(p);
    MEL_REQUIRE_NOT_NULL(q);

    Leak_Acc acc = { 0 };
    mel_leak_dump(leak_report_cb, &acc);
    MEL_REQUIRE_EQ(acc.count, 2);
    MEL_REQUIRE_EQ(acc.bytes, 64);

    mel_dealloc(a, p);
    mel_dealloc(a, q);

    Leak_Acc after = { 0 };
    mel_leak_dump(leak_report_cb, &after);
    MEL_REQUIRE_EQ(after.count, 0);
    MEL_REQUIRE_EQ(after.bytes, 0);
}
