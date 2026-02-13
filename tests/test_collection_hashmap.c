#include "../melody/test.harness.h"
#include "../melody/allocator.h"
#include "../melody/allocator.heap.h"
#include "../melody/collection.hashmap.h"
#include "../melody/hash.xxh.h"

#include <string.h>

#define KEY(v) ((void*)(usize)(v))
#define VAL(v) ((void*)(usize)(v))
#define AS_INT(v) ((i64)(isize)(v))

MEL_TEST(init_empty)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_HashMap hm;
    mel_hashmap_init(&hm, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    MEL_ASSERT(mel_hashmap_empty(&hm));
    MEL_ASSERT_EQ(mel_hashmap_count(&hm), (usize)0);
    MEL_ASSERT_NULL(hm.entries);

    mel_hashmap_free(&hm);
    MEL_PASS();
}

MEL_TEST(put_single)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_HashMap hm;
    mel_hashmap_init(&hm, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    MEL_ASSERT(mel_hashmap_put(&hm, KEY(42), VAL(100)));
    MEL_ASSERT_EQ(mel_hashmap_count(&hm), (usize)1);

    void* val = mel_hashmap_get(&hm, KEY(42));
    MEL_ASSERT_EQ(AS_INT(val), 100);

    mel_hashmap_free(&hm);
    MEL_PASS();
}

MEL_TEST(put_multiple)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_HashMap hm;
    mel_hashmap_init(&hm, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    for (i32 i = 1; i <= 10; i++)
    {
        MEL_ASSERT(mel_hashmap_put(&hm, KEY(i), VAL(i * 10)));
    }

    MEL_ASSERT_EQ(mel_hashmap_count(&hm), (usize)10);

    for (i32 i = 1; i <= 10; i++)
    {
        void* val = mel_hashmap_get(&hm, KEY(i));
        MEL_ASSERT_EQ(AS_INT(val), i * 10);
    }

    mel_hashmap_free(&hm);
    MEL_PASS();
}

MEL_TEST(update_existing)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_HashMap hm;
    mel_hashmap_init(&hm, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    MEL_ASSERT(mel_hashmap_put(&hm, KEY(5), VAL(50)));
    MEL_ASSERT(!mel_hashmap_put(&hm, KEY(5), VAL(500)));
    MEL_ASSERT_EQ(mel_hashmap_count(&hm), (usize)1);

    void* val = mel_hashmap_get(&hm, KEY(5));
    MEL_ASSERT_EQ(AS_INT(val), 500);

    mel_hashmap_free(&hm);
    MEL_PASS();
}

MEL_TEST(get_nonexistent)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_HashMap hm;
    mel_hashmap_init(&hm, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    mel_hashmap_put(&hm, KEY(1), VAL(10));

    void* val = mel_hashmap_get(&hm, KEY(99));
    MEL_ASSERT_NULL(val);

    val = mel_hashmap_get(&hm, KEY(0));
    MEL_ASSERT_NULL(val);

    mel_hashmap_free(&hm);
    MEL_PASS();
}

MEL_TEST(contains)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_HashMap hm;
    mel_hashmap_init(&hm, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    mel_hashmap_put(&hm, KEY(10), VAL(100));
    mel_hashmap_put(&hm, KEY(20), VAL(200));

    MEL_ASSERT(mel_hashmap_contains(&hm, KEY(10)));
    MEL_ASSERT(mel_hashmap_contains(&hm, KEY(20)));
    MEL_ASSERT(!mel_hashmap_contains(&hm, KEY(30)));
    MEL_ASSERT(!mel_hashmap_contains(&hm, KEY(0)));

    mel_hashmap_free(&hm);
    MEL_PASS();
}

MEL_TEST(remove_existing)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_HashMap hm;
    mel_hashmap_init(&hm, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    mel_hashmap_put(&hm, KEY(1), VAL(10));
    mel_hashmap_put(&hm, KEY(2), VAL(20));
    mel_hashmap_put(&hm, KEY(3), VAL(30));

    MEL_ASSERT(mel_hashmap_remove(&hm, KEY(2)));
    MEL_ASSERT_EQ(mel_hashmap_count(&hm), (usize)2);
    MEL_ASSERT(!mel_hashmap_contains(&hm, KEY(2)));
    MEL_ASSERT(mel_hashmap_contains(&hm, KEY(1)));
    MEL_ASSERT(mel_hashmap_contains(&hm, KEY(3)));

    mel_hashmap_free(&hm);
    MEL_PASS();
}

MEL_TEST(remove_nonexistent)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_HashMap hm;
    mel_hashmap_init(&hm, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    mel_hashmap_put(&hm, KEY(1), VAL(10));

    MEL_ASSERT(!mel_hashmap_remove(&hm, KEY(99)));
    MEL_ASSERT_EQ(mel_hashmap_count(&hm), (usize)1);

    mel_hashmap_free(&hm);
    MEL_PASS();
}

static u64 hash_mod16(const void* key)
{
    u64 val = (u64)(usize)key;
    return val;
}

MEL_TEST(backward_shift_deletion)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_HashMap hm;
    mel_hashmap_init(&hm, hash_mod16, mel_hashmap_eq_u64, alloc);

    mel_hashmap_put(&hm, KEY(1), VAL(10));
    mel_hashmap_put(&hm, KEY(17), VAL(170));
    mel_hashmap_put(&hm, KEY(33), VAL(330));

    MEL_ASSERT(mel_hashmap_remove(&hm, KEY(17)));

    MEL_ASSERT(mel_hashmap_contains(&hm, KEY(1)));
    MEL_ASSERT(!mel_hashmap_contains(&hm, KEY(17)));
    MEL_ASSERT(mel_hashmap_contains(&hm, KEY(33)));

    MEL_ASSERT_EQ(AS_INT(mel_hashmap_get(&hm, KEY(1))), 10);
    MEL_ASSERT_EQ(AS_INT(mel_hashmap_get(&hm, KEY(33))), 330);

    mel_hashmap_free(&hm);
    MEL_PASS();
}

MEL_TEST(growth)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_HashMap hm;
    mel_hashmap_init(&hm, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    for (i32 i = 1; i <= 20; i++)
    {
        mel_hashmap_put(&hm, KEY(i), VAL(i * 7));
    }

    MEL_ASSERT_EQ(mel_hashmap_count(&hm), (usize)20);
    MEL_ASSERT_GT(hm.capacity, (usize)16);

    for (i32 i = 1; i <= 20; i++)
    {
        void* val = mel_hashmap_get(&hm, KEY(i));
        MEL_ASSERT_EQ(AS_INT(val), i * 7);
    }

    mel_hashmap_free(&hm);
    MEL_PASS();
}

MEL_TEST(clear)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_HashMap hm;
    mel_hashmap_init(&hm, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    for (i32 i = 1; i <= 10; i++)
    {
        mel_hashmap_put(&hm, KEY(i), VAL(i));
    }

    usize cap_before = hm.capacity;
    mel_hashmap_clear(&hm);

    MEL_ASSERT(mel_hashmap_empty(&hm));
    MEL_ASSERT_EQ(mel_hashmap_count(&hm), (usize)0);
    MEL_ASSERT_EQ(hm.capacity, cap_before);
    MEL_ASSERT_NOT_NULL(hm.entries);

    mel_hashmap_put(&hm, KEY(42), VAL(420));
    MEL_ASSERT_EQ(AS_INT(mel_hashmap_get(&hm, KEY(42))), 420);

    mel_hashmap_free(&hm);
    MEL_PASS();
}

MEL_TEST(reserve)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_HashMap hm;
    mel_hashmap_init(&hm, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    mel_hashmap_reserve(&hm, 100);
    MEL_ASSERT_GE(hm.capacity, (usize)134);

    usize cap_before = hm.capacity;

    for (i32 i = 1; i <= 100; i++)
    {
        mel_hashmap_put(&hm, KEY(i), VAL(i));
    }

    MEL_ASSERT_EQ(hm.capacity, cap_before);

    for (i32 i = 1; i <= 100; i++)
    {
        MEL_ASSERT_EQ(AS_INT(mel_hashmap_get(&hm, KEY(i))), i);
    }

    mel_hashmap_free(&hm);
    MEL_PASS();
}

MEL_TEST(count_empty_tracking)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_HashMap hm;
    mel_hashmap_init(&hm, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    MEL_ASSERT(mel_hashmap_empty(&hm));
    MEL_ASSERT_EQ(mel_hashmap_count(&hm), (usize)0);

    mel_hashmap_put(&hm, KEY(1), VAL(1));
    MEL_ASSERT(!mel_hashmap_empty(&hm));
    MEL_ASSERT_EQ(mel_hashmap_count(&hm), (usize)1);

    mel_hashmap_put(&hm, KEY(2), VAL(2));
    MEL_ASSERT_EQ(mel_hashmap_count(&hm), (usize)2);

    mel_hashmap_remove(&hm, KEY(1));
    MEL_ASSERT_EQ(mel_hashmap_count(&hm), (usize)1);

    mel_hashmap_remove(&hm, KEY(2));
    MEL_ASSERT(mel_hashmap_empty(&hm));
    MEL_ASSERT_EQ(mel_hashmap_count(&hm), (usize)0);

    mel_hashmap_free(&hm);
    MEL_PASS();
}

MEL_TEST(foreach_iteration)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_HashMap hm;
    mel_hashmap_init(&hm, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    for (i32 i = 1; i <= 10; i++)
    {
        mel_hashmap_put(&hm, KEY(i), VAL(i * 10));
    }

    usize iter_count = 0;
    i64 key_sum = 0;
    i64 val_sum = 0;

    mel_hashmap_foreach(&hm, k, v, {
        key_sum += AS_INT(k);
        val_sum += AS_INT(v);
        iter_count++;
    });

    MEL_ASSERT_EQ(iter_count, (usize)10);
    MEL_ASSERT_EQ(key_sum, (i64)55);
    MEL_ASSERT_EQ(val_sum, (i64)550);

    mel_hashmap_free(&hm);
    MEL_PASS();
}

MEL_TEST(string_keys)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_HashMap hm;
    mel_hashmap_init(&hm, mel_hashmap_hash_str, mel_hashmap_eq_str, alloc);

    mel_hashmap_put(&hm, (void*)"hello", VAL(1));
    mel_hashmap_put(&hm, (void*)"world", VAL(2));
    mel_hashmap_put(&hm, (void*)"melody", VAL(3));

    MEL_ASSERT_EQ(mel_hashmap_count(&hm), (usize)3);

    MEL_ASSERT_EQ(AS_INT(mel_hashmap_get(&hm, (void*)"hello")), 1);
    MEL_ASSERT_EQ(AS_INT(mel_hashmap_get(&hm, (void*)"world")), 2);
    MEL_ASSERT_EQ(AS_INT(mel_hashmap_get(&hm, (void*)"melody")), 3);
    MEL_ASSERT_NULL(mel_hashmap_get(&hm, (void*)"missing"));

    MEL_ASSERT(mel_hashmap_contains(&hm, (void*)"hello"));
    MEL_ASSERT(!mel_hashmap_contains(&hm, (void*)"nope"));

    mel_hashmap_free(&hm);
    MEL_PASS();
}

MEL_TEST(pointer_keys)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_HashMap hm;
    mel_hashmap_init(&hm, mel_hashmap_hash_ptr, mel_hashmap_eq_u64, alloc);

    i32 a = 10, b = 20, c = 30;

    mel_hashmap_put(&hm, &a, VAL(100));
    mel_hashmap_put(&hm, &b, VAL(200));
    mel_hashmap_put(&hm, &c, VAL(300));

    MEL_ASSERT_EQ(AS_INT(mel_hashmap_get(&hm, &a)), 100);
    MEL_ASSERT_EQ(AS_INT(mel_hashmap_get(&hm, &b)), 200);
    MEL_ASSERT_EQ(AS_INT(mel_hashmap_get(&hm, &c)), 300);

    mel_hashmap_free(&hm);
    MEL_PASS();
}

MEL_TEST(stress)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_HashMap hm;
    mel_hashmap_init(&hm, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    for (i32 i = 1; i <= 1000; i++)
    {
        mel_hashmap_put(&hm, KEY(i), VAL(i * 3));
    }

    MEL_ASSERT_EQ(mel_hashmap_count(&hm), (usize)1000);

    for (i32 i = 1; i <= 1000; i++)
    {
        MEL_ASSERT_EQ(AS_INT(mel_hashmap_get(&hm, KEY(i))), i * 3);
    }

    for (i32 i = 1; i <= 500; i++)
    {
        MEL_ASSERT(mel_hashmap_remove(&hm, KEY(i)));
    }

    MEL_ASSERT_EQ(mel_hashmap_count(&hm), (usize)500);

    for (i32 i = 1; i <= 500; i++)
    {
        MEL_ASSERT(!mel_hashmap_contains(&hm, KEY(i)));
    }

    for (i32 i = 501; i <= 1000; i++)
    {
        MEL_ASSERT_EQ(AS_INT(mel_hashmap_get(&hm, KEY(i))), i * 3);
    }

    mel_hashmap_free(&hm);
    MEL_PASS();
}

MEL_TEST(collision_handling)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_HashMap hm;
    mel_hashmap_init(&hm, hash_mod16, mel_hashmap_eq_u64, alloc);

    mel_hashmap_put(&hm, KEY(0), VAL(100));
    mel_hashmap_put(&hm, KEY(16), VAL(200));
    mel_hashmap_put(&hm, KEY(32), VAL(300));
    mel_hashmap_put(&hm, KEY(48), VAL(400));

    MEL_ASSERT_EQ(mel_hashmap_count(&hm), (usize)4);
    MEL_ASSERT_EQ(AS_INT(mel_hashmap_get(&hm, KEY(0))), 100);
    MEL_ASSERT_EQ(AS_INT(mel_hashmap_get(&hm, KEY(16))), 200);
    MEL_ASSERT_EQ(AS_INT(mel_hashmap_get(&hm, KEY(32))), 300);
    MEL_ASSERT_EQ(AS_INT(mel_hashmap_get(&hm, KEY(48))), 400);

    mel_hashmap_free(&hm);
    MEL_PASS();
}

MEL_TEST(remove_probe_chain)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_HashMap hm;
    mel_hashmap_init(&hm, hash_mod16, mel_hashmap_eq_u64, alloc);

    mel_hashmap_put(&hm, KEY(1), VAL(10));
    mel_hashmap_put(&hm, KEY(17), VAL(170));
    mel_hashmap_put(&hm, KEY(33), VAL(330));

    MEL_ASSERT(mel_hashmap_remove(&hm, KEY(17)));
    MEL_ASSERT_EQ(mel_hashmap_count(&hm), (usize)2);

    MEL_ASSERT(mel_hashmap_contains(&hm, KEY(1)));
    MEL_ASSERT(mel_hashmap_contains(&hm, KEY(33)));
    MEL_ASSERT(!mel_hashmap_contains(&hm, KEY(17)));

    MEL_ASSERT_EQ(AS_INT(mel_hashmap_get(&hm, KEY(1))), 10);
    MEL_ASSERT_EQ(AS_INT(mel_hashmap_get(&hm, KEY(33))), 330);

    mel_hashmap_free(&hm);
    MEL_PASS();
}

MEL_TEST(large_rehash)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_HashMap hm;
    mel_hashmap_init(&hm, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    for (i32 i = 1; i <= 10000; i++)
    {
        mel_hashmap_put(&hm, KEY(i), VAL(i));
    }

    MEL_ASSERT_EQ(mel_hashmap_count(&hm), (usize)10000);

    for (i32 i = 1; i <= 10000; i++)
    {
        MEL_ASSERT_EQ(AS_INT(mel_hashmap_get(&hm, KEY(i))), i);
    }

    mel_hashmap_free(&hm);
    MEL_PASS();
}

MEL_TEST(get_empty_map)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_HashMap hm;
    mel_hashmap_init(&hm, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    MEL_ASSERT_NULL(mel_hashmap_get(&hm, KEY(1)));
    MEL_ASSERT(!mel_hashmap_contains(&hm, KEY(1)));
    MEL_ASSERT(!mel_hashmap_remove(&hm, KEY(1)));

    mel_hashmap_free(&hm);
    MEL_PASS();
}

MEL_TEST(remove_all_then_reinsert)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_HashMap hm;
    mel_hashmap_init(&hm, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    for (i32 i = 1; i <= 5; i++)
    {
        mel_hashmap_put(&hm, KEY(i), VAL(i));
    }

    for (i32 i = 1; i <= 5; i++)
    {
        mel_hashmap_remove(&hm, KEY(i));
    }

    MEL_ASSERT(mel_hashmap_empty(&hm));

    for (i32 i = 10; i <= 15; i++)
    {
        mel_hashmap_put(&hm, KEY(i), VAL(i * 2));
    }

    MEL_ASSERT_EQ(mel_hashmap_count(&hm), (usize)6);

    for (i32 i = 10; i <= 15; i++)
    {
        MEL_ASSERT_EQ(AS_INT(mel_hashmap_get(&hm, KEY(i))), i * 2);
    }

    mel_hashmap_free(&hm);
    MEL_PASS();
}

int main(void)
{
    MEL_TEST_BEGIN("HashMap Tests");

    MEL_RUN_TEST(init_empty);
    MEL_RUN_TEST(put_single);
    MEL_RUN_TEST(put_multiple);
    MEL_RUN_TEST(update_existing);
    MEL_RUN_TEST(get_nonexistent);
    MEL_RUN_TEST(contains);
    MEL_RUN_TEST(remove_existing);
    MEL_RUN_TEST(remove_nonexistent);
    MEL_RUN_TEST(backward_shift_deletion);
    MEL_RUN_TEST(growth);
    MEL_RUN_TEST(clear);
    MEL_RUN_TEST(reserve);
    MEL_RUN_TEST(count_empty_tracking);
    MEL_RUN_TEST(foreach_iteration);
    MEL_RUN_TEST(string_keys);
    MEL_RUN_TEST(pointer_keys);
    MEL_RUN_TEST(stress);
    MEL_RUN_TEST(collision_handling);
    MEL_RUN_TEST(remove_probe_chain);
    MEL_RUN_TEST(large_rehash);
    MEL_RUN_TEST(get_empty_map);
    MEL_RUN_TEST(remove_all_then_reinsert);

    MEL_TEST_END();
    return MEL_TEST_EXIT_CODE();
}
