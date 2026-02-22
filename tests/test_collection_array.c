#include "../melody/test.harness.h"
#include "../melody/allocator.h"
#include "../melody/allocator.heap.h"
#include "../melody/collection.array.h"

MEL_TEST(push_pop, .tags = "collection")
{
    Mel_Array(i32) arr;
    mel_array_init(&arr, mel_alloc_heap());

    mel_array_push(&arr, 10);
    mel_array_push(&arr, 20);
    mel_array_push(&arr, 30);

    MEL_ASSERT_EQ(arr.count, (usize)3);
    MEL_ASSERT_EQ(arr.items[0], 10);
    MEL_ASSERT_EQ(arr.items[1], 20);
    MEL_ASSERT_EQ(arr.items[2], 30);

    i32 val = mel_array_pop(&arr);
    MEL_ASSERT_EQ(val, 30);
    MEL_ASSERT_EQ(arr.count, (usize)2);

    val = mel_array_pop(&arr);
    MEL_ASSERT_EQ(val, 20);
    MEL_ASSERT_EQ(arr.count, (usize)1);

    val = mel_array_pop(&arr);
    MEL_ASSERT_EQ(val, 10);
    MEL_ASSERT_EQ(arr.count, (usize)0);

    mel_array_free(&arr);
}

MEL_TEST(reserve, .tags = "collection")
{
    Mel_Array(i32) arr;
    mel_array_init(&arr, mel_alloc_heap());

    mel_array_reserve(&arr, 100);
    MEL_ASSERT_GE(arr.capacity, (usize)100);
    MEL_ASSERT_EQ(arr.count, (usize)0);
    MEL_ASSERT_NOT_NULL(arr.items);

    mel_array_reserve(&arr, 50);
    MEL_ASSERT_GE(arr.capacity, (usize)100);

    mel_array_reserve(&arr, 200);
    MEL_ASSERT_GE(arr.capacity, (usize)200);

    mel_array_free(&arr);
}

MEL_TEST(remove_ordered, .tags = "collection")
{
    Mel_Array(i32) arr;
    mel_array_init(&arr, mel_alloc_heap());

    for (i32 i = 0; i < 5; i++) mel_array_push(&arr, i * 10);

    mel_array_remove_ordered(&arr, 2);
    MEL_ASSERT_EQ(arr.count, (usize)4);
    MEL_ASSERT_EQ(arr.items[0], 0);
    MEL_ASSERT_EQ(arr.items[1], 10);
    MEL_ASSERT_EQ(arr.items[2], 30);
    MEL_ASSERT_EQ(arr.items[3], 40);

    mel_array_remove_ordered(&arr, 0);
    MEL_ASSERT_EQ(arr.count, (usize)3);
    MEL_ASSERT_EQ(arr.items[0], 10);
    MEL_ASSERT_EQ(arr.items[1], 30);
    MEL_ASSERT_EQ(arr.items[2], 40);

    mel_array_remove_ordered(&arr, 2);
    MEL_ASSERT_EQ(arr.count, (usize)2);
    MEL_ASSERT_EQ(arr.items[0], 10);
    MEL_ASSERT_EQ(arr.items[1], 30);

    mel_array_free(&arr);
}

MEL_TEST(remove_unordered, .tags = "collection")
{
    Mel_Array(i32) arr;
    mel_array_init(&arr, mel_alloc_heap());

    for (i32 i = 0; i < 5; i++) mel_array_push(&arr, i * 10);

    mel_array_remove_unordered(&arr, 1);
    MEL_ASSERT_EQ(arr.count, (usize)4);
    MEL_ASSERT_EQ(arr.items[0], 0);
    MEL_ASSERT_EQ(arr.items[1], 40);
    MEL_ASSERT_EQ(arr.items[2], 20);
    MEL_ASSERT_EQ(arr.items[3], 30);

    mel_array_free(&arr);
}

MEL_TEST(clear, .tags = "collection")
{
    Mel_Array(i32) arr;
    mel_array_init(&arr, mel_alloc_heap());

    for (i32 i = 0; i < 10; i++) mel_array_push(&arr, i);

    MEL_ASSERT_EQ(arr.count, (usize)10);
    usize cap_before = arr.capacity;

    mel_array_clear(&arr);
    MEL_ASSERT_EQ(arr.count, (usize)0);
    MEL_ASSERT_EQ(arr.capacity, cap_before);

    mel_array_push(&arr, 42);
    MEL_ASSERT_EQ(arr.count, (usize)1);
    MEL_ASSERT_EQ(arr.items[0], 42);

    mel_array_free(&arr);
}

MEL_TEST(growth, .tags = "collection")
{
    Mel_Array(i32) arr;
    mel_array_init(&arr, mel_alloc_heap());

    for (i32 i = 0; i < 1000; i++) mel_array_push(&arr, i);

    MEL_ASSERT_EQ(arr.count, (usize)1000);
    MEL_ASSERT_GE(arr.capacity, (usize)1000);

    for (i32 i = 0; i < 1000; i++) MEL_ASSERT_EQ(arr.items[i], i);

    mel_array_free(&arr);
}

MEL_TEST(insert_beginning, .tags = "collection")
{
    Mel_Array(i32) arr;
    mel_array_init(&arr, mel_alloc_heap());

    mel_array_push(&arr, 20);
    mel_array_push(&arr, 30);
    mel_array_push(&arr, 40);

    mel_array_insert(&arr, 0, 10);
    MEL_ASSERT_EQ(arr.count, (usize)4);
    MEL_ASSERT_EQ(arr.items[0], 10);
    MEL_ASSERT_EQ(arr.items[1], 20);
    MEL_ASSERT_EQ(arr.items[2], 30);
    MEL_ASSERT_EQ(arr.items[3], 40);

    mel_array_free(&arr);
}

MEL_TEST(insert_middle, .tags = "collection")
{
    Mel_Array(i32) arr;
    mel_array_init(&arr, mel_alloc_heap());

    mel_array_push(&arr, 10);
    mel_array_push(&arr, 30);
    mel_array_push(&arr, 40);

    mel_array_insert(&arr, 1, 20);
    MEL_ASSERT_EQ(arr.count, (usize)4);
    MEL_ASSERT_EQ(arr.items[0], 10);
    MEL_ASSERT_EQ(arr.items[1], 20);
    MEL_ASSERT_EQ(arr.items[2], 30);
    MEL_ASSERT_EQ(arr.items[3], 40);

    mel_array_free(&arr);
}

MEL_TEST(insert_end, .tags = "collection")
{
    Mel_Array(i32) arr;
    mel_array_init(&arr, mel_alloc_heap());

    mel_array_push(&arr, 10);
    mel_array_push(&arr, 20);

    mel_array_insert(&arr, 2, 30);
    MEL_ASSERT_EQ(arr.count, (usize)3);
    MEL_ASSERT_EQ(arr.items[0], 10);
    MEL_ASSERT_EQ(arr.items[1], 20);
    MEL_ASSERT_EQ(arr.items[2], 30);

    mel_array_free(&arr);
}

MEL_TEST(insert_empty, .tags = "collection")
{
    Mel_Array(i32) arr;
    mel_array_init(&arr, mel_alloc_heap());

    mel_array_insert(&arr, 0, 42);
    MEL_ASSERT_EQ(arr.count, (usize)1);
    MEL_ASSERT_EQ(arr.items[0], 42);

    mel_array_free(&arr);
}

MEL_TEST(insert_triggers_growth, .tags = "collection")
{
    Mel_Array(i32) arr;
    mel_array_init(&arr, mel_alloc_heap());

    for (i32 i = 0; i < (i32)MEL_DA_INIT_CAP; i++) mel_array_push(&arr, i * 10);
    MEL_ASSERT_EQ(arr.count, (usize)MEL_DA_INIT_CAP);
    MEL_ASSERT_EQ(arr.capacity, (usize)MEL_DA_INIT_CAP);

    mel_array_insert(&arr, 4, 999);
    MEL_ASSERT_EQ(arr.count, (usize)(MEL_DA_INIT_CAP + 1));
    MEL_ASSERT_GT(arr.capacity, (usize)MEL_DA_INIT_CAP);
    MEL_ASSERT_EQ(arr.items[4], 999);
    MEL_ASSERT_EQ(arr.items[3], 30);
    MEL_ASSERT_EQ(arr.items[5], 40);

    mel_array_free(&arr);
}

MEL_TEST(last, .tags = "collection")
{
    Mel_Array(i32) arr;
    mel_array_init(&arr, mel_alloc_heap());

    mel_array_push(&arr, 1);
    MEL_ASSERT_EQ(mel_array_last(&arr), 1);

    mel_array_push(&arr, 2);
    MEL_ASSERT_EQ(mel_array_last(&arr), 2);

    mel_array_push(&arr, 99);
    MEL_ASSERT_EQ(mel_array_last(&arr), 99);

    mel_array_free(&arr);
}

MEL_TEST(free_resets_state, .tags = "collection")
{
    Mel_Array(i32) arr;
    mel_array_init(&arr, mel_alloc_heap());

    mel_array_push(&arr, 1);
    mel_array_push(&arr, 2);

    mel_array_free(&arr);
    MEL_ASSERT_NULL(arr.items);
    MEL_ASSERT_EQ(arr.count, (usize)0);
    MEL_ASSERT_EQ(arr.capacity, (usize)0);

}

MEL_TEST(free_on_empty, .tags = "collection")
{
    Mel_Array(i32) arr;
    mel_array_init(&arr, mel_alloc_heap());

    mel_array_free(&arr);
    MEL_ASSERT_NULL(arr.items);
    MEL_ASSERT_EQ(arr.count, (usize)0);
    MEL_ASSERT_EQ(arr.capacity, (usize)0);

}

MEL_TEST(push_after_clear, .tags = "collection")
{
    Mel_Array(i32) arr;
    mel_array_init(&arr, mel_alloc_heap());

    mel_array_push(&arr, 1);
    mel_array_push(&arr, 2);
    mel_array_push(&arr, 3);
    mel_array_clear(&arr);

    mel_array_push(&arr, 100);
    mel_array_push(&arr, 200);
    MEL_ASSERT_EQ(arr.count, (usize)2);
    MEL_ASSERT_EQ(arr.items[0], 100);
    MEL_ASSERT_EQ(arr.items[1], 200);

    mel_array_free(&arr);
}

MEL_TEST(different_types, .tags = "collection")
{
    Mel_Array(f64) farr;
    mel_array_init(&farr, mel_alloc_heap());

    mel_array_push(&farr, 3.14);
    mel_array_push(&farr, 2.71);
    MEL_ASSERT_FLOAT_EQ((f32)farr.items[0], 3.14f, 0.01f);
    MEL_ASSERT_FLOAT_EQ((f32)farr.items[1], 2.71f, 0.01f);

    mel_array_free(&farr);

    typedef struct { i32 x; i32 y; } Point;
    Mel_Array(Point) parr;
    mel_array_init(&parr, mel_alloc_heap());

    mel_array_push(&parr, ((Point){1, 2}));
    mel_array_push(&parr, ((Point){3, 4}));
    MEL_ASSERT_EQ(parr.items[0].x, 1);
    MEL_ASSERT_EQ(parr.items[0].y, 2);
    MEL_ASSERT_EQ(parr.items[1].x, 3);
    MEL_ASSERT_EQ(parr.items[1].y, 4);

    mel_array_free(&parr);
}
