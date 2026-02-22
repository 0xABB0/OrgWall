#include "../melody/test.harness.h"
#include "../melody/collection.sort.h"

typedef struct
{
    i32 key;
    i32 payload;
} KeyValue;

MEL_TEST(sort_ascending, .tags = "collection")
{
    i32 arr[] = {1, 2, 3, 4, 5, 6, 7, 8};
    mel_sort(arr, 8, a < b);
    for (usize i = 1; i < 8; i++) MEL_ASSERT_LE(arr[i - 1], arr[i]);
}

MEL_TEST(sort_descending_input, .tags = "collection")
{
    i32 arr[] = {8, 7, 6, 5, 4, 3, 2, 1};
    mel_sort(arr, 8, a < b);
    for (usize i = 1; i < 8; i++) MEL_ASSERT_LE(arr[i - 1], arr[i]);
}

MEL_TEST(sort_random, .tags = "collection")
{
    i32 arr[100];
    for (usize i = 0; i < 100; i++) arr[i] = (i32)((i * 2654435761u) % 1000);
    mel_sort(arr, 100, a < b);
    for (usize i = 1; i < 100; i++) MEL_ASSERT_LE(arr[i - 1], arr[i]);
}

MEL_TEST(sort_duplicates, .tags = "collection")
{
    i32 arr[] = {5, 3, 5, 1, 3, 5, 1, 2, 2, 4};
    mel_sort(arr, 10, a < b);
    for (usize i = 1; i < 10; i++) MEL_ASSERT_LE(arr[i - 1], arr[i]);
}

MEL_TEST(sort_single, .tags = "collection")
{
    i32 arr[] = {42};
    mel_sort(arr, 1, a < b);
    MEL_ASSERT_EQ(arr[0], 42);
}

MEL_TEST(sort_two, .tags = "collection")
{
    i32 arr[] = {9, 1};
    mel_sort(arr, 2, a < b);
    MEL_ASSERT_EQ(arr[0], 1);
    MEL_ASSERT_EQ(arr[1], 9);
}

MEL_TEST(sort_two_already_sorted, .tags = "collection")
{
    i32 arr[] = {1, 9};
    mel_sort(arr, 2, a < b);
    MEL_ASSERT_EQ(arr[0], 1);
    MEL_ASSERT_EQ(arr[1], 9);
}

MEL_TEST(sort_empty, .tags = "collection")
{
    i32 arr[] = {0};
    mel_sort(arr, 0, a < b);
}

MEL_TEST(sort_1000, .tags = "collection")
{
    i32 arr[1000];
    for (usize i = 0; i < 1000; i++) arr[i] = (i32)((i * 2654435761u) % 10000);
    mel_sort(arr, 1000, a < b);
    for (usize i = 1; i < 1000; i++) MEL_ASSERT_LE(arr[i - 1], arr[i]);
}

MEL_TEST(sort_descending_order, .tags = "collection")
{
    i32 arr[] = {1, 2, 3, 4, 5, 6, 7, 8};
    mel_sort(arr, 8, a > b);
    for (usize i = 1; i < 8; i++) MEL_ASSERT_GE(arr[i - 1], arr[i]);
}

MEL_TEST(sort_structs, .tags = "collection")
{
    KeyValue arr[] = {
        {5, 50}, {2, 20}, {8, 80}, {1, 10}, {4, 40},
        {7, 70}, {3, 30}, {6, 60}, {9, 90}, {0, 0},
    };
    mel_sort(arr, 10, a.key < b.key);
    for (usize i = 0; i < 10; i++) MEL_ASSERT_EQ(arr[i].key, (i32)i);
    for (usize i = 0; i < 10; i++) MEL_ASSERT_EQ(arr[i].payload, (i32)(i * 10));
}

MEL_TEST(sort_all_same, .tags = "collection")
{
    i32 arr[] = {7, 7, 7, 7, 7, 7, 7, 7};
    mel_sort(arr, 8, a < b);
    for (usize i = 0; i < 8; i++) MEL_ASSERT_EQ(arr[i], 7);
}

MEL_TEST(heap_ascending, .tags = "collection")
{
    i32 arr[] = {1, 2, 3, 4, 5, 6, 7, 8};
    mel_sort_heap(arr, 8, a < b);
    for (usize i = 1; i < 8; i++) MEL_ASSERT_LE(arr[i - 1], arr[i]);
}

MEL_TEST(heap_descending_input, .tags = "collection")
{
    i32 arr[] = {8, 7, 6, 5, 4, 3, 2, 1};
    mel_sort_heap(arr, 8, a < b);
    for (usize i = 1; i < 8; i++) MEL_ASSERT_LE(arr[i - 1], arr[i]);
}

MEL_TEST(heap_random, .tags = "collection")
{
    i32 arr[100];
    for (usize i = 0; i < 100; i++) arr[i] = (i32)((i * 2654435761u) % 1000);
    mel_sort_heap(arr, 100, a < b);
    for (usize i = 1; i < 100; i++) MEL_ASSERT_LE(arr[i - 1], arr[i]);
}

MEL_TEST(heap_duplicates, .tags = "collection")
{
    i32 arr[] = {5, 3, 5, 1, 3, 5, 1, 2, 2, 4};
    mel_sort_heap(arr, 10, a < b);
    for (usize i = 1; i < 10; i++) MEL_ASSERT_LE(arr[i - 1], arr[i]);
}

MEL_TEST(heap_single, .tags = "collection")
{
    i32 arr[] = {42};
    mel_sort_heap(arr, 1, a < b);
    MEL_ASSERT_EQ(arr[0], 42);
}

MEL_TEST(heap_two, .tags = "collection")
{
    i32 arr[] = {9, 1};
    mel_sort_heap(arr, 2, a < b);
    MEL_ASSERT_EQ(arr[0], 1);
    MEL_ASSERT_EQ(arr[1], 9);
}

MEL_TEST(heap_empty, .tags = "collection")
{
    i32 arr[] = {0};
    mel_sort_heap(arr, 0, a < b);
}

MEL_TEST(heap_1000, .tags = "collection")
{
    i32 arr[1000];
    for (usize i = 0; i < 1000; i++) arr[i] = (i32)((i * 2654435761u) % 10000);
    mel_sort_heap(arr, 1000, a < b);
    for (usize i = 1; i < 1000; i++) MEL_ASSERT_LE(arr[i - 1], arr[i]);
}

MEL_TEST(heap_descending_order, .tags = "collection")
{
    i32 arr[] = {1, 2, 3, 4, 5, 6, 7, 8};
    mel_sort_heap(arr, 8, a > b);
    for (usize i = 1; i < 8; i++) MEL_ASSERT_GE(arr[i - 1], arr[i]);
}

MEL_TEST(heap_structs, .tags = "collection")
{
    KeyValue arr[] = {
        {5, 50}, {2, 20}, {8, 80}, {1, 10}, {4, 40},
        {7, 70}, {3, 30}, {6, 60}, {9, 90}, {0, 0},
    };
    mel_sort_heap(arr, 10, a.key < b.key);
    for (usize i = 0; i < 10; i++) MEL_ASSERT_EQ(arr[i].key, (i32)i);
}

MEL_TEST(heap_all_same, .tags = "collection")
{
    i32 arr[] = {7, 7, 7, 7, 7, 7, 7, 7};
    mel_sort_heap(arr, 8, a < b);
    for (usize i = 0; i < 8; i++) MEL_ASSERT_EQ(arr[i], 7);
}

MEL_TEST(insert_small, .tags = "collection")
{
    i32 arr[] = {5, 3, 1, 4, 2};
    mel_sort_insert(arr, 5, a < b);
    for (usize i = 0; i < 5; i++) MEL_ASSERT_EQ(arr[i], (i32)(i + 1));
}

MEL_TEST(insert_already_sorted, .tags = "collection")
{
    i32 arr[] = {1, 2, 3, 4, 5};
    mel_sort_insert(arr, 5, a < b);
    for (usize i = 0; i < 5; i++) MEL_ASSERT_EQ(arr[i], (i32)(i + 1));
}

MEL_TEST(insert_reversed, .tags = "collection")
{
    i32 arr[] = {5, 4, 3, 2, 1};
    mel_sort_insert(arr, 5, a < b);
    for (usize i = 0; i < 5; i++) MEL_ASSERT_EQ(arr[i], (i32)(i + 1));
}

MEL_TEST(insert_single, .tags = "collection")
{
    i32 arr[] = {99};
    mel_sort_insert(arr, 1, a < b);
    MEL_ASSERT_EQ(arr[0], 99);
}

MEL_TEST(insert_empty, .tags = "collection")
{
    i32 arr[] = {0};
    mel_sort_insert(arr, 0, a < b);
}

MEL_TEST(insert_structs, .tags = "collection")
{
    KeyValue arr[] = {{3, 30}, {1, 10}, {2, 20}};
    mel_sort_insert(arr, 3, a.key < b.key);
    MEL_ASSERT_EQ(arr[0].key, 1);
    MEL_ASSERT_EQ(arr[1].key, 2);
    MEL_ASSERT_EQ(arr[2].key, 3);
}

MEL_TEST(is_sorted_yes, .tags = "collection")
{
    i32 arr[] = {1, 2, 3, 4, 5};
    bool result = mel_sort_is_sorted(arr, 5, a < b);
    MEL_ASSERT(result);
}

MEL_TEST(is_sorted_no, .tags = "collection")
{
    i32 arr[] = {1, 3, 2, 4, 5};
    bool result = mel_sort_is_sorted(arr, 5, a < b);
    MEL_ASSERT(!result);
}

MEL_TEST(is_sorted_single, .tags = "collection")
{
    i32 arr[] = {42};
    bool result = mel_sort_is_sorted(arr, 1, a < b);
    MEL_ASSERT(result);
}

MEL_TEST(is_sorted_empty, .tags = "collection")
{
    i32 arr[] = {0};
    bool result = mel_sort_is_sorted(arr, 0, a < b);
    MEL_ASSERT(result);
}

MEL_TEST(is_sorted_descending, .tags = "collection")
{
    i32 arr[] = {5, 4, 3, 2, 1};
    bool result = mel_sort_is_sorted(arr, 5, a > b);
    MEL_ASSERT(result);
}

MEL_TEST(is_sorted_with_duplicates, .tags = "collection")
{
    i32 arr[] = {1, 1, 2, 2, 3, 3};
    bool result = mel_sort_is_sorted(arr, 6, a < b);
    MEL_ASSERT(result);
}

MEL_TEST(reverse_even, .tags = "collection")
{
    i32 arr[] = {1, 2, 3, 4, 5, 6};
    mel_sort_reverse(arr, 6);
    MEL_ASSERT_EQ(arr[0], 6);
    MEL_ASSERT_EQ(arr[1], 5);
    MEL_ASSERT_EQ(arr[2], 4);
    MEL_ASSERT_EQ(arr[3], 3);
    MEL_ASSERT_EQ(arr[4], 2);
    MEL_ASSERT_EQ(arr[5], 1);
}

MEL_TEST(reverse_odd, .tags = "collection")
{
    i32 arr[] = {1, 2, 3, 4, 5};
    mel_sort_reverse(arr, 5);
    MEL_ASSERT_EQ(arr[0], 5);
    MEL_ASSERT_EQ(arr[1], 4);
    MEL_ASSERT_EQ(arr[2], 3);
    MEL_ASSERT_EQ(arr[3], 2);
    MEL_ASSERT_EQ(arr[4], 1);
}

MEL_TEST(reverse_single, .tags = "collection")
{
    i32 arr[] = {42};
    mel_sort_reverse(arr, 1);
    MEL_ASSERT_EQ(arr[0], 42);
}

MEL_TEST(reverse_empty, .tags = "collection")
{
    i32 arr[] = {0};
    mel_sort_reverse(arr, 0);
}

MEL_TEST(reverse_two, .tags = "collection")
{
    i32 arr[] = {10, 20};
    mel_sort_reverse(arr, 2);
    MEL_ASSERT_EQ(arr[0], 20);
    MEL_ASSERT_EQ(arr[1], 10);
}

MEL_TEST(sort_then_verify, .tags = "collection")
{
    i32 arr[500];
    for (usize i = 0; i < 500; i++) arr[i] = (i32)((i * 2654435761u) % 5000);
    MEL_ASSERT(!mel_sort_is_sorted(arr, 500, a < b));
    mel_sort(arr, 500, a < b);
    MEL_ASSERT(mel_sort_is_sorted(arr, 500, a < b));
}

MEL_TEST(sort_floats, .tags = "collection")
{
    f32 arr[] = {3.14f, 1.0f, 2.71f, 0.5f, 9.9f};
    mel_sort(arr, 5, a < b);
    for (usize i = 1; i < 5; i++) MEL_ASSERT(arr[i - 1] <= arr[i]);
}

MEL_TEST(heap_floats, .tags = "collection")
{
    f32 arr[] = {3.14f, 1.0f, 2.71f, 0.5f, 9.9f};
    mel_sort_heap(arr, 5, a < b);
    for (usize i = 1; i < 5; i++) MEL_ASSERT(arr[i - 1] <= arr[i]);
}
