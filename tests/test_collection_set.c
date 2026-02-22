#include "../melody/test.harness.h"
#include "../melody/allocator.h"
#include "../melody/allocator.heap.h"
#include "../melody/collection.set.h"
#include "../melody/collection.hashmap.h"
#include "../melody/hash.xxh.h"

#include <string.h>

#define KEY(v) ((void*)(usize)(v))
#define AS_INT(v) ((i64)(isize)(v))

MEL_TEST(init_empty, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Set s;
    mel_set_init(&s, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    MEL_ASSERT(mel_set_empty(&s));
    MEL_ASSERT_EQ(mel_set_count(&s), (usize)0);

    mel_set_free(&s);
}

MEL_TEST(add_single, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Set s;
    mel_set_init(&s, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    MEL_ASSERT(mel_set_add(&s, KEY(42)));
    MEL_ASSERT(mel_set_contains(&s, KEY(42)));
    MEL_ASSERT_EQ(mel_set_count(&s), (usize)1);

    mel_set_free(&s);
}

MEL_TEST(add_multiple, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Set s;
    mel_set_init(&s, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    for (i32 i = 1; i <= 10; i++)
    {
        MEL_ASSERT(mel_set_add(&s, KEY(i)));
    }

    MEL_ASSERT_EQ(mel_set_count(&s), (usize)10);

    for (i32 i = 1; i <= 10; i++)
    {
        MEL_ASSERT(mel_set_contains(&s, KEY(i)));
    }

    mel_set_free(&s);
}

MEL_TEST(add_duplicate, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Set s;
    mel_set_init(&s, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    MEL_ASSERT(mel_set_add(&s, KEY(5)));
    MEL_ASSERT(!mel_set_add(&s, KEY(5)));
    MEL_ASSERT_EQ(mel_set_count(&s), (usize)1);

    mel_set_free(&s);
}

MEL_TEST(contains_hit_and_miss, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Set s;
    mel_set_init(&s, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    mel_set_add(&s, KEY(10));
    mel_set_add(&s, KEY(20));

    MEL_ASSERT(mel_set_contains(&s, KEY(10)));
    MEL_ASSERT(mel_set_contains(&s, KEY(20)));
    MEL_ASSERT(!mel_set_contains(&s, KEY(30)));
    MEL_ASSERT(!mel_set_contains(&s, KEY(0)));

    mel_set_free(&s);
}

MEL_TEST(remove_existing, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Set s;
    mel_set_init(&s, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    mel_set_add(&s, KEY(1));
    mel_set_add(&s, KEY(2));
    mel_set_add(&s, KEY(3));

    MEL_ASSERT(mel_set_remove(&s, KEY(2)));
    MEL_ASSERT_EQ(mel_set_count(&s), (usize)2);
    MEL_ASSERT(!mel_set_contains(&s, KEY(2)));
    MEL_ASSERT(mel_set_contains(&s, KEY(1)));
    MEL_ASSERT(mel_set_contains(&s, KEY(3)));

    mel_set_free(&s);
}

MEL_TEST(remove_nonexistent, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Set s;
    mel_set_init(&s, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    mel_set_add(&s, KEY(1));

    MEL_ASSERT(!mel_set_remove(&s, KEY(99)));
    MEL_ASSERT_EQ(mel_set_count(&s), (usize)1);

    mel_set_free(&s);
}

MEL_TEST(count_empty_tracking, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Set s;
    mel_set_init(&s, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    MEL_ASSERT(mel_set_empty(&s));
    MEL_ASSERT_EQ(mel_set_count(&s), (usize)0);

    mel_set_add(&s, KEY(1));
    MEL_ASSERT(!mel_set_empty(&s));
    MEL_ASSERT_EQ(mel_set_count(&s), (usize)1);

    mel_set_add(&s, KEY(2));
    MEL_ASSERT_EQ(mel_set_count(&s), (usize)2);

    mel_set_remove(&s, KEY(1));
    MEL_ASSERT_EQ(mel_set_count(&s), (usize)1);

    mel_set_remove(&s, KEY(2));
    MEL_ASSERT(mel_set_empty(&s));
    MEL_ASSERT_EQ(mel_set_count(&s), (usize)0);

    mel_set_free(&s);
}

MEL_TEST(clear_and_reuse, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Set s;
    mel_set_init(&s, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    for (i32 i = 1; i <= 10; i++)
    {
        mel_set_add(&s, KEY(i));
    }

    mel_set_clear(&s);
    MEL_ASSERT(mel_set_empty(&s));
    MEL_ASSERT_EQ(mel_set_count(&s), (usize)0);

    mel_set_add(&s, KEY(42));
    MEL_ASSERT(mel_set_contains(&s, KEY(42)));
    MEL_ASSERT_EQ(mel_set_count(&s), (usize)1);

    mel_set_free(&s);
}

MEL_TEST(reserve, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Set s;
    mel_set_init(&s, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    mel_set_reserve(&s, 100);
    usize cap_before = s.map.capacity;

    for (i32 i = 1; i <= 100; i++)
    {
        mel_set_add(&s, KEY(i));
    }

    MEL_ASSERT_EQ(s.map.capacity, cap_before);
    MEL_ASSERT_EQ(mel_set_count(&s), (usize)100);

    mel_set_free(&s);
}

MEL_TEST(foreach_iteration, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Set s;
    mel_set_init(&s, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    for (i32 i = 1; i <= 10; i++)
    {
        mel_set_add(&s, KEY(i));
    }

    usize iter_count = 0;
    i64 key_sum = 0;

    mel_set_foreach(&s, k, {
        key_sum += AS_INT(k);
        iter_count++;
    });

    MEL_ASSERT_EQ(iter_count, (usize)10);
    MEL_ASSERT_EQ(key_sum, (i64)55);

    mel_set_free(&s);
}

MEL_TEST(set_union, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Set a, b, dst;
    mel_set_init(&a, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);
    mel_set_init(&b, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);
    mel_set_init(&dst, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    mel_set_add(&a, KEY(1));
    mel_set_add(&a, KEY(2));
    mel_set_add(&a, KEY(3));

    mel_set_add(&b, KEY(3));
    mel_set_add(&b, KEY(4));
    mel_set_add(&b, KEY(5));

    mel_set_union(&dst, &a, &b);

    MEL_ASSERT_EQ(mel_set_count(&dst), (usize)5);
    MEL_ASSERT(mel_set_contains(&dst, KEY(1)));
    MEL_ASSERT(mel_set_contains(&dst, KEY(2)));
    MEL_ASSERT(mel_set_contains(&dst, KEY(3)));
    MEL_ASSERT(mel_set_contains(&dst, KEY(4)));
    MEL_ASSERT(mel_set_contains(&dst, KEY(5)));

    mel_set_free(&a);
    mel_set_free(&b);
    mel_set_free(&dst);
}

MEL_TEST(set_intersection, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Set a, b, dst;
    mel_set_init(&a, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);
    mel_set_init(&b, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);
    mel_set_init(&dst, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    mel_set_add(&a, KEY(1));
    mel_set_add(&a, KEY(2));
    mel_set_add(&a, KEY(3));

    mel_set_add(&b, KEY(2));
    mel_set_add(&b, KEY(3));
    mel_set_add(&b, KEY(4));

    mel_set_intersection(&dst, &a, &b);

    MEL_ASSERT_EQ(mel_set_count(&dst), (usize)2);
    MEL_ASSERT(mel_set_contains(&dst, KEY(2)));
    MEL_ASSERT(mel_set_contains(&dst, KEY(3)));
    MEL_ASSERT(!mel_set_contains(&dst, KEY(1)));
    MEL_ASSERT(!mel_set_contains(&dst, KEY(4)));

    mel_set_free(&a);
    mel_set_free(&b);
    mel_set_free(&dst);
}

MEL_TEST(set_difference, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Set a, b, dst;
    mel_set_init(&a, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);
    mel_set_init(&b, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);
    mel_set_init(&dst, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    mel_set_add(&a, KEY(1));
    mel_set_add(&a, KEY(2));
    mel_set_add(&a, KEY(3));

    mel_set_add(&b, KEY(2));
    mel_set_add(&b, KEY(3));
    mel_set_add(&b, KEY(4));

    mel_set_difference(&dst, &a, &b);

    MEL_ASSERT_EQ(mel_set_count(&dst), (usize)1);
    MEL_ASSERT(mel_set_contains(&dst, KEY(1)));
    MEL_ASSERT(!mel_set_contains(&dst, KEY(2)));
    MEL_ASSERT(!mel_set_contains(&dst, KEY(3)));
    MEL_ASSERT(!mel_set_contains(&dst, KEY(4)));

    mel_set_free(&a);
    mel_set_free(&b);
    mel_set_free(&dst);
}

MEL_TEST(set_symmetric_difference, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Set a, b, dst;
    mel_set_init(&a, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);
    mel_set_init(&b, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);
    mel_set_init(&dst, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    mel_set_add(&a, KEY(1));
    mel_set_add(&a, KEY(2));
    mel_set_add(&a, KEY(3));

    mel_set_add(&b, KEY(2));
    mel_set_add(&b, KEY(3));
    mel_set_add(&b, KEY(4));

    mel_set_symmetric_difference(&dst, &a, &b);

    MEL_ASSERT_EQ(mel_set_count(&dst), (usize)2);
    MEL_ASSERT(mel_set_contains(&dst, KEY(1)));
    MEL_ASSERT(mel_set_contains(&dst, KEY(4)));
    MEL_ASSERT(!mel_set_contains(&dst, KEY(2)));
    MEL_ASSERT(!mel_set_contains(&dst, KEY(3)));

    mel_set_free(&a);
    mel_set_free(&b);
    mel_set_free(&dst);
}

MEL_TEST(is_subset, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Set a, b;
    mel_set_init(&a, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);
    mel_set_init(&b, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    mel_set_add(&a, KEY(1));
    mel_set_add(&a, KEY(2));

    mel_set_add(&b, KEY(1));
    mel_set_add(&b, KEY(2));
    mel_set_add(&b, KEY(3));

    MEL_ASSERT(mel_set_is_subset(&a, &b));

    mel_set_clear(&a);
    mel_set_add(&a, KEY(1));
    mel_set_add(&a, KEY(4));

    MEL_ASSERT(!mel_set_is_subset(&a, &b));

    mel_set_free(&a);
    mel_set_free(&b);
}

MEL_TEST(is_superset, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Set a, b;
    mel_set_init(&a, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);
    mel_set_init(&b, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    mel_set_add(&a, KEY(1));
    mel_set_add(&a, KEY(2));
    mel_set_add(&a, KEY(3));

    mel_set_add(&b, KEY(1));
    mel_set_add(&b, KEY(2));

    MEL_ASSERT(mel_set_is_superset(&a, &b));
    MEL_ASSERT(!mel_set_is_superset(&b, &a));

    mel_set_free(&a);
    mel_set_free(&b);
}

MEL_TEST(equals, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Set a, b;
    mel_set_init(&a, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);
    mel_set_init(&b, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    mel_set_add(&a, KEY(1));
    mel_set_add(&a, KEY(2));
    mel_set_add(&a, KEY(3));

    mel_set_add(&b, KEY(3));
    mel_set_add(&b, KEY(1));
    mel_set_add(&b, KEY(2));

    MEL_ASSERT(mel_set_equals(&a, &b));

    mel_set_add(&b, KEY(4));
    MEL_ASSERT(!mel_set_equals(&a, &b));

    mel_set_free(&a);
    mel_set_free(&b);
}

MEL_TEST(string_elements, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Set s;
    mel_set_init(&s, mel_hashmap_hash_str, mel_hashmap_eq_str, alloc);

    mel_set_add(&s, (void*)"hello");
    mel_set_add(&s, (void*)"world");
    mel_set_add(&s, (void*)"melody");

    MEL_ASSERT_EQ(mel_set_count(&s), (usize)3);
    MEL_ASSERT(mel_set_contains(&s, (void*)"hello"));
    MEL_ASSERT(mel_set_contains(&s, (void*)"world"));
    MEL_ASSERT(mel_set_contains(&s, (void*)"melody"));
    MEL_ASSERT(!mel_set_contains(&s, (void*)"missing"));

    mel_set_free(&s);
}

MEL_TEST(stress, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Set s;
    mel_set_init(&s, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    for (i32 i = 1; i <= 1000; i++)
    {
        mel_set_add(&s, KEY(i));
    }

    MEL_ASSERT_EQ(mel_set_count(&s), (usize)1000);

    for (i32 i = 1; i <= 1000; i++)
    {
        MEL_ASSERT(mel_set_contains(&s, KEY(i)));
    }

    for (i32 i = 1; i <= 500; i++)
    {
        MEL_ASSERT(mel_set_remove(&s, KEY(i)));
    }

    MEL_ASSERT_EQ(mel_set_count(&s), (usize)500);

    for (i32 i = 1; i <= 500; i++)
    {
        MEL_ASSERT(!mel_set_contains(&s, KEY(i)));
    }

    for (i32 i = 501; i <= 1000; i++)
    {
        MEL_ASSERT(mel_set_contains(&s, KEY(i)));
    }

    mel_set_free(&s);
}

MEL_TEST(operations_on_empty, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Set a, b, dst;
    mel_set_init(&a, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);
    mel_set_init(&b, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);
    mel_set_init(&dst, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    mel_set_union(&dst, &a, &b);
    MEL_ASSERT_EQ(mel_set_count(&dst), (usize)0);

    mel_set_intersection(&dst, &a, &b);
    MEL_ASSERT_EQ(mel_set_count(&dst), (usize)0);

    mel_set_difference(&dst, &a, &b);
    MEL_ASSERT_EQ(mel_set_count(&dst), (usize)0);

    mel_set_symmetric_difference(&dst, &a, &b);
    MEL_ASSERT_EQ(mel_set_count(&dst), (usize)0);

    MEL_ASSERT(mel_set_is_subset(&a, &b));
    MEL_ASSERT(mel_set_is_superset(&a, &b));
    MEL_ASSERT(mel_set_equals(&a, &b));

    mel_set_free(&a);
    mel_set_free(&b);
    mel_set_free(&dst);
}

MEL_TEST(operations_on_disjoint, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Set a, b, dst;
    mel_set_init(&a, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);
    mel_set_init(&b, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);
    mel_set_init(&dst, mel_hashmap_hash_u64, mel_hashmap_eq_u64, alloc);

    mel_set_add(&a, KEY(1));
    mel_set_add(&a, KEY(2));

    mel_set_add(&b, KEY(3));
    mel_set_add(&b, KEY(4));

    mel_set_union(&dst, &a, &b);
    MEL_ASSERT_EQ(mel_set_count(&dst), (usize)4);

    mel_set_clear(&dst);
    mel_set_intersection(&dst, &a, &b);
    MEL_ASSERT_EQ(mel_set_count(&dst), (usize)0);

    mel_set_clear(&dst);
    mel_set_difference(&dst, &a, &b);
    MEL_ASSERT_EQ(mel_set_count(&dst), (usize)2);
    MEL_ASSERT(mel_set_contains(&dst, KEY(1)));
    MEL_ASSERT(mel_set_contains(&dst, KEY(2)));

    mel_set_clear(&dst);
    mel_set_symmetric_difference(&dst, &a, &b);
    MEL_ASSERT_EQ(mel_set_count(&dst), (usize)4);

    MEL_ASSERT(!mel_set_is_subset(&a, &b));
    MEL_ASSERT(!mel_set_equals(&a, &b));

    mel_set_free(&a);
    mel_set_free(&b);
    mel_set_free(&dst);
}
