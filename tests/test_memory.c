#include "../melody/test.harness.h"
#include "../melody/allocator.h"
#include "../melody/allocator.heap.h"
#include "../melody/allocator.leak.h"
#include "../melody/allocator.tracking.h"
#include "../melody/allocator.arena.h"
#include "../melody/collection.array.h"

MEL_TEST(heap_allocator, .tags = "allocator")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    MEL_ASSERT_NOT_NULL(alloc);
    MEL_ASSERT_NOT_NULL(alloc->alloc_cb);
}

MEL_TEST(alloc_free, .tags = "allocator")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    i32* ptr = mel_alloc_type(alloc, i32);
    MEL_ASSERT_NOT_NULL(ptr);
    *ptr = 42;
    MEL_ASSERT_EQ(*ptr, 42);
    mel_dealloc(alloc, ptr);
}

MEL_TEST(alloc_array, .tags = "allocator")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    i32* arr = mel_alloc_array(alloc, i32, 10);
    MEL_ASSERT_NOT_NULL(arr);
    for (i32 i = 0; i < 10; i++)
    {
        arr[i] = i * 2;
    }
    MEL_ASSERT_EQ(arr[5], 10);
    mel_dealloc(alloc, arr);
}

MEL_TEST(tracking_allocator, .tags = "allocator")
{
    Mel_Tracking_Allocator tracking;
    mel_tracking_init(&tracking, mel_alloc_heap());
    Mel_Alloc alloc = mel_tracking_allocator(&tracking);

    MEL_ASSERT_EQ(tracking.total_allocated, (usize)0);
    MEL_ASSERT_EQ(tracking.alloc_count, (u64)0);

    i32* ptr = mel_alloc_type(&alloc, i32);
    MEL_ASSERT_EQ(tracking.total_allocated, sizeof(i32));
    MEL_ASSERT_EQ(tracking.alloc_count, (u64)1);

    mel_dealloc(&alloc, ptr);
    MEL_ASSERT_EQ(tracking.free_count, (u64)1);

}

MEL_TEST(arena_allocator, .tags = "allocator")
{
    u8 buffer[1024];
    Mel_Arena arena;
    mel_arena_init(&arena, buffer, sizeof(buffer));

    MEL_ASSERT_EQ(arena.offset, (usize)0);

    i32* a = mel_arena_push_struct(&arena, i32);
    MEL_ASSERT_NOT_NULL(a);
    *a = 100;

    i32* b = mel_arena_push_struct(&arena, i32);
    MEL_ASSERT_NOT_NULL(b);
    *b = 200;

    MEL_ASSERT_EQ(*a, 100);
    MEL_ASSERT_EQ(*b, 200);

    mel_arena_reset(&arena);
    MEL_ASSERT_EQ(arena.offset, (usize)0);

}

MEL_TEST(leak_detect_allocator, .tags = "allocator")
{
    const Mel_Alloc* alloc = mel_alloc_leak_detect();
    MEL_ASSERT_NOT_NULL(alloc);

    i32* ptr = mel_alloc_type(alloc, i32);
    MEL_ASSERT_NOT_NULL(ptr);
    *ptr = 99;
    MEL_ASSERT_EQ(*ptr, 99);

    mel_dealloc(alloc, ptr);
}

MEL_TEST(dynamic_array_push, .tags = "allocator")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Array(i32) arr;
    mel_array_init(&arr, alloc);

    MEL_ASSERT_EQ(arr.count, (usize)0);

    mel_array_push(&arr, 10);
    mel_array_push(&arr, 20);
    mel_array_push(&arr, 30);

    MEL_ASSERT_EQ(arr.count, (usize)3);
    MEL_ASSERT_EQ(arr.items[0], 10);
    MEL_ASSERT_EQ(arr.items[1], 20);
    MEL_ASSERT_EQ(arr.items[2], 30);

    mel_array_free(&arr);
    MEL_ASSERT_EQ(arr.count, (usize)0);
    MEL_ASSERT_NULL(arr.items);

}

MEL_TEST(dynamic_array_pop, .tags = "allocator")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Array(i32) arr;
    mel_array_init(&arr, alloc);

    mel_array_push(&arr, 1);
    mel_array_push(&arr, 2);
    mel_array_push(&arr, 3);

    i32 val = mel_array_pop(&arr);
    MEL_ASSERT_EQ(val, 3);
    MEL_ASSERT_EQ(arr.count, (usize)2);

    val = mel_array_pop(&arr);
    MEL_ASSERT_EQ(val, 2);

    mel_array_free(&arr);
}

MEL_TEST(dynamic_array_growth, .tags = "allocator")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Array(i32) arr;
    mel_array_init(&arr, alloc);

    for (i32 i = 0; i < 100; i++)
    {
        mel_array_push(&arr, i);
    }

    MEL_ASSERT_EQ(arr.count, (usize)100);
    MEL_ASSERT_GE(arr.capacity, (usize)100);

    for (i32 i = 0; i < 100; i++)
    {
        MEL_ASSERT_EQ(arr.items[i], i);
    }

    mel_array_free(&arr);
}