#include "../melody/test.harness.h"
#include "../melody/allocator.h"
#include "../melody/allocator.heap.h"

MEL_TEST(heap_singleton)
{
    const Mel_Alloc* a = mel_alloc_heap();
    const Mel_Alloc* b = mel_alloc_heap();
    MEL_ASSERT_NOT_NULL(a);
    MEL_ASSERT_NOT_NULL(a->alloc_cb);
    MEL_ASSERT(a == b);
    MEL_PASS();
}

MEL_TEST(heap_alloc_dealloc)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    i32* ptr = mel_alloc_type(alloc, i32);
    MEL_ASSERT_NOT_NULL(ptr);
    *ptr = 42;
    MEL_ASSERT_EQ(*ptr, 42);
    mel_dealloc(alloc, ptr);
    MEL_PASS();
}

MEL_TEST(heap_alloc_array)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    i32* arr = mel_alloc_array(alloc, i32, 100);
    MEL_ASSERT_NOT_NULL(arr);
    for (i32 i = 0; i < 100; i++) arr[i] = i;
    for (i32 i = 0; i < 100; i++) MEL_ASSERT_EQ(arr[i], i);
    mel_dealloc(alloc, arr);
    MEL_PASS();
}

MEL_TEST(heap_realloc)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    i32* ptr = (i32*)mel_alloc(alloc, sizeof(i32) * 4);
    MEL_ASSERT_NOT_NULL(ptr);
    ptr[0] = 10;
    ptr[1] = 20;
    ptr[2] = 30;
    ptr[3] = 40;

    ptr = (i32*)mel_realloc(alloc, ptr, sizeof(i32) * 8);
    MEL_ASSERT_NOT_NULL(ptr);
    MEL_ASSERT_EQ(ptr[0], 10);
    MEL_ASSERT_EQ(ptr[1], 20);
    MEL_ASSERT_EQ(ptr[2], 30);
    MEL_ASSERT_EQ(ptr[3], 40);

    ptr[4] = 50;
    ptr[5] = 60;
    ptr[6] = 70;
    ptr[7] = 80;
    MEL_ASSERT_EQ(ptr[7], 80);

    mel_dealloc(alloc, ptr);
    MEL_PASS();
}

MEL_TEST(heap_calloc)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    i32* arr = (i32*)mel_calloc(alloc, sizeof(i32) * 16);
    MEL_ASSERT_NOT_NULL(arr);
    for (i32 i = 0; i < 16; i++) MEL_ASSERT_EQ(arr[i], 0);
    mel_dealloc(alloc, arr);
    MEL_PASS();
}

MEL_TEST(heap_large_allocation)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    usize big = 1024 * 1024;
    u8* ptr = (u8*)mel_alloc(alloc, big);
    MEL_ASSERT_NOT_NULL(ptr);
    ptr[0] = 0xAA;
    ptr[big - 1] = 0xBB;
    MEL_ASSERT_EQ(ptr[0], 0xAA);
    MEL_ASSERT_EQ(ptr[big - 1], 0xBB);
    mel_dealloc(alloc, ptr);
    MEL_PASS();
}

MEL_TEST(heap_many_small_allocs)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    void* ptrs[256];
    for (i32 i = 0; i < 256; i++)
    {
        ptrs[i] = mel_alloc(alloc, 16);
        MEL_ASSERT_NOT_NULL(ptrs[i]);
    }
    for (i32 i = 0; i < 256; i++)
    {
        mel_dealloc(alloc, ptrs[i]);
    }
    MEL_PASS();
}

int main(void)
{
    MEL_TEST_BEGIN("Heap Allocator Tests");

    MEL_RUN_TEST(heap_singleton);
    MEL_RUN_TEST(heap_alloc_dealloc);
    MEL_RUN_TEST(heap_alloc_array);
    MEL_RUN_TEST(heap_realloc);
    MEL_RUN_TEST(heap_calloc);
    MEL_RUN_TEST(heap_large_allocation);
    MEL_RUN_TEST(heap_many_small_allocs);

    MEL_TEST_END();
    return MEL_TEST_EXIT_CODE();
}
