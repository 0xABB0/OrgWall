#include "../melody/test.harness.h"
#include "../melody/collection.list.h"

typedef struct {
    i32 value;
    Mel_ListNode node;
} TestItem;

static TestItem make_item(i32 v)
{
    TestItem item = {0};
    item.value = v;
    mel_list_init(&item.node);
    return item;
}

MEL_TEST(init_empty, .tags = "collection")
{
    Mel_ListNode head;
    mel_list_init(&head);

    MEL_ASSERT(mel_list_empty(&head));
    MEL_ASSERT(head.next == &head);
    MEL_ASSERT(head.prev == &head);
}

MEL_TEST(push_front_order, .tags = "collection")
{
    Mel_ListNode head;
    mel_list_init(&head);

    TestItem a = make_item(1);
    TestItem b = make_item(2);
    TestItem c = make_item(3);

    mel_list_push_front(&head, &a.node);
    mel_list_push_front(&head, &b.node);
    mel_list_push_front(&head, &c.node);

    Mel_ListNode* pos = head.next;
    MEL_ASSERT_EQ(mel_list_entry(pos, TestItem, node)->value, 3);
    pos = pos->next;
    MEL_ASSERT_EQ(mel_list_entry(pos, TestItem, node)->value, 2);
    pos = pos->next;
    MEL_ASSERT_EQ(mel_list_entry(pos, TestItem, node)->value, 1);
    pos = pos->next;
    MEL_ASSERT(pos == &head);
}

MEL_TEST(push_back_order, .tags = "collection")
{
    Mel_ListNode head;
    mel_list_init(&head);

    TestItem a = make_item(1);
    TestItem b = make_item(2);
    TestItem c = make_item(3);

    mel_list_push_back(&head, &a.node);
    mel_list_push_back(&head, &b.node);
    mel_list_push_back(&head, &c.node);

    Mel_ListNode* pos = head.next;
    MEL_ASSERT_EQ(mel_list_entry(pos, TestItem, node)->value, 1);
    pos = pos->next;
    MEL_ASSERT_EQ(mel_list_entry(pos, TestItem, node)->value, 2);
    pos = pos->next;
    MEL_ASSERT_EQ(mel_list_entry(pos, TestItem, node)->value, 3);
    pos = pos->next;
    MEL_ASSERT(pos == &head);
}

MEL_TEST(remove_middle, .tags = "collection")
{
    Mel_ListNode head;
    mel_list_init(&head);

    TestItem a = make_item(1);
    TestItem b = make_item(2);
    TestItem c = make_item(3);

    mel_list_push_back(&head, &a.node);
    mel_list_push_back(&head, &b.node);
    mel_list_push_back(&head, &c.node);

    mel_list_remove(&b.node);

    MEL_ASSERT_EQ(mel_list_count(&head), (usize)2);

    Mel_ListNode* pos = head.next;
    MEL_ASSERT_EQ(mel_list_entry(pos, TestItem, node)->value, 1);
    pos = pos->next;
    MEL_ASSERT_EQ(mel_list_entry(pos, TestItem, node)->value, 3);
    pos = pos->next;
    MEL_ASSERT(pos == &head);

    MEL_ASSERT(b.node.next == &b.node);
    MEL_ASSERT(b.node.prev == &b.node);
}

MEL_TEST(foreach_sum, .tags = "collection")
{
    Mel_ListNode head;
    mel_list_init(&head);

    TestItem items[5];
    for (i32 i = 0; i < 5; i++)
    {
        items[i] = make_item(i + 1);
        mel_list_push_back(&head, &items[i].node);
    }

    i32 sum = 0;
    mel_list_foreach(pos, &head)
    {
        sum += mel_list_entry(pos, TestItem, node)->value;
    }
    MEL_ASSERT_EQ(sum, 15);
}

MEL_TEST(foreach_safe_remove_all, .tags = "collection")
{
    Mel_ListNode head;
    mel_list_init(&head);

    TestItem items[5];
    for (i32 i = 0; i < 5; i++)
    {
        items[i] = make_item(i + 1);
        mel_list_push_back(&head, &items[i].node);
    }

    MEL_ASSERT_EQ(mel_list_count(&head), (usize)5);

    mel_list_foreach_safe(pos, tmp, &head)
    {
        mel_list_remove(pos);
    }

    MEL_ASSERT(mel_list_empty(&head));
    MEL_ASSERT_EQ(mel_list_count(&head), (usize)0);
}

MEL_TEST(splice, .tags = "collection")
{
    Mel_ListNode dst;
    mel_list_init(&dst);
    Mel_ListNode src;
    mel_list_init(&src);

    TestItem a = make_item(1);
    TestItem b = make_item(2);
    mel_list_push_back(&dst, &a.node);
    mel_list_push_back(&dst, &b.node);

    TestItem c = make_item(3);
    TestItem d = make_item(4);
    mel_list_push_back(&src, &c.node);
    mel_list_push_back(&src, &d.node);

    mel_list_splice(&dst, &src);

    MEL_ASSERT(mel_list_empty(&src));
    MEL_ASSERT_EQ(mel_list_count(&dst), (usize)4);

    Mel_ListNode* pos = dst.next;
    MEL_ASSERT_EQ(mel_list_entry(pos, TestItem, node)->value, 3);
    pos = pos->next;
    MEL_ASSERT_EQ(mel_list_entry(pos, TestItem, node)->value, 4);
    pos = pos->next;
    MEL_ASSERT_EQ(mel_list_entry(pos, TestItem, node)->value, 1);
    pos = pos->next;
    MEL_ASSERT_EQ(mel_list_entry(pos, TestItem, node)->value, 2);
    pos = pos->next;
    MEL_ASSERT(pos == &dst);
}

MEL_TEST(container_of_entry, .tags = "collection")
{
    TestItem item = make_item(42);
    Mel_ListNode* node_ptr = &item.node;

    TestItem* recovered = mel_container_of(node_ptr, TestItem, node);
    MEL_ASSERT(recovered == &item);
    MEL_ASSERT_EQ(recovered->value, 42);

    TestItem* via_entry = mel_list_entry(node_ptr, TestItem, node);
    MEL_ASSERT(via_entry == &item);
    MEL_ASSERT_EQ(via_entry->value, 42);
}

MEL_TEST(count, .tags = "collection")
{
    Mel_ListNode head;
    mel_list_init(&head);

    MEL_ASSERT_EQ(mel_list_count(&head), (usize)0);

    TestItem a = make_item(1);
    mel_list_push_back(&head, &a.node);
    MEL_ASSERT_EQ(mel_list_count(&head), (usize)1);

    TestItem b = make_item(2);
    mel_list_push_back(&head, &b.node);
    MEL_ASSERT_EQ(mel_list_count(&head), (usize)2);

    mel_list_remove(&a.node);
    MEL_ASSERT_EQ(mel_list_count(&head), (usize)1);

    mel_list_remove(&b.node);
    MEL_ASSERT_EQ(mel_list_count(&head), (usize)0);
}

MEL_TEST(front_back, .tags = "collection")
{
    Mel_ListNode head;
    mel_list_init(&head);

    TestItem a = make_item(10);
    TestItem b = make_item(20);
    TestItem c = make_item(30);

    mel_list_push_back(&head, &a.node);
    mel_list_push_back(&head, &b.node);
    mel_list_push_back(&head, &c.node);

    MEL_ASSERT(mel_list_front(&head) == &a.node);
    MEL_ASSERT(mel_list_back(&head) == &c.node);
    MEL_ASSERT_EQ(mel_list_entry(mel_list_front(&head), TestItem, node)->value, 10);
    MEL_ASSERT_EQ(mel_list_entry(mel_list_back(&head), TestItem, node)->value, 30);
}

MEL_TEST(mixed_push_order, .tags = "collection")
{
    Mel_ListNode head;
    mel_list_init(&head);

    TestItem a = make_item(1);
    TestItem b = make_item(2);
    TestItem c = make_item(3);
    TestItem d = make_item(4);

    mel_list_push_back(&head, &a.node);
    mel_list_push_front(&head, &b.node);
    mel_list_push_back(&head, &c.node);
    mel_list_push_front(&head, &d.node);

    Mel_ListNode* pos = head.next;
    MEL_ASSERT_EQ(mel_list_entry(pos, TestItem, node)->value, 4);
    pos = pos->next;
    MEL_ASSERT_EQ(mel_list_entry(pos, TestItem, node)->value, 2);
    pos = pos->next;
    MEL_ASSERT_EQ(mel_list_entry(pos, TestItem, node)->value, 1);
    pos = pos->next;
    MEL_ASSERT_EQ(mel_list_entry(pos, TestItem, node)->value, 3);
    pos = pos->next;
    MEL_ASSERT(pos == &head);
}

MEL_TEST(remove_single_element, .tags = "collection")
{
    Mel_ListNode head;
    mel_list_init(&head);

    TestItem a = make_item(99);
    mel_list_push_back(&head, &a.node);
    MEL_ASSERT(!mel_list_empty(&head));

    mel_list_remove(&a.node);
    MEL_ASSERT(mel_list_empty(&head));
    MEL_ASSERT(head.next == &head);
    MEL_ASSERT(head.prev == &head);
}

MEL_TEST(splice_empty_into_nonempty, .tags = "collection")
{
    Mel_ListNode dst;
    mel_list_init(&dst);
    Mel_ListNode src;
    mel_list_init(&src);

    TestItem a = make_item(1);
    TestItem b = make_item(2);
    mel_list_push_back(&dst, &a.node);
    mel_list_push_back(&dst, &b.node);

    mel_list_splice(&dst, &src);

    MEL_ASSERT_EQ(mel_list_count(&dst), (usize)2);
    MEL_ASSERT(mel_list_empty(&src));

    Mel_ListNode* pos = dst.next;
    MEL_ASSERT_EQ(mel_list_entry(pos, TestItem, node)->value, 1);
    pos = pos->next;
    MEL_ASSERT_EQ(mel_list_entry(pos, TestItem, node)->value, 2);
}

MEL_TEST(splice_into_empty, .tags = "collection")
{
    Mel_ListNode dst;
    mel_list_init(&dst);
    Mel_ListNode src;
    mel_list_init(&src);

    TestItem a = make_item(5);
    TestItem b = make_item(6);
    mel_list_push_back(&src, &a.node);
    mel_list_push_back(&src, &b.node);

    mel_list_splice(&dst, &src);

    MEL_ASSERT(mel_list_empty(&src));
    MEL_ASSERT_EQ(mel_list_count(&dst), (usize)2);

    Mel_ListNode* pos = dst.next;
    MEL_ASSERT_EQ(mel_list_entry(pos, TestItem, node)->value, 5);
    pos = pos->next;
    MEL_ASSERT_EQ(mel_list_entry(pos, TestItem, node)->value, 6);
}
