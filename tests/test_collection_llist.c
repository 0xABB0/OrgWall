#include "../melody/test.harness.h"
#include "../melody/allocator.h"
#include "../melody/allocator.heap.h"
#include "../melody/collection.llist.h"

MEL_TEST(init_empty, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_LList(i32) list;
    mel_llist_init(&list, alloc);

    MEL_ASSERT_EQ(mel_llist_count(&list), (usize)0);
    MEL_ASSERT(mel_llist_empty(&list));
    MEL_ASSERT_NULL(list.head);
    MEL_ASSERT_NULL(list.tail);

}

MEL_TEST(push_front, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_LList(i32) list;
    mel_llist_init(&list, alloc);

    mel_llist_push_front(&list, 10);
    MEL_ASSERT_EQ(mel_llist_front(&list), 10);
    MEL_ASSERT_EQ(mel_llist_count(&list), (usize)1);

    mel_llist_push_front(&list, 20);
    MEL_ASSERT_EQ(mel_llist_front(&list), 20);
    MEL_ASSERT_EQ(mel_llist_back(&list), 10);
    MEL_ASSERT_EQ(mel_llist_count(&list), (usize)2);

    mel_llist_push_front(&list, 30);
    MEL_ASSERT_EQ(mel_llist_front(&list), 30);
    MEL_ASSERT_EQ(mel_llist_back(&list), 10);
    MEL_ASSERT_EQ(mel_llist_count(&list), (usize)3);

    mel_llist_free(&list);
}

MEL_TEST(push_back, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_LList(i32) list;
    mel_llist_init(&list, alloc);

    mel_llist_push_back(&list, 10);
    MEL_ASSERT_EQ(mel_llist_back(&list), 10);
    MEL_ASSERT_EQ(mel_llist_count(&list), (usize)1);

    mel_llist_push_back(&list, 20);
    MEL_ASSERT_EQ(mel_llist_back(&list), 20);
    MEL_ASSERT_EQ(mel_llist_front(&list), 10);
    MEL_ASSERT_EQ(mel_llist_count(&list), (usize)2);

    mel_llist_push_back(&list, 30);
    MEL_ASSERT_EQ(mel_llist_back(&list), 30);
    MEL_ASSERT_EQ(mel_llist_front(&list), 10);
    MEL_ASSERT_EQ(mel_llist_count(&list), (usize)3);

    mel_llist_free(&list);
}

MEL_TEST(pop_front_fifo, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_LList(i32) list;
    mel_llist_init(&list, alloc);

    mel_llist_push_back(&list, 1);
    mel_llist_push_back(&list, 2);
    mel_llist_push_back(&list, 3);

    MEL_ASSERT_EQ(mel_llist_pop_front(&list), 1);
    MEL_ASSERT_EQ(mel_llist_pop_front(&list), 2);
    MEL_ASSERT_EQ(mel_llist_pop_front(&list), 3);
    MEL_ASSERT(mel_llist_empty(&list));

}

MEL_TEST(pop_back_lifo, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_LList(i32) list;
    mel_llist_init(&list, alloc);

    mel_llist_push_back(&list, 1);
    mel_llist_push_back(&list, 2);
    mel_llist_push_back(&list, 3);

    MEL_ASSERT_EQ(mel_llist_pop_back(&list), 3);
    MEL_ASSERT_EQ(mel_llist_pop_back(&list), 2);
    MEL_ASSERT_EQ(mel_llist_pop_back(&list), 1);
    MEL_ASSERT(mel_llist_empty(&list));

}

MEL_TEST(front_back_peek, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_LList(i32) list;
    mel_llist_init(&list, alloc);

    mel_llist_push_back(&list, 42);
    MEL_ASSERT_EQ(mel_llist_front(&list), 42);
    MEL_ASSERT_EQ(mel_llist_back(&list), 42);

    mel_llist_push_back(&list, 99);
    MEL_ASSERT_EQ(mel_llist_front(&list), 42);
    MEL_ASSERT_EQ(mel_llist_back(&list), 99);

    mel_llist_push_front(&list, 7);
    MEL_ASSERT_EQ(mel_llist_front(&list), 7);
    MEL_ASSERT_EQ(mel_llist_back(&list), 99);

    mel_llist_free(&list);
}

MEL_TEST(mixed_push_order, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_LList(i32) list;
    mel_llist_init(&list, alloc);

    mel_llist_push_back(&list, 2);
    mel_llist_push_front(&list, 1);
    mel_llist_push_back(&list, 3);
    mel_llist_push_front(&list, 0);

    MEL_ASSERT_EQ(mel_llist_pop_front(&list), 0);
    MEL_ASSERT_EQ(mel_llist_pop_front(&list), 1);
    MEL_ASSERT_EQ(mel_llist_pop_front(&list), 2);
    MEL_ASSERT_EQ(mel_llist_pop_front(&list), 3);
    MEL_ASSERT(mel_llist_empty(&list));

}

MEL_TEST(clear, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_LList(i32) list;
    mel_llist_init(&list, alloc);

    mel_llist_push_back(&list, 1);
    mel_llist_push_back(&list, 2);
    mel_llist_push_back(&list, 3);
    MEL_ASSERT_EQ(mel_llist_count(&list), (usize)3);

    mel_llist_clear(&list);
    MEL_ASSERT_EQ(mel_llist_count(&list), (usize)0);
    MEL_ASSERT(mel_llist_empty(&list));
    MEL_ASSERT_NULL(list.head);
    MEL_ASSERT_NULL(list.tail);

}

MEL_TEST(count_empty, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_LList(i32) list;
    mel_llist_init(&list, alloc);

    MEL_ASSERT(mel_llist_empty(&list));
    MEL_ASSERT_EQ(mel_llist_count(&list), (usize)0);

    mel_llist_push_back(&list, 1);
    MEL_ASSERT(!mel_llist_empty(&list));
    MEL_ASSERT_EQ(mel_llist_count(&list), (usize)1);

    mel_llist_push_back(&list, 2);
    MEL_ASSERT_EQ(mel_llist_count(&list), (usize)2);

    mel_llist_pop_front(&list);
    MEL_ASSERT_EQ(mel_llist_count(&list), (usize)1);

    mel_llist_pop_front(&list);
    MEL_ASSERT(mel_llist_empty(&list));

}

MEL_TEST(foreach_sum, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_LList(i32) list;
    mel_llist_init(&list, alloc);

    mel_llist_push_back(&list, 10);
    mel_llist_push_back(&list, 20);
    mel_llist_push_back(&list, 30);
    mel_llist_push_back(&list, 40);

    i32 sum = 0;
    mel_llist_foreach(&list, it, {
        sum += it->value;
    });
    MEL_ASSERT_EQ(sum, 100);

    mel_llist_free(&list);
}

MEL_TEST(remove_node_middle, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_LList(i32) list;
    mel_llist_init(&list, alloc);

    mel_llist_push_back(&list, 1);
    mel_llist_push_back(&list, 2);
    mel_llist_push_back(&list, 3);

    typeof(list.head) middle = list.head->next;
    MEL_ASSERT_EQ(middle->value, 2);

    mel_llist_remove_node(&list, middle);

    MEL_ASSERT_EQ(mel_llist_count(&list), (usize)2);
    MEL_ASSERT_EQ(mel_llist_front(&list), 1);
    MEL_ASSERT_EQ(mel_llist_back(&list), 3);

    MEL_ASSERT_EQ(mel_llist_pop_front(&list), 1);
    MEL_ASSERT_EQ(mel_llist_pop_front(&list), 3);
    MEL_ASSERT(mel_llist_empty(&list));

}

MEL_TEST(remove_node_head, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_LList(i32) list;
    mel_llist_init(&list, alloc);

    mel_llist_push_back(&list, 1);
    mel_llist_push_back(&list, 2);
    mel_llist_push_back(&list, 3);

    mel_llist_remove_node(&list, list.head);

    MEL_ASSERT_EQ(mel_llist_count(&list), (usize)2);
    MEL_ASSERT_EQ(mel_llist_front(&list), 2);
    MEL_ASSERT_EQ(mel_llist_back(&list), 3);

    mel_llist_free(&list);
}

MEL_TEST(remove_node_tail, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_LList(i32) list;
    mel_llist_init(&list, alloc);

    mel_llist_push_back(&list, 1);
    mel_llist_push_back(&list, 2);
    mel_llist_push_back(&list, 3);

    mel_llist_remove_node(&list, list.tail);

    MEL_ASSERT_EQ(mel_llist_count(&list), (usize)2);
    MEL_ASSERT_EQ(mel_llist_front(&list), 1);
    MEL_ASSERT_EQ(mel_llist_back(&list), 2);

    mel_llist_free(&list);
}

MEL_TEST(single_element, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_LList(i32) list;
    mel_llist_init(&list, alloc);

    mel_llist_push_back(&list, 42);
    MEL_ASSERT_EQ(mel_llist_count(&list), (usize)1);
    MEL_ASSERT_EQ(mel_llist_front(&list), 42);
    MEL_ASSERT_EQ(mel_llist_back(&list), 42);

    i32 val = mel_llist_pop_front(&list);
    MEL_ASSERT_EQ(val, 42);
    MEL_ASSERT(mel_llist_empty(&list));
    MEL_ASSERT_NULL(list.head);
    MEL_ASSERT_NULL(list.tail);

}

MEL_TEST(stress_push_pop, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_LList(i32) list;
    mel_llist_init(&list, alloc);

    for (i32 i = 0; i < 100; i++)
    {
        mel_llist_push_back(&list, i);
    }
    MEL_ASSERT_EQ(mel_llist_count(&list), (usize)100);

    for (i32 i = 0; i < 100; i++)
    {
        i32 val = mel_llist_pop_front(&list);
        MEL_ASSERT_EQ(val, i);
    }
    MEL_ASSERT(mel_llist_empty(&list));

}

MEL_TEST(push_front_pop_back_reverse, .tags = "collection")
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_LList(i32) list;
    mel_llist_init(&list, alloc);

    mel_llist_push_front(&list, 1);
    mel_llist_push_front(&list, 2);
    mel_llist_push_front(&list, 3);
    mel_llist_push_front(&list, 4);
    mel_llist_push_front(&list, 5);

    MEL_ASSERT_EQ(mel_llist_pop_back(&list), 1);
    MEL_ASSERT_EQ(mel_llist_pop_back(&list), 2);
    MEL_ASSERT_EQ(mel_llist_pop_back(&list), 3);
    MEL_ASSERT_EQ(mel_llist_pop_back(&list), 4);
    MEL_ASSERT_EQ(mel_llist_pop_back(&list), 5);
    MEL_ASSERT(mel_llist_empty(&list));

}
