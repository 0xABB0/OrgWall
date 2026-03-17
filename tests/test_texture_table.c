#include "../melody/test.harness.h"
#include "../melody/collection.bitset.h"
#include "../melody/allocator.heap.h"

MEL_TEST(texture_table_slot_alloc_sequential, .tags = "gpu, render")
{
    Mel_BitSet used;
    mel_bitset_init(&used, 64, mel_alloc_heap());

    usize s0 = mel_bitset_first_clear(&used);
    MEL_ASSERT_EQ(s0, (usize)0);
    mel_bitset_set(&used, s0);

    usize s1 = mel_bitset_first_clear(&used);
    MEL_ASSERT_EQ(s1, (usize)1);
    mel_bitset_set(&used, s1);

    usize s2 = mel_bitset_first_clear(&used);
    MEL_ASSERT_EQ(s2, (usize)2);
    mel_bitset_set(&used, s2);

    MEL_ASSERT_EQ(mel_bitset_count_set(&used), (usize)3);

    mel_bitset_free(&used);
}

MEL_TEST(texture_table_slot_reuse_after_remove, .tags = "gpu, render")
{
    Mel_BitSet used;
    mel_bitset_init(&used, 64, mel_alloc_heap());

    mel_bitset_set(&used, 0);
    mel_bitset_set(&used, 1);
    mel_bitset_set(&used, 2);
    MEL_ASSERT_EQ(mel_bitset_count_set(&used), (usize)3);

    mel_bitset_clear_bit(&used, 1);
    MEL_ASSERT_EQ(mel_bitset_count_set(&used), (usize)2);

    usize reused = mel_bitset_first_clear(&used);
    MEL_ASSERT_EQ(reused, (usize)1);
    mel_bitset_set(&used, reused);

    MEL_ASSERT_EQ(mel_bitset_count_set(&used), (usize)3);
    MEL_ASSERT(mel_bitset_get(&used, 0));
    MEL_ASSERT(mel_bitset_get(&used, 1));
    MEL_ASSERT(mel_bitset_get(&used, 2));

    mel_bitset_free(&used);
}

MEL_TEST(texture_table_fill_capacity, .tags = "gpu, render")
{
    u32 capacity = 32;
    Mel_BitSet used;
    mel_bitset_init(&used, capacity, mel_alloc_heap());

    for (u32 i = 0; i < capacity; i++)
    {
        usize slot = mel_bitset_first_clear(&used);
        MEL_ASSERT_EQ(slot, (usize)i);
        mel_bitset_set(&used, slot);
    }

    MEL_ASSERT_EQ(mel_bitset_count_set(&used), (usize)capacity);
    MEL_ASSERT(mel_bitset_all(&used));

    mel_bitset_free(&used);
}

MEL_TEST(texture_table_remove_and_count, .tags = "gpu, render")
{
    Mel_BitSet used;
    mel_bitset_init(&used, 64, mel_alloc_heap());

    for (u32 i = 0; i < 10; i++)
        mel_bitset_set(&used, i);

    MEL_ASSERT_EQ(mel_bitset_count_set(&used), (usize)10);

    mel_bitset_clear_bit(&used, 3);
    mel_bitset_clear_bit(&used, 7);
    MEL_ASSERT_EQ(mel_bitset_count_set(&used), (usize)8);

    MEL_ASSERT(!mel_bitset_get(&used, 3));
    MEL_ASSERT(!mel_bitset_get(&used, 7));
    MEL_ASSERT(mel_bitset_get(&used, 0));
    MEL_ASSERT(mel_bitset_get(&used, 9));

    mel_bitset_free(&used);
}

MEL_TEST(texture_table_reuse_lowest_slot, .tags = "gpu, render")
{
    Mel_BitSet used;
    mel_bitset_init(&used, 64, mel_alloc_heap());

    for (u32 i = 0; i < 5; i++)
        mel_bitset_set(&used, i);

    mel_bitset_clear_bit(&used, 1);
    mel_bitset_clear_bit(&used, 3);

    usize first_free = mel_bitset_first_clear(&used);
    MEL_ASSERT_EQ(first_free, (usize)1);

    mel_bitset_set(&used, first_free);
    usize second_free = mel_bitset_first_clear(&used);
    MEL_ASSERT_EQ(second_free, (usize)3);

    mel_bitset_free(&used);
}

MEL_TEST(texture_table_double_remove_slot_stays_clear, .tags = "gpu, render")
{
    Mel_BitSet used;
    mel_bitset_init(&used, 64, mel_alloc_heap());

    mel_bitset_set(&used, 0);
    mel_bitset_set(&used, 1);
    mel_bitset_set(&used, 2);

    mel_bitset_clear_bit(&used, 1);
    MEL_ASSERT(!mel_bitset_get(&used, 1));
    MEL_ASSERT_EQ(mel_bitset_count_set(&used), (usize)2);

    mel_bitset_clear_bit(&used, 1);
    MEL_ASSERT(!mel_bitset_get(&used, 1));
    MEL_ASSERT_EQ(mel_bitset_count_set(&used), (usize)2);

    mel_bitset_free(&used);
}

MEL_TEST(texture_table_all_slots_used_first_clear_returns_cap, .tags = "gpu, render")
{
    u32 capacity = 8;
    Mel_BitSet used;
    mel_bitset_init(&used, capacity, mel_alloc_heap());

    for (u32 i = 0; i < capacity; i++)
        mel_bitset_set(&used, i);

    MEL_ASSERT(mel_bitset_all(&used));

    usize slot = mel_bitset_first_clear(&used);
    MEL_ASSERT_GE(slot, (usize)capacity);

    mel_bitset_free(&used);
}

MEL_TEST(texture_table_interleaved_add_remove, .tags = "gpu, render")
{
    Mel_BitSet used;
    mel_bitset_init(&used, 16, mel_alloc_heap());

    for (u32 i = 0; i < 8; i++)
        mel_bitset_set(&used, i);

    for (u32 i = 0; i < 8; i += 2)
        mel_bitset_clear_bit(&used, i);

    MEL_ASSERT_EQ(mel_bitset_count_set(&used), (usize)4);

    usize first = mel_bitset_first_clear(&used);
    MEL_ASSERT_EQ(first, (usize)0);
    mel_bitset_set(&used, first);

    usize second = mel_bitset_first_clear(&used);
    MEL_ASSERT_EQ(second, (usize)2);

    mel_bitset_free(&used);
}

MEL_TEST(texture_table_single_capacity, .tags = "gpu, render")
{
    Mel_BitSet used;
    mel_bitset_init(&used, 1, mel_alloc_heap());

    usize slot = mel_bitset_first_clear(&used);
    MEL_ASSERT_EQ(slot, (usize)0);
    mel_bitset_set(&used, slot);

    MEL_ASSERT(mel_bitset_all(&used));
    MEL_ASSERT_EQ(mel_bitset_count_set(&used), (usize)1);

    mel_bitset_clear_bit(&used, 0);
    MEL_ASSERT(mel_bitset_none(&used));

    mel_bitset_free(&used);
}
