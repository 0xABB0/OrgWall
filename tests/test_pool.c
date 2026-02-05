#include "../src/test.h"
#include "../src/allocator.h"
#include "../src/allocator.pool.h"

MEL_TEST(pool_init)
{
    _Alignas(16) u8 buffer[1024];
    Mel_Pool pool;
    mel_pool_init(&pool, buffer, sizeof(buffer), .block_size = 64);

    MEL_ASSERT_EQ(pool.block_size, (usize)64);
    MEL_ASSERT_EQ(pool.block_count, sizeof(buffer) / 64);
    MEL_ASSERT_EQ(pool.used_count, (usize)0);
    MEL_ASSERT_NOT_NULL(pool.free_list);
    MEL_PASS();
}

MEL_TEST(pool_alloc_single)
{
    _Alignas(16) u8 buffer[1024];
    Mel_Pool pool;
    mel_pool_init(&pool, buffer, sizeof(buffer), .block_size = sizeof(i64));

    i64* ptr = (i64*)mel_pool_alloc(&pool);
    MEL_ASSERT_NOT_NULL(ptr);
    *ptr = 0xDEADBEEF;
    MEL_ASSERT_EQ(*ptr, (i64)0xDEADBEEF);
    MEL_ASSERT_EQ(pool.used_count, (usize)1);

    mel_pool_free(&pool, ptr);
    MEL_ASSERT_EQ(pool.used_count, (usize)0);
    MEL_PASS();
}

MEL_TEST(pool_exhaust_and_reuse)
{
    _Alignas(16) u8 buffer[128];
    Mel_Pool pool;
    mel_pool_init(&pool, buffer, sizeof(buffer), .block_size = 16);

    usize total = pool.block_count;
    void* ptrs[128 / 16];

    for (usize i = 0; i < total; i++)
    {
        ptrs[i] = mel_pool_alloc(&pool);
        MEL_ASSERT_NOT_NULL(ptrs[i]);
    }
    MEL_ASSERT_EQ(pool.used_count, total);
    MEL_ASSERT_NULL(pool.free_list);

    mel_pool_free(&pool, ptrs[0]);
    MEL_ASSERT_EQ(pool.used_count, total - 1);

    void* reused = mel_pool_alloc(&pool);
    MEL_ASSERT_NOT_NULL(reused);
    MEL_ASSERT_EQ(pool.used_count, total);

    for (usize i = 1; i < total; i++) mel_pool_free(&pool, ptrs[i]);
    mel_pool_free(&pool, reused);
    MEL_ASSERT_EQ(pool.used_count, (usize)0);
    MEL_PASS();
}

MEL_TEST(pool_owns)
{
    _Alignas(16) u8 buffer[256];
    Mel_Pool pool;
    mel_pool_init(&pool, buffer, sizeof(buffer), .block_size = 32);

    void* ptr = mel_pool_alloc(&pool);
    MEL_ASSERT(mel_pool_owns(&pool, ptr));

    u8 outside;
    MEL_ASSERT(!mel_pool_owns(&pool, &outside));

    mel_pool_free(&pool, ptr);
    MEL_PASS();
}

MEL_TEST(pool_reset)
{
    _Alignas(16) u8 buffer[256];
    Mel_Pool pool;
    mel_pool_init(&pool, buffer, sizeof(buffer), .block_size = 32);

    mel_pool_alloc(&pool);
    mel_pool_alloc(&pool);
    mel_pool_alloc(&pool);
    MEL_ASSERT_EQ(pool.used_count, (usize)3);

    mel_pool_reset(&pool);
    MEL_ASSERT_EQ(pool.used_count, (usize)0);
    MEL_ASSERT_NOT_NULL(pool.free_list);

    void* ptr = mel_pool_alloc(&pool);
    MEL_ASSERT_NOT_NULL(ptr);
    mel_pool_free(&pool, ptr);
    MEL_PASS();
}

MEL_TEST(pool_data_integrity)
{
    _Alignas(16) u8 buffer[1024];
    Mel_Pool pool;
    mel_pool_init(&pool, buffer, sizeof(buffer), .block_size = 64);

    typedef struct { i32 x; i32 y; f32 z; } Thing;

    Thing* a = (Thing*)mel_pool_alloc(&pool);
    Thing* b = (Thing*)mel_pool_alloc(&pool);
    Thing* c = (Thing*)mel_pool_alloc(&pool);

    *a = (Thing){1, 2, 3.0f};
    *b = (Thing){4, 5, 6.0f};
    *c = (Thing){7, 8, 9.0f};

    MEL_ASSERT_EQ(a->x, 1);
    MEL_ASSERT_EQ(b->y, 5);
    MEL_ASSERT_FLOAT_EQ(c->z, 9.0f, 0.001f);

    mel_pool_free(&pool, a);
    mel_pool_free(&pool, b);
    mel_pool_free(&pool, c);
    MEL_PASS();
}

MEL_TEST(pool_to_alloc_interface)
{
    _Alignas(16) u8 buffer[512];
    Mel_Pool pool;
    mel_pool_init(&pool, buffer, sizeof(buffer), .block_size = 64);

    Mel_Alloc alloc = mel_pool_to_alloc(&pool);
    MEL_ASSERT_NOT_NULL(alloc.alloc_cb);

    void* ptr = mel_alloc(&alloc, 32);
    MEL_ASSERT_NOT_NULL(ptr);
    MEL_ASSERT_EQ(pool.used_count, (usize)1);

    mel_dealloc(&alloc, ptr);
    MEL_ASSERT_EQ(pool.used_count, (usize)0);
    MEL_PASS();
}

MEL_TEST(pool_free_and_realloc_pattern)
{
    _Alignas(16) u8 buffer[512];
    Mel_Pool pool;
    mel_pool_init(&pool, buffer, sizeof(buffer), .block_size = sizeof(void*));

    usize total = pool.block_count;
    void* ptrs[512 / sizeof(void*)];

    for (usize i = 0; i < total; i++)
    {
        ptrs[i] = mel_pool_alloc(&pool);
    }

    for (usize i = 0; i < total; i += 2)
    {
        mel_pool_free(&pool, ptrs[i]);
    }

    for (usize i = 0; i < total; i += 2)
    {
        ptrs[i] = mel_pool_alloc(&pool);
        MEL_ASSERT_NOT_NULL(ptrs[i]);
    }

    MEL_ASSERT_EQ(pool.used_count, total);

    for (usize i = 0; i < total; i++)
    {
        mel_pool_free(&pool, ptrs[i]);
    }
    MEL_PASS();
}

int main(void)
{
    MEL_TEST_BEGIN("Pool Allocator Tests");

    MEL_RUN_TEST(pool_init);
    MEL_RUN_TEST(pool_alloc_single);
    MEL_RUN_TEST(pool_exhaust_and_reuse);
    MEL_RUN_TEST(pool_owns);
    MEL_RUN_TEST(pool_reset);
    MEL_RUN_TEST(pool_data_integrity);
    MEL_RUN_TEST(pool_to_alloc_interface);
    MEL_RUN_TEST(pool_free_and_realloc_pattern);

    MEL_TEST_END();
    return MEL_TEST_EXIT_CODE();
}
