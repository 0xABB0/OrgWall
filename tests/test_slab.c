#include "../melody/test.harness.h"
#include "../melody/collection.slab.h"
#include <stdatomic.h>

MEL_TEST(slab_init, .tags = "allocator")
{
    _Alignas(16) u8 buf_small[512];
    _Alignas(16) u8 buf_large[1024];

    Mel_Slab_Class classes[2];
    Mel_Slab_Class_Desc descs[] = {
        { .buffer = buf_small, .buffer_size = sizeof(buf_small), .block_size = 16 },
        { .buffer = buf_large, .buffer_size = sizeof(buf_large), .block_size = 64 },
    };

    Mel_Slab_Alloc slab;
    mel_slab_init(&slab, classes, descs, 2);

    MEL_ASSERT_EQ(slab.class_count, 2);
    MEL_ASSERT_EQ(slab.classes[0].block_size, (usize)16);
    MEL_ASSERT_EQ(slab.classes[1].block_size, (usize)64);
}

MEL_TEST(slab_alloc_smallest_class, .tags = "allocator")
{
    _Alignas(16) u8 buf_small[256];
    _Alignas(16) u8 buf_large[512];

    Mel_Slab_Class classes[2];
    Mel_Slab_Class_Desc descs[] = {
        { .buffer = buf_small, .buffer_size = sizeof(buf_small), .block_size = 16 },
        { .buffer = buf_large, .buffer_size = sizeof(buf_large), .block_size = 64 },
    };

    Mel_Slab_Alloc slab;
    mel_slab_init(&slab, classes, descs, 2);

    void* ptr = mel_slab_alloc(&slab, 8);
    MEL_ASSERT_NOT_NULL(ptr);
    MEL_ASSERT(mel_pool_owns(&slab.classes[0].pool, ptr));
    MEL_ASSERT(!mel_pool_owns(&slab.classes[1].pool, ptr));

    mel_slab_free(&slab, ptr);
}

MEL_TEST(slab_alloc_larger_class, .tags = "allocator")
{
    _Alignas(16) u8 buf_small[256];
    _Alignas(16) u8 buf_large[512];

    Mel_Slab_Class classes[2];
    Mel_Slab_Class_Desc descs[] = {
        { .buffer = buf_small, .buffer_size = sizeof(buf_small), .block_size = 16 },
        { .buffer = buf_large, .buffer_size = sizeof(buf_large), .block_size = 64 },
    };

    Mel_Slab_Alloc slab;
    mel_slab_init(&slab, classes, descs, 2);

    void* ptr = mel_slab_alloc(&slab, 32);
    MEL_ASSERT_NOT_NULL(ptr);
    MEL_ASSERT(!mel_pool_owns(&slab.classes[0].pool, ptr));
    MEL_ASSERT(mel_pool_owns(&slab.classes[1].pool, ptr));

    mel_slab_free(&slab, ptr);
}

MEL_TEST(slab_exact_size_match, .tags = "allocator")
{
    _Alignas(16) u8 buf_a[256];
    _Alignas(16) u8 buf_b[256];

    Mel_Slab_Class classes[2];
    Mel_Slab_Class_Desc descs[] = {
        { .buffer = buf_a, .buffer_size = sizeof(buf_a), .block_size = 32 },
        { .buffer = buf_b, .buffer_size = sizeof(buf_b), .block_size = 64 },
    };

    Mel_Slab_Alloc slab;
    mel_slab_init(&slab, classes, descs, 2);

    void* ptr = mel_slab_alloc(&slab, 32);
    MEL_ASSERT_NOT_NULL(ptr);
    MEL_ASSERT(mel_pool_owns(&slab.classes[0].pool, ptr));

    mel_slab_free(&slab, ptr);
}

MEL_TEST(slab_multiple_allocs_same_class, .tags = "allocator")
{
    _Alignas(16) u8 buf[512];

    Mel_Slab_Class classes[1];
    Mel_Slab_Class_Desc descs[] = {
        { .buffer = buf, .buffer_size = sizeof(buf), .block_size = 32 },
    };

    Mel_Slab_Alloc slab;
    mel_slab_init(&slab, classes, descs, 1);

    usize max_allocs = sizeof(buf) / 32;
    void* ptrs[512 / 32];

    for (usize i = 0; i < max_allocs; i++)
    {
        ptrs[i] = mel_slab_alloc(&slab, 16);
        MEL_ASSERT_NOT_NULL(ptrs[i]);
    }

    for (usize i = 0; i < max_allocs; i++)
    {
        mel_slab_free(&slab, ptrs[i]);
    }
}

MEL_TEST(slab_free_correct_class, .tags = "allocator")
{
    _Alignas(16) u8 buf_a[256];
    _Alignas(16) u8 buf_b[256];

    Mel_Slab_Class classes[2];
    Mel_Slab_Class_Desc descs[] = {
        { .buffer = buf_a, .buffer_size = sizeof(buf_a), .block_size = 16 },
        { .buffer = buf_b, .buffer_size = sizeof(buf_b), .block_size = 64 },
    };

    Mel_Slab_Alloc slab;
    mel_slab_init(&slab, classes, descs, 2);

    void* small = mel_slab_alloc(&slab, 8);
    void* large = mel_slab_alloc(&slab, 48);

    usize small_used = atomic_load(&slab.classes[0].pool.used_count);
    usize large_used = atomic_load(&slab.classes[1].pool.used_count);
    MEL_ASSERT_EQ(small_used, (usize)1);
    MEL_ASSERT_EQ(large_used, (usize)1);

    mel_slab_free(&slab, large);
    MEL_ASSERT_EQ(atomic_load(&slab.classes[0].pool.used_count), (usize)1);
    MEL_ASSERT_EQ(atomic_load(&slab.classes[1].pool.used_count), (usize)0);

    mel_slab_free(&slab, small);
    MEL_ASSERT_EQ(atomic_load(&slab.classes[0].pool.used_count), (usize)0);
}

MEL_TEST(slab_reset_all_classes, .tags = "allocator")
{
    _Alignas(16) u8 buf_a[256];
    _Alignas(16) u8 buf_b[256];

    Mel_Slab_Class classes[2];
    Mel_Slab_Class_Desc descs[] = {
        { .buffer = buf_a, .buffer_size = sizeof(buf_a), .block_size = 16 },
        { .buffer = buf_b, .buffer_size = sizeof(buf_b), .block_size = 64 },
    };

    Mel_Slab_Alloc slab;
    mel_slab_init(&slab, classes, descs, 2);

    mel_slab_alloc(&slab, 8);
    mel_slab_alloc(&slab, 48);
    mel_slab_alloc(&slab, 8);

    mel_slab_reset(&slab);
    MEL_ASSERT_EQ(atomic_load(&slab.classes[0].pool.used_count), (usize)0);
    MEL_ASSERT_EQ(atomic_load(&slab.classes[1].pool.used_count), (usize)0);

    void* ptr = mel_slab_alloc(&slab, 8);
    MEL_ASSERT_NOT_NULL(ptr);
    mel_slab_free(&slab, ptr);
}

MEL_TEST(slab_three_classes, .tags = "allocator")
{
    _Alignas(16) u8 buf_a[256];
    _Alignas(16) u8 buf_b[256];
    _Alignas(16) u8 buf_c[512];

    Mel_Slab_Class classes[3];
    Mel_Slab_Class_Desc descs[] = {
        { .buffer = buf_a, .buffer_size = sizeof(buf_a), .block_size = 16 },
        { .buffer = buf_b, .buffer_size = sizeof(buf_b), .block_size = 64 },
        { .buffer = buf_c, .buffer_size = sizeof(buf_c), .block_size = 256 },
    };

    Mel_Slab_Alloc slab;
    mel_slab_init(&slab, classes, descs, 3);

    void* tiny = mel_slab_alloc(&slab, 4);
    void* medium = mel_slab_alloc(&slab, 50);
    void* big = mel_slab_alloc(&slab, 200);

    MEL_ASSERT(mel_pool_owns(&slab.classes[0].pool, tiny));
    MEL_ASSERT(mel_pool_owns(&slab.classes[1].pool, medium));
    MEL_ASSERT(mel_pool_owns(&slab.classes[2].pool, big));

    mel_slab_free(&slab, tiny);
    mel_slab_free(&slab, medium);
    mel_slab_free(&slab, big);
}

MEL_TEST(slab_data_integrity, .tags = "allocator")
{
    _Alignas(16) u8 buf[1024];

    Mel_Slab_Class classes[1];
    Mel_Slab_Class_Desc descs[] = {
        { .buffer = buf, .buffer_size = sizeof(buf), .block_size = 64 },
    };

    Mel_Slab_Alloc slab;
    mel_slab_init(&slab, classes, descs, 1);

    typedef struct { i32 id; f32 value; } Entry;

    Entry* a = (Entry*)mel_slab_alloc(&slab, sizeof(Entry));
    Entry* b = (Entry*)mel_slab_alloc(&slab, sizeof(Entry));
    *a = (Entry){1, 3.14f};
    *b = (Entry){2, 2.71f};

    MEL_ASSERT_EQ(a->id, 1);
    MEL_ASSERT_FLOAT_EQ(a->value, 3.14f, 0.01f);
    MEL_ASSERT_EQ(b->id, 2);
    MEL_ASSERT_FLOAT_EQ(b->value, 2.71f, 0.01f);

    mel_slab_free(&slab, a);
    mel_slab_free(&slab, b);
}