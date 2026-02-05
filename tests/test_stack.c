#include "../src/test.h"
#include "../src/allocator.stack.h"

MEL_TEST(stack_init)
{
    _Alignas(16) u8 buffer[1024];
    Mel_Stack_Alloc stack;
    mel_stack_init(&stack, buffer, sizeof(buffer));

    MEL_ASSERT_EQ(stack.offset, (usize)0);
    MEL_ASSERT_EQ(stack.size, sizeof(buffer));
    MEL_ASSERT_EQ(stack.last_header, (usize)-1);
    MEL_PASS();
}

MEL_TEST(stack_push_pop_single)
{
    _Alignas(16) u8 buffer[1024];
    Mel_Stack_Alloc stack;
    mel_stack_init(&stack, buffer, sizeof(buffer));

    i32* val = mel_stack_push_struct(&stack, i32);
    MEL_ASSERT_NOT_NULL(val);
    *val = 42;
    MEL_ASSERT_EQ(*val, 42);
    MEL_ASSERT_GT(stack.offset, (usize)0);

    mel_stack_pop(&stack);
    MEL_ASSERT_EQ(stack.offset, (usize)0);
    MEL_ASSERT_EQ(stack.last_header, (usize)-1);
    MEL_PASS();
}

MEL_TEST(stack_lifo_order)
{
    _Alignas(16) u8 buffer[2048];
    Mel_Stack_Alloc stack;
    mel_stack_init(&stack, buffer, sizeof(buffer));

    i32* a = mel_stack_push_struct(&stack, i32);
    *a = 10;
    i32* b = mel_stack_push_struct(&stack, i32);
    *b = 20;
    i32* c = mel_stack_push_struct(&stack, i32);
    *c = 30;

    MEL_ASSERT_EQ(*a, 10);
    MEL_ASSERT_EQ(*b, 20);
    MEL_ASSERT_EQ(*c, 30);

    mel_stack_pop(&stack);
    mel_stack_pop(&stack);
    MEL_ASSERT_EQ(*a, 10);

    mel_stack_pop(&stack);
    MEL_ASSERT_EQ(stack.offset, (usize)0);
    MEL_PASS();
}

MEL_TEST(stack_push_array)
{
    _Alignas(16) u8 buffer[4096];
    Mel_Stack_Alloc stack;
    mel_stack_init(&stack, buffer, sizeof(buffer));

    i32* arr = mel_stack_push_array(&stack, i32, 50);
    MEL_ASSERT_NOT_NULL(arr);
    for (i32 i = 0; i < 50; i++) arr[i] = i * 7;
    for (i32 i = 0; i < 50; i++) MEL_ASSERT_EQ(arr[i], i * 7);

    mel_stack_pop(&stack);
    MEL_ASSERT_EQ(stack.offset, (usize)0);
    MEL_PASS();
}

MEL_TEST(stack_alignment)
{
    _Alignas(64) u8 buffer[4096];
    Mel_Stack_Alloc stack;
    mel_stack_init(&stack, buffer, sizeof(buffer));

    u8* one_byte = mel_stack_push_struct(&stack, u8);
    MEL_ASSERT_NOT_NULL(one_byte);

    i64* aligned = mel_stack_push_struct(&stack, i64);
    MEL_ASSERT_NOT_NULL(aligned);
    MEL_ASSERT_EQ((usize)aligned % _Alignof(i64), (usize)0);
    *aligned = 12345;
    MEL_ASSERT_EQ(*aligned, (i64)12345);

    mel_stack_pop(&stack);
    mel_stack_pop(&stack);
    MEL_PASS();
}

MEL_TEST(stack_mark_restore)
{
    _Alignas(16) u8 buffer[2048];
    Mel_Stack_Alloc stack;
    mel_stack_init(&stack, buffer, sizeof(buffer));

    i32* a = mel_stack_push_struct(&stack, i32);
    *a = 100;

    Mel_Stack_Mark mark = mel_stack_mark(&stack);

    i32* b = mel_stack_push_struct(&stack, i32);
    *b = 200;
    i32* c = mel_stack_push_struct(&stack, i32);
    *c = 300;

    mel_stack_restore(&stack, mark);

    MEL_ASSERT_EQ(*a, 100);
    MEL_ASSERT_EQ(stack.offset, mark.offset);

    i32* d = mel_stack_push_struct(&stack, i32);
    *d = 999;
    MEL_ASSERT_EQ(*d, 999);

    mel_stack_pop(&stack);
    mel_stack_pop(&stack);
    MEL_ASSERT_EQ(stack.offset, (usize)0);
    MEL_PASS();
}

MEL_TEST(stack_reset)
{
    _Alignas(16) u8 buffer[1024];
    Mel_Stack_Alloc stack;
    mel_stack_init(&stack, buffer, sizeof(buffer));

    mel_stack_push_struct(&stack, i32);
    mel_stack_push_struct(&stack, i32);
    mel_stack_push_struct(&stack, i32);

    mel_stack_reset(&stack);
    MEL_ASSERT_EQ(stack.offset, (usize)0);
    MEL_ASSERT_EQ(stack.last_header, (usize)-1);

    i32* fresh = mel_stack_push_struct(&stack, i32);
    MEL_ASSERT_NOT_NULL(fresh);
    *fresh = 77;
    MEL_ASSERT_EQ(*fresh, 77);
    mel_stack_pop(&stack);
    MEL_PASS();
}

MEL_TEST(stack_interleaved_push_pop)
{
    _Alignas(16) u8 buffer[4096];
    Mel_Stack_Alloc stack;
    mel_stack_init(&stack, buffer, sizeof(buffer));

    i32* a = mel_stack_push_struct(&stack, i32);
    *a = 1;
    i32* b = mel_stack_push_struct(&stack, i32);
    *b = 2;

    mel_stack_pop(&stack);

    i32* c = mel_stack_push_struct(&stack, i32);
    *c = 3;

    MEL_ASSERT_EQ(*a, 1);
    MEL_ASSERT_EQ(*c, 3);

    mel_stack_pop(&stack);
    mel_stack_pop(&stack);
    MEL_ASSERT_EQ(stack.offset, (usize)0);
    MEL_PASS();
}

MEL_TEST(stack_nested_marks)
{
    _Alignas(16) u8 buffer[4096];
    Mel_Stack_Alloc stack;
    mel_stack_init(&stack, buffer, sizeof(buffer));

    mel_stack_push_struct(&stack, i32);
    Mel_Stack_Mark outer = mel_stack_mark(&stack);

    mel_stack_push_struct(&stack, i32);
    Mel_Stack_Mark inner = mel_stack_mark(&stack);

    mel_stack_push_struct(&stack, i32);
    mel_stack_push_struct(&stack, i32);

    mel_stack_restore(&stack, inner);
    MEL_ASSERT_EQ(stack.offset, inner.offset);

    mel_stack_push_struct(&stack, i32);

    mel_stack_restore(&stack, outer);
    MEL_ASSERT_EQ(stack.offset, outer.offset);

    mel_stack_pop(&stack);
    MEL_ASSERT_EQ(stack.offset, (usize)0);
    MEL_PASS();
}

int main(void)
{
    MEL_TEST_BEGIN("Stack Allocator Tests");

    MEL_RUN_TEST(stack_init);
    MEL_RUN_TEST(stack_push_pop_single);
    MEL_RUN_TEST(stack_lifo_order);
    MEL_RUN_TEST(stack_push_array);
    MEL_RUN_TEST(stack_alignment);
    MEL_RUN_TEST(stack_mark_restore);
    MEL_RUN_TEST(stack_reset);
    MEL_RUN_TEST(stack_interleaved_push_pop);
    MEL_RUN_TEST(stack_nested_marks);

    MEL_TEST_END();
    return MEL_TEST_EXIT_CODE();
}
