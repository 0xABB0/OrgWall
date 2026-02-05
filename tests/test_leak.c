#include "../src/test.h"
#include "../src/allocator.h"
#include "../src/allocator.leak.h"

static i32 s_leak_count;
static usize s_leak_total_size;

static void leak_counter(const char* file, const char* func, u32 line, usize size, void* user_data)
{
    MEL_UNUSED(file);
    MEL_UNUSED(func);
    MEL_UNUSED(line);
    MEL_UNUSED(user_data);
    s_leak_count++;
    s_leak_total_size += size;
}

MEL_TEST(leak_singleton)
{
    const Mel_Alloc* a = mel_alloc_leak_detect();
    const Mel_Alloc* b = mel_alloc_leak_detect();
    MEL_ASSERT_NOT_NULL(a);
    MEL_ASSERT(a == b);
    MEL_PASS();
}

MEL_TEST(leak_alloc_dealloc_no_leak)
{
    const Mel_Alloc* alloc = mel_alloc_leak_detect();
    i32* ptr = mel_alloc_type(alloc, i32);
    MEL_ASSERT_NOT_NULL(ptr);
    *ptr = 99;
    MEL_ASSERT_EQ(*ptr, 99);
    mel_dealloc(alloc, ptr);

    s_leak_count = 0;
    s_leak_total_size = 0;
    mel_leak_dump(leak_counter, NULL);
    MEL_ASSERT_EQ(s_leak_count, 0);
    MEL_PASS();
}

MEL_TEST(leak_detect_single_leak)
{
    const Mel_Alloc* alloc = mel_alloc_leak_detect();
    i32* leaked = mel_alloc_type(alloc, i32);
    *leaked = 0xDEAD;

    s_leak_count = 0;
    s_leak_total_size = 0;
    mel_leak_dump(leak_counter, NULL);
    MEL_ASSERT_GE(s_leak_count, 1);
    MEL_ASSERT_GE(s_leak_total_size, sizeof(i32));

    mel_dealloc(alloc, leaked);
    MEL_PASS();
}

MEL_TEST(leak_detect_multiple_leaks)
{
    const Mel_Alloc* alloc = mel_alloc_leak_detect();

    i32* a = mel_alloc_type(alloc, i32);
    i32* b = mel_alloc_type(alloc, i32);
    i32* c = mel_alloc_type(alloc, i32);

    s_leak_count = 0;
    mel_leak_dump(leak_counter, NULL);
    i32 with_three = s_leak_count;

    mel_dealloc(alloc, a);

    s_leak_count = 0;
    mel_leak_dump(leak_counter, NULL);
    MEL_ASSERT_EQ(s_leak_count, with_three - 1);

    mel_dealloc(alloc, b);
    mel_dealloc(alloc, c);

    s_leak_count = 0;
    mel_leak_dump(leak_counter, NULL);
    MEL_ASSERT_EQ(s_leak_count, with_three - 3);

    MEL_PASS();
}

MEL_TEST(leak_realloc)
{
    const Mel_Alloc* alloc = mel_alloc_leak_detect();

    i32* ptr = (i32*)mel_alloc(alloc, sizeof(i32) * 2);
    ptr[0] = 111;
    ptr[1] = 222;

    ptr = (i32*)mel_realloc(alloc, ptr, sizeof(i32) * 4);
    MEL_ASSERT_NOT_NULL(ptr);
    MEL_ASSERT_EQ(ptr[0], 111);
    MEL_ASSERT_EQ(ptr[1], 222);
    ptr[2] = 333;
    ptr[3] = 444;

    mel_dealloc(alloc, ptr);
    MEL_PASS();
}

MEL_TEST(leak_calloc_zeroed)
{
    const Mel_Alloc* alloc = mel_alloc_leak_detect();
    u8* ptr = (u8*)mel_calloc(alloc, 64);
    MEL_ASSERT_NOT_NULL(ptr);
    for (i32 i = 0; i < 64; i++) MEL_ASSERT_EQ(ptr[i], 0);
    mel_dealloc(alloc, ptr);
    MEL_PASS();
}

MEL_TEST(leak_user_data_passthrough)
{
    static i32 user_val;
    user_val = 0;
    const Mel_Alloc* alloc = mel_alloc_leak_detect();
    i32* leaked = mel_alloc_type(alloc, i32);

    mel_leak_dump(
        (Mel_Leak_Report_Cb)(void(*)(const char*, const char*, u32, usize, void*))
            leak_counter,
        &user_val);

    mel_dealloc(alloc, leaked);
    MEL_PASS();
}

int main(void)
{
    MEL_TEST_BEGIN("Leak Detection Allocator Tests");

    MEL_RUN_TEST(leak_singleton);
    MEL_RUN_TEST(leak_alloc_dealloc_no_leak);
    MEL_RUN_TEST(leak_detect_single_leak);
    MEL_RUN_TEST(leak_detect_multiple_leaks);
    MEL_RUN_TEST(leak_realloc);
    MEL_RUN_TEST(leak_calloc_zeroed);
    MEL_RUN_TEST(leak_user_data_passthrough);

    MEL_TEST_END();
    return MEL_TEST_EXIT_CODE();
}
