#include "../melody/test.h"
#include "../melody/allocator.h"
#include "../melody/allocator.heap.h"
#include "../melody/allocator.tracking.h"

MEL_TEST(tracking_init_zeroed)
{
    Mel_Tracking_Allocator t;
    mel_tracking_init(&t, mel_alloc_heap());

    MEL_ASSERT_EQ(t.total_allocated, (usize)0);
    MEL_ASSERT_EQ(t.total_freed, (usize)0);
    MEL_ASSERT_EQ(t.current_usage, (usize)0);
    MEL_ASSERT_EQ(t.peak_usage, (usize)0);
    MEL_ASSERT_EQ(t.alloc_count, (u64)0);
    MEL_ASSERT_EQ(t.free_count, (u64)0);
    MEL_PASS();
}

MEL_TEST(tracking_single_alloc_free)
{
    Mel_Tracking_Allocator t;
    mel_tracking_init(&t, mel_alloc_heap());
    Mel_Alloc alloc = mel_tracking_allocator(&t);

    i32* ptr = mel_alloc_type(&alloc, i32);
    MEL_ASSERT_NOT_NULL(ptr);
    MEL_ASSERT_EQ(t.total_allocated, sizeof(i32));
    MEL_ASSERT_EQ(t.alloc_count, (u64)1);
    MEL_ASSERT_EQ(t.current_usage, sizeof(i32));
    MEL_ASSERT_EQ(t.peak_usage, sizeof(i32));

    mel_dealloc(&alloc, ptr);
    MEL_ASSERT_EQ(t.free_count, (u64)1);
    MEL_PASS();
}

MEL_TEST(tracking_multiple_allocs)
{
    Mel_Tracking_Allocator t;
    mel_tracking_init(&t, mel_alloc_heap());
    Mel_Alloc alloc = mel_tracking_allocator(&t);

    i32* a = mel_alloc_type(&alloc, i32);
    i32* b = mel_alloc_type(&alloc, i32);
    i32* c = mel_alloc_type(&alloc, i32);

    MEL_ASSERT_EQ(t.alloc_count, (u64)3);
    MEL_ASSERT_EQ(t.total_allocated, sizeof(i32) * 3);

    mel_dealloc(&alloc, a);
    mel_dealloc(&alloc, b);
    mel_dealloc(&alloc, c);

    MEL_ASSERT_EQ(t.free_count, (u64)3);
    MEL_PASS();
}

MEL_TEST(tracking_peak_usage)
{
    Mel_Tracking_Allocator t;
    mel_tracking_init(&t, mel_alloc_heap());
    Mel_Alloc alloc = mel_tracking_allocator(&t);

    void* a = mel_alloc(&alloc, 100);
    MEL_ASSERT_EQ(t.current_usage, (usize)100);
    MEL_ASSERT_EQ(t.peak_usage, (usize)100);

    void* b = mel_alloc(&alloc, 200);
    MEL_ASSERT_EQ(t.current_usage, (usize)300);
    MEL_ASSERT_EQ(t.peak_usage, (usize)300);

    void* c = mel_alloc(&alloc, 50);
    MEL_ASSERT_EQ(t.current_usage, (usize)350);
    MEL_ASSERT_EQ(t.peak_usage, (usize)350);

    mel_dealloc(&alloc, a);
    mel_dealloc(&alloc, b);
    mel_dealloc(&alloc, c);

    MEL_ASSERT_EQ(t.peak_usage, (usize)350);
    MEL_ASSERT_EQ(t.alloc_count, (u64)3);
    MEL_ASSERT_EQ(t.free_count, (u64)3);
    MEL_PASS();
}

MEL_TEST(tracking_realloc_counts)
{
    Mel_Tracking_Allocator t;
    mel_tracking_init(&t, mel_alloc_heap());
    Mel_Alloc alloc = mel_tracking_allocator(&t);

    void* ptr = mel_alloc(&alloc, 32);
    MEL_ASSERT_EQ(t.alloc_count, (u64)1);
    MEL_ASSERT_EQ(t.total_allocated, (usize)32);

    ptr = mel_realloc(&alloc, ptr, 64);
    MEL_ASSERT_EQ(t.alloc_count, (u64)2);
    MEL_ASSERT_EQ(t.total_allocated, (usize)96);

    mel_dealloc(&alloc, ptr);
    MEL_PASS();
}

MEL_TEST(tracking_data_integrity)
{
    Mel_Tracking_Allocator t;
    mel_tracking_init(&t, mel_alloc_heap());
    Mel_Alloc alloc = mel_tracking_allocator(&t);

    i32* arr = mel_alloc_array(&alloc, i32, 50);
    for (i32 i = 0; i < 50; i++) arr[i] = i * 3;
    for (i32 i = 0; i < 50; i++) MEL_ASSERT_EQ(arr[i], i * 3);

    mel_dealloc(&alloc, arr);
    MEL_PASS();
}

int main(void)
{
    MEL_TEST_BEGIN("Tracking Allocator Tests");

    MEL_RUN_TEST(tracking_init_zeroed);
    MEL_RUN_TEST(tracking_single_alloc_free);
    MEL_RUN_TEST(tracking_multiple_allocs);
    MEL_RUN_TEST(tracking_peak_usage);
    MEL_RUN_TEST(tracking_realloc_counts);
    MEL_RUN_TEST(tracking_data_integrity);

    MEL_TEST_END();
    return MEL_TEST_EXIT_CODE();
}
