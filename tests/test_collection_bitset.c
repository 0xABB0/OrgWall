#include "../melody/test.harness.h"
#include "../melody/allocator.h"
#include "../melody/allocator.heap.h"
#include "../melody/collection.bitset.h"

MEL_TEST(init_all_zero, .tags = "collection")
{
    Mel_BitSet bs;
    mel_bitset_init(&bs, 128, mel_alloc_heap());
    MEL_ASSERT_EQ(bs.bit_count, (usize)128);
    MEL_ASSERT_EQ(bs.word_count, (usize)2);
    MEL_ASSERT_NOT_NULL(bs.words);
    for (usize i = 0; i < 128; i++)
        MEL_ASSERT(!mel_bitset_get(&bs, i));
    MEL_ASSERT_EQ(mel_bitset_count_set(&bs), (usize)0);
    mel_bitset_free(&bs);
}

MEL_TEST(set_get, .tags = "collection")
{
    Mel_BitSet bs;
    mel_bitset_init(&bs, 256, mel_alloc_heap());
    mel_bitset_set(&bs, 0);
    mel_bitset_set(&bs, 63);
    mel_bitset_set(&bs, 64);
    mel_bitset_set(&bs, 127);
    mel_bitset_set(&bs, 255);
    MEL_ASSERT(mel_bitset_get(&bs, 0));
    MEL_ASSERT(mel_bitset_get(&bs, 63));
    MEL_ASSERT(mel_bitset_get(&bs, 64));
    MEL_ASSERT(mel_bitset_get(&bs, 127));
    MEL_ASSERT(mel_bitset_get(&bs, 255));
    MEL_ASSERT(!mel_bitset_get(&bs, 1));
    MEL_ASSERT(!mel_bitset_get(&bs, 62));
    MEL_ASSERT(!mel_bitset_get(&bs, 65));
    mel_bitset_free(&bs);
}

MEL_TEST(clear_bit, .tags = "collection")
{
    Mel_BitSet bs;
    mel_bitset_init(&bs, 128, mel_alloc_heap());
    mel_bitset_set(&bs, 42);
    MEL_ASSERT(mel_bitset_get(&bs, 42));
    mel_bitset_clear_bit(&bs, 42);
    MEL_ASSERT(!mel_bitset_get(&bs, 42));
    mel_bitset_free(&bs);
}

MEL_TEST(toggle, .tags = "collection")
{
    Mel_BitSet bs;
    mel_bitset_init(&bs, 64, mel_alloc_heap());
    MEL_ASSERT(!mel_bitset_get(&bs, 10));
    mel_bitset_toggle(&bs, 10);
    MEL_ASSERT(mel_bitset_get(&bs, 10));
    mel_bitset_toggle(&bs, 10);
    MEL_ASSERT(!mel_bitset_get(&bs, 10));
    mel_bitset_free(&bs);
}

MEL_TEST(set_all_clear_all, .tags = "collection")
{
    Mel_BitSet bs;
    mel_bitset_init(&bs, 100, mel_alloc_heap());

    mel_bitset_set_all(&bs);
    MEL_ASSERT_EQ(mel_bitset_count_set(&bs), (usize)100);
    MEL_ASSERT(mel_bitset_all(&bs));
    for (usize i = 0; i < 100; i++)
        MEL_ASSERT(mel_bitset_get(&bs, i));

    mel_bitset_clear(&bs);
    MEL_ASSERT_EQ(mel_bitset_count_set(&bs), (usize)0);
    MEL_ASSERT(mel_bitset_none(&bs));
    for (usize i = 0; i < 100; i++)
        MEL_ASSERT(!mel_bitset_get(&bs, i));

    mel_bitset_free(&bs);
}

MEL_TEST(count_set_clear, .tags = "collection")
{
    Mel_BitSet bs;
    mel_bitset_init(&bs, 200, mel_alloc_heap());
    mel_bitset_set(&bs, 0);
    mel_bitset_set(&bs, 50);
    mel_bitset_set(&bs, 100);
    mel_bitset_set(&bs, 199);
    MEL_ASSERT_EQ(mel_bitset_count_set(&bs), (usize)4);
    MEL_ASSERT_EQ(mel_bitset_count_clear(&bs), (usize)196);
    mel_bitset_free(&bs);
}

MEL_TEST(any_none_all, .tags = "collection")
{
    Mel_BitSet bs;
    mel_bitset_init(&bs, 64, mel_alloc_heap());

    MEL_ASSERT(mel_bitset_none(&bs));
    MEL_ASSERT(!mel_bitset_any(&bs));
    MEL_ASSERT(!mel_bitset_all(&bs));

    mel_bitset_set(&bs, 0);
    MEL_ASSERT(mel_bitset_any(&bs));
    MEL_ASSERT(!mel_bitset_none(&bs));
    MEL_ASSERT(!mel_bitset_all(&bs));

    mel_bitset_set_all(&bs);
    MEL_ASSERT(mel_bitset_any(&bs));
    MEL_ASSERT(!mel_bitset_none(&bs));
    MEL_ASSERT(mel_bitset_all(&bs));

    mel_bitset_free(&bs);
}

MEL_TEST(bitwise_and, .tags = "collection")
{
    Mel_BitSet a, b, dst;
    mel_bitset_init(&a, 128, mel_alloc_heap());
    mel_bitset_init(&b, 128, mel_alloc_heap());
    mel_bitset_init(&dst, 128, mel_alloc_heap());

    mel_bitset_set(&a, 0);
    mel_bitset_set(&a, 1);
    mel_bitset_set(&a, 64);
    mel_bitset_set(&b, 1);
    mel_bitset_set(&b, 64);
    mel_bitset_set(&b, 65);

    mel_bitset_and(&dst, &a, &b);
    MEL_ASSERT(!mel_bitset_get(&dst, 0));
    MEL_ASSERT(mel_bitset_get(&dst, 1));
    MEL_ASSERT(mel_bitset_get(&dst, 64));
    MEL_ASSERT(!mel_bitset_get(&dst, 65));
    MEL_ASSERT_EQ(mel_bitset_count_set(&dst), (usize)2);

    mel_bitset_free(&a);
    mel_bitset_free(&b);
    mel_bitset_free(&dst);
}

MEL_TEST(bitwise_or, .tags = "collection")
{
    Mel_BitSet a, b, dst;
    mel_bitset_init(&a, 128, mel_alloc_heap());
    mel_bitset_init(&b, 128, mel_alloc_heap());
    mel_bitset_init(&dst, 128, mel_alloc_heap());

    mel_bitset_set(&a, 0);
    mel_bitset_set(&a, 64);
    mel_bitset_set(&b, 1);
    mel_bitset_set(&b, 64);

    mel_bitset_or(&dst, &a, &b);
    MEL_ASSERT(mel_bitset_get(&dst, 0));
    MEL_ASSERT(mel_bitset_get(&dst, 1));
    MEL_ASSERT(mel_bitset_get(&dst, 64));
    MEL_ASSERT_EQ(mel_bitset_count_set(&dst), (usize)3);

    mel_bitset_free(&a);
    mel_bitset_free(&b);
    mel_bitset_free(&dst);
}

MEL_TEST(bitwise_xor, .tags = "collection")
{
    Mel_BitSet a, b, dst;
    mel_bitset_init(&a, 128, mel_alloc_heap());
    mel_bitset_init(&b, 128, mel_alloc_heap());
    mel_bitset_init(&dst, 128, mel_alloc_heap());

    mel_bitset_set(&a, 0);
    mel_bitset_set(&a, 1);
    mel_bitset_set(&b, 1);
    mel_bitset_set(&b, 2);

    mel_bitset_xor(&dst, &a, &b);
    MEL_ASSERT(mel_bitset_get(&dst, 0));
    MEL_ASSERT(!mel_bitset_get(&dst, 1));
    MEL_ASSERT(mel_bitset_get(&dst, 2));
    MEL_ASSERT_EQ(mel_bitset_count_set(&dst), (usize)2);

    mel_bitset_free(&a);
    mel_bitset_free(&b);
    mel_bitset_free(&dst);
}

MEL_TEST(bitwise_not, .tags = "collection")
{
    Mel_BitSet src, dst;
    mel_bitset_init(&src, 100, mel_alloc_heap());
    mel_bitset_init(&dst, 100, mel_alloc_heap());

    mel_bitset_set(&src, 0);
    mel_bitset_set(&src, 99);

    mel_bitset_not(&dst, &src);
    MEL_ASSERT(!mel_bitset_get(&dst, 0));
    MEL_ASSERT(!mel_bitset_get(&dst, 99));
    MEL_ASSERT(mel_bitset_get(&dst, 1));
    MEL_ASSERT(mel_bitset_get(&dst, 50));
    MEL_ASSERT_EQ(mel_bitset_count_set(&dst), (usize)98);

    mel_bitset_free(&src);
    mel_bitset_free(&dst);
}

MEL_TEST(first_set, .tags = "collection")
{
    Mel_BitSet bs;
    mel_bitset_init(&bs, 256, mel_alloc_heap());

    MEL_ASSERT_EQ(mel_bitset_first_set(&bs), (usize)256);

    mel_bitset_set(&bs, 130);
    MEL_ASSERT_EQ(mel_bitset_first_set(&bs), (usize)130);

    mel_bitset_set(&bs, 5);
    MEL_ASSERT_EQ(mel_bitset_first_set(&bs), (usize)5);

    mel_bitset_free(&bs);
}

MEL_TEST(first_clear, .tags = "collection")
{
    Mel_BitSet bs;
    mel_bitset_init(&bs, 128, mel_alloc_heap());

    MEL_ASSERT_EQ(mel_bitset_first_clear(&bs), (usize)0);

    mel_bitset_set_all(&bs);
    MEL_ASSERT_EQ(mel_bitset_first_clear(&bs), (usize)128);

    mel_bitset_clear_bit(&bs, 70);
    MEL_ASSERT_EQ(mel_bitset_first_clear(&bs), (usize)70);

    mel_bitset_clear_bit(&bs, 3);
    MEL_ASSERT_EQ(mel_bitset_first_clear(&bs), (usize)3);

    mel_bitset_free(&bs);
}

MEL_TEST(resize_grow, .tags = "collection")
{
    Mel_BitSet bs;
    mel_bitset_init(&bs, 64, mel_alloc_heap());
    mel_bitset_set(&bs, 0);
    mel_bitset_set(&bs, 63);

    mel_bitset_resize(&bs, 256);
    MEL_ASSERT_EQ(bs.bit_count, (usize)256);
    MEL_ASSERT_EQ(bs.word_count, (usize)4);
    MEL_ASSERT(mel_bitset_get(&bs, 0));
    MEL_ASSERT(mel_bitset_get(&bs, 63));
    MEL_ASSERT(!mel_bitset_get(&bs, 64));
    MEL_ASSERT(!mel_bitset_get(&bs, 255));

    mel_bitset_free(&bs);
}

MEL_TEST(resize_shrink, .tags = "collection")
{
    Mel_BitSet bs;
    mel_bitset_init(&bs, 256, mel_alloc_heap());
    mel_bitset_set(&bs, 0);
    mel_bitset_set(&bs, 63);
    mel_bitset_set(&bs, 200);

    mel_bitset_resize(&bs, 100);
    MEL_ASSERT_EQ(bs.bit_count, (usize)100);
    MEL_ASSERT_EQ(bs.word_count, (usize)2);
    MEL_ASSERT(mel_bitset_get(&bs, 0));
    MEL_ASSERT(mel_bitset_get(&bs, 63));
    MEL_ASSERT_EQ(mel_bitset_count_set(&bs), (usize)2);

    mel_bitset_free(&bs);
}

MEL_TEST(edge_bit0_and_last, .tags = "collection")
{
    Mel_BitSet bs;
    mel_bitset_init(&bs, 1, mel_alloc_heap());
    MEL_ASSERT_EQ(bs.word_count, (usize)1);
    mel_bitset_set(&bs, 0);
    MEL_ASSERT(mel_bitset_get(&bs, 0));
    MEL_ASSERT(mel_bitset_all(&bs));
    MEL_ASSERT_EQ(mel_bitset_count_set(&bs), (usize)1);
    mel_bitset_free(&bs);
}

MEL_TEST(edge_word_boundary, .tags = "collection")
{
    Mel_BitSet bs;
    mel_bitset_init(&bs, 65, mel_alloc_heap());

    mel_bitset_set(&bs, 63);
    mel_bitset_set(&bs, 64);
    MEL_ASSERT(mel_bitset_get(&bs, 63));
    MEL_ASSERT(mel_bitset_get(&bs, 64));
    MEL_ASSERT(!mel_bitset_get(&bs, 62));
    MEL_ASSERT_EQ(mel_bitset_count_set(&bs), (usize)2);

    mel_bitset_set_all(&bs);
    MEL_ASSERT_EQ(mel_bitset_count_set(&bs), (usize)65);
    MEL_ASSERT(mel_bitset_all(&bs));

    mel_bitset_free(&bs);
}

MEL_TEST(stress_every_other_1000, .tags = "collection")
{
    Mel_BitSet bs;
    mel_bitset_init(&bs, 1000, mel_alloc_heap());

    for (usize i = 0; i < 1000; i += 2)
        mel_bitset_set(&bs, i);

    MEL_ASSERT_EQ(mel_bitset_count_set(&bs), (usize)500);
    MEL_ASSERT_EQ(mel_bitset_count_clear(&bs), (usize)500);

    for (usize i = 0; i < 1000; i++)
    {
        if (i % 2 == 0)
            MEL_ASSERT(mel_bitset_get(&bs, i));
        else
            MEL_ASSERT(!mel_bitset_get(&bs, i));
    }

    mel_bitset_free(&bs);
}

MEL_TEST(large_10000, .tags = "collection")
{
    Mel_BitSet bs;
    mel_bitset_init(&bs, 10000, mel_alloc_heap());

    MEL_ASSERT(mel_bitset_none(&bs));
    MEL_ASSERT_EQ(mel_bitset_count_set(&bs), (usize)0);

    mel_bitset_set_all(&bs);
    MEL_ASSERT(mel_bitset_all(&bs));
    MEL_ASSERT_EQ(mel_bitset_count_set(&bs), (usize)10000);

    mel_bitset_clear(&bs);
    MEL_ASSERT(mel_bitset_none(&bs));

    mel_bitset_set(&bs, 9999);
    MEL_ASSERT_EQ(mel_bitset_first_set(&bs), (usize)9999);
    MEL_ASSERT_EQ(mel_bitset_first_clear(&bs), (usize)0);

    mel_bitset_free(&bs);
}

MEL_TEST(resize_shrink_clears_tail_bits, .tags = "collection")
{
    Mel_BitSet bs;
    mel_bitset_init(&bs, 128, mel_alloc_heap());
    mel_bitset_set_all(&bs);

    mel_bitset_resize(&bs, 70);
    MEL_ASSERT_EQ(mel_bitset_count_set(&bs), (usize)70);
    MEL_ASSERT(mel_bitset_all(&bs));

    mel_bitset_resize(&bs, 128);
    MEL_ASSERT_EQ(mel_bitset_count_set(&bs), (usize)70);
    for (usize i = 70; i < 128; i++)
        MEL_ASSERT(!mel_bitset_get(&bs, i));

    mel_bitset_free(&bs);
}

MEL_TEST(not_preserves_tail, .tags = "collection")
{
    Mel_BitSet src, dst;
    mel_bitset_init(&src, 65, mel_alloc_heap());
    mel_bitset_init(&dst, 65, mel_alloc_heap());

    mel_bitset_not(&dst, &src);
    MEL_ASSERT_EQ(mel_bitset_count_set(&dst), (usize)65);
    MEL_ASSERT(mel_bitset_all(&dst));

    mel_bitset_free(&src);
    mel_bitset_free(&dst);
}
