#include "../melody/test.harness.h"
#include "../melody/allocator.block.h"

MEL_TEST(block_init, .tags = "allocator")
{
    _Alignas(16) u8 buffer[1024];
    Mel_Block_Alloc block;
    mel_block_init(&block, buffer, sizeof(buffer));

    MEL_ASSERT_EQ(block.offset, (usize)0);
    MEL_ASSERT_EQ(block.size, sizeof(buffer));
}

MEL_TEST(block_push_struct, .tags = "allocator")
{
    _Alignas(16) u8 buffer[1024];
    Mel_Block_Alloc block;
    mel_block_init(&block, buffer, sizeof(buffer));

    i32* a = mel_block_push_struct(&block, i32);
    MEL_ASSERT_NOT_NULL(a);
    *a = 42;

    i64* b = mel_block_push_struct(&block, i64);
    MEL_ASSERT_NOT_NULL(b);
    *b = 9999;

    MEL_ASSERT_EQ(*a, 42);
    MEL_ASSERT_EQ(*b, (i64)9999);
}

MEL_TEST(block_push_array, .tags = "allocator")
{
    _Alignas(16) u8 buffer[4096];
    Mel_Block_Alloc block;
    mel_block_init(&block, buffer, sizeof(buffer));

    f32* arr = mel_block_push_array(&block, f32, 64);
    MEL_ASSERT_NOT_NULL(arr);
    for (i32 i = 0; i < 64; i++) arr[i] = (f32)i * 0.5f;
    for (i32 i = 0; i < 64; i++) MEL_ASSERT_FLOAT_EQ(arr[i], (f32)i * 0.5f, 0.001f);
}

MEL_TEST(block_iteration_single, .tags = "allocator")
{
    _Alignas(16) u8 buffer[1024];
    Mel_Block_Alloc block;
    mel_block_init(&block, buffer, sizeof(buffer));

    i32* val = mel_block_push_struct(&block, i32);
    *val = 777;

    Mel_Block_Iter iter = mel_block_iter_begin(&block);
    usize size;
    i32* found = (i32*)mel_block_iter_next(&iter, &size);
    MEL_ASSERT_NOT_NULL(found);
    MEL_ASSERT_EQ(*found, 777);
    MEL_ASSERT_EQ(size, sizeof(i32));
    MEL_ASSERT(mel_block_iter_end(&iter));
}

MEL_TEST(block_iteration_multiple, .tags = "allocator")
{
    _Alignas(16) u8 buffer[4096];
    Mel_Block_Alloc block;
    mel_block_init(&block, buffer, sizeof(buffer));

    i32* a = mel_block_push_struct(&block, i32);
    *a = 10;
    i32* b = mel_block_push_struct(&block, i32);
    *b = 20;
    i32* c = mel_block_push_struct(&block, i32);
    *c = 30;

    Mel_Block_Iter iter = mel_block_iter_begin(&block);
    i32 count = 0;
    i32 expected[] = {10, 20, 30};
    usize size;
    void* ptr;

    while ((ptr = mel_block_iter_next(&iter, &size)) != NULL)
    {
        MEL_ASSERT_LT(count, 3);
        MEL_ASSERT_EQ(*(i32*)ptr, expected[count]);
        MEL_ASSERT_EQ(size, sizeof(i32));
        count++;
    }
    MEL_ASSERT_EQ(count, 3);
    MEL_ASSERT(mel_block_iter_end(&iter));
}

MEL_TEST(block_iteration_mixed_types, .tags = "allocator")
{
    _Alignas(16) u8 buffer[4096];
    Mel_Block_Alloc block;
    mel_block_init(&block, buffer, sizeof(buffer));

    i32* a = mel_block_push_struct(&block, i32);
    *a = 1;

    f64* b = mel_block_push_struct(&block, f64);
    *b = 3.14;

    u8* c = mel_block_push_array(&block, u8, 10);
    for (i32 i = 0; i < 10; i++) c[i] = (u8)(i + 1);

    Mel_Block_Iter iter = mel_block_iter_begin(&block);
    usize size;

    i32* r1 = (i32*)mel_block_iter_next(&iter, &size);
    MEL_ASSERT_EQ(*r1, 1);
    MEL_ASSERT_EQ(size, sizeof(i32));

    f64* r2 = (f64*)mel_block_iter_next(&iter, &size);
    MEL_ASSERT_FLOAT_EQ((f32)*r2, 3.14f, 0.01f);
    MEL_ASSERT_EQ(size, sizeof(f64));

    u8* r3 = (u8*)mel_block_iter_next(&iter, &size);
    MEL_ASSERT_EQ(size, (usize)10);
    for (i32 i = 0; i < 10; i++) MEL_ASSERT_EQ(r3[i], (u8)(i + 1));

    MEL_ASSERT(mel_block_iter_end(&iter));
}

MEL_TEST(block_reset_and_reuse, .tags = "allocator")
{
    _Alignas(16) u8 buffer[1024];
    Mel_Block_Alloc block;
    mel_block_init(&block, buffer, sizeof(buffer));

    mel_block_push_struct(&block, i32);
    mel_block_push_struct(&block, i32);
    MEL_ASSERT_GT(block.offset, (usize)0);

    mel_block_reset(&block);
    MEL_ASSERT_EQ(block.offset, (usize)0);

    Mel_Block_Iter iter = mel_block_iter_begin(&block);
    MEL_ASSERT(mel_block_iter_end(&iter));

    i32* fresh = mel_block_push_struct(&block, i32);
    *fresh = 42;
    MEL_ASSERT_EQ(*fresh, 42);
}

MEL_TEST(block_iteration_empty, .tags = "allocator")
{
    _Alignas(16) u8 buffer[256];
    Mel_Block_Alloc block;
    mel_block_init(&block, buffer, sizeof(buffer));

    Mel_Block_Iter iter = mel_block_iter_begin(&block);
    MEL_ASSERT(mel_block_iter_end(&iter));
    MEL_ASSERT_NULL(mel_block_iter_next(&iter, NULL));
}

MEL_TEST(block_null_out_size, .tags = "allocator")
{
    _Alignas(16) u8 buffer[512];
    Mel_Block_Alloc block;
    mel_block_init(&block, buffer, sizeof(buffer));

    i32* val = mel_block_push_struct(&block, i32);
    *val = 55;

    Mel_Block_Iter iter = mel_block_iter_begin(&block);
    i32* found = (i32*)mel_block_iter_next(&iter, NULL);
    MEL_ASSERT_NOT_NULL(found);
    MEL_ASSERT_EQ(*found, 55);
}